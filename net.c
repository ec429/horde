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
bool check_msgs(hstate *hst);
char *logline(unsigned int status, unsigned long length, const char *path, const char *ip, const char *ac, const char *ref, const char *ua); // returns a malloc-like pointer

int main(int argc, char **argv)
{
	hstate hst;
	hst_init(&hst, argc?argv[0]:"net", false);
	const char *server="horde/"HTTPD_VERSION;
	while(check_msgs(&hst));
	int newhandle;
	struct sockaddr remote;
	socklen_t addr_size=sizeof(remote);
	time_t stamp=0;
	if((newhandle=accept(3, (struct sockaddr *)&remote, &addr_size))==-1)
	{
		if(hst.debug)
		{
			fprintf(stderr, "horde: %s[%d]: ", hst.name, getpid());
			perror("accept");
		}
		hfin(EXIT_FAILURE);
		return(EXIT_FAILURE);
	}
	else
	{
		close(3); // don't need this any more
		stamp=time(NULL);
		char date[256];
		struct tm *tm = gmtime(&stamp);
		strftime(date, sizeof(date), "%F %H:%M:%S", tm);
		hmsg ac=new_hmsg("accepted", date);
		if(!ac)
		{
			if(hst.debug) fprintf(stderr, "horde: %s[%d]: 500 - allocation failure (hmsg ac): new_hmsg: %s", hst.name, getpid(), strerror(errno));
			err(500, "Internal Server Error", NULL, newhandle);
			close(newhandle);
			hfin(EXIT_FAILURE);
			return(EXIT_FAILURE);
		}
		hsend(1, ac);
		free_hmsg(ac);
		if(hst.debug) fprintf(stderr, "horde: %s[%d]: accepted at %s\n", hst.name, getpid(), date);
	}
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
			if(hst.debug) fprintf(stderr, "horde: %s[%d]: 500 - bad address family, is %u", hst.name, getpid(), ((struct sockaddr_in *)&remote)->sin_family);
			err(500, "Internal Server Error", NULL, newhandle);
			close(newhandle);
			hfin(EXIT_FAILURE);
			return(EXIT_FAILURE);
		break;
	}
	char *ip = malloc(is6?INET6_ADDRSTRLEN:INET_ADDRSTRLEN);
	if(ip) inet_ntop(is6?AF_INET6:AF_INET, is6?(const void *)&((struct sockaddr_in6 *)&remote)->sin6_addr:(const void *)&((struct sockaddr_in *)&remote)->sin_addr, ip, is6?INET6_ADDRSTRLEN:INET_ADDRSTRLEN);
	if(ip&&hst.debug) fprintf(stderr, "horde: %s[%d]: remote IP is %s\n", hst.name, getpid(), ip);
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
		if(hst.debug) fprintf(stderr, "horde: %s[%d]: 500 - allocation failure while reading from socket\n", hst.name, getpid());
		err(500, "Internal Server Error", NULL, newhandle);
		close(newhandle);
		hfin(EXIT_FAILURE);
		return(EXIT_FAILURE);
	}
	while(check_msgs(&hst));
	if(hst.debug) fprintf(stderr, "horde: %s[%d]: read %u bytes\n", hst.name, getpid(), bi);
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
			if(hst.debug) fprintf(stderr, "horde: %s[%d]: 400 - empty request\n", hst.name, getpid());
			err(400, "Bad Request (Empty)", NULL, newhandle);
			close(newhandle);
			hfin(EXIT_SUCCESS);
			return(EXIT_SUCCESS);
		}
		char **n_line=realloc(line, nlines*sizeof(char *));
		if(n_line)
			line=n_line;
		// Request-Line = Method SP Request-URI SP HTTP-Version CRLF
		if(hst.debug) fprintf(stderr, "horde: %s[%d]: Request-Line: %s\n", hst.name, getpid(), line[0]);
		char *method=strtok(line[0], " ");
		char *uri=strtok(NULL, " ");
		char *ver=strtok(NULL, "");
		char *host=NULL;
		char *ref=NULL;
		char *ua=NULL;
		http_version v=get_version(ver);
		if(v==HTTP_VERSION_UNKNOWN)
		{
			err(505, "HTTP Version Not Supported", NULL, newhandle);
			if(hst.debug) fprintf(stderr, "horde: %s[%d]: 505 HTTP Version Not Supported [%s]\n", hst.name, getpid(), ver);
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
						if(hst.debug) fprintf(stderr, "horde: %s[%d]: 400 Bad Request (Malformed URI) '%s'\n", hst.name, getpid(), uri);
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
				if(hst.debug) fprintf(stderr, "horde: %s[%d]: 501 Not Supported (%s)\n", hst.name, getpid(), method);
				close(newhandle);
				hfin(EXIT_SUCCESS); // success because it's an inapplicable method, not merely an unimplemented feature
				return(EXIT_SUCCESS);
			break;
			case HTTP_METHOD_UNKNOWN:
				err(501, "Unknown Method", NULL, newhandle);
				if(hst.debug) fprintf(stderr, "horde: %s[%d]: 501 Unknown Method (%s(\n", hst.name, getpid(), method);
				close(newhandle);
				hfin(EXIT_SUCCESS); // success because it's an unrecognised method, not merely an unimplemented feature
				return(EXIT_SUCCESS);
			break;
			default:
				err(501, "Not Implemented", NULL, newhandle);
				if(hst.debug) fprintf(stderr, "horde: %s[%d]: 501 Not Implemented (%s)\n", hst.name, getpid(), method);
				close(newhandle);
				hfin(EXIT_FAILURE);
				return(EXIT_FAILURE);
			break;
		}
		struct hdr {http_header name; const char *un_name; const char *value;} *headers=malloc(nlines*sizeof(struct hdr));
		if(!headers)
		{
			if(hst.debug) fprintf(stderr, "horde: %s[%d]: 500 - allocation failure (struct hdr *headers): malloc: %s\n", hst.name, getpid(), strerror(errno));
			err(500, "Internal Server Error", NULL, newhandle);
			close(newhandle);
			hfin(EXIT_FAILURE);
			return(EXIT_FAILURE);
		}
		unsigned int l, nhdrs=0;
		for(l=1;l<nlines;l++)
		{
			//fprintf(stderr, "> %s\n", line[l]);
			char *colon=strchr(line[l], ':');
			if(colon)
			{
				*colon++=0;
				while(isspace(*colon)) colon++;
				http_header h=get_header(line[l]);
				headers[nhdrs++]=(struct hdr){.name=h, .un_name=line[l], .value=colon};
				if(h==HTTP_HEADER_HOST)
					host=colon;
				else if(h==HTTP_HEADER_REFERER)
					ref=colon;
				else if(h==HTTP_HEADER_USER_AGENT)
					ua=colon;
			} // otherwise, we just ignore the line
		}
		while(check_msgs(&hst));
		if(hst.shutdown)
		{
			if(hst.debug) fprintf(stderr, "horde: %s[%d]: 503 - server is shutting down\n", hst.name, getpid());
			err(503, "Service Unavailable", NULL, newhandle);
			close(newhandle);
			hfin(EXIT_SUCCESS);
			return(EXIT_SUCCESS);
		}
		switch(m)
		{
			case HTTP_METHOD_GET:;
				if(hst.debug) fprintf(stderr, "horde: %s[%d]: handling GET request (%s) (%u headers)\n", hst.name, getpid(), uri, nhdrs);
				char *rpath=normalise_path(uri);
				if(hst.debug) fprintf(stderr, "horde: %s[%d]: sending request to proc for %s\n", hst.name, getpid(), rpath);
				hmsg p=new_hmsg("proc", rpath);
				if(!p)
				{
					if(hst.debug) fprintf(stderr, "horde: %s[%d]: 500 - allocation failure (hmsg p): new_hmsg: %s\n", hst.name, getpid(), strerror(errno));
					err(500, "Internal Server Error", NULL, newhandle);
					close(newhandle);
					hfin(EXIT_FAILURE);
					return(EXIT_FAILURE);
				}
				unsigned int hdr;
				for(hdr=0;hdr<nhdrs;hdr++)
				{
					char *hline=malloc(strlen(headers[hdr].un_name)+strlen(headers[hdr].value)+3);
					if(hline)
					{
						sprintf(hline, "%s: %s", headers[hdr].un_name, headers[hdr].value);
						add_htag(p, "rqheader", hline);
						free(hline);
					}
					else
					{
						if(hst.debug) fprintf(stderr, "horde: %s[%d]: ign - allocation failure (char *hline): malloc: %s\n", hst.name, getpid(), strerror(errno));
					}
				}
				hsend(1, p);
				free_hmsg(p);
				hmsg h;
				do
				{
					if(hst.shutdown)
					{
						if(hst.debug) fprintf(stderr, "horde: %s[%d]: 503 - server is shutting down\n", hst.name, getpid());
						err(503, "Service Unavailable", NULL, newhandle);
						close(newhandle);
						hfin(EXIT_SUCCESS);
						return(EXIT_SUCCESS);
					}
					char *fromproc=getl(STDIN_FILENO);
					if(!(fromproc&&*fromproc))
					{
						if(hst.debug) fprintf(stderr, "horde: %s[%d]: 500 - failed to read response (getl): %s\n", hst.name, getpid(), strerror(errno));
						err(500, "Internal Server Error", NULL, newhandle);
						close(newhandle);
						hfin(EXIT_FAILURE);
						return(EXIT_FAILURE);
					}
					h=hmsg_from_str(fromproc, true);
					if(h)
						free(fromproc);
					else
					{
						if(hst.debug) fprintf(stderr, "horde: %s[%d]: 500 - couldn't understand the response from proc: %s\n", hst.name, getpid(), fromproc);
						err(500, "Internal Server Error", NULL, newhandle);
						close(newhandle);
						hfin(EXIT_FAILURE);
						return(EXIT_FAILURE);
					}
				}
				while(hmsg_state(h, &hst));
				const char *from=gettag(h, "from");
				if(strcmp(h->funct, "err")==0)
				{
					if(hst.debug)
					{
						fprintf(stderr, "horde: %s[%d]: 500 - proc failed: %s\n", hst.name, getpid(), h->funct);
						unsigned int i;
						for(i=0;i<h->nparms;i++)
						{
							fprintf(stderr, "horde: %s[%d]:\t(%s|%s)\n", hst.name, getpid(), h->p_tag[i], h->p_value[i]);
							if(strcmp(h->p_tag[i], "errno")==0)
							{
								fprintf(stderr, "horde: %s[%d]:\t\t%s\n", hst.name, getpid(), strerror(hgetlong(h->p_value[i])));
							}
						}
						fprintf(stderr, "horde: %s[%d]:\t%s\n", hst.name, getpid(), h->data);
					}
					err(500, "Internal Server Error", NULL, newhandle);
					close(newhandle);
					hfin(EXIT_FAILURE);
					return(EXIT_FAILURE);
				}
				else if(strcmp(h->funct, "proc")!=0)
				{
					if(hst.debug) fprintf(stderr, "horde: %s[%d]: unrecognised funct '%s'\n", hst.name, getpid(), h->funct);
					hmsg eh=new_hmsg("err", NULL);
					if(eh)
					{
						add_htag(eh, "what", "unrecognised-funct");
						if(from) add_htag(eh, "to", from);
						hsend(1, eh);
						free_hmsg(eh);
					}
					free_hmsg(h);
					close(newhandle);
					hfin(EXIT_FAILURE);
					return(EXIT_FAILURE);
				}
				unsigned short status=200;
				const char *statusmsg=gettag(h, "statusmsg");
				char *fserver; unsigned int fsl, fsi;
				init_char(&fserver, &fsl, &fsi);
				append_str(&fserver, &fsl, &fsi, server);
				unsigned int i;
				for(i=0;i<h->nparms;i++)
				{
					if(hst.debug) fprintf(stderr, "horde: %s[%d]:\t(%s|%s)\n", hst.name, getpid(), h->p_tag[i], h->p_value[i]);
					if(strcmp(h->p_tag[i], "status")==0)
					{
						unsigned short ns=hgetshort(h->p_value[i]);
						if((ns<600)&&(ns>99)) status=ns;
					}
					else if(strcmp(h->p_tag[i], "server")==0)
					{
						append_char(&fserver, &fsl, &fsi, ' ');
						append_str(&fserver, &fsl, &fsi, h->p_value[i]);
					}
				}
				if(!statusmsg)
					statusmsg=http_statusmsg(status);
				if(!statusmsg)
					statusmsg="???";
				char date[256];
				time_t timer = time(NULL);
				struct tm *tm = gmtime(&timer);
				size_t datelen = strftime(date, sizeof(date), "%F %H:%M:%S", tm);
				char *head=malloc(9+TL_SHORT+strlen(statusmsg)+1+6+datelen+1+8+strlen(fserver)+1+16+TL_SIZET+1+1);
				if(!head)
				{
					if(hst.debug) fprintf(stderr, "horde: %s[%d]: 500 - allocation failure (char *head): malloc: %s\n", hst.name, getpid(), strerror(errno));
					err(500, "Internal Server Error", NULL, newhandle);
					close(newhandle);
					hfin(EXIT_FAILURE);
					return(EXIT_FAILURE);
				}
				sprintf(head, "HTTP/1.1 %hu %s\nDate: %s\nServer: %s\nContent-Length: %zu\n", status, statusmsg, date, fserver, h->data?h->dlen:0);
				free(fserver);
				ssize_t n=sendall(newhandle, head, strlen(head), 0);
				if(n)
				{
					if(hst.debug) fprintf(stderr, "horde: %s[%d]: 499 - sendall(head) failed, %zd\n", hst.name, getpid(), n);
					close(newhandle);
					hfin(EXIT_FAILURE);
					return(EXIT_FAILURE);
				}
				free(head);
				for(i=0;i<h->nparms;i++)
				{
					if(strcmp(h->p_tag[i], "header")==0)
					{
						n=sendall(newhandle, h->p_value[i], strlen(h->p_value[i]), 0);
						if(n)
						{
							if(hst.debug) fprintf(stderr, "horde: %s[%d]: 499 - sendall(extra header) failed, %zd\n", hst.name, getpid(), n);
							close(newhandle);
							hfin(EXIT_FAILURE);
							return(EXIT_FAILURE);
						}
						if((n=sendall(newhandle, "\n", 1, 0)))
						{
							if(hst.debug) fprintf(stderr, "horde: %s[%d]: 499 - sendall(extra header, \\n) failed, %zd\n", hst.name, getpid(), n);
							close(newhandle);
							hfin(EXIT_FAILURE);
							return(EXIT_FAILURE);
						}
					}
				}
				if((n=sendall(newhandle, "\n", 1, 0)))
				{
					if(hst.debug) fprintf(stderr, "horde: %s[%d]: 499 - sendall(head, \\n) failed, %zd\n", hst.name, getpid(), n);
					close(newhandle);
					hfin(EXIT_FAILURE);
					return(EXIT_FAILURE);
				}
				if(h->data) // otherwise assume status does not require one
				{
					n=sendall(newhandle, h->data, h->dlen, 0);
					if(n)
					{
						if(hst.debug) fprintf(stderr, "horde: %s[%d]: 499 - sendall(body) failed, %zd; %s\n", hst.name, getpid(), n, strerror(errno));
						close(newhandle);
						hfin(EXIT_FAILURE);
						return(EXIT_FAILURE);
					}
				}
				if((status!=302)&&strcmp(ip, is6?"::1":"127.0.0.1")) // log everything except 302 Found and localhost
				{
					char *ll=logline(status, h->dlen, rpath, ip, "GET", ref, ua);
					hmsg l=new_hmsg("log", ll);
					free(ll);
					hsend(1, l);
					free_hmsg(l);
				}
				free_hmsg(h);
				free(rpath);
			break;
			default:
				err(501, "Not Implemented", NULL, newhandle);
				if(hst.debug) fprintf(stderr, "horde: %s[%d]: 501 Not Implemented (%s)\n", hst.name, getpid(), method);
				close(newhandle);
				hfin(EXIT_FAILURE);
				return(EXIT_FAILURE);
			break;
		}
	}
	free(line);
	free(buf);
	if(hst.debug) fprintf(stderr, "horde: %s[%d]: closing conn\n", hst.name, getpid());
	close(newhandle);
	hfin(EXIT_SUCCESS);
	return(EXIT_SUCCESS);
}

void err(unsigned int status, const char *statusmsg, const char *headers, int fd) // should typically only be used if (proc) unavailable
{
	if(!statusmsg)
		statusmsg=http_statusmsg(status);
	if(!statusmsg)
		statusmsg="???";
	char date[256];
	time_t timer = time(NULL);
	struct tm *tm = gmtime(&timer);
	size_t datelen = strftime(date, sizeof(date), "%F %H:%M:%S", tm);
	bool m=true;
	char *buf=malloc(128+datelen+strlen(HTTPD_VERSION)+(strlen(statusmsg))+(headers?strlen(headers):0));
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
	sendall(fd, buf, strlen(buf), 0);
	if(m) free(buf);
}

bool check_msgs(hstate *hst)
{
	fd_set master, readfds;
	FD_ZERO(&master);
	FD_SET(STDIN_FILENO, &master);
	struct timeval timeout;
	timeout.tv_sec=0;
	timeout.tv_usec=0;
	readfds=master;
	select(STDIN_FILENO+1, &readfds, NULL, NULL, &timeout);
	if(FD_ISSET(STDIN_FILENO, &readfds))
	{
		char *buf=getl(STDIN_FILENO);
		if(!buf) return(false);
		if(*buf)
		{
			hmsg h=hmsg_from_str(buf, true);
			if(h)
			{
				if(!hmsg_state(h, hst))
					fprintf(stderr, "horde: %s[%d]: Unrecognised funct %s in check_msgs()\n", hst->name, getpid(), h->funct);
				free_hmsg(h);
			}
			else
			{
				if(hst->debug) fprintf(stderr, "horde: %s[%d]: Nonsense hmsg received\n", hst->name, getpid());
				if(hst->debug) fprintf(stderr, "\t%s\n", buf);
			}
		}
		free(buf);
		return(true);
	}
	return(false);
}

char *logline(unsigned int status, unsigned long length, const char *path, const char *ip, const char *ac, const char *ref, const char *ua)
{
	char date[256];
	time_t timer = time(NULL);
	struct tm *tm = gmtime(&timer);
	size_t datelen = strftime(date, sizeof(date), "%F %H:%M:%S", tm);
	char st[TL_LONG], sz[TL_LONG];
	sprintf(st, "%u", status);
	sprintf(sz, "%lu", length);
	char *rv=malloc(strlen(ip)+1+datelen+1+strlen(st)+1+strlen(ac)+2+strlen(sz)+2+(path?strlen(path):1)+1+(ref?strlen(ref):2)+1+(ua?strlen(ua):2)+1);
	sprintf(rv, "%s\t%s\t%s %s [%s] %s\t%s\t%s", ip, date, st, ac, sz, (path?path:"?"), (ref?ref:"--"), (ua?ua:".."));
	return(rv);
}
