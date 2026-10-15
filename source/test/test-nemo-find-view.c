/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */

/* test-nemo-find-view.c - find results keep their own view, apart from the
   folder's.

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

/* With per-folder settings off, find results took the window's view, so a
 * folder put in icon view searched into icons with no columns, and a view
 * picked for the results became the folder's. The built program runs with
 * per-folder settings off and on, with test-nemo-find-view-probe preloaded to
 * put the folder in icon view, search, pick compact view for the results,
 * search again, and leave.
 *
 * Needs the built program (argv[1]), the probe (argv[2]) and a display. Linux
 * and FreeBSD only, since it works by LD_PRELOAD. */

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

#define CHECKS 3

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
run (const char *exe, const char *probe, gboolean remember)
{
	static const struct { const char *name; gsize size; } files[] = {
		{ "apple.txt", 30 }, { "apple-pie.md", 3000 }, { "pear.c", 300 },
	};
	char *root = test_scratch_dir ("nemo-find-view-XXXXXX", NULL);
	char *folder = g_build_filename (root, "findme", NULL);
	char *config = g_build_filename (root, "config", "nemo-anywhere", NULL);
	char *settings = g_build_filename (config, "settings.shcl", NULL);
	char *out = g_build_filename (root, "find-view.txt", NULL);
	const char *label = remember ? "per-folder on" : "per-folder off";
	char *body, *path, *text = NULL;
	char *spawn_argv[4];
	char **envp, **lines;
	GError *error = NULL;
	GPid pid = 0;
	gboolean done = FALSE;
	guint f;
	int i, results = 0;

	g_mkdir_with_parents (folder, 0700);
	for (f = 0; f < G_N_ELEMENTS (files); f++) {
		char *fill = g_strnfill (files[f].size, 'x');

		path = g_build_filename (folder, files[f].name, NULL);
		check (g_file_set_contents (path, fill, -1, NULL));
		g_free (path);
		g_free (fill);
	}
	g_mkdir_with_parents (config, 0700);
	body = g_strdup_printf ("preferences.remember-folder-settings: %s\n",
				remember ? "true" : "false");
	check (g_file_set_contents (settings, body, -1, NULL));
	g_free (body);

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
	envp = g_environ_setenv (envp, "LD_PRELOAD", probe, TRUE);
	envp = g_environ_setenv (envp, "NEMO_FIND_VIEW_OUT", out, TRUE);

	spawn_argv[0] = (char *) exe;
	spawn_argv[1] = (char *) "--geometry=800x600";
	spawn_argv[2] = folder;
	spawn_argv[3] = NULL;
	if (!g_spawn_async (NULL, spawn_argv, envp, G_SPAWN_DO_NOT_REAP_CHILD, NULL, NULL,
			    &pid, &error)) {
		g_printerr ("%s: spawn: %s\n", label, error->message);
		g_clear_error (&error);
		failures++;
		goto out;
	}

	/* The probe gives each of its seven steps ten seconds. */
	for (i = 0; i < 800 && alive (pid) && !done; i++) {
		g_usleep (100 * 1000);
		g_free (text);
		text = NULL;
		done = g_file_get_contents (out, &text, NULL, NULL) && strstr (text, "done\n") != NULL;
	}
	if (!done) {
		g_printerr ("FAIL %s: the probe never finished (%s)\n", label,
			    alive (pid) ? "still running" : "the program exited");
		failures++;
	}

	if (text != NULL) {
		lines = g_strsplit (text, "\n", -1);
		for (i = 0; lines[i] != NULL; i++) {
			if (g_str_has_prefix (lines[i], "FAIL ")) {
				g_printerr ("%s: %s\n", label, lines[i]);
				failures++;
				results++;
			} else if (g_str_has_prefix (lines[i], "ok ")) {
				g_print ("%s: %s\n", label, lines[i]);
				results++;
			}
		}
		g_strfreev (lines);
	}
	if (done && results != CHECKS) {
		g_printerr ("FAIL %s: %d results, expected %d\n", label, results, CHECKS);
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
	g_free (root);
}

int
main (int argc, char *argv[])
{
#if !defined (__linux__) && !defined (__FreeBSD__)
	g_print ("SKIP: drives the program through LD_PRELOAD, which is Linux and FreeBSD only here\n");
	return 77;
#else
	if (argc < 3) {
		g_printerr ("usage: %s <nemo-anywhere> <probe module>\n", argv[0]);
		return 77;
	}
	if (g_getenv ("DISPLAY") == NULL) {
		g_print ("SKIP: no display\n");
		return 77;
	}

	run (argv[1], argv[2], FALSE);
	run (argv[1], argv[2], TRUE);

	if (failures > 0) {
		g_printerr ("%d failure(s)\n", failures);
		return EXIT_FAILURE;
	}

	g_print ("OK\n");
	return EXIT_SUCCESS;
#endif
}
