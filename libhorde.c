#define _GNU_SOURCE
/*
	horde: modular http server
	Copyright (C) 2011 Edward Cree

	Licensed under GNU GPLv3+; see top of horde.c for details
	
	libhorde: provides routines for module communication
*/
#include "libhorde.h"

void hex_encode(char **buf, const char *src, size_t srclen);
void hex_decode(char **buf, const char *src, size_t srclen);

hmsg new_hmsg(const char *funct, const char *data);
void add_htag(hmsg h, const char *p_tag, const char *p_value);
char *str_from_hmsg(const hmsg h);
hmsg hmsg_from_str(const char *str);
void free_hmsg(hmsg h);
