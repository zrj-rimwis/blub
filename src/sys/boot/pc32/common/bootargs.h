/*-
 * Copyright (c) 2012 Andriy Gapon <avg@FreeBSD.org>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms are freely
 * permitted provided that the above copyright notice and this
 * paragraph and the following disclaimer are duplicated in all
 * such forms.
 *
 * This software is provided "AS IS" and without any express or
 * implied warranties, including, without limitation, the implied
 * warranties of merchantability and fitness for a particular
 * purpose.
 *
 * $FreeBSD: head/sys/boot/i386/common/bootargs.h 235154 2012-05-09 08:04:29Z avg $
 */

#ifndef _BOOT_I386_ARGS_H_
#define	_BOOT_I386_ARGS_H_

struct bootargs
{
	uint32_t	howto;
	uint32_t	bootdev;
	uint32_t	bootflags;
	uint32_t	pxeinfo;
	uint32_t	res2;
	uint32_t	bootinfo;

	/*
	 * If KARGS_FLAGS_EXTARG is set in bootflags, then the above fields
	 * are followed by a uint32_t field that specifies a size of the
	 * extended arguments (including the size field).
	 */
};

#endif	/* !_BOOT_I386_ARGS_H_ */
