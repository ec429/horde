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
	snprintf(buf, 16, "%lu", val);
}

unsigned long hgetlong(const char *buf)
{
	unsigned long rv;
	sscanf(buf, "%lu", &rv);
	return(rv);
}

void hputshort(char *buf, unsigned short val)
{
	snprintf(buf, 8, "%hu", val);
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
		if(data) rv->data=strdup(data); else rv->data=NULL;
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
				free(val);
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
			free(val);
		}
	}
	append_char(&rv, &l, &i, ')');
	return(rv);
}

hmsg hmsg_from_str(const char *str)
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
						return(rv); // no data segment
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
