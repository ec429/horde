# Makefile for horde http server
CC := gcc
CFLAGS := -Wall -Wextra -Werror -pedantic -std=gnu99 -g

all: horde net

horde: horde.c libhorde.o libhorde.h bits.o bits.h
	$(CC) $(CFLAGS) -o $@ horde.c libhorde.o bits.o

net: net.c libhorde.o libhorde.h bits.o bits.h
	$(CC) $(CFLAGS) -o $@ net.c libhorde.o bits.o

libhorde.o: libhorde.c libhorde.h bits.h

%.o: %.c %.h
	$(CC) $(CFLAGS) -o $@ -c $<
