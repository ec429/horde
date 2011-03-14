#define _GNU_SOURCE
/*
	horde: modular http server
	Copyright (C) 2011 Edward Cree

	This program is free software: you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation, either version 3 of the License, or
	(at your option) any later version.

	This program is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
	GNU General Public License for more details.

	You should have received a copy of the GNU General Public License
	along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/socket.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <signal.h>

#include "bits.h"
#include "libhorde.h"

typedef struct
{
	pid_t pid;
	const char *prog, *name;
	int pipe[2];
	enum {NONE, INP, SOCK} special;
	time_t wait;
	bool autoreplace;
	pid_t awaiting;
	char *await_data;
}
worker;

int addworker(unsigned int *nworkers, worker **workers, worker new);
void rmworker(unsigned int *nworkers, worker **workers, unsigned int w);
int worker_set(unsigned int nworkers, worker *workers, int *fdmax, fd_set *fds);
pid_t do_fork(const char *prog, const char *name, unsigned int *nworkers, worker **workers, int rfd, unsigned int *w);
signed int find_worker(unsigned int *nworkers, worker **workers, const char *name, bool creat);

int main(int argc, char **argv)
{
	unsigned short port=8000; // incoming port number
	const char *root="root"; // virtual root
	uid_t realuid=65534; // less-privileged uid, default 'nobody'
	size_t maxbytesdaily=1<<29, bytestoday=0; // daily bandwidth limiter, default 0.5GB
	int arg;
	for(arg=1;arg<argc;arg++)
	{
		const char *varg=argv[arg];
		if(strncmp(varg, "--port=", 7)==0)
		{
			sscanf(varg+7, "%hu", &port);
		}
		else if(strncmp(varg, "--root=", 7)==0)
		{
			root=varg+7;
		}
		else if(strncmp(varg, "--uid=", 6)==0)
		{
			sscanf(varg+6, "%u", (unsigned int*)&realuid);
		}
		else if(strncmp(varg, "--mbd=", 6)==0)
		{
			sscanf(varg+6, "%zu", &maxbytesdaily);
		}
		else
		{
			fprintf(stderr, "horde: unrecognised argument %s (ignoring)\n", varg);
		}
	}
	char portno[6];
	sprintf(portno, "%hu", port);
	int sockfd;
	struct addrinfo hints, *servinfo, *p;
	int rv;
	const int ctrue=1;
	memset(&hints, 0, sizeof hints);
	hints.ai_family = AF_UNSPEC; // expecting IPv4 or IPv6
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_flags = AI_PASSIVE; // use my IP
	if((rv=getaddrinfo(NULL, portno, &hints, &servinfo)))
	{
		fprintf(stderr, "horde: getaddrinfo: %s\n", gai_strerror(rv));
		return(EXIT_FAILURE);
	}
	// loop through all the results and bind to the first we can
	for(p=servinfo; p!=NULL; p=p->ai_next)
	{
		if((sockfd=socket(p->ai_family, p->ai_socktype, p->ai_protocol))==-1)
		{
			perror("horde: socket");
			continue;
		}
		if(setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &ctrue, sizeof(ctrue))==-1)
		{
			perror("horde: setsockopt");
			return(EXIT_FAILURE);
		}
		if(bind(sockfd, p->ai_addr, p->ai_addrlen)==-1)
		{
			close(sockfd);
			perror("horde: bind");
			continue;
		}
		setuid(realuid);
		break;
	}
	if(p==NULL)
	{
		fprintf(stderr, "horde: failed to bind\n");
		return(EXIT_FAILURE);
	}
	if(setuid(realuid)==-1)
		perror("horde: setuid");
	if(getuid()!=0)
		printf("horde: privs safely dropped\n");
	freeaddrinfo(servinfo);
	if(listen(sockfd, 10)==-1)
	{
		perror("horde: listen");
		return(EXIT_FAILURE);
	}
	time_t /*upsince = time(NULL),*/ last_midnight=0;
	fd_set master, readfds;
	int fdmax;
	unsigned int nworkers=0;
	worker *workers=NULL;
	{
		worker inp=(worker){.pid=0, .prog=NULL, .name="<stdin>", .pipe={STDIN_FILENO, STDOUT_FILENO}, .special=INP, .autoreplace=false, .awaiting=0};
		if(addworker(&nworkers, &workers, inp)<0)
		{
			fprintf(stderr, "horde: addworker failed on INP\n");
			return(EXIT_FAILURE);
		}
		worker sock=(worker){.pid=0, .prog=NULL, .name="<socket>", .pipe={sockfd, sockfd}, .special=SOCK, .autoreplace=true, .awaiting=0};
		if(addworker(&nworkers, &workers, sock)<0)
		{
			fprintf(stderr, "horde: addworker failed on SOCK\n");
			return(EXIT_FAILURE);
		}
	}
	if(worker_set(nworkers, workers, &fdmax, &master))
	{
		fprintf(stderr, "horde: worker_set failed\n");
		return(EXIT_FAILURE);
	}
	printf("horde: started ok, listening on port %hu\n", port);
	struct timeval timeout;
	char *input; unsigned int inpl, inpi;
	init_char(&input, &inpl, &inpi);
	int errupt=0;
	while(!errupt)
	{
		time_t now = time(NULL);
		if((now/86400)>(last_midnight/86400)) // reset b_s_m counter at midnight
		{
			last_midnight=now;
			bytestoday=0;
		}
		timeout.tv_sec=0;
		timeout.tv_usec=250000;
		readfds=master;
		if(select(fdmax+1, &readfds, NULL, NULL, &timeout)==-1)
		{
			perror("horde: select");
			fprintf(stderr, "horde: resetting fd_set master\n");
			if(worker_set(nworkers, workers, &fdmax, &master))
			{
				fprintf(stderr, "horde: worker_set failed, bad things may happen\n");
			}
		}
		else
		{
			int rfd;
			for(rfd=0;rfd<=fdmax;rfd++)
			{
				if(FD_ISSET(rfd, &readfds))
				{
					unsigned int w;
					for(w=0;w<nworkers;w++)
					{
						if(workers[w].pipe[0]==rfd) break;
					}
					if(w==nworkers)
					{
						FD_CLR(rfd, &master);
						fprintf(stderr, "horde: data on unrecognised fd %u (ignoring)\n", rfd);
						continue;
					}
					switch(workers[w].special)
					{
						case INP:;
							signed int c;
							while((c=getchar()))
							{
								if(c==EOF)
								{
									free(input);
									input=strdup("(shutdown)");
									break;
								}
								else if(c!='\n')
								{
									append_char(&input, &inpl, &inpi, c);
								}
								else
									break;
							}
							hmsg ih=hmsg_from_str(input);
							if(ih)
							{
								if(strcmp(ih->funct, "shutdown")==0)
								{
									// TODO: check parameters
									fprintf(stderr, "horde: shutting down (requested on stdin)\n");
									errupt++;
								}
								else
								{
									fprintf(stderr, "horde: unrecognised cmd '%s'\n", ih->funct);
								}
								free_hmsg(ih);
							}
							else
							{
								fprintf(stderr, "horde: failed to parse input '%s'\n", input);
							}
							free(input);
							init_char(&input, &inpl, &inpi);
						break;
						case SOCK:
							if(workers[w].wait<time(NULL))
							{
								unsigned int ww;
								pid_t pid=do_fork("./net", "net", &nworkers, &workers, rfd, &ww);
								if(pid<1)
								{
									fprintf(stderr, "horde: request was not satisfied\n");
								}
								else
								{
									fprintf(stderr, "horde: passed on to new instance of net[%d], is #%u\n", pid, ww);
									workers[w].wait=time(NULL); // now no more connections for 1 second (to prevent running several copies of net)
								}
								if(worker_set(nworkers, workers, &fdmax, &master))
								{
									fprintf(stderr, "horde: worker_set failed, bad things may happen\n");
								}
							}
						break;
						case NONE:
							fprintf(stderr, "horde: data from worker #%u, %s[%d] (fd=%u)\n", w, workers[w].prog, workers[w].pid, rfd);
							char *buf=getl(workers[w].pipe[0]);
							if(*buf)
							{
								fprintf(stderr, "horde: < '%s'\n", buf);
								hmsg h=hmsg_from_str(buf);
								if(h)
								{
									if(strcmp(h->funct, "fin")==0)
									{
										rmworker(&nworkers, &workers, w);
										fprintf(stderr, "horde: worker #%u finished, with status %s\n", w, h->data);
										if(worker_set(nworkers, workers, &fdmax, &master))
										{
											fprintf(stderr, "horde: worker_set failed, bad things may happen\n");
										}
									}
									else if(strcmp(h->funct, "path")==0)
									{
										signed int wpath=find_worker(&nworkers, &workers, "path", true);
										if(wpath<0)
										{
											fprintf(stderr, "horde: couldn't find or start \"path\" worker\n");
											hmsg eh=new_hmsg("err", buf);
											add_htag(eh, "what", "worker-init");
											hsend(workers[w].pipe[1], eh);
											if(eh) free_hmsg(eh);
										}
										else
										{
											fprintf(stderr, "horde: passing message on to #%u, path[%u]\n", wpath, workers[wpath].pid);
											char from[32];
											sprintf(from, "net[%u]", workers[w].pid);
											add_htag(h, "from", from);
											hsend(workers[wpath].pipe[1], h);
										}
									}
									else
									{
										hmsg eh=new_hmsg("err", buf);
										add_htag(eh, "what", "unrecognised-funct");
										fprintf(stderr, "horde: unrecognised funct '%s'\n", h->funct);
										hsend(workers[w].pipe[1], eh);
										if(eh) free_hmsg(eh);
									}
									free_hmsg(h);
								}
								else
								{
									fprintf(stderr, "horde: couldn't understand the message\n");
									//fprintf(stderr, "horde: \tfrom worker #%u, %s[%d] (fd=%u)\n", w, workers[w].prog, workers[w].pid, rfd);
									//fprintf(stderr, "horde: \tdata '%s'\n", buf);
								}
							}
							else
							{
								perror("horde: read");
								rmworker(&nworkers, &workers, w);
								fprintf(stderr, "horde: worker #%u died unexpectedly\n", w);
								if(worker_set(nworkers, workers, &fdmax, &master))
								{
									fprintf(stderr, "horde: worker_set failed, bad things may happen\n");
								}
							}
							if(buf) free(buf);
						break;
					}
				}
			}
		}
	}
	fprintf(stderr, "horde: shut down\n");
	close(sockfd);
	return(EXIT_SUCCESS);
}

int addworker(unsigned int *nworkers, worker **workers, worker new)
{
	if(!nworkers)
	{
		fprintf(stderr, "horde: addworker: nworkers==NULL\n");
		return(-1);
	}
	if(!workers)
	{
		fprintf(stderr, "horde: addworker: workers==NULL\n");
		return(-1);
	}
	unsigned int nw=(*nworkers)++;
	worker *new_w=realloc(*workers, *nworkers*sizeof(**workers));
	if(!new_w)
	{
		fprintf(stderr, "horde: addworker: new_w==NULL\n");
		perror("horde: realloc");
		return(-1);
	}
	*workers=new_w;
	new.wait=time(NULL);
	new_w[nw]=new;
	return(nw);
}

void rmworker(unsigned int *nworkers, worker **workers, unsigned int w)
{
	if(w>=*nworkers) return;
	if((*workers)[w].pid) waitpid((*workers)[w].pid, NULL, WNOHANG);
	if((*workers)[w].pipe)
	{
		if((*workers)[w].pipe[0]) close((*workers)[w].pipe[0]);
		if((*workers)[w].pipe[1]) close((*workers)[w].pipe[1]);
	}
	pid_t was=(*workers)[w].pid;
	while(w<*nworkers)
	{
		(*workers)[w]=(*workers)[w+1];
		w++;
	}
	(*nworkers)--;
	for(w=0;w<*nworkers;w++)
	{
		if((*workers)[w].awaiting==was)
		{
			hmsg dead=new_hmsg("err", (*workers)[w].await_data);
			add_htag(dead, "what", "worker-died");
			fprintf(stderr, "horde: reporting death to #%u\n", w);
			char *str=str_from_hmsg(dead);
			if(str)
			{
				write((*workers)[w].pipe[1], str, strlen(str));
				free(str);
			}
			if(dead) free(dead);
		}
	}
	worker *new_w=realloc(*workers, *nworkers*sizeof(**workers));
	if(new_w)
		*workers=new_w;
	return;
}

int worker_set(unsigned int nworkers, worker *workers, int *fdmax, fd_set *fds)
{
	FD_ZERO(fds);
	*fdmax=0;
	if(!nworkers)
		return(0);
	if(!workers)
	{
		fprintf(stderr, "horde: worker_set: workers==NULL and nworkers not 0\n");
		return(1);
	}
	unsigned int w;
	for(w=0;w<nworkers;w++)
	{
		*fdmax=max(*fdmax, workers[w].pipe[0]);
		FD_SET(workers[w].pipe[0], fds);
	}
	return(0);
}

pid_t do_fork(const char *prog, const char *name, unsigned int *nworkers, worker **workers, int rfd, unsigned int *w)
{
	worker new=(worker){.prog=prog, .name=name, .special=NONE, .autoreplace=false, .awaiting=0};
	int s[2][2];
	if(pipe(s[0])==-1)
	{
		perror("horde: pipe");
		return(-1);
	}
	if(pipe(s[1])==-1)
	{
		perror("horde: pipe");
		close(s[0][0]);
		close(s[0][1]);
		return(-1);
	}
	switch((new.pid=fork()))
	{
		case -1: // error
			perror("horde: fork");
			close(s[0][0]);
			close(s[0][1]);
			close(s[1][0]);
			close(s[1][1]);
		break;
		case 0:; // chld
			if(dup2(s[0][0], STDIN_FILENO)==-1)
				perror("horde: chld: dup2(0)");
			if(dup2(s[1][1], STDOUT_FILENO)==-1)
				perror("horde: chld: dup2(1)");
			close(s[0][1]);
			close(s[1][0]);
			if(dup2(rfd, 3)==-1)
				perror("horde: chld: dup2(3)");
			execl(prog, name, NULL);
			// still here?  then it failed
			perror("horde: execl");
			hfin(EXIT_FAILURE);
			exit(EXIT_FAILURE);
		break;
		default: // parent
			new.pipe[0]=s[1][0];
			new.pipe[1]=s[0][1];
			close(s[0][0]);
			close(s[1][1]);
			signed int ww;
			if((ww=addworker(nworkers, workers, new))<0)
			{
				fprintf(stderr, "horde: failed to add worker '%s'\n", name);
				kill(new.pid, SIGHUP);
				close(new.pipe[0]);
				close(new.pipe[1]);
				return(0);
			}
			*w=ww;
		break;
	}
	return(new.pid);
}

signed int find_worker(unsigned int *nworkers, worker **workers, const char *name, bool creat)
{
	unsigned int w;
	for(w=0;w<*nworkers;w++)
	{
		if(strcmp((*workers)[w].name, name)==0)
			return(w);
	}
	if(creat)
	{
		// need the list of known worker types
	}
	return(-1);
}
