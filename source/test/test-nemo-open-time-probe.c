/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */

/* test-nemo-open-time-probe.c - times going into folders from inside the program.

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

/* Preloaded into the real program by test-nemo-open-time. The list view starts
 * on a folder holding the folders named in $NEMO_OPEN_STEPS, split on "|".
 * Once the view has gone quiet, and the next one's row shows its item count,
 * the probe goes into it the way a double click does, and times how long until
 * its first row is in and until all $NEMO_OPEN_FILES of them are. Then it reads
 * the item count of a folder named "inner" in there, if there is one, and goes
 * back up with Alt+Up.
 *
 * To $NEMO_OPEN_OUT it writes one "count <name>|<size text>" line per folder
 * row it reads, one "opened <name>|<first ms>|<all ms>" line per folder, a
 * "FAIL" line for one that never filled in, and "done" at the end.
 *
 * Every answer of the listing of the folder being gone into after its first is
 * held back, so the batches come in the same order on any box: HOLD_MS for a
 * folder whose name starts "slow", and TRICKLE_MS for the others. */

/* RTLD_NEXT. */
#define _GNU_SOURCE

#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <gtk/gtk.h>

typedef enum {
	AT_TOP,		/* waiting to go into steps[step] */
	OPENING,	/* timing its rows */
	INSIDE,		/* reading "inner", then going up */
	GOING_UP,
} Stage;

static FILE         *out;
static int           files_wanted;
static char        **steps;
static int           step;
static Stage         stage;
static GtkWidget    *window;
static GtkTreeView  *tree_view;
static GtkTreeModel *model;
static gint64        quiet_since;
static gboolean      counted;
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
	g_timeout_add (g_str_has_prefix (name, "slow") ? HOLD_MS : TRICKLE_MS, deliver, held);
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
	/* Only the listing of the folder being gone into. Its sub-folders'
	   item counts are read, not timed. */
	hold_back = out != NULL && stage == OPENING && g_strcmp0 (name, steps[step]) == 0;
	g_free (name);
	if (hold_back && g_object_get_data (G_OBJECT (enumerator), "open-time-asked") != NULL) {
		Held *held = g_new0 (Held, 1);

		held->callback = callback;
		held->user_data = user_data;
		real (enumerator, num_files, io_priority, cancellable, hold_cb, held);
		return;
	}
	g_object_set_data (G_OBJECT (enumerator), "open-time-asked", GINT_TO_POINTER (1));
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

static gboolean
find_row (const char *name, GtkTreeIter *iter)
{
	static char *(*file_name) (gpointer);

	if (file_name == NULL) {
		file_name = dlsym (RTLD_DEFAULT, "nemo_file_info_get_name");
	}
	if (file_name == NULL || !gtk_tree_model_get_iter_first (model, iter)) {
		return FALSE;
	}
	do {
		gpointer file = NULL;
		char *row_name;
		gboolean same;

		gtk_tree_model_get (model, iter, 0, &file, -1);
		if (file == NULL) {
			continue;
		}
		row_name = file_name (file);
		g_object_unref (file);
		same = g_strcmp0 (row_name, name) == 0;
		g_free (row_name);
		if (same) {
			return TRUE;
		}
	} while (gtk_tree_model_iter_next (model, iter));
	return FALSE;
}

/* What the Size column shows for the row, as drawn. */
static char *
size_text (GtkTreeIter *iter)
{
	GList *columns = gtk_tree_view_get_columns (tree_view), *l;
	char *text = NULL;

	for (l = columns; l != NULL && text == NULL; l = l->next) {
		GtkTreeViewColumn *column = l->data;
		GList *cells;

		if (g_strcmp0 (gtk_tree_view_column_get_title (column), "Size") != 0) {
			continue;
		}
		gtk_tree_view_column_cell_set_cell_data (column, model, iter, FALSE, FALSE);
		cells = gtk_cell_layout_get_cells (GTK_CELL_LAYOUT (column));
		if (cells != NULL && GTK_IS_CELL_RENDERER_TEXT (cells->data)) {
			g_object_get (cells->data, "text", &text, NULL);
		}
		g_list_free (cells);
	}
	g_list_free (columns);
	return text;
}

/* Item counts come in after the rows now, so wait for one and say what it
   came to. "..." is still counting. */
static gboolean
report_count (GtkTreeIter *iter, const char *name)
{
	char *text = size_text (iter);

	if (text == NULL || *text == '\0' || strcmp (text, "...") == 0) {
		g_free (text);
		return FALSE;
	}
	fprintf (out, "count %s|%s\n", name, text);
	fflush (out);
	g_free (text);
	return TRUE;
}

static void
inserted_cb (G_GNUC_UNUSED GtkTreeModel *m, GtkTreePath *path, G_GNUC_UNUSED GtkTreeIter *iter,
	     G_GNUC_UNUSED gpointer data)
{
	quiet_since = g_get_monotonic_time ();
	if (stage != OPENING || out == NULL || gtk_tree_path_get_depth (path) != 1) {
		return;
	}
	if (first_at == 0) {
		first_at = quiet_since;
	}
	if (gtk_tree_model_iter_n_children (model, NULL) == files_wanted) {
		fprintf (out, "opened %s|%.1f|%.1f\n", steps[step], (first_at - opened_at) / 1000.0,
			 (quiet_since - opened_at) / 1000.0);
		fflush (out);
		stage = INSIDE;
		counted = FALSE;
		step_ticks = 0;
	}
}

static void
changed_cb (G_GNUC_UNUSED GtkTreeModel *m, G_GNUC_UNUSED GtkTreePath *path,
	    G_GNUC_UNUSED GtkTreeIter *iter, G_GNUC_UNUSED gpointer data)
{
	quiet_since = g_get_monotonic_time ();
}

static void
deleted_cb (G_GNUC_UNUSED GtkTreeModel *m, G_GNUC_UNUSED GtkTreePath *path,
	    G_GNUC_UNUSED gpointer data)
{
	quiet_since = g_get_monotonic_time ();
}

static gboolean
tick (G_GNUC_UNUSED gpointer data)
{
	GtkTreeIter iter;
	GtkTreePath *path;

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
		g_signal_connect (model, "row-inserted", G_CALLBACK (inserted_cb), NULL);
		g_signal_connect (model, "row-changed", G_CALLBACK (changed_cb), NULL);
		g_signal_connect (model, "row-deleted", G_CALLBACK (deleted_cb), NULL);
		quiet_since = g_get_monotonic_time ();
		return G_SOURCE_CONTINUE;
	}

	/* Ten seconds for a folder's rows is a hang, not a slow box, held
	 * answers and all. Twenty for anything else. */
	if (++step_ticks > (stage == OPENING ? 200 : 400)) {
		fprintf (out, "FAIL %s stuck at stage %d\n", steps[step] != NULL ? steps[step] : "the end",
			 (int) stage);
		finish ();
		return G_SOURCE_REMOVE;
	}
	if (stage == OPENING) {
		return G_SOURCE_CONTINUE;
	}

	/* Still for a while first, so no update already on its way is timed
	 * with the next folder. The first, its item counts included, has to be
	 * still for a second. */
	if (g_get_monotonic_time () - quiet_since < (step == 0 ? 2 * QUIET_US : QUIET_US)) {
		return G_SOURCE_CONTINUE;
	}

	if (stage == INSIDE) {
		if (!counted && find_row ("inner", &iter) && !report_count (&iter, "inner")) {
			return G_SOURCE_CONTINUE;
		}
		stage = GOING_UP;
		step++;
		step_ticks = 0;
		counted = FALSE;
		gtk_accel_groups_activate (G_OBJECT (window), GDK_KEY_Up, GDK_MOD1_MASK);
		return G_SOURCE_CONTINUE;
	}

	if (steps[step] == NULL) {
		finish ();
		return G_SOURCE_REMOVE;
	}
	if (!find_row (steps[step], &iter)) {
		return G_SOURCE_CONTINUE;
	}
	stage = AT_TOP;
	if (!counted) {
		if (!report_count (&iter, steps[step])) {
			return G_SOURCE_CONTINUE;
		}
		counted = TRUE;
	}

	path = gtk_tree_model_get_path (model, &iter);
	gtk_tree_view_set_cursor (tree_view, path, NULL, FALSE);
	stage = OPENING;
	first_at = 0;
	step_ticks = 0;
	opened_at = g_get_monotonic_time ();
	gtk_tree_view_row_activated (tree_view, path, gtk_tree_view_get_column (tree_view, 0));
	gtk_tree_path_free (path);
	return G_SOURCE_CONTINUE;
}

__attribute__((constructor)) static void
probe_start (void)
{
	const char *path = g_getenv ("NEMO_OPEN_OUT");
	const char *wanted = g_getenv ("NEMO_OPEN_FILES");
	const char *list = g_getenv ("NEMO_OPEN_STEPS");

	if (path == NULL || wanted == NULL || list == NULL) {
		return;
	}
	out = fopen (path, "w");
	if (out == NULL) {
		return;
	}
	files_wanted = atoi (wanted);
	steps = g_strsplit (list, "|", -1);
	/* Thumbnailers and helpers the program starts would open the file again
	 * and wipe it. */
	g_unsetenv ("NEMO_OPEN_OUT");
	g_unsetenv ("LD_PRELOAD");
	g_timeout_add (50, tick, NULL);
}
