/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */

/* test-nemo-bulk-rename.c - the bulk rename setting's default.

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

/* The bulk rename setting starts out as the first known renamer that's
 * installed, or the first known one when none is. On Linux and the BSDs the
 * desktop's own toolkit picks which comes first. The default is never written
 * to the settings file, and an empty value a user sets stays empty. */

#include <config.h>

#include <string.h>
#include <glib/gstdio.h>

#include <libnemo-private/nemo-bulk-rename.h>
#include <libnemo-private/nemo-config.h>
#include <libnemo-private/nemo-global-preferences.h>

#include "test-scratch.h"
#include "test-check.h"

static void
expect (const char *what, char *got, const char *want)
{
	if (g_strcmp0 (got, want) != 0) {
		g_printerr ("FAIL %s: got \"%s\", wanted \"%s\"\n", what, got, want);
		failures++;
	}
	g_free (got);
}

static void
make_file (const char *dir, const char *name, gboolean runs)
{
	char *path = g_build_filename (dir, name, NULL);
	char *parent = g_path_get_dirname (path);

	g_mkdir_with_parents (parent, 0700);
	check (g_file_set_contents (path, "#!/bin/sh\n", -1, NULL));
	check (g_chmod (path, runs ? 0755 : 0644) == 0);
	g_free (parent);
	g_free (path);
}

#ifdef G_OS_WIN32
#define FIRST  "ReNamer\\ReNamer.exe"
#define SECOND "Ant Renamer\\Renamer.exe"

static void
test_platform (const char *root)
{
	char *programs = g_build_filename (root, "Program Files", NULL);
	char *first = g_strdup_printf ("\"%s\\" FIRST "\"", programs);
	char *second = g_strdup_printf ("\"%s\\" SECOND "\" -af", programs);
	char *both = g_strdup_printf ("%s\n%s", first, second);

	g_mkdir_with_parents (programs, 0700);
	g_setenv ("ProgramFiles", programs, TRUE);

	expect ("nothing installed", nemo_bulk_rename_find_default (), first);
	expect ("the list", nemo_bulk_rename_known_commands (), both);

	make_file (programs, SECOND, TRUE);
	expect ("only the second installed", nemo_bulk_rename_find_default (), second);

	make_file (programs, FIRST, TRUE);
	expect ("both installed", nemo_bulk_rename_find_default (), first);

	/* A KDE setting means nothing here. */
	g_setenv ("XDG_CURRENT_DESKTOP", "KDE", TRUE);
	expect ("KDE named on Windows", nemo_bulk_rename_find_default (), first);
	g_unsetenv ("XDG_CURRENT_DESKTOP");

	g_free (both);
	g_free (second);
	g_free (first);
	g_free (programs);
}
#elif defined (__APPLE__)
#define FIRST "NameChanger"
#define LAST  "A Better Finder Rename 12"
#define LIST  "open -a " FIRST "\nopen -a Transnomino\nopen -a Renamer\nopen -a \"" LAST "\""

/* /Applications is the machine's, so a check that depends on what isn't in
 * it runs only when none of these are there. */
static gboolean
any_in_system_apps (int upto)
{
	static const char *const names[] = { FIRST, "Transnomino", "Renamer", LAST };
	gboolean found = FALSE;
	int i;

	for (i = 0; i < upto && !found; i++) {
		char *app = g_strconcat ("/Applications/", names[i], ".app", NULL);

		found = g_file_test (app, G_FILE_TEST_IS_DIR);
		g_free (app);
	}
	return found;
}

static void
make_app (const char *apps, const char *name, gboolean bundle)
{
	char *app = g_strconcat (name, ".app", NULL);
	char *path = g_build_filename (apps, app, NULL);

	if (bundle) {
		check (g_mkdir_with_parents (path, 0700) == 0);
	} else {
		make_file (apps, app, FALSE);
	}
	g_free (path);
	g_free (app);
}

/* Apps, opened with open -a, from /Applications or the user's own. HOME is
 * the scratch root here. */
static void
test_platform (const char *root)
{
	char *apps = g_build_filename (root, "Applications", NULL);
	char *first;

	g_mkdir_with_parents (apps, 0700);
	g_unsetenv ("XDG_CURRENT_DESKTOP");

	expect ("the list", nemo_bulk_rename_known_commands (), LIST);

	if (!any_in_system_apps (4)) {
		expect ("nothing installed", nemo_bulk_rename_find_default (), "open -a " FIRST);
	}

	make_app (apps, LAST, TRUE);
	if (!any_in_system_apps (3)) {
		expect ("only the last installed", nemo_bulk_rename_find_default (),
			"open -a \"" LAST "\"");
	}

	/* A file with the name is not an app. */
	make_app (apps, FIRST, FALSE);
	if (!any_in_system_apps (3)) {
		expect ("a file named like the first", nemo_bulk_rename_find_default (),
			"open -a \"" LAST "\"");
	}

	first = g_build_filename (apps, FIRST ".app", NULL);
	check (g_remove (first) == 0);
	make_app (apps, FIRST, TRUE);
	expect ("first installed", nemo_bulk_rename_find_default (), "open -a " FIRST);

	/* A KDE setting means nothing here. */
	g_setenv ("XDG_CURRENT_DESKTOP", "KDE", TRUE);
	expect ("KDE named on macOS", nemo_bulk_rename_find_default (), "open -a " FIRST);
	expect ("the list, KDE", nemo_bulk_rename_known_commands (), LIST);
	g_unsetenv ("XDG_CURRENT_DESKTOP");

	g_free (first);
	g_free (apps);
}
#else
#define GTK_ONE "thunar --bulk-rename"
#define QT_ONE  "krename"

static void
test_platform (const char *root)
{
	char *bin = g_build_filename (root, "bin", NULL);
	char *thunar;

	g_mkdir_with_parents (bin, 0700);
	g_setenv ("PATH", bin, TRUE);
	g_unsetenv ("XDG_CURRENT_DESKTOP");

	expect ("nothing installed, no desktop", nemo_bulk_rename_find_default (), GTK_ONE);
	expect ("the list, no desktop", nemo_bulk_rename_known_commands (), GTK_ONE "\n" QT_ONE);

	g_setenv ("XDG_CURRENT_DESKTOP", "KDE", TRUE);
	expect ("nothing installed, KDE", nemo_bulk_rename_find_default (), QT_ONE);
	expect ("the list, KDE", nemo_bulk_rename_known_commands (), QT_ONE "\n" GTK_ONE);

	/* Not run, so not there. */
	make_file (bin, "krename", FALSE);
	g_setenv ("XDG_CURRENT_DESKTOP", "XFCE", TRUE);
	expect ("krename not runnable, Xfce", nemo_bulk_rename_find_default (), GTK_ONE);

	make_file (bin, "krename", TRUE);
	expect ("only krename installed, Xfce", nemo_bulk_rename_find_default (), QT_ONE);

	make_file (bin, "thunar", TRUE);
	expect ("both installed, Xfce", nemo_bulk_rename_find_default (), GTK_ONE);

	g_setenv ("XDG_CURRENT_DESKTOP", "ubuntu:GNOME", TRUE);
	expect ("both installed, GNOME", nemo_bulk_rename_find_default (), GTK_ONE);

	g_setenv ("XDG_CURRENT_DESKTOP", "X-Generic:lxqt", TRUE);
	expect ("both installed, LXQt second in the list", nemo_bulk_rename_find_default (), QT_ONE);
	expect ("the list, LXQt", nemo_bulk_rename_known_commands (), QT_ONE "\n" GTK_ONE);

	/* Back to one renamer, for the settings part. */
	g_setenv ("XDG_CURRENT_DESKTOP", "XFCE", TRUE);
	thunar = g_build_filename (bin, "thunar", NULL);
	check (g_remove (thunar) == 0);

	g_free (thunar);
	g_free (bin);
}
#endif

/* Lines of the settings file that aren't comments and name the key. */
static gboolean
stored (void)
{
	char *path = nemo_config_get_path ();
	char *text = NULL;
	char **lines;
	gboolean found = FALSE;
	int i;

	nemo_config_flush ();
	if (!g_file_get_contents (path, &text, NULL, NULL)) {
		g_free (path);
		return FALSE;
	}
	lines = g_strsplit (text, "\n", -1);
	for (i = 0; lines[i] != NULL && !found; i++) {
		const char *line = lines[i] + strspn (lines[i], " \t");

		found = line[0] != '#' && strstr (line, "bulk-rename-tool") != NULL;
	}
	g_strfreev (lines);
	g_free (text);
	g_free (path);
	return found;
}

static void
test_setting (void)
{
	NemoConfigGroup *prefs;
	char *found = nemo_bulk_rename_find_default ();

	nemo_config_init ();
	prefs = nemo_config_get_group ("preferences");

	expect ("unset reads the found renamer",
		nemo_config_get_string (prefs, NEMO_PREFERENCES_BULK_RENAME_TOOL), found);
	check (!nemo_config_is_set (prefs, NEMO_PREFERENCES_BULK_RENAME_TOOL));

	/* A save with other changes leaves it out. */
	nemo_config_set_boolean (prefs, NEMO_PREFERENCES_SHOW_HIDDEN_FILES, TRUE);
	check (!stored ());

	/* As the settings window writes it back when it shows the field. */
	nemo_config_set_string (prefs, NEMO_PREFERENCES_BULK_RENAME_TOOL, found);
	check (!nemo_config_is_set (prefs, NEMO_PREFERENCES_BULK_RENAME_TOOL));
	check (!stored ());

	nemo_config_set_string (prefs, NEMO_PREFERENCES_BULK_RENAME_TOOL, "");
	check (nemo_config_is_set (prefs, NEMO_PREFERENCES_BULK_RENAME_TOOL));
	check (stored ());
	expect ("emptied stays empty", nemo_config_get_string (prefs, NEMO_PREFERENCES_BULK_RENAME_TOOL), "");

	nemo_config_set_string (prefs, NEMO_PREFERENCES_BULK_RENAME_TOOL, "C:\\Tools\\rn.exe /x");
	check (stored ());
	expect ("a path as written", nemo_config_get_string (prefs, NEMO_PREFERENCES_BULK_RENAME_TOOL),
		"C:\\Tools\\rn.exe /x");

	nemo_config_reset (prefs, NEMO_PREFERENCES_BULK_RENAME_TOOL);
	check (!stored ());
	expect ("reset reads the found renamer again",
		nemo_config_get_string (prefs, NEMO_PREFERENCES_BULK_RENAME_TOOL), found);

	nemo_config_reset (prefs, NEMO_PREFERENCES_SHOW_HIDDEN_FILES);
	nemo_config_shutdown ();
	g_free (found);
}

int
main (void)
{
	char *root = test_scratch_config_home ("nemo-bulk-rename-XXXXXX");

	g_log_set_always_fatal (G_LOG_LEVEL_CRITICAL);

	test_platform (root);
	/* The store finds its default once, so this goes after the setup. */
	test_setting ();

	g_free (root);

	if (failures > 0) {
		g_printerr ("%d failure(s)\n", failures);
		return 1;
	}
	g_print ("OK\n");
	return 0;
}
