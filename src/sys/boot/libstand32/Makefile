#
# Careful some nasty assumptions bellow
#

LIBSTAND_SRC=	${.CURDIR}/../../../lib/libstand

.if ${MACHINE_ARCH} == "x86_64" || ${MACHINE_ARCH} == "i386"
.MAKEFLAGS:  MACHINE_ARCH=i386 MACHINE_PLATFORM=pc32
INTERNALLIB=
INCS=
LIB=	stand32
MAN=
.PATH:	${LIBSTAND_SRC}

.include "${LIBSTAND_SRC}/Makefile"
.endif
