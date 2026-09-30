/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */

/* test-nemo-list-view-work.c - the list view's work per row, counted.

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

/* The list view measures every row as it arrives and again as its details
 * fill in, and shades rows from the cell data funcs, so what each costs is
 * paid per row or per cell across a whole folder. The built program opens a
 * folder of FILES files in the list view, once with shading off and once with
 * it on, with test-nemo-list-view-counts preloaded to count the calls behind
 * that work. Each count is held against the cells measured, so none depends
 * on how fast the box is or how many frames it drew:
 *
 * - "No background" goes to a renderer once, not once per cell.
 * - Where a row sits is asked once per row, not once per cell.
 * - The column list and the two theme sizes are kept, not asked per row.
 *
 * The bars and what the counts came to are at the checks below. Needs the built
 * program (argv[1]), the counting module (argv[2]) and a display. Linux only,
 * since it works by LD_PRELOAD. */

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

#define FILES 2000

typedef struct {
	gint64 cells;
	gint64 plain;
	gint64 tinted;
	gint64 areas;
	gint64 column_lists;
	gint64 style_reads;
} Counts;

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
read_counts (const char *path, Counts *counts)
{
	char *text = NULL;
	char **lines;
	int i;

	if (!g_file_get_contents (path, &text, NULL, NULL)) {
		return FALSE;
	}

	memset (counts, 0, sizeof *counts);
	lines = g_strsplit (text, "\n", -1);
	for (i = 0; lines[i] != NULL; i++) {
		char **pair = g_strsplit (lines[i], " ", 2);

		if (pair[0] != NULL && pair[1] != NULL) {
			gint64 value = g_ascii_strtoll (pair[1], NULL, 10);

			if (strcmp (pair[0], "cells") == 0) counts->cells = value;
			else if (strcmp (pair[0], "plain") == 0) counts->plain = value;
			else if (strcmp (pair[0], "tinted") == 0) counts->tinted = value;
			else if (strcmp (pair[0], "areas") == 0) counts->areas = value;
			else if (strcmp (pair[0], "column_lists") == 0) counts->column_lists = value;
			else if (strcmp (pair[0], "style_reads") == 0) counts->style_reads = value;
		}
		g_strfreev (pair);
	}
	g_strfreev (lines);
	g_free (text);

	return TRUE;
}

/* Opens the folder, waits for the measuring to finish, and reads the counts.
   FALSE when the folder never finished loading. */
static gboolean
count_one_load (const char *exe,
		const char *module,
		gboolean    shading,
		Counts     *counts)
{
	char *root = test_scratch_dir ("nemo-list-work-XXXXXX", NULL);
	char *folder = g_build_filename (root, "folder", NULL);
	char *config = g_build_filename (root, "config", "nemo-anywhere", NULL);
	char *settings = g_build_filename (config, "settings.shcl", NULL);
	char *out = g_build_filename (root, "counts.txt", NULL);
	char *path;
	char *argv[] = { (char *) exe, "--geometry=800x600", folder, NULL };
	char **envp = g_get_environ ();
	GError *error = NULL;
	GPid pid = 0;
	gint64 last = -1;
	int i, still = 0;
	gboolean loaded = FALSE;

	g_mkdir_with_parents (folder, 0700);
	g_mkdir_with_parents (config, 0700);
	for (i = 0; i < FILES; i++) {
		char name[32];

		g_snprintf (name, sizeof name, "file-%04d.txt", i);
		path = g_build_filename (folder, name, NULL);
		check (g_file_set_contents (path, name, -1, NULL));
		g_free (path);
	}
	check (g_file_set_contents (settings,
				    shading ? "list-view.row-shading: true\n"
					    : "list-view.row-shading: false\n",
				    -1, NULL));

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
	envp = g_environ_setenv (envp, "LD_PRELOAD", module, TRUE);
	envp = g_environ_setenv (envp, "NEMO_COUNTS_OUT", out, TRUE);

	if (!g_spawn_async (NULL, argv, envp, G_SPAWN_DO_NOT_REAP_CHILD, NULL, NULL,
			    &pid, &error)) {
		g_printerr ("spawn: %s\n", error->message);
		g_clear_error (&error);
		failures++;
		goto out;
	}

	/* Every row is measured at least once, so FILES cells is the least a
	   finished load has done. After that, done is when nothing moves. */
	for (i = 0; i < 600 && alive (pid) && still < 20; i++) {
		g_usleep (100 * 1000);
		if (!read_counts (out, counts)) {
			continue;
		}
		if (counts->cells >= FILES && counts->cells == last) {
			still++;
		} else {
			still = 0;
		}
		last = counts->cells;
	}
	loaded = still >= 20;
	if (!loaded) {
		g_printerr ("shading %s: the folder never finished loading (%" G_GINT64_FORMAT
			    " cells measured)\n", shading ? "on" : "off", last);
	}

out:
	stop (pid);
	g_strfreev (envp);
	g_free (out);
	g_free (settings);
	g_free (config);
	g_free (folder);
	g_free (root);

	return loaded;
}

static void
print_counts (const char *label, const Counts *c)
{
	g_print ("%s: cells %" G_GINT64_FORMAT ", plain %" G_GINT64_FORMAT ", tinted %" G_GINT64_FORMAT
		 ", row places %" G_GINT64_FORMAT ", column lists %" G_GINT64_FORMAT
		 ", theme size reads %" G_GINT64_FORMAT "\n",
		 label, c->cells, c->plain, c->tinted, c->areas, c->column_lists, c->style_reads);
}

int
main (int argc, char *argv[])
{
#ifndef __linux__
	g_print ("SKIP: counts through LD_PRELOAD, which is Linux only here\n");
	return 77;
#else
	Counts off, on;

	if (argc < 3) {
		g_printerr ("usage: %s <nemo-anywhere> <counting module>\n", argv[0]);
		return 77;
	}
	if (g_getenv ("DISPLAY") == NULL) {
		g_print ("SKIP: no display\n");
		return 77;
	}

	/* About 16,000 cells measured. Once per renderer, per column set and per
	   theme came to 18, 6 and 2; once per cell or per row came to 18,588,
	   2,005 and 18,002. The bar is one per twenty cells. */
	if (count_one_load (argv[1], argv[2], FALSE, &off)) {
		print_counts ("shading off", &off);
		check (off.plain * 20 <= off.cells);
		check (off.column_lists * 20 <= off.cells);
		check (off.style_reads * 20 <= off.cells);
	} else {
		failures++;
	}

	/* Once per row came to 2,036, and once per cell to 18,588, against the
	   same 16,000 cells. The bar is one per two cells. */
	if (count_one_load (argv[1], argv[2], TRUE, &on)) {
		print_counts ("shading on", &on);
		check (on.areas * 2 <= on.cells);
	} else {
		failures++;
	}

	if (failures > 0) {
		g_printerr ("%d failure(s)\n", failures);
		return EXIT_FAILURE;
	}

	g_print ("OK\n");
	return EXIT_SUCCESS;
#endif
}
