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

char * getl(int fd)
{
	char * lout;
	unsigned int l,i;
	init_char(&lout, &l, &i);
	unsigned char c;
	while(read(fd, &c, 1)==1)
	{
		if(c=='\n')
			break;
		append_char(&lout, &l, &i, c);
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

ssize_t hslurp(FILE *fp, char **buf)
{
	if(!buf) return(-1);
	char * lout;
	unsigned int l,i,b=0;
	init_char(&lout, &l, &i);
	signed int c;
	while(!feof(fp))
	{
		c=fgetc(fp);
		if(c==EOF)
			break;
		char hex[3];
		snprintf(hex, 3, "%02x", c);
		append_str(&lout, &l, &i, hex);
		b++;
	}
	*buf=lout;
	if(lout)
		return(b);
	return(-1);
}

ssize_t dslurp(FILE *fp, char **buf)
{
	if(!buf) return(-1);
	char * lout;
	unsigned int l,i;
	init_char(&lout, &l, &i);
	signed int c;
	while(!feof(fp))
	{
		c=fgetc(fp);
		if(c==EOF)
			break;
		append_char(&lout, &l, &i, c);
	}
	*buf=lout;
	if(lout)
		return(i);
	return(-1);
}

void append_char(char **buf, unsigned int *l, unsigned int *i, char c)
{
	if(*buf)
	{
		(*buf)[(*i)++]=c;
	}
	else
	{
		return;
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
		*buf=NULL;
		return;
	}
}

void append_str(char **buf, unsigned int *l, unsigned int *i, const char *s)
{
	if(!s) return;
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
			return;
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
			*buf=NULL;
			return;
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

char *normalise_path(char *path)
{
	if(!path) return(NULL);
	unsigned int nelem=1, i=0, j=0;
	char *p=path;
	while((p=strchr(p+1, '/'))) nelem++;
	char *el[nelem];
	p=path;
	el[i]=strtok(p, "/");
	size_t l=1;
	while(el[i])
	{
		if(strcmp(el[i], "..")==0)
		{
			if(i) i--;
			l-=strlen(el[i])+1;
		}
		else if(strcmp(el[i], ".")!=0)
		{
			l+=strlen(el[i])+1;
			i++;
		}
		el[i]=strtok(NULL, "/");
	}
	if(!i) return(strdup("/"));
	char *rpath;unsigned int rl, ri;
	init_char(&rpath, &rl, &ri);
	for(j=0;j<i;j++)
	{
		append_char(&rpath, &rl, &ri, '/');
		append_str(&rpath, &rl, &ri, el[j]);
	}
	return(rpath);
}
