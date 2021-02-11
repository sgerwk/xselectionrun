PROGS=xselectionrun

CFLAGS=-g -Wall -Wextra
LDLIBS=-lX11

all: ${PROGS}

install: all
	mkdir -p ${DESTDIR}/usr/bin
	cp xselectionrun ${DESTDIR}/usr/bin

clean:
	rm -f ${PROGS} *.o
