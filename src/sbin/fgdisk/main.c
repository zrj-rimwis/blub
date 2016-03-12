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
 * CRC32 code derived from work by Gary S. Brown.
 *
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <err.h>

#include "map.h"
#include "gpt.h"

static int pcmd_help(int argc, char *argv[]);

static struct {
	int (*fptr)(int, char *[]);
	const char *name;
} cmdsw[] = {
	{ cmd_add, "add" },
	{ cmd_create, "create" },
	{ cmd_destroy, "destroy" },
	{ pcmd_help, "help" },
	{ cmd_label, "label" },
	{ cmd_installboot, "installboot" },
	{ cmd_migrate, "migrate" },
	{ cmd_recover, "recover" },
	{ cmd_remove, "remove" },
	{ NULL, "rename" },
	{ cmd_resize, "resize" },
	{ cmd_show, "show" },
	{ NULL, "verify" },
	{ NULL, NULL }
};

static void
usage(void)
{
	fprintf(stderr,
	    "usage: %s [-rv] command [options] device ...\n",
	    getprogname());
	exit(1);
}

static int
pcmd_help(int argc __unused, char *argv[] __unused)
{
	fprintf(stderr,
	    "Available commands:\n%s\n",
	    "    add         - add new gpt partition\n"
	    "    create      - create new gpt disk layout\n"
	    "    destroy     - destroy gpt disk layout\n"
	    "    help        - print this help\n"
	    "    label       - change labels of gpt partions\n"
	    "    installboot - install gptboot with pmbr for bios-gpt boot\n"
	    "    migrate     - migrate slices to gpt partitions\n"
	    "    recover     - gpt disk recovery\n"
	    "    remove      - remove gpt partition\n"
	    "    resize      - resize gpt partition\n"
	    "    show        - print info about gpt disk/partitions\n"
	    );
	return (0);
}

static void
prefix(const char *cmd)
{
	char *pfx;
	const char *prg;

	prg = getprogname();
	pfx = malloc(strlen(prg) + strlen(cmd) + 2);
	/* Don't bother failing. It's not important */
	if (pfx == NULL)
		return;

	sprintf(pfx, "%s %s", prg, cmd);
	setprogname(pfx);
}

int
main(int argc, char *argv[])
{
	char *cmd;
	int ch, i;

	/* Get the generic options */
	while ((ch = getopt(argc, argv, "rv")) != -1) {
		switch(ch) {
		case 'r':
			greadonly = 1;
			break;
		case 'v':
			gverbose++;
			break;
		default:
			usage();
		}
	}

	if (argc == optind)
		usage();

	cmd = argv[optind++];
	for (i = 0; cmdsw[i].name != NULL && strcmp(cmd, cmdsw[i].name); i++);

	if (cmdsw[i].fptr == NULL)
		errx(1, "unknown command: %s", cmd);

	prefix(cmd);
	return ((*cmdsw[i].fptr)(argc, argv));
}