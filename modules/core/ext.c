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

int main(int argc, char **argv)
{
	hstate hst;
	hst_init(&hst, argc?argv[0]:"ext", true);
	lib.nuser=lib.nsystem=0;
	lib.user=lib.system=NULL;
	sys_mime_lib(hst.name);
	//fprintf(stderr, "horde: %s[%d]: built mime_lib; ready\n", hst.name, getpid());
	while(!hst.shutdown)
	{
		char *inp=getl(STDIN_FILENO);
		if(!inp) break;
		if(!*inp) {free(inp);continue;}
		hmsg h=hmsg_from_str(inp, true);
		free(inp);
		if(hmsg_state(h, &hst)) {free_hmsg(h);continue;}
		if(h)
		{
			const char *from=gettag(h, "from");
			if(strcmp(h->funct, "ext")==0)
			{
				if(hst.debug) fprintf(stderr, "horde: %s[%d]: looking up '%s'\n", hst.name, getpid(), h->data);
				char *ctype=mime_type(h->data);
				if(!ctype||!*ctype)
				{
					if(hst.debug) fprintf(stderr, "horde: %s[%d]: not found\n", hst.name, getpid());
					if(ctype) free(ctype);
					ctype=strdup("application/octet-stream");
				}
				if(!ctype)
				{
					if(hst.debug) fprintf(stderr, "horde: %s[%d]: allocation failure (ctype): strdup: %s\n", hst.name, getpid(), strerror(errno));
					hmsg r=new_hmsg("err", NULL);
					add_htag(r, "what", "mime-type");
					if(from) add_htag(r, "to", from);
					hsend(1, r);
					free_hmsg(r);
				}
				else
				{
					if(hst.debug) fprintf(stderr, "horde: %s[%d]: found; is %s\n", hst.name, getpid(), ctype);
					hmsg r=new_hmsg("ext", ctype);
					if(from) add_htag(r, "to", from);
					hsend(1, r);
					free_hmsg(r);
				}
				hmsg ready=new_hmsg("ready", NULL);
				hsend(1, ready);
				free_hmsg(ready);
			}
			else
			{
				if(hst.debug) fprintf(stderr, "horde: %s[%d]: unrecognised funct '%s'\n", hst.name, getpid(), h->funct);
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
					fprintf(stderr, "horde: %s[%d]: allocation failure while building mime_lib: realloc: %s\n", name, getpid(), strerror(errno));
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
							fprintf(stderr, "horde: %s[%d]: allocation failure while building mime_lib: realloc: %s\n", name, getpid(), strerror(errno));
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
