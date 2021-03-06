BOOT=		gptboot
BINDIR?=	/boot
BINMODE=	444
PROG=		${BOOT}
MAN=		gptboot.8

LIBSTAND_SRC=	${.CURDIR}/../../../../lib/libstand

.PATH: ${.CURDIR}/../boot2
.PATH: ${.CURDIR}/../common
.PATH: ${.CURDIR}/../../common
.PATH: ${.CURDIR}/../../../../lib/libstand
.PATH: ${.CURDIR}/../../../libkern

BOOT_COMCONSOLE_PORT?= 0x3f8
BOOT_COMCONSOLE_SPEED?= 9600
B2SIOFMT?=	0x3

REL1=	0x700
ORG1=	0x7c00
ORG2=	0x0

# Decide level of UFS support.
#GPTBOOT_UFS?=	UFS1_AND_UFS2
#GPTBOOT_UFS?=	UFS2_ONLY
GPTBOOT_UFS?=	UFS1_ONLY

CFLAGS=	-DBOOTPROG=\"gptboot\" \
	-O1 \
	-fomit-frame-pointer \
	-fno-unwind-tables \
	-fno-asynchronous-unwind-tables \
	-mno-mmx -mno-3dnow -mno-sse -mno-sse2 -mno-sse3 \
	-DGPT -DUFS -DBOOT2 -DHAMMER2FS \
	-D${GPTBOOT_UFS} \
	-DSIOPRT=${BOOT_COMCONSOLE_PORT} \
	-DSIOFMT=${B2SIOFMT} \
	-DSIOSPD=${BOOT_COMCONSOLE_SPEED} \
	-I${.CURDIR}/../../common \
	-I${.CURDIR}/../common \
	-I${.CURDIR}/../btx/lib -I. \
	-I${.CURDIR}/../boot2 \
	-I${.CURDIR}/../../.. \
	-Wall -Waggregate-return -Wbad-function-cast -Wcast-align \
	-Wmissing-declarations -Wmissing-prototypes -Wnested-externs \
	-Wpointer-arith -Wshadow -Wstrict-prototypes -Wwrite-strings \
	-Winline

LDFLAGS=	-nostdlib -static -N --gc-sections
LINKSCRIPT=	--script ${.CURDIR}/gptboot.ldscript

LIBSTAND=	${.OBJDIR}/../../libstand32/libstand32.a
CFLAGS+=	-I${LIBSTAND_SRC}

# BTX components
.if exists(${.OBJDIR}/../btx)
BTXDIR=		${.OBJDIR}/../btx
.else
BTXDIR=		${.CURDIR}/../btx
.endif
BTXLDR=		${BTXDIR}/btxldr/btxldr
BTXKERN=	${BTXDIR}/btx/btx
BTXCRT=		${BTXDIR}/lib/crt0.o

CLEANFILES=	gptboot

gptboot: gptldr.bin gptboot.bin ${BTXKERN}
	btxld -v -E ${ORG2} -f bin -b ${BTXKERN} -l gptldr.bin \
	    -o ${.TARGET} gptboot.bin

CLEANFILES+=	gptldr.bin gptldr.out gptldr.o

gptldr.bin: gptldr.out
	objcopy -S -O binary gptldr.out ${.TARGET}

gptldr.out: gptldr.o
	${LD} ${LDFLAGS} -e start -Ttext ${ORG1} -o ${.TARGET} gptldr.o

CLEANFILES+=	gptboot.bin gptboot.out gptboot.o sio.o crc32.o drv.o \
		cons.o util.o

gptboot.bin: gptboot.out
	objcopy -S -O binary gptboot.out ${.TARGET}

gptboot.out: ${BTXCRT} gptboot.o sio.o crc32.o drv.o cons.o util.o hammer2gpt.o icrc32gpt.o
	${LD} ${LDFLAGS} -Ttext ${ORG2} ${LINKSCRIPT} -o ${.TARGET} ${.ALLSRC} ${LIBSTAND}

gptboot.o:	${.CURDIR}/../../common/ufsread.c

CLEANFILES+=	hammer2gpt.o icrc32gpt.o

hammer2gpt.o: hammer2.c
	${CC} ${CFLAGS} ${.ALLSRC} -o ${.TARGET} -c

icrc32gpt.o: icrc32.c
	${CC} ${CFLAGS} ${.ALLSRC} -o ${.TARGET} -c

.include <bsd.prog.mk>

CWARNFLAGS+=	${CWARNFLAGS.${.TARGET:T:C/^.*\///}}

# XXX: unused_paramater in hammer2.c for BOOT2 case
CWARNFLAGS.hammer2gpt.o+=	-Wno-unused-parameter -Wno-maybe-uninitialized
