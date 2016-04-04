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

#include <err.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "map.h"
#include "gpt.h"

static off_t res_align, size;
static unsigned int entry;

static void
usage_resize(void)
{
	fprintf(stderr,
	    "usage: %s -i index [-a alignment] [-s sectors] device ...\n",
	    getprogname());
	exit(1);
}

static void
resize(int fd)
{
	uuid_t uuid;
	map_t *gpt, *tpg;
	map_t *tbl, *lbt;
	map_t *map;
	struct gpt_hdr *hdr;
	struct gpt_ent *ent;
	unsigned int i;
	off_t alignsecs, newsize;


	gpt = map_find(MAP_TYPE_PRI_GPT_HDR);
	ent = NULL;
	if (gpt == NULL) {
		warnx("%s: error: no primary GPT header; run create or recover",
		    device_name);
		return;
	}

	tpg = map_find(MAP_TYPE_SEC_GPT_HDR);
	if (tpg == NULL) {
		warnx("%s: error: no secondary GPT header; run recover",
		    device_name);
		return;
	}

	tbl = map_find(MAP_TYPE_PRI_GPT_TBL);
	lbt = map_find(MAP_TYPE_SEC_GPT_TBL);
	if (tbl == NULL || lbt == NULL) {
		warnx("%s: error: run recover -- trust me", device_name);
		return;
	}

	hdr = gpt->map_data;
	if (entry > le32toh(hdr->hdr_entries)) {
		warnx("%s: error: index %u out of range (%u max)", device_name,
		    entry, le32toh(hdr->hdr_entries));
		return;
	}

	i = entry - 1;
	ent = (void*)((char*)tbl->map_data + i *
	    le32toh(hdr->hdr_entsz));
	uuid_dec_le(&ent->ent_type, &uuid);
	if (uuid_is_nil(&uuid, NULL)) {
		warnx("%s: error: entry at index %u is unused",
		    device_name, entry);
		return;
	}

	alignsecs = res_align / secsz;

	for (map = map_first(); map != NULL; map = map->map_next) {
		if (entry == map->map_index)
			break;
	}
	if (map == NULL) {
		warnx("%s: error: could not find map entry corresponding "
		      "to index", device_name);
		return;
	}

	if (size > 0 && size == map->map_size)
		if (res_align == 0 ||
		    (res_align > 0 && size % alignsecs == 0)) {
			/* nothing to do */
			warnx("%s: partition does not need resizing",
			    device_name);
			return;
		}

	newsize = map_resize(map, size, alignsecs);
	if (newsize == 0 && res_align > 0) {
		warnx("%s: could not resize partition with alignment "
		      "constraint", device_name);
		return;
	} else if (newsize == 0) {
		warnx("%s: could not resize partition", device_name);
		return;
	}

	ent->ent_lba_end = htole64(map->map_start + newsize - 1LL);

	hdr->hdr_crc_table = htole32(crc32(tbl->map_data,
	    le32toh(hdr->hdr_entries) * le32toh(hdr->hdr_entsz)));
	hdr->hdr_crc_self = 0;
	hdr->hdr_crc_self = htole32(crc32(hdr, le32toh(hdr->hdr_size)));

	gpt_write(fd, gpt);
	gpt_write(fd, tbl);

	hdr = tpg->map_data;
	ent = (void*)((char*)lbt->map_data + i * le32toh(hdr->hdr_entsz));

	ent->ent_lba_end = htole64(map->map_start + newsize - 1LL);

	hdr->hdr_crc_table = htole32(crc32(lbt->map_data,
	    le32toh(hdr->hdr_entries) * le32toh(hdr->hdr_entsz)));
	hdr->hdr_crc_self = 0;
	hdr->hdr_crc_self = htole32(crc32(hdr, le32toh(hdr->hdr_size)));

	gpt_write(fd, lbt);
	gpt_write(fd, tpg);

	printf("%sp%u resized\n", device_name, entry);
}

int
cmd_resize(int argc, char *argv[])
{
	char *p;
	int ch, fd;

	/* Get the resize options */
	while ((ch = getopt(argc, argv, "a:i:s:")) != -1) {
		switch(ch) {
		case 'a':
			if (res_align > 0)
				usage_resize();
			res_align = strtoll(optarg, &p, 10);
			if (res_align < 1)
				usage_resize();
			break;
		case 'i':
			if (entry > 0)
				usage_resize();
			entry = strtoul(optarg, &p, 10);
			if (*p != 0 || entry < 1)
				usage_resize();
			break;
		case 's':
			if (size > 0)
				usage_resize();
			size = strtoll(optarg, &p, 10);
			if (*p != 0 || size < 1)
				usage_resize();
			break;
		default:
			usage_resize();
		}
	}

	if (argc == optind)
		usage_resize();

	if (entry == 0)
		usage_resize();

	while (optind < argc) {
		fd = gpt_open(argv[optind++]);
		if (fd == -1) {
			warn("unable to open device '%s'", device_name);
			continue;
		}

		if (res_align % secsz != 0) {
			warnx("Alignment must be a multiple of sector size;");
			warnx("the sector size for %s is %d bytes.",
			    device_name, secsz);
			continue;
		}

		resize(fd);

		gpt_close(fd);
	}

	return 0;
}
