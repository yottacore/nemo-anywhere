/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */

/* test-nemo-edit-keys.c - a delete leaves the item before it selected, and
   Ctrl+Shift+V pastes into the viewed folder, in the real window.

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

/* Opens a folder of sub/, a.txt, b.txt, and c.txt in the view argv[3] names,
 * list-view unless it's icon-view, with test-nemo-edit-keys-probe preloaded.
 * sub/ has inner.txt in it.
 * The probe deletes b.txt for good and checks a.txt is left selected. In the
 * list view it then copies c.txt and, with sub selected and expanded, checks
 * that Ctrl+V pastes into sub and Ctrl+Shift+V into the viewed folder.
 *
 * Needs the built program (argv[1]), the probe (argv[2]) and a display. Runs
 * the program under its own HOME and XDG dirs, with the delete test guard off
 * so the delete question is the program's own. Linux and FreeBSD only, since
 * it works by LD_PRELOAD. */

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

#if defined (__linux__) || defined (__FreeBSD__)

#define LIST_RESULTS 13
#define ICON_RESULTS 5

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
make_file (const char *folder, const char *name)
{
	char *path = g_build_filename (folder, name, NULL);

	check (g_file_set_contents (path, name, -1, NULL));
	g_free (path);
}

#endif

int
main (int argc, char *argv[])
{
#if !defined (__linux__) && !defined (__FreeBSD__)
	(void) argc;
	(void) argv;
	g_print ("SKIP: drives the program through LD_PRELOAD, which is Linux and FreeBSD only here\n");
	return 77;
#else
	const char *view;
	gboolean list_mode;
	char *root, *folder, *sub, *config, *settings, *out, *path, *body, *text = NULL;
	char *spawn_argv[4];
	char **envp, **lines;
	GError *error = NULL;
	GPid pid = 0;
	gboolean done = FALSE;
	int i, results = 0, expected;

	if (argc < 3) {
		g_printerr ("usage: %s <nemo-anywhere> <probe module> [list-view|icon-view]\n", argv[0]);
		return 77;
	}
	view = argc > 3 ? argv[3] : "list-view";
	list_mode = g_strcmp0 (view, "icon-view") != 0;
	expected = list_mode ? LIST_RESULTS : ICON_RESULTS;

	test_own_display (argc, argv, "1280x900x24");
	if (g_getenv ("DISPLAY") == NULL) {
		g_print ("SKIP: no display\n");
		return 77;
	}

	root = test_scratch_dir ("nemo-edit-keys-XXXXXX", NULL);
	folder = g_build_filename (root, "editkeys", NULL);
	sub = g_build_filename (folder, "sub", NULL);
	config = g_build_filename (root, "config", "nemo-anywhere", NULL);
	settings = g_build_filename (config, "settings.shcl", NULL);
	out = g_build_filename (root, "editkeys.txt", NULL);
	g_mkdir_with_parents (sub, 0700);
	g_mkdir_with_parents (config, 0700);
	make_file (sub, "inner.txt");
	make_file (folder, "a.txt");
	make_file (folder, "b.txt");
	make_file (folder, "c.txt");
	body = g_strdup_printf ("preferences.default-folder-viewer: %s\n", view);
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
	envp = g_environ_setenv (envp, "NEMO_TESTGUARD_ALL_DELETES", "0", TRUE);
	envp = g_environ_setenv (envp, "LD_PRELOAD", argv[2], TRUE);
	envp = g_environ_setenv (envp, "NEMO_EDITKEYS_OUT", out, TRUE);
	envp = g_environ_setenv (envp, "NEMO_EDITKEYS_FOLDER", folder, TRUE);
	envp = g_environ_setenv (envp, "NEMO_EDITKEYS_VIEW", view, TRUE);

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

	for (i = 0; i < 950 && alive (pid) && !done; i++) {
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
	if (done && results != expected) {
		g_printerr ("FAIL %d results, expected %d\n", results, expected);
		failures++;
	}

	/* On disk: b.txt is gone, and in the list view sub has the plain
	   paste while the viewed folder took the other. */
	path = g_build_filename (folder, "b.txt", NULL);
	check (!g_file_test (path, G_FILE_TEST_EXISTS));
	g_free (path);
	if (list_mode) {
		path = g_build_filename (sub, "c.txt", NULL);
		check (g_file_test (path, G_FILE_TEST_EXISTS));
		g_free (path);
	}

out:
	stop (pid);
	g_strfreev (envp);
	g_free (text);
	g_free (out);
	g_free (settings);
	g_free (config);
	g_free (sub);
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
