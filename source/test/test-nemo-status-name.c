/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */

/* test-nemo-status-name.c - how the status bar names the selected file.

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

/* In search results the file is named by its whole path, spelled with the
 * separator the user chose, since a hit can be anywhere. In a folder it is
 * the name alone, and so is anything with no local path. */

#include <config.h>

#include <stdlib.h>
#include <string.h>
#include <glib/gstdio.h>
#include <gtk/gtk.h>

#include <libnemo-private/nemo-file.h>
#include <libnemo-private/nemo-file-utilities.h>
#include <libnemo-private/nemo-global-preferences.h>

#include "test-scratch.h"
#include "test-check.h"

static void
test_local (const char *tmp)
{
	g_autofree char *dir = g_build_filename (tmp, "found here", NULL);
	g_autofree char *path = g_build_filename (dir, "notes.txt", NULL);
	g_autofree char *uri = NULL;
	g_autofree char *want = NULL;
	char *got;
	NemoFile *file;

	check (g_mkdir (dir, 0755) == 0);
	check (g_file_set_contents (path, "x", -1, NULL));
	uri = g_filename_to_uri (path, NULL, NULL);
	file = nemo_file_get_by_uri (uri);

	got = nemo_file_get_status_name (file, FALSE);
	check (g_strcmp0 (got, "notes.txt") == 0);
	g_free (got);

	want = g_strdup (path);
	nemo_path_apply_display_separator (want);
	got = nemo_file_get_status_name (file, TRUE);
	check (g_strcmp0 (got, want) == 0);
	check (got != NULL && g_str_has_suffix (got, "found here" G_DIR_SEPARATOR_S "notes.txt"));
	g_free (got);

#ifdef G_OS_WIN32
	/* The separator follows the setting, not the one the path came with. */
	nemo_config_set_string (nemo_windows_preferences, NEMO_PREFERENCES_PATH_SEPARATOR, "slash");
	got = nemo_file_get_status_name (file, TRUE);
	check (got != NULL && strchr (got, '\\') == NULL && g_str_has_suffix (got, "found here/notes.txt"));
	g_free (got);
	nemo_config_set_string (nemo_windows_preferences, NEMO_PREFERENCES_PATH_SEPARATOR, "backslash");
	got = nemo_file_get_status_name (file, TRUE);
	check (got != NULL && strchr (got, '/') == NULL);
	g_free (got);
#endif

	nemo_file_unref (file);
}

/* A remote hit has no local path to show, so it keeps its name. */
static void
test_remote (void)
{
	NemoFile *file = nemo_file_get_by_uri ("sftp://example.invalid/home/pat/report.pdf");
	char *got;

	got = nemo_file_get_status_name (file, TRUE);
	check (g_strcmp0 (got, "report.pdf") == 0);
	g_free (got);

	nemo_file_unref (file);
}

int
main (int argc, char *argv[])
{
	char *tmp;

	tmp = test_scratch_config_home ("nemo-status-name-test-XXXXXX");
	gtk_init_check (&argc, &argv);
	nemo_global_preferences_init ();

	test_local (tmp);
	test_remote ();

	g_free (tmp);

	if (failures > 0) {
		g_printerr ("%d check(s) failed\n", failures);
		return EXIT_FAILURE;
	}
	g_print ("PASS\n");
	return EXIT_SUCCESS;
}
