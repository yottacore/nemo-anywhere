/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */

/* test-nemo-leaks.c - operations that are done over and over leave the heap
   where it was.

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

/* Each case repeats one operation the way the app does it, and fails where
 * the heap grows by a block or more a round. A cache that fills on first use
 * is filled in the warm-up rounds, before the count starts.
 *
 * Argument: the case to run. A progress manager is held throughout, as the
 * app holds one for its life. */

#include <config.h>

#include <stdlib.h>
#include <string.h>
#include <gtk/gtk.h>

#include <libnemo-private/nemo-global-preferences.h>
#include <libnemo-private/nemo-progress-info.h>
#include <libnemo-private/nemo-progress-info-manager.h>

#include "test-heap.h"
#include "test-scratch.h"
#include "test-check.h"

#define WARMUP_ROUNDS 8
#define COUNTED_ROUNDS 64

static void
drain_main_loop (void)
{
	while (g_main_context_pending (NULL)) {
		g_main_context_iteration (NULL, FALSE);
	}
}

/* A job's progress, from start to finish. */
static void
progress_round (gpointer data)
{
	NemoProgressInfo *info;

	(void) data;

	info = nemo_progress_info_new ();
	nemo_progress_info_start (info);
	nemo_progress_info_finish (info);
	drain_main_loop ();
	g_object_unref (info);
}

static int
run_case (const char *name, void (*op) (gpointer), gpointer data)
{
	gint64 growth;

	growth = test_heap_growth (op, data, WARMUP_ROUNDS, COUNTED_ROUNDS);
	if (growth < 0) {
		g_print ("SKIP: the heap cannot be read here\n");
		return 77;
	}

	g_print ("%s: the heap grew %" G_GINT64_FORMAT " bytes over %d rounds\n",
		 name, growth, COUNTED_ROUNDS);
	/* The smallest leak there is, once a round, is twice this. */
	check (growth < (gint64) COUNTED_ROUNDS * TEST_HEAP_SMALLEST_BLOCK / 2);

	return 0;
}

int
main (int argc, char *argv[])
{
	NemoProgressInfoManager *manager;
	const char *which;
	char *tmp;
	int skipped = 0;

	test_heap_init (argc, argv);

	which = argc > 1 ? argv[1] : "progress";
	tmp = test_scratch_config_home ("nemo-leaks-XXXXXX");

	if (!gtk_init_check (&argc, &argv)) {
		g_print ("SKIP: no display\n");
		g_free (tmp);
		return 77;
	}
	nemo_global_preferences_init ();
	manager = nemo_progress_info_manager_new ();

	if (strcmp (which, "progress") == 0) {
		skipped = run_case (which, progress_round, NULL);
	} else {
		g_printerr ("unknown case: %s\n", which);
		failures++;
	}

	g_object_unref (manager);
	g_free (tmp);

	if (skipped != 0) {
		return skipped;
	}
	if (failures > 0) {
		g_printerr ("%d failure(s)\n", failures);
		return EXIT_FAILURE;
	}

	g_print ("OK\n");
	return EXIT_SUCCESS;
}
