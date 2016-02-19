# $FreeBSD: head/sys/boot/i386/pmbr/Makefile 264400 2014-04-13 05:21:56Z imp $

PROG=		pmbr
NOMAN=
STRIP=
BINDIR?=	/boot
BINMODE=	444

ORG=	0x600

pmbr: pmbr.o
	${LD} ${LDFLAGS} -N -e start -Ttext ${ORG} -o pmbr.out pmbr.o
	objcopy -S -O binary pmbr.out ${.TARGET}

CLEANFILES+= pmbr.out pmbr.o

.include <bsd.prog.mk>
