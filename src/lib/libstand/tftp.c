/*	$NetBSD: tftp.c,v 1.4 1997/09/17 16:57:07 drochner Exp $	 */

/*
 * Copyright (c) 1996
 *	Matthias Drochner.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *	This product includes software developed for the NetBSD Project
 *	by Matthias Drochner.
 * 4. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * $FreeBSD: src/lib/libstand/tftp.c,v 1.2.6.4 2001/07/14 14:00:03 mikeh Exp $
 */

/*
 * Simple TFTP implementation for libsa.
 * Assumes:
 *  - socket descriptor (int) at open_file->f_devdata
 *  - server host IP in global servip
 * Restrictions:
 *  - read only
 *  - lseek only with SEEK_SET or SEEK_CUR
 *  - no big time differences between transfers (<tftp timeout)
 */

#include <sys/param.h>
#include <sys/stat.h>
#include <netinet/in.h>
#include <netinet/udp.h>
#include <netinet/ip.h>
#include <netinet/in_systm.h>
#include <arpa/tftp.h>

#include <string.h>

#include "stand.h"
#include "net.h"
#include "netif.h"

#include "tftp.h"

static int	tftp_open(const char *path, struct open_file *f);
static int	tftp_close(struct open_file *f);
static int	tftp_read(struct open_file *f, void *buf, size_t size, size_t *resid);
static int	tftp_write(struct open_file *f, void *buf, size_t size, size_t *resid);
static off_t	tftp_seek(struct open_file *f, off_t offset, int where);
static int	tftp_stat(struct open_file *f, struct stat *sb);

struct fs_ops tftp_fsops = {
	"tftp",
	tftp_open,
	tftp_close,
	tftp_read,
	tftp_write,
	tftp_seek,
	tftp_stat,
	null_readdir
};

extern struct in_addr servip;

static int      tftpport = 2000;

#define RSPACE 520		/* max data packet, rounded up */

struct tftp_handle {
	struct iodesc  *iodesc;
	int             currblock;	/* contents of lastdata */
	int             islastblock;	/* flag */
	int             validsize;
	int             off;
	char           *path;	/* saved for re-requests */
	struct {
		u_char header[HEADER_SIZE];
		struct tftphdr t;
		u_char space[RSPACE];
	} __packed __aligned(4) lastdata;
};

static int tftperrors[8] = {
	0,			/* ??? */
	ENOENT,
	EPERM,
	ENOSPC,
	EINVAL,			/* ??? */
	EINVAL,			/* ??? */
	EEXIST,
	EINVAL			/* ??? */
};

static ssize_t
recvtftp(struct iodesc *d, void *pkt, size_t max_len, time_t tleft)
{
	struct tftphdr *t;
	ssize_t len;
	ssize_t tmp_len;

	/*
	 * Note: errno of 0 with -1 return means udp poll failed or
	 * packet was not for us.
	 *
	 * We may end up broadcasting the initial TFTP request.  Take the
	 * first DATA result and save any ERROR result in case we do not
	 * get a DATA.
	 */
	errno = 0;
	bzero(pkt, max_len);
	if (d->xid == 1) {
		len = -1;
		while ((tmp_len = readudp(d, pkt, max_len, tleft)) > 0) {
			len = tmp_len;
			t = (struct tftphdr *)pkt;
			if (ntohs(t->th_opcode) == DATA)
				break;
		}
	} else {
		len = readudp(d, pkt, max_len, tleft);
	}
	if ((int)len < (int)sizeof(*t))
		return (-1);
	t = (struct tftphdr *)pkt;
	errno = 0;

	switch (ntohs(t->th_opcode)) {
	case DATA: {
		int got;

		if (htons(t->th_block) != d->xid) {
			/*
			 * Expected block?
			 */
			return (-1);
		}
		if (d->xid == 1) {
			/*
			 * First data packet from new port.  Set destip in
			 * case we got replies from multiple hosts, so only
			 * one host is selected.
			 */
			struct udphdr *uh;
			struct ip *ip;

			uh = (struct udphdr *) pkt - 1;
			ip = (struct ip *)uh - 1;
			d->destport = uh->uh_sport;
			d->destip = ip->ip_src;
		} /* else check uh_sport has not changed??? */
		got = len - (t->th_data - (char *)t);
		return got;
	}
	case ERROR:
		if ((unsigned) ntohs(t->th_code) >= 8) {
			printf("illegal tftp error %d\n", ntohs(t->th_code));
			errno = EIO;
		} else {
#ifdef TFTP_DEBUG
			printf("tftp-error %d\n", ntohs(t->th_code));
#endif
			errno = tftperrors[ntohs(t->th_code)];
		}
		return (-1);
	default:
#ifdef TFTP_DEBUG
		printf("tftp type %d not handled\n", ntohs(t->th_opcode));
#endif
		return (-1);
	}
}

/* send request, expect first block (or error) */
static int
tftp_makereq(struct tftp_handle *h)
{
	struct {
		u_char header[HEADER_SIZE];
		struct tftphdr  t;
		u_char space[FNAME_SIZE + 6];
	} __packed __aligned(4) wbuf;
	char           *wtail;
	int             l;
	ssize_t         res;
	struct tftphdr *t;

	wbuf.t.th_opcode = htons((u_short) RRQ);
	wtail = wbuf.t.th_stuff;
	l = strlen(h->path);
	bcopy(h->path, wtail, l + 1);
	wtail += l + 1;
	bcopy("octet", wtail, 6);
	wtail += 6;

	t = &h->lastdata.t;

	/* h->iodesc->myport = htons(--tftpport); */
	h->iodesc->myport = htons(tftpport + (getsecs() & 0x3ff));
	h->iodesc->destport = htons(IPPORT_TFTP);
	h->iodesc->xid = 1;	/* expected block */

	res = sendrecv(h->iodesc, sendudp, &wbuf.t, wtail - (char *) &wbuf.t,
		       recvtftp, t, sizeof(*t) + RSPACE);

	if (res == -1)
		return (errno);

	h->currblock = 1;
	h->validsize = res;
	h->islastblock = 0;
	if (res < SEGSIZE)
		h->islastblock = 1;	/* very short file */
	return (0);
}

/* ack block, expect next */
static int
tftp_getnextblock(struct tftp_handle *h)
{
	struct {
		u_char header[HEADER_SIZE];
		struct tftphdr t;
	} __packed __aligned(4) wbuf;
	char           *wtail;
	int             res;
	struct tftphdr *t;

	/*
	 * Ack previous block
	 */
	wbuf.t.th_opcode = htons((u_short) ACK);
	wtail = (char *) &wbuf.t.th_block;
	wbuf.t.th_block = htons((u_short) h->currblock);
	wtail += 2;

	t = &h->lastdata.t;

	h->iodesc->xid = h->currblock + 1;	/* expected block */

	res = sendrecv(h->iodesc, sendudp, &wbuf.t, wtail - (char *) &wbuf.t,
		       recvtftp, t, sizeof(*t) + RSPACE);

	if (res == -1)		/* 0 is OK! */
		return (errno);

	h->currblock++;
	h->validsize = res;
	if (res < SEGSIZE)
		h->islastblock = 1;	/* EOF */
	return (0);
}

static int
tftp_open(const char *path, struct open_file *f)
{
	struct tftp_handle *tftpfile;
	struct iodesc  *io;
	int             res;

#ifndef __i386__
	if (strcmp(f->f_dev->dv_name, "net") != 0)
		return (EINVAL);
#endif

	tftpfile = (struct tftp_handle *) malloc(sizeof(*tftpfile));
	if (!tftpfile)
		return (ENOMEM);

	tftpfile->iodesc = io = socktodesc(*(int *) (f->f_devdata));
	if (io == NULL) {
		free(tftpfile);
		return (EINVAL);
	}

	io->destip = servip;
	tftpfile->off = 0;
	tftpfile->path = strdup(path);
	if (tftpfile->path == NULL) {
	    free(tftpfile);
	    return(ENOMEM);
	}

	res = tftp_makereq(tftpfile);

	if (res) {
		free(tftpfile->path);
		free(tftpfile);
		return (res);
	}
	f->f_fsdata = (void *) tftpfile;
	return (0);
}

/*
 * Parameters:
 *	resid:	out
 */
static int
tftp_read(struct open_file *f, void *addr, size_t size, size_t *resid)
{
	struct tftp_handle *tftpfile;
	static int      tc = 0;
	tftpfile = (struct tftp_handle *) f->f_fsdata;

	while (size > 0) {
		int needblock, count;

		if (!(tc++ % 16))
			twiddle();

		needblock = tftpfile->off / SEGSIZE + 1;

		if (tftpfile->currblock > needblock)	/* seek backwards */
			tftp_makereq(tftpfile);	/* no error check, it worked
						 * for open */

		while (tftpfile->currblock < needblock) {
			int res;

			res = tftp_getnextblock(tftpfile);
			if (res) {	/* no answer */
#ifdef TFTP_DEBUG
				printf("tftp: read error\n");
#endif
				return (res);
			}
			if (tftpfile->islastblock)
				break;
		}

		if (tftpfile->currblock == needblock) {
			int offinblock, inbuffer;

			offinblock = tftpfile->off % SEGSIZE;

			inbuffer = tftpfile->validsize - offinblock;
			if (inbuffer < 0) {
#ifdef TFTP_DEBUG
				printf("tftp: invalid offset %d\n",
				    tftpfile->off);
#endif
				return (EINVAL);
			}
			count = (size < inbuffer ? size : inbuffer);
			bcopy(tftpfile->lastdata.t.th_data + offinblock,
			      addr, count);

			addr = (char *)addr + count;
			tftpfile->off += count;
			size -= count;

			if ((tftpfile->islastblock) && (count == inbuffer))
				break;	/* EOF */
		} else {
#ifdef TFTP_DEBUG
			printf("tftp: block %d not found\n", needblock);
#endif
			return (EINVAL);
		}

	}

	if (resid)
		*resid = size;
	return (0);
}

static int
tftp_close(struct open_file *f)
{
	struct tftp_handle *tftpfile;
	tftpfile = (struct tftp_handle *) f->f_fsdata;

	/* let it time out ... */
	f->f_fsdata = NULL;
	if (tftpfile) {
		free(tftpfile->path);
		free(tftpfile);
		f->f_fsdata = NULL;
	}
	return (0);
}

/*
 * Parameters:
 *	resid:	out
 */
static int
tftp_write(struct open_file *f __unused, void *start __unused, size_t size __unused,
    size_t *resid __unused)
{
	return (EROFS);
}

static int
tftp_stat(struct open_file *f __unused, struct stat *sb)
{
	sb->st_mode = 0444 | S_IFREG;
	sb->st_nlink = 1;
	sb->st_uid = 0;
	sb->st_gid = 0;
	sb->st_size = -1;
	return (0);
}

static off_t
tftp_seek(struct open_file *f, off_t offset, int where)
{
	struct tftp_handle *tftpfile;
	tftpfile = (struct tftp_handle *) f->f_fsdata;

	switch (where) {
	case SEEK_SET:
		tftpfile->off = offset;
		break;
	case SEEK_CUR:
		tftpfile->off += offset;
		break;
	default:
		errno = EOFFSET;
		return (-1);
	}
	return (tftpfile->off);
}
