/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */

/* test-nemo-bookmark-info.c - a bookmark gets its name and icon on its own.

   Copyright © 2026 t00mietum (CryptogID: ปʬϝღถɔ4რఠΔթะ9ƾǝu).

   This program is free software; you can redistribute it and/or
   modify it under the terms of the GNU General Public License as
   published by the Free Software Foundation; version 2 of the
   License.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, see <http://www.gnu.org/licenses/>.
*/

/* A bookmark's name and icon come from its folder's info. Nothing else in
 * this process loads the folders here, so the bookmark has to ask. Home is
 * named "Home", and on Windows the system drive's root is named like "C:\"
 * rather than the "\" its path ends in. A folder on a share is never asked. */

#include <config.h>

#include <stdlib.h>
#include <gio/gio.h>
#include <gtk/gtk.h>

#include <libnemo-private/nemo-bookmark.h>
#include <libnemo-private/nemo-file.h>
#include <libnemo-private/nemo-file-utilities.h>
#include <libnemo-private/nemo-global-preferences.h>
#include <libnemo-private/nemo-icon-names.h>
#include <libnemo-private/nemo-share.h>

#include "test-scratch.h"
#include "test-check.h"

/* Up to 10 s for the name to read as wanted. */
static gboolean
name_becomes (NemoBookmark *bookmark, const char *want)
{
	gint64 deadline = g_get_monotonic_time () + 10 * G_USEC_PER_SEC;

	while (g_get_monotonic_time () < deadline) {
		if (g_strcmp0 (nemo_bookmark_get_name (bookmark), want) == 0) {
			return TRUE;
		}
		g_main_context_iteration (NULL, FALSE);
		g_usleep (10000);
	}

	g_printerr ("  name is '%s', wanted '%s'\n", nemo_bookmark_get_name (bookmark), want);
	return FALSE;
}

static void
check_home (void)
{
	g_autoptr (GFile) location = g_file_new_for_path (g_get_home_dir ());
	NemoBookmark *bookmark = nemo_bookmark_new (location, NULL, NULL, NULL);
	g_autofree char *icon = NULL;

	nemo_bookmark_connect (bookmark);
	check (name_becomes (bookmark, "Home"));
	icon = nemo_bookmark_get_icon_name (bookmark);
	check (g_strcmp0 (icon, NEMO_ICON_SYMBOLIC_HOME) == 0);

	g_object_unref (bookmark);
}

#ifdef G_OS_WIN32
static void
check_drive_root (void)
{
	const char *drive = g_getenv ("SystemDrive");
	g_autofree char *root = g_strdup_printf ("%s\\", drive != NULL ? drive : "C:");
	g_autoptr (GFile) location = g_file_new_for_path (root);
	g_autofree char *want = nemo_get_drive_root_name (location);
	NemoBookmark *bookmark = nemo_bookmark_new (location, NULL, NULL, NULL);

	check (want != NULL);
	nemo_bookmark_connect (bookmark);
	check (name_becomes (bookmark, want));

	g_object_unref (bookmark);
}
#endif

static void
check_share (const char *scratch)
{
	g_autofree char *nas = g_build_filename (scratch, "nas", NULL);
	g_autofree char *folder = g_build_filename (nas, "pics", NULL);
	g_autoptr (GFile) location = g_file_new_for_path (folder);
	const char *roots[] = { nas, NULL };
	NemoBookmark *bookmark;
	NemoFile *file;
	gint64 until;

	check (g_mkdir_with_parents (folder, 0700) == 0);
	nemo_share_set_roots_for_test (roots);

	bookmark = nemo_bookmark_new (location, NULL, NULL, NULL);
	nemo_bookmark_connect (bookmark);

	until = g_get_monotonic_time () + G_USEC_PER_SEC;
	while (g_get_monotonic_time () < until) {
		g_main_context_iteration (NULL, FALSE);
		g_usleep (10000);
	}

	file = nemo_file_get (location);
	check (!nemo_file_check_if_ready (file, NEMO_FILE_ATTRIBUTE_INFO));
	nemo_file_unref (file);

	g_object_unref (bookmark);
	nemo_share_set_roots_for_test (NULL);
}

int
main (int argc, char *argv[])
{
	g_autofree char *scratch = test_scratch_config_home ("nemo-bookmark-info-XXXXXX");

	if (scratch == NULL) {
		g_printerr ("could not make a scratch dir\n");
		return 77;
	}

	if (!gtk_init_check (&argc, &argv)) {
		g_print ("SKIP: no display\n");
		return 77;
	}
	nemo_global_preferences_init ();

	check_home ();
#ifdef G_OS_WIN32
	check_drive_root ();
#endif
	check_share (scratch);

	if (failures > 0) {
		g_printerr ("%d check(s) failed\n", failures);
		return EXIT_FAILURE;
	}

	g_print ("OK\n");
	return EXIT_SUCCESS;
}
