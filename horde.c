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
#include <sys/time.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <sys/socket.h>
#include <dirent.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <signal.h>
#include <errno.h>

#include "bits.h"
#include "libhorde.h"

#define CWD_BUF_SIZE	4096

typedef struct
{
	pid_t pid;
	const char *prog, *name;
	int pipe[2];
	enum {NONE, INP, SOCK} special;
	bool accepting; // for 'net's and handling accept()s on SOCKs; (ready) status on all others
	bool autoreplace;
	pid_t blocks; // who is blocking on us?
	unsigned long t_micro;
	unsigned int n_rqs;
	struct timeval current;
}
worker;

typedef struct
{
	char *name, *prog;
	unsigned int n_init;
	hmsg *init;
	bool only; // only run one instance of this worker?
}
handler;

int addworker(worker new);
void rmworker(unsigned int w);
int worker_set(int *fdmax, fd_set *fds);
pid_t do_fork(const char *prog, const char *name, int rfd, unsigned int *w);
signed int find_worker(const char *name, bool creat, int *fdmax, fd_set *fds);
int add_handler(handler new);

int rcreaddir(const char *dn);
int rcread(const char *fn);

int handle(const char *inp, const char *file);
int handle_add(const hmsg h);
int handle_workers(hmsg h);

void uptime_respond(unsigned int w, hmsg h, time_t upsince);
void stats_respond(unsigned int w, hmsg h);
void statsup(hmsg h);

unsigned int nworkers;
worker *workers;
unsigned int nhandlers;
handler *handlers;

bool debug, pipeline, transcript;
const char *root, *host;
size_t maxbytesdaily, bytes_today;
unsigned long net_micro, net_rqs;

int main(int argc, char **argv)
{
	unsigned short port=8000; // incoming port number
	char cwdbuf[CWD_BUF_SIZE];
	const char *confdir=getcwd(cwdbuf, CWD_BUF_SIZE); // location of config files
	if(!confdir)
	{
		perror("horde: getcwd");
		return(EXIT_FAILURE);
	}
	char hostbuf[HOST_NAME_MAX+1];
	if(gethostname(hostbuf, HOST_NAME_MAX))
		host=NULL;
	else
		host=hostbuf;
	root="root"; // virtual root
	uid_t realuid=65534; // less-privileged uid, default 'nobody'
	gid_t realgid=65534; // less-privileged gid, default 'nogroup'
	maxbytesdaily=1<<29; // daily bandwidth limiter, default 0.5GB
	bytes_today=0;
	debug=false; // write debugging info to stderr?
	transcript=false; // write extra info?
	pipeline=true; // run daemons in pipelined mode? (generally preferred for efficiency reasons; however debugging may be easier in single-invoke mode)
	int arg;
	for(arg=1;arg<argc;arg++)
	{
		const char *varg=argv[arg];
		if(strncmp(varg, "--port=", 7)==0)
			sscanf(varg+7, "%hu", &port);
		else if(strncmp(varg, "--conf=", 7)==0)
			confdir=varg+7;
		else if(strncmp(varg, "--host=", 7)==0)
			host=varg+7;
		else if(strncmp(varg, "--root=", 7)==0)
			root=varg+7;
		else if(strncmp(varg, "--uid=", 6)==0)
			sscanf(varg+6, "%u", (unsigned int*)&realuid);
		else if(strncmp(varg, "--gid=", 6)==0)
			sscanf(varg+6, "%u", (unsigned int*)&realgid);
		else if(strncmp(varg, "--mbd=", 6)==0)
			sscanf(varg+6, "%zu", &maxbytesdaily);
		else if(strcmp(varg, "--debug")==0)
			debug=true;
		else if(strcmp(varg, "--no-debug")==0)
			debug=false;
		else if(strcmp(varg, "--transcript")==0)
			transcript=true;
		else if(strcmp(varg, "--no-transcript")==0)
			transcript=false;
		else if(strcmp(varg, "--pipe")==0)
			pipeline=true;
		else if(strcmp(varg, "--no-pipe")==0)
			pipeline=false;
		else
			fprintf(stderr, "horde: unrecognised argument %s (ignoring)\n", varg);
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
		break;
	}
	if(p==NULL)
	{
		fprintf(stderr, "horde: failed to bind\n");
		return(EXIT_FAILURE);
	}
	if(setgid(realgid)==-1)
		perror("horde: setgid");
	if(setuid(realuid)==-1)
		perror("horde: setuid");
	if((getuid()==realuid)&&(getgid()==realgid))
		if(debug) printf("horde: privs safely dropped\n");
	freeaddrinfo(servinfo);
	if(listen(sockfd, 10)==-1)
	{
		perror("horde: listen");
		return(EXIT_FAILURE);
	}
	time_t upsince = time(NULL), last_midnight=0;
	fd_set master, readfds;
	int fdmax;
	nworkers=0;
	workers=NULL;
	{
		worker inp=(worker){.pid=0, .prog=NULL, .name="<stdin>", .pipe={STDIN_FILENO, STDOUT_FILENO}, .special=INP, .autoreplace=false};
		if(addworker(inp)<0)
		{
			fprintf(stderr, "horde: addworker failed on INP\n");
			return(EXIT_FAILURE);
		}
		worker sock=(worker){.pid=0, .prog=NULL, .name="<socket>", .pipe={sockfd, sockfd}, .special=SOCK, .accepting=true, .autoreplace=true};
		if(addworker(sock)<0)
		{
			fprintf(stderr, "horde: addworker failed on SOCK\n");
			return(EXIT_FAILURE);
		}
	}
	if(worker_set(&fdmax, &master))
	{
		fprintf(stderr, "horde: worker_set failed\n");
		return(EXIT_FAILURE);
	}
	nhandlers=0;
	handlers=NULL;
	if(chdir(confdir))
	{
		perror("horde: chdir");
		return(EXIT_FAILURE);
	}
	if(rcread("horde.rc"))
	{
		fprintf(stderr, "horde: bad rc, giving up\n");
		return(EXIT_FAILURE);
	}
	if(rcreaddir("."))
	{
		fprintf(stderr, "horde: bad rc, giving up\n");
		return(EXIT_FAILURE);
	}
	if(chdir(cwdbuf))
	{
		perror("horde: chdir");
		return(EXIT_FAILURE);
	}
	fprintf(stderr, "horde: setting signal handlers\n");
	if(signal(SIGPIPE, SIG_IGN)==SIG_ERR)
		perror("horde: failed to set SIGPIPE handler: signal");
	if(signal(SIGCHLD, SIG_IGN)==SIG_ERR)
		perror("horde: failed to set SIGCHLD handler: signal");
	if(debug) printf("horde: started ok, listening on port %hu\n", port);
	struct timeval timeout;
	char *input; unsigned int inpl, inpi;
	init_char(&input, &inpl, &inpi);
	time_t shuttime=0;
	net_micro=0;
	net_rqs=0;
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
			bytes_today=0;
		}
		timeout.tv_sec=0;
		timeout.tv_usec=250000;
		readfds=master;
		if(select(fdmax+1, &readfds, NULL, NULL, &timeout)==-1)
		{
			perror("horde: select");
			if(debug) fprintf(stderr, "horde: resetting fd_set master\n");
			if(worker_set(&fdmax, &master))
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
						if(debug) fprintf(stderr, "horde: data on unrecognised fd %u (ignoring)\n", rfd);
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
							if(handle(input, NULL)==2)
							{
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
							free(input);
							init_char(&input, &inpl, &inpi);
						break;
						case SOCK:
							if(workers[w].accepting)
							{
								pid_t pid=do_fork("./net", "net", rfd, NULL);
								if(pid<1)
								{
									if(debug) fprintf(stderr, "horde: request was not satisfied\n");
								}
								else
								{
									if(debug) fprintf(stderr, "horde: passed on to new instance of net[%d]\n", pid);
									workers[w].accepting=false; // now no more connections until we get an (accepted) - to prevent running several copies of net
									gettimeofday(&workers[w].current, NULL);
								}
								if(worker_set(&fdmax, &master))
								{
									fprintf(stderr, "horde: worker_set failed, bad things may happen\n");
								}
							}
						break;
						case NONE:;
							if(transcript) fprintf(stderr, "horde: data from %s[%d] (fd=%u)\n", workers[w].name, workers[w].pid, rfd);
							char *buf=getl(workers[w].pipe[0]);
							if(buf&&*buf)
							{
								if(transcript) fprintf(stderr, "horde: < '%s'\n", buf);
								hmsg h=hmsg_from_str(buf, false);
								if(h)
								{
									bool to=false;
									unsigned int i;
									for(i=0;i<h->nparms;i++)
									{
										if(strcmp(h->p_tag[i], "to")==0)
										{
											to=true; // the module, at least, believes its work to be done
											char *l=strchr(h->p_value[i], '[');
											if(!l) continue;
											char *r=strchr(l, ']');
											if(!r) continue;
											pid_t p;
											if(sscanf(l, "[%d", &p)!=1) continue;
											unsigned int who;
											for(who=0;who<nworkers;who++)
											{
												if(p!=workers[who].pid) continue;
												if(strncmp(h->p_value[i], workers[who].name, l-h->p_value[i])) continue;
												if(debug) fprintf(stderr, "horde: passing response on to %s[%u]\n", workers[who].name, workers[who].pid);
												if(p==workers[w].blocks) workers[w].blocks=0;
												hsend(workers[who].pipe[1], h);
												break;
											}
											if(who==nworkers) // message dropped on the floor
												fprintf(stderr, "horde: couldn't find recipient %s\n", h->p_value[i]);
										}
									}
									if(to)
									{
										struct timeval now;
										gettimeofday(&now, NULL);
										workers[w].n_rqs++;
										workers[w].t_micro+=(now.tv_sec-workers[w].current.tv_sec)*1000000+now.tv_usec-workers[w].current.tv_usec;
									}
									else
									{
										if(strcmp(h->funct, "fin")==0)
										{
											if(debug) fprintf(stderr, "horde: %s[%u] finished with status %s\n", workers[w].name, workers[w].pid, h->data);
											if(strcmp(workers[w].name, "net")==0)
											{
												struct timeval now;
												gettimeofday(&now, NULL);
												net_rqs++;
												net_micro+=(now.tv_sec-workers[w].current.tv_sec)*1000000+now.tv_usec-workers[w].current.tv_usec;
											}
											rmworker(w);
											if(worker_set(&fdmax, &master))
											{
												fprintf(stderr, "horde: worker_set failed, bad things may happen\n");
											}
										}
										else if(strcmp(h->funct, "ready")==0)
										{
											if(debug) fprintf(stderr, "horde: %s[%u] ready\n", workers[w].name, workers[w].pid);
											workers[w].accepting=true;
										}
										else if(strcmp(h->funct, "accepted")==0)
										{
											unsigned int wsock;
											for(wsock=0;wsock<nworkers;wsock++)
											{
												if(workers[wsock].special==SOCK)
													workers[wsock].accepting=true;
											}
										}
										else if(strcmp(h->funct, "uptime")==0)
											uptime_respond(w, h, upsince);
										else if(strcmp(h->funct, "stats")==0)
											stats_respond(w, h);
										else if(strcmp(h->funct, "statsup")==0)
											statsup(h);
										else if(strcmp(h->funct, "err")==0)
										{
											fprintf(stderr, "horde: err without to, dropping (from %s[%u])\n", workers[w].name, workers[w].pid);
											for(i=0;i<h->nparms;i++)
											{
												fprintf(stderr, "horde:\t(%s|%s)\n", h->p_tag[i], h->p_value[i]);
												if(strcmp(h->p_tag[i], "errno")==0)
													fprintf(stderr, "horde:\t\t%s\n", strerror(hgetlong(h->p_value[i])));
												fprintf(stderr, "horde:\t%s\n", h->data);
											}
										}
										else
										{
											signed int wproc=find_worker(h->funct, true, &fdmax, &master);
											if(wproc<0)
											{
												if(debug) fprintf(stderr, "horde: unrecognised funct '%s'\n", h->funct);
												hmsg eh=new_hmsg("err", buf);
												if(eh)
												{
													add_htag(eh, "what", "unrecognised-funct");
													hsend(workers[w].pipe[1], eh);
													if(eh) free_hmsg(eh);
												}
											}
											else
											{
												if(debug) fprintf(stderr, "horde: passing message on to %s[%u]\n", workers[wproc].name, workers[wproc].pid);
												char *from=malloc(16+strlen(workers[w].name));
												sprintf(from, "%s[%u]", workers[w].name, workers[w].pid);
												add_htag(h, "from", from);
												hsend(workers[wproc].pipe[1], h);
												if(!workers[wproc].autoreplace) // ie. !only
													workers[wproc].accepting=false;
												workers[wproc].blocks=workers[w].pid;
												gettimeofday(&workers[wproc].current, NULL);
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
									if(debug)
									{
										fprintf(stderr, "horde: couldn't understand the message\n");
										fprintf(stderr, "horde: \tfrom %s[%d] (fd=%u)\n", workers[w].name, workers[w].pid, rfd);
										fprintf(stderr, "horde: \tdata '%s'\n", buf);
									}
								}
							}
							else
							{
								perror("horde: read");
								fprintf(stderr, "horde: worker %s[%d] died unexpectedly\n", workers[w].name, workers[w].pid);
								rmworker(w);
								if(worker_set(&fdmax, &master))
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

int rcreaddir(const char *dn)
{
	char olddir[CWD_BUF_SIZE];
	if(!getcwd(olddir, CWD_BUF_SIZE))
	{
		perror("horde: getcwd");
		return(1);
	}
	if(debug) fprintf(stderr, "horde: searching %s/%s for config files\n", olddir, dn);
	if(chdir(dn))
	{
		if(debug) fprintf(stderr, "horde: failed to chdir(%s): %s\n", dn, strerror(errno));
		return(1);
	}
	DIR *rcdir=opendir(".");
	if(!rcdir)
	{
		fprintf(stderr, "horde: failed to opendir %s: %s\n", dn, strerror(errno));
		closedir(rcdir);
		chdir(olddir);
		return(1);
	}
	struct dirent *entry;
	while((entry=readdir(rcdir)))
	{
		if(entry->d_name[0]=='.') continue;
		struct stat st;
		if(stat(entry->d_name, &st))
		{
			if(debug) fprintf(stderr, "horde: failed to stat %s: %s\n", entry->d_name, strerror(errno));
			closedir(rcdir);
			chdir(olddir);
			return(1);
		}
		if(st.st_mode&S_IFDIR)
		{
			if(rcreaddir(entry->d_name))
			{
				chdir(olddir);
				return(1);
			}
		}
		else if(strcmp(entry->d_name+strlen(entry->d_name)-6, ".horde")==0)
		{
			if(rcread(entry->d_name))
			{
				closedir(rcdir);
				chdir(olddir);
				return(1);
			}
		}
	}
	if(debug) fprintf(stderr, "horde: done searching %s\n", dn);
	closedir(rcdir);
	chdir(olddir);
	return(0);
}

int rcread(const char *fn)
{
	FILE *rc=fopen(fn, "r");
	if(rc)
	{
		if(debug) fprintf(stderr, "horde: reading rc file '%s'\n", fn);
		char *line;
		int e=0;
		while((line=fgetl(rc))&&!e)
		{
			if(!*line)
			{
				free(line);
				if(feof(rc)) break;
				continue;
			}
			size_t end;
			while(line[(end=strlen(line))-1]=='\\')
			{
				line[end-1]=0;
				char *cont=fgetl(rc);
				if(!cont) break;
				char *newl=realloc(line, strlen(line)+strlen(cont)+1);
				if(!newl)
				{
					free(cont);
					break;
				}
				strcat(newl, cont);
				free(cont);
				line=newl;
			}
			e=handle(line, fn);
			free(line);
		}
		fclose(rc);
		if(e)
		{
			if(debug) fprintf(stderr, "horde: choked on rc file %s\n", fn);
			return(e);
		}
		if(debug) fprintf(stderr, "horde: finished reading rc file\n");
		return(0);
	}
	fprintf(stderr, "horde: failed to open rc file '%s': fopen: %s\n", fn, strerror(errno));
	return(1);
}

int handle(const char *inp, const char *file)
{
	int e=0;
	hmsg h=hmsg_from_str(inp, true);
	if(h)
	{
		if(strcmp(h->funct, "add")==0)
			e=handle_add(h);
		else if(strcmp(h->funct, "workers")==0)
		{
			e=handle_workers(h);
		}
		else if(strcmp(h->funct, "shutdown")==0)
		{
			if(debug) fprintf(stderr, "horde: server is shutting down\n");
			e=2;
		}
		else if(strcmp(h->funct, "kill")==0)
		{
			int pid;
			if(sscanf(h->data, "%d", &pid)==1)
			{
				bool found=false;
				hmsg k=new_hmsg("kill", NULL);
				for(unsigned int w=0;w<nworkers;w++)
				{
					if(workers[w].pid==pid)
					{
						hsend(workers[w].pipe[1], k);
						found=true;
						fprintf(stderr, "horde: killed %s[%u]\n", workers[w].name, workers[w].pid);
					}
				}
				if(!found)
					fprintf(stderr, "horde: kill: not found: %s\n", h->data);
				free_hmsg(k);
			}
			else
				fprintf(stderr, "horde: usage: kill <pid>\n");
		}
		else if(strcmp(h->funct, "killall")==0)
		{
			if(debug) fprintf(stderr, "horde: killing all %s\n", h->data);
			bool found=false;
			hmsg k=new_hmsg("kill", NULL);
			for(unsigned int w=0;w<nworkers;w++)
			{
				if(strcmp(workers[w].name, h->data)==0)
				{
					hsend(workers[w].pipe[1], k);
					fprintf(stderr, "horde: killed %s[%u]\n", workers[w].name, workers[w].pid);
					found=true;
				}
			}
			free_hmsg(k);
			if(!found)
			{
				fprintf(stderr, "horde: killall: not found: %s\n", h->data);
				e=3;
			}
		}
		else if(strcmp(h->funct, "debug")==0)
		{
			if(h->data)
			{
				if(strcmp(h->data, "true")==0)
					debug=true;
				else if(strcmp(h->data, "false")==0)
					debug=false;
			}
			else
				debug=true;
			e=0;
		}
		else if(strcmp(h->funct, "transcript")==0)
		{
			if(h->data)
			{
				if(strcmp(h->data, "true")==0)
					transcript=true;
				else if(strcmp(h->data, "false")==0)
					transcript=false;
			}
			else
				transcript=true;
			e=0;
		}
		else if(strcmp(h->funct, "pipeline")==0)
		{
			if(h->data)
			{
				if(strcmp(h->data, "true")==0)
					pipeline=true;
				else if(strcmp(h->data, "false")==0)
					pipeline=false;
			}
			else
				pipeline=true;
			e=0;
		}
		else
		{
			fprintf(stderr, "horde: unrecognised cmd %s\n", h->funct);
			e=1;
		}
		free_hmsg(h);
		return(e);
	}
	if(debug)
		fprintf(stderr, "horde: malformed message in %s: %s\n", file?file:"stdin", inp);
	return(1);
}

int handle_add(const hmsg h)
{
	handler newh=(handler){.name=NULL, .prog=NULL, .n_init=0, .init=NULL, .only=false};
	unsigned int i;
	for(i=0;i<h->nparms;i++)
	{
		if(strcmp(h->p_tag[i], "name")==0)
		{
			free(newh.name);
			newh.name=strdup(h->p_value[i]);
		}
		else if(strcmp(h->p_tag[i], "prog")==0)
		{
			free(newh.prog);
			newh.prog=strdup(h->p_value[i]);
		}
		else if(strcmp(h->p_tag[i], "only")==0)
		{
			newh.only=true;
		}
		else if(strcmp(h->p_tag[i], "stdinit")==0)
		{
			if(!newh.n_init)
			{
				newh.init=malloc((newh.n_init=(pipeline?2:1))*sizeof(hmsg));
				if(!newh.init)
				{
					fprintf(stderr, "horde: allocation failure (newh.init): malloc: %s\n", strerror(errno));
					return(1);
				}
				newh.init[0]=new_hmsg("root", root);
				if(!newh.init[0])
				{
					fprintf(stderr, "horde: allocation failure (newh.init[0]): new_hmsg: %s\n", strerror(errno));
					return(1);
				}
				if(pipeline)
				{
					newh.init[1]=new_hmsg("pipeline", NULL);
					if(!newh.init[1])
					{
						fprintf(stderr, "horde: allocation failure (newh.init[1]): new_hmsg: %s\n", strerror(errno));
						return(1);
					}
				}
			}
		}
	}
	if(add_handler(newh)<0)
	{
		fprintf(stderr, "horde: add_handler() failed\n");
		free(newh.name);
		free(newh.prog);
		return(1);
	}
	if(debug)
		fprintf(stderr, "horde: added handler for %s\n", newh.name);
	return(0);
}

int handle_workers(__attribute__((unused)) hmsg h)
{
	fprintf(stderr, "horde: workers (%u)\n", nworkers);
	unsigned int w;
	for(w=0;w<nworkers;w++)
	{
		char name[13];
		memset(name, ' ', 12);
		name[12]=0;
		memcpy(name, workers[w].name, strlen(workers[w].name));
		unsigned int m=workers[w].n_rqs?workers[w].t_micro*1e-3/workers[w].n_rqs:0;
		fprintf(stderr, "horde:\t%s%.12s[%05u]%s:: %u rq, mean %03ums\n", workers[w].accepting?"+":"-", name, workers[w].pid, workers[w].autoreplace?"*":" ", workers[w].n_rqs, m);
	}
	unsigned int m=net_rqs?net_micro*1e-3/net_rqs:0;
	fprintf(stderr, "horde:\t net         [     ] :: %lu rq, mean %03ums\n", net_rqs, m);
	return(0);
}

int addworker(worker new)
{
	unsigned int nw=nworkers++;
	worker *new_w=realloc(workers, nworkers*sizeof(*workers));
	if(!new_w)
	{
		fprintf(stderr, "horde: addworker: new_w==NULL\n");
		nworkers=nw;
		perror("horde: realloc");
		return(-1);
	}
	workers=new_w;
	new.t_micro=new.n_rqs=0;
	new.blocks=0; // never a valid pid of a real process
	gettimeofday(&new.current, NULL);
	new_w[nw]=new;
	return(nw);
}

void rmworker(unsigned int w)
{
	if(w>=nworkers) return;
	if(workers[w].pipe)
	{
		if(workers[w].pipe[0]) close(workers[w].pipe[0]);
		if(workers[w].pipe[1]) close(workers[w].pipe[1]);
	}
	pid_t was=workers[w].pid;
	pid_t blocks=workers[w].blocks;
	bool autoreplace=workers[w].autoreplace;
	const char *prog=workers[w].prog, *name=workers[w].name;
	while(w<nworkers)
	{
		workers[w]=workers[w+1];
		w++;
	}
	nworkers--;
	if(blocks)
	{
		for(w=0;w<nworkers;w++)
		{
			if(workers[w].pid==blocks)
			{
				hmsg dead=new_hmsg("err", NULL);
				add_htag(dead, "what", "worker-died");
				add_htag(dead, "worker-name", name);
				if(debug) fprintf(stderr, "horde: reporting death to %s[%d]\n", workers[w].name, workers[w].pid);
				hsend(workers[w].pipe[1], dead);
				if(dead) free(dead);
			}
		}
	}
	worker *new_w=realloc(workers, nworkers*sizeof(*workers));
	if(new_w)
		workers=new_w;
	if(autoreplace)
	{
		unsigned int w;
		pid_t p=do_fork(prog, name, 0, &w);
		if(p<1)
		{
			fprintf(stderr, "horde: rmworker: autoreplace failed\n");
		}
		else
		{
			if(debug) fprintf(stderr, "horde: rmworker: automatically replaced worker %s[%u->%u]\n", name, was, p);
			workers[w].autoreplace=true;
		}
	}
	return;
}

int worker_set(int *fdmax, fd_set *fds)
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

pid_t do_fork(const char *prog, const char *name, int rfd, unsigned int *w)
{
	struct stat stbuf;
	if(stat(prog, &stbuf)) return(-1); // not there (or can't stat for some other reason)
	if(access(prog, X_OK)) return(-1); // can't execute
	worker new=(worker){.prog=prog, .name=name, .special=NONE, .autoreplace=false};
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
			if((ww=addworker(new))<0)
			{
				fprintf(stderr, "horde: failed to add worker '%s'\n", name);
				kill(new.pid, SIGHUP);
				close(new.pipe[0]);
				close(new.pipe[1]);
				return(0);
			}
			if(w) *w=ww;
			hmsg h=new_hmsg("debug", debug?"true":"false");
			if(h)
			{
				hsend(workers[ww].pipe[1], h);
				free_hmsg(h);
			}
		break;
	}
	return(new.pid);
}

signed int find_worker(const char *name, bool creat, int *fdmax, fd_set *fds)
{
	unsigned int w;
	for(w=0;w<nworkers;w++)
	{
		if(!workers[w].accepting) continue;
		if(strcmp(workers[w].name, name)==0)
			return(w);
	}
	if(creat)
	{
		unsigned int h;
		for(h=0;h<nhandlers;h++)
		{
			if(strcmp(handlers[h].name, name)==0)
			{
				pid_t p=do_fork(handlers[h].prog, handlers[h].name, 0, &w);
				if(p>0)
				{
					if(debug) fprintf(stderr, "horde: started new instance of %s[%u]\n", name, p);
					workers[w].autoreplace=handlers[h].only;
					workers[w].accepting=true;
					if(worker_set(fdmax, fds))
					{
						fprintf(stderr, "horde: worker_set failed, bad things may happen\n");
					}
					unsigned int i;
					for(i=0;i<handlers[h].n_init;i++)
					{
						hsend(workers[w].pipe[1], handlers[h].init[i]);
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

void uptime_respond(unsigned int w, hmsg h, time_t upsince)
{
	time_t now=time(NULL);
	char *rv;
	unsigned int l,i;
	init_char(&rv, &l, &i);
	char *d=h->data;
	int seconds;
	char ups[320];
	while(*d)
	{
		switch(*d)
		{
			case '^':
				switch(d[1])
				{
					case 'h':
						seconds=difftime(now, upsince);
						snprintf(ups, 320, "%u days, %.2u:%.2u:%.2u", (seconds/86400), (seconds/3600)%24, (seconds/60)%60, seconds%60);
						append_str(&rv, &l, &i, ups);
						d++;
					break;
					case 's':;
						FILE *sysupp=fopen("/proc/uptime", "r");
						double dseconds=-1;
						fscanf(sysupp, "%lg", &dseconds);
						seconds=dseconds;
						fclose(sysupp);
						snprintf(ups, 320, "%u days, %.2u:%.2u:%.2u", (seconds/86400), (seconds/3600)%24, (seconds/60)%60, seconds%60);
						append_str(&rv, &l, &i, ups);
						d++;
					break;
					default:
						append_char(&rv, &l, &i, *d);
					break;
				}
			break;
			default:
				append_char(&rv, &l, &i, *d);
			break;
		}
		d++;
	}
	hmsg u=new_hmsg("uptime", rv);
	free(rv);
	hsend(workers[w].pipe[1], u);
	free_hmsg(u);
}

void stats_respond(unsigned int w, hmsg h)
{
	if(strcmp(h->data, "bytes_today")==0)
	{
		char rv[12];
		if(bytes_today<2<<10)
			snprintf(rv, 12, "%zu", bytes_today);
		else if(bytes_today<2<<19)
			snprintf(rv, 12, "%1.2fk", bytes_today/1024.0);
		else if(bytes_today<2<<29)
			snprintf(rv, 12, "%1.2fM", bytes_today/1048576.0);
		else
			snprintf(rv, 12, "%1.2fG", bytes_today/1073741824.0);
		hmsg u=new_hmsg("stats", rv);
		hsend(workers[w].pipe[1], u);
		free_hmsg(u);
	}
	else
	{
		hmsg u=new_hmsg("err", "stats");
		if(u) add_htag(u, "what", "unrecognised-arg");
		hsend(workers[w].pipe[1], u);
		free_hmsg(u);
	}
}

void statsup(hmsg h)
{
	for(unsigned int i=0;i<h->nparms;i++)
	{
		if(strcmp(h->p_tag[i], "bytes_today")==0)
		{
			size_t morebytes;
			if(sscanf(h->p_value[i], "%zu", &morebytes)==1)
				bytes_today+=morebytes;
			continue;
		}
	}
}
