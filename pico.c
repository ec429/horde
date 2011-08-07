#define _GNU_SOURCE
/*
	horde: modular http server
	Copyright (C) 2011 Edward Cree

	Licensed under GNU GPLv3+; see top of horde.c for details
	
	pico: process <?pico> tags like httpico
*/

#include <stdio.h>
#include <stdbool.h>
#include <string.h>
#include <errno.h>

#include "bits.h"
#include "libhorde.h"

typedef struct
{
	const char *root;
}
pdata;

int handle(const char *inp, const char *name, char **root);
char *picofy(hmsg h, const char *name, pdata p);

bool debug, pipeline;

int main(int argc, char **argv)
{
	const char *name=argc?argv[0]:"pico";
	char *root=strdup("root");
	pipeline=false;
	debug=false;
	FILE *rc=fopen(".pico", "r");
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
			int e=handle(line, name, &root);
			free(line);
			if(e) break;
		}
		fclose(rc);
		if(debug) fprintf(stderr, "horde: %s[%d]: finished reading rc file\n", name, getpid());
	}
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
		unsigned long length=0;
		unsigned int i;
		for(i=0;i<h->nparms;i++)
		{
			if(strcmp(h->p_tag[i], "from")==0)
				from=h->p_value[i];
			else if(strcmp(h->p_tag[i], "length")==0)
				length=hgetlong(h->p_value[i]);
		}
		if(strcmp(h->funct, "pico")==0)
		{
			if(!h->data)
			{
				if(debug) fprintf(stderr, "horde: %s[%d]: missing data in (pico)\n", name, getpid());
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
				char *resp=picofy(h, name, (pdata){.root=*root});
				ssize_t length=strlen(resp);
				char *resph=hex_encode(resp, strlen(resp));
				free(resp);
				hmsg r=new_hmsg("pico", resph);
				free(resph);
				if(from) add_htag(r, "to", from);
				char ln[17];
				hputlong(ln, length);
				add_htag(r, "length", ln);
				hsend(1, r);
				free_hmsg(r);
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

char *picofy(hmsg h, const char *name, pdata p)
{
	const char *from=NULL;
	unsigned long length=0;
	for(unsigned int i=0;i<h->nparms;i++)
	{
		if(strcmp(h->p_tag[i], "from")==0)
			from=h->p_value[i];
		else if(strcmp(h->p_tag[i], "length")==0)
			length=hgetlong(h->p_value[i]);
	}
	char *rv;
	unsigned int l,i;
	init_char(&rv, &l, &i);
	char *data=hex_decode(h->data, length<<1);
	char *d=data;
	if(!d) return(NULL);
	while(*d)
	{
		if(strncmp(d, "<?pico", 6)==0)
		{
			d+=6;
			while(isspace(*d)) d++;
			char *e=strchr(d, '>');
			if(!e)
				append_char(&rv, &l, &i, *d++);
			else
			{
				*e++=0;
				char *f=strchr(d, '=');
				if(f)
				{
					*f++=0;
					if(*f++=='"')
					{
						char *g=strchr(f, '"');
						if(g)
						{
							*g=0;
						}
					}
				}
				// now d is the name and f is the format (if any)
				if(strcmp(d, "pfile")==0)
				{
					if(*f=='/')
					{
						FILE *pf=NULL;
						char *fn=malloc(strlen(f)+strlen(p.root)+1);
						if(fn)
						{
							sprintf(fn, "%s%s", p.root, f);
							pf=fopen(fn, "r");
						}
						if(pf)
						{
							char *pfd;
							ssize_t length=hslurp(pf, &pfd);
							fclose(pf);
							hmsg ph=new_hmsg("pico", pfd);
							free(pfd);
							for(unsigned int i=0;i<h->nparms;i++)
							{
								if(strcmp(h->p_tag[i], "length")==0)
								{
									char ln[17];
									hputlong(ln, length);
									add_htag(ph, "length", ln);
								}
								else
									add_htag(ph, h->p_tag[i], h->p_value[i]);
							}
							char *pd=picofy(ph, name, p);
							if(pd)
								append_str(&rv, &l, &i, pd);
							free(pd);
							free_hmsg(ph);
						}
					}
					
				}
				// jump over the tag
				d=e;
			}
		}
		else
			append_char(&rv, &l, &i, *d++);
	}
	free(data);
	return(rv);
}
