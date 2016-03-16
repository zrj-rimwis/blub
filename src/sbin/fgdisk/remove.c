/*-
 * Copyright (c) 2004 Marcel Moolenaar
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

#include <err.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "map.h"
#include "gpt.h"
#include "gpt_private.h"

static int all;
static uuid_t type;
static off_t block, size;
static unsigned int entry;

static void
usage_remove(void)
{

	fprintf(stderr,
	    "usage: %s -a device ...\n"
	    "       %s [-b sector] [-i index] [-s sectors] [-t uuid] device ...\n",
	    getprogname(), getprogname());
	exit(1);
}

static void
rem(gd_t gd)
{
	uuid_t uuid;
	map_t m;
	struct gpt_hdr *hdr;
	struct gpt_ent *ent;
	unsigned int i;

	if ((hdr = gpt_gethdr(gd)) == NULL)
		return;

	/* Remove all matching entries in the map. */
	for (m = map_first(gd); m != NULL; m = m->map_next) {
		if (m->map_type != MAP_TYPE_GPT_PART || m->map_index < 1)
			continue;
		if (entry > 0 && entry != m->map_index)
			continue;
		if (block > 0 && block != m->map_start)
			continue;
		if (size > 0 && size != m->map_size)
			continue;

		i = m->map_index - 1;

		hdr = gd->gpt->map_data;
		ent = (void*)((char*)gd->tbl->map_data + i *
		    le32toh(hdr->hdr_entsz));
		uuid_dec_le(&ent->ent_type, &uuid);
		if (!uuid_is_nil(&type, NULL) &&
		    !uuid_equal(&type, &uuid, NULL))
			continue;

		/* Remove the primary entry by clearing the partition type. */
		uuid_create_nil(&uuid, NULL);
		uuid_enc_le(&ent->ent_type, &uuid);

		hdr->hdr_crc_table = htole32(crc32(gd->tbl->map_data,
		    le32toh(hdr->hdr_entries) * le32toh(hdr->hdr_entsz)));
		hdr->hdr_crc_self = 0;
		hdr->hdr_crc_self = htole32(crc32(hdr, le32toh(hdr->hdr_size)));

		gpt_write(gd, gd->gpt);
		gpt_write(gd, gd->tbl);

		hdr = gd->tpg->map_data;
		ent = (void*)((char*)gd->lbt->map_data + i *
		    le32toh(hdr->hdr_entsz));

		/* Remove the secondary entry. */
		uuid_enc_le(&ent->ent_type, &uuid);

		hdr->hdr_crc_table = htole32(crc32(gd->lbt->map_data,
		    le32toh(hdr->hdr_entries) * le32toh(hdr->hdr_entsz)));
		hdr->hdr_crc_self = 0;
		hdr->hdr_crc_self = htole32(crc32(hdr, le32toh(hdr->hdr_size)));

		gpt_write(gd, gd->lbt);
		gpt_write(gd, gd->tpg);

		gpt_status(gd, m->map_index, "removed");
	}
}

int
cmd_remove(int argc, char *argv[])
{
	char *p;
	int ch;
	int flags = 0;
	gd_t gd;

	/* Get the remove options */
	while ((ch = getopt(argc, argv, "ab:i:s:t:")) != -1) {
		switch(ch) {
		case 'a':
			if (all > 0)
				usage_remove();
			all = 1;
			break;
		case 'b':
			if (block > 0)
				usage_remove();
			block = strtoll(optarg, &p, 10);
			if (*p != 0 || block < 1)
				usage_remove();
			break;
		case 'i':
			if (entry > 0)
				usage_remove();
			entry = strtol(optarg, &p, 10);
			if (*p != 0 || entry < 1)
				usage_remove();
			break;
		case 's':
			if (size > 0)
				usage_remove();
			size = strtoll(optarg, &p, 10);
			if (*p != 0 || size < 1)
				usage_remove();
			break;
		case 't':
			if (!uuid_is_nil(&type, NULL))
				usage_remove();
			if (parse_uuid(optarg, &type) != 0)
				usage_remove();
			break;
		default:
			usage_remove();
		}
	}

	if (!all ^
	    (block > 0 || entry > 0 || size > 0 || !uuid_is_nil(&type, NULL)))
		usage_remove();

	if (argc == optind)
		usage_remove();

	while (optind < argc) {
		gd = gpt_open(argv[optind++], flags);
		if (gd == NULL) {
			continue;
		}

		rem(gd);

		gpt_close(gd);
	}

	return (0);
}
