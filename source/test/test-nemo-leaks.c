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

#include <libnemo-private/nemo-archive.h>
#include <libnemo-private/nemo-clipboard.h>
#include <libnemo-private/nemo-file.h>
#include <libnemo-private/nemo-file-operations.h>
#include <libnemo-private/nemo-global-preferences.h>
#include <libnemo-private/nemo-progress-info.h>
#include <libnemo-private/nemo-progress-info-manager.h>

#include "test-heap.h"
#include "test-scratch.h"
#include "test-check.h"

#define WARMUP_ROUNDS 8
#define COUNTED_ROUNDS 64
#define JOB_TIMEOUT_SECONDS 30

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

typedef struct {
	GtkWidget *window;
	GFile *dir_a;
	GFile *dir_b;
	GList *in_a;
	GList *in_b;
	gboolean done;
} MoveCase;

static void
move_done (GHashTable *debuting_uris, gboolean success, gpointer data)
{
	MoveCase *move = data;

	(void) debuting_uris;
	check (success);
	move->done = TRUE;
	gtk_main_quit ();
}

static gboolean
give_up (gpointer data)
{
	(void) data;
	g_printerr ("FAIL: a job did not finish within %d seconds\n",
		    JOB_TIMEOUT_SECONDS);
	failures++;
	gtk_main_quit ();
	return G_SOURCE_REMOVE;
}

/* Two files moved to the other folder and back, by rename, as on one disk. */
static void
move_round (gpointer data)
{
	MoveCase *move = data;
	GList *swap_files;
	GFile *swap_dir;
	guint timeout_id;

	move->done = FALSE;
	timeout_id = g_timeout_add_seconds (JOB_TIMEOUT_SECONDS, give_up, NULL);
	nemo_file_operations_move (move->in_a, NULL, move->dir_b,
				   GTK_WINDOW (move->window), move_done, move);
	gtk_main ();
	if (move->done) {
		g_source_remove (timeout_id);
	}
	drain_main_loop ();

	swap_files = move->in_a;
	move->in_a = move->in_b;
	move->in_b = swap_files;
	swap_dir = move->dir_a;
	move->dir_a = move->dir_b;
	move->dir_b = swap_dir;
}

static GList *
make_files (GFile *dir_a, GFile *dir_b, GList **in_b)
{
	const char *names[] = { "one.txt", "two.txt" };
	GList *in_a = NULL;
	guint i;

	*in_b = NULL;
	for (i = 0; i < G_N_ELEMENTS (names); i++) {
		GFile *file = g_file_get_child (dir_a, names[i]);
		char *path = g_file_get_path (file);

		check (g_file_set_contents (path, "x", -1, NULL));
		g_free (path);
		in_a = g_list_append (in_a, file);
		*in_b = g_list_append (*in_b, g_file_get_child (dir_b, names[i]));
	}

	return in_a;
}

static void
setup_move (MoveCase *move, const char *tmp)
{
	char *path_a = g_build_filename (tmp, "a", NULL);
	char *path_b = g_build_filename (tmp, "b", NULL);

	check (g_mkdir_with_parents (path_a, 0700) == 0);
	check (g_mkdir_with_parents (path_b, 0700) == 0);
	move->window = gtk_window_new (GTK_WINDOW_TOPLEVEL);
	move->dir_a = g_file_new_for_path (path_a);
	move->dir_b = g_file_new_for_path (path_b);
	move->in_a = make_files (move->dir_a, move->dir_b, &move->in_b);
	g_free (path_a);
	g_free (path_b);
}

static void
teardown_move (MoveCase *move)
{
	g_list_free_full (move->in_a, g_object_unref);
	g_list_free_full (move->in_b, g_object_unref);
	g_object_unref (move->dir_a);
	g_object_unref (move->dir_b);
	gtk_widget_destroy (move->window);
}

typedef struct {
	GtkWidget *window;
	GList *on_clipboard;
	GList *dragged;
} ClipboardCase;

/* A drag of files that are not the ones cut, which leaves the cut alone. */
static void
clipboard_round (gpointer data)
{
	ClipboardCase *clip = data;

	nemo_clipboard_clear_if_colliding_uris (clip->window, clip->dragged,
						gdk_atom_intern_static_string ("x-special/gnome-copied-files"));
}

static char *
make_file_uri (const char *dir, const char *name)
{
	char *path = g_build_filename (dir, name, NULL);
	char *uri;

	check (g_file_set_contents (path, "x", -1, NULL));
	uri = g_filename_to_uri (path, NULL, NULL);
	g_free (path);

	return uri;
}

static void
setup_clipboard (ClipboardCase *clip, const char *tmp)
{
	char *cut = make_file_uri (tmp, "cut.txt");

	clip->window = gtk_window_new (GTK_WINDOW_TOPLEVEL);
	clip->on_clipboard = g_list_append (NULL, nemo_file_get_by_uri (cut));
	clip->dragged = g_list_append (NULL, make_file_uri (tmp, "dragged.txt"));
	nemo_clipboard_set_files (clip->window, clip->on_clipboard, TRUE);
	g_free (cut);
}

static void
teardown_clipboard (ClipboardCase *clip)
{
	GtkSelectionData *held;

	/* Still there, so every round had something to read. */
	held = gtk_clipboard_wait_for_contents (nemo_clipboard_get (clip->window),
						gdk_atom_intern_static_string ("x-special/gnome-copied-files"));
	check (held != NULL);
	if (held != NULL) {
		gtk_selection_data_free (held);
	}

	nemo_file_list_free (clip->on_clipboard);
	g_list_free_full (clip->dragged, g_free);
	gtk_widget_destroy (clip->window);
}

typedef struct {
	GtkWidget *window;
	GList *sources;
	GFile *destination;
	NemoArchiveOptions options;
	gboolean done;
	gboolean succeeded;
	gboolean canceled;
} ZipCase;

static void
zip_done (GFile *result, gboolean success, gpointer data)
{
	ZipCase *zip = data;

	(void) result;
	zip->succeeded = success;
	zip->done = TRUE;
	gtk_main_quit ();
}

/* Once some of the file is in, its entry is open, which is where a stop used
   to leave the compressor's state behind. */
static void
cancel_once_writing (NemoProgressInfo *info, gpointer data)
{
	ZipCase *zip = data;

	if (!zip->canceled && nemo_progress_info_get_progress (info) > 0) {
		zip->canceled = TRUE;
		nemo_progress_info_cancel (info);
	}
}

static void
watch_new_progress (NemoProgressInfoManager *manager,
		    NemoProgressInfo        *info,
		    gpointer                 data)
{
	(void) manager;
	g_signal_connect (info, "progress-changed", G_CALLBACK (cancel_once_writing), data);
}

/* A zip of one big file, stopped partway through it. */
static void
zip_cancel_round (gpointer data)
{
	ZipCase *zip = data;
	guint timeout_id;

	zip->done = FALSE;
	zip->succeeded = FALSE;
	zip->canceled = FALSE;
	timeout_id = g_timeout_add_seconds (JOB_TIMEOUT_SECONDS, give_up, NULL);
	nemo_archive_create (zip->sources, zip->destination, &zip->options,
			     GTK_WINDOW (zip->window), zip_done, zip);
	gtk_main ();
	if (zip->done) {
		g_source_remove (timeout_id);
	}
	drain_main_loop ();

	/* Stopped mid-entry every time, or the round proves nothing. */
	check (zip->canceled && !zip->succeeded);
}

/* Bytes that do not compress, so there is time to stop it. */
#define NOISE_BYTES (8 * 1024 * 1024)

static void
setup_zip (ZipCase *zip, const char *tmp)
{
	char *source = g_build_filename (tmp, "noise.bin", NULL);
	char *archive = g_build_filename (tmp, "noise.zip", NULL);
	GRand *rand = g_rand_new_with_seed (7);
	gsize n = NOISE_BYTES / sizeof (guint32);
	guint32 *words = g_new (guint32, n);
	gsize i;

	for (i = 0; i < n; i++) {
		words[i] = g_rand_int (rand);
	}
	check (g_file_set_contents (source, (const char *) words, NOISE_BYTES, NULL));

	zip->window = gtk_window_new (GTK_WINDOW_TOPLEVEL);
	zip->sources = g_list_append (NULL, g_file_new_for_path (source));
	zip->destination = g_file_new_for_path (archive);
	nemo_archive_options_init (&zip->options);
	zip->options.format = NEMO_ARCHIVE_FORMAT_ZIP;

	g_free (words);
	g_rand_free (rand);
	g_free (archive);
	g_free (source);
}

static void
teardown_zip (ZipCase *zip)
{
	nemo_archive_options_clear (&zip->options);
	g_list_free_full (zip->sources, g_object_unref);
	g_object_unref (zip->destination);
	gtk_widget_destroy (zip->window);
}

/* The smallest leak there is, once a round, is twice this. */
#define ANY_LEAK (TEST_HEAP_SMALLEST_BLOCK / 2)

/* Every zip, stopped or not, still grows the heap by about 150 bytes, which
   is backlog item 2026100307122400 and not a leak a sanitizer can see. What a
   stop left behind was a quarter of a megabyte. */
#define ZIP_LIMIT 1024

static int
run_case (const char *name, void (*op) (gpointer), gpointer data, gint64 limit)
{
	gint64 growth;

	growth = test_heap_growth (op, data, WARMUP_ROUNDS, COUNTED_ROUNDS);
	if (growth < 0) {
		g_print ("SKIP: the heap cannot be read here\n");
		return 77;
	}

	g_print ("%s: the heap grew %" G_GINT64_FORMAT " bytes over %d rounds\n",
		 name, growth, COUNTED_ROUNDS);
	check (growth < (gint64) COUNTED_ROUNDS * limit);

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

	/* The delete test guard stops to ask before every move. */
	g_setenv ("NEMO_TESTGUARD_ALL_DELETES", "0", TRUE);

	which = argc > 1 ? argv[1] : "progress";

	/* The clipboard is one per display, and the clipboard test beside this
	   one in the suite owns it too. */
	if (strcmp (which, "clipboard") == 0) {
		test_own_display (argc, argv, NULL);
	}
	tmp = test_scratch_config_home ("nemo-leaks-XXXXXX");

	if (!gtk_init_check (&argc, &argv)) {
		g_print ("SKIP: no display\n");
		g_free (tmp);
		return 77;
	}
	nemo_global_preferences_init ();
	manager = nemo_progress_info_manager_new ();

	if (strcmp (which, "progress") == 0) {
		skipped = run_case (which, progress_round, NULL, ANY_LEAK);
	} else if (strcmp (which, "move") == 0) {
		MoveCase move = { 0 };

		setup_move (&move, tmp);
		skipped = run_case (which, move_round, &move, ANY_LEAK);
		teardown_move (&move);
	} else if (strcmp (which, "clipboard") == 0) {
		ClipboardCase clip = { 0 };

		setup_clipboard (&clip, tmp);
		skipped = run_case (which, clipboard_round, &clip, ANY_LEAK);
		teardown_clipboard (&clip);
	} else if (strcmp (which, "zip-cancel") == 0) {
		ZipCase zip = { 0 };
		gulong watch_id;

		setup_zip (&zip, tmp);
		watch_id = g_signal_connect (manager, "new-progress-info",
					     G_CALLBACK (watch_new_progress), &zip);
		skipped = run_case (which, zip_cancel_round, &zip, ZIP_LIMIT);
		g_signal_handler_disconnect (manager, watch_id);
		teardown_zip (&zip);
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
