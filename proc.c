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
#include <unistd.h>
#include <dirent.h>

#include "bits.h"
#include "libhorde.h"

#define MAXBLOBLEN	(1<<16) // the maximum length of a file to send in-band unprocessed (instead of using 'read').  If any processing is needed, this is ignored
#define CWD_BUF_SIZE	4096
#define LINK_BUF_SIZE	4096

typedef struct
{
	lform rule;
	char *functor;
	enum {P_THRU, P_404, P_500} onfail;
}
processor;

int rcreaddir(const char *dn, hstate *hst);
int rcread(const char *fn, hstate *hst);
int handle(const char *inp, hstate *hst, bool rc);
int add_processor(processor p);

lvalue app(lform lf, lvars lv);

unsigned int nprocs;
processor *procs;

int main(int argc, char **argv)
{
	hstate hst;
	hst_init(&hst, argc?argv[0]:"proc", false);
	nprocs=0;
	procs=NULL;
	char cwdbuf[CWD_BUF_SIZE];
	if(!getcwd(cwdbuf, CWD_BUF_SIZE))
	{
		fprintf(stderr, "horde: %s[%d]: getcwd: %s\n", hst.name, getpid(), strerror(errno));
		hfin(EXIT_FAILURE);
		return(EXIT_FAILURE);
	}
	if(rcread("proc.rc", &hst))
	{
		fprintf(stderr, "horde: %s[%d]: bad rc, giving up\n", hst.name, getpid());
		hfin(EXIT_FAILURE);
		return(EXIT_FAILURE);
	}
	if(rcreaddir(cwdbuf, &hst))
	{
		fprintf(stderr, "horde: %s[%d]: bad rc, giving up\n", hst.name, getpid());
		hfin(EXIT_FAILURE);
		return(EXIT_FAILURE);
	}
	while(!hst.shutdown)
	{
		char *inp=getl(STDIN_FILENO);
		if(!inp) break;
		if(!*inp) {free(inp);continue;}
		handle(inp, &hst, false);
		free(inp);
	}
	hfin(EXIT_SUCCESS);
	return(EXIT_SUCCESS);
}

int rcreaddir(const char *dn, hstate *hst)
{
	char olddir[CWD_BUF_SIZE];
	if(!getcwd(olddir, CWD_BUF_SIZE))
	{
		fprintf(stderr, "horde: %s[%d]: getcwd: %s\n", hst->name, getpid(), strerror(errno));
		return(1);
	}
	if(hst->debug) fprintf(stderr, "horde: %s[%d]: searching %s/%s for config files\n", hst->name, getpid(), olddir, dn);
	if(chdir(dn))
	{
		if(hst->debug) fprintf(stderr, "horde: %s[%d]: failed to chdir(%s): %s\n", hst->name, getpid(), dn, strerror(errno));
		return(1);
	}
	DIR *rcdir=opendir(".");
	if(!rcdir)
	{
		fprintf(stderr, "horde: %s[%d]: failed to opendir %s: %s\n", hst->name, getpid(), dn, strerror(errno));
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
			if(hst->debug) fprintf(stderr, "horde: %s[%d]: failed to stat %s: %s\n", hst->name, getpid(), entry->d_name, strerror(errno));
			closedir(rcdir);
			chdir(olddir);
			return(1);
		}
		if(st.st_mode&S_IFDIR)
		{
			if(rcreaddir(entry->d_name, hst))
			{
				chdir(olddir);
				return(1);
			}
		}
		else if(strcmp(entry->d_name+strlen(entry->d_name)-5, ".proc")==0)
		{
			if(rcread(entry->d_name, hst))
			{
				closedir(rcdir);
				chdir(olddir);
				return(1);
			}
		}
	}
	if(hst->debug) fprintf(stderr, "horde: %s[%d]: done searching %s\n", hst->name, getpid(), dn);
	closedir(rcdir);
	chdir(olddir);
	return(0);
}

int rcread(const char *fn, hstate *hst)
{
	FILE *rc=fopen(fn, "r");
	if(rc)
	{
		if(hst->debug) fprintf(stderr, "horde: %s[%d]: reading rc file %s\n", hst->name, getpid(), fn);
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
			e=handle(line, hst, true);
			free(line);
		}
		fclose(rc);
		if(e)
		{
			if(hst->debug) fprintf(stderr, "horde: %s[%d]: choked on rc file %s\n", hst->name, getpid(), fn);
			return(e);
		}
		if(hst->debug) fprintf(stderr, "horde: %s[%d]: finished reading rc file\n", hst->name, getpid());
		return(0);
	}
	fprintf(stderr, "horde: %s[%d]: failed to open rc file %s: fopen: %s\n", hst->name, getpid(), fn, strerror(errno));
	return(1);
}

int handle(const char *inp, hstate *hst, bool rc)
{
	hmsg h=hmsg_from_str(inp, true);
	if(hmsg_state(h, hst)) return(0);
	int e=0;
	if(h)
	{
		const char *from=gettag(h, "from");
		unsigned short status=200;
		const char *statusmsg=gettag(h, "statusmsg");
		unsigned int i;
		for(i=0;i<h->nparms;i++)
		{
			if(strcmp(h->p_tag[i], "status")==0)
			{
				unsigned short ns=hgetshort(h->p_value[i]);
				if((ns<600)&&(ns>99)) status=ns;
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
						newp.functor=strdup(h->p_value[i]);
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
					else if(strcmp(h->p_value[i], "500")==0)
					{
						newp.onfail=P_500;
					}
				}
			}
			add_processor(newp);
		}
		else if(strcmp(h->funct, "proc")==0)
		{
			if(rc)
			{
				fprintf(stderr, "horde: %s[%d]: (proc) in rc file\n", hst->name, getpid());
				free_hmsg(h);
				return(1);
			}
			if(!h->data)
			{
				hmsg r=new_hmsg("proc", "/400.htm"); // tail-recursive proc call
				char st[TL_SHORT];
				hputshort(st, 400);
				add_htag(r, "status", st);
				add_htag(r, "statusmsg", "Bad Request");
				if(from) add_htag(r, "from", from);
				hsend(1, r);
				free_hmsg(r);
			}
			else if(strstr(h->data, "/../"))
			{
				hmsg r=new_hmsg("err", NULL);
				add_htag(r, "what", "illegal-path"); // this shouldn't ever happen, hence why we report instead of giving a 4xx
				if(from) add_htag(r, "to", from);
				hsend(1, r);
				free_hmsg(r);
			}
			else
			{
				char *path=malloc(strlen(hst->root)+strlen(h->data)+1);
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
					strcpy(path, hst->root);
					strcat(path, h->data);
					bool isdir=false;
					struct stat stbuf;
					if(stat(path, &stbuf))
					{
						if(hst->debug) fprintf(stderr, "horde: %s[%d]: stat(%s) failed: %s\n", hst->name, getpid(), path, strerror(errno));
						hmsg r;
						switch(errno)
						{
							case ENOENT:
								if(status==200)
								{
									r=new_hmsg("proc", "/404.htm"); // tail-recursive proc call
									char st[TL_SHORT];
									hputshort(st, 404);
									add_htag(r, "status", st);
									add_htag(r, "statusmsg", "Not Found");
									if(from) add_htag(r, "from", from);
									hsend(1, r);
									free_hmsg(r);
								}
								else if(status==403)
								{
									if(hst->debug) fprintf(stderr, "horde: %s[%d]: using static 403\n", hst->name, getpid());
									r=new_hmsg("proc", "<!DOCTYPE html PUBLIC \"-//W3C//DTD XHTML 1.0 Strict//EN\" \"http://www.w3.org/TR/xhtml1/DTD/xhtml1-strict.dtd\">\n<html><head>\n<title>403 -- Forbidden</title>\n</head><body>\n<h1>HTTP Error 403: Forbidden</h1>\n<p>You don't have permission to view the requested resource.</p>\n</body></html>");
									char st[TL_SHORT];
									hputshort(st, 403);
									add_htag(r, "status", st);
									add_htag(r, "statusmsg", "Not Found");
									if(from) add_htag(r, "to", from);
									hsend(1, r);
									free_hmsg(r);
								}
								else if(status==404)
								{
									if(hst->debug) fprintf(stderr, "horde: %s[%d]: using static 404\n", hst->name, getpid());
									r=new_hmsg("proc", "<!DOCTYPE html PUBLIC \"-//W3C//DTD XHTML 1.0 Strict//EN\" \"http://www.w3.org/TR/xhtml1/DTD/xhtml1-strict.dtd\">\n<html><head>\n<title>404 -- Not Found</title>\n</head><body>\n<h1>HTTP Error 404: Not Found</h1>\n<p>The requested URL was not found on this server.</p>\n</body></html>");
									char st[TL_SHORT];
									hputshort(st, 404);
									add_htag(r, "status", st);
									add_htag(r, "statusmsg", "Not Found");
									if(from) add_htag(r, "to", from);
									hsend(1, r);
									free_hmsg(r);
								}
								else
								{
									if(hst->debug) fprintf(stderr, "horde: %s[%d]: using semi-static HTTP%hu\n", hst->name, getpid(), status);
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
										char st[TL_SHORT];
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
								char en[TL_LONG];
								hputlong(en, errno);
								add_htag(r, "errno", en);
								if(from) add_htag(r, "to", from);
								hsend(1, r);
								free_hmsg(r);
							break;
						}
						free(path);
						goto procdone;
					}
					else
					{
						isdir=stbuf.st_mode&S_IFDIR;
					}
					if(stbuf.st_mode&S_IFLNK)
					{
						char *lpath=malloc(stbuf.st_size+1);
						if(lpath)
						{
							ssize_t b=readlink(path, lpath, stbuf.st_size);
							if((b!=-1)&&(lpath[0]!='/'))
							{
								lpath[b]=0;
								char *ipath=malloc(strlen(h->data)+strlen(lpath)+1);
								if(ipath)
								{
									strcpy(ipath, h->data);
									char *slash=strrchr(ipath, '/');
									if(slash) slash[1]=0;
									strcat(ipath, lpath);
									hmsg r=new_hmsg("proc", NULL);
									char st[TL_SHORT];
									hputshort(st, 302);
									add_htag(r, "status", st);
									add_htag(r, "statusmsg", "Found");
									char *loc=malloc(10+strlen(ipath)+1);
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
										sprintf(loc, "Location: %s", ipath);
										add_htag(r, "header", loc);
										free(loc);
										if(from) add_htag(r, "to", from);
										hsend(1, r);
										free_hmsg(r);
										free(ipath);
										free(lpath);
										free(path);
										goto procdone;
									}
									free_hmsg(r);
								}
								else
								{
									hmsg r=new_hmsg("err", NULL);
									add_htag(r, "what", "allocation-failure");
									if(from) add_htag(r, "to", from);
									hsend(1, r);
									free_hmsg(r);
								}
								free(ipath);
							}
						}
						else
						{
							hmsg r=new_hmsg("err", NULL);
							add_htag(r, "what", "allocation-failure");
							if(from) add_htag(r, "to", from);
							hsend(1, r);
							free_hmsg(r);
						}
						free(lpath);
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
								char st[TL_SHORT];
								hputshort(st, 403);
								add_htag(r, "status", st);
								add_htag(r, "statusmsg", "Forbidden");
								if(from) add_htag(r, "from", from);
								hsend(1, r);
								free_hmsg(r);
							}
							else
							{
								hmsg r=new_hmsg("proc", NULL);
								char st[TL_SHORT];
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
							char st[TL_SHORT];
							switch(errno)
							{
								case ENOENT:
									r=new_hmsg("proc", "/404.htm"); // tail-recursive proc call
									hputshort(st, 404);
									add_htag(r, "status", st);
									add_htag(r, "statusmsg", "Not Found");
									if(from) add_htag(r, "from", from);
									hsend(1, r);
									free_hmsg(r);
								break;
								case EPERM:
									r=new_hmsg("proc", "/403.htm"); // tail-recursive proc call
									hputshort(st, 403);
									add_htag(r, "status", st);
									add_htag(r, "statusmsg", "Forbidden");
									if(from) add_htag(r, "from", from);
									hsend(1, r);
									free_hmsg(r);
								break;
								default:
									r=new_hmsg("err", NULL);
									add_htag(r, "what", "open-failure");
									char en[TL_LONG];
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
							char *ext=strrchr(h->data, '.');
							if(ext&&strchr(ext, '/'))
							{
								ext=NULL;
							}
							if(ext)
							{
								if(hst->debug) fprintf(stderr, "horde: %s[%d]: sending request to ext\n", hst->name, getpid());
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
											exed=true;
										}
										else
										{
											hmsg h2=hmsg_from_str(inp2, true);
											if(h2)
											{
												if(hmsg_state(h2, hst));
												else if(strcmp(h2->funct, "ext")==0)
												{
													if(h2->data)
													{
														content_type=strdup(h2->data);
														exed=true;
													}
												}
												else if(strcmp(h2->funct, "err")==0)
												{
													if(hst->debug)
													{
														fprintf(stderr, "horde: %s[%d]: ext failed: %s\n", hst->name, getpid(), h2->funct);
														unsigned int i;
														for(i=0;i<h2->nparms;i++)
														{
															fprintf(stderr, "horde: %s[%d]:\t(%s|%s)\n", hst->name, getpid(), h2->p_tag[i], h2->p_value[i]);
															if(strcmp(h2->p_tag[i], "errno")==0)
															{
																fprintf(stderr, "horde: %s[%d]:\t\t%s\n", hst->name, getpid(), strerror(hgetlong(h2->p_value[i])));
															}
														}
														fprintf(stderr, "horde: %s[%d]:\t%s\n", hst->name, getpid(), h2->data);
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
													hst->shutdown=true;
													return(1);
												}
												free_hmsg(h2);
											}
										}
										free(inp2);
									}
									else
									{
										exed=true;
									}
								}
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
								lvars lv=NOVARS;
								l_addvar(&lv, "ext", l_str(ext?ext+1:""));
								l_addvar(&lv, "ctype", l_str(content_type?content_type:""));
								l_addvar(&lv, "body", l_blo(buf, length));
								hmsg r=new_hmsg_d("proc", buf, length);
								free(buf);
								if(content_type)
								{
									char *hctype=malloc(16+strlen(content_type)+16);
									if(hctype)
									{
										sprintf(hctype, "Content-Type: %s; charset=UTF-8", content_type);
										add_htag(h, "header", hctype);
										free(hctype);
									}
								}
								for(unsigned int i=0;i<h->nparms;i++)
								{
									if((strcmp(h->p_tag[i], "from")!=0)&&(strcmp(h->p_tag[i], "to")!=0))
										add_htag(r, h->p_tag[i], h->p_value[i]);
								}
								unsigned int proc;
								for(proc=0;proc<nprocs;proc++)
								{
									lvalue apply=l_eval(procs[proc].rule, lv, app);
									//if(hst->debug) fprintf(stderr, "horde: %s[%d]: processor %u: %s\n", hst->name, getpid(), proc, l_asbool(apply)?"match":"nomatch");
									if(l_asbool(apply))
									{
										hmsg fh=new_hmsg_d(procs[proc].functor, r->data, r->dlen);
										for(unsigned int i=0;i<r->nparms;i++)
										{
											if((strcmp(r->p_tag[i], "from")!=0)&&(strcmp(r->p_tag[i], "to")!=0))
												add_htag(fh, r->p_tag[i], r->p_value[i]);
										}
										add_htag(fh, "rqpath", h->data);
										hsend(1, fh);
										free_hmsg(fh);
										processed=true;
										bool brk=false;
										while(!brk)
										{
											char *resp=getl(STDIN_FILENO);
											if(resp)
											{
												if(*resp)
												{
													hmsg h2=hmsg_from_str(resp, true);
													if(h2)
													{
														if(strcmp(h2->funct, procs[proc].functor)==0)
														{
															if(h2->data)
															{
																free_hmsg(r);
																r=new_hmsg_d("proc", h2->data, h2->dlen);
																for(unsigned int i=0;i<h2->nparms;i++)
																{
																	if(strcmp(h2->p_tag[i], "to")==0);
																	else if(strcmp(h2->p_tag[i], "from")==0);
																	else
																		add_htag(r, h2->p_tag[i], h2->p_value[i]);
																}
																buf=malloc(h2->dlen+1);
																memcpy(buf, h2->data, h2->dlen);
																buf[h2->dlen]=0;
																l_addvar(&lv, "body", l_blo(buf, h2->dlen));
															}
															brk=true;
														}
														else if(strcmp(h2->funct, "err")==0)
														{
															if(hst->debug)
															{
																fprintf(stderr, "horde: %s[%d]: %s failed: %s\n", hst->name, getpid(), procs[proc].functor, h2->funct);
																unsigned int i;
																for(i=0;i<h2->nparms;i++)
																{
																	fprintf(stderr, "horde: %s[%d]:\t(%s|%s)\n", hst->name, getpid(), h2->p_tag[i], h2->p_value[i]);
																	if(strcmp(h2->p_tag[i], "errno")==0)
																	{
																		fprintf(stderr, "horde: %s[%d]:\t\t%s\n", hst->name, getpid(), strerror(hgetlong(h2->p_value[i])));
																	}
																}
																fprintf(stderr, "horde: %s[%d]:\t%s\n", hst->name, getpid(), h2->data);
															}
															hmsg eh=new_hmsg("err", inp);
															if(eh)
															{
																add_htag(eh, "what", "chld-failure");
																add_htag(eh, "fatal", NULL);
																add_htag(eh, "chld", procs[proc].functor);
																add_htag(eh, "err", resp);
																if(from) add_htag(eh, "to", from);
																hsend(1, eh);
																free_hmsg(eh);
															}
															hst->shutdown=true;
															return(1);
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
								free_lvars(&lv);
								if(!gettag(r, "status"))
								{
									char st[TL_SHORT];
									hputshort(st, status);
									add_htag(r, "status", st);
									add_htag(r, "statusmsg", statusmsg);
								}
								if(from) add_htag(r, "to", from);
								if(!processed)
								{
									free(r->data);
									r->data=NULL;
									r->dlen=0;
									add_htag(r, "read", path);
								}
								hsend(1, r);
								free_hmsg(r);
							}
						}
					}
				}
				free(path);
			}
			procdone:
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
			if(!rc)
			{
				hmsg eh=new_hmsg("err", inp);
				if(eh)
				{
					add_htag(eh, "what", "unrecognised-funct");
					if(from) add_htag(eh, "to", from);
					hsend(1, eh);
					free_hmsg(eh);
				}
			}
			e=1;
		}
		free_hmsg(h);
		return(e);
	}
	if(hst->debug)
		fprintf(stderr, "horde: %s[%d]: malformed message%s: %s\n", hst->name, getpid(), rc?" in rc file":"", inp);
	return(1);
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

lvalue app(lform lf, __attribute__((unused)) lvars lv)
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
