/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */

/* test-nemo-uri-under.c - whether one uri lies under another.

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

/* Trashing a folder takes the favorites under it along, and a location is
 * matched to the mount it sits on. Both once compared by bare prefix, so
 * trashing ".../ab" dropped the favorite ".../abc.txt", and a location could
 * be put on a sibling mount whose root merely started the same way. Both go
 * through one rule now. */

#include <config.h>

#include <stdlib.h>
#include <glib.h>

#include <libnemo-private/nemo-file-utilities.h>

#include "test-check.h"

static void
check_under (const char *uri, const char *root, gboolean want)
{
	if (nemo_uri_is_at_or_under (uri, root) != want) {
		g_printerr ("FAIL: %s %s under %s\n", uri != NULL ? uri : "(null)",
			    want ? "should be" : "should not be",
			    root != NULL ? root : "(null)");
		failures++;
	}
}

int
main (void)
{
	/* The item itself, and what is below it. */
	check_under ("file:///a/ab", "file:///a/ab", TRUE);
	check_under ("file:///a/ab/c.txt", "file:///a/ab", TRUE);
	check_under ("file:///a/ab/deep/er", "file:///a/ab", TRUE);

	/* A sibling that only starts the same way. */
	check_under ("file:///a/abc.txt", "file:///a/ab", FALSE);
	check_under ("file:///a/ab-copy/c.txt", "file:///a/ab", FALSE);
	check_under ("smb://server/share2/x", "smb://server/share", FALSE);

	/* Unrelated, and the wrong way round. */
	check_under ("file:///b/ab", "file:///a/ab", FALSE);
	check_under ("file:///a", "file:///a/ab", FALSE);

	/* A root that already ends in its separator. */
	check_under ("file:///anything", "file:///", TRUE);
	check_under ("file:///", "file:///", TRUE);
	check_under ("smb://server/share/x", "smb://server/share/", TRUE);

	check_under (NULL, "file:///", FALSE);
	check_under ("file:///a", NULL, FALSE);

	if (failures == 0) {
		g_print ("nemo-uri-under: all checks passed\n");
	}

	return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
