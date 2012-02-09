# Makefile for horde core modules
HORDE := ../../
CC := gcc
CFLAGS := -Wall -Wextra -Werror -std=gnu99 -g -I$(HORDE)

LIBS := $(HORDE)libhorde.o $(HORDE)bits.o
INCLUDES := $(HORDE)libhorde.h $(HORDE)bits.h

all: ext file log stats

file: file.c $(INCLUDES) $(LIBS)
	$(CC) $(CFLAGS) -o $@ file.c $(LIBS) -lmagic

%: %.c $(INCLUDES) $(LIBS)
	$(CC) $(CFLAGS) -o $@ $< $(LIBS)

.PHONY: clean
clean:
	-rm ext log
