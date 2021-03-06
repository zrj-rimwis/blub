/*-
 * Copyright (c) 2005 Marcel Moolenaar
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
#include "gpt_private.h"

static int all;
static uuid_t type;
static off_t block, size;
static unsigned int entry;
static uint8_t *name;

static void
usage_label(void)
{
	const char *common = "<-l label | -f file> device ...";

	fprintf(stderr,
	    "usage: %s -a %s\n"
	    "       %s [-b sector] [-i index] [-s sectors] [-t uuid] %s\n",
	    getprogname(), common, getprogname(), common);
	exit(1);
}

static void
label(gd_t gd)
{
	uuid_t uuid;
	map_t m;
	struct gpt_hdr *hdr;
	struct gpt_ent *ent;
	unsigned int i;

	if ((hdr = gpt_gethdr(gd)) == NULL)
		return;

	/* Relabel all matching entries in the map. */
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

		/* Label the primary entry. */
		utf8_to_utf16(name, ent->ent_name, NELEM(ent->ent_name));

		hdr->hdr_crc_table = htole32(crc32(gd->tbl->map_data,
		    le32toh(hdr->hdr_entries) * le32toh(hdr->hdr_entsz)));
		hdr->hdr_crc_self = 0;
		hdr->hdr_crc_self = htole32(crc32(hdr, le32toh(hdr->hdr_size)));

		gpt_write(gd, gd->gpt);
		gpt_write(gd, gd->tbl);

		hdr = gd->tpg->map_data;
		ent = (void*)((char*)gd->lbt->map_data + i *
		    le32toh(hdr->hdr_entsz));

		/* Label the secondary entry. */
		utf8_to_utf16(name, ent->ent_name, NELEM(ent->ent_name));

		hdr->hdr_crc_table = htole32(crc32(gd->lbt->map_data,
		    le32toh(hdr->hdr_entries) * le32toh(hdr->hdr_entsz)));
		hdr->hdr_crc_self = 0;
		hdr->hdr_crc_self = htole32(crc32(hdr, le32toh(hdr->hdr_size)));

		gpt_write(gd, gd->lbt);
		gpt_write(gd, gd->tpg);

		gpt_status(gd, m->map_index, "labeled");
	}
}

static void
name_from_file(const char *fn)
{
	FILE *f;
	char *p;
	size_t maxlen = 1024;
	size_t len;

	if (strcmp(fn, "-") != 0) {
		f = fopen(fn, "r");
		if (f == NULL)
			err(1, "unable to open file %s", fn);
	} else
		f = stdin;
	name = malloc(maxlen);
	len = fread(name, 1, maxlen - 1, f);
	if (ferror(f))
		err(1, "unable to read label from file %s", fn);
	if (f != stdin)
		fclose(f);
	name[len] = '\0';
	/* Only keep the first line, excluding the newline character. */
	p = strchr(name, '\n');
	if (p != NULL)
		*p = '\0';
}

int
cmd_label(int argc, char *argv[])
{
	char *p;
	int ch;
	int flags = 0;
	gd_t gd;

	/* Get the label options */
	while ((ch = getopt(argc, argv, "ab:f:i:l:s:t:")) != -1) {
		switch(ch) {
		case 'a':
			if (all > 0)
				usage_label();
			all = 1;
			break;
		case 'b':
			if (block > 0)
				usage_label();
			block = strtoll(optarg, &p, 10);
			if (*p != 0 || block < 1)
				usage_label();
			break;
		case 'f':
			if (name != NULL)
				usage_label();
			name_from_file(optarg);
			break;
		case 'i':
			if (entry > 0)
				usage_label();
			entry = strtol(optarg, &p, 10);
			if (*p != 0 || entry < 1)
				usage_label();
			break;
		case 'l':
			if (name != NULL)
				usage_label();
			name = strdup(optarg);
			break;
		case 's':
			if (size > 0)
				usage_label();
			size = strtoll(optarg, &p, 10);
			if (*p != 0 || size < 1)
				usage_label();
			break;
		case 't':
			if (!uuid_is_nil(&type, NULL))
				usage_label();
			if (parse_uuid(optarg, &type) != 0)
				usage_label();
			break;
		default:
			usage_label();
		}
	}

	if (!all ^
	    (block > 0 || entry > 0 || size > 0 || !uuid_is_nil(&type, NULL)))
		usage_label();

	if (name == NULL || argc == optind)
		usage_label();

	while (optind < argc) {
		gd = gpt_open(argv[optind++], flags);
		if (gd == NULL) {
			continue;
		}

		label(gd);

		gpt_close(gd);
	}

	return (0);
}
