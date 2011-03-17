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
						bool isdir=false;
						struct stat stbuf;
						if(stat(path, &stbuf))
						{
							fprintf(stderr, "horde: %s[%d]: stat() failed: %s\n", name, getpid(), strerror(errno));
							hmsg r;
							switch(errno)
							{
								case ENOENT:;
									if(status==200)
									{
										r=new_hmsg("proc", "/404.htm"); // tail-recursive proc call
										char st[9];
										hputshort(st, 404);
										add_htag(r, "status", st);
										add_htag(r, "statusmsg", "Not Found");
										if(from) add_htag(r, "from", from);
										hsend(1, r);
										free_hmsg(r);
									}
									else if(status==403)
									{
										fprintf(stderr, "horde: %s[%d]: using static 404\n", name, getpid());
										r=new_hmsg("proc", "<!DOCTYPE html PUBLIC \"-//W3C//DTD XHTML 1.0 Strict//EN\" \"http://www.w3.org/TR/xhtml1/DTD/xhtml1-strict.dtd\">\n<html><head>\n<title>404 -- Not Found</title>\n</head><body>\n<h1>HTTP Error 403: Forbidden</h1>\n<p>You don't have permission to view the requested resource.</p>\n</body></html>");
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
										fprintf(stderr, "horde: %s[%d]: using static 404\n", name, getpid());
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
										fprintf(stderr, "horde: %s[%d]: using semi-static HTTP%hu\n", name, getpid(), status);
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
											sprintf(sed, "<!DOCTYPE html PUBLIC \"-//W3C//DTD XHTML 1.0 Strict//EN\" \"http://www.w3.org/TR/xhtml1/DTD/xhtml1-strict.dtd\">\n<html><head>\n<title>404 -- Not Found</title>\n</head><body>\n<h1>HTTP Error %hu: %s</h1>\n<p>The above error occurred while trying to process the request.  Furthermore, no default or custom error page matching the error in question was found.</p>\n</body></html>", status, statusmsg);
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
									if(from) add_htag(r, "from", from);
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
										if(from) add_htag(r, "from", from);
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
								char *buf;
								ssize_t length=hslurp(fp, &buf);
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
									char st[9];
									hputshort(st, status);
									add_htag(r, "status", st);
									add_htag(r, "statusmsg", statusmsg);
									char ln[17];
									hputlong(ln, length);
									add_htag(r, "length", ln);
									char *ext=h->data, *last=NULL;
									while((ext=strchr(ext, '.'))) last=ext++;
									if((ext=last))
									{
										fprintf(stderr, "horde: %s[%d]: sending request to ext\n", name, getpid());
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
													add_htag(r, "header", "Content-Type: application/octet-stream");
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
																ext=malloc(16+strlen(h2->data));
																if(!ext)
																{
																	fprintf(stderr, "horde: %s[%d]: allocation failure (char *ext): malloc: %s\n", name, getpid(), strerror(errno));
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
																else
																{
																	sprintf(ext, "Content-Type: %s", h2->data);
																	add_htag(r, "header", ext);
																	free(ext);
																	exed=true;
																}
															}
														}
														else if(strcmp(h2->funct, "err")==0)
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
												add_htag(r, "header", "Content-Type: application/octet-stream");
												exed=true;
											}
										}
									}
									else
									{
										add_htag(r, "header", "Content-Type: text/plain");
									}
									if(from) add_htag(r, "to", from);
									hsend(1, r);
									free(buf);
									free_hmsg(r);
								}
							}
						}
					}
					free(path);
				}
				fprintf(stderr, "horde: %s[%d]: finished service, not making self available again\n", name, getpid());
				errupt++;
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
						}
						hfin(EXIT_FAILURE);
						return(EXIT_FAILURE);
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
