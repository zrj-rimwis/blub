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

static int check_pri, check_sec;

static void
usage_verify(void)
{
	fprintf(stderr,
	    "usage: %s [-PS] device ...\n", getprogname());
	exit(1);
}

static int
verify_print2(int cond, unsigned int num ,const char *msg)
{
	if (cond) {
		if (gverbose > 1)
			printf("%s %u - OK\n", msg, num);
		return (0);
	} else {
		printf("%s %u - FAIL\n", msg, num);
		return (-1);
	}
}

static int
verify_print(int cond, const char *msg)
{
	if (cond) {
		if (gverbose)
			printf("%s - OK\n", msg);
		return (0);
	} else {
		printf("%s - FAIL\n", msg);
		return (-1);
	}
}

static int
print_header(struct gpt_hdr *hdr)
{
	unsigned int rev;
	struct uuid guid;
	char *s;
	static char buf[80];

	if (hdr == NULL) {
		warnx("no headers, can't dump");
		return (-1);
	}

	printf(" hdr_sig= %.8s\n", hdr->hdr_sig);

	rev = le32toh(hdr->hdr_revision);
	printf(" hdr_revision=  %u.%u\n", rev >> 16, rev & 0xffff);
	printf(" hdr_size=      0x%x\n", hdr->hdr_size);

	hdr->hdr_crc_self = 0;
	hdr->hdr_crc_self = htole32(crc32(hdr, le32toh(hdr->hdr_size)));
	printf(" hdr_src_elf=   0x%x\n", hdr->hdr_crc_self);

	printf(" hdr_lba_self=  0x%lx\n", hdr->hdr_lba_self);
	printf(" hdr_lba_alt=   0x%lx\n", hdr->hdr_lba_alt);
	printf(" hdr_lba_start= 0x%lx\n", hdr->hdr_lba_start);
	printf(" hdr_lba_end=   0x%lx\n", hdr->hdr_lba_end);

	uuid_dec_le(&hdr->hdr_uuid, &guid);
	uuid_to_string(&guid, &s, NULL);
	strlcpy(buf, s, sizeof(buf));
	free(s);
	printf(" hdr_uuid = %s\n", buf);

	printf(" hdr_lba_table= 0x%lx\n", hdr->hdr_lba_table);

	printf(" hdr_entries=   0x%x\n", hdr->hdr_entries);
	printf(" hdr_entsz=     0x%x\n", hdr->hdr_entsz);
	printf(" hdr_crc_table= 0x%x\n", hdr->hdr_crc_table);

	return (0);
}

static int
compare_headers(struct gpt_hdr *hp, struct gpt_hdr *hs)
{
	int lcond;
	int ret = 0;

	if (hp == NULL || hs == NULL) {
		warnx("no primary or secondary GPT headers, can't verify");
		return (-1);
	}

	if (gverbose)
		printf("\nComparing headers:\n");

	lcond = (!memcmp(hp->hdr_sig, hs->hdr_sig, sizeof(hp->hdr_sig)));
	ret += verify_print(lcond, "  signatures");

	lcond = (hp->hdr_revision == hs->hdr_revision);
	ret += verify_print(lcond, "  revisions");

	lcond = (hp->hdr_size == hs->hdr_size);
	ret += verify_print(lcond, "  size");

	hp->hdr_crc_self = 0;
	hs->hdr_crc_self = 0;
	hp->hdr_crc_self = htole32(crc32(hp, le32toh(hp->hdr_size)));
	hs->hdr_crc_self = htole32(crc32(hs, le32toh(hs->hdr_size)));
	lcond = (hp->hdr_crc_self != hs->hdr_crc_self);
	ret += verify_print(lcond, "  crc_self differs");

	lcond = (hp->hdr_lba_self == hs->hdr_lba_alt);
	ret += verify_print(lcond, "  lba_self == lba_alt");

	lcond = (hp->hdr_lba_alt == hs->hdr_lba_self);
	ret += verify_print(lcond, "  lba_alt == lba_self");

	lcond = (hp->hdr_lba_start == hs->hdr_lba_start);
	ret += verify_print(lcond, "  lba_start");

	lcond = (hp->hdr_lba_end == hs->hdr_lba_end);
	ret += verify_print(lcond, "  lba_end");

	lcond = (hp->hdr_lba_table != hs->hdr_lba_table);
	ret += verify_print(lcond, "  lba_table differs");

	lcond = uuid_equal(&hp->hdr_uuid, &hs->hdr_uuid, NULL);
	ret += verify_print(lcond, "  guid");

	lcond = (hp->hdr_entries == hs->hdr_entries);
	ret += verify_print(lcond, "  entries");

	lcond = (hp->hdr_entsz == hs->hdr_entsz);
	ret += verify_print(lcond, "  entsz");

	lcond = (hp->hdr_crc_table = hs->hdr_crc_table);
	ret += verify_print(lcond, "  crc_table");

	return (ret);
}

static int
compare_entries(struct gpt_ent *e1, struct gpt_ent *e2, unsigned int num)
{
	int lcond;
	int ret = 0;

	if (e1 == NULL || e2 == NULL) {
		warnx("NULL entries, can't verify");
		return (-1);
	}

	lcond = uuid_equal(&e1->ent_type, &e2->ent_type, NULL);
	ret += verify_print2(lcond, num, "    type");
	lcond = uuid_equal(&e1->ent_uuid, &e2->ent_uuid, NULL);
	ret += verify_print2(lcond, num, "    guid");

	lcond = (e1->ent_lba_start == e2->ent_lba_start);
	ret += verify_print2(lcond, num, "    lba_start");

	lcond = (e1->ent_lba_end == e2->ent_lba_end);
	ret += verify_print2(lcond, num, "    lba_end");

	lcond = (e1->ent_attr == e2->ent_attr);
	ret += verify_print2(lcond, num, "    attr");

	lcond = (!memcmp(e1->ent_name, e1->ent_name, sizeof(e1->ent_name)));
	ret += verify_print2(lcond, num, "    name");

	return (ret);
}

static int
compare_tables(struct gpt_hdr *hp, struct gpt_hdr *hs, struct gpt_ent *ep,
    struct gpt_ent *es)
{
	int lcond;
	unsigned int entries, entsz;
	unsigned int i;
	int ret = 0;
	struct gpt_ent *ent1 = NULL, *ent2 = NULL;

	if (hp == NULL || hs == NULL) {
		warnx("no primary or secondary GPT headers, can't verify");
		return (-1);
	}
	if (ep == NULL || es == NULL) {
		warnx("no primary or secondary GPT tables, can't verify");
		return (-1);
	}

	entries = le32toh(hp->hdr_entries);
	if (le32toh(hp->hdr_entries) != le32toh(hs->hdr_entries)) {
		warn("hdr_entries differ");
		entries = MIN(le32toh(hp->hdr_entries), le32toh(hs->hdr_entries));
	}

	entsz = le32toh(hp->hdr_entsz);
	if (le32toh(hp->hdr_entsz) != le32toh(hs->hdr_entsz)) {
		warn("hdr_entsz differ");
		entsz = MIN(le32toh(hp->hdr_entsz), le32toh(hs->hdr_entsz));
	}

	if ((entries != entsz)) {
		entsz = MIN(entries, entsz);
		warnx("using only %u entries", entsz);
	}

	if (gverbose)
		printf("\nComparing tables:\n");

	for (i = 0; i < entsz; i++) {
		ent1 = (void*)((char*)ep + i * entsz);
		ent2 = (void*)((char*)es + i * entsz);
		lcond = !compare_entries(ent1, ent2, i+1);
		ret += verify_print2(lcond, i + 1, "  entry");
	}

	return (ret);
}

static void
verify(gd_t gd)
{
	map_t pmbr;
	struct gpt_hdr *hp = NULL, *hs = NULL;
	struct gpt_ent *ep = NULL, *es = NULL;

	if (check_pri) {
		gd->gpt = map_find(gd, MAP_TYPE_PRI_GPT_HDR);
		gd->tbl = map_find(gd, MAP_TYPE_PRI_GPT_TBL);
		if (verify_print((gd->gpt != NULL), "Have Primary header") == 0)
			hp = gd->gpt->map_data;
		if (verify_print((gd->tbl != 0), "Have Primary table") == 0)
			ep = (void*)(char*)gd->tbl->map_data;
		/* XXX somehow recover from increased media size */
		if (gd->gpt != NULL &&
		    (((struct gpt_hdr *)(gd->gpt->map_data))->hdr_lba_alt !=
		    (uint64_t)(gd->mediasz/gd->secsz - 1LL))) {
			warnx("%s: media size has changed", gd->device_name);
			return;
		}
	}

	if (check_sec) {
		gd->tpg = map_find(gd, MAP_TYPE_SEC_GPT_HDR);
		gd->lbt = map_find(gd, MAP_TYPE_SEC_GPT_TBL);
		if (verify_print((gd->tpg != NULL), "Have Secondary header") == 0)
			hs = gd->tpg->map_data;
		if (verify_print((gd->lbt != 0), "Have Secondary table") == 0)
			es = (void*)(char*)gd->lbt->map_data;
	}
	if (check_pri && check_sec) {
		/* First check if there is a PMBR */
		pmbr = map_find(gd, MAP_TYPE_PMBR);
		verify_print((pmbr != 0), "Have PMBR");

		verify_print((compare_headers(hp, hs) == 0), "Both headers match");
		verify_print((compare_tables(hp, hs, ep, es) == 0), "Both tables match");
	} else if (check_pri) {
		print_header(hp);
	} else if (check_sec) {
		print_header(hs);
	}
}

int
cmd_verify(int argc, char *argv[])
{
	int ch;
	int flags = 0;
	gd_t gd;

	while ((ch = getopt(argc, argv, "PS")) != -1) {
		switch(ch) {
		case 'P':
			check_pri = 1;
			break;
		case 'S':
			check_sec = 1;
			break;
		default:
			usage_verify();
		}
	}

	if (argc == optind)
		usage_verify();

	if ((check_pri == 0) && (check_sec == 0)) {
		check_pri = 1;
		check_sec = 1;
	}

	/* Open w/o mbr part parsing */
	flags |= GPT_NOMBR;

	while (optind < argc) {
		gd = gpt_open(argv[optind++], flags);
		if (gd == NULL) {
			continue;
		}

		verify(gd);

		gpt_close(gd);
	}

	return (0);
}
