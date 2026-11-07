/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */

/* test-nemo-tab-columns-probe.c - opens a bookmark in a new tab from inside
   the program.

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

/* Preloaded into the real program by test-nemo-tab-columns. Once the window
 * shows its first folder it middle-clicks the place $NEMO_TABCOL_PLACE names,
 * which opens that folder in a tab behind the current one. When the tab has
 * loaded it goes to it, as a click on the tab does, and notes the Name
 * column's width. Then it goes back to the first tab and opens the same place
 * there with a plain click, and the Name column there is what the new tab's
 * should have been. Writes one "ok" or "FAIL" line per check to
 * $NEMO_TABCOL_OUT, and "done" at the end. */

#include <stdio.h>

#include <gtk/gtk.h>

static FILE       *out;
static const char *place_uri;
static const char *first_name;
static const char *second_name;
static int         second_rows;
static GtkWidget  *window;
static GtkWidget  *places;
static GtkWidget  *notebook;
static int         step;
static int         waited;
static int         tab_width;

static void
report (gboolean passed, const char *what)
{
	fprintf (out, "%s %s\n", passed ? "ok" : "FAIL", what);
	fflush (out);
}

static void
finish (void)
{
	fprintf (out, "done\n");
	fflush (out);
}

static gboolean
is_a (GtkWidget *widget, const char *type_name)
{
	GType type = g_type_from_name (type_name);

	return type != 0 && widget != NULL && g_type_is_a (G_OBJECT_TYPE (widget), type);
}

typedef struct {
	const char *type_name;
	gboolean    mapped_only;
	GtkWidget  *found;
} Find;

static void
find_cb (GtkWidget *widget, gpointer data)
{
	Find *find = data;

	if (find->found != NULL) {
		return;
	}
	if (is_a (widget, find->type_name) &&
	    (!find->mapped_only || gtk_widget_get_mapped (widget))) {
		find->found = widget;
	} else if (GTK_IS_CONTAINER (widget)) {
		gtk_container_forall (GTK_CONTAINER (widget), find_cb, data);
	}
}

static GtkWidget *
find_in (GtkWidget *root, const char *type_name, gboolean mapped_only)
{
	Find find = { type_name, mapped_only, NULL };

	find_cb (root, &find);
	return find.found;
}

/* The tree view of the list view in one tab, mapped or not. */
static GtkTreeView *
tab_tree (int page)
{
	GtkWidget *child, *view, *tree;

	if (notebook == NULL || page >= gtk_notebook_get_n_pages (GTK_NOTEBOOK (notebook))) {
		return NULL;
	}
	child = gtk_notebook_get_nth_page (GTK_NOTEBOOK (notebook), page);
	view = find_in (child, "NemoListView", FALSE);
	tree = view != NULL ? gtk_bin_get_child (GTK_BIN (view)) : NULL;

	return GTK_IS_TREE_VIEW (tree) ? GTK_TREE_VIEW (tree) : NULL;
}

static int
rows_in (GtkTreeView *tree)
{
	GtkTreeModel *model = tree != NULL ? gtk_tree_view_get_model (tree) : NULL;

	return model != NULL ? gtk_tree_model_iter_n_children (model, NULL) : 0;
}

static int
name_width (GtkTreeView *tree)
{
	GList *columns, *l;
	int width = -1;

	if (tree == NULL) {
		return -1;
	}
	columns = gtk_tree_view_get_columns (tree);
	for (l = columns; l != NULL; l = l->next) {
		if (g_strcmp0 (g_object_get_data (l->data, "column-id"), "name") == 0) {
			width = gtk_tree_view_column_get_width (l->data);
		}
	}
	g_list_free (columns);

	return width;
}

static gboolean
title_starts (const char *name)
{
	const char *title = gtk_window_get_title (GTK_WINDOW (window));

	return title != NULL && g_str_has_prefix (title, name);
}

typedef struct {
	const char  *uri;
	GtkTreePath *path;
} RowSearch;

static gboolean
row_cb (GtkTreeModel *model, GtkTreePath *path, GtkTreeIter *iter, gpointer data)
{
	RowSearch *search = data;
	int column;

	for (column = 0; column < gtk_tree_model_get_n_columns (model); column++) {
		GValue value = G_VALUE_INIT;
		gboolean match;

		if (gtk_tree_model_get_column_type (model, column) != G_TYPE_STRING) {
			continue;
		}
		gtk_tree_model_get_value (model, iter, column, &value);
		match = g_strcmp0 (g_value_get_string (&value), search->uri) == 0;
		g_value_unset (&value);
		if (match) {
			search->path = gtk_tree_path_copy (path);
			return TRUE;
		}
	}
	return FALSE;
}

static void
send_button (GdkEventType type, guint button, int x, int y)
{
	GdkWindow *bin = gtk_tree_view_get_bin_window (GTK_TREE_VIEW (places));
	GdkSeat   *seat = gdk_display_get_default_seat (gtk_widget_get_display (places));
	GdkEvent  *event = gdk_event_new (type);
	int        origin_x, origin_y;

	gdk_window_get_origin (bin, &origin_x, &origin_y);
	event->button.window = g_object_ref (bin);
	/* Every click gets its own time, so two of them never read as a double. */
	event->button.time = (guint32) (g_get_monotonic_time () / 1000);
	event->button.x = x;
	event->button.y = y;
	event->button.x_root = origin_x + x;
	event->button.y_root = origin_y + y;
	event->button.button = button;
	if (type == GDK_BUTTON_RELEASE) {
		event->button.state = button == 1 ? GDK_BUTTON1_MASK : GDK_BUTTON2_MASK;
	}
	gdk_event_set_device (event, gdk_seat_get_pointer (seat));
	gtk_main_do_event (event);
	gdk_event_free (event);
}

/* FALSE when the place is not listed. */
static gboolean
click_place (guint button)
{
	RowSearch    search = { place_uri, NULL };
	GdkRectangle area;
	int          x, y;

	gtk_tree_model_foreach (gtk_tree_view_get_model (GTK_TREE_VIEW (places)), row_cb, &search);
	if (search.path == NULL) {
		return FALSE;
	}
	gtk_tree_view_get_cell_area (GTK_TREE_VIEW (places), search.path, NULL, &area);
	gtk_tree_path_free (search.path);

	/* Mid-row, clear of the eject column at the right edge. */
	x = gtk_widget_get_allocated_width (places) / 3;
	y = area.y + area.height / 2;
	send_button (GDK_BUTTON_PRESS, button, x, y);
	send_button (GDK_BUTTON_RELEASE, button, x, y);
	return TRUE;
}

/* Each step waits for what the one before set going. Up to ten seconds each,
 * and a few ticks more once it is there, so the widths have settled. */
static gboolean
tick (G_GNUC_UNUSED gpointer data)
{
	static int settled;
	GtkTreeView *tree;
	GtkWidget *view;
	char *line;
	int width;

	if (++waited > 100) {
		report (FALSE, "timed out waiting");
		finish ();
		return G_SOURCE_REMOVE;
	}

	switch (step) {
	case 0: {
		GList *toplevels = gtk_window_list_toplevels ();
		GList *l;

		for (l = toplevels; l != NULL; l = l->next) {
			if (is_a (l->data, "NemoWindow") && gtk_widget_get_mapped (l->data)) {
				window = l->data;
			}
		}
		g_list_free (toplevels);
		if (window == NULL || !title_starts (first_name)) {
			return G_SOURCE_CONTINUE;
		}
		places = find_in (window, "NemoPlacesTreeView", TRUE);
		view = find_in (window, "NemoListView", TRUE);
		notebook = view != NULL ? gtk_widget_get_ancestor (view, GTK_TYPE_NOTEBOOK) : NULL;
		if (places == NULL || notebook == NULL || rows_in (tab_tree (0)) < 3) {
			return G_SOURCE_CONTINUE;
		}
		/* Bookmarks are listed a moment after the window shows. */
		if (!click_place (2)) {
			return G_SOURCE_CONTINUE;
		}
		break;
	}
	case 1:
		tree = tab_tree (1);
		if (rows_in (tree) < second_rows || ++settled < 10) {
			return G_SOURCE_CONTINUE;
		}
		settled = 0;
		report (gtk_notebook_get_current_page (GTK_NOTEBOOK (notebook)) == 0 &&
			!gtk_widget_get_mapped (GTK_WIDGET (tree)),
			"a middle click opens the place in a tab behind this one");
		gtk_notebook_set_current_page (GTK_NOTEBOOK (notebook), 1);
		break;
	case 2:
		if (!title_starts (second_name) || ++settled < 10) {
			return G_SOURCE_CONTINUE;
		}
		settled = 0;
		tab_width = name_width (tab_tree (1));
		gtk_notebook_set_current_page (GTK_NOTEBOOK (notebook), 0);
		break;
	case 3:
		if (!title_starts (first_name)) {
			return G_SOURCE_CONTINUE;
		}
		click_place (1);
		break;
	case 4:
		tree = tab_tree (0);
		if (!title_starts (second_name) || rows_in (tree) < second_rows || ++settled < 10) {
			return G_SOURCE_CONTINUE;
		}
		width = name_width (tree);
		line = g_strdup_printf ("Name in the new tab is as wide as when the place opens in this one (%d, %d)",
					tab_width, width);
		report (tab_width > 0 && tab_width == width, line);
		g_free (line);
		finish ();
		return G_SOURCE_REMOVE;
	}

	step++;
	waited = 0;
	return G_SOURCE_CONTINUE;
}

__attribute__((constructor)) static void
probe_start (void)
{
	const char *path = g_getenv ("NEMO_TABCOL_OUT");
	const char *rows = g_getenv ("NEMO_TABCOL_ROWS");

	place_uri = g_getenv ("NEMO_TABCOL_PLACE");
	first_name = g_getenv ("NEMO_TABCOL_FIRST");
	second_name = g_getenv ("NEMO_TABCOL_SECOND");
	if (path == NULL || place_uri == NULL || first_name == NULL || second_name == NULL ||
	    rows == NULL) {
		return;
	}
	second_rows = (int) g_ascii_strtoll (rows, NULL, 10);
	out = fopen (path, "w");
	if (out == NULL) {
		return;
	}
	g_timeout_add (100, tick, NULL);
}
