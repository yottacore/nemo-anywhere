/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */

/* fuzz-main.c - replays seed files through a fuzz target.

   Copyright © 2026 t00mietum (CryptogID: ปʬϝღถɔ4რఠΔթะ9ƾǝu).

   This program is free software; you can redistribute it and/or
   modify it under the terms of the GNU General Public License as
   published by the Free Software Foundation; version 2 of the
   License.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   General Public License for more details.

   You should have received a copy of the GNU General Public
   License along with this program; if not, write to the
   Free Software Foundation, Inc., 51 Franklin Street, Suite 500,
   Boston, MA 02110-1335, USA.
*/

/* libFuzzer supplies its own main, so this file is only compiled into the
 * ordinary build. Without it the targets would compile and never run, and the
 * seed corpus would drift out of meaning with nothing to say so. Replaying the
 * seeds on every suite run checks both, and costs milliseconds.
 *
 * Plain stdio on purpose: the shcl target links nothing else, and there is no
 * reason for the driver to be the thing that drags glib into it. */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

int LLVMFuzzerTestOneInput (const uint8_t *data, size_t size);

static int
run_file (const char *path)
{
	FILE *f;
	long len = 0;
	size_t got;
	uint8_t *body;

	f = fopen (path, "rb");

	if (f == NULL) {
		fprintf (stderr, "cannot open %s\n", path);
		return -1;
	}

	if (fseek (f, 0, SEEK_END) != 0 || (len = ftell (f)) < 0) {
		fprintf (stderr, "cannot measure %s\n", path);
		fclose (f);
		return -1;
	}

	/* Not rewind, which can't say it failed. */
	if (fseek (f, 0, SEEK_SET) != 0) {
		fprintf (stderr, "cannot read %s\n", path);
		fclose (f);
		return -1;
	}

	/* An empty seed is a fair thing to hand a parser, and malloc(0) may
	   return NULL, which isn't the failure it looks like. */
	body = malloc ((size_t) len + 1);

	if (body == NULL) {
		fprintf (stderr, "out of memory reading %s\n", path);
		fclose (f);
		return -1;
	}

	got = fread (body, 1, (size_t) len, f);
	fclose (f);

	if (got != (size_t) len) {
		fprintf (stderr, "short read on %s\n", path);
		free (body);
		return -1;
	}

	LLVMFuzzerTestOneInput (body, got);
	free (body);

	return 0;
}

int
main (int argc, char **argv)
{
	int i;

	/* No corpus is a test that couldn't run rather than one that passed. */
	if (argc < 2) {
		fprintf (stderr, "no corpus files given\n");
		return 77;
	}

	for (i = 1; i < argc; i++) {
		if (run_file (argv[i]) != 0) {
			return 1;
		}
	}

	return 0;
}
