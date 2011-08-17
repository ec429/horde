#define _GNU_SOURCE
/*
	horde: modular http server
	Copyright (C) 2011 Edward Cree

	Licensed under GNU GPLv3+; see top of horde.c for details
	
	log: append lines to the logfileile
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
	FILE *rc=fopen(".log", "r");
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
		if(strcmp(h->funct, "log")==0)
		{
			if(h->data&&logfile)
			{
				FILE *f=fopen(logfile, "a");
				if(f)
				{
					fputs(h->data, f);
					fputc('\n', f);
					fclose(f);
				}
				else
				{
					fprintf(stderr, "horde: log[%d]: can't open log file\n", getpid());
				}
			}
		}
		else if(strcmp(h->funct, "shutdown")==0)
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
