# Makefile for pico, a horde module
HORDE := ../../
CC := gcc
CFLAGS := -Wall -Wextra -Werror -std=gnu99 -g -I$(HORDE)

all: pico

pico: pico.c $(HORDE)libhorde.o $(HORDE)libhorde.h $(HORDE)bits.o $(HORDE)bits.h
	$(CC) $(CFLAGS) -o $@ pico.c $(HORDE)libhorde.o $(HORDE)bits.o
