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
		fprintf(stderr, "%s[%d]: ", name, getpid());
		perror("accept");
		hfin(EXIT_FAILURE);
		return(EXIT_FAILURE);
	}
	close(3); // don't need this any more
	fprintf(stderr, "%s[%d]: accepted\n", name, getpid());
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
			fprintf(stderr, "%s[%d]: bad address family, is %u", name, getpid(), ((struct sockaddr_in *)&remote)->sin_family);
			err(500, "Internal Server Error", NULL, newhandle);
			close(newhandle);
			hfin(EXIT_FAILURE);
			return(EXIT_FAILURE);
		break;
	}
	char *ip = malloc(is6?INET6_ADDRSTRLEN:INET_ADDRSTRLEN);
	if(ip) inet_ntop(is6?AF_INET6:AF_INET, is6?(const void *)&((struct sockaddr_in6 *)&remote)->sin6_addr:(const void *)&((struct sockaddr_in *)&remote)->sin_addr, ip, is6?INET6_ADDRSTRLEN:INET_ADDRSTRLEN);
	if(ip) fprintf(stderr, "%s[%d]: remote IP is %s\n", name, getpid(), ip);
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
		fprintf(stderr, "%s[%d]: allocation failure while reading from socket\n", name, getpid());
		err(500, "Internal Server Error", NULL, newhandle);
		close(newhandle);
		hfin(EXIT_FAILURE);
		return(EXIT_FAILURE);
	}
	fprintf(stderr, "%s[%d]: read %u bytes\n", name, getpid(), bi);
	fprintf(stderr, "%s\n", buf);
	char **line=malloc(bi*sizeof(char *));
	unsigned int nlines=0;
	char *next=buf, *last;
	while(*next)
	{
		unsigned int ns=0;
		while(*next&&strchr("\n\r", *next)) // skip over any \n or \r, incrementing line counter if \n
			if(*next++=='\n') // count a newline
				if(ns++) // if it was already nonzero
					line[nlines++]=""; // then we have a blank line
		last=next;
		while(!strchr("\n\r", *next)) next++; // next now points at first \r, \n or \0.
		line[nlines++]=last;
	}
	if(nlines==0)
	{
		fprintf(stderr, "%s[%d]: empty request!\n", name, getpid());
		err(400, "Bad Request (Empty)", NULL, newhandle);
		close(newhandle);
		hfin(EXIT_SUCCESS); // we may not have sent a response, but /we/ didn't fail
		return(EXIT_SUCCESS);
	}
	char **n_line=realloc(line, nlines*sizeof(char *));
	if(n_line)
		line=n_line;
	//
	free(line);
	free(buf);
	//fprintf(stderr, "%s[%d]: \n", name, getpid());
	fprintf(stderr, "%s[%d]: closing conn\n", name, getpid());
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
