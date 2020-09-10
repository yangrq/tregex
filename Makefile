CFLAGS=-O3 -Wall -DUSE_LABELS_AS_VALUES

.PHONY: clean

tregex: tregex.c main.c tregex.h

clean:
	rm -rf tregex
