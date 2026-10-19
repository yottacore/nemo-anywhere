/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */

/* test-nemo-delete-after-dialog.c - one delete key is enough right after
   Properties closes, in the real window.

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

/* The delete guard ignores a delete key in the first second after a window
 * takes the focus, for keys meant for some other program. Focus coming back
 * from the program's own dialog isn't that, so the first press counts. This
 * runs the built program with test-nemo-delete-after-dialog-probe preloaded,
 * which opens Properties, closes it and presses the key at once.
 *
 * Needs the built program (argv[1]), the probe (argv[2]) and a display. Runs
 * on an X server of its own, with openbox where there is one. Linux and
 * FreeBSD only, since it works by LD_PRELOAD. */

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

#define RESULTS 6

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

int
main (int argc, char *argv[])
{
#if !defined (__linux__) && !defined (__FreeBSD__)
	g_print ("SKIP: drives the program through LD_PRELOAD, which is Linux and FreeBSD only here\n");
	return 77;
#else
	char *root, *folder, *victim, *out, *path, *text = NULL;
	char *spawn_argv[4];
	char *wm_argv[] = { (char *) "openbox", NULL };
	char **envp, **lines;
	GError *error = NULL;
	GPid pid = 0, wm = 0;
	gboolean done = FALSE;
	int i, results = 0;

	if (argc < 3) {
		g_printerr ("usage: %s <nemo-anywhere> <probe module>\n", argv[0]);
		return 77;
	}

	test_own_display (argc, argv, "1280x900x24");

	if (g_getenv ("DISPLAY") == NULL) {
		g_print ("SKIP: no display\n");
		return 77;
	}

	/* A window manager hands the focus back the way it does on a desktop.
	   Without one the probe does it. */
	if (g_getenv ("NEMO_TEST_OWN_DISPLAY") != NULL &&
	    g_spawn_async (NULL, wm_argv, NULL,
			   G_SPAWN_SEARCH_PATH | G_SPAWN_DO_NOT_REAP_CHILD |
			   G_SPAWN_STDOUT_TO_DEV_NULL | G_SPAWN_STDERR_TO_DEV_NULL,
			   NULL, NULL, &wm, NULL)) {
		g_usleep (G_USEC_PER_SEC);
	}

	root = test_scratch_dir ("nemo-delete-after-dialog-XXXXXX", NULL);
	folder = g_build_filename (root, "keepers", NULL);
	victim = g_build_filename (folder, "alpha.txt", NULL);
	out = g_build_filename (root, "deldlg.txt", NULL);

	g_mkdir_with_parents (folder, 0700);
	g_file_set_contents (victim, "alpha\n", -1, NULL);
	path = g_build_filename (root, "config", "nemo-anywhere", NULL);
	g_mkdir_with_parents (path, 0700);
	g_free (path);

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
	envp = g_environ_setenv (envp, "NEMO_DELDLG_OUT", out, TRUE);
	envp = g_environ_setenv (envp, "NEMO_DELDLG_FOLDER", folder, TRUE);

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

	for (i = 0; i < 600 && alive (pid) && !done; i++) {
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
	if (done && results != RESULTS) {
		g_printerr ("FAIL %d results, expected %d\n", results, RESULTS);
		failures++;
	}

	/* The question was cancelled, so nothing is gone. */
	g_usleep (G_USEC_PER_SEC);
	check (g_file_test (victim, G_FILE_TEST_EXISTS));

out:
	stop (pid);
	stop (wm);
	g_strfreev (envp);
	g_free (text);
	g_free (out);
	g_free (victim);
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
