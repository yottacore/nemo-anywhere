/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */

/* test-nemo-tooltip-focus.c - no tooltip while another program has the focus.

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

/* A tooltip is always on top, so one the app showed while another program
 * had the focus sat over that program's window. Now the app shows none while
 * none of its windows has the focus, and one already up goes when the focus
 * leaves. The built program runs with test-nemo-tooltip-focus-probe preloaded,
 * which hovers a toolbar button, a place, an icon and a list row, first
 * focused and then with this program, run again as --thief, holding the focus.
 *
 * Needs the built program (argv[1]), the probe (argv[2]) and a display. Runs
 * on an X server of its own, with no window manager, so focus moves only when
 * asked. Linux and FreeBSD only, since it works by LD_PRELOAD. */

#include <config.h>

#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

#include <gio/gio.h>
#include <glib/gstdio.h>
#include <gtk/gtk.h>

#include "test-scratch.h"
#include "test-check.h"

#define RESULTS 12

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
keep_focus (gpointer data)
{
	GtkWidget *thief_window = data;

	if (!gtk_window_is_active (GTK_WINDOW (thief_window))) {
		gdk_window_focus (gtk_widget_get_window (thief_window), GDK_CURRENT_TIME);
	}
	return G_SOURCE_CONTINUE;
}

static gboolean
give_up (G_GNUC_UNUSED gpointer data)
{
	gtk_main_quit ();
	return G_SOURCE_REMOVE;
}

/* The other program: a small window clear of the app's, holding the focus
   until it is stopped, or for a minute at most. */
static int
run_thief (int argc, char *argv[])
{
	GtkWidget *thief_window;

	if (!gtk_init_check (&argc, &argv)) {
		return EXIT_FAILURE;
	}
	thief_window = gtk_window_new (GTK_WINDOW_TOPLEVEL);
	gtk_window_set_title (GTK_WINDOW (thief_window), "thief");
	gtk_window_set_default_size (GTK_WINDOW (thief_window), 200, 120);
	gtk_window_move (GTK_WINDOW (thief_window), 1040, 740);
	gtk_widget_show (thief_window);
	g_timeout_add (100, keep_focus, thief_window);
	g_timeout_add_seconds (60, give_up, NULL);
	gtk_main ();
	return EXIT_SUCCESS;
}

static void
make_files (const char *folder)
{
	static const char *names[] = { "notes for monday.txt", "b.txt", "a longer name than the others.txt", "zz.txt" };
	guint i;

	for (i = 0; i < G_N_ELEMENTS (names); i++) {
		char *path = g_build_filename (folder, names[i], NULL);

		check (g_file_set_contents (path, names[i], -1, NULL));
		g_free (path);
	}
}

int
main (int argc, char *argv[])
{
#if !defined (__linux__) && !defined (__FreeBSD__)
	g_print ("SKIP: drives the program through LD_PRELOAD, which is Linux and FreeBSD only here\n");
	return 77;
#else
	char *root, *folder, *config, *settings, *out, *path, *self, *text = NULL;
	char *spawn_argv[4];
	char **envp, **lines;
	GError *error = NULL;
	GPid pid = 0;
	gboolean done = FALSE;
	int i, results = 0;

	if (argc >= 2 && strcmp (argv[1], "--thief") == 0) {
		return run_thief (argc, argv);
	}

	if (argc < 3) {
		g_printerr ("usage: %s <nemo-anywhere> <probe module>\n", argv[0]);
		return 77;
	}

	test_own_display (argc, argv, "1280x900x24");

	if (g_getenv ("DISPLAY") == NULL) {
		g_print ("SKIP: no display\n");
		return 77;
	}

	root = test_scratch_dir ("nemo-tooltip-focus-XXXXXX", NULL);
	folder = g_build_filename (root, "tips", NULL);
	config = g_build_filename (root, "config", "nemo-anywhere", NULL);
	settings = g_build_filename (config, "settings.shcl", NULL);
	out = g_build_filename (root, "tips.txt", NULL);
	self = g_canonicalize_filename (argv[0], NULL);

	g_mkdir_with_parents (folder, 0700);
	make_files (folder);
	g_mkdir_with_parents (config, 0700);
	check (g_file_set_contents (settings,
				    "preferences.default-folder-viewer: icon-view\n"
				    "preferences.tooltips-in-icon-view: true\n"
				    "preferences.tooltips-in-list-view: true\n", -1, NULL));

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
	envp = g_environ_setenv (envp, "NEMO_TIP_OUT", out, TRUE);
	envp = g_environ_setenv (envp, "NEMO_TIP_FOLDER", "tips", TRUE);
	envp = g_environ_setenv (envp, "NEMO_TIP_THIEF", self, TRUE);

	spawn_argv[0] = argv[1];
	spawn_argv[1] = (char *) "--geometry=900x600+0+0";
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

out:
	stop (pid);
	g_strfreev (envp);
	g_free (text);
	g_free (self);
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
