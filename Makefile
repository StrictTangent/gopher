CC=gcc

CFLAGS= -g -Wall -lmenu -lncurses

PREFIX = /usr/local

FILES = gopher.c argparse.c
OBJECTS = ${FILES:.c=.o}

gopher: $(OBJECTS)
	$(CC) -o gopher $(OBJECTS) $(CFLAGS)

clean:
	rm -rf gopher $(OBJECTS)

install: gopher
	install gopher $(PREFIX)/bin
