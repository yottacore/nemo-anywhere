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
#include <glib/gstdio.h>
#include <gtk/gtk.h>

#include <libegg/eggtreemultidnd.h>
#include <libnemo-private/nemo-archive.h>
#include <libnemo-private/nemo-clipboard.h>
#include <libnemo-private/nemo-config.h>
#include <libnemo-private/nemo-directory.h>
#include <libnemo-private/nemo-extract.h>
#include <libnemo-private/nemo-file.h>
#include <libnemo-private/nemo-file-digest.h>
#include <libnemo-private/nemo-file-operations.h>
#include <libnemo-private/nemo-global-preferences.h>
#include <libnemo-private/nemo-progress-info.h>
#include <libnemo-private/nemo-progress-info-manager.h>
#include <libnemo-private/nemo-query.h>

#include "nemo-list-model.h"
#include "nemo-query-editor.h"

/* Taken in whole, since a load of the file is only started from inside it:
   by its file monitor, which is seconds slow, or at start. */
#include "nemo-bookmark-list.c"

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

/* An archive of one big file, stopped partway through it. */
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
setup_zip (ZipCase *zip, const char *tmp, NemoArchiveFormat format)
{
	char *source = g_build_filename (tmp, "noise.bin", NULL);
	char *archive = g_build_filename (tmp, "noise.archive", NULL);
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
	zip->options.format = format;

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

typedef struct {
	GtkWidget *window;
	GList *archives;
	GFile *destination;
	char *unpacked;
	gboolean done;
	gboolean succeeded;
} ExtractCase;

static void
extract_done (GFile *destination_dir, gboolean success, gpointer data)
{
	ExtractCase *extract = data;

	(void) destination_dir;
	extract->succeeded = success;
	extract->done = TRUE;
	gtk_main_quit ();
}

/* A small zip unpacked, and what came out taken away again, so every round
   unpacks under the same name. */
static void
extract_round (gpointer data)
{
	ExtractCase *extract = data;
	guint timeout_id;

	extract->done = FALSE;
	extract->succeeded = FALSE;
	timeout_id = g_timeout_add_seconds (JOB_TIMEOUT_SECONDS, give_up, NULL);
	nemo_extract_files (extract->archives, extract->destination, NEMO_EXTRACT_HERE,
			    GTK_WINDOW (extract->window), extract_done, extract);
	gtk_main ();
	if (extract->done) {
		g_source_remove (timeout_id);
	}
	drain_main_loop ();

	check (extract->succeeded);
	check (g_remove (extract->unpacked) == 0);
}

/* The zip comes from the compress job, as a user's would. */
static void
setup_extract (ExtractCase *extract, const char *tmp)
{
	char *source = g_build_filename (tmp, "packed.txt", NULL);
	char *archive = g_build_filename (tmp, "packed.zip", NULL);
	char *out = g_build_filename (tmp, "out", NULL);
	ZipCase zip = { 0 };
	guint timeout_id;

	check (g_file_set_contents (source, "packed", -1, NULL));
	check (g_mkdir_with_parents (out, 0700) == 0);

	extract->window = gtk_window_new (GTK_WINDOW_TOPLEVEL);
	zip.sources = g_list_append (NULL, g_file_new_for_path (source));
	zip.destination = g_file_new_for_path (archive);
	nemo_archive_options_init (&zip.options);
	zip.options.format = NEMO_ARCHIVE_FORMAT_ZIP;
	timeout_id = g_timeout_add_seconds (JOB_TIMEOUT_SECONDS, give_up, NULL);
	nemo_archive_create (zip.sources, zip.destination, &zip.options,
			     GTK_WINDOW (extract->window), zip_done, &zip);
	gtk_main ();
	if (zip.done) {
		g_source_remove (timeout_id);
	}
	check (zip.succeeded);
	nemo_archive_options_clear (&zip.options);
	g_list_free_full (zip.sources, g_object_unref);

	extract->archives = g_list_append (NULL, zip.destination);
	extract->destination = g_file_new_for_path (out);
	extract->unpacked = g_build_filename (out, "packed.txt", NULL);

	g_free (out);
	g_free (archive);
	g_free (source);
}

static void
teardown_extract (ExtractCase *extract)
{
	g_list_free_full (extract->archives, g_object_unref);
	g_object_unref (extract->destination);
	g_free (extract->unpacked);
	gtk_widget_destroy (extract->window);
}

typedef struct {
	GtkWidget *window;
	GtkWidget *editor;
	NemoQuery *query;
} QueryCase;

/* What a search tab does when it opens or its query changes. */
static void
query_round (gpointer data)
{
	QueryCase *search = data;

	nemo_query_editor_set_query (NEMO_QUERY_EDITOR (search->editor), search->query);
}

static void
setup_query (QueryCase *search, const char *tmp)
{
	char *uri = g_filename_to_uri (tmp, NULL, NULL);

	search->editor = nemo_query_editor_new ();
	search->window = gtk_window_new (GTK_WINDOW_TOPLEVEL);
	gtk_container_add (GTK_CONTAINER (search->window), search->editor);
	search->query = nemo_query_new ();
	nemo_query_set_file_pattern (search->query, "report");
	nemo_query_set_content_pattern (search->query, "total");
	nemo_query_set_location (search->query, uri);
	g_free (uri);
}

static void
teardown_query (QueryCase *search)
{
	g_object_unref (search->query);
	gtk_widget_destroy (search->window);
}

static void
wait_for_bookmark_ops (NemoBookmarkList *bookmarks)
{
	gint64 give_up_at = g_get_monotonic_time () + JOB_TIMEOUT_SECONDS * G_USEC_PER_SEC;

	while (!g_queue_is_empty (bookmarks->pending_ops)) {
		if (g_get_monotonic_time () > give_up_at) {
			g_printerr ("FAIL: a bookmarks load did not finish within %d seconds\n",
				    JOB_TIMEOUT_SECONDS);
			failures++;
			return;
		}
		g_main_context_iteration (NULL, TRUE);
	}
}

/* A load with no bookmarks file and no metadata file, as on a first run, or
   after the file is removed. */
static void
bookmarks_round (gpointer data)
{
	NemoBookmarkList *bookmarks = data;

	nemo_bookmark_list_load_file (bookmarks);
	wait_for_bookmark_ops (bookmarks);
}

/* Spins until *flag is set, or gives up and fails. */
static void
wait_for_flag (const gboolean *flag, const char *what)
{
	gint64 give_up_at = g_get_monotonic_time () + JOB_TIMEOUT_SECONDS * G_USEC_PER_SEC;

	while (!*flag) {
		if (g_get_monotonic_time () > give_up_at) {
			g_printerr ("FAIL: %s did not happen within %d seconds\n",
				    what, JOB_TIMEOUT_SECONDS);
			failures++;
			return;
		}
		if (!g_main_context_iteration (NULL, FALSE)) {
			g_usleep (1000);
		}
	}
}

/* 2026101913541100: GLib's file monitor on Windows leaks the new name of
   every rename it sees, and each round has three. */
#ifdef G_OS_WIN32
#define MONITOR_LEAKS_RENAMES TRUE
#else
#define MONITOR_LEAKS_RENAMES FALSE
#endif

/* 2026101913541101: on Windows, loading a bookmark makes the process heap move
   by a few 4 KB COM buffers either way, which is more than a leak of a block a
   round adds up to. */
#ifdef G_OS_WIN32
#define BOOKMARK_LOAD_NOISY TRUE
#else
#define BOOKMARK_LOAD_NOISY FALSE
#endif

typedef struct {
	NemoConfigGroup *group;
	char *path;
	guint round;
	gboolean changed;
} SettingsCase;

static void
settings_changed (NemoConfigGroup *group, const char *key, gpointer data)
{
	SettingsCase *settings = data;

	(void) group;
	(void) key;
	settings->changed = TRUE;
}

/* A setting changed in the app and saved, then the file edited by hand and
   read back while the app runs. The values are all one length, so each round
   asks for the same sizes. */
static void
settings_round (gpointer data)
{
	SettingsCase *settings = data;
	const char *ours = settings->round % 2 == 0 ? "640x480+10+10" : "640x480+20+20";
	const char *theirs = settings->round % 2 == 0 ? "640x480+30+30" : "640x480+40+40";
	char *text = NULL;
	char *at = NULL;
	char *now;

	settings->round++;
	nemo_config_set_string (settings->group, "geometry", ours);
	nemo_config_flush ();

	check (g_file_get_contents (settings->path, &text, NULL, NULL));
	if (text != NULL) {
		at = strstr (text, ours);
	}
	check (at != NULL);
	if (at == NULL) {
		g_free (text);
		return;
	}
	memcpy (at, theirs, strlen (theirs));

	settings->changed = FALSE;
	check (g_file_set_contents (settings->path, text, -1, NULL));
	g_free (text);
	wait_for_flag (&settings->changed, "a reload of the hand edit");

	now = nemo_config_get_string (settings->group, "geometry");
	check (g_strcmp0 (now, theirs) == 0);
	g_free (now);
	drain_main_loop ();
}

static void
setup_settings (SettingsCase *settings)
{
	settings->group = nemo_config_get_group ("window-state");
	settings->path = nemo_config_get_path ();
	g_signal_connect (settings->group, "changed::geometry",
			  G_CALLBACK (settings_changed), settings);
}

static void
teardown_settings (SettingsCase *settings)
{
	g_signal_handlers_disconnect_by_data (settings->group, settings);
	g_free (settings->path);
}

typedef struct {
	GFile *file;
	guint8 digest[NEMO_CACHE_DIGEST_LEN];
} ChecksumCase;

/* The checksum a thumbnail takes of every file it reads. */
static void
checksum_round (gpointer data)
{
	ChecksumCase *sum = data;
	guint8 digest[NEMO_CACHE_DIGEST_LEN] = { 0 };

	check (nemo_file_digest_file (sum->file, digest, NULL, NULL));
	check (memcmp (digest, sum->digest, sizeof (digest)) == 0);
}

/* A meg and a bit, so blake3 gets whole batches and a short tail. */
#define CHECKSUM_BYTES (1024 * 1024 + 4099)

static void
setup_checksum (ChecksumCase *sum, const char *tmp)
{
	char *path = g_build_filename (tmp, "summed.bin", NULL);
	GRand *rand = g_rand_new_with_seed (11);
	guint8 *bytes = g_malloc (CHECKSUM_BYTES);
	gsize i;

	for (i = 0; i < CHECKSUM_BYTES; i++) {
		bytes[i] = (guint8) g_rand_int_range (rand, 0, 256);
	}
	check (g_file_set_contents (path, (const char *) bytes, CHECKSUM_BYTES, NULL));
	nemo_file_digest_bytes (bytes, CHECKSUM_BYTES, sum->digest);
	sum->file = g_file_new_for_path (path);

	g_free (bytes);
	g_rand_free (rand);
	g_free (path);
}

typedef struct {
	GtkWidget *window;
	GtkWidget *tree;
	GtkWidget *receiver;
	NemoListModel *model;
	NemoColumn *column;
	NemoDirectory *directory;
	GList *files;
	GtkTargetList *targets;
	gboolean received;
	gboolean ended;
	gboolean context_gone;
	GdkWindow *drag_window;
	int client;
} DragCase;

/* The list view's own drag handlers are static, so these do what they do:
   the selected rows are kept on the drag, its icon is the first row's, and
   the data is asked for through libegg's interface on the model. */
static void
drag_case_rows_free (GList *rows)
{
	g_list_free_full (rows, (GDestroyNotify) gtk_tree_row_reference_free);
}

static void
drag_case_begin (GtkWidget *widget, GdkDragContext *context, gpointer data)
{
	DragCase *drag = data;
	GtkTreeSelection *selection = gtk_tree_view_get_selection (GTK_TREE_VIEW (widget));
	GtkTreeModel *model = GTK_TREE_MODEL (drag->model);
	GList *paths = gtk_tree_selection_get_selected_rows (selection, NULL);
	GList *rows = NULL;
	GList *l;

	for (l = paths; l != NULL; l = l->next) {
		rows = g_list_prepend (rows, gtk_tree_row_reference_new (model, l->data));
	}
	g_object_set_data_full (G_OBJECT (context), "drag-info", g_list_reverse (rows),
				(GDestroyNotify) drag_case_rows_free);

	if (paths != NULL) {
		GtkTreeIter iter;
		cairo_surface_t *icon = NULL;

		if (gtk_tree_model_get_iter (model, &iter, paths->data)) {
			gtk_tree_model_get (model, &iter, NEMO_LIST_MODEL_SMALL_ICON_COLUMN, &icon, -1);
		}
		if (icon != NULL) {
			gtk_drag_set_icon_surface (context, icon);
			cairo_surface_destroy (icon);
		} else {
			gtk_drag_set_icon_default (context);
		}
	}
	g_list_free_full (paths, (GDestroyNotify) gtk_tree_path_free);
}

static gint
drag_case_icon_scale (NemoListModel *model, gpointer data)
{
	DragCase *drag = data;

	(void) model;
	return gtk_widget_get_scale_factor (drag->tree);
}

static void
drag_case_data_get (GtkWidget *widget, GdkDragContext *context, GtkSelectionData *selection,
		    guint info, guint time, gpointer data)
{
	DragCase *drag = data;
	GList *rows = g_object_get_data (G_OBJECT (context), "drag-info");

	(void) widget;
	(void) info;
	(void) time;
	if (rows != NULL) {
		egg_tree_multi_drag_source_drag_data_get (EGG_TREE_MULTI_DRAG_SOURCE (drag->model),
							  rows, selection);
	}
}

static void
drag_case_end (GtkWidget *widget, GdkDragContext *context, gpointer data)
{
	DragCase *drag = data;

	(void) widget;
	(void) context;
	drag->ended = TRUE;
}

/* What a drop target hears back when it asks for the files. */
static void
drag_case_received (GtkWidget *widget, GtkSelectionData *selection, guint time, gpointer data)
{
	DragCase *drag = data;
	const char *text = (const char *) gtk_selection_data_get_data (selection);
	GList *l;

	(void) widget;
	(void) time;
	check (text != NULL && gtk_selection_data_get_length (selection) > 0);
	for (l = drag->files; text != NULL && l != NULL; l = l->next) {
		char *name = nemo_file_get_name (l->data);

		check (strstr (text, name) != NULL);
		g_free (name);
	}
	drag->received = TRUE;
}

static void
drag_case_context_gone (gpointer data, GObject *where_it_was)
{
	DragCase *drag = data;

	(void) where_it_was;
	drag->context_gone = TRUE;
}

/* Rows dragged, the files asked for once, then the drag stopped short, as
   with Escape. */
static void
drag_round (gpointer data)
{
	DragCase *drag = data;
	GdkDragContext *context;

	drag->received = FALSE;
	drag->ended = FALSE;
	context = gtk_drag_begin_with_coordinates (drag->tree, drag->targets,
						   GDK_ACTION_MOVE | GDK_ACTION_COPY | GDK_ACTION_LINK | GDK_ACTION_ASK,
						   1, NULL, 4, 4);
	check (context != NULL);
	if (context == NULL) {
		return;
	}

	drag->drag_window = gdk_drag_context_get_drag_window (context);
	if (drag->drag_window != NULL) {
		g_object_add_weak_pointer (G_OBJECT (drag->drag_window), (gpointer *) &drag->drag_window);
	}

	gtk_selection_convert (drag->receiver, gdk_drag_get_selection (context),
			       gdk_atom_intern_static_string ("text/uri-list"), GDK_CURRENT_TIME);
	wait_for_flag (&drag->received, "the dragged files' answer");

	drag->context_gone = FALSE;
	g_object_weak_ref (G_OBJECT (context), drag_case_context_gone, drag);
	gtk_drag_cancel (context);
	wait_for_flag (&drag->ended, "the end of a stopped drag");
	/* GDK keeps the context, and the icon window with it, until the icon's
	   snap-back is over, some way past drag-end. Unwaited, a reading counts
	   however many are still flying, and on a loaded box that's a lot. */
	wait_for_flag (&drag->context_gone, "the stopped drag's context to go");
	/* Its drag window is let go on the server's DestroyNotify. */
	gdk_display_sync (gtk_widget_get_display (drag->tree));
	drain_main_loop ();

	/* 2026101916044103: GTK's icon window refs the drag window and never
	   lets go, so one is kept per drag. Dropped here, and only while it's
	   still alive, so the rounds count what's ours and libegg's. */
	if (drag->drag_window != NULL) {
		GdkWindow *kept = drag->drag_window;

		g_object_remove_weak_pointer (G_OBJECT (kept), (gpointer *) &drag->drag_window);
		drag->drag_window = NULL;
		g_object_unref (kept);
	}
}

static gboolean
drag_case_loaded (NemoDirectory *directory)
{
	gint64 give_up_at = g_get_monotonic_time () + JOB_TIMEOUT_SECONDS * G_USEC_PER_SEC;

	while (!nemo_directory_are_all_files_seen (directory)) {
		if (g_get_monotonic_time () > give_up_at) {
			return FALSE;
		}
		if (!g_main_context_iteration (NULL, FALSE)) {
			g_usleep (1000);
		}
	}

	return TRUE;
}

static void
setup_drag (DragCase *drag, const char *tmp)
{
	const char *names[] = { "one.txt", "two.txt", "three.txt" };
	char *dir = g_build_filename (tmp, "rows", NULL);
	char *uri;
	GtkWidget *scrolled;
	guint i;

	check (g_mkdir_with_parents (dir, 0700) == 0);
	for (i = 0; i < G_N_ELEMENTS (names); i++) {
		g_free (make_file_uri (dir, names[i]));
	}

	uri = g_filename_to_uri (dir, NULL, NULL);
	drag->directory = nemo_directory_get_by_uri (uri);
	nemo_directory_file_monitor_add (drag->directory, &drag->client, TRUE,
					 NEMO_FILE_ATTRIBUTE_INFO, NULL, NULL);
	check (drag_case_loaded (drag->directory));

	drag->model = g_object_new (NEMO_TYPE_LIST_MODEL, NULL);
	/* The model borrows its columns. */
	drag->column = nemo_column_new ("name", "name", "Name", "");
	nemo_list_model_add_column (drag->model, drag->column);
	for (i = 0; i < G_N_ELEMENTS (names); i++) {
		char *path = g_build_filename (dir, names[i], NULL);
		char *file_uri = g_filename_to_uri (path, NULL, NULL);
		NemoFile *file = nemo_file_get_by_uri (file_uri);

		check (nemo_list_model_add_file (drag->model, file, drag->directory));
		drag->files = g_list_append (drag->files, file);
		g_free (file_uri);
		g_free (path);
	}

	drag->window = gtk_window_new (GTK_WINDOW_TOPLEVEL);
	gtk_window_set_default_size (GTK_WINDOW (drag->window), 300, 200);
	scrolled = gtk_scrolled_window_new (NULL, NULL);
	drag->tree = gtk_tree_view_new_with_model (GTK_TREE_MODEL (drag->model));
	gtk_tree_view_insert_column_with_attributes (GTK_TREE_VIEW (drag->tree), -1, "Name",
						     gtk_cell_renderer_text_new (), NULL);
	gtk_container_add (GTK_CONTAINER (scrolled), drag->tree);
	gtk_container_add (GTK_CONTAINER (drag->window), scrolled);
	nemo_list_model_set_drag_view (drag->model, GTK_TREE_VIEW (drag->tree), 4, 4);
	gtk_tree_selection_set_mode (gtk_tree_view_get_selection (GTK_TREE_VIEW (drag->tree)),
				     GTK_SELECTION_MULTIPLE);
	gtk_tree_selection_select_all (gtk_tree_view_get_selection (GTK_TREE_VIEW (drag->tree)));
	drag->targets = nemo_list_model_get_drag_target_list ();

	g_signal_connect (drag->model, "get-icon-scale", G_CALLBACK (drag_case_icon_scale), drag);
	g_signal_connect (drag->tree, "drag-begin", G_CALLBACK (drag_case_begin), drag);
	g_signal_connect (drag->tree, "drag-data-get", G_CALLBACK (drag_case_data_get), drag);
	g_signal_connect (drag->tree, "drag-end", G_CALLBACK (drag_case_end), drag);

	drag->receiver = gtk_invisible_new ();
	gtk_widget_realize (drag->receiver);
	g_signal_connect (drag->receiver, "selection-received", G_CALLBACK (drag_case_received), drag);

	/* A drag grabs the pointer, which takes a window on screen. */
	gtk_widget_show_all (drag->window);
	while (!gtk_widget_get_mapped (drag->window) && g_main_context_iteration (NULL, TRUE)) {
	}
	drain_main_loop ();

	g_free (uri);
	g_free (dir);
}

static void
teardown_drag (DragCase *drag)
{
	gtk_widget_destroy (drag->receiver);
	gtk_widget_destroy (drag->window);
	gtk_target_list_unref (drag->targets);
	nemo_list_model_clear (drag->model);
	g_object_unref (drag->model);
	g_object_unref (drag->column);
	nemo_file_list_free (drag->files);
	nemo_directory_file_monitor_remove (drag->directory, &drag->client);
	nemo_directory_unref (drag->directory);
}

/* With what the thread itself takes, a little under what a leak of one block
   a round adds up to over the counted rounds. */
#define WORKER_HOLDS 768

typedef struct {
	GThread *thread;
	GMutex lock;
	GCond changed;
	gboolean holding;
	gboolean let_go;
} Worker;

static gpointer
worker_main (gpointer data)
{
	Worker *worker = data;
	char *held = g_malloc0 (WORKER_HOLDS);

	g_mutex_lock (&worker->lock);
	worker->holding = TRUE;
	g_cond_broadcast (&worker->changed);
	while (!worker->let_go) {
		g_cond_wait (&worker->changed, &worker->lock);
	}
	g_mutex_unlock (&worker->lock);
	g_free (held);

	return NULL;
}

/* Returns once the worker holds its memory, so it counts in this round. */
static void
worker_start (Worker *worker)
{
	g_mutex_init (&worker->lock);
	g_cond_init (&worker->changed);
	worker->thread = g_thread_new ("leak-self-check", worker_main, worker);
	g_mutex_lock (&worker->lock);
	while (!worker->holding) {
		g_cond_wait (&worker->changed, &worker->lock);
	}
	g_mutex_unlock (&worker->lock);
}

static void
worker_end (Worker *worker)
{
	if (worker->thread == NULL) {
		return;
	}
	g_mutex_lock (&worker->lock);
	worker->let_go = TRUE;
	g_cond_broadcast (&worker->changed);
	g_mutex_unlock (&worker->lock);
	g_thread_join (worker->thread);
	worker->thread = NULL;
	g_mutex_clear (&worker->lock);
	g_cond_clear (&worker->changed);
}

typedef struct {
	guint round;
	gpointer leaked;
	Worker first;
	Worker second;
} SelfCheck;

/* Leaks one small block a round. A thread starts in each of the first two
   counts and the first one ends in the third, giving back more than half of
   what the leak took there. Taking the third count as it came, the leak got
   through. */
static void
self_check_round (gpointer data)
{
	SelfCheck *self = data;
	gpointer *block = g_malloc (3 * sizeof (gpointer));
	guint counted = self->round - WARMUP_ROUNDS;

	*block = self->leaked;
	self->leaked = block;

	if (self->round >= WARMUP_ROUNDS) {
		if (counted == 0) {
			worker_start (&self->first);
		} else if (counted == COUNTED_ROUNDS + COUNTED_ROUNDS / 2) {
			worker_start (&self->second);
		} else if (counted == 2 * COUNTED_ROUNDS + COUNTED_ROUNDS / 2) {
			worker_end (&self->first);
		}
	}
	self->round++;
}

static void
teardown_self_check (SelfCheck *self)
{
	worker_end (&self->first);
	worker_end (&self->second);
	while (self->leaked != NULL) {
		gpointer *block = self->leaked;

		self->leaked = *block;
		g_free (block);
	}
}

/* The smallest leak there is, once a round, is twice this. */
#define ANY_LEAK (TEST_HEAP_SMALLEST_BLOCK / 2)

static int
run_case_expecting (const char *name, void (*op) (gpointer), gpointer data,
		    gint64 limit, gboolean leaks)
{
	gint64 growth;

	switch (test_heap_growth (op, data, WARMUP_ROUNDS, COUNTED_ROUNDS,
				  (gint64) COUNTED_ROUNDS * limit, &growth)) {
	case TEST_HEAP_READ:
		break;
	case TEST_HEAP_UNREADABLE:
		g_print ("SKIP: the heap cannot be read here\n");
		return 77;
	case TEST_HEAP_UNSETTLED:
	default:
		g_printerr ("FAIL: a thread started or ended in every reading\n");
		failures++;
		return 0;
	}

	g_print ("%s: the heap grew %" G_GINT64_FORMAT " bytes over %d rounds\n",
		 name, growth, COUNTED_ROUNDS);
	if (leaks) {
		check (growth >= (gint64) COUNTED_ROUNDS * limit);
	} else {
		check (growth < (gint64) COUNTED_ROUNDS * limit);
	}

	return 0;
}

static int
run_case (const char *name, void (*op) (gpointer), gpointer data, gint64 limit)
{
	return run_case_expecting (name, op, data, limit, FALSE);
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
	   one in the suite owns it too. A drag grabs the pointer. */
	if (strcmp (which, "clipboard") == 0 || strcmp (which, "drag") == 0) {
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
	} else if (strcmp (which, "zip-cancel") == 0 || strcmp (which, "tar.gz-cancel") == 0 ||
		   strcmp (which, "7z-cancel") == 0) {
		/* A tar fails what is written after a stop, where a zip drops it,
		   and the 7z writer is closed as failed. Each way has to leave
		   nothing behind. */
		NemoArchiveFormat format = NEMO_ARCHIVE_FORMAT_ZIP;
		ZipCase zip = { 0 };
		gulong watch_id;

		if (strcmp (which, "tar.gz-cancel") == 0) {
			format = NEMO_ARCHIVE_FORMAT_TAR_GZ;
		} else if (strcmp (which, "7z-cancel") == 0) {
			format = NEMO_ARCHIVE_FORMAT_7Z;
		}
		setup_zip (&zip, tmp, format);
		/* Or a 7z goes to the 7-Zip program, where it is installed. */
		if (format == NEMO_ARCHIVE_FORMAT_7Z) {
			zip.options.links.symlinks = ARC_LINK_IGNORE;
			nemo_archive_hide_backend (NEMO_ARCHIVE_BACKEND_7Z, TRUE);
			check (nemo_archive_pick_backend (format, &zip.options) ==
			       NEMO_ARCHIVE_BACKEND_LIBARCHIVE);
		}
		watch_id = g_signal_connect (manager, "new-progress-info",
					     G_CALLBACK (watch_new_progress), &zip);
		skipped = run_case (which, zip_cancel_round, &zip, ANY_LEAK);
		g_signal_handler_disconnect (manager, watch_id);
		teardown_zip (&zip);
		nemo_archive_hide_backend (NEMO_ARCHIVE_BACKEND_7Z, FALSE);
	} else if (strcmp (which, "extract") == 0) {
		ExtractCase extract = { 0 };

		setup_extract (&extract, tmp);
		skipped = run_case (which, extract_round, &extract, ANY_LEAK);
		teardown_extract (&extract);
	} else if (strcmp (which, "query-editor") == 0) {
		QueryCase search = { 0 };

		setup_query (&search, tmp);
		skipped = run_case (which, query_round, &search, ANY_LEAK);
		teardown_query (&search);
	} else if (strcmp (which, "bookmarks-file") == 0 && BOOKMARK_LOAD_NOISY) {
		g_print ("SKIP: the heap moves on its own here while a bookmark loads\n");
		skipped = 77;
	} else if (strcmp (which, "bookmarks") == 0 || strcmp (which, "bookmarks-file") == 0) {
		NemoBookmarkList *bookmarks;

		/* With a bookmarks file, the load goes on to the metadata file,
		   which is missing in the usual case. */
		if (strcmp (which, "bookmarks-file") == 0) {
			GFile *file = nemo_bookmark_list_get_file ();
			GFile *parent = g_file_get_parent (file);
			char *dir = g_file_get_path (parent);
			char *path = g_file_get_path (file);
			char *uri = g_filename_to_uri (tmp, NULL, NULL);
			char *line = g_strdup_printf ("%s Scratch\n", uri);

			check (g_mkdir_with_parents (dir, 0700) == 0);
			check (g_file_set_contents (path, line, -1, NULL));
			g_free (uri);
			g_free (line);
			g_free (path);
			g_free (dir);
			g_object_unref (parent);
			g_object_unref (file);
		}

		bookmarks = nemo_bookmark_list_get_default ();
		wait_for_bookmark_ops (bookmarks);
		if (strcmp (which, "bookmarks-file") == 0) {
			check (nemo_bookmark_list_length (bookmarks) == 1);
		}
		skipped = run_case (which, bookmarks_round, bookmarks, ANY_LEAK);
	} else if (strcmp (which, "settings") == 0) {
		SettingsCase settings = { 0 };

		if (MONITOR_LEAKS_RENAMES) {
			g_print ("SKIP: GLib leaks a name on every rename seen here\n");
			skipped = 77;
		} else {
			setup_settings (&settings);
			skipped = run_case (which, settings_round, &settings, ANY_LEAK);
			teardown_settings (&settings);
		}
	} else if (strcmp (which, "checksum") == 0) {
		ChecksumCase sum = { 0 };

		setup_checksum (&sum, tmp);
		skipped = run_case (which, checksum_round, &sum, ANY_LEAK);
		g_object_unref (sum.file);
	} else if (strcmp (which, "drag") == 0) {
		DragCase drag = { 0 };

		/* A service session on Windows has no monitor, and no window
		   there is ever shown, so a drag can't take the pointer. */
		if (gdk_display_get_n_monitors (gdk_display_get_default ()) == 0) {
			g_print ("SKIP: no monitor\n");
			skipped = 77;
		} else {
			setup_drag (&drag, tmp);
			skipped = run_case (which, drag_round, &drag, ANY_LEAK);
			teardown_drag (&drag);
		}
	} else if (strcmp (which, "self-check") == 0) {
		SelfCheck self = { 0 };

		skipped = run_case_expecting (which, self_check_round, &self, ANY_LEAK, TRUE);
		teardown_self_check (&self);
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
