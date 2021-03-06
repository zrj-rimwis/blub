# $FreeBSD: src/lib/libstand/Makefile,v 1.14.2.10 2002/07/22 14:21:50 ru Exp $
#
# Originally from	$NetBSD: Makefile,v 1.21 1997/10/26 22:08:38 lukem Exp $
#
# Notes:
# - We don't use the libc strerror/sys_errlist because the string table is
#   quite large.
#

LIBSTAND_SRC?=	${.CURDIR}
LIBC_SRC=	${LIBSTAND_SRC}/../libc
LIB?=		stand
NOPROFILE=	YES
NOPIC=		YES
INCS?=		stand.h
MAN?=		libstand.3
CSTD?=		c99
WARNS?=		2

# Mostly OK, some of the libc imports are a bit noisy
CFLAGS+=	-ffreestanding -Wformat
CFLAGS+=	-I${LIBSTAND_SRC}

.if ${MACHINE_ARCH} == "i386" || ${MACHINE_ARCH} == "x86_64"
CFLAGS+=	-mno-mmx -mno-3dnow -mno-sse -mno-sse2 -mno-sse3 -msoft-float
.endif

.if ${MACHINE_ARCH} == "i386"
FORCE_CPUTYPE=	i386
CFLAGS+=	-m32
.if ${CCVER:Mgcc*}
CFLAGS+=	-mpreferred-stack-boundary=2
.endif
.endif

.if ${MACHINE_ARCH} == "x86_64"
CFLAGS+=	-fPIC -mno-red-zone
.endif

# standalone components and stuff we have modified locally
SRCS+=	gzguts.h zutil.h __main.c assert.c bcd.c bswap.c environment.c getopt.c gets.c \
	globals.c pager.c printf.c strdup.c strerror.c strtol.c random.c \
	sbrk.c twiddle.c zalloc.c zalloc_malloc.c

# private (pruned) versions of libc string functions
SRCS+=	strcasecmp.c

# string functions from libc
.PATH:	${LIBC_SRC}/string
.if ${MACHINE_ARCH} == "i386" || ${MACHINE_ARCH} == "x86_64"
SRCS+=	bcmp.c bcopy.c bzero.c ffs.c index.c memccpy.c memchr.c memcmp.c \
        memcpy.c memmove.c memset.c qdivrem.c rindex.c strcat.c strchr.c \
        strcmp.c strcpy.c strcspn.c strlen.c strncat.c strncmp.c strncpy.c \
	strpbrk.c strrchr.c strsep.c strspn.c strstr.c strtok.c swab.c
.endif
.PATH:	${LIBSTAND_SRC}/../../sys/libkern
SRCS+=  icrc32.c

# uuid functions from libc
.PATH: ${LIBC_SRC}/uuid
SRCS+= uuid_equal.c uuid_is_nil.c

# _setjmp/_longjmp
.PATH:	${LIBSTAND_SRC}/${MACHINE_ARCH}

SRCS+=	_setjmp.S

# decompression functionality from libbz2
BZ2DIR=	${LIBSTAND_SRC}/../../contrib/bzip2
.PATH:	${BZ2DIR}
CFLAGS+=-I${BZ2DIR} -DBZ_NO_STDIO -DBZ_NO_COMPRESS
SRCS+=	libstand_bzlib_private.h

CLEANFILES+= bzlib_de.c
bzlib_de.c: bzlib.c	# Don't forget to adjust line numbers on bzip2 update
	awk -v s="#ifndef BZ_NO_COMPRESS" -v e="#endif /* BZ_NO_COMPRESS */"	\
		'NR~/^(33|114|880|88|486|1569)$$/ {x++;print(x%2?s:e)} {print}'	\
		${.ALLSRC} > ${.TARGET}

.for file in bzlib_de.c crctable.c decompress.c huffman.c randtable.c
SRCS+=	_${file}
CLEANFILES+=	_${file}

_${file}: ${file}
	sed "s|bzlib_private\.h|libstand_bzlib_private.h|" \
	    ${.ALLSRC} > ${.TARGET}
.endfor

CLEANFILES+= libstand_bzlib_private.h
libstand_bzlib_private.h: bzlib_private.h
	sed -e 's|<stdlib.h>|"stand.h"|' \
		${.ALLSRC} > ${.TARGET}

# decompression functionality from libz
ZLIBDIR= ${LIBSTAND_SRC}/../../contrib/zlib-1.2
.PATH: ${ZLIBDIR}
CFLAGS+=-DCOMPILING_LIBSTAND -DHAVE_MEMCPY -I${ZLIBDIR}
SRCS+=	adler32.c crc32.c libstand_zutil.h libstand_gzguts.h

.for file in infback.c inffast.c inflate.c inftrees.c zutil.c
SRCS+=	_${file}
CLEANFILES+=	_${file}

_${file}: ${file}
	sed -e "s|zutil\.h|libstand_zutil.h|" \
	    -e "s|gzguts\.h|libstand_gzguts.h|" \
	    ${.ALLSRC} > ${.TARGET}
.endfor

# depend on stand.h being able to be included multiple times
.for file in zutil.h gzguts.h
CLEANFILES+= libstand_${file}
libstand_${file}: ${file}
	sed -e 's|<fcntl.h>|"stand.h"|' \
	    -e 's|<stddef.h>|"stand.h"|' \
	    -e 's|<string.h>|"stand.h"|' \
	    -e 's|<stdio.h>|"stand.h"|' \
	    -e 's|<stdlib.h>|"stand.h"|' \
	    ${.ALLSRC} > ${.TARGET}
.endfor

# io routines
SRCS+=	closeall.c dev.c ioctl.c nullfs.c stat.c \
	fstat.c close.c lseek.c open.c read.c write.c readdir.c

# network routines
SRCS+=	arp.c ether.c inet_ntoa.c in_cksum.c net.c udp.c netif.c rpc.c

# network info services:
SRCS+=	bootp.c rarp.c bootparam.c

# boot filesystems
SRCS+=	ufs.c nfs.c cd9660.c tftp.c gzipfs.c bzipfs.c
SRCS+=	netif.c nfs.c
SRCS+=	dosfs.c ext2fs.c
SRCS+=	splitfs.c
SRCS+=	hammer1.c
SRCS+=	hammer2.c

.include <bsd.lib.mk>
