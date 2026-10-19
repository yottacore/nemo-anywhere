/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */

/* test-nemo-sort-views.c - the list and icon views share one sort.

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

/* A list sorted by Size opened a picture folder, which switches to icons,
 * sorted by name. The icon view kept a sort of its own. The built program runs
 * with per-folder settings off and on, and test-nemo-sort-views-probe
 * preloaded to sort, move between folders and views, and look.
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

#define RESULTS 5

/* A real 1x1 picture, so the folder counts as mostly images. Anything after
   the end chunk is ignored by readers, which gives each one its own size. */
static const guchar tiny_png[] = {
	0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a, 0x00, 0x00, 0x00, 0x0d,
	0x49, 0x48, 0x44, 0x52, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x01,
	0x08, 0x06, 0x00, 0x00, 0x00, 0x1f, 0x15, 0xc4, 0x89, 0x00, 0x00, 0x00,
	0x0a, 0x49, 0x44, 0x41, 0x54, 0x78, 0x9c, 0x63, 0x00, 0x01, 0x00, 0x00,
	0x05, 0x00, 0x01, 0x0d, 0x0a, 0x2d, 0xb4, 0x00, 0x00, 0x00, 0x00, 0x49,
	0x45, 0x4e, 0x44, 0xae, 0x42, 0x60, 0x82,
};

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
fill (const char *folder, const char *name, const void *head, gsize head_size, gsize size)
{
	char *path = g_build_filename (folder, name, NULL);
	guchar *bytes = g_malloc0 (size);

	memcpy (bytes, head, MIN (head_size, size));
	check (g_file_set_contents (path, (const char *) bytes, (gssize) size, NULL));
	g_free (bytes);
	g_free (path);
}

static void
run (const char *exe, const char *probe, gboolean remember)
{
	/* Size order is not name order or type order, so each sort shows. */
	static const struct { const char *name; gsize size; } files[] = {
		{ "apple.txt", 30 }, { "banana.dat", 3000 }, { "cherry.md", 300 },
	};
	static const struct { const char *name; gsize size; } pictures[] = {
		{ "a.png", 2000 }, { "b.png", 200 }, { "c.png", 900 },
	};
	char *root = test_scratch_dir ("nemo-sort-views-XXXXXX", NULL);
	char *folder = g_build_filename (root, "sortme", NULL);
	char *pics = g_build_filename (folder, "pics", NULL);
	char *config = g_build_filename (root, "config", "nemo-anywhere", NULL);
	char *settings = g_build_filename (config, "settings.shcl", NULL);
	char *out = g_build_filename (root, "sort.txt", NULL);
	const char *label = remember ? "per-folder on" : "per-folder off";
	char *body, *path, *text = NULL;
	char *spawn_argv[4];
	char **envp, **lines;
	GError *error = NULL;
	GPid pid = 0;
	gboolean done = FALSE;
	guint f;
	int i, results = 0;

	g_mkdir_with_parents (pics, 0700);
	for (f = 0; f < G_N_ELEMENTS (files); f++) {
		fill (folder, files[f].name, "", 0, files[f].size);
	}
	for (f = 0; f < G_N_ELEMENTS (pictures); f++) {
		fill (pics, pictures[f].name, tiny_png, sizeof (tiny_png), pictures[f].size);
	}
	g_mkdir_with_parents (config, 0700);
	body = g_strdup_printf ("preferences.default-folder-viewer: list-view\n"
				"preferences.remember-folder-settings: %s\n",
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
	envp = g_environ_setenv (envp, "NEMO_SORTSW_OUT", out, TRUE);
	envp = g_environ_setenv (envp, "NEMO_SORTSW_REMEMBER", remember ? "true" : "false", TRUE);

	spawn_argv[0] = (char *) exe;
	spawn_argv[1] = (char *) "--geometry=900x600";
	spawn_argv[2] = folder;
	spawn_argv[3] = NULL;
	if (!g_spawn_async (NULL, spawn_argv, envp, G_SPAWN_DO_NOT_REAP_CHILD, NULL, NULL,
			    &pid, &error)) {
		g_printerr ("%s: spawn: %s\n", label, error->message);
		g_clear_error (&error);
		failures++;
		goto out;
	}

	/* The probe gives each step ten seconds, and there are six. */
	for (i = 0; i < 700 && alive (pid) && !done; i++) {
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
	if (done && results != RESULTS && failures == 0) {
		g_printerr ("FAIL %s: %d results, expected %d\n", label, results, RESULTS);
		failures++;
	}

out:
	stop (pid);
	g_strfreev (envp);
	g_free (text);
	g_free (out);
	g_free (settings);
	g_free (config);
	g_free (pics);
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

	test_own_display (argc, argv, "1280x900x24");

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
