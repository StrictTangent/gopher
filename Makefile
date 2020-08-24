CC=gcc

CFLAGS= -lmenu -lncurses

PREFIX = /usr/local

FILES = gopher.c argparse.c
OBJECTS = ${FILES:.c=.o}

gopher: $(OBJECTS)
	$(CC) $(CFLAGS) -o gopher $(OBJECTS)

clean:
	rm -rf gopher $(OBJECTS)

install: gopher
	install gopher $(PREFIX)/bin
