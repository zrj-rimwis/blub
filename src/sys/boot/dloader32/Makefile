.MAKEFLAGS:  MACHINE_ARCH=i386 MACHINE_PLATFORM=pc32 REALLY_X86_64=true

.if exists(../${MACHINE_PLATFORM}/Makefile.inc)
.include "../${MACHINE_PLATFORM}/Makefile.inc"
.endif

LIB=	dloader32
INTERNALLIB=	yes

CFLAGS+=	-I${.CURDIR}/../common
CFLAGS+=	-I${.CURDIR}/../dloader
# Ensure to use correct stand.h header
CFLAGS+=	-I${.CURDIR}/../../../lib/libstand

OBJS+=		cmds_32.o config_32.o subs_32.o stack_protector_32.o

stack_protector_32.o: ${.CURDIR}/../../libkern/stack_protector.c
	${CC} ${CFLAGS} ${.ALLSRC} -o ${.TARGET} -c

cmds_32.o: ${.CURDIR}/../dloader/cmds.c
	${CC} ${CFLAGS} ${.ALLSRC} -o ${.TARGET} -c

subs_32.o: ${.CURDIR}/../dloader/subs.c
	${CC} ${CFLAGS} ${.ALLSRC} -o ${.TARGET} -c

config_32.o: ${.CURDIR}/../dloader/config.c
	${CC} ${CFLAGS} ${.ALLSRC} -o ${.TARGET} -c

.include <bsd.lib.mk>

CWARNFLAGS+=	${CWARNFLAGS.${.TARGET:T:C/^.*\///}}

# XXX: unused_paramater "damaged" in stack_protector.c
CWARNFLAGS.stack_protector_32.o+=	-Wno-unused-parameter
