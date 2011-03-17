#pragma once
#define _GNU_SOURCE
/*
	horde: modular http server
	Copyright (C) 2011 Edward Cree

	Licensed under GNU GPLv3+; see top of horde.c for details
	
	libhorde: provides routines for module communication
*/

#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <ctype.h>
#include <regex.h>

#include "bits.h"
#include "http.h"

#define HTTPD_VERSION	"0.0.1"

typedef struct
{
	char *funct;
	unsigned int nparms;
	char **p_tag;
	char **p_value;
	char *data;
}
*hmsg;

char *hex_encode(const char *src, size_t srclen);
char *hex_decode(const char *src, size_t srclen);

typedef struct _lnode
{
	char *funct;
	unsigned int nchld;
	struct _lnode *chld;
}
*lform;

typedef struct
{
	enum {L_NUM, L_STR, L_BLO} type;
	union
	{
		unsigned long num;
		char *str;
		struct _blo
		{
			char *bytes;
			size_t len;
		}
		blo;
	}
	data;
}
lvalue;

void hputlong(char *buf, unsigned long val);
unsigned long hgetlong(const char *buf);
void hputshort(char *buf, unsigned short val);
unsigned short hgetshort(const char *buf);

ssize_t sendall(int sockfd, const void *buf, size_t length, int flags);

hmsg new_hmsg(const char *funct, const char *data);
int add_htag(hmsg h, const char *p_tag, const char *p_value);
char *str_from_hmsg(const hmsg h);
hmsg hmsg_from_str(const char *str);
void free_hmsg(hmsg h);

lform new_lform(const char *funct);
int add_lchld(lform lf, lform chld);
lform lform_str(const char *str, const char **end);
char *str_lform(const lform lf);
void free_lform(lform lf);

lvalue l_eval(lform lf, lvalue app(lform lf));

bool l_asbool(lvalue val);
lvalue l_num(unsigned long num);
lvalue l_str(char *str);
lvalue l_blo(char *bytes, size_t len);

ssize_t hsend(int fd, const hmsg h);
void hfin(unsigned char status);

http_method get_method(const char *name);
http_version get_version(const char *name);
http_header get_header(const char *name);
const char *http_statusmsg(unsigned int status); // returns a static string (do not free()!)
