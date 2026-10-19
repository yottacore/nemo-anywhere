/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */

/* test-nemo-rename-photo.c - F2 on a picture in a folder of pictures.

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

/* In a folder of pictures the icon view shows big icons, and F2 there showed
 * nothing anyone could type into. The name under a tall picture sat below
 * what was in sight, and a second F2 on the same file after the size changed
 * got a field at the old width, with the name outside it. The built program
 * runs in a short window with test-nemo-rename-photo-probe preloaded to press
 * the keys and look.
 *
 * Needs the built program (argv[1]), the probe (argv[2]) and a display. Runs
 * on an X server of its own. Linux and FreeBSD only, since it works by
 * LD_PRELOAD. Another probe can be given, with how many results it reports
 * as argv[3]. */

#include <config.h>

#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

#include <gio/gio.h>
#include <glib/gstdio.h>
#include <gdk-pixbuf/gdk-pixbuf.h>

#include "test-scratch.h"
#include "test-check.h"

#define RESULTS 6

#if defined (__linux__) || defined (__FreeBSD__)
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

/* Different shapes, so the rows are not all one height. */
static void
make_pictures (const char *folder)
{
	static const int sizes[][2] = {
		{ 1600, 1200 }, { 900, 1400 }, { 2000, 700 }, { 1200, 1200 }, { 1500, 1000 }, { 800, 1200 },
	};
	guint i;

	for (i = 0; i < G_N_ELEMENTS (sizes); i++) {
		GdkPixbuf *picture = gdk_pixbuf_new (GDK_COLORSPACE_RGB, FALSE, 8, sizes[i][0], sizes[i][1]);
		char *path = g_strdup_printf ("%s/beach day %u.png", folder, i + 1);

		gdk_pixbuf_fill (picture, 0x3070a0ff + (i << 16));
		check (gdk_pixbuf_save (picture, path, "png", NULL, NULL));
		g_free (path);
		g_object_unref (picture);
	}
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
	char *root, *folder, *config, *settings, *out, *path, *text = NULL;
	char *spawn_argv[4];
	char **envp, **lines;
	GError *error = NULL;
	GPid pid = 0;
	gboolean done = FALSE;
	int i, results = 0, expected = RESULTS;

	if (argc < 3) {
		g_printerr ("usage: %s <nemo-anywhere> <probe module>\n", argv[0]);
		return 77;
	}

	if (argc > 3) {
		expected = atoi (argv[3]);
	}

	test_own_display (argc, argv, "1280x900x24");

	if (g_getenv ("DISPLAY") == NULL) {
		g_print ("SKIP: no display\n");
		return 77;
	}

	root = test_scratch_dir ("nemo-rename-photo-XXXXXX", NULL);
	folder = g_build_filename (root, "pics", NULL);
	config = g_build_filename (root, "config", "nemo-anywhere", NULL);
	settings = g_build_filename (config, "settings.shcl", NULL);
	out = g_build_filename (root, "rename.txt", NULL);

	g_mkdir_with_parents (folder, 0700);
	make_pictures (folder);
	g_mkdir_with_parents (config, 0700);
	check (g_file_set_contents (settings, "preferences.default-folder-viewer: icon-view\n", -1, NULL));

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
	envp = g_environ_setenv (envp, "NEMO_RENAME_OUT", out, TRUE);
	envp = g_environ_setenv (envp, "NEMO_RENAME_FOLDER", "pics", TRUE);
	envp = g_environ_setenv (envp, "NEMO_RENAME_PATH", folder, TRUE);

	/* Short, so a big picture's name starts below what is in sight. */
	spawn_argv[0] = argv[1];
	spawn_argv[1] = (char *) "--geometry=900x480";
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
	if (done && results != expected) {
		g_printerr ("FAIL %d results, expected %d\n", results, expected);
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

	if (failures > 0) {
		g_printerr ("%d failure(s)\n", failures);
		return EXIT_FAILURE;
	}

	g_print ("OK\n");
	return EXIT_SUCCESS;
#endif
}
