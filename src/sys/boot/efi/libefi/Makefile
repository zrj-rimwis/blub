# $FreeBSD: head/sys/boot/efi/libefi/Makefile 294028 2016-01-14 18:46:57Z smh $

LIB=	efi
INTERNALLIB=
WARNS?=	3

SRCS=	delay.c efi_console.c efinet.c efipart.c errno.c handles.c \
	libefi.c time.c

.if ${MACHINE_ARCH} == "x86_64"
CFLAGS+= -fPIC -mno-red-zone
.endif
CFLAGS+= -I${.CURDIR}/../include
CFLAGS+= -I${.CURDIR}/../include/${MACHINE}
CFLAGS+= -I${.CURDIR}/../../../../lib/libstand

# Pick up the bootstrap header for some interface items
CFLAGS+= -I${.CURDIR}/../../common

CFLAGS+= -DTERM_EMU

.include <bsd.lib.mk>
