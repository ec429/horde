#define _GNU_SOURCE
/*
	horde: modular http server
	Copyright (C) 2011 Edward Cree

	Licensed under GNU GPLv3+; see top of horde.c for details
	
	pico: process <?pico> tags like httpico
	Note that pico assumes supplied data to be textual (ie. no \0s)
*/

#include <stdio.h>
#include <stdbool.h>
#include <string.h>
#include <time.h>
#include <errno.h>

#include "bits.h"
#include "libhorde.h"

#define PICO_VER	"0.0.6"

void handle(const char *inp, hstate *hst);
char *picofy(const hmsg h, hstate *hst);

bool debug, pipeline;

int main(int argc, char **argv)
{
	hstate hst;
	hst_init(&hst, argc?argv[0]:"pico", false);
	FILE *rc=fopen("modules/pico/pico.rc", "r");
	if(rc)
	{
		char *line;
		while((line=fgetl(rc)))
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
			handle(line, &hst);
			free(line);
		}
		fclose(rc);
		if(hst.debug) fprintf(stderr, "horde: %s[%d]: finished reading rc file\n", hst.name, getpid());
	}
	while(!hst.shutdown)
	{
		char *inp=getl(STDIN_FILENO);
		if(!inp) break;
		if(!*inp) {free(inp);continue;}
		handle(inp, &hst);
		free(inp);
	}
	hfin(EXIT_SUCCESS);
	return(EXIT_SUCCESS);
}

void handle(const char *inp, hstate *hst)
{
	hmsg h=hmsg_from_str(inp, true);
	if(hmsg_state(h, hst)) {free_hmsg(h); return;}
	if(h)
	{
		const char *from=gettag(h, "from");
		if(strcmp(h->funct, "pico")==0)
		{
			if(!h->data)
			{
				if(hst->debug) fprintf(stderr, "horde: %s[%d]: missing data in (pico)\n", hst->name, getpid());
				hmsg eh=new_hmsg("err", inp);
				if(eh)
				{
					add_htag(eh, "what", "missing-data");
					if(from) add_htag(eh, "to", from);
					hsend(1, eh);
					free_hmsg(eh);
				}
			}
			else
			{
				char *resp=picofy(h, hst);
				free(h->data);
				h->data=resp;
				h->dlen=strlen(resp);
				for(unsigned int i=0;i<h->nparms;i++)
				{
					if(strcmp(h->p_tag[i], "from")==0)
						strcpy(h->p_tag[i], "to");
				}
				add_htag(h, "server", "pico "PICO_VER);
				hsend(1, h);
			}
			if(hst->pipeline)
			{
				if(hst->debug) fprintf(stderr, "horde: %s[%d]: request serviced, available for another\n", hst->name, getpid());
				hmsg ready=new_hmsg("ready", NULL);
				hsend(1, ready);
				free_hmsg(ready);
			}
			else
			{
				if(hst->debug) fprintf(stderr, "horde: %s[%d]: finished service, not making self available again\n", hst->name, getpid());
				hst->shutdown=true;
			}
		}
		else
		{
			if(hst->debug) fprintf(stderr, "horde: %s[%d]: unrecognised funct '%s'\n", hst->name, getpid(), h->funct);
			hmsg eh=new_hmsg("err", inp);
			if(eh)
			{
				add_htag(eh, "what", "unrecognised-funct");
				if(from) add_htag(eh, "to", from);
				hsend(1, eh);
				free_hmsg(eh);
			}
		}
		free_hmsg(h);
	}
	return;
}

char *picofy(const hmsg h, hstate *hst)
{
	const char *ua=NULL;
	const char *rqpath=gettag(h, "rqpath"), *rspath=gettag(h, "rspath");
	for(unsigned int i=0;i<h->nparms;i++)
	{
		//if(hst->debug) fprintf(stderr, "horde: %s[%d]: \t (%s|%s)\n", hst->name, getpid(), h->p_tag[i], h->p_value[i]);
		if(strcmp(h->p_tag[i], "rqheader")==0)
		{
			off_t colon=strcspn(h->p_value[i], ":");
			char *hname=strndup(h->p_value[i], colon++);
			while(isspace(h->p_value[i][colon])) colon++;
			http_header hdr=get_header(hname);
			if(hdr==HTTP_HEADER_USER_AGENT)
				ua=h->p_value[i]+colon;
			free(hname);
		}
	}
	char *rv;
	size_t l,i;
	init_char(&rv, &l, &i);
	char *d=strdup(h->data);
	if(!d) return(NULL);
	while(*d)
	{
		if(strncmp(d, "<?pico", 6)==0)
		{
			d+=6;
			unsigned int bc=1;
			while(isspace(*d)) d++;
			char *e=d;
			while(bc)
			{
				if(*e=='<') bc++;
				else if(*e=='>') bc--;
				if(bc) e++;
				if(!*e) break;
			}
			if(!*e)
				append_char(&rv, &l, &i, *d++);
			else
			{
				*e++=0;
				char *f=strchr(d, '=');
				if(f)
				{
					*f++=0;
					if(*f++=='"')
					{
						char *g=strchr(f, '"');
						if(g)
						{
							*g=0;
						}
					}
				}
				// now d is the name and f is the format (if any)
				if(strcmp(d, "pfile")==0)
				{
					if(f)
					{
						FILE *pf=NULL;
						hmsg fh=new_hmsg("pico", f);
						for(unsigned int i=0;i<h->nparms;i++)
							add_htag(fh, h->p_tag[i], h->p_value[i]);
						char *fpd=picofy(fh, hst);
						free_hmsg(fh);
						if(fpd&&(*fpd=='/'))
						{
							char *fn=malloc(strlen(fpd)+strlen(hst->root)+1);
							if(fn)
							{
								sprintf(fn, "%s%s", hst->root, fpd);
								pf=fopen(fn, "r");
							}
							if(pf)
							{
								char *pfd;
								ssize_t length=dslurp(pf, &pfd);
								fclose(pf);
								hmsg ph=new_hmsg_d("pico", pfd, length);
								free(pfd);
								for(unsigned int i=0;i<h->nparms;i++)
									add_htag(ph, h->p_tag[i], h->p_value[i]);
								char *pd=picofy(ph, hst);
								if(pd)
									append_str(&rv, &l, &i, pd);
								free(pd);
								free_hmsg(ph);
							}
							free(fn);
						}
					}
				}
				else if(strcmp(d, "file")==0)
				{
					if(f)
					{
						FILE *pf=NULL;
						hmsg fh=new_hmsg("pico", f);
						for(unsigned int i=0;i<h->nparms;i++)
							add_htag(fh, h->p_tag[i], h->p_value[i]);
						char *fpd=picofy(fh, hst);
						free_hmsg(fh);
						if(fpd&&(*fpd=='/'))
						{
							char *fn=malloc(strlen(fpd)+strlen(hst->root)+1);
							if(fn)
							{
								sprintf(fn, "%s%s", hst->root, fpd);
								pf=fopen(fn, "r");
							}
							if(pf)
							{
								char *pfd;
								ssize_t length=dslurp(pf, &pfd);
								fclose(pf);
								for(off_t o=0;o<length;o++)
									append_char(&rv, &l, &i, pfd[o]);
								free(pfd);
							}
							free(fn);
						}
					}
				}
				else if(strcmp(d, "log")==0)
				{
					FILE *pf=NULL;
					if(f)
						pf=fopen(f, "r");
					if(pf)
					{
						char *pfd;
						ssize_t length=dslurp(pf, &pfd);
						fclose(pf);
						for(off_t o=0;o<length;o++)
						{
							if(pfd[o]=='\n')
								append_str(&rv, &l, &i, "<br />");
							append_char(&rv, &l, &i, pfd[o]);
						}
						free(pfd);
					}
				}
				else if(strcmp(d, "useragent")==0)
				{
					if(ua)
					{
						hmsg u=new_hmsg("escape", ua);
						add_htag(u, "map", "html");
						hsend(1, u);
						while(!hst->shutdown)
						{
							char *inp=getl(STDIN_FILENO);
							if(!inp) {append_str(&rv, &l, &i, "[error]");break;}
							if(!*inp) {free(inp);continue;}
							hmsg h=hmsg_from_str(inp, true);
							if(h&&!hmsg_state(h, hst))
							{
								if(strcmp(h->funct, "escape")==0)
								{
									append_str(&rv, &l, &i, h->data);
									break;
								}
								else if(strcmp(h->funct, "err")==0)
								{
									append_str(&rv, &l, &i, "[error]");
									break;
								}
							}
							free_hmsg(h);
							free(inp);
						}
						if(hst->shutdown)
							exit(EXIT_FAILURE);
					}
					else append_char(&rv, &l, &i, '?');
				}
				else if(strcmp(d, "version")==0)
					append_str(&rv, &l, &i, "horde "HTTPD_VERSION" / pico "PICO_VER);
				else if(strcmp(d, "uptime")==0)
				{
					hmsg u=new_hmsg("uptime", "horde: ^h | system: ^s");
					hsend(1, u);
					while(!hst->shutdown)
					{
						char *inp=getl(STDIN_FILENO);
						if(!inp) {append_str(&rv, &l, &i, "[error]");break;}
						if(!*inp) {free(inp);continue;}
						hmsg h=hmsg_from_str(inp, true);
						if(h&&!hmsg_state(h, hst))
						{
							if(strcmp(h->funct, "uptime")==0)
							{
								append_str(&rv, &l, &i, h->data);
								break;
							}
							else if(strcmp(h->funct, "err")==0)
							{
								append_str(&rv, &l, &i, "[error]");
								break;
							}
						}
						free_hmsg(h);
						free(inp);
					}
					if(hst->shutdown)
						exit(EXIT_FAILURE);
				}
				else if(strcmp(d, "rqpath")==0)
				{
					if(rqpath)
					{
						hmsg u=new_hmsg("escape", rqpath);
						add_htag(u, "map", "html-ns");
						hsend(1, u);
						while(!hst->shutdown)
						{
							char *inp=getl(STDIN_FILENO);
							if(!inp) {append_str(&rv, &l, &i, "[error]");break;}
							if(!*inp) {free(inp);continue;}
							hmsg h=hmsg_from_str(inp, true);
							if(h&&!hmsg_state(h, hst))
							{
								if(strcmp(h->funct, "escape")==0)
								{
									append_str(&rv, &l, &i, h->data);
									break;
								}
								else if(strcmp(h->funct, "err")==0)
								{
									append_str(&rv, &l, &i, "[error]");
									break;
								}
							}
							free_hmsg(h);
							free(inp);
						}
						if(hst->shutdown)
							exit(EXIT_FAILURE);
					}
				}
				else if(strcmp(d, "rspath")==0)
				{
					const char *p=rspath;
					if(!p) p=rqpath;
					if(p)
					{
						hmsg u=new_hmsg("escape", p);
						add_htag(u, "map", "html-ns");
						hsend(1, u);
						while(!hst->shutdown)
						{
							char *inp=getl(STDIN_FILENO);
							if(!inp) {append_str(&rv, &l, &i, "[error]");break;}
							if(!*inp) {free(inp);continue;}
							hmsg h=hmsg_from_str(inp, true);
							if(h&&!hmsg_state(h, hst))
							{
								if(strcmp(h->funct, "escape")==0)
								{
									append_str(&rv, &l, &i, h->data);
									break;
								}
								else if(strcmp(h->funct, "err")==0)
								{
									append_str(&rv, &l, &i, "[error]");
									break;
								}
							}
							free_hmsg(h);
							free(inp);
						}
						if(hst->shutdown)
							exit(EXIT_FAILURE);
					}
				}
				else if(strcmp(d, "host")==0)
					append_str(&rv, &l, &i, hst->host);
				else if(strcmp(d, "now")==0)
				{
					char now[256];
					time_t timer = time(NULL);
					struct tm *tm = gmtime(&timer);
					strftime(now, sizeof(now), f, tm);
					append_str(&rv, &l, &i, now);
				}
				else if(strcmp(d, "stats")==0)
				{
					hmsg u=new_hmsg("stats", f);
					hsend(1, u);
					while(!hst->shutdown)
					{
						char *inp=getl(STDIN_FILENO);
						if(!inp) {append_str(&rv, &l, &i, "[error]");break;}
						if(!*inp) {free(inp);continue;}
						hmsg h=hmsg_from_str(inp, true);
						if(h&&!hmsg_state(h, hst))
						{
							if(strcmp(h->funct, "stats")==0)
							{
								append_str(&rv, &l, &i, h->data);
								break;
							}
							else if(strcmp(h->funct, "err")==0)
							{
								append_str(&rv, &l, &i, "[error]");
								break;
							}
						}
						free_hmsg(h);
						free(inp);
					}
					if(hst->shutdown)
						exit(EXIT_FAILURE);
				}
				else if(strcmp(d, "system")==0)
				{
					hmsg u=new_hmsg("workers", NULL);
					hsend(1, u);
					while(!hst->shutdown)
					{
						char *inp=getl(STDIN_FILENO);
						if(!inp) {append_str(&rv, &l, &i, "[error]");break;}
						if(!*inp) {free(inp);continue;}
						hmsg h=hmsg_from_str(inp, true);
						if(h&&!hmsg_state(h, hst))
						{
							if(strcmp(h->funct, "workers")==0)
							{
								if(h->data)
								{
									hmsg u=new_hmsg("escape", h->data);
									add_htag(u, "map", "html");
									hsend(1, u);
									while(!hst->shutdown)
									{
										char *inp=getl(STDIN_FILENO);
										if(!inp) {append_str(&rv, &l, &i, "[error]");break;}
										if(!*inp) {free(inp);continue;}
										hmsg h=hmsg_from_str(inp, true);
										if(h&&!hmsg_state(h, hst))
										{
											if(strcmp(h->funct, "escape")==0)
											{
												append_str(&rv, &l, &i, "<table>\n<tr><td>");
												const char *p=h->data;
												while(p&&*p)
												{
													if(*p=='\n')
														append_str(&rv, &l, &i, "</td></tr>\n<tr><td>");
													else if(*p=='\t')
													{
														while(p[1]=='\t') p++;
														append_str(&rv, &l, &i, "</td><td>");
													}
													else if(*p==' ')
														append_str(&rv, &l, &i, "&nbsp;");
													else
														append_char(&rv, &l, &i, *p);
													p++;
												}
												append_str(&rv, &l, &i, "</td></tr>\n</table>\n");
												break;
											}
											else if(strcmp(h->funct, "err")==0)
											{
												append_str(&rv, &l, &i, "[error]");
												break;
											}
										}
										free_hmsg(h);
										free(inp);
									}
									if(hst->shutdown)
										exit(EXIT_FAILURE);
								}
								break;
							}
							else if(strcmp(h->funct, "err")==0)
							{
								append_str(&rv, &l, &i, "[error]");
								break;
							}
						}
						free_hmsg(h);
						free(inp);
					}
					if(hst->shutdown)
						exit(EXIT_FAILURE);
				}
				// jump over the tag
				d=e;
			}
		}
		else
			append_char(&rv, &l, &i, *d++);
	}
	return(rv);
}
