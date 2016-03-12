/*
 * Copyright (c) 2011-2012 The DragonFly Project.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE
 * COPYRIGHT HOLDERS OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY OR CONSEQUENTIAL DAMAGES (INCLUDING,
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED
 * AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT
 * OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

/*
 * Everything that shouldn't be put into the public header goes here.
 */

#ifndef _GPT_PRIVATE_H_
#define _GPT_PRIVATE_H_

#include <sys/types.h>
#include <sys/param.h>
#include <sys/stat.h>

/* stolen from sys/boot/common/bootstrap.h */
#ifndef CTASSERT                /* Allow lint to override */
#define CTASSERT(x)             _CTASSERT(x, __LINE__)
#define _CTASSERT(x, y)         __CTASSERT(x, y)
#define __CTASSERT(x, y)        typedef char __assert ## y[(x) ? 1 : -1]
#endif

/* XXX: should be in sys/diskmbr.h */
#ifndef DOSPTYP_UNUSED
#define DOSPTYP_UNUSED	0x00
#endif
#ifndef DOSPTYP_EFI
#define DOSPTYP_EFI	DOSPTYP_GPT
#endif

struct gd {
	char device_name[MAXPATHLEN];
	int fd;
	int flags;
	int verbose;
	int lbawidth;
	struct map *mediamap;
	struct map *tbl, *lbt, *gpt, *tpg;
	u_int secsz;
	off_t mediasz;
	struct stat sb;
};

#endif /* _GPT_PRIVATE_H_ */
