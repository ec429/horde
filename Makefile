# Makefile for horde http server
CC := gcc
CFLAGS := -Wall -Wextra -Werror -std=gnu99 -g

LIBS := libhorde.o bits.o
INCLUDES := libhorde.h bits.h

all: horde net proc modules tests run-tests

horde: horde.c $(INCLUDES) $(LIBS)
	$(CC) $(CFLAGS) $(CPPFLAGS) $< $(LDFLAGS) $(LIBS) -o $@

net: net.c $(INCLUDES) $(LIBS) http.h
	$(CC) $(CFLAGS) $(CPPFLAGS) $< $(LDFLAGS) $(LIBS) -o $@

proc: proc.c $(INCLUDES) $(LIBS)
	$(CC) $(CFLAGS) $(CPPFLAGS) $< $(LDFLAGS) $(LIBS) -o $@

tests: tests.c bits.o bits.h
	$(CC) $(CFLAGS) $(CPPFLAGS) $< $(LDFLAGS) bits.o -o $@

.PHONY: modules clean run-tests
FORCE:

modules: $(INCLUDES) $(LIBS)
	$(MAKE) -C modules

local: modules
	$(MAKE) -C modules/local

clean:
	-rm $(LIBS) horde net proc
	$(MAKE) -C modules clean

run-tests: tests horde net proc modules FORCE
	./tests

version.h: FORCE
	sh gitversion

libhorde.o: libhorde.c libhorde.h http.h bits.h version.h

%.o: %.c %.h
	$(CC) $(CFLAGS) -o $@ -c $<
