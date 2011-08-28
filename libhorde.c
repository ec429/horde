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
		if(!isxdigit(hex[0])) {free(rv); return(NULL);}
		if(!isxdigit(hex[1])) {free(rv); return(NULL);}
		sscanf(hex, "%02x", &c);
		append_char(&rv, &l, &i, (unsigned char)c);
	}
	return(rv);
}

void hputlong(char *buf, unsigned long val)
{
	snprintf(buf, TL_LONG-1, "%lu", val);
}

unsigned long hgetlong(const char *buf)
{
	unsigned long rv;
	sscanf(buf, "%lu", &rv);
	return(rv);
}

void hputshort(char *buf, unsigned short val)
{
	snprintf(buf, TL_SHORT-1, "%hu", val);
}

unsigned short hgetshort(const char *buf)
{
	unsigned short rv;
	sscanf(buf, "%hu", &rv);
	return(rv);
}

ssize_t sendall(int sockfd, const void *buf, size_t length, int flags)
{
	const char *p=buf;
	ssize_t left=length;
	ssize_t n;
	while((n=send(sockfd, p, left, flags))<left)
	{
		if(n<0)
			return(n);
		p+=n;
		left-=n;
	}
	return(0);
}

hmsg new_hmsg(const char *funct, const char *data)
{
	hmsg rv=malloc(sizeof(*rv));
	if(rv)
	{
		if(funct) rv->funct=strdup(funct); else rv->funct=NULL;
		if(data)
		{
			rv->data=strdup(data);
			rv->dlen=strlen(data);
		}
		else
		{
			rv->data=NULL;
			rv->dlen=0;
		}
		rv->nparms=0;
		rv->p_tag=rv->p_value=NULL;
	}
	return(rv);
}

hmsg new_hmsg_d(const char *funct, const char *data, size_t dlen)
{
	hmsg rv=malloc(sizeof(*rv));
	if(rv)
	{
		if(funct) rv->funct=strdup(funct); else rv->funct=NULL;
		if(data)
		{
			rv->dlen=dlen;
			rv->data=malloc(dlen+1);
			memcpy(rv->data, data, dlen);
			rv->data[dlen]=0;
		}
		else
		{
			rv->dlen=0;
			rv->data=NULL;
		}
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
			if((h->p_value[p][0]=='#') || (strpbrk(h->p_value[p], "( )")))
			{
				char *val=hex_encode(h->p_value[p], strlen(h->p_value[p]));
				if(val)
				{
					append_char(&rv, &l, &i, ' ');
					append_char(&rv, &l, &i, '#');
					append_str(&rv, &l, &i, val);
					free(val);
				}
			}
			else
			{
				append_char(&rv, &l, &i, ' ');
				append_str(&rv, &l, &i, h->p_value[p]);
			}
		}
		append_char(&rv, &l, &i, ')');
	}
	if(h->data)
	{
		if((h->data[0]=='#') || (strpbrk(h->data, "( )")) || (h->dlen!=strlen(h->data)))
		{
			char *val=hex_encode(h->data, h->dlen);
			if(val)
			{
				append_char(&rv, &l, &i, ' ');
				append_char(&rv, &l, &i, '#');
				append_str(&rv, &l, &i, val);
				free(val);
			}
		}
		else
		{
			append_char(&rv, &l, &i, ' ');
			append_str(&rv, &l, &i, h->data);
		}
	}
	append_char(&rv, &l, &i, ')');
	return(rv);
}

hmsg hmsg_from_str(const char *str, bool read)
{
	const char *p=str, *funct=NULL, *tag=NULL, *tage=NULL, *curr=NULL;
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
					ff=strndup(funct, p-funct);
				}
				else if(*p=='(') // bad paren
				{
					fprintf(stderr, "hmsg_from_str: bad paren in funct\n\t%s\n", str);
					state=1024;
					break;
				}
				else if(*p==')')
				{
					ff=strndup(funct, p-funct);
					if(!ff)
					{
						fprintf(stderr, "hmsg_from_str: allocation failure\n");
						perror("\tstrndup");
						state=1024;
						break;
					}
					rv=new_hmsg(ff, NULL);
					if(!rv)
					{
						fprintf(stderr, "hmsg_from_str: allocation failure\n");
						perror("\tnew_hmsg");
						state=1024;
						break;
					}
					return(rv);
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
						state=3;tag=p+1;
					}
					else
					{
						state=4;p--;
					}
				}
			break;
			case 3:
				if(isspace(*p))
				{
					tage=p;
					state=6;
				}
				else if(*p==')')
				{
					char *htag=strndup(tag, p-tag);
					add_htag(rv, htag, NULL);
					free(htag);
					state=7;
				}
			break;
			case 4:
				switch(*p)
				{
					case '(':
						fprintf(stderr, "hmsg_from_str: bad paren in data\n\t%s\n", str);
						state=1024;
						break;
					break;
					case ')':
						return(read?hmsg_read(rv):rv); // no data segment
					break;
					case '#':
						state=5;
						curr=p+1;
					break;
					default:
						if(!isspace(*p))
						{
							state=9;
							curr=p;
						}
					break;
				}
			break;
			case 5:
				if(*p==')')
				{
					rv->data=hex_decode(curr, p-curr);
					rv->dlen=(p-curr)>>1;
					return(rv);
				}
			break;
			case 6:
				switch(*p)
				{
					case '(':
						fprintf(stderr, "hmsg_from_str: bad paren in data\n\t%s\n", str);
						state=1024;
						break;
					break;
					case ')':;
						char *htag=strndup(tag, p-tag);
						add_htag(rv, htag, NULL);
						free(htag);
						state=7;
					break;
					case '#':
						state=8;
						curr=p+1;
					break;
					default:
						if(!isspace(*p))
						{
							state=10;
							curr=p;
						}
					break;
				}
			break;
			case 7:
				if(!isspace(*p))
				{
					if(*p=='(')
					{
						state=3;tag=p+1;
					}
					else
					{
						state=4;p--;
					}
				}
			break;
			case 8:
				if(*p==')')
				{
					char *htag=strndup(tag, tage-tag);
					char *hval=hex_decode(curr, p-curr);
					add_htag(rv, htag, hval);
					if(htag) free(htag);
					if(hval) free(hval);
					state=7;
				}
			break;
			case 9:
				if(*p==')')
				{
					rv->data=strndup(curr, p-curr);
					rv->dlen=p-curr;
					return(rv);
				}
			break;
			case 10:
				if(*p==')')
				{
					char *htag=strndup(tag, tage-tag);
					char *hval=strndup(curr, p-curr);
					add_htag(rv, htag, hval);
					if(htag) free(htag);
					if(hval) free(hval);
					state=7;
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

hmsg hmsg_read(hmsg h)
{
	if(!h) return(NULL);
	if(h->data) return(h);
	for(unsigned int i=0;i<h->nparms;i++)
	{
		if(!h->p_tag[i]) continue;
		if(strcmp(h->p_tag[i], "read")) continue;
		FILE *fp=fopen(h->p_value[i], "r");
		if(!fp) continue;
		h->dlen=dslurp(fp, &h->data);
		fclose(fp);
		break;
	}
	return(h);
}

bool hmsg_state(hmsg h, hstate *s)
{
	if(!h) return(false);
	if(!h->funct) return(false);
	if(!s) return(false);
	if(strcmp(h->funct, "shutdown")==0)
	{
		if(s->debug) fprintf(stderr, "horde: %s[%d]: server is shutting down\n", s->name, getpid());
		s->shutdown=true;
		return(true);
	}
	if(strcmp(h->funct, "debug")==0)
	{
		if(h->data)
		{
			if(strcmp(h->data, "true")==0)
				s->debug=true;
			else if(strcmp(h->data, "false")==0)
				s->debug=false;
		}
		else
			s->debug=true;
		return(true);
	}
	if(strcmp(h->funct, "pipeline")==0)
	{
		if(h->data)
		{
			if(strcmp(h->data, "true")==0)
				s->pipeline=true;
			else if(strcmp(h->data, "false")==0)
				s->pipeline=false;
		}
		else
			s->pipeline=true;
		return(true);
	}
	if(strcmp(h->funct, "root")==0)
	{
		if(!h->data)
		{
			if(s->debug) fprintf(stderr, "horde: %s[%d]: missing data in (root)\n", s->name, getpid());
			hmsg eh=new_hmsg("err", NULL);
			if(eh)
			{
				add_htag(eh, "what", "missing-data");
				const char *from=gettag(h, "from");
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
				if(s->debug) fprintf(stderr, "horde: %s[%d]: allocation failure (char *root): strdup: %s\n", s->name, getpid(), strerror(errno));
				hmsg eh=new_hmsg("err", NULL);
				if(eh)
				{
					add_htag(eh, "what", "allocation-failure");
					add_htag(eh, "fatal", NULL);
					const char *from=gettag(h, "from");
					if(from) add_htag(eh, "to", from);
					hsend(1, eh);
					free_hmsg(eh);
				}
				hfin(EXIT_FAILURE);
				exit(EXIT_FAILURE); // shouldn't really happen
			}
			if(s->root) free(s->root);
			s->root=nr;
			if(s->debug) fprintf(stderr, "horde: %s[%d]: root set to '%s'\n", s->name, getpid(), s->root);
		}
		return(true);
	}
	return(false);
}

const char *gettag(hmsg h, const char *tag)
{
	if(!h) return(NULL);
	if(!tag) return(NULL);
	for(unsigned int i=0;i<h->nparms;i++)
	{
		if(!h->p_tag[i]) continue;
		if(strcmp(h->p_tag[i], tag)) continue;
		if(h->p_value[i]) return(h->p_value[i]);
		return("true"); // for implicit tags
	}
	return(NULL);
}

void free_hmsg(hmsg h)
{
	if(!h) return;
	for(unsigned int i=0;i<h->nparms;i++)
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

lform new_lform(const char *funct)
{
	lform rv=malloc(sizeof(*rv));
	if(rv)
	{
		if(funct) rv->funct=strdup(funct);
		rv->nchld=0;
		rv->chld=NULL;
	}
	return(rv);
}

int add_lchld(lform lf, lform chld)
{
	if(!lf) return(-1);
	if(!chld) return(-1);
	unsigned int nchld=lf->nchld++;
	lform nc=realloc(lf->chld, lf->nchld*sizeof(*nc));
	if(!nc)
	{
		lf->nchld=nchld;
		return(-1);
	}
	(lf->chld=nc)[nchld]=*chld;
	return(0);
}

lform lform_str(const char *str, const char **end)
{
	const char *p=str, *funct=NULL;
	unsigned int state=0;
	lform rv=new_lform(NULL);
	while(p&&(*p)&&(state!=1024))
	{
		switch(state)
		{
			case 0:
				if(*p=='[')
				{
					state=1;
					funct=p+1;
				}
			break;
			case 1:
				if(isspace(*p))
				{
					rv->funct=strndup(funct, p-funct);
					state=2;
				}
				else if(*p==']')
				{
					rv->funct=strndup(funct, p-funct);
					state=1024;
				}
			break;
			case 2:
				if(!isspace(*p))
				{
					if(*p=='[')
					{
						const char *chend=NULL;
						lform chld=lform_str(p, &chend);
						p=chend-1;
						if(add_lchld(rv, chld))
							free_lform(chld);
						else
							free(chld); // shallow only, because we only did a shallow copy
					}
					else if(*p==']')
					{
						state=1024;
					}
				}
			break;
			default:
				fprintf(stderr, "lform_str: internal error: bad state %u in parser\n", state);
				state=1024;
			break;
		}
		p++;
	}
	if(end) *end=p;
	return(rv);
}

char *str_lform(const lform lf)
{
	char *rv; unsigned int l,i;
	init_char(&rv, &l, &i);
	if(lf)
	{
		append_char(&rv, &l, &i, '[');
		if(lf->funct)
		{
			append_str(&rv, &l, &i, lf->funct);
			append_char(&rv, &l, &i, ' ');
		}
		unsigned int c;
		for(c=0;c<lf->nchld;c++)
		{
			char *chld=str_lform(&lf->chld[c]);
			if(chld)
			{
				append_str(&rv, &l, &i, chld);
				free(chld);
			}
		}
		append_char(&rv, &l, &i, ']');
	}
	return(rv);
}

void _free_lform(lform lf)
{
	if(!lf) return;
	if(lf->funct) free(lf->funct);
	if(!lf->chld) return;
	unsigned int c;
	for(c=0;c<lf->nchld;c++)
	{
		_free_lform(&lf->chld[c]);
	}
	free(lf->chld);
}

void free_lform(lform lf)
{
	_free_lform(lf);
	free(lf);
}

lvalue l_eval(lform lf, lvars lv, lvalue app(lform lf, lvars lv))
{
	//fprintf(stderr, "l_eval %p (%s)\n", (void *)lf, lf?lf->funct:NULL);
	if(!lf)
		return(l_str(NULL));
	if(!lf->funct)
		return(l_str(NULL));
	if(lf->nchld&&!lf->chld)
		return(l_str(NULL));
	if(strcmp(lf->funct, "=")==0)
	{
		if(!lf->nchld) return(l_num(-1));
		lvalue first=l_eval(&lf->chld[0], lv, app);
		unsigned int chld;
		for(chld=1;chld<lf->nchld;chld++)
		{
			lvalue res=l_eval(&lf->chld[chld], lv, app);
			if(res.type!=first.type)
			{
				free_lvalue(first);
				free_lvalue(res);
				return(l_num(0));
			}
			switch(first.type)
			{
				case L_NUM:
					if(res.data.num!=first.data.num) return(l_num(0));
				break;
				case L_STR:
					if(res.data.str&&!first.data.str) {free_lvalue(first); free_lvalue(res); return(l_num(0));}
					if(!res.data.str&&first.data.str) {free_lvalue(first); free_lvalue(res); return(l_num(0));}
					if(res.data.str&&first.data.str&&strcmp(res.data.str, first.data.str)) {free_lvalue(first); free_lvalue(res); return(l_num(0));}
					free_lvalue(res);
				break;
				case L_BLO:
					if(res.data.blo.len!=first.data.blo.len) {free_lvalue(first); free_lvalue(res); return(l_num(0));}
					if(!res.data.str||!first.data.str) {free_lvalue(first); free_lvalue(res); return(l_num(0));} // error case
					if(memcmp(res.data.blo.bytes, first.data.blo.bytes, res.data.blo.len)) {free_lvalue(first); free_lvalue(res); return(l_num(0));}
					free_lvalue(res);
				break;
			}
		}
		free_lvalue(first);
		return(l_num(-1));
	}
	else if(strcmp(lf->funct, "and")==0)
	{
		unsigned int chld;
		for(chld=0;chld<lf->nchld;chld++)
		{
			lvalue res=l_eval(&lf->chld[chld], lv, app);
			bool b=l_asbool(res);
			free_lvalue(res);
			if(!b)
				return(l_num(0));
		}
		return(l_num(-1));
	}
	else if(strcmp(lf->funct, "or")==0)
	{
		unsigned int chld;
		for(chld=0;chld<lf->nchld;chld++)
		{
			lvalue res=l_eval(&lf->chld[chld], lv, app);
			bool b=l_asbool(res);
			free_lvalue(res);
			if(b)
				return(l_num(-1));
		}
		return(l_num(0));
	}
	else if(strcmp(lf->funct, "not")==0)
	{
		if(lf->nchld==1)
		{
			lvalue res=l_eval(&lf->chld[0], lv, app);
			bool b=l_asbool(res);
			free_lvalue(res);
			if(!b)
				return(l_num(-1));
		}
		return(l_num(0));
	}
	else if(strcmp(lf->funct, "grep")==0)
	{
		if(!lf->nchld) return(l_str(NULL));
		lvalue pattern=l_eval(&lf->chld[0], lv, app);
		if(pattern.type!=L_STR) {free_lvalue(pattern); return(l_str(NULL));}
		regex_t reg;
		if(regcomp(&reg, pattern.data.str, REG_NOSUB|REG_ICASE)) {free_lvalue(pattern); return(l_str(NULL));}
		free_lvalue(pattern);
		unsigned int chld;
		for(chld=1;chld<lf->nchld;chld++)
		{
			lvalue res=l_eval(&lf->chld[chld], lv, app);
			if(res.type!=L_STR) continue;
			if(regexec(&reg, res.data.str, 0, NULL, 0)==0)
			{
				free_lvalue(res);
				regfree(&reg);
				return(l_num(-1));
			}
			free_lvalue(res);
		}
		regfree(&reg);
		return(l_num(0));
	}
	else if(strcmp(lf->funct, "egrep")==0)
	{
		if(!lf->nchld) return(l_str(NULL));
		lvalue pattern=l_eval(&lf->chld[0], lv, app);
		if(pattern.type!=L_STR) {free_lvalue(pattern); return(l_str(NULL));}
		regex_t reg;
		if(regcomp(&reg, pattern.data.str, REG_NOSUB|REG_EXTENDED|REG_ICASE)) {free_lvalue(pattern); return(l_str(NULL));}
		free_lvalue(pattern);
		unsigned int chld;
		for(chld=1;chld<lf->nchld;chld++)
		{
			lvalue res=l_eval(&lf->chld[chld], lv, app);
			if(res.type!=L_STR) continue;
			if(regexec(&reg, res.data.str, 0, NULL, 0)==0)
			{
				free_lvalue(res);
				regfree(&reg);
				return(l_num(-1));
			}
			free_lvalue(res);
		}
		regfree(&reg);
		return(l_num(0));
	}
	else if(strcmp(lf->funct, "Grep")==0)
	{
		if(!lf->nchld) return(l_str(NULL));
		lvalue pattern=l_eval(&lf->chld[0], lv, app);
		if(pattern.type!=L_STR) {free_lvalue(pattern); return(l_str(NULL));}
		regex_t reg;
		if(regcomp(&reg, pattern.data.str, REG_NOSUB)) {free_lvalue(pattern); return(l_str(NULL));}
		free_lvalue(pattern);
		unsigned int chld;
		for(chld=1;chld<lf->nchld;chld++)
		{
			lvalue res=l_eval(&lf->chld[chld], lv, app);
			if(res.type!=L_STR) continue;
			if(regexec(&reg, res.data.str, 0, NULL, 0)==0)
			{
				free_lvalue(res);
				regfree(&reg);
				return(l_num(-1));
			}
			free_lvalue(res);
		}
		regfree(&reg);
		return(l_num(0));
	}
	else if(strcmp(lf->funct, "eGrep")==0)
	{
		if(!lf->nchld) return(l_str(NULL));
		lvalue pattern=l_eval(&lf->chld[0], lv, app);
		if(pattern.type!=L_STR) {free_lvalue(pattern); return(l_str(NULL));}
		regex_t reg;
		if(regcomp(&reg, pattern.data.str, REG_NOSUB|REG_EXTENDED)) {free_lvalue(pattern); return(l_str(NULL));}
		free_lvalue(pattern);
		unsigned int chld;
		for(chld=1;chld<lf->nchld;chld++)
		{
			lvalue res=l_eval(&lf->chld[chld], lv, app);
			if(res.type!=L_STR) continue;
			if(regexec(&reg, res.data.str, 0, NULL, 0)==0)
			{
				free_lvalue(res);
				regfree(&reg);
				return(l_num(-1));
			}
			free_lvalue(res);
		}
		regfree(&reg);
		return(l_num(0));
	}
	else if(strcmp(lf->funct, "subst")==0)
	{
		if(lf->nchld<3) return(l_str(NULL));
		lvalue index=l_eval(&lf->chld[0], lv, app), length=l_eval(&lf->chld[1], lv, app);
		if(index.type!=L_NUM) {free_lvalue(index); free_lvalue(length); return(l_str(NULL));}
		if(length.type!=L_NUM) {free_lvalue(index); free_lvalue(length); return(l_str(NULL));}
		char *rv;unsigned int l,i;
		init_char(&rv, &l, &i);
		unsigned int chld;
		for(chld=2;chld<lf->nchld;chld++)
		{
			lvalue res=l_eval(&lf->chld[chld], lv, app);
			if((res.type==L_STR)&&res.data.str&&(strlen(res.data.str)>index.data.num))
			{
				char *str=strndup(res.data.str+index.data.num, length.data.num);
				if(str)
				{
					append_str(&rv, &l, &i, str);
					free(str);
				}
			}
			free_lvalue(res);
		}
		free_lvalue(index);
		free_lvalue(length);
		return(l_str(rv));
	}
	else if(lf->funct[0]=='"')
	{
		char *sm=strchr(lf->funct+1, '"');
		if(sm)
		{
			char *str=strndup(lf->funct+1, sm-lf->funct-1);
			return(l_str(str));
		}
		else
			return(l_str(NULL));
	}
	else if(isdigit(lf->funct[0]))
	{
		unsigned long num;
		if(sscanf(lf->funct, "%lu", &num)==1)
		{
			return(l_num(num));
		}
		else
			return(l_str(NULL));
	}
	else if(strcmp(lf->funct, "num")==0)
	{
		if(!lf->nchld) return(l_num(0));
		lvalue v=l_eval(&lf->chld[0], lv, app);
		unsigned long num;
		switch(v.type)
		{
			case L_NUM:
				return(v);
			case L_STR:
				if(sscanf(v.data.str, "%lu", &num)==1) {free_lvalue(v);return(l_num(num));}
				free_lvalue(v);
				return(l_num(0));
			case L_BLO:;
				if(sscanf(v.data.blo.bytes, "%lu", &num)==1) {free_lvalue(v);return(l_num(num));}
				free_lvalue(v);
				return(l_num(0));
		}
	}
	else if(strcmp(lf->funct, "str")==0)
	{
		if(!lf->nchld) return(l_str(NULL));
		lvalue v=l_eval(&lf->chld[0], lv, app);
		switch(v.type)
		{
			case L_NUM:;
				char str[TL_LONG];
				hputlong(str, v.data.num);
				return(l_str(strdup(str)));
			case L_STR:
				return(v);
			case L_BLO:
			{
				char *str=malloc(v.data.blo.len+1);
				memcpy(str, v.data.blo.bytes, v.data.blo.len);
				str[v.data.blo.len]=0;
				free_lvalue(v);
				return(l_str(str));
			}
		}
	}
	else if(strcmp(lf->funct, "blo")==0)
	{
		if(!lf->nchld) return(l_str(NULL));
		lvalue v=l_eval(&lf->chld[0], lv, app);
		switch(v.type)
		{
			case L_NUM:;
				char str[TL_LONG];
				hputlong(str, v.data.num);
				return(l_blo(strdup(str), strlen(str)));
			case L_STR:
				return(l_blo(v.data.str, strlen(v.data.str)));
			case L_BLO:;
				return(v);
		}
	}
	unsigned int i;
	if(find_lvar(lv, lf->funct, &i))
		return(l_dup(lv.var[i]));
	if(app) return(app(lf, lv));
	fprintf(stderr, "libhorde[%u]: l_eval: unrecognised funct %s\n", getpid(), lf->funct);
	return(l_str(NULL));
}

bool l_asbool(lvalue val)
{
	switch(val.type)
	{
		case L_NUM:
			return(val.data.num);
		case L_STR:
			return(val.data.str&&strlen(val.data.str));
		case L_BLO:
			return(val.data.blo.len);
	}
	return(false);
}

lvalue l_num(unsigned long num)
{
	return((lvalue){.type=L_NUM, .data.num=num});
}

lvalue l_str(char *str)
{
	return((lvalue){.type=L_STR, .data.str=str});
}

lvalue l_blo(char *bytes, size_t len)
{
	return((lvalue){.type=L_BLO, .data.blo=(struct _blo){.bytes=bytes, .len=len}});
}

lvalue l_dup(lvalue val)
{
	switch(val.type)
	{
		case L_NUM:
			return(l_num(val.data.num));
		case L_STR:
			return(l_str(strdup(val.data.str)));
		case L_BLO:;
			char *bytes=malloc(val.data.blo.len);
			memcpy(bytes, val.data.blo.bytes, val.data.blo.len);
			return(l_blo(bytes, val.data.blo.len));
	}
	return l_str(NULL); // can't happen
}

void free_lvalue(lvalue l)
{
	switch(l.type)
	{
		case L_NUM:
			return;
		case L_STR:
			if(l.data.str) free(l.data.str);
			return;
		case L_BLO:
			if(l.data.blo.bytes) free(l.data.blo.bytes);
			return;
	}
}

bool find_lvar(lvars lv, const char *name, unsigned int *i)
{
	for((*i)=0;(*i)<lv.nvars;(*i)++)
		if(strcmp(name, lv.name[*i])==0) return(true);
	return(false);
}

void l_addvar(lvars *lv, const char *name, lvalue val)
{
	if(!lv) return;
	if(!name) return;
	unsigned int i;
	if(find_lvar(*lv, name, &i))
	{
		free_lvalue(lv->var[i]);
		lv->var[i]=l_dup(val);
		return;
	}
	unsigned int n=lv->nvars++;
	char **nname=realloc(lv->name, lv->nvars*sizeof(char *));
	if(!nname) {lv->nvars=n;return;}
	lvalue *var=realloc(lv->var, lv->nvars*sizeof(lvalue));
	if(!var) {lv->nvars=n;return;}
	(lv->name=nname)[n]=strdup(name);
	(lv->var=var)[n]=l_dup(val);
}

void free_lvars(lvars *lv)
{
	if(!lv) return;
	for(unsigned int i=0;i<lv->nvars;i++)
	{
		free(lv->name[i]);
		free_lvalue(lv->var[i]);
	}
	free(lv->name);
	free(lv->var);
}

ssize_t hsend(int fd, const hmsg h)
{
	ssize_t rv;
	char *str=str_from_hmsg(h);
	if(str)
	{
		if(write(fd, str, strlen(str))<(ssize_t)strlen(str))
			perror("hsend: write");
		if(write(fd, "\n", 1)<1)
			perror("hsend: write");
		rv=1+strlen(str);
		free(str);
		return(rv);
	}
	return(-1);
}

void hfin(unsigned char status)
{
	char st[4];
	snprintf(st, 4, "%hhu", status);
	hmsg fin=new_hmsg("fin", st);
	if(!fin) return;
	hsend(1, fin);
	free_hmsg(fin);
}

http_method get_method(const char *name)
{
	if(strcmp(name, "OPTIONS")==0) return(HTTP_METHOD_OPTIONS);
	if(strcmp(name, "GET")==0) return(HTTP_METHOD_GET);
	if(strcmp(name, "HEAD")==0) return(HTTP_METHOD_HEAD);
	if(strcmp(name, "POST")==0) return(HTTP_METHOD_POST);
	if(strcmp(name, "PUT")==0) return(HTTP_METHOD_PUT);
	if(strcmp(name, "DELETE")==0) return(HTTP_METHOD_DELETE);
	if(strcmp(name, "TRACE")==0) return(HTTP_METHOD_TRACE);
	if(strcmp(name, "CONNECT")==0) return(HTTP_METHOD_CONNECT);
	return(HTTP_METHOD_UNKNOWN);
}

http_version get_version(const char *name)
{
	if(strcmp(name, "HTTP/0.1")==0) return(HTTP_VERSION_0_1);
	if(strcmp(name, "HTTP/1.0")==0) return(HTTP_VERSION_1_0);
	if(strcmp(name, "HTTP/1.1")==0) return(HTTP_VERSION_1_1);
	return(HTTP_VERSION_UNKNOWN);
}

http_header get_header(const char *name)
{
	if(strcmp(name, "Cache-Control")==0) return(HTTP_HEADER_CACHE_CONTROL);
	if(strcmp(name, "Connection")==0) return(HTTP_HEADER_CONNECTION);
	if(strcmp(name, "Date")==0) return(HTTP_HEADER_DATE);
	if(strcmp(name, "Pragma")==0) return(HTTP_HEADER_PRAGMA);
	if(strcmp(name, "Trailer")==0) return(HTTP_HEADER_TRAILER);
	if(strcmp(name, "Transfer-Encoding")==0) return(HTTP_HEADER_TRANSFER_ENCODING);
	if(strcmp(name, "Upgrade")==0) return(HTTP_HEADER_UPGRADE);
	if(strcmp(name, "Via")==0) return(HTTP_HEADER_VIA);
	if(strcmp(name, "Warning")==0) return(HTTP_HEADER_WARNING);
	if(strcmp(name, "Accept")==0) return(HTTP_HEADER_ACCEPT);
	if(strcmp(name, "Accept-Charset")==0) return(HTTP_HEADER_ACCEPT_CHARSET);
	if(strcmp(name, "Accept-Encoding")==0) return(HTTP_HEADER_ACCEPT_ENCODING);
	if(strcmp(name, "Accept-Language")==0) return(HTTP_HEADER_ACCEPT_LANGUAGE);
	if(strcmp(name, "Authorization")==0) return(HTTP_HEADER_AUTHORIZATION);
	if(strcmp(name, "Expect")==0) return(HTTP_HEADER_EXPECT);
	if(strcmp(name, "From")==0) return(HTTP_HEADER_FROM);
	if(strcmp(name, "Host")==0) return(HTTP_HEADER_HOST);
	if(strcmp(name, "If-Match")==0) return(HTTP_HEADER_IF_MATCH);
	if(strcmp(name, "If-Modified-Since")==0) return(HTTP_HEADER_IF_MODIFIED_SINCE);
	if(strcmp(name, "If-None-Match")==0) return(HTTP_HEADER_IF_NONE_MATCH);
	if(strcmp(name, "If-Range")==0) return(HTTP_HEADER_IF_RANGE);
	if(strcmp(name, "If-Unmodified-Since")==0) return(HTTP_HEADER_IF_UNMODIFIED_SINCE);
	if(strcmp(name, "Max-Forwards")==0) return(HTTP_HEADER_MAX_FORWARDS);
	if(strcmp(name, "Proxy-Authorization")==0) return(HTTP_HEADER_PROXY_AUTHORIZATION);
	if(strcmp(name, "Range")==0) return(HTTP_HEADER_RANGE);
	if(strcmp(name, "Referer")==0) return(HTTP_HEADER_REFERER);
	if(strcmp(name, "TE")==0) return(HTTP_HEADER_TE);
	if(strcmp(name, "User-Agent")==0) return(HTTP_HEADER_USER_AGENT);
	if(strcmp(name, "Accept-Ranges")==0) return(HTTP_HEADER_ACCEPT_RANGES);
	if(strcmp(name, "Age")==0) return(HTTP_HEADER_AGE);
	if(strcmp(name, "ETag")==0) return(HTTP_HEADER_ETAG);
	if(strcmp(name, "Location")==0) return(HTTP_HEADER_LOCATION);
	if(strcmp(name, "Proxy-Authenticate")==0) return(HTTP_HEADER_PROXY_AUTHENTICATE);
	if(strcmp(name, "Retry-After")==0) return(HTTP_HEADER_RETRY_AFTER);
	if(strcmp(name, "Server")==0) return(HTTP_HEADER_SERVER);
	if(strcmp(name, "Vary")==0) return(HTTP_HEADER_VARY);
	if(strcmp(name, "WWW-Authenticate")==0) return(HTTP_HEADER_WWW_AUTHENTICATE);
	if(strcmp(name, "Allow")==0) return(HTTP_HEADER_ALLOW);
	if(strcmp(name, "Content-Encoding")==0) return(HTTP_HEADER_CONTENT_ENCODING);
	if(strcmp(name, "Content-Language")==0) return(HTTP_HEADER_CONTENT_LANGUAGE);
	if(strcmp(name, "Content-Length")==0) return(HTTP_HEADER_CONTENT_LENGTH);
	if(strcmp(name, "Content-Location")==0) return(HTTP_HEADER_CONTENT_LOCATION);
	if(strcmp(name, "Content-MD5")==0) return(HTTP_HEADER_CONTENT_MD5);
	if(strcmp(name, "Content-Range")==0) return(HTTP_HEADER_CONTENT_RANGE);
	if(strcmp(name, "Content-Type")==0) return(HTTP_HEADER_CONTENT_TYPE);
	if(strcmp(name, "Expires")==0) return(HTTP_HEADER_EXPIRES);
	if(strcmp(name, "Last-Modified")==0) return(HTTP_HEADER_LAST_MODIFIED);
	if(strcmp(name, "X-Frame-Options")==0) return(HTTP_HEADER_X_FRAME_OPTIONS);
	if(strcmp(name, "X-XSS-Protection")==0) return(HTTP_HEADER_X_XSS_PROTECTION);
	if(strcmp(name, "X-Content-Type-Options")==0) return(HTTP_HEADER_X_CONTENT_TYPE_OPTIONS);
	if(strcmp(name, "X-Requested-With")==0) return(HTTP_HEADER_X_REQUESTED_WITH);
	if(strcmp(name, "X-Forwarded-For")==0) return(HTTP_HEADER_X_FORWARDED_FOR);
	if(strcmp(name, "X-Forwarded-Proto")==0) return(HTTP_HEADER_X_FORWARDED_PROTO);
	if(strcmp(name, "X-Powered-By")==0) return(HTTP_HEADER_X_POWERED_BY);
	if(strcmp(name, "X-Do-Not-Track")==0) return(HTTP_HEADER_X_DO_NOT_TRACK);
	if(strcmp(name, "DNT")==0) return(HTTP_HEADER_X_DNT);
	if(strncmp(name, "X-", 2)==0) return(HTTP_HEADER_X__UNKNOWN);
	return(HTTP_HEADER_UNKNOWN);
}

const char *http_statusmsg(unsigned int status)
{
	switch(status/100)
	{
		case 1:
			switch(status)
			{
				case 100:
					return("Continue");
				case 101:
					return("Switching Protocols");
				default:
					return("Informational");
			}
		break;
		case 2:
			switch(status)
			{
				case 200:
					return("OK");
				case 201:
					return("Created");
				case 202:
					return("Accepted");
				case 203:
					return("Non-Authoritative Information");
				case 204:
					return("No Content");
				case 205:
					return("Reset Content");
				case 206:
					return("Partial Content");
				default:
					return("Success");
			}
		break;
		case 3:
			switch(status)
			{
				case 300:
					return("Multiple Choices");
				case 301:
					return("Moved Permanently");
				case 302:
					return("Found");
				case 303:
					return("See Other");
				case 304:
					return("Not Modified");
				case 305:
					return("Use Proxy");
				case 307:
					return("Temporary Redirect");
				default:
					return("Redirection");
			}
		break;
		case 4:
			switch(status)
			{
				case 400:
					return("Bad Request");
				case 401:
					return("Unauthorised");
				case 402:
					return("Payment Required");
				case 403:
					return("Forbidden");
				case 404:
					return("Not Found");
				case 405:
					return("Method Not Allowed");
				case 406:
					return("Not Acceptable");
				case 407:
					return("Proxy Authentication Required");
				case 408:
					return("Request Time-out");
				case 409:
					return("Conflict");
				case 410:
					return("Gone");
				case 411:
					return("Length Required");
				case 412:
					return("Precondition Failed");
				case 413:
					return("Request Entity Too Large");
				case 414:
					return("Request-URI Too Large");
				case 415:
					return("Unsupported Media Type");
				case 416:
					return("Requested range not satisfiable");
				case 417:
					return("Expectation Failed");
				default:
					return("Client Error");
			}
		break;
		case 5:
			switch(status)
			{
				case 500:
					return("Internal Server Error");
				case 501:
					return("Not Implemented");
				case 502:
					return("Bad Gateway");
				case 503:
					return("Service Unavailable");
				case 504:
					return("Gateway Time-out");
				case 505:
					return("HTTP Version not supported");
				default:
					return("Server Error");
			}
		break;
		default:
			return("???");
		break;
	}
}
