/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */

/* test-eel-rename-region.c - what a rename selects before the user types.

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

/* Starting a rename selects the name without its extension, so typing
 * replaces "photo" and keeps ".jpg". The region is in characters, since the
 * entry it is handed to counts characters, not bytes. Whether the whole name
 * is selected instead is the caller's choice (rename-selects-whole-name); this
 * only covers the part the helper decides. */

#include <config.h>

#include <stdlib.h>
#include <glib.h>

#include <eel/eel-vfs-extensions.h>

#include "test-check.h"

static void
check_region (const char *name, int want_end)
{
	int start = -1;
	int end = -1;

	eel_filename_get_rename_region (name, &start, &end);

	if (start != 0 || end != want_end) {
		g_printerr ("FAIL: %s -> %d..%d (wanted 0..%d)\n", name, start, end, want_end);
		failures++;
	}
}

int
main (int argc, char *argv[])
{
	check_region ("photo.jpg", 5);
	check_region ("noext", 5);

	/* Only the last dot counts, so a dotted name keeps its inner dots. */
	check_region ("a.b.c", 3);

	/* A compressed tarball's two parts are one extension. */
	check_region ("archive.tar.gz", 7);
	check_region ("backup.tar.xz", 6);

	/* A leading dot hides a file; it does not start an extension. */
	check_region (".bashrc", 7);
	check_region (".config.bak", 7);

	/* A trailing dot has nothing after it to keep. */
	check_region ("odd.", 4);

	/* Characters, not bytes: u-umlaut and the CJK are two and three bytes. */
	check_region ("B\xc3\xbc" "cher.pdf", 6);
	check_region ("\xe5\x86\x99\xe7\x9c\x9f.jpeg", 2);
	check_region ("caf\xc3\xa9.tar.gz", 4);

	if (failures == 0) {
		g_print ("eel-rename-region: all checks passed\n");
	}

	return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
