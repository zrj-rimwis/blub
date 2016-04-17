# $FreeBSD: src/sys/boot/Makefile,v 1.18 2003/08/16 02:48:20 obrien Exp $

# Build DLoader
SUBDIR+=		dloader

.if ${MACHINE_ARCH} == "x86_64"
# Build i386 version of libstand
SUBDIR+=		libstand32
SUBDIR+=		dloader32
.endif

# Pick the machine-dependent subdir based on the target architecture.
.if ${MACHINE_ARCH} == "x86_64"
SUBDIR+=		pc32
.else
SUBDIR+=		${MACHINE_PLATFORM}
.endif

# UEFI support
.if !defined(LOADER_NO_EFI_SUPPORT)
SUBDIR+=		efi
.endif

.include <bsd.subdir.mk>
