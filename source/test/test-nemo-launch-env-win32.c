/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */

/* test-nemo-launch-env-win32.c - a program the user starts gets their
   environment, not the settings the app made for itself.

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

/* The app turns the session bus off for itself, and sets 2 more things only it
 * needs. test-env-dump, the argument, writes down what it was started with.
 * It is started each way the app starts a user's program, and once as a
 * helper, which keeps the app's settings. The routes through Explorer or the
 * management service hand it their own environment, so for those only the 2
 * settings nobody else makes are checked. That environment has none of the
 * build's libraries on PATH, which is why the dumper needs none. */

#include <config.h>

#include <stdio.h>
#include <string.h>

#include <glib.h>
#include <glib/gstdio.h>

#include <libnemo-private/nemo-dnd-win32.h>
#include <libnemo-private/nemo-file-utilities.h>
#include <libnemo-private/nemo-launch-win32.h>

#include "test-scratch.h"
#include "test-check.h"

#define WAIT_SECONDS 30
#define USERS_BUS "nemo-test:the-users-own"
#define PASSED_ON "NEMO_TEST_LAUNCH_PASSED_ON"

/* What the dumper wrote, as NAME=VALUE lines; NULL if it never did.
 * Returns: (transfer full): free with g_strfreev */
static char **
wait_for_dump (const char *path)
{
	gint64 until = g_get_monotonic_time () + WAIT_SECONDS * G_USEC_PER_SEC;
	char *text = NULL;
	char **lines;

	while (!g_file_get_contents (path, &text, NULL, NULL)) {
		if (g_get_monotonic_time () > until) {
			return NULL;
		}
		g_usleep (50 * 1000);
	}
	lines = g_strsplit (text, "\n", -1);
	g_free (text);
	return lines;
}

static const char *
seen (char **lines, const char *name)
{
	return g_environ_getenv (lines, name);
}

/* None of the app's own settings reached it. */
static void
check_not_ours (const char *how, char **lines, gboolean in_process)
{
	const char *bus;

	if (lines == NULL) {
		g_printerr ("FAIL: %s: the program never wrote what it saw\n", how);
		failures++;
		return;
	}

	bus = seen (lines, "DBUS_SESSION_BUS_ADDRESS");
	g_print ("%s: bus %s\n", how, bus != NULL ? bus : "(unset)");
	check (seen (lines, "FREETYPE_PROPERTIES") == NULL);
	check (seen (lines, "GDK_WIN32_USE_EXPERIMENTAL_OLE2_DND") == NULL);
	/* Explorer's own may say disabled: too, as the suite's does under wine. */
	if (in_process) {
		/* Started from here, so it has the rest of ours as it is. */
		check (g_strcmp0 (bus, USERS_BUS) == 0);
		check (g_strcmp0 (seen (lines, PASSED_ON), "yes") == 0);
	}
}

static void
check_ours_still (void)
{
	check (g_strcmp0 (g_getenv ("DBUS_SESSION_BUS_ADDRESS"), "disabled:") == 0);
	check (g_getenv ("FREETYPE_PROPERTIES") != NULL);
}

static char *
dump_path (const char *dir, const char *name)
{
	return g_build_filename (dir, name, NULL);
}

int
main (int argc, char *argv[])
{
	g_autofree char *scratch = NULL;
	g_autofree char *dumper = NULL;
	char **lines, **env;
	GError *error = NULL;

	if (argc != 2) {
		g_printerr ("usage: %s <test-env-dump.exe>\n", argv[0]);
		return 77;
	}

	scratch = test_scratch_dir ("nemo-launch-env-XXXXXX", NULL);
	dumper = g_canonicalize_filename (argv[1], NULL);
	if (scratch == NULL) {
		g_printerr ("FAIL: no scratch folder\n");
		return 1;
	}

	/* As the user had it before the app started. */
	g_setenv ("DBUS_SESSION_BUS_ADDRESS", USERS_BUS, TRUE);
	g_unsetenv ("FREETYPE_PROPERTIES");
	g_unsetenv ("GDK_WIN32_USE_EXPERIMENTAL_OLE2_DND");
	g_unsetenv ("NEMO_NO_SHELL_DRAG");

	/* As nemo-main.c does at startup. */
	nemo_setup_runtime_environment ();
	nemo_setenv_own ("FREETYPE_PROPERTIES", "truetype:interpreter-version=35", FALSE);
	nemo_dnd_win32_prepare ();
	check_ours_still ();
	check (g_strcmp0 (g_getenv ("GDK_WIN32_USE_EXPERIMENTAL_OLE2_DND"), "1") == 0);
	g_setenv (PASSED_ON, "yes", TRUE);

	env = nemo_get_user_environ ();
	check (g_strcmp0 (g_environ_getenv (env, "DBUS_SESSION_BUS_ADDRESS"), USERS_BUS) == 0);
	check (g_environ_getenv (env, "FREETYPE_PROPERTIES") == NULL);
	check (g_environ_getenv (env, "GDK_WIN32_USE_EXPERIMENTAL_OLE2_DND") == NULL);
	check (g_strcmp0 (g_environ_getenv (env, PASSED_ON), "yes") == 0);
	g_strfreev (env);

	/* Nested, as a launch inside a launch would be. */
	nemo_launch_win32_user_environ_enter ();
	nemo_launch_win32_user_environ_enter ();
	check (g_strcmp0 (g_getenv ("DBUS_SESSION_BUS_ADDRESS"), USERS_BUS) == 0);
	nemo_launch_win32_user_environ_leave ();
	check (g_strcmp0 (g_getenv ("DBUS_SESSION_BUS_ADDRESS"), USERS_BUS) == 0);
	nemo_launch_win32_user_environ_leave ();
	check_ours_still ();

	/* A console program of the user's, as an action starts one. */
	{
		g_autofree char *out = dump_path (scratch, "spawn.txt");
		const char *run[] = { dumper, out, NULL };

		check (nemo_launch_win32_spawn (run, FALSE, &error));
		g_clear_error (&error);
		lines = wait_for_dump (out);
		check_not_ours ("spawn", lines, TRUE);
		g_strfreev (lines);
		check_ours_still ();
	}

	/* A named program, as Open With and the terminal do. */
	{
		g_autofree char *out = dump_path (scratch, "run.txt");
		g_autofree char *args = g_strdup_printf ("\"%s\"", out);

		check (nemo_launch_win32_run (dumper, args, scratch, &error));
		g_clear_error (&error);
		lines = wait_for_dump (out);
		check_not_ours ("run", lines, FALSE);
		g_strfreev (lines);
		check_ours_still ();
	}

	/* A file opened with whatever its type opens with. */
	{
		g_autofree char *out = dump_path (scratch, "open.txt");
		g_autofree char *bat = dump_path (scratch, "dump.bat");
		g_autofree char *body = g_strdup_printf ("@\"%s\" \"%s\"\r\n", dumper, out);

		check (g_file_set_contents (bat, body, -1, NULL));
		check (nemo_launch_win32_open_path (bat, scratch, &error));
		g_clear_error (&error);
		lines = wait_for_dump (out);
		check_not_ours ("open", lines, FALSE);
		g_strfreev (lines);
		check_ours_still ();
	}

	/* A new copy of ours, the way nemo-new-process.c starts one. */
	{
		g_autofree char *out = dump_path (scratch, "copy.txt");
		const char *run[] = { dumper, out, NULL };

		check (nemo_launch_win32_new_copy (run, &error));
		g_clear_error (&error);
		lines = wait_for_dump (out);
		check_not_ours ("copy", lines, TRUE);
		g_strfreev (lines);
		check_ours_still ();
	}

	/* A helper keeps the app's settings. */
	{
		g_autofree char *out = dump_path (scratch, "helper.txt");
		const char *run[] = { dumper, out, NULL };

		check (nemo_launch_win32_pipe (run, NULL, WAIT_SECONDS, NULL, NULL, NULL));
		lines = wait_for_dump (out);
		check (lines != NULL);
		if (lines != NULL) {
			check (g_strcmp0 (seen (lines, "DBUS_SESSION_BUS_ADDRESS"), "disabled:") == 0);
			check (g_strcmp0 (seen (lines, "GDK_WIN32_USE_EXPERIMENTAL_OLE2_DND"), "1") == 0);
		}
		g_strfreev (lines);
	}

	if (failures == 0) {
		g_print ("launch environment: all checks passed\n");
	}
	return failures == 0 ? 0 : 1;
}
