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
#include <errno.h>

#include "bits.h"
#include "libhorde.h"

typedef struct
{
	pid_t pid;
	const char *prog, *name;
	int pipe[2];
	enum {NONE, INP, SOCK} special;
	bool accepting; // for 'net's and handling accept()s on SOCKs
	bool autoreplace;
	pid_t awaiting;
}
worker;

typedef struct
{
	const char *name, *prog;
	unsigned int n_init;
	hmsg *init;
	bool only; // only run one instance of this worker?
}
handler;

int addworker(unsigned int *nworkers, worker **workers, worker new);
void rmworker(unsigned int *nworkers, worker **workers, unsigned int w);
int worker_set(unsigned int nworkers, worker *workers, int *fdmax, fd_set *fds);
pid_t do_fork(const char *prog, const char *name, unsigned int *nworkers, worker **workers, int rfd, unsigned int *w);
signed int find_worker(unsigned int *nworkers, worker **workers, const char *name, bool creat, int *fdmax, fd_set *fds);

int add_handler(handler new);

unsigned int nhandlers;
handler *handlers;

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
		worker sock=(worker){.pid=0, .prog=NULL, .name="<socket>", .pipe={sockfd, sockfd}, .special=SOCK, .accepting=true, .autoreplace=true, .awaiting=0};
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
	nhandlers=0;
	handlers=NULL;
	{
		handler path=(handler){.name="path", .prog="./path", .n_init=0, .only=true};
		if(add_handler(path)<0)
		{
			fprintf(stderr, "horde: add_handler(\"path\") failed\n");
			return(EXIT_FAILURE);
		}
		handler proc=(handler){.name="proc", .prog="./proc", .n_init=1, .only=false};
		proc.init=malloc(proc.n_init*sizeof(hmsg));
		if(!proc.init)
		{
			fprintf(stderr, "horde: allocation failure (proc.init): malloc: %s\n", strerror(errno));
			return(EXIT_FAILURE);
		}
		proc.init[0]=new_hmsg("root", root);
		if(!proc.init[0])
		{
			fprintf(stderr, "horde: allocation failure (proc.init[0]): new_hmsg: %s\n", strerror(errno));
			return(EXIT_FAILURE);
		}
		if(add_handler(proc)<0)
		{
			fprintf(stderr, "horde: add_handler(\"proc\") failed\n");
			return(EXIT_FAILURE);
		}
		handler ext=(handler){.name="ext", .prog="./ext", .n_init=0, .only=true};
		if(add_handler(ext)<0)
		{
			fprintf(stderr, "horde: add_handler(\"ext\") failed\n");
			return(EXIT_FAILURE);
		}
	}
	signal(SIGPIPE, SIG_IGN);
	printf("horde: started ok, listening on port %hu\n", port);
	struct timeval timeout;
	char *input; unsigned int inpl, inpi;
	init_char(&input, &inpl, &inpi);
	time_t shuttime=0;
	int errupt=0;
	while(!errupt)
	{
		time_t now = time(NULL);
		if(shuttime)
		{
			if((shuttime<now)||(nworkers<=2))
				errupt++;
		}
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
									hmsg sh=new_hmsg("shutdown", NULL);
									unsigned int w;
									for(w=0;w<nworkers;w++)
									{
										workers[w].autoreplace=false;
										if(workers[w].pid>0)
											hsend(workers[w].pipe[1], sh);
									}
									shuttime=time(NULL)+8;
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
							if(workers[w].accepting)
							{
								pid_t pid=do_fork("./net", "net", &nworkers, &workers, rfd, NULL);
								if(pid<1)
								{
									fprintf(stderr, "horde: request was not satisfied\n");
								}
								else
								{
									fprintf(stderr, "horde: passed on to new instance of net[%d]\n", pid);
									workers[w].accepting=false; // now no more connections until we get an (accepted) - to prevent running several copies of net
									workers[w].awaiting=pid;
								}
								if(worker_set(nworkers, workers, &fdmax, &master))
								{
									fprintf(stderr, "horde: worker_set failed, bad things may happen\n");
								}
							}
						break;
						case NONE:;
							//fprintf(stderr, "horde: data from %s[%d] (fd=%u)\n", workers[w].name, workers[w].pid, rfd);
							char *buf=getl(workers[w].pipe[0]);
							if(*buf)
							{
								//fprintf(stderr, "horde: < '%s'\n", buf);
								hmsg h=hmsg_from_str(buf);
								if(h)
								{
									bool to=false;
									unsigned int i;
									for(i=0;i<h->nparms;i++)
									{
										if(strcmp(h->p_tag[i], "to")==0)
										{
											char *l=strchr(h->p_value[i], '[');
											if(!l) continue;
											char *r=strchr(l, ']');
											if(!r) continue;
											pid_t p;
											if(sscanf(l, "[%d", &p)!=1) continue;
											unsigned int who;
											for(who=0;who<nworkers;who++)
											{
												if(strncmp(h->p_value[i], workers[who].name, l-h->p_value[i])) continue;
												if(p==workers[who].pid)
												{
													fprintf(stderr, "horde: passing response on to %s[%u]\n", workers[who].name, workers[who].pid);
													if(workers[who].awaiting==workers[w].pid)
														workers[who].awaiting=0;
													hsend(workers[who].pipe[1], h);
													to=true;
													break;
												}
											}
											if(who==nworkers)
											{
												fprintf(stderr, "horde: couldn't find recipient %s\n", h->p_value[i]);
											}
										}
									}
									if(!to)
									{
										if(strcmp(h->funct, "fin")==0)
										{
											fprintf(stderr, "horde: %s[%u] finished with status %s\n", workers[w].name, workers[w].pid, h->data);
											rmworker(&nworkers, &workers, w);
											if(worker_set(nworkers, workers, &fdmax, &master))
											{
												fprintf(stderr, "horde: worker_set failed, bad things may happen\n");
											}
										}
										else if(strcmp(h->funct, "path")==0)
										{
											signed int wpath=find_worker(&nworkers, &workers, "path", true, &fdmax, &master);
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
												fprintf(stderr, "horde: passing message on to path[%u]\n", workers[wpath].pid);
												char *from=malloc(16+strlen(workers[w].name));
												sprintf(from, "%s[%u]", workers[w].name, workers[w].pid);
												add_htag(h, "from", from);
												hsend(workers[wpath].pipe[1], h);
												workers[w].awaiting=workers[wpath].pid;
											}
										}
										else if(strcmp(h->funct, "proc")==0)
										{
											signed int wproc=find_worker(&nworkers, &workers, "proc", true, &fdmax, &master);
											if(wproc<0)
											{
												fprintf(stderr, "horde: couldn't start \"proc\" worker\n");
												hmsg eh=new_hmsg("err", buf);
												add_htag(eh, "what", "worker-init");
												hsend(workers[w].pipe[1], eh);
												if(eh) free_hmsg(eh);
											}
											else
											{
												fprintf(stderr, "horde: passing message on to proc[%u]\n", workers[wproc].pid);
												char *from=malloc(16+strlen(workers[w].name));
												sprintf(from, "%s[%u]", workers[w].name, workers[w].pid);
												add_htag(h, "from", from);
												hsend(workers[wproc].pipe[1], h);
												workers[w].awaiting=workers[wproc].pid;
											}
										}
										else if(strcmp(h->funct, "ext")==0)
										{
											signed int wext=find_worker(&nworkers, &workers, "ext", true, &fdmax, &master);
											if(wext<0)
											{
												fprintf(stderr, "horde: couldn't find or start \"ext\" worker\n");
												hmsg eh=new_hmsg("err", buf);
												add_htag(eh, "what", "worker-init");
												hsend(workers[w].pipe[1], eh);
												if(eh) free_hmsg(eh);
											}
											else
											{
												fprintf(stderr, "horde: passing message on to ext[%u]\n", workers[wext].pid);
												char *from=malloc(16+strlen(workers[w].name));
												sprintf(from, "%s[%u]", workers[w].name, workers[w].pid);
												add_htag(h, "from", from);
												hsend(workers[wext].pipe[1], h);
												workers[w].awaiting=workers[wext].pid;
											}
										}
										else if(strcmp(h->funct, "accepted")==0)
										{
											unsigned int wsock;
											for(wsock=0;wsock<nworkers;wsock++)
											{
												if((workers[wsock].special==SOCK)&&(workers[wsock].awaiting==workers[w].pid)&&!workers[wsock].accepting)
												{
													workers[wsock].accepting=true;
													workers[wsock].awaiting=0;
												}
											}
										}
										else if(strcmp(h->funct, "err")==0)
										{
											fprintf(stderr, "horde: err without to, dropping (from %s[%u])\n", workers[w].name, workers[w].pid);
											for(i=0;i<h->nparms;i++)
											{
												fprintf(stderr, "horde:\t(%s|%s)\n", h->p_tag[i], h->p_value[i]);
												if(strcmp(h->p_tag[i], "errno")==0)
												{
													fprintf(stderr, "horde:\t\t%s\n", strerror(hgetlong(h->p_value[i])));
												}
												fprintf(stderr, "horde:\t%s\n", h->data);
											}
										}
										else
										{
											fprintf(stderr, "horde: unrecognised funct '%s'\n", h->funct);
											hmsg eh=new_hmsg("err", buf);
											if(eh)
											{
												add_htag(eh, "what", "unrecognised-funct");
												hsend(workers[w].pipe[1], eh);
												if(eh) free_hmsg(eh);
											}
										}
									}
									free_hmsg(h);
								}
								else
								{
									hmsg eh=new_hmsg("err", buf);
									add_htag(eh, "what", "parse-fail");
									hsend(workers[w].pipe[1], eh);
									if(eh) free_hmsg(eh);
									fprintf(stderr, "horde: couldn't understand the message\n");
									fprintf(stderr, "horde: \tfrom %s[%d] (fd=%u)\n", workers[w].name, workers[w].pid, rfd);
									fprintf(stderr, "horde: \tdata '%s'\n", buf);
								}
							}
							else
							{
								perror("horde: read");
								fprintf(stderr, "horde: worker %s[%d] died unexpectedly\n", workers[w].name, workers[w].pid);
								rmworker(&nworkers, &workers, w);
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
	close(sockfd);
	fprintf(stderr, "horde: shut down\n");
	unsigned int w;
	for(w=0;w<nworkers;w++)
	{
		if(workers[w].pid>0)
			kill(workers[w].pid, SIGHUP);
	}
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
		*nworkers=nw;
		perror("horde: realloc");
		return(-1);
	}
	*workers=new_w;
	new.awaiting=0;
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
	bool autoreplace=(*workers)[w].autoreplace;
	const char *prog=(*workers)[w].prog, *name=(*workers)[w].name;
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
			hmsg dead=new_hmsg("err", NULL);
			add_htag(dead, "what", "worker-died");
			add_htag(dead, "worker-name", name);
			fprintf(stderr, "horde: reporting death to %s[%d]\n", (*workers)[w].name, (*workers)[w].pid);
			hsend((*workers)[w].pipe[1], dead);
			if(dead) free(dead);
		}
	}
	worker *new_w=realloc(*workers, *nworkers*sizeof(**workers));
	if(new_w)
		*workers=new_w;
	if(autoreplace)
	{
		unsigned int w;
		pid_t p=do_fork(prog, name, nworkers, workers, 0, &w);
		if(p<1)
		{
			fprintf(stderr, "horde: rmworker: autoreplace failed\n");
		}
		else
		{
			fprintf(stderr, "horde: rmworker: automatically replaced worker %s[%u->%u]\n", name, was, p);
			(*workers)[w].autoreplace=true;
		}
	}
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
			if(rfd&&dup2(rfd, 3)==-1)
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
			if(w) *w=ww;
		break;
	}
	return(new.pid);
}

signed int find_worker(unsigned int *nworkers, worker **workers, const char *name, bool creat, int *fdmax, fd_set *fds)
{
	unsigned int w;
	for(w=0;w<*nworkers;w++)
	{
		if(!(*workers)[w].autoreplace) continue;
		if(strcmp((*workers)[w].name, name)==0)
			return(w);
	}
	if(creat)
	{
		unsigned int h;
		for(h=0;h<nhandlers;h++)
		{
			if(strcmp(handlers[h].name, name)==0)
			{
				pid_t p=do_fork(handlers[h].prog, handlers[h].name, nworkers, workers, 0, &w);
				if(p>0)
				{
					fprintf(stderr, "horde: started new instance of %s[%u]\n", name, p);
					(*workers)[w].autoreplace=handlers[h].only;
					if(worker_set(*nworkers, *workers, fdmax, fds))
					{
						fprintf(stderr, "horde: worker_set failed, bad things may happen\n");
					}
					unsigned int i;
					for(i=0;i<handlers[h].n_init;i++)
					{
						hsend((*workers)[w].pipe[1], handlers[h].init[i]);
					}
					return(w);
				}
			}
		}
	}
	return(-1);
}

int add_handler(handler new)
{
	unsigned int nh=nhandlers++;
	handler *new_h=realloc(handlers, nhandlers*sizeof(*handlers));
	if(!new_h)
	{
		fprintf(stderr, "horde: add_handler: new_h==NULL\n");
		nhandlers=nh;
		perror("horde: realloc");
		return(-1);
	}
	handlers=new_h;
	new_h[nh]=new;
	return(nh);
}
