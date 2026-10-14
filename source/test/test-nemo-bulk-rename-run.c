/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */

/* test-nemo-bulk-rename-run.c - what a bulk renamer is handed.

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

/* F2 with several files selected starts the bulk renamer with the files after
 * its command. On Linux they went as file:// addresses, which a renamer that
 * takes a list of files can't open. The built program runs with a stand-in
 * renamer that writes down what it got, and with
 * test-nemo-bulk-rename-run-probe preloaded to press the keys.
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

/* A space, a percent sign and a quote, so the quoting shows. */
static const char *const names[] = { "100% done.txt", "it's here.txt", "plain.txt" };

static int
compare_names (gconstpointer a, gconstpointer b)
{
	return g_strcmp0 (*(const char *const *) a, *(const char *const *) b);
}

int
main (int argc, char *argv[])
{
#if !defined (__linux__) && !defined (__FreeBSD__)
	g_print ("SKIP: drives the program through LD_PRELOAD, which is Linux and FreeBSD only here\n");
	return 77;
#else
	char *root, *folder, *config, *settings, *out, *got, *renamer, *path, *line;
	char *text = NULL, *args = NULL;
	char *spawn_argv[4];
	char **envp, **lines;
	GError *error = NULL;
	GPid pid = 0;
	guint i;
	int tries;

	if (argc < 3) {
		g_printerr ("usage: %s <nemo-anywhere> <probe module>\n", argv[0]);
		return 77;
	}

	test_own_display (argc, argv, "1280x900x24");

	if (g_getenv ("DISPLAY") == NULL) {
		g_print ("SKIP: no display\n");
		return 77;
	}

	root = test_scratch_dir ("nemo-bulk-rename-run-XXXXXX", NULL);
	folder = g_build_filename (root, "my notes", NULL);
	config = g_build_filename (root, "config", "nemo-anywhere", NULL);
	settings = g_build_filename (config, "settings.shcl", NULL);
	out = g_build_filename (root, "probe.txt", NULL);
	got = g_build_filename (root, "got.txt", NULL);
	renamer = g_build_filename (root, "renamer.sh", NULL);

	g_mkdir_with_parents (folder, 0700);
	for (i = 0; i < G_N_ELEMENTS (names); i++) {
		path = g_build_filename (folder, names[i], NULL);
		check (g_file_set_contents (path, names[i], -1, NULL));
		g_free (path);
	}
	text = g_strdup_printf ("#!/bin/sh\nfor a in \"$@\"; do printf '%%s\\n' \"$a\"; done > '%s.part'\nmv '%s.part' '%s'\n",
				got, got, got);
	check (g_file_set_contents (renamer, text, -1, NULL));
	check (g_chmod (renamer, 0755) == 0);
	g_free (text);
	text = NULL;

	g_mkdir_with_parents (config, 0700);
	line = g_strdup_printf ("preferences.default-folder-viewer: list-view\npreferences.bulk-rename-tool: %s --flag\n",
				renamer);
	check (g_file_set_contents (settings, line, -1, NULL));
	g_free (line);

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
	envp = g_environ_setenv (envp, "NEMO_BRUN_OUT", out, TRUE);
	envp = g_environ_setenv (envp, "NEMO_BRUN_FOLDER", "my notes", TRUE);

	spawn_argv[0] = argv[1];
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

	for (tries = 0; tries < 400 && alive (pid) && args == NULL; tries++) {
		g_usleep (100 * 1000);
		g_free (text);
		text = NULL;
		if (g_file_get_contents (out, &text, NULL, NULL) && g_str_has_prefix (text, "FAIL")) {
			g_printerr ("%s", text);
			failures++;
			break;
		}
		if (!g_file_get_contents (got, &args, NULL, NULL)) {
			args = NULL;
		}
	}
	if (args == NULL) {
		g_printerr ("FAIL the renamer never ran (%s)\n", alive (pid) ? "still running" : "the program exited");
		failures++;
		goto out;
	}

	lines = g_strsplit (args, "\n", -1);
	check (g_strv_length (lines) == G_N_ELEMENTS (names) + 2);
	if (g_strv_length (lines) == G_N_ELEMENTS (names) + 2) {
		check (g_strcmp0 (lines[0], "--flag") == 0);
		qsort (lines + 1, G_N_ELEMENTS (names), sizeof (char *), compare_names);
		for (i = 0; i < G_N_ELEMENTS (names); i++) {
			path = g_build_filename (folder, names[i], NULL);
			if (g_strcmp0 (lines[i + 1], path) != 0) {
				g_printerr ("FAIL file %u went as \"%s\", wanted \"%s\"\n", i, lines[i + 1], path);
				failures++;
			} else {
				g_print ("ok %s\n", path);
			}
			g_free (path);
		}
	}
	g_strfreev (lines);

out:
	stop (pid);
	g_strfreev (envp);
	g_free (args);
	g_free (text);
	g_free (renamer);
	g_free (got);
	g_free (out);
	g_free (settings);
	g_free (config);
	g_free (folder);
	g_free (root);

	if (failures > 0) {
		g_printerr ("%d failure(s)\n", failures);
		return EXIT_FAILURE;
	}

	g_print ("OK\n");
	return EXIT_SUCCESS;
#endif
}
