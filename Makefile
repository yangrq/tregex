FLAGS=-O3 -std=c99 -Wall -Wextra -Wno-unused-result -Wno-implicit-fallthrough -DUSE_LABELS_AS_VALUES

.PHONY: clean

tregex: tregex.c main.c tregex.h

clean:
	rm -rf tregex
