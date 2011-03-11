#pragma once
#define _GNU_SOURCE
/*
	horde: modular http server
	Copyright (C) 2011 Edward Cree

	Licensed under GNU GPLv3+; see top of horde.c for details
	
	libhorde: provides routines for module communication
*/
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <ctype.h>

#include "bits.h"

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

hmsg new_hmsg(const char *funct, const char *data);
int add_htag(hmsg h, const char *p_tag, const char *p_value);
char *str_from_hmsg(const hmsg h);
hmsg hmsg_from_str(const char *str);
void free_hmsg(hmsg h);
