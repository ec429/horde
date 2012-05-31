# Makefile for persist, a horde module
HORDE := ../../
CC := gcc
CFLAGS := -Wall -Wextra -Werror -std=gnu99 -g -I$(HORDE)

LIBS := $(HORDE)libhorde.o $(HORDE)bits.o
INCLUDES := $(HORDE)libhorde.h $(HORDE)bits.h

all: persist

persist: persist.c $(INCLUDES) $(LIBS)
	$(CC) $(CFLAGS) $(CPPFLAGS) $< $(LDFLAGS) $(LIBS) -o $@

.PHONY: clean
clean:
	-rm persist
