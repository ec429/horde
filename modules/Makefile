# Makefile for horde modules
MODULES := pico core

all: $(MODULES)

.PHONY: all $(MODULES) clean

$(MODULES):
	$(MAKE) -C $@

clean:
	for dir in $(MODULES); do \
	  $(MAKE) -C $$dir clean; \
	done

