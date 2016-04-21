/*
 * Copyright (c) 2005, 2006 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by Julio M. Merino Vidal, developed as part of Google's Summer of Code
 * 2005 program.
 * by Jason R. Thorpe of the Numerical Aerospace Simulation Facility,
 * NASA Ames Research Center, by Luke Mewburn and by Tomas Svensson.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE NETBSD FOUNDATION, INC. AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE FOUNDATION OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#include <inttypes.h>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <limits.h>

#include "meatbag.h"

/*
 * Converts the number given in 'str', which may be given in a humanized
 * form (as described in humanize_number(3), but with some limitations),
 * to an int64_t without units.
 * In case of success, 0 is returned and *size holds the value.
 * Otherwise, -1 is returned and *size is untouched.
 *
 * TODO: Internationalization, SI units.
 */

int
meat_in(const char *str, int64_t *size)
{
	char *ep, unit;
	const char *delimit;
	long multiplier;
	long long tmp, tmp2;
	size_t len;

	len = strlen(str);
	if (len == 0) {
		errno = EINVAL;
		return -1;
	}

	multiplier = 1;

	unit = str[len - 1];
	if (isalpha((unsigned char)unit)) {
		switch (tolower((unsigned char)unit)) {
		case 'b':
			multiplier = 1;
			break;

		case 'k':
			multiplier = 1024;
			break;

		case 'm':
			multiplier = 1024 * 1024;
			break;

		case 'g':
			multiplier = 1024 * 1024 * 1024;
			break;

		default:
			errno = EINVAL;
			return -1; /* Invalid suffix. */
		}

		delimit = &str[len - 1];
	} else
		delimit = NULL;

	errno = 0;
	tmp = strtoll(str, &ep, 10);
	if (str[0] == '\0' || (ep != delimit && *ep != '\0'))
		return -1; /* Not a number. */
	else if (errno == ERANGE && (tmp == LLONG_MAX || tmp == LLONG_MIN))
		return -1; /* Out of range. */

	tmp2 = tmp * multiplier;
	tmp2 = tmp2 / multiplier;
	if (tmp != tmp2) {
		errno = ERANGE;
		return -1; /* Out of range. */
	}
	tmp *= multiplier;
	*size = (int64_t)tmp;

	return 0;
}

int
meat_out(char *buf, size_t len, int64_t bytes,
    const char *suffix, int scale, int flags)
{
	const char *prefixes, *sep;
	int	b, r, s1, s2, sign;
	int64_t	divisor, max, post = 1;
	size_t	i, baselen, maxscale;

	if (flags & MB_DIVISOR_1000) {
		/* SI for decimal multiplies */
		divisor = 1000;
		if (flags & MB_B)
			prefixes = "B\0k\0M\0G\0T\0P\0E";
		else
			prefixes = "\0\0k\0M\0G\0T\0P\0E";
	} else {
		/*
		 * binary multiplies
		 * XXX IEC 60027-2 recommends Ki, Mi, Gi...
		 */
		divisor = 1024;
		if (flags & MB_B)
			prefixes = "B\0K\0M\0G\0T\0P\0E";
		else
			prefixes = "\0\0K\0M\0G\0T\0P\0E";
	}

#define	SCALE2PREFIX(scale)	(&prefixes[(scale) << 1])
	maxscale = 7;

	if ((size_t)scale >= maxscale &&
	    (scale & (MB_AUTOSCALE | MB_GETSCALE)) == 0)
		return (-1);

	if (buf == NULL || suffix == NULL)
		return (-1);

	if (len > 0)
		buf[0] = '\0';
	if (bytes < 0) {
		sign = -1;
		baselen = 3;		/* sign, digit, prefix */
		if (-bytes < INT64_MAX / 100)
			bytes *= -100;
		else {
			bytes = -bytes;
			post = 100;
			baselen += 2;
		}
	} else {
		sign = 1;
		baselen = 2;		/* digit, prefix */
		if (bytes < INT64_MAX / 100)
			bytes *= 100;
		else {
			post = 100;
			baselen += 2;
		}
	}
	if (flags & MB_NOSPACE)
		sep = "";
	else {
		sep = " ";
		baselen++;
	}
	baselen += strlen(suffix);

	/* Check if enough room for `x y' + suffix + `\0' */
	if (len < baselen + 1)
		return (-1);

	if (scale & (MB_AUTOSCALE | MB_GETSCALE)) {
		/* See if there is additional columns can be used. */
		for (max = 100, i = len - baselen; i-- > 0;)
			max *= 10;

		/*
		 * Divide the number until it fits the given column.
		 * If there will be an overflow by the rounding below,
		 * divide once more.
		 */
		for (i = 0; bytes >= max - 50 && i < maxscale; i++)
			bytes /= divisor;

		if (scale & MB_GETSCALE)
			return (int)i;
	} else
		for (i = 0; i < (size_t)scale && i < maxscale; i++)
			bytes /= divisor;
	bytes *= post;

	/* If a value <= 9.9 after rounding and ... */
	if (bytes < 995 && i > 0 && flags & MB_DECIMAL) {
		/* baselen + \0 + .N */
		if (len < baselen + 1 + 2)
			return (-1);
		b = ((int)bytes + 5) / 10;
		s1 = b / 10;
		s2 = b % 10;
		r = snprintf(buf, len, "%d%s%d%s%s%s",
		    sign * s1, ".", s2,
		    sep, SCALE2PREFIX(i), suffix);
	} else
		r = snprintf(buf, len, "%" PRId64 "%s%s%s",
		    sign * ((bytes + 50) / 100),
		    sep, SCALE2PREFIX(i), suffix);

	return (r);
}

void
meat_show(const char *prompt, uintmax_t num)
{
	char meatbag_num[5];
	if (meat_out(meatbag_num, 5, (int64_t)num ,
	    "", MB_AUTOSCALE, MB_NOSPACE|MB_B) < 0)
		meatbag_num[0] = '\0';
	printf("%s: %jub", prompt, num);
	if (meatbag_num[0] != '\0')
		printf(" (%s)", meatbag_num);
	printf("\n");
}

void
meat_print_num(int len, uintmax_t num)
{
	char meat_num[7];
	int llen;
	int64_t mbs;

	llen = (len < 7) ? len : 7;
	mbs = (int64_t)(num);
	if (meat_out(meat_num, llen, mbs, "", MB_AUTOSCALE, MB_B) >= 0)
		printf("  %*s", len, meat_num);
	else
		printf("  %*llub", len, (long long)mbs);

}
