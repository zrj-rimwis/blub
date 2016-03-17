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
#include <sys/diskslice.h>
#include <sys/diskmbr.h>

#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <paths.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "map.h"
#include "gpt.h"
#include "gpt_private.h"

static uuid_t ren_type, new_type;
static off_t ren_block, ren_size;
static unsigned int ren_entry;

static void
usage_rename(void)
{
	fprintf(stderr,
	    "usage: %s [-b sector] [-i index] [-s sectors] [-t uuid]\n"
	    "       %*s -T uuid device ...\n",
	    getprogname(),(int)strlen(getprogname()),"");
	exit(1);
}

static void
change_name(gd_t gd)
{
	uuid_t uuid;
	map_t m;
	struct gpt_hdr *hdr;
	struct gpt_ent *ent;
	unsigned int i;

	if ((hdr = gpt_gethdr(gd)) == NULL)
		return;

	/* Rename all matching entries in the map. */
	for (m = map_first(gd); m != NULL; m = m->map_next) {
		if (m->map_type != MAP_TYPE_GPT_PART || m->map_index < 1)
			continue;
		if (ren_entry > 0 && ren_entry != m->map_index)
			continue;
		if (ren_block > 0 && ren_block != m->map_start)
			continue;
		if (ren_size > 0 && ren_size != m->map_size)
			continue;

		i = m->map_index - 1;

		hdr = gd->gpt->map_data;
		ent = (void*)((char*)gd->tbl->map_data + i *
		    le32toh(hdr->hdr_entsz));
		uuid_dec_le(&ent->ent_type, &uuid);
		if (!uuid_is_nil(&ren_type, NULL) &&
		    !uuid_equal(&ren_type, &uuid, NULL))
			continue;

		/* Rename the primary entry. */
		uuid_enc_le(&ent->ent_type, &new_type);

		hdr->hdr_crc_table = htole32(crc32(gd->tbl->map_data,
		    le32toh(hdr->hdr_entries) * le32toh(hdr->hdr_entsz)));
		hdr->hdr_crc_self = 0;
		hdr->hdr_crc_self = htole32(crc32(hdr, le32toh(hdr->hdr_size)));

		gpt_write(gd, gd->gpt);
		gpt_write(gd, gd->tbl);

		hdr = gd->tpg->map_data;
		ent = (void*)((char*)gd->lbt->map_data + i *
		    le32toh(hdr->hdr_entsz));

		/* Rename the secondary entry. */
		uuid_enc_le(&ent->ent_type, &new_type);

		hdr->hdr_crc_table = htole32(crc32(gd->lbt->map_data,
		    le32toh(hdr->hdr_entries) * le32toh(hdr->hdr_entsz)));
		hdr->hdr_crc_self = 0;
		hdr->hdr_crc_self = htole32(crc32(hdr, le32toh(hdr->hdr_size)));

		gpt_write(gd, gd->lbt);
		gpt_write(gd, gd->tpg);

		gpt_status(gd, m->map_index, "renamed");
	}
}

int
cmd_rename(int argc, char *argv[])
{
	char *p;
	int ch;
	int flags = 0;
	gd_t gd;

	while ((ch = getopt(argc, argv, "b:i:s:t:T:")) != -1) {
		switch(ch) {
		case 'b':
			if (ren_block > 0)
				usage_rename();
			ren_block = strtoll(optarg, &p, 10);
			if (*p != 0 || ren_block < 1)
				usage_rename();
			break;
		case 'i':
			if (ren_entry > 0)
				usage_rename();
			ren_entry = strtol(optarg, &p, 10);
			if (*p != 0 || ren_entry < 1)
				usage_rename();
			break;
		case 's':
			if (ren_size > 0)
				usage_rename();
			ren_size = strtoll(optarg, &p, 10);
			if (*p != 0 || ren_size < 1)
				usage_rename();
			break;
		case 't':
			if (!uuid_is_nil(&ren_type, NULL))
				usage_rename();
			if (parse_uuid(optarg, &ren_type) != 0)
				usage_rename();
			break;
		case 'T':
			if (!uuid_is_nil(&new_type, NULL))
				usage_rename();
			if (parse_uuid(optarg, &new_type) != 0)
				usage_rename();
			break;
		default:
			usage_rename();
		}
	}

	if (argc == optind || uuid_is_nil(&new_type, NULL))
		usage_rename();

	while (optind < argc) {
		gd = gpt_open(argv[optind++], flags);
		if (gd == NULL) {
			continue;
		}

		change_name(gd);

		gpt_close(gd);
	}

	return (0);
}
