#define _GNU_SOURCE
/*
	horde: modular http server
	Copyright (C) 2011 Edward Cree

	Licensed under GNU GPLv3+; see top of horde.c for details
	
	proc: processes files before serving
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

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
			if(strcmp(h->funct, "proc")==0)
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
											ced="<!DOCTYPE html PUBLIC \"-//W3C//DTD XHTML 1.0 Strict//EN\" \"http://www.w3.org/TR/xhtml1/DTD/xhtml1-strict.dtd\">\n<html><head>\n<title>404 -- Not Found</title>\n</head><body>\n<h1>HTTP Error 404: Not Found</h1>\n<p>The requested URL was not found on this server.</p>\n</body></html>";
										}
										else
										{
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
					free(path);
				}
			}
			else if(strcmp(h->funct, "shutdown")==0)
			{
				fprintf(stderr, "horde: %s[%d]: server is shutting down\n", name, getpid());
				errupt++;
			}
			free_hmsg(h);
		}
		free(inp);
	}
	hfin(EXIT_SUCCESS);
	return(EXIT_SUCCESS);
}
