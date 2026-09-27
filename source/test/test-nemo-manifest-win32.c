/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */

/* test-nemo-manifest-win32.c - what the application manifest asks of Windows.

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

/* The manifest is read before any of our code runs, so the only way to see it
 * work is from inside a process that carries it. This exe links the app's own.
 *
 * - The process code page is UTF-8, so a narrow call anywhere under us takes
 *   a name outside the machine's code page.
 * - The process is per-monitor v2 DPI aware, on a desktop.
 * - The settings file lives and reloads under a folder named in German and
 *   Japanese, the case the code page is there for. */

#include <config.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <glib/gstdio.h>
#include <gtk/gtk.h>

#include <windows.h>

#include <libnemo-private/nemo-config.h>
#include <libnemo-private/nemo-global-preferences.h>

#include "test-scratch.h"
#include "test-check.h"

/* By address, since the headers only declare these for newer targets. The
   context is a handle; -4 is per-monitor v2. */
typedef HANDLE (WINAPI *GetContextFunc) (void);
typedef BOOL (WINAPI *EqualFunc) (HANDLE, HANDLE);
typedef int (WINAPI *AwarenessFunc) (HANDLE);

static void
check_dpi (void)
{
	HMODULE user32 = GetModuleHandleW (L"user32.dll");
	GetContextFunc get_context;
	EqualFunc equal;
	AwarenessFunc awareness;
	DWORD session = 0;

	get_context = (GetContextFunc) (void (*) (void)) GetProcAddress (user32, "GetThreadDpiAwarenessContext");
	equal = (EqualFunc) (void (*) (void)) GetProcAddress (user32, "AreDpiAwarenessContextsEqual");
	awareness = (AwarenessFunc) (void (*) (void)) GetProcAddress (user32, "GetAwarenessFromDpiAwarenessContext");

	if (get_context == NULL || equal == NULL || awareness == NULL) {
		g_print ("  note: no DPI awareness context before Windows 10 1607\n");
		return;
	}

	ProcessIdToSessionId (GetCurrentProcessId (), &session);
	g_print ("  session %lu, awareness %d, v2 %d\n", session, awareness (get_context ()),
		 equal (get_context (), (HANDLE) (INT_PTR) -4));

	/* Session 0, where an ssh login runs, answers per-monitor v1 with the
	   manifest and without it, so only a desktop session can tell. */
	if (session == 0) {
		g_print ("  note: session 0, DPI awareness not checked\n");
		return;
	}

	check (equal (get_context (), (HANDLE) (INT_PTR) -4));
}

static int changed;

static void
on_changed (NemoConfigGroup *group, const char *key, gpointer data)
{
	changed++;
}

static void
check_config_round_trip (const char *home)
{
	NemoConfigGroup *prefs;
	char *path, *text = NULL, *value;
	FILE *narrow;
	int spins = 0;
	const char *tool = "C:/Daten/Gr\xc3\xb6\xc3\x9f" "e/\xe8\xa8\xad\xe5\xae\x9a.exe";

	nemo_config_init ();
	prefs = nemo_config_get_group ("preferences");

	path = nemo_config_get_path ();
	g_print ("  settings: %s\n", path);
	check (g_str_has_prefix (path, home));

	nemo_config_set_string (prefs, "bulk-rename-tool", tool);
	nemo_config_flush ();

	check (g_file_get_contents (path, &text, NULL, NULL));
	check (text != NULL && strstr (text, "Gr\xc3\xb6\xc3\x9f") != NULL);
	g_free (text);

	/* The C runtime's own open, with the UTF-8 name as it is: this only
	   finds the file when the code page is UTF-8. */
	narrow = fopen (path, "rb");
	check (narrow != NULL);
	if (narrow != NULL) {
		fclose (narrow);
	}

	/* Live reload from the same folder, as an editor would save it. */
	g_signal_connect (prefs, "changed::bulk-rename-tool", G_CALLBACK (on_changed), NULL);
	check (g_file_set_contents (path, "preferences:\n\tbulk-rename-tool: C:/\xe6\x97\xa5\xe6\x9c\xac.exe\n", -1, NULL));
	while (changed == 0 && spins++ < 500) {
		g_main_context_iteration (NULL, FALSE);
		g_usleep (10000);
	}
	check (changed > 0);

	value = nemo_config_get_string (prefs, "bulk-rename-tool");
	g_print ("  reloaded: %s\n", value ? value : "(null)");
	check (g_strcmp0 (value, "C:/\xe6\x97\xa5\xe6\x9c\xac.exe") == 0);
	g_free (value);

	nemo_config_shutdown ();
	g_free (path);
}

int
main (int argc, char *argv[])
{
	char *home;
	GError *error = NULL;

	g_print ("  code page %u\n", GetACP ());
	check (GetACP () == CP_UTF8);

	/* Before the toolkit, which would set an awareness of its own if the
	   manifest had not. */
	check_dpi ();

	home = test_scratch_dir ("nemo-Gr\xc3\xb6\xc3\x9f" "e-\xe8\xa8\xad\xe5\xae\x9a-XXXXXX", &error);
	check (home != NULL);
	if (home != NULL) {
		test_scratch_point_config_at (home);
	} else {
		g_printerr ("  %s\n", error->message);
		g_clear_error (&error);
	}

	gtk_init_check (&argc, &argv);

	if (home != NULL) {
		check_config_round_trip (home);
	}
	g_free (home);

	if (failures == 0) {
		g_print ("manifest: all checks passed\n");
	}
	return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
