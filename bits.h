#pragma once
// generic helper functions; 'bits and pieces'
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>

#define max(a,b) ((a)>(b)?(a):(b))
#define min(a,b) ((a)<(b)?(a):(b))

typedef uint32_t uchar_t;

char * fgetl(FILE *); // gets a line of string data; returns a malloc-like pointer
char * slurp(FILE *); // reads an entire file up to EOF; returns a malloc-like pointer
ssize_t hslurp(FILE *fp, char **buf); // reads a binary file up to EOF; returns size and places malloc-like pointer to hex data in buf
char * getl(int fd); // like fgetl but with a file descriptor instead of a handle
void init_char(char **buf, unsigned int *l, unsigned int *i); // initialises a string buffer in heap.  *buf becomes a malloc-like pointer
void append_char(char **buf, unsigned int *l, unsigned int *i, char c); // adds a character to a string buffer in heap (and realloc()s if needed)
void append_str(char **buf, unsigned int *l, unsigned int *i, const char *s); // adds a string to a string buffer in heap (and realloc()s if needed)

void u_init_char(uchar_t **buf, unsigned int *l, unsigned int *i); // initialises a unicode string buffer in heap.  *buf becomes a malloc-like pointer
void u_append_char(uchar_t **buf, unsigned int *l, unsigned int *i, uchar_t c); // adds a unicode character to a unicode string buffer in heap (and realloc()s if needed)
void u_append_str(uchar_t **buf, unsigned int *l, unsigned int *i, const uchar_t *s); // adds a unicode string to a unicode string buffer in heap (and realloc()s if needed)
uchar_t *u_strdup(const uchar_t *s); // duplicates a unicode string on the heap; returns a malloc-like pointer

char *normalise_path(char *p); // handles .. and . in paths; returns a malloc-like pointer
