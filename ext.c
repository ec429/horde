#define _GNU_SOURCE
/*
	horde: modular http server
	Copyright (C) 2011 Edward Cree

	Licensed under GNU GPLv3+; see top of horde.c for details
	
	ext: determines Content-Types from extensions
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
	char *mime_type;
	unsigned int nexts;
	char **ext;
}
mime_ent;

typedef struct
{
	unsigned int nuser, nsystem;
	mime_ent *user, *system;
}
mime_lib;

char *mime_type(const char *ext);
void sys_mime_lib(const char *name);

mime_lib lib;

bool debug;

int main(int argc, char **argv)
{
	const char *name=argc?argv[0]:"ext";
	debug=false;
	lib.nuser=lib.nsystem=0;
	lib.user=lib.system=NULL;
	sys_mime_lib(name);
	//fprintf(stderr, "horde: %s[%d]: built mime_lib; ready\n", name, getpid());
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
			if(strcmp(h->funct, "ext")==0)
			{
				if(debug) fprintf(stderr, "horde: %s[%d]: looking up '%s'\n", name, getpid(), h->data);
				char *ctype=mime_type(h->data);
				if(!ctype||!*ctype)
				{
					if(debug) fprintf(stderr, "horde: %s[%d]: not found\n", name, getpid());
					if(ctype) free(ctype);
					ctype=strdup("application/octet-stream");
				}
				if(!ctype)
				{
					if(debug) fprintf(stderr, "horde: %s[%d]: allocation failure (ctype): strdup: %s\n", name, getpid(), strerror(errno));
					hmsg r=new_hmsg("err", NULL);
					add_htag(r, "what", "mime-type");
					if(from) add_htag(r, "to", from);
					hsend(1, r);
					free_hmsg(r);
				}
				else
				{
					if(debug) fprintf(stderr, "horde: %s[%d]: found; is %s\n", name, getpid(), ctype);
					hmsg r=new_hmsg("ext", ctype);
					if(from) add_htag(r, "to", from);
					hsend(1, r);
					free_hmsg(r);
				}
				hmsg ready=new_hmsg("ready", NULL);
				hsend(1, ready);
				free_hmsg(ready);
			}
			else if(strcmp(h->funct, "shutdown")==0)
			{
				if(debug) fprintf(stderr, "horde: %s[%d]: server is shutting down\n", name, getpid());
				errupt++;
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
		free(inp);
	}
	hfin(EXIT_SUCCESS);
	return(EXIT_SUCCESS);
}

void sys_mime_lib(const char *name)
{
	FILE *fp=fopen("/etc/mime.types", "r");
	if(!fp) return;
	char *line;
	while((!feof(fp))&&(line=fgetl(fp)))
	{
		if(*line!='#')
		{
			char *mime=strtok(line, " \t");
			if(mime)
			{
				unsigned int nsystem=lib.nsystem++;
				mime_ent *ns=realloc(lib.system, lib.nsystem*sizeof(mime_ent));
				if(!ns)
				{
					lib.nsystem=nsystem;
					fprintf(stderr, "horde: %s[%d]: allocation failure wihle building mime_lib: realloc: %s\n", name, getpid(), strerror(errno));
				}
				else
				{
					mime_ent new=(mime_ent){.mime_type=strdup(mime), .nexts=0, .ext=NULL};
					char *ext;
					while((ext=strtok(NULL, " \t")))
					{
						unsigned int next=new.nexts++;
						char **ne=realloc(new.ext, new.nexts*sizeof(char *));
						if(!ne)
						{
							new.nexts=next;
							fprintf(stderr, "horde: %s[%d]: allocation failure wihle building mime_lib: realloc: %s\n", name, getpid(), strerror(errno));
						}
						else
						{
							new.ext=ne;
							ne[next]=strdup(ext);
						}
					}
					(lib.system=ns)[nsystem]=new;
				}
			}
		}
		free(line);
	}
	fclose(fp);
}

char *mime_type(const char *ext)
{
	unsigned int i,j;
	for(i=0;i<lib.nuser;i++)
	{
		for(j=0;j<lib.user[i].nexts;j++)
		{
			if(strcmp(ext, lib.user[i].ext[j])==0)
			{
				return(lib.user[i].mime_type);
			}
		}
	}
	for(i=0;i<lib.nsystem;i++)
	{
		for(j=0;j<lib.system[i].nexts;j++)
		{
			if(strcmp(ext, lib.system[i].ext[j])==0)
			{
				return(lib.system[i].mime_type);
			}
		}
	}
	return(NULL);
}
