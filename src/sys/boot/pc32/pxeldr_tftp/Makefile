# Works like the normal loader except the kernel, modules, and other loader
# files are loaded via TFTP instead of NFS.

ORG=		0x7c00

LDR=		pxeldr
BOOT=		pxeboot_tftp
LOADER=		loader_tftp
PROG=		${BOOT}
MAN=		${BOOT}.8
STRIP=
BINDIR?=	/boot

.PATH: ${.CURDIR}/../pxeldr

.if defined(BOOT_PXELDR_PROBE_KEYBOARD)
AFLAGS+=	-DPROBE_KEYBOARD
.endif

.if defined(BOOT_PXELDR_ALWAYS_SERIAL)
AFLAGS+=	-DALWAYS_SERIAL
.endif

.if exists(${.OBJDIR}/../${LOADER})
LOADERBIN=	${.OBJDIR}/../${LOADER}/loader_tftp.bin
.else
LOADERBIN=	${.CURDIR}/../${LOADER}/loader_tftp.bin
.endif

.include "${.CURDIR}/../pxeldr/Makefile"
