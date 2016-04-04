/*-
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
 * $FreeBSD: head/sys/boot/i386/gptboot/gptboot.c 294925 2016-01-27 16:36:18Z imp $
 */

#define AOUT_H_FORCE32
#include <sys/param.h>
#include <sys/gpt.h>
#include <sys/dirent.h>
#include <sys/reboot.h>

#include <machine/bootinfo.h>
#include <machine/elf.h>
#include <machine/psl.h>

#include <stdarg.h>

#include <a.out.h>

#include <btxv86.h>

#include "boot2.h"
#include "lib.h"
#include "rbx.h"
#include "drv.h"
#include "util.h"
#include "cons.h"
#include "gpt.h"
#include "../bootasm.h"

#define PATH_CONFIG	"/boot.config"
#define PATH_BOOT3	"/boot/loader"		/* /boot in root */
#define PATH_BOOT3_ALT	"/loader"		/* /boot is dedicated */
#define PATH_KERNEL	"/boot/kernel/kernel"

#define NOPT		14
#define NDEV		3
#define MEM_BASE	0x12
#define MEM_EXT		0x15

#define DRV_HARD	0x80
#define DRV_MASK	0x7f

#define TYPE_AD		0
#define TYPE_DA		1
#define TYPE_MAXHARD	TYPE_DA
#define TYPE_FD		2

#define INVALID_S	"Bad %s\n"

extern uint32_t _end;

static const uuid_t freebsd_ufs_uuid = GPT_ENT_TYPE_FREEBSD_UFS;
static const char optstr[NOPT] = { "VhaCcdgmnPprsv" };
static const unsigned char flags[NOPT] = {
	RBX_VIDEO,
	RBX_SERIAL,
	RBX_ASKNAME,
	RBX_CDROM,
	RBX_CONFIG,
	RBX_KDB,
	RBX_GDB,
	RBX_MUTE,
	RBX_NOINTR,
	RBX_PROBEKBD,
	RBX_PAUSE,
	RBX_DFLTROOT,
	RBX_SINGLE,
	RBX_VERBOSE
};
uint32_t opts;

static const char *const dev_nm[NDEV] = {"ad", "da", "fd"};
static const unsigned char dev_maj[NDEV] = {30, 4, 2};

static struct dsk dsk;
static char kname[1024];
/* XXX static int comspeed = SIOSPD; */
static struct bootinfo bootinfo;

/*
 * boot2 encapsulated ABI elements provided to *fsread.c
 *
 * NOTE: boot2_dmadat is extended by per-filesystem APIs
 */
uint32_t fs_off;
int	no_io_error;
int	ls;
static u_int8_t dsk_meta;
struct boot2_dmadat *boot2_dmadat;

void exit(int);
static void load(void);
static int parse(char *, int *);
static int dskprobe(void);
static uint32_t memsize(void);

#include "ufsread.c"

#if defined(UFS) && defined(HAMMER2FS)

const struct boot2_fsapi *fsapi;

#elif defined(UFS)

#define fsapi	(&boot2_ufs_api)

#elif defined(HAMMER2FS)

#define fsapi	(&boot2_hammer2_api)

#endif

static inline int
xfsread(boot2_ino_t inode, void *buf, size_t nbyte)
{
	if ((size_t)fsapi->fsread(inode, buf, nbyte) != nbyte) {
		printf("Invalid %s\n", "format");
		return (-1);
	}
	return (0);
}

static inline uint32_t
memsize(void)
{
	v86.addr = MEM_EXT;
	v86.eax = 0x8800;
	v86int();
	return (v86.eax);
}

static int
gptinit(void)
{
	if (gptread(&freebsd_ufs_uuid, &dsk, boot2_dmadat->secbuf) == -1) {
		printf("%s: unable to load GPT\n", BOOTPROG);
		return (-1);
	}
	if (gptfind(&freebsd_ufs_uuid, &dsk, dsk.part) == -1) {
		printf("%s: no UFS partition was found\n", BOOTPROG);
		return (-1);
	}
	dsk_meta = 0;
	return (0);
}

int
main(void)
{
	char cmd[512], cmdtmp[512];
	ssize_t sz;
	uint8_t autoboot;
	int dskupdated;
	boot2_ino_t ino;

	boot2_dmadat =
		(void *)(roundup2(__base + (int32_t)&_end, 0x10000) - __base);
	v86.ctl = V86_FLAGS;
	v86.efl = PSL_RESERVED_DEFAULT | PSL_I;
	dsk.drive = *(uint8_t *)PTOV(MEM_ARG);
	dsk.type = dsk.drive & DRV_HARD ? TYPE_AD : TYPE_FD;
	dsk.unit = dsk.drive & DRV_MASK;
	dsk.part = -1;
	dsk.start = 0;
	bootinfo.bi_version = BOOTINFO_VERSION;
	bootinfo.bi_size = sizeof(bootinfo);
	bootinfo.bi_basemem = 0;	/* XXX will be filled by loader or kernel */
	bootinfo.bi_extmem = memsize();
	bootinfo.bi_memsizes_valid++;

	/* Process configuration file */
	/* force VIDEO output */
	opts |= OPT_SET(RBX_VIDEO);

	if (gptinit() != 0)
		return (-1);

	autoboot = 1;
	*cmd = '\0';

/* XXX printf() works only from here */
printf("zrj drive=%u, type=%u, unit=%u\n", dsk.drive,dsk.type,dsk.unit);
	for (;;) {
		*kname = '\0';

		/*
		 * Probe the default disk and process the configuration file if
		 * successful.
		 */
		if (dskprobe() == 0) {
			if ((ino = fsapi->fslookup(PATH_CONFIG)))
				sz = fsapi->fsread(ino, cmd, sizeof(cmd) - 1);
			cmd[(sz < 0) ? 0 : sz] = '\0';
		}

		/*
		 * Parse config file if present.  parse() will re-probe if necessary.
		 */
		if (*cmd != '\0') {
			memcpy(cmdtmp, cmd, sizeof(cmdtmp));
			if (parse(cmdtmp, &dskupdated))
				break;
			if (dskupdated && gptinit() != 0)
				break;
			printf("%s: %s", PATH_CONFIG, cmd);
		/* Do not process this command twice */
			*cmd = '\0';
	/*
	 * Setup our (serial) console after processing the config file.  If
	 * the initialization fails, don't try to use the serial port.  This
	 * can happen if the serial port is unmaped (happens on new laptops a lot).
	 */
	if ((opts & (OPT_SET(RBX_MUTE)|OPT_SET(RBX_SERIAL)|OPT_SET(RBX_VIDEO))) == 0)
		opts |= (OPT_SET(RBX_SERIAL)|OPT_SET(RBX_VIDEO));
	if (OPT_CHECK(RBX_SERIAL)) {
		if (sio_init())
			opts = OPT_SET(RBX_VIDEO);
	}
		}

		if (autoboot && keyhit(3)) {
			if (*kname == '\0')
				memcpy(kname, PATH_BOOT3, sizeof(PATH_BOOT3));
			break;
		}
		autoboot = 0;

		/*
		 * Try to exec stage 3 boot loader. If interrupted by a
		 * keypress, or in case of failure, try to load a kernel
		 * directly instead.
		 *
		 * We have to try boot /boot/loader and /loader to support booting
		 * from a /boot partition instead of a root partition.
		 */
		if (*kname != '\0')
			load();
		memcpy(kname, PATH_BOOT3, sizeof(PATH_BOOT3));
		load();
		memcpy(kname, PATH_BOOT3_ALT, sizeof(PATH_BOOT3_ALT));
		load();
		memcpy(kname, PATH_KERNEL, sizeof(PATH_KERNEL));
		load();
		gptbootfailed(&dsk);
		if (gptfind(&freebsd_ufs_uuid, &dsk, -1) == -1)
			break;
		dsk_meta = 0;
	}

	/* Present the user with the boot2 prompt. */

	for (;;) {
		printf("\nDragonFly/x86 boot\n"
		    "Default: %u:%s(%up%u)%s\n"
		    "boot: ",
		    dsk.drive & DRV_MASK, dev_nm[dsk.type], dsk.unit,
		    dsk.part, kname);
		if (OPT_CHECK(RBX_SERIAL))
			sio_flush();
		*cmd = '\0';
		if (keyhit(0))
			getstr(cmd, sizeof(cmd));
		else
			putchar('\n');
		if (parse(cmd, &dskupdated)) {
			putchar('\a');
			continue;
		}
		if (dskupdated && gptinit() != 0)
			continue;
		load();
	}
	/* NOTREACHED */
}

/* XXX - Needed for btxld to link the boot2 binary; do not remove. */
void
exit(int x)
{
}

static void
load(void)
{
    union {
	struct exec ex;
	Elf32_Ehdr eh;
    } hdr;
    static Elf32_Phdr ep[2];
    static Elf32_Shdr es[2];
    caddr_t p;
    boot2_ino_t ino;
    uint32_t addr;
    int i, j;

    if (!(ino = fsapi->fslookup(kname))) {
	if (!ls) {
	    printf("%s: No %s on %u:%s(%up%u)\n", BOOTPROG,
		kname, dsk.drive & DRV_MASK, dev_nm[dsk.type], dsk.unit,
		dsk.part);
	}
	return;
    }
    if (xfsread(ino, &hdr, sizeof(hdr)))
	return;
    if (N_GETMAGIC(hdr.ex) == ZMAGIC) {
	addr = hdr.ex.a_entry & 0xffffff;
	p = PTOV(addr);
	fs_off = PAGE_SIZE;
	if (xfsread(ino, p, hdr.ex.a_text))
	    return;
	p += roundup2(hdr.ex.a_text, PAGE_SIZE);
	if (xfsread(ino, p, hdr.ex.a_data))
	    return;
    } else if (IS_ELF(hdr.eh)) {
	fs_off = hdr.eh.e_phoff;
	for (j = i = 0; i < hdr.eh.e_phnum && j < 2; i++) {
	    if (xfsread(ino, ep + j, sizeof(ep[0])))
		return;
	    if (ep[j].p_type == PT_LOAD)
		j++;
	}
	for (i = 0; i < 2; i++) {
	    p = PTOV(ep[i].p_paddr & 0xffffff);
	    fs_off = ep[i].p_offset;
	    if (xfsread(ino, p, ep[i].p_filesz))
		return;
	}
	p += roundup2(ep[1].p_memsz, PAGE_SIZE);
	bootinfo.bi_symtab = VTOP(p);
	if (hdr.eh.e_shnum == hdr.eh.e_shstrndx + 3) {
	    fs_off = hdr.eh.e_shoff + sizeof(es[0]) *
		(hdr.eh.e_shstrndx + 1);
	    if (xfsread(ino, &es, sizeof(es)))
		return;
	    for (i = 0; i < 2; i++) {
		*(Elf32_Word *)p = es[i].sh_size;
		p += sizeof(es[i].sh_size);
		fs_off = es[i].sh_offset;
		if (xfsread(ino, p, es[i].sh_size))
		    return;
		p += es[i].sh_size;
	    }
	}
	addr = hdr.eh.e_entry & 0xffffff;
    } else {
	printf(INVALID_S, "format");
	return;
    }
    bootinfo.bi_esymtab = VTOP(p);
    bootinfo.bi_kernelname = VTOP(kname);
    bootinfo.bi_bios_dev = dsk.drive;
    __exec((caddr_t)addr, RB_BOOTINFO | (opts & RBX_MASK),
	   MAKEBOOTDEV(dev_maj[dsk.type], dsk.part + 1, dsk.unit, 0xff),
	   0, 0, 0, VTOP(&bootinfo));
}

static int
parse(char *cmdstr, int *dskupdated)
{
    char *arg = cmdstr;
    char *ep, *p, *q;
    unsigned int drv;
    int c, i;

    *dskupdated = 0;
    while ((c = *arg++)) {
	if (c == ' ' || c == '\t' || c == '\n')
	    continue;
	for (p = arg; *p && *p != '\n' && *p != ' ' && *p != '\t'; p++);
	ep = p;
	if (*p)
	    *p++ = 0;
	if (c == '-') {
	    while ((c = *arg++)) {
		for (i = NOPT - 1; i >= 0; --i) {
		    if (optstr[i] == c) {
			opts ^= 1 << flags[i];
			goto ok;
		    }
		}
		return(-1);
		ok: ;	/* ugly but save space */
	    }
	    if (OPT_CHECK(RBX_PROBEKBD)) {
		i = *(uint8_t *)PTOV(0x496) & 0x10;
		if (!i) {
		    printf("NO KB\n");
		    opts |= OPT_SET(RBX_VIDEO) | OPT_SET(RBX_SERIAL);
		}
		opts &= ~OPT_SET(RBX_PROBEKBD);
	    }
	} else {
	    for (q = arg--; *q && *q != '('; q++);
	    if (*q) {
		drv = -1;
		if (arg[1] == ':') {
		    drv = *arg - '0';
		    if (drv > 9)
			return (-1);
		    arg += 2;
		}
		if (q - arg != 2)
		    return -1;
		for (i = 0; arg[0] != dev_nm[i][0] ||
			    arg[1] != dev_nm[i][1]; i++)
		    if (i == NDEV - 1)
			return -1;
		dsk.type = i;
		arg += 3;
		dsk.unit = *arg - '0';
		if (arg[1] != 'p' || dsk.unit > 9)
		    return -1;
		arg += 2;
		dsk.part = *arg - '0';
		if (dsk.part < 1 || dsk.part > 9)
		    return -1;
		arg++;
		if (arg[0] != ')')
		    return -1;
		arg++;
		if (drv == -1)
		    drv = dsk.unit;
		dsk.drive = (dsk.type <= TYPE_MAXHARD
			     ? DRV_HARD : 0) + drv;
		*dskupdated = 1;
	    }
	    if ((i = ep - arg)) {
		if ((size_t)i >= sizeof(kname))
		    return -1;
		memcpy(kname, arg, i + 1);
	    }
	}
	arg = p;
    }
    return dskprobe();
}

static int
dskprobe(void)
{
    /*
     * Probe filesystem
     */
#if defined(UFS) && defined(HAMMER2FS)
    if (boot2_ufs_api.fsinit() == 0) {
	fsapi = &boot2_ufs_api;
    } else if (boot2_hammer2_api.fsinit() == 0) {
	fsapi = &boot2_hammer2_api;
    } else {
	printf("fs probe failed\n");
	fsapi = &boot2_ufs_api;
	return -1;
    }
    return 0;
#else
    return fsapi->fsinit();
#endif
}

/*
 * Read from the probed disk.  We have established the slice and partition
 * base sector.
 */
int
dskread(void *buf, daddr_t lba, unsigned nblk)
{
	return drvread(&dsk, buf, lba + dsk.start, nblk);
}
