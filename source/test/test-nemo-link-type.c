/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */

/* test-nemo-link-type.c - a .desktop link file reads as one everywhere.

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

/* Windows registers no type for .desktop, so its content type is the bare
 * extension and every "is this a link file" question said no there: no link
 * page in Properties, no Name or URL read. */

#include <config.h>

#include <stdlib.h>
#include <string.h>
#include <glib/gstdio.h>

#include <libnemo-private/nemo-file.h>
#include <libnemo-private/nemo-file-attributes.h>
#include <libnemo-private/nemo-file-utilities.h>
#include <libnemo-private/nemo-global-preferences.h>

#include "nemo-desktop-item-properties.h"

#include "test-scratch.h"
#include "test-check.h"

static NemoFile *
loaded (const char *path)
{
	g_autofree char *uri = g_filename_to_uri (path, NULL, NULL);
	NemoFile *file = nemo_file_get_by_uri (uri);
	NemoFileAttributes wanted = NEMO_FILE_ATTRIBUTE_INFO | NEMO_FILE_ATTRIBUTE_LINK_INFO;
	int spins;

	nemo_file_monitor_add (file, file, wanted);
	for (spins = 0; spins < 5000 && !nemo_file_check_if_ready (file, wanted); spins++) {
		g_main_context_iteration (NULL, FALSE);
		g_usleep (2000);
	}
	check (nemo_file_check_if_ready (file, wanted));

	return file;
}

static void
release (NemoFile *file)
{
	nemo_file_monitor_remove (file, file);
	nemo_file_unref (file);
}

static gboolean
page_shows (NemoFile *file)
{
	GList one = { file, NULL, NULL };

	return nemo_desktop_item_properties_should_show (&one);
}

static void
test_link (const char *dir)
{
	g_autofree char *target = g_build_filename (dir, "target.txt", NULL);
	g_autofree char *target_uri = g_filename_to_uri (target, NULL, NULL);
	g_autofree char *path = g_build_filename (dir, "docs.desktop", NULL);
	g_autofree char *text = NULL;
	g_autofree char *name = NULL;
	g_autofree char *activation = NULL;
	NemoFile *f;

	check (g_file_set_contents (target, "x", -1, NULL));
#ifdef G_OS_WIN32
	/* Written by hand, single backslashes. */
	text = g_strdup_printf ("[Desktop Entry]\nType=Link\nName=Docs\nURL=%s\n", target);
#else
	text = g_strdup_printf ("[Desktop Entry]\nType=Link\nName=Docs\nURL=%s\n", target_uri);
#endif
	check (g_file_set_contents (path, text, -1, NULL));

	f = loaded (path);
	check (nemo_file_is_nemo_link (f));
	check (nemo_file_is_mime_type (f, "application/x-desktop"));
	check (page_shows (f));
	check (!nemo_file_is_launcher (f));

	name = nemo_file_get_display_name (f);
	check (g_strcmp0 (name, "Docs") == 0);
	activation = nemo_file_get_activation_uri (f);
	check (g_strcmp0 (activation, target_uri) == 0);
	if (g_strcmp0 (activation, target_uri) != 0) {
		g_printerr ("  activation %s, wanted %s\n", activation, target_uri);
	}
	release (f);
}

static void
test_launcher (const char *dir)
{
	g_autofree char *path = g_build_filename (dir, "app.desktop", NULL);
	NemoFile *f;

	check (g_file_set_contents (path,
				    "[Desktop Entry]\nType=Application\nName=App\nExec=true\n", -1, NULL));

	f = loaded (path);
	check (nemo_file_is_nemo_link (f));
	check (page_shows (f));
#ifdef G_OS_UNIX
	check (nemo_file_is_launcher (f));
#else
	/* Nothing runs one there, so it must not take drops or clicks as one. */
	check (!nemo_file_is_launcher (f));
#endif
	release (f);
}

static void
test_not_links (const char *dir)
{
	g_autofree char *plain = g_build_filename (dir, "plain.txt", NULL);
	g_autofree char *theme = g_build_filename (dir, "look.theme", NULL);
	g_autofree char *empty = g_build_filename (dir, "empty.desktop", NULL);
	NemoFile *f;

	check (g_file_set_contents (plain, "plain", -1, NULL));
	check (g_file_set_contents (theme, "[Desktop Entry]\nType=X-GNOME-Metatheme\nName=Look\n", -1, NULL));
	check (g_file_set_contents (empty, "", 0, NULL));

	f = loaded (plain);
	check (!nemo_file_is_nemo_link (f));
	check (!page_shows (f));
	release (f);

	/* A kind of desktop file to the type system, never a link. */
	f = loaded (theme);
	check (!nemo_file_is_nemo_link (f));
	release (f);

	/* An empty one is typed from its name. */
	f = loaded (empty);
	check (nemo_file_is_nemo_link (f));
	release (f);
}

static void
test_type_helpers (void)
{
	check (!nemo_content_type_equals (NULL, "application/x-desktop"));
	check (!nemo_content_type_is_a (NULL, "application/x-desktop"));
#ifdef G_OS_WIN32
	check (nemo_content_type_equals (".desktop", "application/x-desktop"));
	check (nemo_content_type_equals (".DESKTOP", "application/x-desktop"));
	check (nemo_content_type_is_a (".desktop", "application/x-desktop"));
	check (!nemo_content_type_equals (".txt", "application/x-desktop"));
	check (!nemo_content_type_equals (".desktopx", "application/x-desktop"));
#else
	check (nemo_content_type_equals ("application/x-desktop", "application/x-desktop"));
	check (!nemo_content_type_equals ("application/x-theme", "application/x-desktop"));
#endif
}

int
main (void)
{
	g_autofree char *dir = NULL;
	char *tmp;

	tmp = test_scratch_config_home ("nemo-link-type-test-XXXXXX");
	nemo_global_preferences_init ();

	dir = g_build_filename (tmp, "here", NULL);
	check (g_mkdir (dir, 0755) == 0);

	test_type_helpers ();
	test_link (dir);
	test_launcher (dir);
	test_not_links (dir);

	g_free (tmp);

	if (failures > 0) {
		g_printerr ("%d check(s) failed\n", failures);
		return EXIT_FAILURE;
	}
	g_print ("PASS\n");
	return EXIT_SUCCESS;
}
