all: iosubmit.c
	gcc -o iosubmit iosubmit.c -laio -O3 -g
