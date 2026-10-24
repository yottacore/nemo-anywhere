/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */

/* test-nemo-list-expand-time.c - a folder opened by its expander shows its rows at once.

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

/* A small folder opened by its list view expander took about 200 ms to show
 * its rows, though it was listed in a few. The view held the rows for the
 * timer that batches a big folder's load, nothing cut the wait short once the
 * folder was all in, and pictures waited for their thumbnails first
 * (2026101417352261). The built program opens a folder of sub-folders in the
 * list view, and test-nemo-list-expand-time-probe, preloaded, opens each by
 * its expander and times its rows. The probe holds back each answer of a
 * listing after the first, so the batches come in the same order on any box:
 *
 * - FOLDERS folders listed a little slower than a local disk does: the best
 *   time to all rows in has to be under the bar, and so does the first
 *   folder's. The first is where the thumbnail store is opened, which is what
 *   the pictures in it used to wait on.
 * - SLOW folders listed a second per batch, like a share might be: the best
 *   time to the first row has to be under the bar, so they show what they have.
 *
 * A sanitizer build checks all but the time bars (TEST_TIME_BARS_HOLD).
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

#define FOLDERS 4
#define SLOW    2

/* More than one batch of the listing, so the last rows come in apart from
   the first. */
#define FILES   120

/* What the probe holds each later answer of a slow listing for. */
#define HOLD_MS 1000.0

/* The old wait was a 200 ms timer started when the first rows came in, so it
 * could never come in under this. A fixed build took 60 to 110 ms to all rows,
 * and 10 to 35 to the first. */
#define BAR_MS  180.0

#define RUNS    3

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

/* One blue pixel. */
static const guint8 pixel_png[] = {
	0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a, 0x00, 0x00, 0x00, 0x0d, 0x49, 0x48, 0x44, 0x52,
	0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x01, 0x08, 0x02, 0x00, 0x00, 0x00, 0x90, 0x77, 0x53,
	0xde, 0x00, 0x00, 0x00, 0x0c, 0x49, 0x44, 0x41, 0x54, 0x78, 0x9c, 0x63, 0x90, 0xd3, 0x38, 0x01,
	0x00, 0x01, 0x76, 0x01, 0x0f, 0xf2, 0x94, 0x99, 0xe3, 0x00, 0x00, 0x00, 0x00, 0x49, 0x45, 0x4e,
	0x44, 0xae, 0x42, 0x60, 0x82,
};

/* Names, sizes and dates that differ, so no two rows measure or sort alike.
   A few are pictures, which wait for a thumbnail in a folder's first load. */
static void
make_folder (const char *root, const char *kind, int number)
{
	static const char *stems[] = { "alpha", "Bravo", "charlie notes", "delta_7", "Echo", "foxtrot (2)" };
	static const char *exts[] = { ".txt", ".c", ".md", ".log", "", ".csv", ".png" };
	char *name = g_strdup_printf ("%s %d", kind, number);
	char *folder = g_build_filename (root, name, NULL);
	int i;

	g_mkdir_with_parents (folder, 0700);
	for (i = 0; i < FILES; i++) {
		const char *stem = stems[(i + number) % G_N_ELEMENTS (stems)];
		const char *ext = exts[i % G_N_ELEMENTS (exts)];
		char *file = g_strdup_printf ("%s %d%s", stem, i, ext);
		char *path = g_build_filename (folder, file, NULL);
		char *text = g_strnfill ((gsize) (i * 37 + number) % 5000, 'x');
		GFile *location = g_file_new_for_path (path);
		guint64 when = 1600000000 + (guint64) (i * 86400 * 7 + number * 3600);

		/* One of them a folder, so a row in there has an expander too. */
		if (i == FILES - 1) {
			g_mkdir_with_parents (path, 0700);
		} else if (g_str_has_suffix (file, ".png")) {
			check (g_file_set_contents (path, (const char *) pixel_png, sizeof pixel_png, NULL));
		} else {
			check (g_file_set_contents (path, text, -1, NULL));
		}
		g_file_set_attribute_uint64 (location, G_FILE_ATTRIBUTE_TIME_MODIFIED, when,
					     G_FILE_QUERY_INFO_NONE, NULL, NULL);
		g_object_unref (location);
		g_free (text);
		g_free (path);
		g_free (file);
	}
	g_free (folder);
	g_free (name);
}

/* Starts the program on top with a home of its own, so no run finds the
   thumbnails or view settings an earlier one left, and reads the probe's
   times back. A wrong count or a FAIL from the probe is a failure here; the
   times are left to the caller. */
static void
open_all (const char *nemo, char **base_env, const char *root, int run, char *top,
	  const char *out, double *best_all, double *best_first, double *first_folder_all)
{
	char *home = g_strdup_printf ("%s/run %d", root, run);
	char *text = NULL;
	char **envp, **lines;
	char *spawn_argv[4];
	char *path;
	GError *error = NULL;
	GPid pid = 0;
	gboolean done = FALSE;
	int i, opened = 0;

	*best_all = *best_first = *first_folder_all = -1;
	g_unlink (out);

	envp = g_strdupv (base_env);
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

	spawn_argv[0] = (char *) nemo;
	spawn_argv[1] = (char *) "--geometry=900x700";
	spawn_argv[2] = top;
	spawn_argv[3] = NULL;
	if (!g_spawn_async (NULL, spawn_argv, envp, G_SPAWN_DO_NOT_REAP_CHILD, NULL, NULL,
			    &pid, &error)) {
		g_printerr ("spawn: %s\n", error->message);
		g_clear_error (&error);
		failures++;
		goto out;
	}

	/* Ten seconds a folder at most, plus the start. */
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
			} else if (g_str_has_prefix (lines[i], "opened ")) {
				char **fields = g_strsplit (lines[i] + 7, "|", 3);
				double first, all;

				if (g_strv_length (fields) != 3) {
					g_printerr ("FAIL: can't read \"%s\"\n", lines[i]);
					failures++;
					g_strfreev (fields);
					continue;
				}
				first = g_ascii_strtod (fields[1], NULL);
				all = g_ascii_strtod (fields[2], NULL);
				g_print ("%s: first row %.1f ms, all %.1f ms\n", fields[0], first, all);
				if (g_str_has_prefix (fields[0], "slow ")) {
					/* Else the hold never happened and the check below
					   proves nothing. */
					check (all >= HOLD_MS);
					if (*best_first < 0 || first < *best_first) {
						*best_first = first;
					}
				} else {
					if (*first_folder_all < 0) {
						*first_folder_all = all;
					}
					if (*best_all < 0 || all < *best_all) {
						*best_all = all;
					}
				}
				opened++;
				g_strfreev (fields);
			}
		}
		g_strfreev (lines);
	}
	if (done && opened != FOLDERS + SLOW) {
		g_printerr ("FAIL: %d folders opened, expected %d\n", opened, FOLDERS + SLOW);
		failures++;
	}

out:
	stop (pid);
	g_strfreev (envp);
	g_free (text);
	g_free (home);
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
	char *root, *top, *out, *path, *wanted;
	char **envp;
	double best_all, best_first, first_folder_all;
	int i, run;

	if (argc < 3) {
		g_printerr ("usage: %s <nemo-anywhere> <probe module>\n", argv[0]);
		return 77;
	}
	if (g_getenv ("DISPLAY") == NULL) {
		g_print ("SKIP: no display\n");
		return 77;
	}

	root = test_scratch_dir ("nemo-expand-time-XXXXXX", NULL);
	top = g_build_filename (root, "top", NULL);
	for (i = 0; i < FOLDERS; i++) {
		make_folder (top, "folder", i);
	}
	for (i = 0; i < SLOW; i++) {
		make_folder (top, "slow", i);
	}
	path = g_build_filename (top, "loose file.txt", NULL);
	check (g_file_set_contents (path, "loose", -1, NULL));
	g_free (path);
	out = g_build_filename (root, "expand.txt", NULL);
	wanted = g_strdup_printf ("%d", FILES);

	envp = g_get_environ ();
	envp = g_environ_setenv (envp, "DBUS_SESSION_BUS_ADDRESS", "disabled:", TRUE);
	envp = g_environ_setenv (envp, "LD_PRELOAD", argv[2], TRUE);
	envp = g_environ_setenv (envp, "NEMO_EXPAND_OUT", out, TRUE);
	envp = g_environ_setenv (envp, "NEMO_EXPAND_FILES", wanted, TRUE);

	/* A loaded box can go over the bar now and then (2026101613310525).
	   The old timer can't come in under it at all, so it fails every run. */
	for (run = 1; run <= RUNS; run++) {
		int before = failures;

		open_all (argv[1], envp, root, run, top, out, &best_all, &best_first, &first_folder_all);
		if (failures > before || !TEST_TIME_BARS_HOLD ||
		    (best_all < BAR_MS && first_folder_all < BAR_MS && best_first < BAR_MS)) {
			break;
		}
		if (run < RUNS) {
			g_print ("over the bar on run %d of %d, once more\n", run, RUNS);
		}
	}
	if (!TEST_TIME_BARS_HOLD) {
		g_print ("time bars not held in a sanitizer build\n");
	} else {
		if (best_all >= BAR_MS) {
			g_printerr ("FAIL: the quickest folder took %.1f ms to show all its rows, the bar is %.0f ms\n",
				    best_all, BAR_MS);
			failures++;
		}
		if (first_folder_all >= BAR_MS) {
			g_printerr ("FAIL: the first folder took %.1f ms to show all its rows, the bar is %.0f ms\n",
				    first_folder_all, BAR_MS);
			failures++;
		}
		if (best_first >= BAR_MS) {
			g_printerr ("FAIL: the quickest slow folder took %.1f ms to show a row, the bar is %.0f ms\n",
				    best_first, BAR_MS);
			failures++;
		}
	}

	g_strfreev (envp);
	g_free (wanted);
	g_free (out);
	g_free (top);
	g_free (root);

	if (failures > 0) {
		g_printerr ("%d failure(s)\n", failures);
		return EXIT_FAILURE;
	}

	g_print ("OK\n");
	return EXIT_SUCCESS;
#endif
}
