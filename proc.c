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

int main(int argc, char **argv)
{
	const char *name=argc?argv[0]:"proc";
	char *root=strdup("root");
	int errupt=0;
	while(!errupt)
	{
		char *inp=getl(STDIN_FILENO);
		if(!inp) break;
		if(!*inp) {free(inp);continue;}
		hmsg h=hmsg_from_str(inp);
		if(h)
		{
			const char *from=NULL;
			unsigned int i;
			for(i=0;i<h->nparms;i++)
			{
				if(strcmp(h->p_tag[i], "from")==0)
				{
					from=h->p_value[i];
					break;
				}
			}
			if(strcmp(h->funct, "proc")==0)
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
					char *path=malloc(strlen(root)+strlen(h->data)+1);
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
						strcpy(path, root);
						strcat(path, h->data);
						fprintf(stderr, "%s\n", path);
						bool isdir=false;
						struct stat stbuf;
						if(stat(path, &stbuf))
						{
							fprintf(stderr, "horde: %s[%d]: stat() failed: %s\n", name, getpid(), strerror(errno));
							hmsg r;
							switch(errno)
							{
								case ENOENT:;
									char *cep=malloc(strlen(root)+strlen("/404.htm")+1);
									if(!cep)
									{
										hmsg r=new_hmsg("err", NULL);
										add_htag(r, "what", "allocation-failure");
										if(from) add_htag(r, "to", from);
										hsend(1, r);
										free_hmsg(r);
									}
									else
									{
										strcpy(cep, root);
										strcat(cep, "/404.htm");
										FILE *ce=fopen(cep, "r");
										char *ced=NULL;
										if(!ce)
										{
											fprintf(stderr, "horde: %s[%d]: using static 404\n", name, getpid());
											ced="<!DOCTYPE html PUBLIC \"-//W3C//DTD XHTML 1.0 Strict//EN\" \"http://www.w3.org/TR/xhtml1/DTD/xhtml1-strict.dtd\">\n<html><head>\n<title>404 -- Not Found</title>\n</head><body>\n<h1>HTTP Error 404: Not Found</h1>\n<p>The requested URL was not found on this server.</p>\n</body></html>";
										}
										else
										{
											fprintf(stderr, "horde: %s[%d]: serving custom 404\n", name, getpid());
											ced=slurp(ce);
											fclose(ce);
										}
										r=new_hmsg("proc", ced);
										char st[9];
										hputshort(st, 404);
										add_htag(r, "status", st);
										add_htag(r, "statusmsg", "Not Found");
										if(from) add_htag(r, "to", from);
										hsend(1, r);
										free_hmsg(r);
										if(ce) free(ced);
										free(cep);
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
									hmsg r=new_hmsg("proc", NULL);
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
									case ENOENT:;
										char *cep=malloc(strlen(root)+strlen("/404.htm")+1);
										if(!cep)
										{
											hmsg r=new_hmsg("err", NULL);
											add_htag(r, "what", "allocation-failure");
											if(from) add_htag(r, "to", from);
											hsend(1, r);
											free_hmsg(r);
										}
										else
										{
											strcpy(cep, root);
											strcat(cep, "/404.htm");
											FILE *ce=fopen(cep, "r");
											char *ced=NULL;
											if(!ce)
											{
												fprintf(stderr, "horde: %s[%d]: using static 404\n", name, getpid());
												ced="<!DOCTYPE html PUBLIC \"-//W3C//DTD XHTML 1.0 Strict//EN\" \"http://www.w3.org/TR/xhtml1/DTD/xhtml1-strict.dtd\">\n<html><head>\n<title>404 -- Not Found</title>\n</head><body>\n<h1>HTTP Error 404: Not Found</h1>\n<p>The requested URL was not found on this server.</p>\n</body></html>";
											}
											else
											{
												fprintf(stderr, "horde: %s[%d]: serving custom 404\n", name, getpid());
												ced=slurp(ce);
												fclose(ce);
											}
											r=new_hmsg("proc", ced);
											char st[9];
											hputshort(st, 404);
											add_htag(r, "status", st);
											add_htag(r, "statusmsg", "Not Found");
											if(from) add_htag(r, "to", from);
											hsend(1, r);
											free_hmsg(r);
											if(ce) free(ced);
											free(cep);
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
							}
							else
							{
								char *buf=slurp(fp);
								fclose(fp);
								if(!buf)
								{
									hmsg r=new_hmsg("err", NULL);
									add_htag(r, "what", "allocation-failure");
									if(from) add_htag(r, "to", from);
									hsend(1, r);
									free_hmsg(r);
								}
								else
								{
									hmsg r=new_hmsg("proc", buf);
									if(from) add_htag(r, "to", from);
									hsend(1, r);
									free_hmsg(r);
								}
							}
						}
					}
					free(path);
				}
			}
			else if(strcmp(h->funct, "shutdown")==0)
			{
				fprintf(stderr, "horde: %s[%d]: server is shutting down\n", name, getpid());
				errupt++;
			}
			else if(strcmp(h->funct, "root")==0)
			{
				if(!h->data)
				{
					fprintf(stderr, "horde: %s[%d]: missing data in (root)\n", name, getpid());
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
						fprintf(stderr, "horde: %s[%d]: allocation failure (char *root): strdup: %s\n", name, getpid(), strerror(errno));
						hmsg eh=new_hmsg("err", inp);
						if(eh)
						{
							add_htag(eh, "what", "allocation-failure");
							add_htag(eh, "fatal", NULL);
							if(from) add_htag(eh, "to", from);
							hsend(1, eh);
							free_hmsg(eh);
							hfin(EXIT_FAILURE);
							return(EXIT_FAILURE);
						}
					}
					if(root) free(root);
					root=nr;
					fprintf(stderr, "horde: %s[%d]: root set to '%s'\n", name, getpid(), root);
				}
			}
			else
			{
				fprintf(stderr, "horde: %s[%d]: unrecognised funct '%s'\n", name, getpid(), h->funct);
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
		free(inp);
	}
	hfin(EXIT_SUCCESS);
	return(EXIT_SUCCESS);
}
