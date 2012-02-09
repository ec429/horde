#define _GNU_SOURCE
/*
	horde: modular http server
	Copyright (C) 2011 Edward Cree

	Licensed under GNU GPLv3+; see top of horde.c for details
	
	stats: keep track of simple server statistics (for now, just bytes_today)
*/

#include <stdio.h>
#include <stdbool.h>
#include <string.h>
#include <errno.h>

#include "bits.h"
#include "libhorde.h"

int handle(const char *inp);
size_t bytes_today;

int main(void)
{
	bytes_today=0;
	FILE *rc=fopen("modules/core/stats.rc", "r");
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
			const char /* *ip=gettag(h, "ip"), *st=gettag(h, "status"), *ac=gettag(h, "method"),*/ *sz=gettag(h, "bytes");
			if(sz)
			{
				size_t bytes=0;
				sscanf(sz, "%zu", &bytes);
				bytes_today+=bytes;
			}
			else
				fprintf(stderr, "horde: stats[%d]: sz is NULL\n", getpid());
		}
		else if(strcmp(h->funct, "stats")==0)
		{
			const char *from=gettag(h, "from");
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
				if(from) add_htag(u, "to", from);
				hsend(1, u);
				free_hmsg(u);
			}
			else
			{
				hmsg u=new_hmsg("err", "stats");
				add_htag(u, "what", "unrecognised-arg");
				if(from) add_htag(u, "to", from);
				hsend(1, u);
				free_hmsg(u);
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
		else if(strcmp(h->funct, "pulse")==0)
		{
			if(strcmp(h->data, "midnight")==0)
				bytes_today=0;
		}
		free_hmsg(h);
	}
	return(errupt);
}
