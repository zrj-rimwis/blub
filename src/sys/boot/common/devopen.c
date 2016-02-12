/*-
 * Copyright (c) 1998 Michael Smith <msmith@freebsd.org>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $FreeBSD: src/sys/boot/common/devopen.c,v 1.4 2003/08/25 23:30:41 obrien Exp $
 */

#include <stand.h>
#include <string.h>

#include "bootstrap.h"

/*
 * Install f_dev and f_devinfo.  Some devices uses the generic
 * devdesc, others will replace it with their own and clear F_DEVDESC.
 */
int
devopen(struct open_file *f, const char *fname, const char **file)
{
	struct devdesc *dev;
	int result;

	result = archsw.arch_getdev((void **)&dev, fname, file);
	if (result)
		return (result);

	/* point to device-specific data so that device open can use it */
	f->f_devdata = dev;
	f->f_flags |= F_DEVDESC;
	result = dev->d_dev->dv_open(f, dev);	/* try to open it */
	if (result != 0) {
		devclose(f);
		return (result);
	}

	/* reference the devsw entry from the open_file structure */
	f->f_dev = dev->d_dev;
	return (0);
}

int
devclose(struct open_file *f)
{
	if (f->f_flags & F_DEVDESC) {
		if (f->f_devdata != NULL) {
			free(f->f_devdata);
			f->f_devdata = NULL;
		}
		f->f_flags &= ~F_DEVDESC;
	}
	return (0);
}

void
devreplace(struct open_file *f, void *devdata)
{
	if (f->f_flags & F_DEVDESC) {
		free(f->f_devdata);
		f->f_flags &= ~F_DEVDESC;
	}
	f->f_devdata = devdata;
}
