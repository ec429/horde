#pragma once
#define _GNU_SOURCE
/*
	horde: modular http server
	Copyright (C) 2011 Edward Cree

	Licensed under GNU GPLv3+; see top of horde.c for details
	
	libhorde: provides routines for module communication
*/
#include <stdlib.h>
#include <sys/types.h>

void hex_encode(char **buf, const char *src, size_t srclen);
void hex_decode(char **buf, const char *src, size_t srclen);
