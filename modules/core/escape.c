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

unsigned int nmaps=0;
map *maps=NULL;
char *defaultmap=NULL;

void handle(const char *inp, hstate *hst);
char *apply_map(const hmsg h, map m, size_t *len);

int main(int argc, char **argv)
{
	hstate hst;
	hst_init(&hst, argc?argv[0]:"escape", true);
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
			handle(line, &hst);
			free(line);
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
			const char *map=gettag(h, "map");
			if(!map) map=defaultmap;
			if(!map)
			{
				if(hst->debug) fprintf(stderr, "horde: %s[%d]: (escape) without map and no default set\n", hst->name, getpid());
				hmsg eh=new_hmsg("err", inp);
				if(eh)
				{
					add_htag(eh, "what", "missing-arg");
					if(from) add_htag(eh, "to", from);
					hsend(1, eh);
					free_hmsg(eh);
				}
			}
			else
			{
				unsigned int i;
				for(i=0;i<nmaps;i++)
					if(strcmp(map, maps[i].name)==0) break;
				if(i<nmaps)
				{
					size_t rlen=0;
					char *resp=apply_map(h, maps[i], &rlen);
					free(h->data);
					h->data=resp;
					h->dlen=rlen;
					for(unsigned int i=0;i<h->nparms;i++)
					{
						if(strcmp(h->p_tag[i], "from")==0)
							strcpy(h->p_tag[i], "to");
					}
					hsend(1, h);
				}
				else
				{
					if(hst->debug) fprintf(stderr, "horde: %s[%d]: unrecognised map `%s'\n", hst->name, getpid(), map);
					hmsg eh=new_hmsg("err", inp);
					if(eh)
					{
						add_htag(eh, "what", "unrecognised-arg");
						if(from) add_htag(eh, "to", from);
						hsend(1, eh);
						free_hmsg(eh);
					}
				}
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
				if(hst->debug) fprintf(stderr, "horde: %s[%d]: (add) without data, failing\n", hst->name, getpid());
				hst->shutdown=true;
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
				unsigned int n=nmaps++;
				map *newmaps=realloc(maps, nmaps*sizeof(*maps));
				if(!newmaps)
				{
					nmaps=n;
					hmsg eh=new_hmsg("err", inp);
					if(eh)
					{
						add_htag(eh, "what", "allocation-failure");
						if(from) add_htag(eh, "to", from);
						hsend(1, eh);
						free_hmsg(eh);
					}
				}
				else
				{
					(maps=newmaps)[n]=(map){.name=strdup(h->data), .nchars=h->nparms};
					if(!maps[n].name)
					{
						nmaps--;
						hmsg eh=new_hmsg("err", inp);
						if(eh)
						{
							add_htag(eh, "what", "allocation-failure");
							if(from) add_htag(eh, "to", from);
							hsend(1, eh);
							free_hmsg(eh);
						}
					}
					else if(!(maps[n].from=malloc(maps[n].nchars+1)))
					{
						free(maps[n].name);
						nmaps--;
						hmsg eh=new_hmsg("err", inp);
						if(eh)
						{
							add_htag(eh, "what", "allocation-failure");
							if(from) add_htag(eh, "to", from);
							hsend(1, eh);
							free_hmsg(eh);
						}
					}
					else if(!(maps[n].to=malloc(maps[n].nchars*sizeof(*maps[n].to))))
					{
						free(maps[n].name);
						free(maps[n].from);
						nmaps--;
						hmsg eh=new_hmsg("err", inp);
						if(eh)
						{
							add_htag(eh, "what", "allocation-failure");
							if(from) add_htag(eh, "to", from);
							hsend(1, eh);
							free_hmsg(eh);
						}
					}
					else
					{
						for(unsigned int i=0;i<h->nparms;i++)
						{
							maps[n].from[i]=h->p_tag[i][0];
							if(!(maps[n].to[i]=strdup(h->p_value[i])))
							{
								for(;i>0;i--)
									free(maps[n].to[i-1]);
								free(maps[n].to);
								free(maps[n].from);
								free(maps[n].name);
								nmaps--;
								hmsg eh=new_hmsg("err", inp);
								if(eh)
								{
									add_htag(eh, "what", "allocation-failure");
									if(from) add_htag(eh, "to", from);
									hsend(1, eh);
									free_hmsg(eh);
								}
								break;
							}
						}
					}
				}
			}
		}
		else if(strcmp(h->funct, "default")==0)
		{
			free(defaultmap);
			if(h->data)
			{
				if(!(defaultmap=strdup(h->data)))
				{
					hmsg eh=new_hmsg("err", inp);
					if(eh)
					{
						add_htag(eh, "what", "allocation-failure");
						if(from) add_htag(eh, "to", from);
						hsend(1, eh);
						free_hmsg(eh);
					}
				}
			}
			else
				defaultmap=NULL;
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

char *apply_map(const hmsg h, map m, size_t *len)
{
	char *rv; size_t l,i;
	init_char(&rv, &l, &i);
	size_t p;
	for(p=0;p<h->dlen;p++)
	{
		char c=h->data[p];
		char *f=strchr(m.from, c); // XXX bad things if one of the 'from' is \0
		if(f&&c)
		{
			unsigned int w=f-m.from;
			append_str(&rv, &l, &i, m.to[w]);
		}
		else
			append_char(&rv, &l, &i, c);
	}
	if(len) *len=i;
	return(rv);
}
