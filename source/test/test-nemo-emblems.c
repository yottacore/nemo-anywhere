/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */

/* test-nemo-emblems.c - which link emblem a file wears.

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

/* A .lnk and a .desktop launcher wear the shortcut emblem, a symlink the
 * symlink one and never both, a plain file neither. Both emblems are our own
 * art, found through the app icon resource path, so each name has to find a
 * picture there. Which icon a .lnk itself gets is in test-nemo-lnk.c. */

#include <config.h>

#include <stdlib.h>
#include <glib/gstdio.h>
#include <gtk/gtk.h>
#ifndef G_OS_WIN32
#include <unistd.h>
#endif

#include <libnemo-private/nemo-file.h>
#include <libnemo-private/nemo-file-attributes.h>
#include <libnemo-private/nemo-global-preferences.h>
#include <libnemo-private/nemo-lnk.h>

#include "test-scratch.h"
#include "test-check.h"

static NemoFile *
loaded (const char *dir, const char *name)
{
	g_autofree char *path = g_build_filename (dir, name, NULL);
	g_autofree char *uri = g_filename_to_uri (path, NULL, NULL);
	NemoFile *file = nemo_file_get_by_uri (uri);
	int spins;

	nemo_file_monitor_add (file, file, NEMO_FILE_ATTRIBUTE_INFO);
	for (spins = 0; spins < 5000 && !nemo_file_check_if_ready (file, NEMO_FILE_ATTRIBUTE_INFO); spins++) {
		g_main_context_iteration (NULL, FALSE);
		g_usleep (2000);
	}
	check (nemo_file_check_if_ready (file, NEMO_FILE_ATTRIBUTE_INFO));

	return file;
}

static void
release (NemoFile *file)
{
	nemo_file_monitor_remove (file, file);
	nemo_file_unref (file);
}

static gboolean
wears (NemoFile *file, const char *emblem)
{
	GList *icons, *l;
	gboolean found = FALSE;

	icons = nemo_file_get_emblem_icons (file, NULL);
	for (l = icons; l != NULL; l = l->next) {
		if (G_IS_THEMED_ICON (l->data) &&
		    g_strv_contains ((const gchar * const *) g_themed_icon_get_names (l->data), emblem)) {
			found = TRUE;
		}
	}
	g_list_free_full (icons, g_object_unref);

	return found;
}

static void
test_which_emblem (const char *dir)
{
	g_autofree char *folder = g_build_filename (dir, "target", NULL);
	g_autofree char *doc = g_build_filename (dir, "plain.txt", NULL);
	g_autofree char *lnk = g_build_filename (dir, "x.lnk", NULL);
	g_autofree char *desktop = g_build_filename (dir, "x.desktop", NULL);
	NemoFile *f;

	check (g_mkdir (folder, 0755) == 0);
	check (g_file_set_contents (doc, "plain", -1, NULL));
	check (nemo_lnk_write (lnk, folder, FALSE, NULL));
	check (g_file_set_contents (desktop,
				    "[Desktop Entry]\nType=Application\nName=X\nExec=true\n", -1, NULL));

	f = loaded (dir, "x.lnk");
	check (wears (f, NEMO_FILE_EMBLEM_NAME_SHORTCUT));
	check (!wears (f, NEMO_FILE_EMBLEM_NAME_SYMBOLIC_LINK));
	release (f);

	f = loaded (dir, "x.desktop");
	check (wears (f, NEMO_FILE_EMBLEM_NAME_SHORTCUT));
	check (!wears (f, NEMO_FILE_EMBLEM_NAME_SYMBOLIC_LINK));
	release (f);

	f = loaded (dir, "plain.txt");
	check (!wears (f, NEMO_FILE_EMBLEM_NAME_SHORTCUT));
	check (!wears (f, NEMO_FILE_EMBLEM_NAME_SYMBOLIC_LINK));
	release (f);

	f = loaded (dir, "target");
	check (!wears (f, NEMO_FILE_EMBLEM_NAME_SHORTCUT));
	check (!wears (f, NEMO_FILE_EMBLEM_NAME_SYMBOLIC_LINK));
	release (f);

#ifndef G_OS_WIN32
	{
		g_autofree char *to_doc = g_build_filename (dir, "to plain", NULL);
		g_autofree char *to_folder = g_build_filename (dir, "to target", NULL);

		check (symlink (doc, to_doc) == 0);
		check (symlink (folder, to_folder) == 0);

		f = loaded (dir, "to plain");
		check (wears (f, NEMO_FILE_EMBLEM_NAME_SYMBOLIC_LINK));
		check (!wears (f, NEMO_FILE_EMBLEM_NAME_SHORTCUT));
		release (f);

		f = loaded (dir, "to target");
		check (wears (f, NEMO_FILE_EMBLEM_NAME_SYMBOLIC_LINK));
		check (!wears (f, NEMO_FILE_EMBLEM_NAME_SHORTCUT));
		release (f);
	}
#endif
}

/* The names in nemo-file.h against the art in the resource. The icon theme
   finds an emblem by name under the app icon path, so the file has to carry
   the name exactly. */
static void
test_art_is_found (void)
{
	const char *names[] = { NEMO_FILE_EMBLEM_NAME_SHORTCUT, NEMO_FILE_EMBLEM_NAME_SYMBOLIC_LINK };
	guint i;

	for (i = 0; i < G_N_ELEMENTS (names); i++) {
		g_autofree char *path = g_strdup_printf ("/org/nemo/appicons/scalable/emblems/%s.svg",
							 names[i]);

		if (!g_resources_get_info (path, G_RESOURCE_LOOKUP_FLAGS_NONE, NULL, NULL, NULL)) {
			g_printerr ("FAIL no picture for emblem %s\n", names[i]);
			failures++;
		}
	}
}

int
main (int argc, char *argv[])
{
	g_autofree char *dir = NULL;
	char *tmp;

	tmp = test_scratch_config_home ("nemo-emblems-test-XXXXXX");

	if (!gtk_init_check (&argc, &argv)) {
		g_print ("SKIP: no display\n");
		g_free (tmp);
		return 77;
	}

	nemo_global_preferences_init ();

	dir = g_build_filename (tmp, "here", NULL);
	check (g_mkdir (dir, 0755) == 0);

	test_which_emblem (dir);
	test_art_is_found ();

	g_free (tmp);

	if (failures > 0) {
		g_printerr ("%d check(s) failed\n", failures);
		return EXIT_FAILURE;
	}
	g_print ("PASS\n");
	return EXIT_SUCCESS;
}
