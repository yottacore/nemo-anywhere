/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */

/* test-nemo-launchable-win32.c - which files Windows runs as programs, for the
   Scripts menu and a double-click.

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

/* The Scripts menu lists a file only when it is launchable, and GLib called a
 * type executable only for .exe, .com and .bat, compared as written. Windows
 * runs anything PATHEXT lists, in any case. */

#include <config.h>

#include <glib.h>
#include <glib/gstdio.h>
#include <gtk/gtk.h>

#include <libnemo-private/nemo-file.h>
#include <libnemo-private/nemo-file-utilities.h>
#include <libnemo-private/nemo-global-preferences.h>

#include "test-scratch.h"
#include "test-check.h"

#define WINDOWS_PATHEXT ".COM;.EXE;.BAT;.CMD;.VBS;.VBE;.JS;.JSE;.WSF;.WSH;.MSC"

static char *dir;

static gboolean
launchable (const char *name)
{
	g_autofree char *path = g_build_filename (dir, name, NULL);
	g_autofree char *uri = g_filename_to_uri (path, NULL, NULL);
	NemoFile *file = nemo_file_get_by_uri (uri);
	gboolean answer;
	int spins;

	nemo_file_monitor_add (file, file, NEMO_FILE_ATTRIBUTE_INFO);
	for (spins = 0; spins < 5000 && !nemo_file_check_if_ready (file, NEMO_FILE_ATTRIBUTE_INFO); spins++) {
		g_main_context_iteration (NULL, FALSE);
		g_usleep (2000);
	}
	check (nemo_file_check_if_ready (file, NEMO_FILE_ATTRIBUTE_INFO));

	answer = nemo_file_is_launchable (file);
	nemo_file_monitor_remove (file, file);
	nemo_file_unref (file);

	return answer;
}

static void
make (const char *name)
{
	g_autofree char *path = g_build_filename (dir, name, NULL);

	check (g_file_set_contents (path, "@echo off\r\n", -1, NULL));
}

int
main (int argc, char *argv[])
{
	g_autofree char *scratch = NULL, *folder = NULL;
	const char *runs[] = { "plain.exe", "tidy.cmd", "TIDY.BAT", "Tool.EXE", "old.Com", "hello.vbs", "Build.JS", "snap.msc", NULL };
	const char *never[] = { "tidy.ps1", "notes.txt", "tidy", NULL };
	guint i;

	scratch = test_scratch_config_home ("nemo-launchable-XXXXXX");
	if (scratch == NULL) {
		g_printerr ("FAIL: no scratch folder\n");
		return 1;
	}

	if (!gtk_init_check (&argc, &argv)) {
		g_print ("SKIP: no display\n");
		return 77;
	}
	nemo_global_preferences_init ();

	dir = g_build_filename (scratch, "scripts", NULL);
	check (g_mkdir (dir, 0755) == 0);
	for (i = 0; runs[i] != NULL; i++) {
		make (runs[i]);
	}
	for (i = 0; never[i] != NULL; i++) {
		make (never[i]);
	}
	folder = g_build_filename (dir, "folder.exe", NULL);
	check (g_mkdir (folder, 0755) == 0);

	g_setenv ("PATHEXT", WINDOWS_PATHEXT, TRUE);
	for (i = 0; runs[i] != NULL; i++) {
		if (!launchable (runs[i])) {
			g_printerr ("not launchable: %s\n", runs[i]);
			failures++;
		}
	}
	for (i = 0; never[i] != NULL; i++) {
		if (launchable (never[i])) {
			g_printerr ("launchable: %s\n", never[i]);
			failures++;
		}
	}
	check (!launchable ("folder.exe"));

	/* The user's own list, whatever its case and spacing. */
	g_setenv ("PATHEXT", ".exe; .Ps1 ;.bat", TRUE);
	check (launchable ("tidy.ps1"));
	check (launchable ("Tool.EXE"));
	check (!launchable ("tidy.cmd"));

	/* cmd's own list when there is none. */
	g_unsetenv ("PATHEXT");
	check (launchable ("tidy.cmd"));
	check (launchable ("old.Com"));
	check (!launchable ("hello.vbs"));

	check (nemo_name_is_on_pathext ("a.cmd", ".CMD"));
	check (!nemo_name_is_on_pathext ("a.cmd", ""));
	check (!nemo_name_is_on_pathext ("a.cmd.txt", WINDOWS_PATHEXT));
	check (!nemo_name_is_on_pathext ("a.c", WINDOWS_PATHEXT));
	check (!nemo_name_is_on_pathext ("acmd", "CMD"));

	if (failures > 0) {
		g_printerr ("%d check(s) failed\n", failures);
		return EXIT_FAILURE;
	}
	g_print ("PASS\n");
	return EXIT_SUCCESS;
}
