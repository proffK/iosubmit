all: iosubmit Makefile

iosubmit: iosubmit.c
	gcc -o $@ $^ -laio -O2

debug: iosubmit.c
	gcc -o iosubmit $^ -laio -g
