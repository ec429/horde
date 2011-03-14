#define _GNU_SOURCE
/*
	horde: modular http server
	Copyright (C) 2011 Edward Cree

	Licensed under GNU GPLv3+; see top of horde.c for details
	
	net: receives, parses, and handles HTTP requests
*/
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <signal.h>
#include <limits.h>
#include <errno.h>

#include "bits.h"
#include "libhorde.h"

void err(unsigned int status, const char *statusmsg, const char *headers, int fd);

int main(int argc, char **argv)
{
	const char *name=argc?argv[0]:"net";
	int newhandle;
	struct sockaddr remote;
	socklen_t addr_size=sizeof(remote);
	if((newhandle=accept(3, (struct sockaddr *)&remote, &addr_size))==-1)
	{
		fprintf(stderr, "horde: %s[%d]: ", name, getpid());
		perror("accept");
		hfin(EXIT_FAILURE);
		return(EXIT_FAILURE);
	}
	close(3); // don't need this any more
	fprintf(stderr, "horde: %s[%d]: accepted\n", name, getpid());
	bool is6=false; // is this IPv6 (else v4)?
	switch(((struct sockaddr_in *)&remote)->sin_family)
	{
		case AF_INET:
			is6=false;
		break;
		case AF_INET6:
			is6=true;
		break;
		default:
			fprintf(stderr, "horde: %s[%d]: 500 - bad address family, is %u", name, getpid(), ((struct sockaddr_in *)&remote)->sin_family);
			err(500, "Internal Server Error", NULL, newhandle);
			close(newhandle);
			hfin(EXIT_FAILURE);
			return(EXIT_FAILURE);
		break;
	}
	char *ip = malloc(is6?INET6_ADDRSTRLEN:INET_ADDRSTRLEN);
	if(ip) inet_ntop(is6?AF_INET6:AF_INET, is6?(const void *)&((struct sockaddr_in6 *)&remote)->sin6_addr:(const void *)&((struct sockaddr_in *)&remote)->sin_addr, ip, is6?INET6_ADDRSTRLEN:INET_ADDRSTRLEN);
	if(ip) fprintf(stderr, "horde: %s[%d]: remote IP is %s\n", name, getpid(), ip);
	char *buf;unsigned int bl, bi;
	init_char(&buf, &bl, &bi);
	while((bi<4) || strncmp(buf+bi-4, "\r\n\r\n", 4))
	{
		unsigned char c;
		if(recv(newhandle, &c, 1, MSG_WAITALL)!=1)
			break;
		append_char(&buf, &bl, &bi, c);
		if(bi>ULONG_MAX-3)
			break;
		if(bi==4&&buf)
		{
			if(strcmp(buf, "\r\n\r\n")==0)
			{
				buf[bi=0]=0;
			}
		}
	}
	if(!buf)
	{
		fprintf(stderr, "horde: %s[%d]: 500 - allocation failure while reading from socket\n", name, getpid());
		err(500, "Internal Server Error", NULL, newhandle);
		close(newhandle);
		hfin(EXIT_FAILURE);
		return(EXIT_FAILURE);
	}
	fprintf(stderr, "horde: %s[%d]: read %u bytes\n", name, getpid(), bi);
	char **line=malloc(bi*sizeof(char *));
	char *next=buf, *last;
	while(*next)
	{
		unsigned int nlines=0;
		while(*next)
		{
			unsigned int ns=0;
			while(*next&&strchr("\n\r", *next)) // skip over any \n or \r, incrementing line counter if \n
			{
				if(*next=='\n' && ns++) // count a newline; if it was already nonzero,
				{
					*next++=0;
					goto eoh; // it's the end of the headers
				}
				else
					*next++=0;
			}
			last=next;
			while(!strchr("\n\r", *next)) next++; // next now points at first \r, \n or \0.
			line[nlines++]=last;
		}
		eoh:
		if(nlines==0)
		{
			fprintf(stderr, "horde: %s[%d]: 400 - empty request\n", name, getpid());
			err(400, "Bad Request (Empty)", NULL, newhandle);
			close(newhandle);
			hfin(EXIT_SUCCESS);
			return(EXIT_SUCCESS);
		}
		char **n_line=realloc(line, nlines*sizeof(char *));
		if(n_line)
			line=n_line;
		// Request-Line = Method SP Request-URI SP HTTP-Version CRLF
		char *method=strtok(line[0], " ");
		char *uri=strtok(NULL, " ");
		char *ver=strtok(NULL, "");
		char *host=NULL;
		http_version v=get_version(ver);
		if(v==HTTP_VERSION_UNKNOWN)
		{
			err(505, "HTTP Version Not Supported", NULL, newhandle);
			fprintf(stderr, "horde: %s[%d]: 505 HTTP Version Not Supported [%s]\n", name, getpid(), ver);
			close(newhandle);
			hfin(EXIT_SUCCESS); // success because it's an unrecognised version, not merely an unimplemented feature
			return(EXIT_SUCCESS);
		}
		http_method m=get_method(method);
		switch(m)
		{
			case HTTP_METHOD_GET:
				if(*uri!='/')
				{
					if(strncmp(uri, "http://", 7)!=0)
					{
						fprintf(stderr, "horde: %s[%d]: 400 Bad Request (Malformed URI) '%s'\n", name, getpid(), uri);
						err(400, "Bad Request (Malformed URI)", NULL, newhandle);
						close(newhandle);
						hfin(EXIT_SUCCESS);
						return(EXIT_SUCCESS);
					}
					else
					{
						// absoluteURI
						host=uri+7;
						char *slash=strchr(host, '/');
						if(slash)
						{
							uri=slash+1;
							*slash=0;
						}
						else
							uri="/";
					}
				}
			break;
			case HTTP_METHOD_CONNECT:
				err(501, "Not Supported", NULL, newhandle);
				fprintf(stderr, "horde: %s[%d]: 501 Not Supported (%s)\n", name, getpid(), method);
				close(newhandle);
				hfin(EXIT_SUCCESS); // success because it's an inapplicable method, not merely an unimplemented feature
				return(EXIT_SUCCESS);
			break;
			case HTTP_METHOD_UNKNOWN:
				err(501, "Unknown Method", NULL, newhandle);
				fprintf(stderr, "horde: %s[%d]: 501 Unknown Method (%s(\n", name, getpid(), method);
				close(newhandle);
				hfin(EXIT_SUCCESS); // success because it's an unrecognised method, not merely an unimplemented feature
				return(EXIT_SUCCESS);
			break;
			default:
				err(501, "Not Implemented", NULL, newhandle);
				fprintf(stderr, "horde: %s[%d]: 501 Not Implemented (%s)\n", name, getpid(), method);
				close(newhandle);
				hfin(EXIT_FAILURE);
				return(EXIT_FAILURE);
			break;
		}
		struct hdr {http_header name; const char *value;} *headers=malloc(nlines*sizeof(struct hdr));
		if(!headers)
		{
			fprintf(stderr, "horde: %s[%d]: 500 - allocation failure (struct hdr *headers): malloc: %s\n", name, getpid(), strerror(errno));
			err(500, "Internal Server Error", NULL, newhandle);
			close(newhandle);
			hfin(EXIT_FAILURE);
			return(EXIT_FAILURE);
		}
		unsigned int l, nhdrs=0;
		for(l=1;l<nlines;l++)
		{
			fprintf(stderr, "> %s\n", line[l]);
			char *colon=strchr(line[l], ':');
			if(colon)
			{
				*colon++=0;
				http_header h=get_header(line[l]);
				headers[nhdrs++]=(struct hdr){.name=h, .value=colon};
			} // otherwise, we just ignore the line
		}
		switch(m)
		{
			case HTTP_METHOD_GET:;
				hmsg path=new_hmsg("path", uri);
				if(!path)
				{
					fprintf(stderr, "horde: %s[%d]: 500 - allocation failure (new_hmsg): %s\n", name, getpid(), strerror(errno));
					err(500, "Internal Server Error", NULL, newhandle);
					close(newhandle);
					hfin(EXIT_FAILURE);
					return(EXIT_FAILURE);
				}
				if(add_htag(path, "host", host))
				{
					fprintf(stderr, "horde: %s[%d]: allocation failure (add_htag): %s\n", name, getpid(), strerror(errno));
					err(500, "Internal Server Error", NULL, newhandle);
					close(newhandle);
					hfin(EXIT_FAILURE);
					return(EXIT_FAILURE);
				}
				fprintf(stderr, "horde: %s[%d]: sending request to path\n", name, getpid());
				if(hsend(1, path)>0)
				{
					free(path);
				}
				else
				{
					fprintf(stderr, "horde: %s[%d]: 500 - communication failure (hsend): %s\n", name, getpid(), strerror(errno));
					err(500, "Internal Server Error", NULL, newhandle);
					close(newhandle);
					hfin(EXIT_FAILURE);
					return(EXIT_FAILURE);
				}
				char *frompath=getl(STDIN_FILENO);
				if(!(frompath&&*frompath))
				{
					fprintf(stderr, "horde: %s[%d]: 500 - failed to read response (getl): %s\n", name, getpid(), strerror(errno));
					err(500, "Internal Server Error", NULL, newhandle);
					close(newhandle);
					hfin(EXIT_FAILURE);
					return(EXIT_FAILURE);
				}
				fprintf(stderr, "horde: %s[%d]: < '%s'\n", name, getpid(), frompath);
				hmsg h=hmsg_from_str(frompath);
				if(h)
				{
					free(frompath);
				}
				else
				{
					fprintf(stderr, "horde: %s[%d]: 500 - couldn't understand the response from path: %s\n", name, getpid(), frompath);
					err(500, "Internal Server Error", NULL, newhandle);
					close(newhandle);
					hfin(EXIT_FAILURE);
					return(EXIT_FAILURE);
				}
				if(strcmp(h->funct, "path"))
				{
					if(strcmp(h->funct, "shutdown")==0)
					{
						fprintf(stderr, "horde: %s[%d]: 503 - server is shutting down\n", name, getpid());
						err(503, "Service Unavailable", NULL, newhandle);
					}
					else
					{
						fprintf(stderr, "horde: %s[%d]: 500 - path rewriting failed: %s\n", name, getpid(), h->funct);
						unsigned int i;
						for(i=0;i<h->nparms;i++)
							fprintf(stderr, "horde: %s[%d]:\t(%s|%s)\n", name, getpid(), h->p_tag[i], h->p_value[i]);
						fprintf(stderr, "horde: %s[%d]:\t%s\n", name, getpid(), h->data);
						err(500, "Internal Server Error", NULL, newhandle);
					}
					close(newhandle);
					hfin(EXIT_FAILURE);
					return(EXIT_FAILURE);
				}
				char *rpath=strdup(h->data);
				if(!rpath)
				{
					fprintf(stderr, "horde: %s[%d]: 500 - allocation failure (char *path): strdup: %s\n", name, getpid(), strerror(errno));
					err(500, "Internal Server Error", NULL, newhandle);
					close(newhandle);
					hfin(EXIT_FAILURE);
					return(EXIT_FAILURE);
				}
				free_hmsg(h);
				fprintf(stderr, "%s\n", rpath);
				// TODO: send the rpath to proc
				free(rpath);
			break;
			default:
				err(501, "Not Implemented", NULL, newhandle);
				fprintf(stderr, "horde: %s[%d]: 501 Not Implemented (%s)\n", name, getpid(), method);
				close(newhandle);
				hfin(EXIT_FAILURE);
				return(EXIT_FAILURE);
			break;
		}
	}
	free(line);
	free(buf);
	//fprintf(stderr, "horde: %s[%d]: \n", name, getpid());
	fprintf(stderr, "horde: %s[%d]: closing conn\n", name, getpid());
	close(newhandle);
	hfin(EXIT_SUCCESS);
	return(EXIT_SUCCESS);
}

void err(unsigned int status, const char *statusmsg, const char *headers, int fd)
{
	char date[256];
	time_t timer = time(NULL);
	struct tm *tm = gmtime(&timer);
	size_t datelen = strftime(date, sizeof(date), "%F %H:%M:%S", tm);
	bool m=true;
	char *buf=malloc(128+datelen+strlen(HTTPD_VERSION)+(statusmsg?strlen(statusmsg):0)+(headers?strlen(headers):0));
	if(!buf)
	{
		m=false;
		buf="\
HTTP/1.1 500 Internal Server Error\n\
Server: "HTTPD_VERSION" (Unix)\n\
Content-Length: 0\n\
Connection: close\n\
\n";
	}
	else
	{
		sprintf(buf, "\
HTTP/1.1 %d %s\n\
Date: %s\n\
Server: "HTTPD_VERSION" (Unix)\n\
Content-Length: 0\n\
%s\
Connection: close\n\
\n", status, statusmsg, date, headers?headers:"");
	}
	send(fd, buf, strlen(buf), 0);
	if(m) free(buf);
}
