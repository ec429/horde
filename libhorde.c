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

char *str_from_hmsg(const hmsg h)
{
	if(!h) return(strdup("(nil)"));
	char *rv; unsigned int l,i;
	init_char(&rv, &l, &i);
	append_char(&rv, &l, &i, '(');
	append_str(&rv, &l, &i, h->funct?h->funct:"(nil)");
	unsigned int p;
	for(p=0;p<h->nparms;p++)
	{
		append_char(&rv, &l, &i, ' ');
		append_char(&rv, &l, &i, '(');
		append_str(&rv, &l, &i, h->p_tag&&h->p_tag[p]?h->p_tag[p]:"(nil)");
		if(h->p_value&&h->p_value[p])
		{
			char *val=hex_encode(h->p_value[p], strlen(h->p_value[p]));
			if(val)
			{
				append_char(&rv, &l, &i, ' ');
				append_char(&rv, &l, &i, '#');
				append_str(&rv, &l, &i, val);
			}
		}
		append_char(&rv, &l, &i, ')');
	}
	if(h->data)
	{
		char *val=hex_encode(h->data, strlen(h->data));
		if(val)
		{
			append_char(&rv, &l, &i, ' ');
			append_char(&rv, &l, &i, '#');
			append_str(&rv, &l, &i, val);
		}
	}
	append_char(&rv, &l, &i, ')');
	return(rv);
}

hmsg hmsg_from_str(const char *str)
{
	const char *p=str, *funct=NULL, /**tag=NULL,*/ *curr=NULL;
	char *ff=NULL;
	unsigned int state=0;
	hmsg rv=NULL;
	while((*p)&&(state!=1024))
	{
		switch(state)
		{
			case 0:
				if(*p=='(')
				{
					state=1;
					funct=p+1;
				}
			break;
			case 1:
				if(isspace(*p))
				{
					if(*(p-1)=='(')
					{
						state=1024;
						break;
					}
					state=2;
					ff=strndup(funct, p-funct-1);
				}
				else if((*p=='(')||(*p==')')) // bad paren
				{
					fprintf(stderr, "hmsg_from_str: bad paren in input\n\t%s\n", str);
					state=1024;
					break;
				}
			break;
			case 2:
				if(!isspace(*p))
				{
					rv=new_hmsg(ff, NULL);
					if(!rv)
					{
						fprintf(stderr, "hmsg_from_str: allocation failure\n");
						perror("\tnew_hmsg");
						state=1024;
						break;
					}
					if(*p=='(')
					{
						state=3;
					}
					else
					{
						state=4;
					}
				}
			break;
			case 4:
				switch(*p)
				{
					case '(':
						fprintf(stderr, "hmsg_from_str: bad paren in input\n\t%s\n", str);
						state=1024;
						break;
					break;
					case ')':
						return(rv); // no data segment
					break;
					case '#':
						state=5;
						curr=p+1;
					break;
					default:
						if(!isspace(*p))
						{
							fprintf(stderr, "hmsg_from_str: malformed input\n\t%s\n", str);
							state=1024;
						}
					break;
				}
			break;
			case 5:
				if(*p==')')
				{
					rv->data=hex_decode(curr, p-curr-1);
					return(rv);
				}
			break;
			default:
				fprintf(stderr, "hmsg_from_str: internal error: bad state %u in parser\n", state);
				state=1024;
			break;
		}
		p++;
	}
	if(ff) free(ff);
	if(rv) free_hmsg(rv);
	return(NULL);
}

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

void hfin(unsigned char status)
{
	char st[4];
	snprintf(st, 4, "%hhu", status);
	hmsg fin=new_hmsg("fin", st);
	if(!fin) return;
	char *str=str_from_hmsg(fin);
	if(str)
	{
		printf("%s\n", str);
		free(str);
	}
	free_hmsg(fin);
}
