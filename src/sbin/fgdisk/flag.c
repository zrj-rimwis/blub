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

static unsigned int flag_entry;
static uint64_t set_attr;
static uint64_t toggle_attr;
static uint64_t unset_attr;

static void
usage_flag(void)
{
	fprintf(stderr,
	    "usage: %s -i index [-s flag] [-t flag] [-u flag] device ...\n",
	    getprogname());
	exit(1);
}

static void
change_flags(gd_t gd)
{
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
		if (flag_entry > 0 && flag_entry != m->map_index)
			continue;

		i = m->map_index - 1;

		hdr = gd->gpt->map_data;
		ent = (void*)((char*)gd->tbl->map_data + i *
		    le32toh(hdr->hdr_entsz));

		/* Perform flags actions on primary entry. */
		ent->ent_attr |= set_attr;	/* set */
		ent->ent_attr ^= toggle_attr;	/* toggle */
		ent->ent_attr &= ~unset_attr;	/* unset */

		hdr->hdr_crc_table = htole32(crc32(gd->tbl->map_data,
		    le32toh(hdr->hdr_entries) * le32toh(hdr->hdr_entsz)));
		hdr->hdr_crc_self = 0;
		hdr->hdr_crc_self = htole32(crc32(hdr, le32toh(hdr->hdr_size)));

		gpt_write(gd, gd->gpt);
		gpt_write(gd, gd->tbl);

		hdr = gd->tpg->map_data;
		ent = (void*)((char*)gd->lbt->map_data + i *
		    le32toh(hdr->hdr_entsz));

		/* Perform flags actions on entry. */
		ent->ent_attr |= set_attr;	/* set */
		ent->ent_attr ^= toggle_attr;	/* toggle */
		ent->ent_attr &= ~unset_attr;	/* unset */

		hdr->hdr_crc_table = htole32(crc32(gd->lbt->map_data,
		    le32toh(hdr->hdr_entries) * le32toh(hdr->hdr_entsz)));
		hdr->hdr_crc_self = 0;
		hdr->hdr_crc_self = htole32(crc32(hdr, le32toh(hdr->hdr_size)));

		gpt_write(gd, gd->lbt);
		gpt_write(gd, gd->tpg);

		gpt_status(gd, m->map_index, "flags changed");
	}
}

int
cmd_flag(int argc, char *argv[])
{
	char *p;
	int ch;
	int flags = 0;
	gd_t gd;

	while ((ch = getopt(argc, argv, "i:s:t:u:")) != -1) {
		switch(ch) {
		case 'i':
			if (flag_entry > 0)
				usage_flag();
			flag_entry = strtol(optarg, &p, 10);
			if (*p != 0 || flag_entry < 1)
				usage_flag();
			break;
		case 's':
			if (strcmp(optarg, "bootme") == 0)
				set_attr |= GPT_ENT_ATTR_BOOTME;
			else if (strcmp(optarg, "bootonce") == 0)
				set_attr |= GPT_ENT_ATTR_BOOTONCE;
			else if (strcmp(optarg, "bootfailed") == 0)
				set_attr |= GPT_ENT_ATTR_BOOTFAILED;
			else
				usage_flag();
			break;
		case 't':
			if (strcmp(optarg, "bootme") == 0)
				toggle_attr |= GPT_ENT_ATTR_BOOTME;
			else if (strcmp(optarg, "bootonce") == 0)
				toggle_attr |= GPT_ENT_ATTR_BOOTONCE;
			else if (strcmp(optarg, "bootfailed") == 0)
				toggle_attr |= GPT_ENT_ATTR_BOOTFAILED;
			else
				usage_flag();
			break;
		case 'u':
			if (strcmp(optarg, "bootme") == 0)
				unset_attr |= GPT_ENT_ATTR_BOOTME;
			else if (strcmp(optarg, "bootonce") == 0)
				unset_attr |= GPT_ENT_ATTR_BOOTONCE;
			else if (strcmp(optarg, "bootfailed") == 0)
				unset_attr |= GPT_ENT_ATTR_BOOTFAILED;
			else
				usage_flag();
			break;
		default:
			usage_flag();
		}
	}

	if (argc == optind || flag_entry == 0)
		usage_flag();

	if (set_attr == 0 && toggle_attr == 0 && unset_attr == 0)
		usage_flag();

	while (optind < argc) {
		gd = gpt_open(argv[optind++], flags);
		if (gd == NULL) {
			continue;
		}

		change_flags(gd);

		gpt_close(gd);
	}

	return (0);
}
