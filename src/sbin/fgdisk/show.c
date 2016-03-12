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
#include "gpt_private.h"

static int show_label = 0;
static int show_uuid = 0;
static int show_guid = 0;
static unsigned int entry = 0;

static void
usage_show(void)
{

	fprintf(stderr,
	    "usage: %s [-glu] [-i index] device ...\n", getprogname());
	exit(1);
}

static const char *
friendly(uuid_t *t, int u)
{
	static const uuid_t boot =	GPT_ENT_TYPE_FREEBSD_BOOT;
	static const uuid_t efi_slice =	GPT_ENT_TYPE_EFI;
	static const uuid_t hammer =	GPT_ENT_TYPE_DRAGONFLY_HAMMER;
	static const uuid_t hammer2 =	GPT_ENT_TYPE_DRAGONFLY_HAMMER2;
	static const uuid_t swap =	GPT_ENT_TYPE_DRAGONFLY_SWAP;
	static const uuid_t ufs =	GPT_ENT_TYPE_DRAGONFLY_UFS1;
	static const uuid_t dl32 =	GPT_ENT_TYPE_DRAGONFLY_LABEL32;
	static const uuid_t dl64 =	GPT_ENT_TYPE_DRAGONFLY_LABEL64;
	static const uuid_t vinum =	GPT_ENT_TYPE_DRAGONFLY_VINUM;
	static const uuid_t msdata =	GPT_ENT_TYPE_MS_BASIC_DATA;
	static const uuid_t freebsd =	GPT_ENT_TYPE_FREEBSD;
	static const uuid_t fswap =	GPT_ENT_TYPE_FREEBSD_SWAP;
	static const uuid_t fufs =	GPT_ENT_TYPE_FREEBSD_UFS;
	static const uuid_t fvinum =	GPT_ENT_TYPE_FREEBSD_VINUM;
	static const uuid_t hfs =	GPT_ENT_TYPE_APPLE_HFS;
	static const uuid_t bios_boot =	GPT_ENT_TYPE_BIOS_BOOT;
	static const uuid_t linuxdata =	GPT_ENT_TYPE_LINUX_DATA;
	static const uuid_t linuxswap =	GPT_ENT_TYPE_LINUX_SWAP;
	static const uuid_t msr =	GPT_ENT_TYPE_MS_RESERVED;
	static char *save_name1 /*= NULL*/;
	static char buf[80];
	char *s;

	if (u == 1)
		goto unfriendly;

	if (uuid_equal(t, &efi_slice, NULL))
		return ("EFI System");
	if (uuid_equal(t, &boot, NULL))
		return ("FreeBSD boot");

	if (uuid_equal(t, &hammer, NULL))
		return ("DragonFly HAMMER");
	if (uuid_equal(t, &hammer2, NULL))
		return ("DragonFly HAMMER2");
	if (uuid_equal(t, &ufs, NULL))
		return ("DragonFly UFS1");
	if (uuid_equal(t, &swap, NULL))
		return ("DragonFly swap");
	if (uuid_equal(t, &dl32, NULL))
		return ("DragonFly disklabel32");
	if (uuid_equal(t, &dl64, NULL))
		return ("DragonFly disklabel64");
	if (uuid_equal(t, &vinum, NULL))
		return ("DragonFly vinum");

	if (uuid_equal(t, &fswap, NULL))
		return ("FreeBSD swap");
	if (uuid_equal(t, &fufs, NULL))
		return ("FreeBSD UFS/UFS2");
	if (uuid_equal(t, &fvinum, NULL))
		return ("FreeBSD vinum");

	if (uuid_equal(t, &bios_boot, NULL))
		return ("BIOS boot");
	if (uuid_equal(t, &linuxdata, NULL))
		return ("Linux data");
	if (uuid_equal(t, &linuxswap, NULL))
		return ("Linux swap");

	if (uuid_equal(t, &freebsd, NULL))
		return ("FreeBSD legacy");
	if (uuid_equal(t, &msdata, NULL))
		return ("Windows");
	if (uuid_equal(t, &msr, NULL))
		return ("Windows reserved");
	if (uuid_equal(t, &hfs, NULL))
		return ("Apple HFS");

	uuid_addr_lookup(t, &save_name1, NULL);
	if (save_name1)
		return (save_name1);

unfriendly:
	uuid_to_string(t, &s, NULL);
	strlcpy(buf, s, sizeof(buf));
	free(s);
	return (buf);
}

static void
show(gd_t gd)
{
	uuid_t guid, type;
	off_t start;
	map_t m, p;
	struct mbr *mbr;
	struct gpt_ent *ent;
	unsigned int i;
	char *s;
	uint8_t utfbuf[NELEM(ent->ent_name) * 3 + 1];

	printf("  %*s", gd->lbawidth, "start");
	printf("  %*s", gd->lbawidth, "size");
	printf("  index  contents\n");

	m = map_first(gd);
	while (m != NULL) {
		printf("  %*llu", gd->lbawidth, (long long)m->map_start);
		printf("  %*llu", gd->lbawidth, (long long)m->map_size);
		putchar(' ');
		putchar(' ');
		if (m->map_index > 0)
			printf("%5d", m->map_index);
		else
			printf("     ");
		putchar(' ');
		putchar(' ');
		switch (m->map_type) {
		case MAP_TYPE_UNUSED:
			printf("Unused");
			break;
		case MAP_TYPE_MBR:
			if (m->map_start != 0)
				printf("Extended ");
			printf("MBR");
			break;
		case MAP_TYPE_PRI_GPT_HDR:
			printf("Pri GPT header");
			break;
		case MAP_TYPE_SEC_GPT_HDR:
			printf("Sec GPT header");
			break;
		case MAP_TYPE_PRI_GPT_TBL:
			printf("Pri GPT table");
			break;
		case MAP_TYPE_SEC_GPT_TBL:
			printf("Sec GPT table");
			break;
		case MAP_TYPE_MBR_PART:
			p = m->map_data;
			if (p->map_start != 0)
				printf("Extended ");
			printf("MBR part ");
			mbr = p->map_data;
			for (i = 0; i < 4; i++) {
				start = le16toh(mbr->mbr_part[i].part_start_hi);
				start = (start << 16) +
				    le16toh(mbr->mbr_part[i].part_start_lo);
				if (m->map_start == p->map_start + start)
					break;
			}
			printf("%d", mbr->mbr_part[i].part_typ);
			break;
		case MAP_TYPE_GPT_PART:
			printf("GPT part ");
			ent = m->map_data;
			if (show_label) {
				utf16_to_utf8(ent->ent_name, utfbuf,
				    sizeof(utfbuf));
				printf("- \"%s\"", (char *)utfbuf);
			} else if (show_guid) {
				uuid_dec_le(&ent->ent_uuid, &guid);
				uuid_to_string(&guid, &s, NULL);
				printf("- %s", s);
				free(s);
			} else {
				uuid_dec_le(&ent->ent_type, &type);
				printf("- %s", friendly(&type, show_uuid));
			}
			break;
		case MAP_TYPE_PMBR:
			printf("PMBR");
			break;
		default:
			printf("Unknown 0x%x", m->map_type);
			break;
		}
		putchar('\n');
		m = m->map_next;
	}
}

static void
show_one(gd_t gd)
{
	uuid_t guid, type;
	map_t m;
	struct gpt_ent *ent;
	const char *s1;
	char *s2;
	uint8_t utfbuf[NELEM(ent->ent_name) * 3 + 1];

	for (m = map_first(gd); m != NULL; m = m->map_next)
		if (entry == m->map_index)
			break;
	if (m == NULL) {
		warnx("%s: error: could not find index %d",
		    gd->device_name, entry);
		return;
	}
	ent = m->map_data;

	printf("Details for index %d:\n", entry);
	printf("Start: %ju\n", (uintmax_t)m->map_start);
	printf("Size:  %ju\n", (uintmax_t)m->map_size);

	uuid_dec_le(&ent->ent_type, &type);
	s1 = friendly(&type, 0);
	uuid_to_string(&type, &s2, NULL);
	if (strcmp(s1, s2) == 0)
		s1 = "unknown";
	printf("Type: %s (%s)\n", s1, s2);
	free(s2);

	uuid_dec_le(&ent->ent_uuid, &guid);
	uuid_to_string(&guid, &s2, NULL);
	printf("GUID: %s\n", s2);
	free(s2);

	utf16_to_utf8(ent->ent_name, utfbuf, sizeof(utfbuf));
	printf("Label: %s\n", (char *)utfbuf);

	printf("Attributes:\n");
	if (ent->ent_attr == 0)
		printf("  None\n");
	else {
		if (ent->ent_attr & GPT_ENT_ATTR_BOOTME)
			printf("  indicates a bootable partition\n");
		if (ent->ent_attr & GPT_ENT_ATTR_BOOTONCE)
			printf("  attempt to boot this partition only once\n");
		if (ent->ent_attr & GPT_ENT_ATTR_BOOTFAILED)
			printf("  partition that was marked bootonce but failed to boot\n");
	}
}

int
cmd_show(int argc, char *argv[])
{
	char *p;
	int ch;
	int flags = 0;
	gd_t gd;

	while ((ch = getopt(argc, argv, "gi:lu")) != -1) {
		switch(ch) {
		case 'g':
			show_guid = 1;
			break;
		case 'i':
			if (entry > 0)
				usage_show();
			entry = strtoul(optarg, &p, 10);
			if (*p != 0 || entry < 1)
				usage_show();
			break;
		case 'l':
			show_label = 1;
			break;
		case 'u':
			show_uuid = 1;
			break;
		default:
			usage_show();
		}
	}

	if (argc == optind)
		usage_show();

	while (optind < argc) {
		gd = gpt_open(argv[optind++], flags);
		if (gd == NULL) {
			continue;
		}

		if (entry > 0)
			show_one(gd);
		else
			show(gd);

		gpt_close(gd);
	}

	return (0);
}
