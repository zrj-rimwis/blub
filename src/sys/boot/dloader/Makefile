# The DragonFly Loader
#
#

SRCS=	subs.c config.c cmds.c

# For i386, dloader32/Makefile takes care of the extra CFLAGS
.if ${MACHINE_ARCH} != "i386"
CFLAGS+=	-fPIC -mno-red-zone
CFLAGS+=	-ffreestanding
.endif

.if ${MACHINE_ARCH} == "x86_64"
CFLAGS+=	-mno-mmx -mno-3dnow -mno-sse -mno-sse2 -mno-sse3 -msoft-float
.endif

.if HAVE_PNP
CFLAGS+=	-DHAVE_PNP
.endif
.ifmake testmain
CFLAGS+=	-DTESTMAIN -D_TESTMAIN
SRCS+=		testmain.c
PROG=		testmain
OBJS+=		rel_open.o

rel_open.o: ../common/rel_open.c

.include <bsd.prog.mk>
.else
OBJS+=		stack_protector.o
LIB?=		dloader
INTERNALLIB=	yes

stack_protector.o: ../../libkern/stack_protector.c

.include <bsd.lib.mk>

CWARNFLAGS+=	${CWARNFLAGS.${.TARGET:T:C/^.*\///}}

# XXX: unused_paramater "damaged" in stack_protector.c
CWARNFLAGS.stack_protector.o+=	-Wno-unused-parameter

.endif

CFLAGS+=	-I${.CURDIR}
CFLAGS+=	-I${.CURDIR}/../common
# Ensure to use correct stand.h header
CFLAGS+=	-I${.CURDIR}/../../../lib/libstand
