/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */

/* test-nemo-open-time.c - going into a folder shows its rows at once.

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

/* Going into a small folder showed its rows about 200 ms after it was listed.
 * The view held them for the timer that batches a big folder's load, and once
 * the folder was all in it waited that long again, so the sub-folders' item
 * counts could come in first (2026101419191454). The built program starts in
 * the list view on a folder of folders, and test-nemo-open-time-probe,
 * preloaded, goes into each and back up, and times its rows. The probe holds
 * back each answer of a listing after the first, so the batches come in the
 * same order on any box. Each kind below has a bar it has to come in under:
 *
 * - LEVELS folders of varied files, pictures and a sub-folder among them,
 *   listed a little slower than a local disk does: the best time to all rows.
 *   The first isn't held to the bar alone, since it's where the thumbnail
 *   store is opened, and the pictures among a folder's first files still wait
 *   on their lookup.
 * - PLAIN folders of text files only, listed the same way: the best time to
 *   all rows. Nothing in them changes after it's listed, so only the end of
 *   the listing can put the last rows in.
 * - "slow pictures", varied files listed a second per batch, like a share
 *   might be: the time to the first row. Its first rows wait on the pictures'
 *   lookups, then go in.
 * - "slow plain", text files listed the same way: the time to the first row,
 *   with no lookups or other changes to set it off.
 *
 * Each folder's row has to show its item count too, and so does the
 * sub-folder in each of LEVELS, since the counts no longer get a head start on
 * the rows.
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

#define LEVELS 4
#define PLAIN  3

/* More than one batch of the listing, so the last rows come in apart from
   the first. */
#define FILES  120

/* Files in the sub-folder of each of LEVELS. */
#define INNER_FILES 3

/* What the probe holds each later answer of a slow listing for. */
#define HOLD_MS 1000.0

/* The old wait was a 200 ms timer started when the first rows came in, so it
 * could never come in under this. A fixed build took 35 to 95 ms to all rows
 * and 10 to 20 to the first, and with every core busy, at best 57 to 140 and
 * 16 to 82. */
#define BAR_MS  180.0

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
   With varied set, a few are pictures, which wait for a thumbnail lookup in a
   folder's first load. */
static void
make_folder (const char *parent, const char *name, int number, int files, gboolean varied)
{
	static const char *stems[] = { "alpha", "Bravo", "charlie notes", "delta_7", "Echo", "foxtrot (2)" };
	static const char *exts[] = { ".txt", ".c", ".md", ".log", "", ".csv", ".png" };
	char *folder = g_build_filename (parent, name, NULL);
	int i;

	g_mkdir_with_parents (folder, 0700);
	for (i = 0; i < files; i++) {
		const char *stem = stems[(i + number) % G_N_ELEMENTS (stems)];
		const char *ext = varied ? exts[i % G_N_ELEMENTS (exts)] : ".txt";
		char *file = g_strdup_printf ("%s %d%s", stem, i, ext);
		char *path = g_build_filename (folder, file, NULL);
		char *text = g_strnfill ((gsize) (i * 37 + number) % 5000, 'x');
		GFile *location = g_file_new_for_path (path);
		guint64 when = 1600000000 + (guint64) (i * 86400 * 7 + number * 3600);

		if (g_str_has_suffix (file, ".png")) {
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
}

/* Lowest of a kind so far, or -1. */
static void
keep_best (double *best, double value)
{
	if (*best < 0 || value < *best) {
		*best = value;
	}
}

static void
check_bar (const char *what, double value)
{
	if (value < 0 || value >= BAR_MS) {
		g_printerr ("FAIL: %s took %.1f ms, the bar is %.0f ms\n", what, value, BAR_MS);
		failures++;
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
	char *root, *top, *out, *path, *wanted, *count_want;
	char *text = NULL;
	char **envp, **lines;
	char *spawn_argv[4];
	GString *steps;
	GError *error = NULL;
	GPid pid = 0;
	gboolean done = FALSE;
	double levels_all = -1, plain_all = -1, slow_pictures_first = -1, slow_plain_first = -1;
	int i, opened = 0, counts = 0;

	if (argc < 3) {
		g_printerr ("usage: %s <nemo-anywhere> <probe module>\n", argv[0]);
		return 77;
	}
	if (g_getenv ("DISPLAY") == NULL) {
		g_print ("SKIP: no display\n");
		return 77;
	}

	steps = g_string_new (NULL);
	root = test_scratch_dir ("nemo-open-time-XXXXXX", NULL);
	top = g_build_filename (root, "top", NULL);
	g_mkdir_with_parents (top, 0700);
	path = g_build_filename (top, "loose file.txt", NULL);
	check (g_file_set_contents (path, "loose", -1, NULL));
	g_free (path);

	for (i = 0; i < LEVELS; i++) {
		char *name = g_strdup_printf ("level %d", i);
		char *folder = g_build_filename (top, name, NULL);

		make_folder (top, name, i, FILES - 1, TRUE);
		make_folder (folder, "inner", i, INNER_FILES, FALSE);
		g_string_append_printf (steps, "%s|", name);
		g_free (folder);
		g_free (name);
	}
	for (i = 0; i < PLAIN; i++) {
		char *name = g_strdup_printf ("plain %d", i);

		make_folder (top, name, LEVELS + i, FILES, FALSE);
		g_string_append_printf (steps, "%s|", name);
		g_free (name);
	}
	make_folder (top, "slow pictures", 10, FILES, TRUE);
	make_folder (top, "slow plain", 11, FILES, FALSE);
	g_string_append (steps, "slow pictures|slow plain");

	out = g_build_filename (root, "open.txt", NULL);
	wanted = g_strdup_printf ("%d", FILES);

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
	envp = g_environ_setenv (envp, "NEMO_OPEN_OUT", out, TRUE);
	envp = g_environ_setenv (envp, "NEMO_OPEN_FILES", wanted, TRUE);
	envp = g_environ_setenv (envp, "NEMO_OPEN_STEPS", steps->str, TRUE);

	spawn_argv[0] = argv[1];
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
	for (i = 0; i < 1000 && alive (pid) && !done; i++) {
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
			} else if (g_str_has_prefix (lines[i], "count ")) {
				char **fields = g_strsplit (lines[i] + 6, "|", 2);

				if (g_strv_length (fields) != 2) {
					g_printerr ("FAIL: can't read \"%s\"\n", lines[i]);
					failures++;
					g_strfreev (fields);
					continue;
				}
				count_want = g_strdup_printf ("%d items",
							      strcmp (fields[0], "inner") == 0 ? INNER_FILES : FILES);
				g_print ("%s: %s\n", fields[0], fields[1]);
				if (strcmp (fields[1], count_want) != 0) {
					g_printerr ("FAIL: %s shows \"%s\" for its size, expected \"%s\"\n",
						    fields[0], fields[1], count_want);
					failures++;
				}
				counts++;
				g_free (count_want);
				g_strfreev (fields);
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
					/* Else the hold never happened and the check
					   proves nothing. */
					check (all >= HOLD_MS);
					keep_best (strcmp (fields[0], "slow plain") == 0
						   ? &slow_plain_first : &slow_pictures_first, first);
				} else if (g_str_has_prefix (fields[0], "plain ")) {
					keep_best (&plain_all, all);
				} else {
					keep_best (&levels_all, all);
				}
				opened++;
				g_strfreev (fields);
			}
		}
		g_strfreev (lines);
	}
	if (done && opened != LEVELS + PLAIN + 2) {
		g_printerr ("FAIL: %d folders opened, expected %d\n", opened, LEVELS + PLAIN + 2);
		failures++;
	}
	if (done && counts != 2 * LEVELS + PLAIN + 2) {
		g_printerr ("FAIL: %d item counts read, expected %d\n", counts, 2 * LEVELS + PLAIN + 2);
		failures++;
	}
	if (done) {
		check_bar ("the quickest of the varied folders to show all its rows", levels_all);
		check_bar ("the quickest of the plain folders to show all its rows", plain_all);
		check_bar ("the slow folder with pictures to show a row", slow_pictures_first);
		check_bar ("the slow plain folder to show a row", slow_plain_first);
	}

out:
	stop (pid);
	g_strfreev (envp);
	g_string_free (steps, TRUE);
	g_free (text);
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
