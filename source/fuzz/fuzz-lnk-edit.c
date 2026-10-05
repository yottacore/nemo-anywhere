/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */

/* fuzz-lnk-edit.c - new paths put in a Windows shortcut of arbitrary bytes.

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

/* Edit link walks the old file with code of its own, apart from the reader,
 * to copy what it keeps. nemo_lnk_set_paths only takes a path, so the bytes
 * go to a file first.
 *
 * Not crashing is not enough here, since a writer that slips by a few bytes
 * leaves a shortcut that reads as something else. So every edit that works
 * has to read back with the new paths and the old strings, and every one
 * that fails has to leave the file as it was. The item ID list going is by
 * design and is not checked. */

#include <config.h>

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <glib.h>

#include <libnemo-private/nemo-lnk.h>

#include "test/test-scratch.h"

int LLVMFuzzerTestOneInput (const uint8_t *data, size_t size);

typedef struct {
	const char *absolute;
	const char *relative;
	const char *portable;
} Edit;

/* The same drive as the seeds, so the serial is kept; a share; and no
   absolute path at all. */
static const Edit edits[] = {
	{ "C:\\new\\file.txt", "..\\new\\file.txt", "%USERPROFILE%\\new.txt" },
	{ "\\\\srv\\share\\dir\\f.txt", NULL, NULL },
	{ NULL, ".\\f.txt", NULL },
};

static char *lnk_path;

static void
fail (const char *what, const Edit *edit)
{
	fprintf (stderr, "edit %s | %s | %s: %s\n",
		 edit->absolute ? edit->absolute : "-",
		 edit->relative ? edit->relative : "-",
		 edit->portable ? edit->portable : "-", what);
	abort ();
}

static void
check_kept (const char *name, const char *before, const char *after, const Edit *edit)
{
	if (g_strcmp0 (before, after) != 0) {
		char *what = g_strdup_printf ("%s was \"%s\", now \"%s\"", name,
					      before ? before : "(none)", after ? after : "(none)");

		fail (what, edit);
	}
}

static void
check_edit (const NemoLnk *old, const Edit *edit)
{
	NemoLnk now;

	if (!nemo_lnk_read (lnk_path, &now)) {
		fail ("the edited file does not read as a shortcut", edit);
	}

	if (edit->absolute != NULL && edit->absolute[0] == '\\') {
		if (g_strcmp0 (now.net_share, "\\\\srv\\share") != 0 ||
		    g_strcmp0 (now.net_path, "dir\\f.txt") != 0 || now.local_path != NULL) {
			fail ("share path not read back", edit);
		}
	} else if (g_strcmp0 (now.local_path, edit->absolute) != 0 || now.net_share != NULL) {
		fail ("absolute path not read back", edit);
	}
	if (g_strcmp0 (now.relative_path, edit->relative) != 0) {
		fail ("relative path not read back", edit);
	}
	if (g_strcmp0 (now.env_path, edit->portable) != 0) {
		fail ("portable path not read back", edit);
	}

	if (now.attributes != old->attributes) {
		fail ("attributes changed", edit);
	}
	check_kept ("description", old->description, now.description, edit);
	check_kept ("Start in", old->working_dir, now.working_dir, edit);
	check_kept ("arguments", old->arguments, now.arguments, edit);
	check_kept ("icon", old->icon_location, now.icon_location, edit);

	nemo_lnk_clear (&now);
}

int
LLVMFuzzerTestOneInput (const uint8_t *data, size_t size)
{
	NemoLnk old;
	gboolean is_lnk;
	guint i;

	if (lnk_path == NULL) {
		char *dir = test_scratch_dir ("fuzz-lnk-edit-XXXXXX", NULL);

		if (dir == NULL) {
			fprintf (stderr, "no scratch directory\n");
			abort ();
		}
		lnk_path = g_build_filename (dir, "x.lnk", NULL);
		g_free (dir);
	}

	is_lnk = nemo_lnk_parse (data, size, &old);

	for (i = 0; i < G_N_ELEMENTS (edits); i++) {
		char *after = NULL;
		gsize after_size = 0;

		if (!g_file_set_contents_full (lnk_path, (const char *) data, size,
					       G_FILE_SET_CONTENTS_NONE, 0600, NULL)) {
			fprintf (stderr, "could not write %s\n", lnk_path);
			abort ();
		}

		if (nemo_lnk_set_paths (lnk_path, edits[i].absolute, edits[i].relative,
					edits[i].portable, NULL)) {
			if (!is_lnk) {
				fail ("edited a file the reader does not take", &edits[i]);
			}
			check_edit (&old, &edits[i]);
			continue;
		}

		if (!g_file_get_contents (lnk_path, &after, &after_size, NULL) ||
		    after_size != size || (size > 0 && memcmp (after, data, size) != 0)) {
			fail ("a refused edit changed the file", &edits[i]);
		}
		g_free (after);
	}

	if (is_lnk) {
		nemo_lnk_clear (&old);
	}

	return 0;
}
