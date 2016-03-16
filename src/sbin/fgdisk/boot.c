/*-
 * Copyright (c) 2007 Yahoo!, Inc.
 * All rights reserved.
 * Written by: John Baldwin <jhb@FreeBSD.org>
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the author nor the names of any co-contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
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
 */

#include <sys/types.h>
#include <sys/stat.h>
#include <sys/diskmbr.h>

#include <assert.h>
#include <err.h>
#include <errno.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>

#include "map.h"
#include "gpt.h"
#include "gpt_private.h"

static const uuid_t boot_uuid = GPT_ENT_TYPE_FREEBSD_BOOT;
static const char *pmbr_path = "/boot/pmbr";
static const char *gptboot_path = "/boot/gptboot";
static u_long boot_size;
static int pmbr_boot;

static void
usage_installboot(void)
{
	fprintf(stderr,
	    "usage: %s [-H] [-b pmbr] [-g gptboot] [-s sectors] device ...\n",
	    getprogname());
	exit(1);
}

static int
gpt_lookup(gd_t gd, const uuid_t *type, map_t *mapp, unsigned int *index)
{
	uuid_t uuid;
	map_t map;
	struct gpt_hdr *hdr;
	struct gpt_ent *ent;
	unsigned int i;

	if ((hdr = gpt_gethdr(gd)) == NULL)
		return (ENXIO);

	for (i = 0; i < le32toh(hdr->hdr_entries); i++) {
		ent = (void *)((char *)gd->tbl->map_data + i *
		    le32toh(hdr->hdr_entsz));
		uuid_dec_le(&ent->ent_type, &uuid);
		if (uuid_equal(&uuid, type, NULL))
			break;
	}
	if (i == le32toh(hdr->hdr_entries)) {
		*mapp = NULL;
		return (0);
	}

	/* Update entry. */
	*index = i + 1;

	/* Lookup the map corresponding to this partition. */
	for (map = map_find(gd, MAP_TYPE_GPT_PART); map != NULL;
	     map = map->map_next) {
		if (map->map_type != MAP_TYPE_GPT_PART)
			continue;
		if (map->map_start == (off_t)le64toh(ent->ent_lba_start)) {
			assert(map->map_start + map->map_size - 1LL ==
			    (off_t)le64toh(ent->ent_lba_end));
			*mapp = map;
			return (0);
		}
	}

	/* Hmm, the map list is not in sync with the GPT table. */
	errx(1, "internal map list is corrupted");
}

static void
installboot(gd_t gd)
{
	struct stat sb;
	off_t bsize, ofs;
	map_t pmbr, gptboot;
	struct mbr *mbr;
	char *buf;
	ssize_t nbytes;
	unsigned int entry;
	int bfd;

	entry = 0;	/* To keep track of boot partition. */

	/* First step: verify boot partition size. */
	if (boot_size == 0) {
		/* Default to 64k. */
		bsize = 65536 / gd->secsz;
	} else {
		if (boot_size * gd->secsz < 16384) {
			warnx("invalid boot partition size %lu", boot_size);
			return;
		}
		bsize = boot_size;
	}

	/* Second step: write the PMBR boot loader into the PMBR. */
	pmbr = map_find(gd, MAP_TYPE_PMBR);
	if (pmbr == NULL) {
		warnx("%s: error: PMBR not found", gd->device_name);
		return;
	}
	bfd = open(pmbr_path, O_RDONLY);
	if (bfd < 0 || fstat(bfd, &sb) < 0) {
		warn("unable to open PMBR boot loader");
		return;
	}
	if (sb.st_size != gd->secsz) {
		warnx("invalid PMBR boot loader");
		return;
	}
	mbr = pmbr->map_data;
	nbytes = read(bfd, mbr->mbr_code, sizeof(mbr->mbr_code));
	if (nbytes < 0) {
		warn("unable to read PMBR boot loader");
		return;
	}
	if (nbytes != sizeof(mbr->mbr_code)) {
		warnx("short read of PMBR boot loader");
		return;
	}
	close(bfd);

	if (pmbr_boot && (mbr->mbr_part[0].part_typ == DOSPTYP_PMBR)) {
		/* Sprinkle some magic dust by toggling 0xee bootflags . */
		mbr->mbr_part[0].part_flag ^= 0x80;
		printf("%s: toggled bootable flag in 0xEE part_flags\n",
		    gd->device_name);
	}

	gpt_write(gd, pmbr);

	/* Third step: open gptboot and obtain its size. */
	bfd = open(gptboot_path, O_RDONLY);
	if (bfd < 0 || fstat(bfd, &sb) < 0) {
		warn("unable to open GPT boot loader");
		return;
	}

	/* Fourth step: find an existing boot partition or create one. */
	if (gpt_lookup(gd, &boot_uuid, &gptboot, &entry) != 0)
		return;
	if (gptboot != NULL) {
		if (gptboot->map_size * gd->secsz < sb.st_size) {
			warnx("%s: error: boot partition is too small",
			    gd->device_name);
			return;
		}
	} else if (bsize * gd->secsz < sb.st_size) {
		warnx(
		    "%s: error: proposed size for boot partition is too small",
		    gd->device_name);
		return;
	} else {
		/* Requirements for gptboot are low, so care about size only */
		gptboot = gpt_add_part(gd, boot_uuid, 0, 0, "GPTBOOT",
		    bsize, &entry);
		if (gptboot == NULL)
			return;
		gpt_status(gd, entry, "added");
	}

	/*
	 * Fifth step, write out the gptboot binary to the boot partition.
	 * When writing to a disk device, the write must be sector aligned
	 * and not write to any partial sectors, so round up the buffer size
	 * to the next sector and zero it.
	 */
	bsize = ((sb.st_size + gd->secsz - 1) / gd->secsz) * gd->secsz;
	buf = calloc(1, bsize);
	nbytes = read(bfd, buf, sb.st_size);
	if (nbytes < 0) {
		warn("unable to read GPT boot loader");
		return;
	}
	if (nbytes != sb.st_size) {
		warnx("short read of GPT boot loader");
		return;
	}
	close(bfd);
	ofs = gptboot->map_start * gd->secsz;
	if (lseek(gd->fd, ofs, SEEK_SET) != ofs) {
		warn("%s: error: unable to seek to boot partition",
		    gd->device_name);
		return;
	}
	nbytes = write(gd->fd, buf, bsize);
	gd->flags |= GPT_MODIFIED;
	if (nbytes < 0) {
		warn("unable to write GPT boot loader");
		return;
	}
	if (nbytes != bsize) {
		warnx("short write of GPT boot loader");
		return;
	}
	free(buf);

	gpt_status(gd, entry, "and PMBR updated");
}

int
cmd_installboot(int argc, char *argv[])
{
	char *p;
	int ch;
	int flags = 0;
	gd_t gd;

	while ((ch = getopt(argc, argv, "Hb:g:s:")) != -1) {
		switch (ch) {
		case 'H':
			pmbr_boot = 1;
			break;
		case 'b':
			pmbr_path = optarg;
			break;
		case 'g':
			gptboot_path = optarg;
			break;
		case 's':
			if (boot_size > 0)
				usage_installboot();
			boot_size = strtol(optarg, &p, 10);
			if (*p != '\0' || boot_size < 1)
				usage_installboot();
			break;
		default:
			usage_installboot();
		}
	}

	if (argc == optind)
		usage_installboot();

	while (optind < argc) {
		gd = gpt_open(argv[optind++], flags);
		if (gd == NULL) {
			continue;
		}

		installboot(gd);

		gpt_close(gd);
	}

	return (0);
}
