/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */

/* test-nemo-places-bookmarks-heading.c - no empty Bookmarks heading after a
   settings edit.

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

/* With no bookmarks, Places showed an empty Bookmarks heading once the
 * settings file was edited by hand. The window saves where its own Bookmarks
 * section starts, and an edit that leaves that line out puts the key back to
 * its "never set" value, which was then read as an index.
 *
 * The built program (argv[1]) opens a folder in a home with no bookmarks,
 * with test-nemo-places-bookmarks-heading-probe (argv[2]) preloaded to read
 * Places. Once the program has saved its settings, the file is rewritten the
 * way a person would, with that line gone and Desktop turned off so the probe
 * can tell the edit was read. Linux only, since it works by LD_PRELOAD. */

#include <config.h>

#include <signal.h>
#include <stdio.h>
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

static gboolean
file_has (const char *path, const char *needle)
{
	char *text = NULL;
	gboolean found = g_file_get_contents (path, &text, NULL, NULL) && strstr (text, needle) != NULL;

	g_free (text);
	return found;
}

/* In place, as an editor saving over the file does. */
static gboolean
hand_edit (const char *path, const char *text)
{
	FILE *f = fopen (path, "w");
	gboolean written;

	if (f == NULL) {
		return FALSE;
	}
	written = fputs (text, f) >= 0;
	return fclose (f) == 0 && written;
}

int
main (int argc, char *argv[])
{
#ifndef __linux__
	(void) argc;
	(void) argv;
	g_print ("SKIP: drives the program through LD_PRELOAD, which is Linux only here\n");
	return 77;
#else
	char *root, *home, *out, *settings, *path;
	char *text = NULL;
	char **envp, **lines;
	char *spawn_argv[4];
	GError *error = NULL;
	GPid pid = 0;
	gboolean ready = FALSE, saved = FALSE, done = FALSE;
	int i, results = 0;

	if (argc < 3) {
		g_printerr ("usage: %s <nemo-anywhere> <probe module>\n", argv[0]);
		return 77;
	}
	test_own_display (argc, argv, NULL);
	if (g_getenv ("DISPLAY") == NULL) {
		g_print ("SKIP: no display\n");
		return 77;
	}

	root = test_scratch_dir ("nemo-places-bmhead-XXXXXX", NULL);
	home = g_build_filename (root, "home", NULL);
	g_mkdir_with_parents (home, 0700);
	out = g_build_filename (root, "places.txt", NULL);
	settings = g_build_filename (root, "config", "nemo-anywhere", "settings.shcl", NULL);

	envp = g_get_environ ();
	envp = g_environ_setenv (envp, "HOME", home, TRUE);
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
	envp = g_environ_setenv (envp, "NEMO_BMHEAD_OUT", out, TRUE);

	spawn_argv[0] = argv[1];
	spawn_argv[1] = (char *) "--geometry=800x600";
	spawn_argv[2] = home;
	spawn_argv[3] = NULL;
	if (!g_spawn_async (NULL, spawn_argv, envp, G_SPAWN_DO_NOT_REAP_CHILD, NULL, NULL,
			    &pid, &error)) {
		g_printerr ("spawn: %s\n", error->message);
		g_clear_error (&error);
		failures++;
		goto out;
	}

	for (i = 0; i < 300 && alive (pid) && !ready; i++) {
		g_usleep (100 * 1000);
		ready = file_has (out, "ready\n");
	}
	/* Edited before the save, the program would put the line back itself. */
	for (i = 0; i < 100 && alive (pid) && ready && !saved; i++) {
		saved = file_has (settings, "sidebar-bookmark-breakpoint");
		if (!saved) {
			g_usleep (100 * 1000);
		}
	}
	if (!ready || !saved) {
		g_printerr ("FAIL: %s\n", !ready ? "Places never listed Desktop" :
			    "the program never saved where its Bookmarks section starts");
		failures++;
	} else if (!hand_edit (settings, "preferences:\n\tdesktop-is-home-dir: true\n"
					 "list-view:\n\trow-shading: true\n")) {
		g_printerr ("FAIL: could not edit %s\n", settings);
		failures++;
	}

	for (i = 0; i < 300 && alive (pid) && !done; i++) {
		g_usleep (100 * 1000);
		done = file_has (out, "done\n");
	}
	if (!done) {
		g_printerr ("FAIL: the probe never finished (%s)\n",
			    alive (pid) ? "still running" : "the program exited");
		failures++;
	}

	if (g_file_get_contents (out, &text, NULL, NULL)) {
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
	if (done && results != 3) {
		g_printerr ("FAIL: %d results, expected 3\n", results);
		failures++;
	}

out:
	stop (pid);
	g_strfreev (envp);
	g_free (text);
	g_free (settings);
	g_free (out);
	g_free (home);
	g_free (root);

	if (failures > 0) {
		g_printerr ("%d failure(s)\n", failures);
		return EXIT_FAILURE;
	}

	g_print ("OK\n");
	return EXIT_SUCCESS;
#endif
}
