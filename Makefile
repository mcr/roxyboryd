BINDIR =	/usr/local/sbin
MANDIR =	/usr/share/man/man8
CC =		gcc
#CFLAGS =	-g
#SYSVLIBS =	-lnsl -lsocket
#LDFLAGS =	-s ${SYSVLIBS}
LDFLAGS =	-g ${SYSVLIBS}
VERSION	=	$(shell cat VERSION)
CFLAGS =	-O -g -DVERSION="\"${VERSION}\"" -D_LARGEFILE64_SOURCE

all:		roxboryd

roxboryd:	roxboryd.o
	${CC} ${CFLAGS} roxboryd.o ${LDFLAGS} -o roxboryd

roxboryd.o:	roxboryd.c
	${CC} ${CFLAGS} -c roxboryd.c

install:	all
	rm -f ${BINDIR}/roxboryd
	cp roxboryd ${BINDIR}
	rm -f ${MANDIR}/roxboryd.8
	cp roxboryd.8 ${MANDIR}

clean:
	rm -f roxboryd *.o core core.* *.core
