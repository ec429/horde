#include "bits.h"

char * fgetl(FILE *fp)
{
	char * lout;
	unsigned int l,i;
	init_char(&lout, &l, &i);
	signed int c;
	while(!feof(fp))
	{
		c=fgetc(fp);
		if((c==EOF)||(c=='\n'))
			break;
		if(c!=0)
		{
			append_char(&lout, &l, &i, c);
		}
	}
	return(lout);
}

char * slurp(FILE *fp)
{
	char * lout;
	unsigned int l,i;
	init_char(&lout, &l, &i);
	signed int c;
	while(!feof(fp))
	{
		c=fgetc(fp);
		if(c==EOF)
			break;
		if(c!=0)
		{
			append_char(&lout, &l, &i, c);
		}
	}
	return(lout);
}

void append_char(char **buf, unsigned int *l, unsigned int *i, char c)
{
	if(!((c==0)||(c==EOF)))
	{
		if(*buf)
		{
			(*buf)[(*i)++]=c;
		}
		else
		{
			init_char(buf, l, i);
			append_char(buf, l, i, c);
		}
		char *nbuf=*buf;
		if((*i)>=(*l))
		{
			*l=1+*i*2;
			nbuf=realloc(*buf, *l);
		}
		if(nbuf)
		{
			*buf=nbuf;
			(*buf)[*i]=0;
		}
		else
		{
			free(*buf);
			init_char(buf, l, i);
		}
	}
}

void append_str(char **buf, unsigned int *l, unsigned int *i, const char *s)
{
	while(*s)
		append_char(buf, l, i, *s++);
}

void init_char(char **buf, unsigned int *l, unsigned int *i)
{
	*l=80;
	*buf=malloc(*l);
	if(*buf)
	{
		(*buf)[0]=0;
		*i=0;
		return;
	}
	*l=0;
}

void u_append_char(uchar_t **buf, unsigned int *l, unsigned int *i, uchar_t c)
{
	if(c)
	{
		if(*buf)
		{
			(*buf)[(*i)++]=c;
		}
		else
		{
			u_init_char(buf, l, i);
			u_append_char(buf, l, i, c);
		}
		uchar_t *nbuf=*buf;
		if((*i)>=(*l))
		{
			*l=*i*2;
			nbuf=realloc(*buf, *l*sizeof(uchar_t));
		}
		if(nbuf)
		{
			*buf=nbuf;
			(*buf)[*i]=0;
		}
		else
		{
			free(*buf);
			u_init_char(buf, l, i);
		}
	}
}

void u_append_str(uchar_t **buf, unsigned int *l, unsigned int *i, const uchar_t *s)
{
	while(*s)
		u_append_char(buf, l, i, *s++);
}

void u_init_char(uchar_t **buf, unsigned int *l, unsigned int *i)
{
	*l=80;
	*buf=malloc(*l*sizeof(uchar_t));
	(*buf)[0]=0;
	*i=0;
}

uchar_t *u_strdup(const uchar_t *s)
{
	// inefficient approach, but who's counting?
	uchar_t *buf; unsigned int l,i;
	u_init_char(&buf, &l, &i);
	u_append_str(&buf, &l, &i, s);
	return(buf);
}
