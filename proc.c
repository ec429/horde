#define _GNU_SOURCE
/*
	horde: modular http server
	Copyright (C) 2011 Edward Cree

	Licensed under GNU GPLv3+; see top of horde.c for details
	
	proc: processes files before serving
*/

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <errno.h>
#include <sys/stat.h>

#include "bits.h"
#include "libhorde.h"

#define MAXBLOBLEN	(1<<16) // the maximum length of a file to send in-band unprocessed (instead of using 'read').  If any processing is needed, this is ignored

typedef struct
{
	lform rule;
	hmsg functor;
	enum {P_THRU, P_404} onfail;
}
processor;

int handle(const char *inp, const char *name, char **root);
int add_processor(processor p);

lvalue app(lform lf);

bool debug, pipeline;
unsigned int nprocs;
processor *procs;

int main(int argc, char **argv)
{
	const char *name=argc?argv[0]:"proc";
	char *root=strdup("root");
	pipeline=false;
	debug=false;
	nprocs=0;
	procs=NULL;
	FILE *rc=fopen(".proc", "r");
	if(rc)
	{
		//fprintf(stderr, "horde: %s[%d]: reading rc file '.proc'\n", name, getpid());
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
			int e=handle(line, name, &root);
			free(line);
			if(e) break;
		}
		fclose(rc);
		if(debug) fprintf(stderr, "horde: %s[%d]: finished reading rc file\n", name, getpid());
	}
	/*else
		fprintf(stderr, "horde: %s[%d]: failed to open rc file '.proc': fopen: %s\n", name, getpid(), strerror(errno));*/
	int errupt=0;
	while(!errupt)
	{
		char *inp=getl(STDIN_FILENO);
		if(!inp) break;
		if(!*inp) {free(inp);continue;}
		errupt=handle(inp, name, &root);
		free(inp);
	}
	hfin(EXIT_SUCCESS);
	return(EXIT_SUCCESS);
}

int handle(const char *inp, const char *name, char **root)
{
	int errupt=0;
	hmsg h=hmsg_from_str(inp);
	if(h)
	{
		const char *from=NULL;
		unsigned short status=200;
		const char *statusmsg=NULL;
		unsigned int i;
		for(i=0;i<h->nparms;i++)
		{
			if(strcmp(h->p_tag[i], "from")==0)
			{
				from=h->p_value[i];
				break;
			}
			else if(strcmp(h->p_tag[i], "status")==0)
			{
				unsigned short ns=hgetshort(h->p_value[i]);
				if((ns<600)&&(ns>99)) status=ns;
			}
			else if(strcmp(h->p_tag[i], "statusmsg")==0)
			{
				statusmsg=h->p_value[i];
			}
		}
		if(!statusmsg)
			statusmsg=http_statusmsg(status);
		if(!statusmsg)
			statusmsg="???";
		if(strcmp(h->funct, "add")==0)
		{
			processor newp=(processor){.rule=NULL, .functor=NULL, .onfail=P_404};
			unsigned int i;
			for(i=0;i<h->nparms;i++)
			{
				if(strcmp(h->p_tag[i], "rule")==0)
				{
					lform nrule=lform_str(h->p_value[i], NULL);
					if(!nrule) break;
					if(newp.rule)
					{
						lform and=malloc(sizeof(*and));
						if(!and)
						{
							free_lform(nrule);
						}
						and->funct=strdup("and");
						if(!and->funct)
						{
							free_lform(nrule);
							free(and);
							break;
						}
						and->nchld=2;
						and->chld=malloc(2*sizeof(*and));
						if(!and->chld)
						{
							free_lform(nrule);
							free(and->funct);
							free(and);
							break;
						}
						and->chld[0]=*newp.rule;
						and->chld[1]=*nrule;
						free(newp.rule);
						free(nrule);
						newp.rule=and;
					}
					else
					{
						newp.rule=nrule;
					}
					continue;
				}
				else if(strcmp(h->p_tag[i], "proc")==0)
				{
					if(!newp.functor)
					{
						hmsg nf=new_hmsg(h->p_value[i], NULL);
						if(nf)
							newp.functor=nf;
					}
				}
				else if(strcmp(h->p_tag[i], "onfail")==0)
				{
					if(strcmp(h->p_value[i], "passthru")==0)
					{
						newp.onfail=P_THRU;
					}
					else if(strcmp(h->p_value[i], "404")==0)
					{
						newp.onfail=P_404;
					}
				}
			}
			add_processor(newp);
		}
		else if(strcmp(h->funct, "proc")==0)
		{
			if(strstr(h->data, "/../"))
			{
				hmsg r=new_hmsg("err", NULL);
				add_htag(r, "what", "illegal-path"); // this shouldn't ever happen, hence why we report instead of giving a 4xx
				if(from) add_htag(r, "to", from);
				hsend(1, r);
				free_hmsg(r);
			}
			else
			{
				char *path=malloc(strlen(*root)+strlen(h->data)+1);
				if(!path)
				{
					hmsg r=new_hmsg("err", NULL);
					add_htag(r, "what", "allocation-failure");
					if(from) add_htag(r, "to", from);
					hsend(1, r);
					free_hmsg(r);
				}
				else
				{
					strcpy(path, *root);
					strcat(path, h->data);
					bool isdir=false;
					struct stat stbuf;
					if(stat(path, &stbuf))
					{
						if(debug) fprintf(stderr, "horde: %s[%d]: stat(%s) failed: %s\n", name, getpid(), path, strerror(errno));
						hmsg r;
						switch(errno)
						{
							case ENOENT:
								if(status==200)
								{
									r=new_hmsg("proc", "/404.htm"); // tail-recursive proc call
									char st[9];
									hputshort(st, 404);
									add_htag(r, "status", st);
									add_htag(r, "statusmsg", "Not Found");
									if(from) add_htag(r, "to", from);
									hsend(1, r);
									free_hmsg(r);
								}
								else if(status==403)
								{
									if(debug) fprintf(stderr, "horde: %s[%d]: using static 403\n", name, getpid());
									r=new_hmsg("proc", "<!DOCTYPE html PUBLIC \"-//W3C//DTD XHTML 1.0 Strict//EN\" \"http://www.w3.org/TR/xhtml1/DTD/xhtml1-strict.dtd\">\n<html><head>\n<title>403 -- Forbidden</title>\n</head><body>\n<h1>HTTP Error 403: Forbidden</h1>\n<p>You don't have permission to view the requested resource.</p>\n</body></html>");
									char st[9];
									hputshort(st, 404);
									add_htag(r, "status", st);
									add_htag(r, "statusmsg", "Not Found");
									if(from) add_htag(r, "to", from);
									hsend(1, r);
									free_hmsg(r);
								}
								else if(status==404)
								{
									if(debug) fprintf(stderr, "horde: %s[%d]: using static 404\n", name, getpid());
									r=new_hmsg("proc", "<!DOCTYPE html PUBLIC \"-//W3C//DTD XHTML 1.0 Strict//EN\" \"http://www.w3.org/TR/xhtml1/DTD/xhtml1-strict.dtd\">\n<html><head>\n<title>404 -- Not Found</title>\n</head><body>\n<h1>HTTP Error 404: Not Found</h1>\n<p>The requested URL was not found on this server.</p>\n</body></html>");
									char st[9];
									hputshort(st, 404);
									add_htag(r, "status", st);
									add_htag(r, "statusmsg", "Not Found");
									if(from) add_htag(r, "to", from);
									hsend(1, r);
									free_hmsg(r);
								}
								else
								{
									if(debug) fprintf(stderr, "horde: %s[%d]: using semi-static HTTP%hu\n", name, getpid(), status);
									char *sed=malloc(400+strlen(statusmsg));
									if(!sed)
									{
										hmsg r=new_hmsg("err", NULL);
										add_htag(r, "what", "allocation-failure");
										if(from) add_htag(r, "to", from);
										hsend(1, r);
										free_hmsg(r);
									}
									else
									{
										sprintf(sed, "<!DOCTYPE html PUBLIC \"-//W3C//DTD XHTML 1.0 Strict//EN\" \"http://www.w3.org/TR/xhtml1/DTD/xhtml1-strict.dtd\">\n<html><head>\n<title>%1$hu -- %2$s</title>\n</head><body>\n<h1>HTTP Error %1$hu: %2$s</h1>\n<p>The above error occurred while trying to process the request.  Furthermore, no default or custom error page matching the error in question was found.</p>\n</body></html>", status, statusmsg);
										r=new_hmsg("proc", sed);
										char st[9];
										hputshort(st, status);
										add_htag(r, "status", st);
										add_htag(r, "statusmsg", statusmsg);
										if(from) add_htag(r, "to", from);
									}
								}
							break;
							default:
								r=new_hmsg("err", NULL);
								add_htag(r, "what", "open-failure");
								char en[16];
								hputlong(en, errno);
								add_htag(r, "errno", en);
								if(from) add_htag(r, "to", from);
								hsend(1, r);
								free_hmsg(r);
							break;
						}
						if(pipeline)
						{
							if(debug) fprintf(stderr, "horde: %s[%d]: request serviced, available for another\n", name, getpid());
						}
						else
						{
							if(debug) fprintf(stderr, "horde: %s[%d]: finished service, not making self available again\n", name, getpid());
							errupt++;
						}
						free(path);
						return(errupt);
					}
					else
					{
						isdir=stbuf.st_mode&S_IFDIR;
					}
					if(isdir)
					{
						char *ipath=malloc(strlen(path)+9+1);
						if(!ipath)
						{
							hmsg r=new_hmsg("err", NULL);
							add_htag(r, "what", "allocation-failure");
							if(from) add_htag(r, "to", from);
							hsend(1, r);
							free_hmsg(r);
						}
						else
						{
							sprintf(ipath, "%sindex.htm", path);
							if(stat(ipath, &stbuf)) // redirect to index; if not present, forbid directory listings
							{
								hmsg r=new_hmsg("proc", "/403.htm"); // tail-recursive proc call
								char st[9];
								hputshort(st, 403);
								add_htag(r, "status", st);
								add_htag(r, "statusmsg", "Forbidden");
								if(from) add_htag(r, "to", from);
								hsend(1, r);
								free_hmsg(r);
							}
							else
							{
								hmsg r=new_hmsg("proc", NULL);
								char st[9];
								hputshort(st, 302);
								add_htag(r, "status", st);
								add_htag(r, "statusmsg", "Found");
								char *loc=malloc(10+strlen(h->data)+9+1);
								if(!loc)
								{
									hmsg r=new_hmsg("err", NULL);
									add_htag(r, "what", "allocation-failure");
									if(from) add_htag(r, "to", from);
									hsend(1, r);
									free_hmsg(r);
								}
								else
								{
									sprintf(loc, "Location: %sindex.htm", h->data);
									add_htag(r, "header", loc);
									free(loc);
									if(from) add_htag(r, "to", from);
									hsend(1, r);
									free_hmsg(r);
								}
							}
							free(ipath);
						}
					}
					else
					{
						FILE *fp=fopen(path, "r");
						if(!fp)
						{
							hmsg r;
							switch(errno)
							{
								case ENOENT:
									r=new_hmsg("proc", "/404.htm"); // tail-recursive proc call
									char st[9];
									hputshort(st, 404);
									add_htag(r, "status", st);
									add_htag(r, "statusmsg", "Not Found");
									if(from) add_htag(r, "to", from);
									hsend(1, r);
									free_hmsg(r);
								break;
								default:
									r=new_hmsg("err", NULL);
									add_htag(r, "what", "open-failure");
									char en[16];
									hputlong(en, errno);
									add_htag(r, "errno", en);
									if(from) add_htag(r, "to", from);
									hsend(1, r);
									free_hmsg(r);
								break;
							}
						}
						else
						{
							char *content_type=NULL;
							char *ext=h->data, *last=NULL;
							while((ext=strchr(ext, '.'))) last=ext++;
							if((ext=last))
							{
								if(debug) fprintf(stderr, "horde: %s[%d]: sending request to ext\n", name, getpid());
								hmsg ex=new_hmsg("ext", ext+1);
								hsend(1, ex);
								free_hmsg(ex);
								bool exed=false;
								while(!exed)
								{
									char *inp2=getl(STDIN_FILENO);
									if(inp2)
									{
										if(!*inp2)
										{
											content_type=strdup("text/plain");
											exed=true;
										}
										else
										{
											hmsg h2=hmsg_from_str(inp2);
											if(h2)
											{
												if(strcmp(h2->funct, "ext")==0)
												{
													if(h2->data)
													{
														content_type=strdup(h2->data);
														exed=true;
													}
												}
												else if(strcmp(h2->funct, "err")==0)
												{
													if(debug)
													{
														fprintf(stderr, "horde: %s[%d]: ext failed: %s\n", name, getpid(), h2->funct);
														unsigned int i;
														for(i=0;i<h2->nparms;i++)
														{
															fprintf(stderr, "horde: %s[%d]:\t(%s|%s)\n", name, getpid(), h2->p_tag[i], h2->p_value[i]);
															if(strcmp(h2->p_tag[i], "errno")==0)
															{
																fprintf(stderr, "horde: %s[%d]:\t\t%s\n", name, getpid(), strerror(hgetlong(h2->p_value[i])));
															}
														}
														fprintf(stderr, "horde: %s[%d]:\t%s\n", name, getpid(), h2->data);
													}
													hmsg eh=new_hmsg("err", inp);
													if(eh)
													{
														add_htag(eh, "what", "chld-failure");
														add_htag(eh, "fatal", NULL);
														add_htag(eh, "chld", "ext");
														add_htag(eh, "err", inp2);
														if(from) add_htag(eh, "to", from);
														hsend(1, eh);
														free_hmsg(eh);
													}
													hfin(EXIT_FAILURE);
													return(EXIT_FAILURE);
												}
												free_hmsg(h2);
											}
										}
										free(inp2);
									}
									else
									{
										content_type=strdup("text/plain");
										exed=true;
									}
								}
							}
							if(!content_type)
							{
								content_type=strdup("text/plain");
							}
							bool processed=false;
							char *buf;
							ssize_t length=dslurp(fp, &buf);
							fclose(fp);
							if((!buf)||(length<0))
							{
								hmsg r=new_hmsg("err", NULL);
								add_htag(r, "what", "allocation-failure");
								if(from) add_htag(r, "to", from);
								hsend(1, r);
								free_hmsg(r);
							}
							else
							{
								unsigned int proc;
								for(proc=0;proc<nprocs;proc++)
								{
									lvalue apply=l_eval(procs[proc].rule, app);
									if(debug) fprintf(stderr, "horde: %s[%d]: processor %u: %s\n", name, getpid(), proc, l_asbool(apply)?"match":"nomatch");
									if(l_asbool(apply))
									{
										hmsg h=procs[proc].functor;
										h->data=buf;
										hsend(1, h);
										processed=true;
										bool brk=false;
										while(!brk)
										{
											char *resp=getl(STDIN_FILENO);
											if(resp)
											{
												if(*resp)
												{
													hmsg h2=hmsg_from_str(resp);
													if(h2)
													{
														if(strcmp(h2->funct, procs[proc].functor->funct)==0)
														{
															if(h2->data)
															{
																free(buf);
																buf=h2->data;
															}
															brk=true;
														}
														else if(strcmp(h2->funct, "err")==0)
														{
															if(debug)
															{
																fprintf(stderr, "horde: %s[%d]: %s failed: %s\n", name, getpid(), procs[proc].functor->funct, h2->funct);
																unsigned int i;
																for(i=0;i<h2->nparms;i++)
																{
																	fprintf(stderr, "horde: %s[%d]:\t(%s|%s)\n", name, getpid(), h2->p_tag[i], h2->p_value[i]);
																	if(strcmp(h2->p_tag[i], "errno")==0)
																	{
																		fprintf(stderr, "horde: %s[%d]:\t\t%s\n", name, getpid(), strerror(hgetlong(h2->p_value[i])));
																	}
																}
																fprintf(stderr, "horde: %s[%d]:\t%s\n", name, getpid(), h2->data);
															}
															hmsg eh=new_hmsg("err", inp);
															if(eh)
															{
																add_htag(eh, "what", "chld-failure");
																add_htag(eh, "fatal", NULL);
																add_htag(eh, "chld", procs[proc].functor->funct);
																add_htag(eh, "err", resp);
																if(from) add_htag(eh, "to", from);
																hsend(1, eh);
																free_hmsg(eh);
															}
															hfin(EXIT_FAILURE);
															return(EXIT_FAILURE);
														}
														free_hmsg(h2);
													}
												}
												free(resp);
											}
										}
									}
									free_lvalue(apply);
								}
								bool useread=false;
								if((!processed)&&(length>MAXBLOBLEN))
								{
									free(buf);
									buf=NULL;
									useread=true;
								}
								hmsg r=new_hmsg("proc", buf);
								char st[9];
								hputshort(st, status);
								add_htag(r, "status", st);
								add_htag(r, "statusmsg", statusmsg);
								if(content_type)
								{
									char *ctype=malloc(16+strlen(content_type));
									if(ctype)
									{
										sprintf(ctype, "Content-Type: %s", content_type);
										add_htag(r, "header", ctype);
										free(ctype);
									}
									free(content_type);
								}
								char ln[17];
								hputlong(ln, length);
								add_htag(r, "length", ln);
								if(from) add_htag(r, "to", from);
								if(useread)
									add_htag(r, "read", path);
								hsend(1, r);
								if(buf) free(buf);
								free_hmsg(r);
							}
						}
					}
				}
				free(path);
			}
			if(pipeline)
			{
				if(debug) fprintf(stderr, "horde: %s[%d]: request serviced, available for another\n", name, getpid());
			}
			else
			{
				if(debug) fprintf(stderr, "horde: %s[%d]: finished service, not making self available again\n", name, getpid());
				errupt++;
			}
		}
		else if(strcmp(h->funct, "shutdown")==0)
		{
			if(debug) fprintf(stderr, "horde: %s[%d]: server is shutting down\n", name, getpid());
			errupt++;
		}
		else if(strcmp(h->funct, "root")==0)
		{
			if(!h->data)
			{
				if(debug) fprintf(stderr, "horde: %s[%d]: missing data in (root)\n", name, getpid());
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
				char *nr=strdup(h->data);
				if(!nr)
				{
					if(debug) fprintf(stderr, "horde: %s[%d]: allocation failure (char *root): strdup: %s\n", name, getpid(), strerror(errno));
					hmsg eh=new_hmsg("err", inp);
					if(eh)
					{
						add_htag(eh, "what", "allocation-failure");
						add_htag(eh, "fatal", NULL);
						if(from) add_htag(eh, "to", from);
						hsend(1, eh);
						free_hmsg(eh);
					}
					hfin(EXIT_FAILURE);
					return(EXIT_FAILURE);
				}
				if(*root) free(*root);
				*root=nr;
				if(debug) fprintf(stderr, "horde: %s[%d]: root set to '%s'\n", name, getpid(), *root);
			}
		}
		else if(strcmp(h->funct, "pipeline")==0)
		{
			if(h->data)
			{
				if(strcmp(h->data, "false")==0)
					pipeline=false;
				else if(strcmp(h->data, "true")==0)
					pipeline=true;
				else
				{
					hmsg eh=new_hmsg("err", inp);
					if(eh)
					{
						add_htag(eh, "what", "invalid-data");
						add_htag(eh, "expected", "bool");
						if(from) add_htag(eh, "to", from);
						hsend(1, eh);
						free_hmsg(eh);
					}
				}
			}
			else
				pipeline=true;
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
		}
		else
		{
			if(debug) fprintf(stderr, "horde: %s[%d]: unrecognised funct '%s'\n", name, getpid(), h->funct);
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
	return(errupt);
}

int add_processor(processor p)
{
	unsigned int newn=nprocs++;
	processor *newp=realloc(procs, nprocs*sizeof(processor));
	if(!newp)
	{
		nprocs=newn;
		return(-1);
	}
	(procs=newp)[newn]=p;
	return(newn);
}

lvalue app(lform lf)
{
	fprintf(stderr, "proc[%u]: app: called\n", getpid());
	if(!lf)
		return(l_str(NULL));
	if(!lf->funct)
		return(l_str(NULL));
	if(lf->nchld&&!lf->chld)
		return(l_str(NULL));
	fprintf(stderr, "proc[%u]: app: unrecognised funct %s\n", getpid(), lf->funct);
	return(l_str(NULL));
}
