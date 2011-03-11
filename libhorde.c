#define _GNU_SOURCE
/*
	horde: modular http server
	Copyright (C) 2011 Edward Cree

	Licensed under GNU GPLv3+; see top of horde.c for details
	
	libhorde: provides routines for module communication
*/
#include "libhorde.h"

char *hex_encode(const char *src, size_t srclen)
{
	if(!src||!srclen)
		return(NULL);
	char *rv;unsigned int l,i;
	init_char(&rv, &l, &i);
	unsigned int p;
	for(p=0;p<srclen;p++)
	{
		unsigned char c=src[p];
		char hex[3];
		snprintf(hex, 3, "%02x", c);
		append_str(&rv, &l, &i, hex);
	}
	return(rv);
}

char *hex_decode(const char *src, size_t srclen)
{
	if(!src||!srclen||(srclen&1))
		return(NULL);
	char *rv;unsigned int l,i;
	init_char(&rv, &l, &i);
	unsigned int p;
	for(p=0;p<srclen;p+=2)
	{
		unsigned int c;
		const char hex[3]={src[p], src[p+1], 0};
		if(!isxdigit(hex[0])) return(rv);
		if(!isxdigit(hex[1])) return(rv);
		sscanf(hex, "%02x", &c);
		append_char(&rv, &l, &i, (unsigned char)c);
	}
	return(rv);
}

hmsg new_hmsg(const char *funct, const char *data)
{
	hmsg rv=malloc(sizeof(*rv));
	if(rv)
	{
		if(funct) rv->funct=strdup(funct);
		if(data) rv->data=strdup(data);
		rv->nparms=0;
		rv->p_tag=rv->p_value=NULL;
	}
	return(rv);
}

int add_htag(hmsg h, const char *p_tag, const char *p_value)
{
	if(!h) return(-1);
	unsigned int parm=h->nparms++;
	char **n_tag=realloc(h->p_tag, h->nparms*sizeof(*h->p_tag));
	if(!n_tag)
	{
		h->nparms=parm;
		return(-1);
	}
	h->p_tag=n_tag;
	char **n_value=realloc(h->p_value, h->nparms*sizeof(*h->p_value));
	if(!n_value)
	{
		h->nparms=parm;
		return(-1);
	}
	h->p_value=n_value;
	if(p_tag) h->p_tag[parm]=strdup(p_tag); else h->p_tag[parm]=NULL;
	if(p_value) h->p_value[parm]=strdup(p_value); else h->p_value[parm]=NULL;
	return(0);
}

char *str_from_hmsg(const hmsg h);
hmsg hmsg_from_str(const char *str);

void free_hmsg(hmsg h)
{
	if(!h) return;
	unsigned int i;
	for(i=0;i<h->nparms;i++)
	{
		if(h->p_tag&&h->p_tag[i]) free(h->p_tag[i]);
		if(h->p_value&&h->p_value[i]) free(h->p_value[i]);
	}
	if(h->p_tag) free(h->p_tag);
	if(h->p_value) free(h->p_value);
	if(h->funct) free(h->funct);
	if(h->data) free(h->data);
	free(h);
}
