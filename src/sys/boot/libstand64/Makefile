#
# Careful some nasty assumptions bellow
#

LIBSTAND_SRC=	${.CURDIR}/../../../lib/libstand

.if ${MACHINE_ARCH} == "x86_64"
INTERNALLIB=
INCS=
LIB=	stand64
MAN=
.PATH:	${LIBSTAND_SRC}

.include "${LIBSTAND_SRC}/Makefile"
.endif
