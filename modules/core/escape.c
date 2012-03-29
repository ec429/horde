#define _GNU_SOURCE
/*
	horde: modular http server
	Copyright (C) 2011 Edward Cree

	Licensed under GNU GPLv3+; see top of horde.c for details
	
	escape: performs various escapings of untrusted strings
*/

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <errno.h>

#include "bits.h"
#include "libhorde.h"

typedef struct
{
	char *name;
	unsigned int nchars;
	char *from;
	char **to;
}
map;

void handle(const char *inp);

int main(int argc, char **argv)
{
	hstate hst;
	hst_init(&hst, argc?argv[0]:"escape", true);
	unsigned int nmaps=0;
	map *maps=NULL;
	FILE *rc=fopen("modules/core/escape.rc", "r");
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
		if(strcmp(h->funct, "escape")==0)
		{
			if(!h->data)
			{
				h->data=strdup("");
				h->dlen=0;
			}
			else
			{
/*				char *resp=apply(h, hst);
				free(h->data);
				h->data=resp;
				h->dlen=strlen(resp);
				for(unsigned int i=0;i<h->nparms;i++)
				{
					if(strcmp(h->p_tag[i], "from")==0)
						strcpy(h->p_tag[i], "to");
				}
				add_htag(h, "server", "pico "PICO_VER);
				hsend(1, h);*/
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
		else if(strcmp(h->funct, "add")==0)
		{
			if(!h->data)
			{
				
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
