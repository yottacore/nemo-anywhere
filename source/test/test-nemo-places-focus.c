/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */

/* test-nemo-places-focus.c - "Places" never keeps the keyboard.

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

/* A click on a place left the keyboard in "Places", so arrow keys and typing
 * went there instead of to the folder it opened. The built program opens one
 * folder with a bookmark to another, and test-nemo-places-focus-probe,
 * preloaded, clicks that bookmark, clicks it again, and renames it. After a
 * click the focus is in the folder with nothing selected, Places never takes
 * it except while a rename is under way, and the end of a rename hands it back.
 *
 * Needs the built program (argv[1]), the probe (argv[2]) and a display. Linux
 * only, since it works by LD_PRELOAD. */

#include <config.h>

#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

#include <gio/gio.h>
#include <glib/gstdio.h>

#include "test-scratch.h"
#include "test-check.h"

static gboolean
alive (GPid pid)
{
	int status;

	return pid > 0 && waitpid (pid, &status, WNOHANG) == 0;
}

static void
stop (GPid pid)
{
	int i;

	if (pid <= 0) {
		return;
	}
	if (alive (pid)) {
		kill (pid, SIGTERM);
		for (i = 0; i < 50 && alive (pid); i++) {
			g_usleep (100 * 1000);
		}
		if (alive (pid)) {
			kill (pid, SIGKILL);
			waitpid (pid, NULL, 0);
		}
	}
	g_spawn_close_pid (pid);
}

static void
make_folder (const char *root, const char *name)
{
	char *folder = g_build_filename (root, name, NULL);
	const char *files[] = { "apple.txt", "banana.txt", "cherry.txt" };
	guint i;

	g_mkdir_with_parents (folder, 0700);
	for (i = 0; i < G_N_ELEMENTS (files); i++) {
		char *path = g_build_filename (folder, files[i], NULL);

		check (g_file_set_contents (path, files[i], -1, NULL));
		g_free (path);
	}
	g_free (folder);
}

int
main (int argc, char *argv[])
{
#ifndef __linux__
	g_print ("SKIP: drives the program through LD_PRELOAD, which is Linux only here\n");
	return 77;
#else
	char *root, *first, *second, *second_uri, *gtk_config, *bookmarks, *line, *out, *path;
	char *text = NULL;
	char **envp, **lines;
	char *spawn_argv[4];
	GError *error = NULL;
	GPid pid = 0;
	gboolean done = FALSE;
	int i, results = 0;

	if (argc < 3) {
		g_printerr ("usage: %s <nemo-anywhere> <probe module>\n", argv[0]);
		return 77;
	}
	if (g_getenv ("DISPLAY") == NULL) {
		g_print ("SKIP: no display\n");
		return 77;
	}

	root = test_scratch_dir ("nemo-places-focus-XXXXXX", NULL);
	make_folder (root, "first");
	make_folder (root, "second");
	first = g_build_filename (root, "first", NULL);
	second = g_build_filename (root, "second", NULL);
	second_uri = g_filename_to_uri (second, NULL, NULL);

	gtk_config = g_build_filename (root, "config", "gtk-3.0", NULL);
	g_mkdir_with_parents (gtk_config, 0700);
	bookmarks = g_build_filename (gtk_config, "bookmarks", NULL);
	line = g_strdup_printf ("%s Second\n", second_uri);
	check (g_file_set_contents (bookmarks, line, -1, NULL));
	out = g_build_filename (root, "focus.txt", NULL);

	envp = g_get_environ ();
	envp = g_environ_setenv (envp, "HOME", root, TRUE);
	path = g_build_filename (root, "config", NULL);
	envp = g_environ_setenv (envp, "XDG_CONFIG_HOME", path, TRUE);
	g_free (path);
	path = g_build_filename (root, "data", NULL);
	envp = g_environ_setenv (envp, "XDG_DATA_HOME", path, TRUE);
	g_free (path);
	path = g_build_filename (root, "cache", NULL);
	envp = g_environ_setenv (envp, "XDG_CACHE_HOME", path, TRUE);
	g_free (path);
	envp = g_environ_setenv (envp, "DBUS_SESSION_BUS_ADDRESS", "disabled:", TRUE);
	envp = g_environ_setenv (envp, "LD_PRELOAD", argv[2], TRUE);
	envp = g_environ_setenv (envp, "NEMO_FOCUS_OUT", out, TRUE);
	envp = g_environ_setenv (envp, "NEMO_FOCUS_PLACE", second_uri, TRUE);
	envp = g_environ_setenv (envp, "NEMO_FOCUS_FIRST", "first", TRUE);
	envp = g_environ_setenv (envp, "NEMO_FOCUS_SECOND", "second", TRUE);

	spawn_argv[0] = argv[1];
	spawn_argv[1] = (char *) "--geometry=800x600";
	spawn_argv[2] = first;
	spawn_argv[3] = NULL;
	if (!g_spawn_async (NULL, spawn_argv, envp, G_SPAWN_DO_NOT_REAP_CHILD, NULL, NULL,
			    &pid, &error)) {
		g_printerr ("spawn: %s\n", error->message);
		g_clear_error (&error);
		failures++;
		goto out;
	}

	/* The probe gives each step ten seconds, and there are six. */
	for (i = 0; i < 900 && alive (pid) && !done; i++) {
		g_usleep (100 * 1000);
		g_free (text);
		text = NULL;
		done = g_file_get_contents (out, &text, NULL, NULL) && strstr (text, "done\n") != NULL;
	}
	if (!done) {
		g_printerr ("FAIL: the probe never finished (%s)\n",
			    alive (pid) ? "still running" : "the program exited");
		failures++;
	}

	if (text != NULL) {
		lines = g_strsplit (text, "\n", -1);
		for (i = 0; lines[i] != NULL; i++) {
			if (g_str_has_prefix (lines[i], "FAIL ")) {
				g_printerr ("%s\n", lines[i]);
				failures++;
				results++;
			} else if (g_str_has_prefix (lines[i], "ok ")) {
				g_print ("%s\n", lines[i]);
				results++;
			}
		}
		g_strfreev (lines);
	}
	/* Eleven checks; fewer means a step never ran. */
	if (done && results != 11) {
		g_printerr ("FAIL: %d results, expected 11\n", results);
		failures++;
	}

out:
	stop (pid);
	g_strfreev (envp);
	g_free (text);
	g_free (out);
	g_free (line);
	g_free (bookmarks);
	g_free (gtk_config);
	g_free (second_uri);
	g_free (second);
	g_free (first);
	g_free (root);

	if (failures > 0) {
		g_printerr ("%d failure(s)\n", failures);
		return EXIT_FAILURE;
	}

	g_print ("OK\n");
	return EXIT_SUCCESS;
#endif
}
