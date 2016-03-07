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
#include <sys/param.h>

#include <err.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "map.h"
#include "gpt.h"

static uuid_t add_type;
static off_t add_align, add_block, add_size;
static unsigned int add_entry;
static char *add_name;

static void
usage_add(void)
{
	fprintf(stderr,
	    "usage: %s [-a alignment] [-b sector] [-i index] [-l label] \n"
	    "       %*s [-s sectors] [-t uuid] device ...\n",
	    getprogname(), (int)strlen(getprogname()),"");
	exit(1);
}

map_t *
gpt_add_part(int fd, uuid_t type, off_t align, off_t start, const char *name,
    off_t size, unsigned int *entry)
{
	uuid_t uuid;
	map_t *gpt, *tpg;
	map_t *tbl, *lbt;
	map_t *map;
	struct gpt_hdr *hdr;
	struct gpt_ent *ent;
	unsigned int i;
	off_t alignsecs;

	gpt = map_find(MAP_TYPE_PRI_GPT_HDR);
	if (gpt == NULL) {
		warnx("%s: error: no primary GPT header; run create or recover",
		    device_name);
		return (NULL);
	}

	tpg = map_find(MAP_TYPE_SEC_GPT_HDR);
	if (tpg == NULL) {
		warnx("%s: error: no secondary GPT header; run recover",
		    device_name);
		return (NULL);
	}

	tbl = map_find(MAP_TYPE_PRI_GPT_TBL);
	lbt = map_find(MAP_TYPE_SEC_GPT_TBL);
	if (tbl == NULL || lbt == NULL) {
		warnx("%s: error: run recover -- trust me", device_name);
		return (NULL);
	}

	hdr = gpt->map_data;
	if (*entry > le32toh(hdr->hdr_entries)) {
		warnx("%s: error: index %u out of range (%u max)", device_name,
		    *entry, le32toh(hdr->hdr_entries));
		return (NULL);
	}

	if (*entry > 0) {
		i = *entry - 1;
		ent = (void*)((char*)tbl->map_data + i *
		    le32toh(hdr->hdr_entsz));
		uuid_dec_le(&ent->ent_type, &uuid);
		if (!uuid_is_nil(&uuid, NULL)) {
			warnx("%s: error: entry at index %u is not free",
			    device_name, *entry);
			return (NULL);
		}
	} else {
		/* Find empty slot in GPT table. */
		ent = NULL;
		for (i = 0; i < le32toh(hdr->hdr_entries); i++) {
			ent = (void*)((char*)tbl->map_data + i *
			    le32toh(hdr->hdr_entsz));
			uuid_dec_le(&ent->ent_type, &uuid);
			if (uuid_is_nil(&uuid, NULL))
				break;
		}
		if (i == le32toh(hdr->hdr_entries)) {
			warnx("%s: error: no available table entries",
			    device_name);
			return (NULL);
		}
	}

	if (align > 0) {
		alignsecs = align / secsz;
		map = map_alloc(start, size, alignsecs);
		if (map == NULL) {
			warnx("%s: error: not enough space available on "
			      "device for an aligned partition", device_name);
			return (NULL);
		}
	} else {
		map = map_alloc(start, size, 0);
		if (map == NULL) {
			warnx("%s: error: not enough space available on device",
			    device_name);
			return (NULL);
		}
	}

	uuid_enc_le(&ent->ent_type, &type);
	ent->ent_lba_start = htole64(map->map_start);
	ent->ent_lba_end = htole64(map->map_start + map->map_size - 1LL);
	if (name != NULL)
		utf8_to_utf16(name, ent->ent_name, NELEM(ent->ent_name));

	hdr->hdr_crc_table = htole32(crc32(tbl->map_data,
	    le32toh(hdr->hdr_entries) * le32toh(hdr->hdr_entsz)));
	hdr->hdr_crc_self = 0;
	hdr->hdr_crc_self = htole32(crc32(hdr, le32toh(hdr->hdr_size)));

	gpt_write(fd, gpt);
	gpt_write(fd, tbl);

	hdr = tpg->map_data;
	ent = (void*)((char*)lbt->map_data + i * le32toh(hdr->hdr_entsz));

	uuid_enc_le(&ent->ent_type, &type);
	ent->ent_lba_start = htole64(map->map_start);
	ent->ent_lba_end = htole64(map->map_start + map->map_size - 1LL);
	if (name != NULL)
		utf8_to_utf16(name, ent->ent_name, NELEM(ent->ent_name));

	hdr->hdr_crc_table = htole32(crc32(lbt->map_data,
	    le32toh(hdr->hdr_entries) * le32toh(hdr->hdr_entsz)));
	hdr->hdr_crc_self = 0;
	hdr->hdr_crc_self = htole32(crc32(hdr, le32toh(hdr->hdr_size)));

	gpt_write(fd, lbt);
	gpt_write(fd, tpg);

	*entry = i + 1;

	return (map);
}

static void
add(int fd)
{
	map_t *map;
	unsigned int index;

	index = add_entry;
	map = gpt_add_part(fd, add_type, add_align, add_block, add_name,
	    add_size, &index);
	if (map == NULL)
		return;

	printf("%sp%u (compat %ss%u) added\n", device_name, index,
	    device_name, index-1);
}

int
cmd_add(int argc, char *argv[])
{
	char *p;
	int ch, fd;

	/* Get the add options */
	while ((ch = getopt(argc, argv, "a:b:i:l:s:t:")) != -1) {
		switch(ch) {
		case 'a':
			if (add_align > 0)
				usage_add();
			add_align = strtoll(optarg, &p, 10);
			if (add_align < 1)
				usage_add();
			break;
		case 'b':
			if (add_block > 0)
				usage_add();
			add_block = strtoll(optarg, &p, 10);
			if (*p != 0 || add_block < 1)
				usage_add();
			break;
		case 'i':
			if (add_entry > 0)
				usage_add();
			add_entry = strtol(optarg, &p, 10);
			if (*p != 0 || add_entry < 1)
				usage_add();
			break;
		case 'l':
			if (add_name != NULL)
				usage_add();
			add_name = (uint8_t *)strdup(optarg);
			break;
		case 's':
			if (add_size > 0)
				usage_add();
			add_size = strtoll(optarg, &p, 10);
			if (*p != 0 || add_size < 1)
				usage_add();
			break;
		case 't':
			if (!uuid_is_nil(&add_type, NULL))
				usage_add();
			if (parse_uuid(optarg, &add_type) != 0)
				usage_add();
			break;
		default:
			usage_add();
		}
	}

	if (argc == optind)
		usage_add();

	/* Create UFS1 partitions by default. */
	if (uuid_is_nil(&add_type, NULL)) {
		static const uuid_t ufs = GPT_ENT_TYPE_DRAGONFLY_UFS1;
		add_type = ufs;
	}

	while (optind < argc) {
		fd = gpt_open(argv[optind++]);
		if (fd == -1) {
			warn("unable to open device '%s'", device_name);
			continue;
		}

		if (add_align % secsz != 0) {
			warnx("Alignment must be a multiple of sector size;");
			warnx("the sector size for %s is %d bytes.",
			    device_name, secsz);
			continue;
		}

		add(fd);

		gpt_close(fd);
	}

	if (add_name != NULL)
		free(add_name);

	return (0);
}
