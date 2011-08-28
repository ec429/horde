# Makefile for pico, a horde module
HORDE := ../../
CC := gcc
CFLAGS := -Wall -Wextra -Werror -std=gnu99 -g -I$(HORDE)

LIBS := $(HORDE)libhorde.o $(HORDE)bits.o
INCLUDES := $(HORDE)libhorde.h $(HORDE)bits.h

all: pico

pico: pico.c $(INCLUDES) $(LIBS)
	$(CC) $(CFLAGS) -o $@ pico.c $(LIBS)

.PHONY: clean
clean:
	-rm pico
