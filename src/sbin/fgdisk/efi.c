/*
 * Copyright (c) 2015-2016 The DragonFly Project.  All rights reserved.
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

static const uuid_t efi_uuid = GPT_ENT_TYPE_EFI;
static const char *fat_path = "/boot/boot1.efifat";
static u_long efi_size;

static void
usage_installefi(void)
{
	fprintf(stderr,
	    "usage: %s [-b efifat] [-s sectors] device ...\n",
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
installefi(gd_t gd)
{
	struct stat sb;
	off_t bsize, ofs;
	map_t efiboot;
	char *buf;
	ssize_t nbytes;
	unsigned int entry;
	int bfd;

	entry = 0;	/* To keep track of boot partition. */

	if (efi_size == 0) {
		/* Default to 800k. */
		bsize = 819200 / gd->secsz;
	} else {
		if (efi_size * gd->secsz < 16384) {
			warnx("invalid efi partition size %lu", efi_size);
			return;
		}
		bsize = efi_size;
	}

	/* Open efi.fat image and obtain its size. */
	bfd = open(fat_path, O_RDONLY);
	if (bfd < 0 || fstat(bfd, &sb) < 0) {
		warn("unable to open efi.fat image");
		return;
	}

	/* Find an existing efi partition or create one. */
	if (gpt_lookup(gd, &efi_uuid, &efiboot, &entry) != 0)
		return;
	if (efiboot != NULL) {
		if (efiboot->map_size * gd->secsz < sb.st_size) {
			warnx("%s: error: boot partition is too small",
			    gd->device_name);
			return;
		}
	} else if (bsize * gd->secsz < sb.st_size) {
		warnx(
		    "%s: error: proposed size for efi partition is too small",
		    gd->device_name);
		return;
	} else {
		/* Requirements for efi.fat are low, so care about size only */
		efiboot = gpt_add_part(gd, efi_uuid, 0, 0, "EFIBOOT",
		    bsize, &entry);
		if (efiboot == NULL)
			return;
		gpt_status(gd, entry, "added");
	}

	/* Write efi.fat image */
	bsize = ((sb.st_size + gd->secsz - 1) / gd->secsz) * gd->secsz;
	buf = calloc(1, bsize);
	nbytes = read(bfd, buf, sb.st_size);
	if (nbytes < 0) {
		warn("unable to read efi.fat image");
		return;
	}
	if (nbytes != sb.st_size) {
		warnx("short read of efi.fat image");
		return;
	}
	close(bfd);
	ofs = efiboot->map_start * gd->secsz;
	if (lseek(gd->fd, ofs, SEEK_SET) != ofs) {
		warn("%s: error: unable to seek to efi partition",
		    gd->device_name);
		return;
	}
	nbytes = write(gd->fd, buf, bsize);
	gd->flags |= GPT_MODIFIED;
	if (nbytes < 0) {
		warn("unable to write efi.fat image");
		return;
	}
	if (nbytes != bsize) {
		warnx("short write of efi.fat image");
		return;
	}
	free(buf);

	gpt_status(gd, entry, "updated");
}

int
cmd_installefi(int argc, char *argv[])
{
	char *p;
	int ch;
	int flags = 0;
	gd_t gd;

	while ((ch = getopt(argc, argv, "b:s:")) != -1) {
		switch (ch) {
		case 'b':
			fat_path = optarg;
			break;
		case 's':
			if (efi_size > 0)
				usage_installefi();
			efi_size = strtol(optarg, &p, 10);
			if (*p != '\0' || efi_size < 1)
				usage_installefi();
			break;
		default:
			usage_installefi();
		}
	}

	if (argc == optind)
		usage_installefi();

	while (optind < argc) {
		gd = gpt_open(argv[optind++], flags);
		if (gd == NULL) {
			continue;
		}

		installefi(gd);

		gpt_close(gd);
	}

	return (0);
}
