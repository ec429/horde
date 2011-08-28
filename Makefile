# Makefile for horde http server
CC := gcc
CFLAGS := -Wall -Wextra -Werror -std=gnu99 -g

LIBS := libhorde.o bits.o
INCLUDES := libhorde.h bits.h

all: horde net proc modules

horde: horde.c $(INCLUDES) $(LIBS)
	$(CC) $(CFLAGS) -o $@ horde.c $(LIBS)

net: net.c $(INCLUDES) $(LIBS) http.h
	$(CC) $(CFLAGS) -o $@ net.c $(LIBS)

proc: proc.c $(INCLUDES) $(LIBS)
	$(CC) $(CFLAGS) -o $@ proc.c $(LIBS)

.PHONY: modules

modules: $(INCLUDES) $(LIBS)
	$(MAKE) -C modules

libhorde.o: libhorde.c libhorde.h http.h bits.h

%.o: %.c %.h
	$(CC) $(CFLAGS) -o $@ -c $<
