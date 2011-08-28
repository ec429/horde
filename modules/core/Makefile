# Makefile for horde core modules
HORDE := ../../
CC := gcc
CFLAGS := -Wall -Wextra -Werror -std=gnu99 -g -I$(HORDE)

LIBS := $(HORDE)libhorde.o $(HORDE)bits.o
INCLUDES := $(HORDE)libhorde.h $(HORDE)bits.h

all: ext log

ext: ext.c $(INCLUDES) $(LIBS)
	$(CC) $(CFLAGS) -o $@ ext.c $(LIBS)

log: log.c $(INCLUDES) $(LIBS)
	$(CC) $(CFLAGS) -o $@ log.c $(LIBS)

