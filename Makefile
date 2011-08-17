# Makefile for horde http server
CC := gcc
CFLAGS := -Wall -Wextra -Werror -std=gnu99 -g

all: horde net proc ext pico log

horde: horde.c libhorde.o libhorde.h http.h bits.o bits.h
	$(CC) $(CFLAGS) -o $@ horde.c libhorde.o bits.o

net: net.c libhorde.o libhorde.h http.h bits.o bits.h
	$(CC) $(CFLAGS) -o $@ net.c libhorde.o bits.o

proc: proc.c libhorde.o libhorde.h bits.o bits.h
	$(CC) $(CFLAGS) -o $@ proc.c libhorde.o bits.o

ext: ext.c libhorde.o libhorde.h bits.o bits.h
	$(CC) $(CFLAGS) -o $@ ext.c libhorde.o bits.o

pico: pico.c libhorde.o libhorde.h bits.o bits.h
	$(CC) $(CFLAGS) -o $@ pico.c libhorde.o bits.o

log: log.c libhorde.o libhorde.h bits.o bits.h
	$(CC) $(CFLAGS) -o $@ log.c libhorde.o bits.o

libhorde.o: libhorde.c libhorde.h http.h bits.h

%.o: %.c %.h
	$(CC) $(CFLAGS) -o $@ -c $<
