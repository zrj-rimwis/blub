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
#include <sys/disklabel32.h>
#include <sys/disklabel64.h>
#include <sys/dtype.h>
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

/*
 * Note: struct gpt_hdr is not a multiple of 8 bytes in size and thus
 * contains padding we must not include in the size.
 */
CTASSERT(GPT_MIN_HDR_SIZE == offsetof(struct gpt_hdr, padding));
CTASSERT(GPT_MIN_HDR_SIZE == 0x5c);

static int force;
static int slice;
static u_int parts;

static void
usage_migrate(void)
{
	fprintf(stderr,
	    "usage: %s [-fs] [-p partitions] device ...\n", getprogname());
	exit(1);
}

static struct gpt_ent*
migrate_disklabel32(gd_t gd, off_t start, struct gpt_ent *ent, int *status)
{
	char *buf;
	struct disklabel32 *dl;
	off_t ofs, rawofs;
	int i;

	*status = 1;
	buf = gpt_read(gd, start + LABELSECTOR32, 1);
	dl = (void*)(buf + LABELOFFSET32);

	if (le32toh(dl->d_magic) != DISKMAGIC32 ||
	    le32toh(dl->d_magic2) != DISKMAGIC32) {
		warnx("%s: warning: disklabel32 slice without disklabel",
		    gd->device_name);
		*status = -1;		/* not a disklabel32 */
		free(buf);
		return (ent);
	}

	rawofs = le32toh(dl->d_partitions[RAW_PART].p_offset) *
	    le32toh(dl->d_secsize);
	for (i = 0; i < le16toh(dl->d_npartitions); i++) {
		if (dl->d_partitions[i].p_fstype == FS_UNUSED)
			continue;
		ofs = le32toh(dl->d_partitions[i].p_offset) *
		    le32toh(dl->d_secsize);
		if (ofs < rawofs)
			rawofs = 0;
	}
	rawofs /= gd->secsz;

	for (i = 0; i < le16toh(dl->d_npartitions); i++) {
		switch (dl->d_partitions[i].p_fstype) {
		case FS_UNUSED:
			continue;
		case FS_SWAP: {
			static const uuid_t swap = GPT_ENT_TYPE_DRAGONFLY_SWAP;
			uuid_enc_le(&ent->ent_type, &swap);
			utf8_to_utf16("DragonFly swap partition",
			    ent->ent_name, NELEM(ent->ent_name));
			break;
		}
		case FS_BSDFFS: {
			static const uuid_t ufs = GPT_ENT_TYPE_DRAGONFLY_UFS1;
			uuid_enc_le(&ent->ent_type, &ufs);
			utf8_to_utf16("DragonFly UFS1 partition",
			    ent->ent_name, NELEM(ent->ent_name));
			break;
		}
		case FS_VINUM: {
			static const uuid_t vinum = GPT_ENT_TYPE_DRAGONFLY_VINUM;
			uuid_enc_le(&ent->ent_type, &vinum);
			utf8_to_utf16("DragonFly vinum partition",
			    ent->ent_name, NELEM(ent->ent_name));
			break;
		}
		default:
			warnx("%s: warning: unknown DragonFly partition (%d)",
			    gd->device_name, dl->d_partitions[i].p_fstype);
			continue;
		}

		ofs = (le32toh(dl->d_partitions[i].p_offset) *
		    le32toh(dl->d_secsize)) / gd->secsz;
		ofs = (ofs > 0) ? ofs - rawofs : 0;
		ent->ent_lba_start = htole64(start + ofs);
		ent->ent_lba_end = htole64(start + ofs +
		    le32toh(dl->d_partitions[i].p_size) - 1LL);
		ent++;
	}

	*status = 32;		/* return type on success */
	free(buf);
	return (ent);
}

static struct gpt_ent*
migrate_disklabel64(gd_t gd, off_t start, struct gpt_ent *ent, int *status)
{
	char *buf;
	struct disklabel64 *dl;
	off_t ofs;
	int i;

	*status = 0;
	/* XXX: assumes 512 byte sectors here? */
	buf = gpt_read(gd, start, (sizeof(struct disklabel64) +
	    (gd->secsz -1 )) / gd->secsz);
	dl = (void*)(buf);

	if (le32toh(dl->d_magic) != DISKMAGIC64) {
		warnx("%s: warning: disklabel64 slice without disklabel",
		    gd->device_name);
		*status = -1;		/* not a disklabel64 */
		free(buf);
		return (ent);
	}

	/* safety check, if any unknown - baylout now */
	for (i = 0; i < le16toh(dl->d_npartitions); i++) {
		switch (dl->d_partitions[i].p_fstype) {
		case FS_UNUSED:
		case FS_SWAP:
		case FS_BSDFFS:
		case FS_VINUM:
		case FS_HAMMER:
		case FS_HAMMER2:
			continue;
		default:
			warnx("%s: warning: unknown DragonFly partition (%d)",
			    gd->device_name, dl->d_partitions[i].p_fstype);
			*status = -13;
			free(buf);
			return (ent);
		}
	}

	for (i = 0; i < le16toh(dl->d_npartitions); i++) {
		switch (dl->d_partitions[i].p_fstype) {
		case FS_UNUSED:
			continue;
		case FS_SWAP: {
			static const uuid_t swap = GPT_ENT_TYPE_DRAGONFLY_SWAP;
			uuid_enc_le(&ent->ent_type, &swap);
			utf8_to_utf16("DragonFly swap partition",
			    ent->ent_name, NELEM(ent->ent_name));
			break;
		}
		case FS_BSDFFS: {
			static const uuid_t ufs = GPT_ENT_TYPE_DRAGONFLY_UFS1;
			uuid_enc_le(&ent->ent_type, &ufs);
			utf8_to_utf16("DragonFly UFS1 partition",
			    ent->ent_name, NELEM(ent->ent_name));
			break;
		}
		case FS_VINUM: {
			static const uuid_t vinum = GPT_ENT_TYPE_DRAGONFLY_VINUM;
			uuid_enc_le(&ent->ent_type, &vinum);
			utf8_to_utf16("DragonFly vinum partition",
			    ent->ent_name, NELEM(ent->ent_name));
			break;
		}
		case FS_HAMMER: {
			static const uuid_t vinum = GPT_ENT_TYPE_DRAGONFLY_HAMMER;
			uuid_enc_le(&ent->ent_type, &vinum);
			utf8_to_utf16("DragonFly HAMMER partition",
			    ent->ent_name, NELEM(ent->ent_name));
			break;
		}
		case FS_HAMMER2: {
			static const uuid_t vinum = GPT_ENT_TYPE_DRAGONFLY_HAMMER2;
			uuid_enc_le(&ent->ent_type, &vinum);
			utf8_to_utf16("DragonFly HAMMER2 partition",
			    ent->ent_name, NELEM(ent->ent_name));
			break;
		}
		default:
			warnx("%s: warning: unknown DragonFly partition (%d)",
			    gd->device_name, dl->d_partitions[i].p_fstype);
			continue;
		}

		if ((dl->d_partitions[i].p_boffset) % gd->secsz)
			warnx("partition [%d] start not multiple of %d", i, gd->secsz);
		if ((dl->d_partitions[i].p_bsize) % gd->secsz)
			warnx("partition [%d] size not multiple of %d", i, gd->secsz);

		ofs = (dl->d_partitions[i].p_boffset) / gd->secsz;
		ent->ent_lba_start = htole64((uint64_t)start + ofs);
		ent->ent_lba_end = htole64(start + ofs +
		    (dl->d_partitions[i].p_bsize) / gd->secsz - 1LL);
		ent++;
	}

	*status = 64;		/* return type on success */
	free(buf);
	return (ent);
}

static void
migrate(gd_t gd)
{
	uuid_t uuid;
	off_t blocks, last;
	map_t map;
	struct gpt_hdr *hdr;
	struct gpt_ent *ent;
	struct mbr *mbr;
	uint32_t start, size;
	unsigned int i;

	last = gd->mediasz / gd->secsz - 1LL;

	map = map_find(gd, MAP_TYPE_MBR);
	if (map == NULL || map->map_start != 0) {
		warnx("%s: error: no partitions to convert", gd->device_name);
		return;
	}

	mbr = map->map_data;

	if (map_find(gd, MAP_TYPE_PRI_GPT_HDR) != NULL ||
	    map_find(gd, MAP_TYPE_SEC_GPT_HDR) != NULL) {
		warnx("%s: error: device already contains a GPT", gd->device_name);
		return;
	}

	/* Get the amount of free space after the MBR */
	blocks = map_free(gd, 1LL, 0LL);
	if (blocks == 0LL) {
		warnx("%s: error: no room for the GPT header", gd->device_name);
		return;
	}

	/* Don't create more than parts entries. */
	if ((uint64_t)(blocks - 1) * gd->secsz >
	    parts * sizeof(struct gpt_ent)) {
		blocks = (parts * sizeof(struct gpt_ent)) / gd->secsz;
		if ((parts * sizeof(struct gpt_ent)) % gd->secsz)
			blocks++;
		blocks++;		/* Don't forget the header itself */
	}

	/* Never cross the median of the device. */
	if ((blocks + 1LL) > ((last + 1LL) >> 1))
		blocks = ((last + 1LL) >> 1) - 1LL;

	/*
	 * Get the amount of free space at the end of the device and
	 * calculate the size for the GPT structures.
	 */
	map = map_last(gd);
	if (map->map_type != MAP_TYPE_UNUSED) {
		warnx("%s: error: no room for the backup header", gd->device_name);
		return;
	}

	if (map->map_size < blocks)
		blocks = map->map_size;
	if (blocks == 1LL) {
		warnx("%s: error: no room for the GPT table", gd->device_name);
		return;
	}

	blocks--;		/* Number of blocks in the GPT table. */
	gd->gpt = map_add(gd, 1LL, 1LL, MAP_TYPE_PRI_GPT_HDR,
	    calloc(1, gd->secsz), 1);
	gd->tbl = map_add(gd, 2LL, blocks, MAP_TYPE_PRI_GPT_TBL,
	    calloc(blocks, gd->secsz), 2);
	if (gd->gpt == NULL || gd->tbl == NULL)
		return;

	gd->lbt = map_add(gd, last - blocks, blocks, MAP_TYPE_SEC_GPT_TBL,
	    gd->tbl->map_data, 0);
	gd->tpg = map_add(gd, last, 1LL, MAP_TYPE_SEC_GPT_HDR,
	    calloc(1, gd->secsz), 1);

	hdr = gd->gpt->map_data;
	memcpy(hdr->hdr_sig, GPT_HDR_SIG, sizeof(hdr->hdr_sig));
	hdr->hdr_revision = htole32(GPT_HDR_REVISION);
	hdr->hdr_size = htole32(GPT_MIN_HDR_SIZE);
	hdr->hdr_lba_self = htole64(gd->gpt->map_start);
	hdr->hdr_lba_alt = htole64(gd->tpg->map_start);
	hdr->hdr_lba_start = htole64(gd->tbl->map_start + blocks);
	hdr->hdr_lba_end = htole64(gd->lbt->map_start - 1LL);
	uuid_create(&uuid, NULL);
	uuid_enc_le(&hdr->hdr_uuid, &uuid);
	hdr->hdr_lba_table = htole64(gd->tbl->map_start);
	hdr->hdr_entries = htole32((blocks * gd->secsz) /
	    sizeof(struct gpt_ent));
	if (le32toh(hdr->hdr_entries) > parts)
		hdr->hdr_entries = htole32(parts);
	hdr->hdr_entsz = htole32(sizeof(struct gpt_ent));

	ent = gd->tbl->map_data;
	for (i = 0; i < le32toh(hdr->hdr_entries); i++) {
		uuid_create(&uuid, NULL);
		/* slightly obfuscate mac address for radomness */
		uuid.node[0] = (uuid.node[0] + i) & 0xff;
		uuid_enc_le(&ent[i].ent_uuid, &uuid);
	}

	/* Mirror partitions. */
	for (i = 0; i < 4; i++) {
		start = le16toh(mbr->mbr_part[i].part_start_hi);
		start = (start << 16) + le16toh(mbr->mbr_part[i].part_start_lo);
		size = le16toh(mbr->mbr_part[i].part_size_hi);
		size = (size << 16) + le16toh(mbr->mbr_part[i].part_size_lo);

		switch (mbr->mbr_part[i].part_typ) {
		case DOSPTYP_UNUSED:
			continue;
		case DOSPTYP_386BSD: {	/* DragonFly/FreeBSD */
			if (slice) {
				/* XXX: w/o probing for magic play safe */
				static const uuid_t legacy = GPT_ENT_TYPE_DRAGONFLY_LEGACY;
				uuid_enc_le(&ent->ent_type, &legacy);
				ent->ent_lba_start = htole64((uint64_t)start);
				ent->ent_lba_end = htole64(start + size - 1LL);
				utf8_to_utf16("DragonFly disklabelXX partition",
				    ent->ent_name, NELEM(ent->ent_name));
				ent++;
			} else {
				int status = 0;
				ent = migrate_disklabel64(gd, start, ent, &status);
				if (status == -1) {
					ent = migrate_disklabel32(gd, start, ent, &status);
				}
				if (status <= 0) {
					/* we failed, fallback to legacy */
					static const uuid_t legacy = GPT_ENT_TYPE_DRAGONFLY_LEGACY;
					uuid_enc_le(&ent->ent_type, &legacy);
					ent->ent_lba_start = htole64((uint64_t)start);
					ent->ent_lba_end = htole64(start + size - 1LL);
					utf8_to_utf16("DragonFly unknown partition",
					    ent->ent_name, NELEM(ent->ent_name));
					ent++;
				}
			}
			break;
		}
		case DOSPTYP_EFI: {	/* EFI */
			static const uuid_t efi_slice = GPT_ENT_TYPE_EFI;
			uuid_enc_le(&ent->ent_type, &efi_slice);
			ent->ent_lba_start = htole64((uint64_t)start);
			ent->ent_lba_end = htole64(start + size - 1LL);
			utf8_to_utf16("EFI system partition",
			    ent->ent_name, NELEM(ent->ent_name));
			ent++;
			break;
		}
		default:
			if (!force) {
				warnx("%s: error: unknown partition type (%d)",
				    gd->device_name, mbr->mbr_part[i].part_typ);
				return;
			}
		}
	}
	ent = gd->tbl->map_data;

	hdr->hdr_crc_table = htole32(crc32(ent, le32toh(hdr->hdr_entries) *
	    le32toh(hdr->hdr_entsz)));
	hdr->hdr_crc_self = htole32(crc32(hdr, le32toh(hdr->hdr_size)));

	gpt_write(gd, gd->gpt);
	gpt_write(gd, gd->tbl);

	/*
	 * Create backup GPT.
	 */
	memcpy(gd->tpg->map_data, gd->gpt->map_data, gd->secsz);
	hdr = gd->tpg->map_data;
	hdr->hdr_lba_self = htole64(gd->tpg->map_start);
	hdr->hdr_lba_alt = htole64(gd->gpt->map_start);
	hdr->hdr_lba_table = htole64(gd->lbt->map_start);
	hdr->hdr_crc_self = 0;			/* Don't ever forget this! */
	hdr->hdr_crc_self = htole32(crc32(hdr, le32toh(hdr->hdr_size)));

	gpt_write(gd, gd->lbt);
	gpt_write(gd, gd->tpg);

	map = map_find(gd, MAP_TYPE_MBR);
	mbr = map->map_data;
	/*
	 * Turn the MBR into a Protective MBR.
	 */
	bzero(mbr->mbr_part, sizeof(mbr->mbr_part));
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
	gpt_write(gd, map);

	gpt_status(gd, -1, "slices migrated to gpt partitions, update fstab");
}

int
cmd_migrate(int argc, char *argv[])
{
	char *p;
	int ch;
	int flags = 0;
	gd_t gd;

	parts = 128;

	/* Get the migrate options */
	while ((ch = getopt(argc, argv, "fp:s")) != -1) {
		switch(ch) {
		case 'f':
			force = 1;
			break;
		case 'p':
			parts = strtol(optarg, &p, 10);
			if (*p != 0 || parts < 1)
				usage_migrate();
			break;
		case 's':
			slice = 1;
			break;
		default:
			usage_migrate();
		}
	}

	if (argc == optind)
		usage_migrate();

	while (optind < argc) {
		gd = gpt_open(argv[optind++], flags);
		if (gd == NULL) {
			continue;
		}

		migrate(gd);

		gpt_close(gd);
	}

	return (0);
}
