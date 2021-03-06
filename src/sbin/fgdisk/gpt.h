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

#ifndef _GPT_H_
#define	_GPT_H_

#include <sys/endian.h>
#include <sys/gpt.h>

#include <uuid.h>

int	parse_uuid(const char *, uuid_t *);

struct mbr_part {
	uint8_t		part_flag;		/* bootstrap flags */
	uint8_t		part_shd;		/* starting head */
	uint8_t		part_ssect;		/* starting sector */
	uint8_t		part_scyl;		/* starting cylinder */
	uint8_t		part_typ;		/* partition type */
	uint8_t		part_ehd;		/* end head */
	uint8_t		part_esect;		/* end sector */
	uint8_t		part_ecyl;		/* end cylinder */
	uint16_t	part_start_lo;		/* absolute starting ... */
	uint16_t	part_start_hi;		/* ... sector number */
	uint16_t	part_size_lo;		/* partition size ... */
	uint16_t	part_size_hi;		/* ... in sectors */
};

struct mbr {
	uint16_t	mbr_code[223];
	struct mbr_part	mbr_part[4];
	uint16_t	mbr_sig;
#define	MBR_SIG		0xAA55
};

typedef struct gd *gd_t;
typedef struct map *map_t;

extern int gnombr, gquiet, greadonly, gverbose;

uint32_t crc32(const void *, size_t);
map_t	gpt_add_part(gd_t, uuid_t, off_t, off_t, const char *, off_t, unsigned int *);
void	gpt_close(gd_t);
gd_t	gpt_open(const char *, int flags);
void*	gpt_read(gd_t, off_t, size_t);
int	gpt_write(gd_t, map_t);
struct gpt_hdr *gpt_gethdr(gd_t);
void	gpt_status(gd_t, int, const char *);

void	utf16_to_utf8(const uint16_t *, uint8_t *, size_t);
void	utf8_to_utf16(const uint8_t *, uint16_t *, size_t);

int	cmd_add(int, char *[]);
int	cmd_create(int, char *[]);
int	cmd_destroy(int, char *[]);
int	cmd_flag(int, char *[]);
int	cmd_installboot(int, char *[]);
int	cmd_installefi(int, char *[]);
int	cmd_label(int, char *[]);
int	cmd_migrate(int, char *[]);
int	cmd_recover(int, char *[]);
int	cmd_rename(int, char *[]);
int	cmd_remove(int, char *[]);
int	cmd_resize(int, char *[]);
int	cmd_show(int, char *[]);
int	cmd_verify(int, char *[]);

#define GPT_READONLY	0x01
#define GPT_MODIFIED	0x02
#define GPT_QUIET	0x04
#define GPT_NOMBR	0x08
#define GPT_FILE	0x10

#endif /* _GPT_H_ */
