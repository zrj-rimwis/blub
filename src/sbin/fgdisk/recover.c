/*-
 * Copyright (c) 2002 Marcel Moolenaar
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
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
 */

#include <sys/types.h>
#include <sys/diskmbr.h>

#include <err.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "map.h"
#include "gpt.h"

static int force;
static int rec_pmbr;
static int recoverable;

static void
usage_recover(void)
{

	fprintf(stderr,
	    "usage: %s [-fP] device ...\n", getprogname());
	exit(1);
}

static void
recover(int fd)
{
	off_t last;
	map_t *gpt, *tpg;
	map_t *tbl, *lbt;
	struct gpt_hdr *hdr;

	if (map_find(MAP_TYPE_MBR) != NULL) {
		if (!force) {
			warnx("%s: error: device contains a MBR", device_name);
			return;
		}
	}

	gpt = map_find(MAP_TYPE_PRI_GPT_HDR);
	tpg = map_find(MAP_TYPE_SEC_GPT_HDR);
	tbl = map_find(MAP_TYPE_PRI_GPT_TBL);
	lbt = map_find(MAP_TYPE_SEC_GPT_TBL);

	if (gpt == NULL && tpg == NULL) {
		warnx("%s: no primary or secondary GPT headers, can't recover",
		    device_name);
		return;
	}
	if (tbl == NULL && lbt == NULL) {
		warnx("%s: no primary or secondary GPT tables, can't recover",
		    device_name);
		return;
	}

	last = mediasz / secsz - 1LL;

	if (tbl != NULL && lbt == NULL) {
		lbt = map_add(last - tbl->map_size, tbl->map_size,
		    MAP_TYPE_SEC_GPT_TBL, tbl->map_data);
		if (lbt == NULL) {
			warnx("%s: adding secondary GPT table failed",
			    device_name);
			return;
		}
		gpt_write(fd, lbt);
		warnx("%s: recovered secondary GPT table from primary",
		    device_name);
	} else if (tbl == NULL && lbt != NULL) {
		tbl = map_add(2LL, lbt->map_size, MAP_TYPE_PRI_GPT_TBL,
		    lbt->map_data);
		if (tbl == NULL) {
			warnx("%s: adding primary GPT table failed",
			    device_name);
			return;
		}
		gpt_write(fd, tbl);
		warnx("%s: recovered primary GPT table from secondary",
		    device_name);
	}

	if (gpt != NULL && tpg == NULL) {
		tpg = map_add(last, 1LL, MAP_TYPE_SEC_GPT_HDR,
		    calloc(1, secsz));
		if (tpg == NULL) {
			warnx("%s: adding secondary GPT header failed",
			    device_name);
			return;
		}
		memcpy(tpg->map_data, gpt->map_data, secsz);
		hdr = tpg->map_data;
		hdr->hdr_lba_self = htole64(tpg->map_start);
		hdr->hdr_lba_alt = htole64(gpt->map_start);
		hdr->hdr_lba_table = htole64(lbt->map_start);
		hdr->hdr_crc_self = 0;
		hdr->hdr_crc_self = htole32(crc32(hdr, le32toh(hdr->hdr_size)));
		gpt_write(fd, tpg);
		warnx("%s: recovered secondary GPT header from primary",
		    device_name);
	} else if (gpt == NULL && tpg != NULL) {
		gpt = map_add(1LL, 1LL, MAP_TYPE_PRI_GPT_HDR,
		    calloc(1, secsz));
		if (gpt == NULL) {
			warnx("%s: adding primary GPT header failed",
			    device_name);
			return;
		}
		memcpy(gpt->map_data, tpg->map_data, secsz);
		hdr = gpt->map_data;
		hdr->hdr_lba_self = htole64(gpt->map_start);
		hdr->hdr_lba_alt = htole64(tpg->map_start);
		hdr->hdr_lba_table = htole64(tbl->map_start);
		hdr->hdr_crc_self = 0;
		hdr->hdr_crc_self = htole32(crc32(hdr, le32toh(hdr->hdr_size)));
		gpt_write(fd, gpt);
		warnx("%s: recovered primary GPT header from secondary",
		    device_name);
	}
}

static void
rewrite_pmbr(int fd)
{
	off_t last;
	map_t *gpt, *tpg;
	map_t *tbl, *lbt;
	map_t *map;
	struct mbr *mbr;

	/* First check if we already have a PMBR */
	map = map_find(MAP_TYPE_PMBR);
	if (map != NULL) {
		if (!force) {
			warnx("%s: error: device contains a PMBR", device_name);
			return;
		}
		printf("%s: warning: about to rewrite a PMBR\n", device_name);

		/* Nuke the PMBR in our internal map. */
		map->map_type = MAP_TYPE_UNUSED;
	}

	last = mediasz / secsz - 1LL;

	gpt = map_find(MAP_TYPE_PRI_GPT_HDR);
	tpg = map_find(MAP_TYPE_SEC_GPT_HDR);
	tbl = map_find(MAP_TYPE_PRI_GPT_TBL);
	lbt = map_find(MAP_TYPE_SEC_GPT_TBL);

	if ((gpt == NULL && tpg == NULL) || (tbl == NULL && lbt == NULL)) {
		warnx("%s: no GPT headers or tables, run recover first",
		    device_name);
		return;
	}

	map = map_find(MAP_TYPE_MBR);
	if (map != NULL) {
		if (!force) {
			warnx("%s: error: device contains a MBR", device_name);
			return;
		}
		printf("%s: warning: about to overwrite a MBR\n", device_name);

		/* Nuke the MBR in our internal map. */
		map->map_type = MAP_TYPE_UNUSED;
	}

	/*
	 * Create PMBR.
	 */
	if (map_find(MAP_TYPE_PMBR) == NULL) {
		if (map_free(0LL, 1LL) == 0) {
			warnx("%s: error: no room for the PMBR", device_name);
			return;
		}
		mbr = gpt_read(fd, 0LL, 1);
		bzero(mbr, sizeof(*mbr));
		mbr->mbr_sig = htole16(MBR_SIG);
		mbr->mbr_part[0].part_shd = 0x00;
		mbr->mbr_part[0].part_ssect = 0x02;
		mbr->mbr_part[0].part_scyl = 0x00;
		mbr->mbr_part[0].part_typ = DOSPTYP_PMBR;
		mbr->mbr_part[0].part_ehd = 0xfe;
		mbr->mbr_part[0].part_esect = 0xff;
		mbr->mbr_part[0].part_ecyl = 0xff;
		mbr->mbr_part[0].part_start_lo = htole16(1);
		if (last > 0xffffffff) {
			mbr->mbr_part[0].part_size_lo = htole16(0xffff);
			mbr->mbr_part[0].part_size_hi = htole16(0xffff);
		} else {
			mbr->mbr_part[0].part_size_lo = htole16(last);
			mbr->mbr_part[0].part_size_hi = htole16(last >> 16);
		}
		map = map_add(0LL, 1LL, MAP_TYPE_PMBR, mbr);
		gpt_write(fd, map);
		printf("%s: written a fresh PMBR\n", device_name);
	}
}

int
cmd_recover(int argc, char *argv[])
{
	int ch, fd;

	while ((ch = getopt(argc, argv, "frP")) != -1) {
		switch(ch) {
		case 'f':
			force = 1;
			break;
		case 'r':
			recoverable = 1;
			break;
		case 'P':
			rec_pmbr = 1;
			break;
		default:
			usage_recover();
		}
	}

	if (argc == optind)
		usage_recover();

	while (optind < argc) {
		fd = gpt_open(argv[optind++]);
		if (fd == -1) {
			warn("unable to open device '%s'", device_name);
			continue;
		}

		if (rec_pmbr)
			rewrite_pmbr(fd);
		else
			recover(fd);

		gpt_close(fd);
	}

	return (0);
}
