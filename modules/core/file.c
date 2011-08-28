#define _GNU_SOURCE
/*
	horde: modular http server
	Copyright (C) 2011 Edward Cree

	Licensed under GNU GPLv3+; see top of horde.c for details
	
	file: determines Content-Types from magic numbers.
	Requires libmagic (debian packages libmagic1, libmagic-dev)
*/

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <errno.h>
#include <magic.h>

#include "bits.h"
#include "libhorde.h"

int main(int argc, char **argv)
{
	hstate hst;
	hst.name=argc?argv[0]:"file";
	hst.shutdown=false;
	hst.debug=false;
	hst.pipeline=true;
	magic_t filemagic=magic_open(MAGIC_SYMLINK|MAGIC_MIME_TYPE);
	if(filemagic==NULL)
	{
		fprintf(stderr, "horde: %s[%d]: magic_open: %s\n", hst.name, getpid(), magic_error(NULL));
		hfin(EXIT_FAILURE);
		return(EXIT_FAILURE);
	}
	if(magic_load(filemagic, NULL)!=0)
	{
		fprintf(stderr, "horde: %s[%d]: magic_load: %s\n", hst.name, getpid(), magic_error(filemagic));
		hfin(EXIT_FAILURE);
		return(EXIT_FAILURE);
	}
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
			if(strcmp(h->funct, "file")==0)
			{
				if(hst.debug) fprintf(stderr, "horde: %s[%d]: filemagic\n", hst.name, getpid());
				const char *ctype=magic_buffer(filemagic, h->data, h->dlen); 
				if(!ctype||!*ctype)
				{
					if(hst.debug) fprintf(stderr, "horde: %s[%d]: not found\n", hst.name, getpid());
					ctype="application/octet-stream";
				}
				if(hst.debug) fprintf(stderr, "horde: %s[%d]: found; is %s\n", hst.name, getpid(), ctype);
				for(unsigned int i=0;i<h->nparms;i++)
				{
					if(strcmp(h->p_tag[i], "from")==0)
						strcpy(h->p_tag[i], "to");
				}
				char *hctype=malloc(16+strlen(ctype)+16);
				if(hctype)
				{
					sprintf(hctype, "Content-Type: %s; charset=UTF-8", ctype);
					add_htag(h, "header", hctype);
					free(hctype);
				}
				hsend(1, h);
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
