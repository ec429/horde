#define _GNU_SOURCE
/*
	horde: modular http server
	Copyright (C) 2011 Edward Cree

	Licensed under GNU GPLv3+; see top of horde.c for details
	
	log: append lines to the logfile
*/

#include <stdio.h>
#include <stdbool.h>
#include <string.h>
#include <errno.h>

#include "bits.h"
#include "libhorde.h"

int handle(const char *inp);
char *logfile; // we don't use hstate because we're an 'only', and we're atypical

int main(void)
{
	logfile=NULL;
	FILE *rc=fopen("modules/core/log.rc", "r");
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
			int e=handle(line);
			free(line);
			if(e) break;
		}
		fclose(rc);
	}
	int errupt=0;
	while(!errupt)
	{
		char *inp=getl(STDIN_FILENO);
		if(!inp) break;
		if(!*inp) {free(inp);continue;}
		errupt=handle(inp);
		free(inp);
	}
	hfin(EXIT_SUCCESS);
	return(EXIT_SUCCESS);
}

int handle(const char *inp)
{
	int errupt=0;
	hmsg h=hmsg_from_str(inp, true);
	if(h)
	{
		if(strcmp(h->funct, "tail")==0)
		{
			if(logfile)
			{
				const char *ip=gettag(h, "ip"), *date=gettag(h, "date"), *st=gettag(h, "status"), *ac=gettag(h, "method"), *sz=gettag(h, "bytes"), *path=gettag(h, "rpath"), *ref=gettag(h, "referrer"), *ua=gettag(h, "user-agent");
				if(st&&(strcmp(st, "302")!=0)&&ip&&(strcmp(ip, "127.0.0.1")!=0)&&(strcmp(ip, "::1")!=0)) // Don't log 302 Found, nor localhost activity
				{
					FILE *f=fopen(logfile, "a");
					if(f)
					{
						fprintf(f, "%s\t%s\t%s %s [%s] %s\t%s\t%s\n", ip, date, st, ac, sz, path?path:"?", ref?ref:"--", ua?ua:"..");
						fclose(f);
					}
					else
					{
						fprintf(stderr, "horde: log[%d]: can't open log file\n", getpid());
					}
				}
			}
		}
		else if(strcmp(h->funct, "shutdown")==0)
		{
			errupt++;
		}
		else if(strcmp(h->funct, "kill")==0)
		{
			errupt++;
		}
		else if(strcmp(h->funct, "logf")==0)
		{
			if(h->data)
			{
				char *nf=strdup(h->data);
				if(!nf)
				{
					hfin(EXIT_FAILURE);
					return(EXIT_FAILURE);
				}
				if(logfile) free(logfile);
				logfile=nf;
			}
		}
		free_hmsg(h);
	}
	return(errupt);
}
