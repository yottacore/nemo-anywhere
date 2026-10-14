/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */

/* test-nemo-search-escape.c - Escape in find mode.

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

/* Escape in find mode went back to the folder only from the search box.
 * With the results in front it cleared the selection and nothing else, so
 * find mode had no way out from the keyboard there. The built program runs
 * in the list view and then the icon view, with test-nemo-search-escape-probe
 * preloaded to search and press the keys.
 *
 * Needs the built program (argv[1]), the probe (argv[2]) and a display. Runs
 * on an X server of its own. Linux and FreeBSD only, since it works by
 * LD_PRELOAD. */

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

#define RESULTS 10

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
make_files (const char *folder)
{
	static const char *const names[] = { "note one.txt", "note two.txt", "other.txt" };
	char *path;
	guint i;

	for (i = 0; i < G_N_ELEMENTS (names); i++) {
		path = g_build_filename (folder, names[i], NULL);
		check (g_file_set_contents (path, names[i], -1, NULL));
		g_free (path);
	}
	path = g_build_filename (folder, "photos", NULL);
	check (g_mkdir (path, 0700) == 0);
	g_free (path);
}

/* One run of the program in one view. */
static void
run_view (const char *nemo, const char *probe, const char *root, const char *viewer)
{
	char *home, *folder, *config, *settings, *out, *path, *text = NULL, *line;
	char *spawn_argv[4];
	char **envp, **lines;
	GError *error = NULL;
	GPid pid = 0;
	gboolean done = FALSE;
	int i, results = 0;

	g_print ("%s:\n", viewer);
	home = g_build_filename (root, viewer, NULL);
	folder = g_build_filename (home, "notes", NULL);
	config = g_build_filename (home, "config", "nemo-anywhere", NULL);
	settings = g_build_filename (config, "settings.shcl", NULL);
	out = g_build_filename (home, "escape.txt", NULL);

	g_mkdir_with_parents (folder, 0700);
	make_files (folder);
	g_mkdir_with_parents (config, 0700);
	line = g_strdup_printf ("preferences.default-folder-viewer: %s\n", viewer);
	check (g_file_set_contents (settings, line, -1, NULL));
	g_free (line);

	envp = g_get_environ ();
	envp = g_environ_setenv (envp, "HOME", home, TRUE);
	path = g_build_filename (home, "config", NULL);
	envp = g_environ_setenv (envp, "XDG_CONFIG_HOME", path, TRUE);
	g_free (path);
	path = g_build_filename (home, "data", NULL);
	envp = g_environ_setenv (envp, "XDG_DATA_HOME", path, TRUE);
	g_free (path);
	path = g_build_filename (home, "cache", NULL);
	envp = g_environ_setenv (envp, "XDG_CACHE_HOME", path, TRUE);
	g_free (path);
	envp = g_environ_setenv (envp, "DBUS_SESSION_BUS_ADDRESS", "disabled:", TRUE);
	envp = g_environ_setenv (envp, "LD_PRELOAD", probe, TRUE);
	envp = g_environ_setenv (envp, "NEMO_ESC_OUT", out, TRUE);
	envp = g_environ_setenv (envp, "NEMO_ESC_FOLDER", "notes", TRUE);

	spawn_argv[0] = (char *) nemo;
	spawn_argv[1] = (char *) "--geometry=900x600";
	spawn_argv[2] = folder;
	spawn_argv[3] = NULL;
	if (!g_spawn_async (NULL, spawn_argv, envp, G_SPAWN_DO_NOT_REAP_CHILD, NULL, NULL,
			    &pid, &error)) {
		g_printerr ("spawn: %s\n", error->message);
		g_clear_error (&error);
		failures++;
		goto out;
	}

	for (i = 0; i < 900 && alive (pid) && !done; i++) {
		g_usleep (100 * 1000);
		g_free (text);
		text = NULL;
		done = g_file_get_contents (out, &text, NULL, NULL) && strstr (text, "done\n") != NULL;
	}
	if (!done) {
		g_printerr ("FAIL the probe never finished (%s)\n",
			    alive (pid) ? "still running" : "the program exited");
		failures++;
	}

	if (text != NULL) {
		lines = g_strsplit (text, "\n", -1);
		for (i = 0; lines[i] != NULL; i++) {
			if (g_str_has_prefix (lines[i], "FAIL ")) {
				g_printerr ("  %s\n", lines[i]);
				failures++;
				results++;
			} else if (g_str_has_prefix (lines[i], "ok ")) {
				g_print ("  %s\n", lines[i]);
				results++;
			}
		}
		g_strfreev (lines);
	}
	if (done && results != RESULTS) {
		g_printerr ("FAIL %d results, expected %d\n", results, RESULTS);
		failures++;
	}

out:
	stop (pid);
	g_strfreev (envp);
	g_free (text);
	g_free (out);
	g_free (settings);
	g_free (config);
	g_free (folder);
	g_free (home);
}

int
main (int argc, char *argv[])
{
#if !defined (__linux__) && !defined (__FreeBSD__)
	g_print ("SKIP: drives the program through LD_PRELOAD, which is Linux and FreeBSD only here\n");
	return 77;
#else
	char *root;

	if (argc < 3) {
		g_printerr ("usage: %s <nemo-anywhere> <probe module>\n", argv[0]);
		return 77;
	}

	test_own_display (argc, argv, "1280x900x24");

	if (g_getenv ("DISPLAY") == NULL) {
		g_print ("SKIP: no display\n");
		return 77;
	}

	root = test_scratch_dir ("nemo-search-escape-XXXXXX", NULL);
	run_view (argv[1], argv[2], root, "list-view");
	run_view (argv[1], argv[2], root, "icon-view");
	g_free (root);

	if (failures > 0) {
		g_printerr ("%d failure(s)\n", failures);
		return EXIT_FAILURE;
	}

	g_print ("OK\n");
	return EXIT_SUCCESS;
#endif
}
