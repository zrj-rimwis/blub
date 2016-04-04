/*-
 * Copyright (c) 2002 Networks Associates Technology, Inc.
 * All rights reserved.
 *
 * This software was developed for the FreeBSD Project by Marshall
 * Kirk McKusick and Network Associates Laboratories, the Security
 * Research Division of Network Associates, Inc. under DARPA/SPAWAR
 * contract N66001-01-C-8035 ("CBOSS"), as part of the DARPA CHATS
 * research program
 *
 * Copyright (c) 1998 Robert Nordier
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
 * $FreeBSD: src/sys/boot/common/ufsread.c,v 1.12 2003/08/25 23:30:41 obrien Exp $
 */

#include <vfs/ufs/dir.h>
#include "dinode.h"
#include "fs.h"

#ifdef UFS_SMALL_CGBASE
/* XXX: Revert to old (broken for over 1.5Tb filesystems) version of cgbase
   (see sys/ufs/ffs/fs.h rev 1.39) so that small boot loaders (e.g. boot2) can
   support both UFS1 and UFS2. */
#undef cgbase
#define cgbase(fs, c)   ((ufs2_daddr_t)((fs)->fs_fpg * (c)))
#endif

/*
 * We use 4k `virtual' blocks for filesystem data, whatever the actual
 * filesystem block size. FFS blocks are always a multiple of 4k.
 */
#define VBLKSHIFT	12
#define VBLKSIZE	(1 << VBLKSHIFT)
#define VBLKMASK	(VBLKSIZE - 1)
#define DBPERVBLK	(VBLKSIZE / DEV_BSIZE)
#define INDIRPERVBLK(fs) (NINDIR(fs) / ((fs)->fs_bsize >> VBLKSHIFT))
#define IPERVBLK(fs)	(INOPB(fs) / ((fs)->fs_bsize >> VBLKSHIFT))
#define INO_TO_VBA(fs, ipervblk, x) \
    (fsbtodb(fs, cgimin(fs, ino_to_cg(fs, x))) + \
    (((x) % (fs)->fs_ipg) / (ipervblk) * DBPERVBLK))
#define INO_TO_VBO(ipervblk, x) ((x) % ipervblk)
#define FS_TO_VBA(fs, fsb, off) (fsbtodb(fs, fsb) + \
    ((off) / VBLKSIZE) * DBPERVBLK)
#define FS_TO_VBO(fs, fsb, off) ((off) & VBLKMASK)

/* Buffers that must not span a 64k boundary. */
struct ufs_dmadat {
	struct boot2_dmadat boot2;
	char blkbuf[VBLKSIZE];	/* filesystem blocks */
	char indbuf[VBLKSIZE];	/* indir blocks */
	char sbbuf[SBLOCKSIZE];	/* superblock */
};

#define fsdmadat	((struct ufs_dmadat *)boot2_dmadat)

static boot2_ino_t boot2_ufs_lookup(const char *);
static ssize_t boot2_ufs_read(boot2_ino_t, void *, size_t);
static int boot2_ufs_init(void);

const struct boot2_fsapi boot2_ufs_api = {
	.fsinit = boot2_ufs_init,
	.fslookup = boot2_ufs_lookup,
	.fsread = boot2_ufs_read
};

static __inline__ int
fsfind(const char *name, ufs_ino_t *ino)
{
	char buf[DEV_BSIZE];
	struct direct *d;
	char *s;
	ssize_t n;

	fs_off = 0;
	while ((n = boot2_ufs_read(*ino, buf, DEV_BSIZE)) > 0)
		for (s = buf; s < buf + DEV_BSIZE;) {
			d = (void *)s;
			if (ls)
				printf("%s ", d->d_name);
			else if (!strcmp(name, d->d_name)) {
				*ino = d->d_ino;
				return d->d_type;
			}
			s += d->d_reclen;
		}
	if (n != -1 && ls)
		printf("\n");
	return 0;
}

static boot2_ino_t
boot2_ufs_lookup(const char *path)
{
	char name[MAXNAMLEN + 1];
	const char *s;
	ufs_ino_t ino;
	ssize_t n;
	int dt;

	ino = ROOTINO;
	dt = DT_DIR;
	name[0] = '/';
	name[1] = '\0';
	for (;;) {
		if (*path == '/')
			path++;
		if (!*path)
			break;
		for (s = path; *s && *s != '/'; s++);
		if ((n = s - path) > MAXNAMLEN)
			return 0;
		ls = *path == '?' && n == 1 && !*s;
		memcpy(name, path, n);
		name[n] = 0;
		if (dt != DT_DIR) {
			printf("%s: not a directory.\n", name);
			return (0);
		}
		if ((dt = fsfind(name, &ino)) <= 0)
			break;
		path = s;
	}
	return dt == DT_REG ? ino : 0;
}

/*
 * Possible superblock locations ordered from most to least likely.
 */
static int sblock_try[] = SBLOCKSEARCH;

#if defined(UFS2_ONLY)
#define DIP(field) dp2.field
#elif defined(UFS1_ONLY)
#define DIP(field) dp1.field
#else
#define DIP(field) fs->fs_magic == FS_UFS1_MAGIC ? dp1.field : dp2.field
#endif

static ufs_ino_t inomap;
static ufs2_daddr_t blkmap, indmap;

static int
boot2_ufs_init(void)
{
	struct fs *fs;
	size_t n;

	inomap = 0;
	fs = (struct fs *)fsdmadat->sbbuf;

	for (n = 0; sblock_try[n] != -1; n++) {
		if (dskread(fs, sblock_try[n] / DEV_BSIZE,
			    SBLOCKSIZE / DEV_BSIZE)) {
			return -1;
		}
		if ((
#if defined(UFS1_ONLY)
		     fs->fs_magic == FS_UFS1_MAGIC
#elif defined(UFS2_ONLY)
		    (fs->fs_magic == FS_UFS2_MAGIC &&
		    fs->fs_sblockloc == sblock_try[n])
#else
		     fs->fs_magic == FS_UFS1_MAGIC ||
		    (fs->fs_magic == FS_UFS2_MAGIC &&
		    fs->fs_sblockloc == sblock_try[n])
#endif
		    ) &&
		    fs->fs_bsize <= MAXBSIZE &&
		    fs->fs_bsize >= sizeof(struct fs))
			break;
	}
	if (sblock_try[n] == -1)
		return -1;
	return 0;
}

static ssize_t
boot2_ufs_read(boot2_ino_t boot2_inode, void *buf, size_t nbyte)
{
#ifndef UFS2_ONLY
	static struct ufs1_dinode dp1;
#endif
#ifndef UFS1_ONLY
	static struct ufs2_dinode dp2;
#endif
	ufs_ino_t ufs_inode = (ufs_ino_t)boot2_inode;
	char *blkbuf;
	void *indbuf;
	struct fs *fs;
	char *s;
	size_t n, nb, size, off, vboff;
	ufs_lbn_t lbn;
	ufs2_daddr_t addr, vbaddr;
	u_int u;

	blkbuf = fsdmadat->blkbuf;
	indbuf = fsdmadat->indbuf;
	fs = (struct fs *)fsdmadat->sbbuf;

	if (!ufs_inode)
		return 0;
	if (inomap != ufs_inode) {
		n = IPERVBLK(fs);
		if (dskread(blkbuf, INO_TO_VBA(fs, n, ufs_inode), DBPERVBLK))
			return -1;
		n = INO_TO_VBO(n, ufs_inode);
#if defined(UFS1_ONLY)
		dp1 = ((struct ufs1_dinode *)blkbuf)[n];
#elif defined(UFS2_ONLY)
		dp2 = ((struct ufs2_dinode *)blkbuf)[n];
#else
		if (fs->fs_magic == FS_UFS1_MAGIC)
			dp1 = ((struct ufs1_dinode *)blkbuf)[n];
		else
			dp2 = ((struct ufs2_dinode *)blkbuf)[n];
#endif
		inomap = ufs_inode;
		fs_off = 0;
		blkmap = indmap = 0;
	}
	s = buf;
	size = DIP(di_size);
	n = size - fs_off;
	if (nbyte > n)
		nbyte = n;
	nb = nbyte;
	while (nb) {
		lbn = lblkno(fs, fs_off);
		off = blkoff(fs, fs_off);
		if (lbn < NDADDR) {
			addr = DIP(di_db[lbn]);
		} else if (lbn < NDADDR + NINDIR(fs)) {
			n = INDIRPERVBLK(fs);
			addr = DIP(di_ib[0]);
			u = (u_int)(lbn - NDADDR) / n * DBPERVBLK;
			vbaddr = fsbtodb(fs, addr) + u;
			if (indmap != vbaddr) {
				if (dskread(indbuf, vbaddr, DBPERVBLK))
					return -1;
				indmap = vbaddr;
			}
			n = (lbn - NDADDR) & (n - 1);
#if defined(UFS1_ONLY)
			addr = ((ufs1_daddr_t *)indbuf)[n];
#elif defined(UFS2_ONLY)
			addr = ((ufs2_daddr_t *)indbuf)[n];
#else
			if (fs->fs_magic == FS_UFS1_MAGIC)
				addr = ((ufs1_daddr_t *)indbuf)[n];
			else
				addr = ((ufs2_daddr_t *)indbuf)[n];
#endif
		} else {
			return -1;
		}
		vbaddr = fsbtodb(fs, addr) + (off >> VBLKSHIFT) * DBPERVBLK;
		vboff = off & VBLKMASK;
		n = sblksize(fs, size, lbn) - (off & ~VBLKMASK);
		if (n > VBLKSIZE)
			n = VBLKSIZE;
		if (blkmap != vbaddr) {
			if (dskread(blkbuf, vbaddr, n >> DEV_BSHIFT))
				return -1;
			blkmap = vbaddr;
		}
		n -= vboff;
		if (n > nb)
			n = nb;
		memcpy(s, blkbuf + vboff, n);
		s += n;
		fs_off += n;
		nb -= n;
	}
	return nbyte;
}
