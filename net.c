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
bool check_msgs(const char *name);

bool debug;

int main(int argc, char **argv)
{
	const char *name=argc?argv[0]:"net";
	char *server=malloc(6+strlen(HTTPD_VERSION)+7+1);
	sprintf(server, "horde/%s (Unix)", HTTPD_VERSION);
	debug=false;
	while(check_msgs(name));
	int newhandle;
	struct sockaddr remote;
	socklen_t addr_size=sizeof(remote);
	if((newhandle=accept(3, (struct sockaddr *)&remote, &addr_size))==-1)
	{
		if(debug)
		{
			fprintf(stderr, "horde: %s[%d]: ", name, getpid());
			perror("accept");
		}
		hfin(EXIT_FAILURE);
		return(EXIT_FAILURE);
	}
	else
	{
		close(3); // don't need this any more
		hmsg ac=new_hmsg("accepted", NULL);
		if(!ac)
		{
			if(debug) fprintf(stderr, "horde: %s[%d]: 500 - allocation failure (hmsg ac): new_hmsg: %s", name, getpid(), strerror(errno));
			err(500, "Internal Server Error", NULL, newhandle);
			close(newhandle);
			hfin(EXIT_FAILURE);
			return(EXIT_FAILURE);
		}
		hsend(1, ac);
		free_hmsg(ac);
		if(debug) fprintf(stderr, "horde: %s[%d]: accepted\n", name, getpid());
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
			if(debug) fprintf(stderr, "horde: %s[%d]: 500 - bad address family, is %u", name, getpid(), ((struct sockaddr_in *)&remote)->sin_family);
			err(500, "Internal Server Error", NULL, newhandle);
			close(newhandle);
			hfin(EXIT_FAILURE);
			return(EXIT_FAILURE);
		break;
	}
	char *ip = malloc(is6?INET6_ADDRSTRLEN:INET_ADDRSTRLEN);
	if(ip) inet_ntop(is6?AF_INET6:AF_INET, is6?(const void *)&((struct sockaddr_in6 *)&remote)->sin6_addr:(const void *)&((struct sockaddr_in *)&remote)->sin_addr, ip, is6?INET6_ADDRSTRLEN:INET_ADDRSTRLEN);
	if(ip&&debug) fprintf(stderr, "horde: %s[%d]: remote IP is %s\n", name, getpid(), ip);
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
		if(debug) fprintf(stderr, "horde: %s[%d]: 500 - allocation failure while reading from socket\n", name, getpid());
		err(500, "Internal Server Error", NULL, newhandle);
		close(newhandle);
		hfin(EXIT_FAILURE);
		return(EXIT_FAILURE);
	}
	while(check_msgs(name));
	if(debug) fprintf(stderr, "horde: %s[%d]: read %u bytes\n", name, getpid(), bi);
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
			if(debug) fprintf(stderr, "horde: %s[%d]: 400 - empty request\n", name, getpid());
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
			if(debug) fprintf(stderr, "horde: %s[%d]: 505 HTTP Version Not Supported [%s]\n", name, getpid(), ver);
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
						if(debug) fprintf(stderr, "horde: %s[%d]: 400 Bad Request (Malformed URI) '%s'\n", name, getpid(), uri);
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
				if(debug) fprintf(stderr, "horde: %s[%d]: 501 Not Supported (%s)\n", name, getpid(), method);
				close(newhandle);
				hfin(EXIT_SUCCESS); // success because it's an inapplicable method, not merely an unimplemented feature
				return(EXIT_SUCCESS);
			break;
			case HTTP_METHOD_UNKNOWN:
				err(501, "Unknown Method", NULL, newhandle);
				if(debug) fprintf(stderr, "horde: %s[%d]: 501 Unknown Method (%s(\n", name, getpid(), method);
				close(newhandle);
				hfin(EXIT_SUCCESS); // success because it's an unrecognised method, not merely an unimplemented feature
				return(EXIT_SUCCESS);
			break;
			default:
				err(501, "Not Implemented", NULL, newhandle);
				if(debug) fprintf(stderr, "horde: %s[%d]: 501 Not Implemented (%s)\n", name, getpid(), method);
				close(newhandle);
				hfin(EXIT_FAILURE);
				return(EXIT_FAILURE);
			break;
		}
		struct hdr {http_header name; const char *un_name; const char *value;} *headers=malloc(nlines*sizeof(struct hdr));
		if(!headers)
		{
			if(debug) fprintf(stderr, "horde: %s[%d]: 500 - allocation failure (struct hdr *headers): malloc: %s\n", name, getpid(), strerror(errno));
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
			} // otherwise, we just ignore the line
		}
		while(check_msgs(name));
		switch(m)
		{
			case HTTP_METHOD_GET:;
				if(debug) fprintf(stderr, "horde: %s[%d]: handling GET request (%s) (%u headers)\n", name, getpid(), uri, nhdrs);
				hmsg path=new_hmsg("path", uri);
				if(!path)
				{
					if(debug) fprintf(stderr, "horde: %s[%d]: 500 - allocation failure (new_hmsg): %s\n", name, getpid(), strerror(errno));
					err(500, "Internal Server Error", NULL, newhandle);
					close(newhandle);
					hfin(EXIT_FAILURE);
					return(EXIT_FAILURE);
				}
				if(add_htag(path, "host", host))
				{
					if(debug) fprintf(stderr, "horde: %s[%d]: allocation failure (add_htag): %s\n", name, getpid(), strerror(errno));
					err(500, "Internal Server Error", NULL, newhandle);
					close(newhandle);
					hfin(EXIT_FAILURE);
					return(EXIT_FAILURE);
				}
				if(debug) fprintf(stderr, "horde: %s[%d]: sending request to path\n", name, getpid());
				if(hsend(1, path)>0)
				{
					free(path);
				}
				else
				{
					if(debug) fprintf(stderr, "horde: %s[%d]: 500 - communication failure (hsend): %s\n", name, getpid(), strerror(errno));
					err(500, "Internal Server Error", NULL, newhandle);
					close(newhandle);
					hfin(EXIT_FAILURE);
					return(EXIT_FAILURE);
				}
				hmsg h=NULL;
				while(1)
				{
					char *frompath=getl(STDIN_FILENO);
					if(!(frompath&&*frompath))
					{
						if(debug) fprintf(stderr, "horde: %s[%d]: 500 - failed to read response (getl): %s\n", name, getpid(), strerror(errno));
						err(500, "Internal Server Error", NULL, newhandle);
						close(newhandle);
						hfin(EXIT_FAILURE);
						return(EXIT_FAILURE);
					}
					//fprintf(stderr, "horde: %s[%d]: < '%s'\n", name, getpid(), frompath);
					h=hmsg_from_str(frompath);
					if(h)
					{
						free(frompath);
					}
					else
					{
						if(debug) fprintf(stderr, "horde: %s[%d]: 500 - couldn't understand the response from path: %s\n", name, getpid(), frompath);
						err(500, "Internal Server Error", NULL, newhandle);
						close(newhandle);
						hfin(EXIT_FAILURE);
						return(EXIT_FAILURE);
					}
					const char *from=NULL;
					unsigned int i;
					for(i=0;i<h->nparms;i++)
					{
						if(strcmp(h->p_tag[i], "from")==0)
						{
							from=h->p_value[i];
							break;
						}
					}
					if(strcmp(h->funct, "path"))
					{
						if(strcmp(h->funct, "shutdown")==0)
						{
							if(debug) fprintf(stderr, "horde: %s[%d]: 503 - server is shutting down\n", name, getpid());
							err(503, "Service Unavailable", NULL, newhandle);
							close(newhandle);
							hfin(EXIT_SUCCESS);
							return(EXIT_SUCCESS);
						}
						else if(strcmp(h->funct, "err")==0)
						{
							if(debug) fprintf(stderr, "horde: %s[%d]: 500 - path rewriting failed: %s\n", name, getpid(), h->funct);
							unsigned int i;
							for(i=0;i<h->nparms;i++)
								fprintf(stderr, "horde: %s[%d]:\t(%s|%s)\n", name, getpid(), h->p_tag[i], h->p_value[i]);
							fprintf(stderr, "horde: %s[%d]:\t%s\n", name, getpid(), h->data);
							err(500, "Internal Server Error", NULL, newhandle);
							close(newhandle);
							hfin(EXIT_FAILURE);
							return(EXIT_FAILURE);
						}
						else if(strcmp(h->funct, "debug")==0)
						{
							if(h->data)
							{
								if(strcmp(h->data, "true")==0)
									debug=true;
								else if(strcmp(h->data, "false")==0)
									debug=false;
							}
							else
								debug=true;
						}
						else
						{
							if(debug) fprintf(stderr, "horde: %s[%d]: unrecognised funct '%s'\n", name, getpid(), h->funct);
							hmsg eh=new_hmsg("err", frompath);
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
					else
						break;
				}
				if(!h->data)
				{
					if(debug) fprintf(stderr, "horde: %s[%d]: 500 - path data is empty\n", name, getpid());
					err(500, "Internal Server Error", NULL, newhandle);
					close(newhandle);
					hfin(EXIT_FAILURE);
					return(EXIT_FAILURE);
				}
				if(debug) fprintf(stderr, "horde: %s[%d]: sending request to proc for %s\n", name, getpid(), h->data);
				hmsg p=new_hmsg("proc", h->data);
				if(!p)
				{
					if(debug) fprintf(stderr, "horde: %s[%d]: 500 - allocation failure (hmsg p): new_hmsg: %s\n", name, getpid(), strerror(errno));
					err(500, "Internal Server Error", NULL, newhandle);
					close(newhandle);
					hfin(EXIT_FAILURE);
					return(EXIT_FAILURE);
				}
				free_hmsg(h);
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
						if(debug) fprintf(stderr, "horde: %s[%d]: ign - allocation failure (char *hline): malloc: %s\n", name, getpid(), strerror(errno));
					}
				}
				hsend(1, p);
				free_hmsg(p);
				while(1)
				{
					char *fromproc=getl(STDIN_FILENO);
					if(!(fromproc&&*fromproc))
					{
						if(debug) fprintf(stderr, "horde: %s[%d]: 500 - failed to read response (getl): %s\n", name, getpid(), strerror(errno));
						err(500, "Internal Server Error", NULL, newhandle);
						close(newhandle);
						hfin(EXIT_FAILURE);
						return(EXIT_FAILURE);
					}
					//fprintf(stderr, "horde: %s[%d]: < '%s'\n", name, getpid(), fromproc);
					h=hmsg_from_str(fromproc);
					if(h)
					{
						free(fromproc);
					}
					else
					{
						if(debug) fprintf(stderr, "horde: %s[%d]: 500 - couldn't understand the response from proc: %s\n", name, getpid(), fromproc);
						err(500, "Internal Server Error", NULL, newhandle);
						close(newhandle);
						hfin(EXIT_FAILURE);
						return(EXIT_FAILURE);
					}
					const char *from=NULL;
					unsigned int i;
					for(i=0;i<h->nparms;i++)
					{
						if(strcmp(h->p_tag[i], "from")==0)
						{
							from=h->p_value[i];
							break;
						}
					}
					if(strcmp(h->funct, "proc"))
					{
						if(strcmp(h->funct, "shutdown")==0)
						{
							if(debug) fprintf(stderr, "horde: %s[%d]: 503 - server is shutting down\n", name, getpid());
							err(503, "Service Unavailable", NULL, newhandle);
							close(newhandle);
							hfin(EXIT_SUCCESS);
							return(EXIT_SUCCESS);
						}
						else if(strcmp(h->funct, "err")==0)
						{
							if(debug)
							{
								fprintf(stderr, "horde: %s[%d]: 500 - proc failed: %s\n", name, getpid(), h->funct);
								unsigned int i;
								for(i=0;i<h->nparms;i++)
								{
									fprintf(stderr, "horde: %s[%d]:\t(%s|%s)\n", name, getpid(), h->p_tag[i], h->p_value[i]);
									if(strcmp(h->p_tag[i], "errno")==0)
									{
										fprintf(stderr, "horde: %s[%d]:\t\t%s\n", name, getpid(), strerror(hgetlong(h->p_value[i])));
									}
								}
								fprintf(stderr, "horde: %s[%d]:\t%s\n", name, getpid(), h->data);
							}
							err(500, "Internal Server Error", NULL, newhandle);
							close(newhandle);
							hfin(EXIT_FAILURE);
							return(EXIT_FAILURE);
						}
						else if(strcmp(h->funct, "debug")==0)
						{
							if(h->data)
							{
								if(strcmp(h->data, "true")==0)
									debug=true;
								else if(strcmp(h->data, "false")==0)
									debug=false;
							}
							else
								debug=true;
						}
						else
						{
							if(debug) fprintf(stderr, "horde: %s[%d]: unrecognised funct '%s'\n", name, getpid(), h->funct);
							hmsg eh=new_hmsg("err", fromproc);
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
					else
						break;
				}
				unsigned short status=200;
				const char *statusmsg=NULL;
				unsigned long length=0;
				unsigned int i;
				for(i=0;i<h->nparms;i++)
				{
					if(debug) fprintf(stderr, "horde: %s[%d]:\t(%s|%s)\n", name, getpid(), h->p_tag[i], h->p_value[i]);
					if(strcmp(h->p_tag[i], "status")==0)
					{
						unsigned short ns=hgetshort(h->p_value[i]);
						if((ns<600)&&(ns>99)) status=ns;
					}
					else if(strcmp(h->p_tag[i], "statusmsg")==0)
					{
						statusmsg=h->p_value[i];
					}
					else if(strcmp(h->p_tag[i], "length")==0)
					{
						length=hgetlong(h->p_value[i]);
					}
					else if(strcmp(h->p_tag[i], "read")==0)
					{
						FILE *fp=fopen(h->p_value[i], "r");
						if(fp)
						{
							if(h->data) free(h->data);
							hslurp(fp, &h->data);
						}
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
				char *head=malloc(9+8+strlen(statusmsg)+1+6+datelen+1+8+strlen(server)+1+16+16+1+1);
				if(!head)
				{
					if(debug) fprintf(stderr, "horde: %s[%d]: 500 - allocation failure (char *head): malloc: %s\n", name, getpid(), strerror(errno));
					err(500, "Internal Server Error", NULL, newhandle);
					close(newhandle);
					hfin(EXIT_FAILURE);
					return(EXIT_FAILURE);
				}
				sprintf(head, "HTTP/1.1 %hu %s\nDate: %s\nServer: %s\nContent-Length: %lu\n", status, statusmsg, date, server, h->data?length:0);
				ssize_t n=sendall(newhandle, head, strlen(head), 0);
				if(n)
				{
					if(debug) fprintf(stderr, "horde: %s[%d]: 499 - sendall(head) failed, %zd\n", name, getpid(), n);
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
							if(debug) fprintf(stderr, "horde: %s[%d]: 499 - sendall(extra header) failed, %zd\n", name, getpid(), n);
							close(newhandle);
							hfin(EXIT_FAILURE);
							return(EXIT_FAILURE);
						}
						if((n=sendall(newhandle, "\n", 1, 0)))
						{
							if(debug) fprintf(stderr, "horde: %s[%d]: 499 - sendall(extra header, \\n) failed, %zd\n", name, getpid(), n);
							close(newhandle);
							hfin(EXIT_FAILURE);
							return(EXIT_FAILURE);
						}
					}
				}
				sendall(newhandle, "\n", 1, 0);
				if(h->data) // otherwise assume status does not require one
				{
					char *data=hex_decode(h->data, strlen(h->data));
					n=sendall(newhandle, data, length, 0);
					if(n)
					{
						if(debug) fprintf(stderr, "horde: %s[%d]: 499 - sendall(body) failed, %zd\n", name, getpid(), n);
						close(newhandle);
						hfin(EXIT_FAILURE);
						return(EXIT_FAILURE);
					}
				}
				free_hmsg(h);
			break;
			default:
				err(501, "Not Implemented", NULL, newhandle);
				if(debug) fprintf(stderr, "horde: %s[%d]: 501 Not Implemented (%s)\n", name, getpid(), method);
				close(newhandle);
				hfin(EXIT_FAILURE);
				return(EXIT_FAILURE);
			break;
		}
	}
	free(line);
	free(buf);
	if(debug) fprintf(stderr, "horde: %s[%d]: closing conn\n", name, getpid());
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

bool check_msgs(const char *name)
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
		if(*buf)
		{
			hmsg h=hmsg_from_str(buf);
			if(h)
			{
				if(strcmp(h->funct, "debug")==0)
				{
					if(h->data)
					{
						if(strcmp(h->data, "true")==0)
							debug=true;
						else if(strcmp(h->data, "false")==0)
							debug=false;
					}
					else
						debug=true;
				}
				free_hmsg(h);
			}
			else
			{
				if(debug) fprintf(stderr, "horde: %s[%d]: Nonsense hmsg received\n", name, getpid());
				if(debug) fprintf(stderr, "\t%s\n", buf);
			}
		}
		free(buf);
		return(true);
	}
	return(false);
}
