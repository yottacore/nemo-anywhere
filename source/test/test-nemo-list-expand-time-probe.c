/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */

/* test-nemo-list-expand-time-probe.c - times list view expanders from inside the program.

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

/* Preloaded into the real program by test-nemo-list-expand-time. Once the
 * window has gone quiet it opens each folder row's expander in turn, waiting
 * for quiet again before each, and times how long until its first row is in
 * and until all $NEMO_EXPAND_FILES of them are. One
 * "opened <name>|<first ms>|<all ms>" line per folder goes to
 * $NEMO_EXPAND_OUT, a "FAIL" line for one that never filled in, and "done" at
 * the end.
 *
 * Every answer of a listing after its first is held back, so the batches come
 * in the same order on any box: TRICKLE_MS for a folder named "folder ...",
 * and HOLD_MS for one named "slow ...". */

/* RTLD_NEXT. */
#define _GNU_SOURCE

#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <gtk/gtk.h>

static FILE         *out;
static int           files_wanted;
static GtkWidget    *window;
static GtkTreeView  *tree_view;
static GtkTreeModel *model;
static gint64        quiet_since;
static GList        *folders;	/* GtkTreeRowReference, one per folder row */
static GList        *next_folder;
static GtkTreeRowReference *opening;
static char         *opening_name;
static gint64        opened_at;
static gint64        first_at;
static int           step_ticks;

#define QUIET_US   (500 * 1000)
#define TRICKLE_MS 10
#define HOLD_MS    1000

typedef void (*NextFilesFunc) (GFileEnumerator *, int, int, GCancellable *,
			       GAsyncReadyCallback, gpointer);

typedef struct {
	GAsyncReadyCallback callback;
	gpointer            user_data;
	GObject            *source;
	GAsyncResult       *result;
} Held;

static gboolean
deliver (gpointer data)
{
	Held *held = data;

	held->callback (held->source, held->result, held->user_data);
	g_object_unref (held->source);
	g_object_unref (held->result);
	g_free (held);
	return G_SOURCE_REMOVE;
}

static void
hold_cb (GObject *source, GAsyncResult *result, gpointer data)
{
	Held *held = data;
	char *name = g_file_get_basename (g_file_enumerator_get_container (G_FILE_ENUMERATOR (source)));

	held->source = g_object_ref (source);
	held->result = g_object_ref (result);
	g_timeout_add (g_str_has_prefix (name, "slow ") ? HOLD_MS : TRICKLE_MS, deliver, held);
	g_free (name);
}

void
g_file_enumerator_next_files_async (GFileEnumerator *enumerator, int num_files, int io_priority,
				    GCancellable *cancellable, GAsyncReadyCallback callback,
				    gpointer user_data)
{
	static NextFilesFunc real;
	char *name;
	gboolean hold_back;

	if (real == NULL) {
		real = (NextFilesFunc) dlsym (RTLD_NEXT, "g_file_enumerator_next_files_async");
	}
	name = g_file_get_basename (g_file_enumerator_get_container (enumerator));
	hold_back = out != NULL && (g_str_has_prefix (name, "slow ") || g_str_has_prefix (name, "folder "));
	g_free (name);
	if (hold_back && g_object_get_data (G_OBJECT (enumerator), "expand-time-asked") != NULL) {
		Held *held = g_new0 (Held, 1);

		held->callback = callback;
		held->user_data = user_data;
		real (enumerator, num_files, io_priority, cancellable, hold_cb, held);
		return;
	}
	g_object_set_data (G_OBJECT (enumerator), "expand-time-asked", GINT_TO_POINTER (1));
	real (enumerator, num_files, io_priority, cancellable, callback, user_data);
}

static gboolean
is_a (gpointer object, const char *type_name)
{
	GType type = g_type_from_name (type_name);

	return type != 0 && g_type_is_a (G_OBJECT_TYPE (object), type);
}

typedef struct {
	const char *type_name;
	GtkWidget  *found;
} Find;

static void
find_cb (GtkWidget *widget, gpointer data)
{
	Find *find = data;

	if (find->found != NULL) {
		return;
	}
	if (is_a (widget, find->type_name) && gtk_widget_get_mapped (widget)) {
		find->found = widget;
	} else if (GTK_IS_CONTAINER (widget)) {
		gtk_container_forall (GTK_CONTAINER (widget), find_cb, data);
	}
}

static GtkWidget *
find_widget (GtkWidget *inside, const char *type_name)
{
	Find find = { type_name, NULL };

	find_cb (inside, &find);
	return find.found;
}

static void
finish (void)
{
	fprintf (out, "done\n");
	fflush (out);
	fclose (out);
	out = NULL;
}

/* The placeholder row has no file. */
static gboolean
real_rows_in (GtkTreeIter *parent)
{
	GtkTreeIter child;
	gpointer file = NULL;

	if (!gtk_tree_model_iter_children (model, &child, parent)) {
		return FALSE;
	}
	gtk_tree_model_get (model, &child, 0, &file, -1);
	if (file == NULL) {
		return FALSE;
	}
	g_object_unref (file);
	return TRUE;
}

static void
check_opened (void)
{
	GtkTreePath *path;
	GtkTreeIter parent;

	if (opening == NULL || out == NULL) {
		return;
	}
	path = gtk_tree_row_reference_get_path (opening);
	if (path == NULL || !gtk_tree_model_get_iter (model, &parent, path) || !real_rows_in (&parent)) {
		gtk_tree_path_free (path);
		return;
	}
	if (first_at == 0) {
		first_at = g_get_monotonic_time ();
	}
	if (gtk_tree_model_iter_n_children (model, &parent) == files_wanted) {
		fprintf (out, "opened %s|%.1f|%.1f\n", opening_name, (first_at - opened_at) / 1000.0,
			 (g_get_monotonic_time () - opened_at) / 1000.0);
		fflush (out);
		opening = NULL;
		g_clear_pointer (&opening_name, g_free);
		next_folder = next_folder->next;
		step_ticks = 0;
	}
	gtk_tree_path_free (path);
}

static void
row_cb (G_GNUC_UNUSED GtkTreeModel *m, GtkTreePath *path, G_GNUC_UNUSED GtkTreeIter *iter,
	G_GNUC_UNUSED gpointer data)
{
	quiet_since = g_get_monotonic_time ();
	if (gtk_tree_path_get_depth (path) > 1) {
		check_opened ();
	}
}

static void
open_folder (GtkTreeRowReference *ref)
{
	static char *(*file_name) (gpointer);
	GtkTreePath *path = gtk_tree_row_reference_get_path (ref);
	GtkTreeIter iter;
	gpointer file = NULL;

	if (file_name == NULL) {
		file_name = dlsym (RTLD_DEFAULT, "nemo_file_info_get_name");
	}
	if (file_name != NULL && gtk_tree_model_get_iter (model, &iter, path)) {
		gtk_tree_model_get (model, &iter, 0, &file, -1);
	}
	opening_name = file != NULL ? file_name (file) : g_strdup ("?");
	g_clear_object (&file);

	opening = ref;
	first_at = 0;
	opened_at = g_get_monotonic_time ();
	gtk_tree_view_expand_row (tree_view, path, FALSE);
	gtk_tree_path_free (path);
}

static gboolean
tick (G_GNUC_UNUSED gpointer data)
{
	GtkTreeIter iter;

	if (out == NULL) {
		return G_SOURCE_REMOVE;
	}

	if (window == NULL) {
		GList *toplevels = gtk_window_list_toplevels (), *l;

		for (l = toplevels; l != NULL; l = l->next) {
			if (is_a (l->data, "NemoWindow") && gtk_widget_get_mapped (l->data)) {
				window = l->data;
			}
		}
		g_list_free (toplevels);
		return G_SOURCE_CONTINUE;
	}

	if (tree_view == NULL) {
		GtkWidget *view = find_widget (window, "NemoView");
		GtkWidget *found = view != NULL ? find_widget (view, "GtkTreeView") : NULL;

		if (found == NULL || !is_a (gtk_tree_view_get_model (GTK_TREE_VIEW (found)), "NemoListModel")) {
			return G_SOURCE_CONTINUE;
		}
		tree_view = GTK_TREE_VIEW (found);
		model = gtk_tree_view_get_model (tree_view);
		g_signal_connect (model, "row-inserted", G_CALLBACK (row_cb), NULL);
		g_signal_connect (model, "row-changed", G_CALLBACK (row_cb), NULL);
		quiet_since = g_get_monotonic_time ();
		return G_SOURCE_CONTINUE;
	}

	if (folders == NULL) {
		/* The first folder, its item counts included, has to be still for a
		 * second, so nothing of its load is timed with an expander. */
		if (g_get_monotonic_time () - quiet_since < G_USEC_PER_SEC ||
		    !gtk_tree_model_get_iter_first (model, &iter)) {
			return G_SOURCE_CONTINUE;
		}
		do {
			if (gtk_tree_model_iter_has_child (model, &iter)) {
				GtkTreePath *path = gtk_tree_model_get_path (model, &iter);

				folders = g_list_append (folders, gtk_tree_row_reference_new (model, path));
				gtk_tree_path_free (path);
			}
		} while (gtk_tree_model_iter_next (model, &iter));
		if (folders == NULL) {
			fprintf (out, "FAIL no folder row has an expander\n");
			finish ();
			return G_SOURCE_REMOVE;
		}
		next_folder = folders;
	}

	if (opening != NULL) {
		/* Ten seconds is a hang, not a slow box, held answers and all. */
		if (++step_ticks > 200) {
			fprintf (out, "FAIL a folder never showed all its rows\n");
			finish ();
			return G_SOURCE_REMOVE;
		}
		return G_SOURCE_CONTINUE;
	}

	if (next_folder == NULL) {
		finish ();
		return G_SOURCE_REMOVE;
	}

	/* Still for a while first, so no update already on its way is timed
	 * with the next folder. Twenty seconds of rows changing is a fault. */
	if (g_get_monotonic_time () - quiet_since < QUIET_US) {
		if (++step_ticks > 400) {
			fprintf (out, "FAIL the rows never stopped changing\n");
			finish ();
			return G_SOURCE_REMOVE;
		}
		return G_SOURCE_CONTINUE;
	}
	step_ticks = 0;
	open_folder (next_folder->data);
	return G_SOURCE_CONTINUE;
}

__attribute__((constructor)) static void
probe_start (void)
{
	const char *path = g_getenv ("NEMO_EXPAND_OUT");
	const char *wanted = g_getenv ("NEMO_EXPAND_FILES");

	if (path == NULL || wanted == NULL) {
		return;
	}
	out = fopen (path, "w");
	if (out == NULL) {
		return;
	}
	files_wanted = atoi (wanted);
	/* Thumbnailers and helpers the program starts would open the file again
	 * and wipe it. */
	g_unsetenv ("NEMO_EXPAND_OUT");
	g_unsetenv ("LD_PRELOAD");
	g_timeout_add (50, tick, NULL);
}
