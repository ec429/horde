#define _GNU_SOURCE
/*
	horde: modular http server
	Copyright (C) 2011 Edward Cree

	Licensed under GNU GPLv3+; see top of horde.c for details
	
	path: provides path normalisation
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "bits.h"
#include "libhorde.h"

int main(int argc, char **argv)
{
	const char *name=argc?argv[0]:"path";
	int errupt=0;
	while(!errupt)
	{
		char *inp=getl(STDIN_FILENO);
		if(!inp) break;
		if(!*inp) {free(inp);continue;}
		hmsg h=hmsg_from_str(inp);
		if(h)
		{
			if(strcmp(h->funct, "path")==0)
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
				char *newpath=normalise_path(h->data);
				if(newpath&&!*newpath)
				{
					free(newpath);
					newpath=strdup("/");
				}
				if(!newpath)
				{
					hmsg r=new_hmsg("err", NULL);
					add_htag(r, "what", "normalise-path");
					if(from) add_htag(r, "to", from);
					hsend(1, r);
					free_hmsg(r);
				}
				else
				{
					hmsg r=new_hmsg("path", newpath);
					if(from) add_htag(r, "to", from);
					hsend(1, r);
					free_hmsg(r);
					free(newpath);
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