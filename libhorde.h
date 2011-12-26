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
#include <errno.h>

#include "bits.h"
#include "http.h"

#define HTTPD_VERSION	"0.0.4"

typedef struct
{
	char *funct;
	unsigned int nparms;
	char **p_tag;
	char **p_value;
	char *data;
	size_t dlen;
}
*hmsg;

typedef struct
{
	const char *name;
	char *root;
	char *host;
	bool debug;
	bool pipeline;
	bool shutdown;
}
hstate;

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

typedef struct
{
	unsigned int nvars;
	char **name;
	lvalue *var;
}
lvars;

#define NOVARS	(lvars){.nvars=0, .name=NULL, .var=NULL}

// lengths to use for char buffers for hput{long,short}.  TODO: some magic with strlen(#INT_MAX) things? (ANSI stringizing!)
#define TL_LONG		24
#define TL_SIZET	TL_LONG
#define TL_SHORT	16

void hputlong(char *buf, unsigned long val);
unsigned long hgetlong(const char *buf);
void hputshort(char *buf, unsigned short val);
unsigned short hgetshort(const char *buf);

hmsg new_hmsg(const char *funct, const char *data);
hmsg new_hmsg_d(const char *funct, const char *data, size_t dlen);
int add_htag(hmsg h, const char *p_tag, const char *p_value);
char *str_from_hmsg(const hmsg h);
hmsg hmsg_from_str(const char *str, bool read); // should we hmsg_read() where appropriate?  (Typically, YES except in the dispatcher)
hmsg hmsg_read(hmsg h); // apply a (read) tag if one is present (and data is absent)
void hst_init(hstate *s, const char *name, bool pipeline);
bool hmsg_state(hmsg h, hstate *s); // returns false if message was not a recognised state message
const char *gettag(hmsg h, const char *tag);
void free_hmsg(hmsg h);

lform new_lform(const char *funct);
int add_lchld(lform lf, lform chld);
lform lform_str(const char *str, const char **end);
char *str_lform(const lform lf);
void free_lform(lform lf);

lvalue l_eval(lform lf, lvars lv, lvalue app(lform lf, lvars lv));

bool l_asbool(lvalue val);
lvalue l_num(unsigned long num);
lvalue l_str(char *str);
lvalue l_blo(char *bytes, size_t len);
lvalue l_dup(lvalue val);
void free_lvalue(lvalue l);

bool find_lvar(lvars lv, const char *name, unsigned int *i);
void l_addvar(lvars *lv, const char *name, lvalue val);
void free_lvars(lvars *lv);

ssize_t hsend(int fd, const hmsg h);
void hfin(unsigned char status);

http_method get_method(const char *name);
http_version get_version(const char *name);
http_header get_header(const char *name);
const char *http_statusmsg(unsigned int status); // returns a static string (do not free()!)
