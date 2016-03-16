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
#include "gpt_private.h"

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
recover(gd_t gd)
{
	off_t last;
	struct gpt_hdr *hdr;

	if (map_find(gd, MAP_TYPE_MBR) != NULL) {
		if (!force) {
			warnx("%s: error: device contains a MBR", gd->device_name);
			return;
		}
	}

	gd->gpt = map_find(gd, MAP_TYPE_PRI_GPT_HDR);
	gd->tpg = map_find(gd, MAP_TYPE_SEC_GPT_HDR);
	gd->tbl = map_find(gd, MAP_TYPE_PRI_GPT_TBL);
	gd->lbt = map_find(gd, MAP_TYPE_SEC_GPT_TBL);

	if (gd->gpt == NULL && gd->tpg == NULL) {
		warnx("%s: no primary or secondary GPT headers, can't recover",
		    gd->device_name);
		return;
	}
	if (gd->tbl == NULL && gd->lbt == NULL) {
		warnx("%s: no primary or secondary GPT tables, can't recover",
		    gd->device_name);
		return;
	}

	last = gd->mediasz / gd->secsz - 1LL;

	if (gd->tbl != NULL && gd->lbt == NULL) {
		gd->lbt = map_add(gd, last - gd->tbl->map_size,
		    gd->tbl->map_size, MAP_TYPE_SEC_GPT_TBL,
		    gd->tbl->map_data, 1);
		if (gd->lbt == NULL) {
			warnx("%s: adding secondary GPT table failed",
			    gd->device_name);
			return;
		}
		gpt_write(gd, gd->lbt);
		warnx("%s: recovered secondary GPT table from primary",
		    gd->device_name);
	} else if (gd->tbl == NULL && gd->lbt != NULL) {
		gd->tbl = map_add(gd, 2LL, gd->lbt->map_size,
		    MAP_TYPE_PRI_GPT_TBL, gd->lbt->map_data, 0);
		if (gd->tbl == NULL) {
			warnx("%s: adding primary GPT table failed",
			    gd->device_name);
			return;
		}
		gpt_write(gd, gd->tbl);
		warnx("%s: recovered primary GPT table from secondary",
		    gd->device_name);
	}

	if (gd->gpt != NULL && gd->tpg == NULL) {
		gd->tpg = map_add(gd, last, 1LL, MAP_TYPE_SEC_GPT_HDR,
		    calloc(1, gd->secsz), 1);
		if (gd->tpg == NULL) {
			warnx("%s: adding secondary GPT header failed",
			    gd->device_name);
			return;
		}
		memcpy(gd->tpg->map_data, gd->gpt->map_data, gd->secsz);
		hdr = gd->tpg->map_data;
		hdr->hdr_lba_self = htole64(gd->tpg->map_start);
		hdr->hdr_lba_alt = htole64(gd->gpt->map_start);
		hdr->hdr_lba_table = htole64(gd->lbt->map_start);
		hdr->hdr_crc_self = 0;
		hdr->hdr_crc_self = htole32(crc32(hdr, le32toh(hdr->hdr_size)));
		gpt_write(gd, gd->tpg);
		warnx("%s: recovered secondary GPT header from primary",
		    gd->device_name);
	} else if (gd->gpt == NULL && gd->tpg != NULL) {
		gd->gpt = map_add(gd, 1LL, 1LL, MAP_TYPE_PRI_GPT_HDR,
		    calloc(1, gd->secsz), 1);
		if (gd->gpt == NULL) {
			warnx("%s: adding primary GPT header failed",
			    gd->device_name);
			return;
		}
		memcpy(gd->gpt->map_data, gd->tpg->map_data, gd->secsz);
		hdr = gd->gpt->map_data;
		hdr->hdr_lba_self = htole64(gd->gpt->map_start);
		hdr->hdr_lba_alt = htole64(gd->tpg->map_start);
		hdr->hdr_lba_table = htole64(gd->tbl->map_start);
		hdr->hdr_crc_self = 0;
		hdr->hdr_crc_self = htole32(crc32(hdr, le32toh(hdr->hdr_size)));
		gpt_write(gd, gd->gpt);
		warnx("%s: recovered primary GPT header from secondary",
		    gd->device_name);
	}
}

static void
rewrite_pmbr(gd_t gd)
{
	off_t last;
	map_t map;
	struct mbr *mbr;

	/* First check if we already have a PMBR */
	map = map_find(gd, MAP_TYPE_PMBR);
	if (map != NULL) {
		if (!force) {
			warnx("%s: error: device contains a PMBR", gd->device_name);
			return;
		}
		printf("%s: warning: about to rewrite a PMBR\n", gd->device_name);

		/* Nuke the PMBR in our internal map. */
		map->map_type = MAP_TYPE_UNUSED;
	}

	last = gd->mediasz / gd->secsz - 1LL;

	gd->gpt = map_find(gd, MAP_TYPE_PRI_GPT_HDR);
	gd->tpg = map_find(gd, MAP_TYPE_SEC_GPT_HDR);
	gd->tbl = map_find(gd, MAP_TYPE_PRI_GPT_TBL);
	gd->lbt = map_find(gd, MAP_TYPE_SEC_GPT_TBL);

	if ((gd->gpt == NULL && gd->tpg == NULL) ||
	    (gd->tbl == NULL && gd->lbt == NULL)) {
		warnx("%s: no GPT headers or tables, run recover first",
		    gd->device_name);
		return;
	}

	map = map_find(gd, MAP_TYPE_MBR);
	if (map != NULL) {
		if (!force) {
			warnx("%s: error: device contains a MBR", gd->device_name);
			return;
		}
		printf("%s: warning: about to overwrite a MBR\n", gd->device_name);

		/* Nuke the MBR in our internal map. */
		map->map_type = MAP_TYPE_UNUSED;
	}

	/*
	 * Create PMBR.
	 */
	if (map_find(gd, MAP_TYPE_PMBR) == NULL) {
		if (map_free(gd, 0LL, 1LL) == 0) {
			warnx("%s: error: no room for the PMBR", gd->device_name);
			return;
		}
		mbr = gpt_read(gd, 0LL, 1);
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
		map = map_add(gd, 0LL, 1LL, MAP_TYPE_PMBR, mbr, 1);
		gpt_write(gd, map);
		printf("%s: written a fresh PMBR\n", gd->device_name);
	}
}

int
cmd_recover(int argc, char *argv[])
{
	int ch;
	int flags = 0;
	gd_t gd;

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
		gd = gpt_open(argv[optind++], flags);
		if (gd == NULL) {
			continue;
		}

		if (rec_pmbr)
			rewrite_pmbr(gd);
		else
			recover(gd);

		gpt_close(gd);
	}

	return (0);
}
