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
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <sys/types.h>
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
	const char *prog;
	int pipe[2];
	enum {NONE, INP, SOCK} special;
	time_t wait;
}
worker;

int addworker(unsigned int *nworkers, worker **workers, worker new);
int worker_set(unsigned int nworkers, worker *workers, int *fdmax, fd_set *fds);
pid_t do_fork(const char *prog, const char *name, unsigned int *nworkers, worker **workers, int rfd);

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
	setuid(realuid);
	if(getuid()!=0)
		printf("horde: Privs safely dropped\n");
	freeaddrinfo(servinfo);
	if(listen(sockfd, 10)==-1)
	{
		perror("horde: listen");
		return(EXIT_FAILURE);
	}
	printf("horde: started ok, listening on port %hu\n", port);
	time_t /*upsince = time(NULL),*/ last_midnight=0;
	fd_set master, readfds;
	int fdmax;
	unsigned int nworkers=0;
	worker *workers=NULL;
	{
		worker inp=(worker){.pid=0, .prog=NULL, .pipe={STDIN_FILENO, STDOUT_FILENO}, .special=INP};
		if(addworker(&nworkers, &workers, inp))
			return(EXIT_FAILURE);
		worker sock=(worker){.pid=0, .prog=NULL, .pipe={sockfd, sockfd}, .special=SOCK};
		if(addworker(&nworkers, &workers, sock))
			return(EXIT_FAILURE);
	}
	if(worker_set(nworkers, workers, &fdmax, &master))
		return(EXIT_FAILURE);
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
							fprintf(stderr, "horde: cmd '%s'\n", input);
							free(input);
							init_char(&input, &inpl, &inpi);
						break;
						case SOCK:
							if(workers[w].wait<time(NULL))
							{
								pid_t pid=do_fork("./net", "horde: net", &nworkers, &workers, rfd);
								if(pid<1)
								{
									fprintf(stderr, "horde: request was not satisfied\n");
								}
								else
								{
									fprintf(stderr, "horde: passed on to new instance of net[%d]\n", pid);
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
						break;
					}
				}
			}
		}
	}
	fprintf(stderr, "horde: shutting down\n");
	close(sockfd);
	return(EXIT_SUCCESS);
}

int addworker(unsigned int *nworkers, worker **workers, worker new)
{
	if(!nworkers)
	{
		fprintf(stderr, "horde: addworker: nworkers==NULL\n");
		return(1);
	}
	if(!workers)
	{
		fprintf(stderr, "horde: addworker: workers==NULL\n");
		return(1);
	}
	unsigned int nw=(*nworkers)++;
	worker *new_w=realloc(*workers, *nworkers*sizeof(**workers));
	if(!new_w)
	{
		fprintf(stderr, "horde: addworker: new_w==NULL\n");
		perror("horde: realloc");
		return(1);
	}
	*workers=new_w;
	new.wait=time(NULL);
	new_w[nw]=new;
	return(0);
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

pid_t do_fork(const char *prog, const char *name, unsigned int *nworkers, worker **workers, int rfd)
{
	worker new=(worker){.prog=prog, .special=NONE};
	if(pipe(new.pipe)==-1)
	{
		perror("horde: pipe");
		return(-1);
	}
	switch((new.pid=fork()))
	{
		case -1: // error
			perror("horde: fork");
			close(new.pipe[0]);
			close(new.pipe[1]);
		break;
		case 0:; // chld
			dup2(new.pipe[0], STDIN_FILENO);
			dup2(new.pipe[1], STDOUT_FILENO);
			close(new.pipe[0]);
			close(new.pipe[1]);
			dup2(rfd, 3);
			execl(prog, name, NULL);
			// still here?  then it failed
			perror("horde: execl");
			exit(EXIT_FAILURE);
		break;
		default: // parent
			if(addworker(nworkers, workers, new))
			{
				kill(new.pid, SIGHUP);
				close(new.pipe[0]);
				close(new.pipe[1]);
				return(0);
			}
		break;
	}
	return(new.pid);
}
