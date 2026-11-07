/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */

/* test-nemo-tree-scroll-probe.c - looks at the folder tree's sideways scroll
   from inside the program.

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

/* Preloaded into the real program by test-nemo-tree-scroll. Once the window
 * shows the deep folder n6 selected in the tree, it opens n6's row and checks
 * that n6's name and its child's are inside the pane's visible width. Then it
 * clicks top, near the left, and checks the pane went back to the left edge.
 * One "ok" or "FAIL" line per check goes to $NEMO_TREE_SCROLL_OUT, and "done"
 * at the end. */

#include <stdio.h>
#include <string.h>

#include <gtk/gtk.h>

static FILE        *out;
static GtkWidget   *window;
static GtkTreeView *tree;
static int          step;
static int          waited;
static guint32      stamp;

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
find_widget (GtkWidget *within, const char *type_name)
{
	Find find = { type_name, NULL };

	find_cb (within, &find);
	return find.found;
}

static GtkTreeView *
folder_tree (void)
{
	GtkWidget *side = find_widget (window, "FMTreeView");
	GtkWidget *found = side != NULL ? find_widget (side, "GtkTreeView") : NULL;

	return GTK_IS_TREE_VIEW (found) ? GTK_TREE_VIEW (found) : NULL;
}

/* The first row named so, depth first; the display name is column 0. */
static gboolean
find_row (GtkTreeModel *model, GtkTreeIter *parent, const char *name, GtkTreeIter *found)
{
	GtkTreeIter iter;
	gboolean more;

	for (more = gtk_tree_model_iter_children (model, &iter, parent); more;
	     more = gtk_tree_model_iter_next (model, &iter)) {
		char *text = NULL;
		gboolean match;

		gtk_tree_model_get (model, &iter, 0, &text, -1);
		match = g_strcmp0 (text, name) == 0;
		g_free (text);
		if (match) {
			*found = iter;
			return TRUE;
		}
		if (find_row (model, &iter, name, found)) {
			return TRUE;
		}
	}
	return FALSE;
}

static GtkTreePath *
tree_row (const char *name)
{
	GtkTreeModel *model = gtk_tree_view_get_model (tree);
	GtkTreeIter iter;

	if (model == NULL || !find_row (model, NULL, name, &iter)) {
		return NULL;
	}
	return gtk_tree_model_get_path (model, &iter);
}

static gboolean
tree_selected_is (GtkTreePath *path)
{
	return path != NULL && gtk_tree_selection_path_is_selected (gtk_tree_view_get_selection (tree), path);
}

static void
send (GdkWindow *window_for, GdkEventType type, double x, double y, guint state)
{
	GdkSeat *seat = gdk_display_get_default_seat (gdk_window_get_display (window_for));
	GdkEvent *event = gdk_event_new (type);
	int origin_x, origin_y;

	gdk_window_get_origin (window_for, &origin_x, &origin_y);
	gdk_device_warp (gdk_seat_get_pointer (seat), gdk_window_get_screen (window_for),
			 origin_x + (int) x, origin_y + (int) y);
	stamp += 20;
	event->button.window = g_object_ref (window_for);
	event->button.time = stamp;
	event->button.x = x;
	event->button.y = y;
	event->button.x_root = origin_x + x;
	event->button.y_root = origin_y + y;
	event->button.button = 1;
	event->button.state = state;
	gdk_event_set_device (event, gdk_seat_get_pointer (seat));
	gtk_main_do_event (event);
	gdk_event_free (event);
}

/* Frees path. */
static gboolean
click_tree_path (GtkTreePath *path)
{
	GdkWindow *bin = gtk_tree_view_get_bin_window (tree);
	GdkRectangle area;

	if (path == NULL) {
		return FALSE;
	}
	gtk_tree_view_get_cell_area (tree, path, gtk_tree_view_get_column (tree, 0), &area);
	gtk_tree_path_free (path);
	stamp += 1000;
	send (bin, GDK_BUTTON_PRESS, area.x + 30, area.y + area.height / 2, 0);
	send (bin, GDK_BUTTON_RELEASE, area.x + 30, area.y + area.height / 2, GDK_BUTTON1_MASK);
	return TRUE;
}

/* The row's icon and name sit inside what the pane shows. The name's width is
   measured apart from the tree, and the icon given its usual 16 and padding. */
static gboolean
in_view (const char *name)
{
	GtkAdjustment *adjustment = gtk_scrollable_get_hadjustment (GTK_SCROLLABLE (tree));
	GtkTreePath *path = tree_row (name);
	PangoLayout *layout;
	GdkRectangle area;
	double value = gtk_adjustment_get_value (adjustment);
	double page = gtk_adjustment_get_page_size (adjustment);
	int width;
	gboolean inside;

	if (path == NULL) {
		return FALSE;
	}
	gtk_tree_view_get_cell_area (tree, path, gtk_tree_view_get_column (tree, 0), &area);
	gtk_tree_path_free (path);
	layout = gtk_widget_create_pango_layout (GTK_WIDGET (tree), name);
	pango_layout_get_pixel_size (layout, &width, NULL);
	g_object_unref (layout);
	inside = area.x >= value && area.x + 20 + width <= value + page;
	if (!inside) {
		fprintf (out, "# %s from %d to %d, the pane shows %.0f to %.0f\n",
			 name, area.x, area.x + 20 + width, value, value + page);
	}
	return inside;
}

static gboolean
steps (void)
{
	GtkAdjustment *adjustment;
	GtkTreePath *path;

	switch (step) {
	case 0: {
		GList *toplevels = gtk_window_list_toplevels ();
		GList *l;
		const char *title;

		for (l = toplevels; l != NULL; l = l->next) {
			if (is_a (l->data, "NemoWindow") && gtk_widget_get_mapped (l->data)) {
				window = l->data;
			}
		}
		g_list_free (toplevels);
		title = window != NULL ? gtk_window_get_title (GTK_WINDOW (window)) : NULL;
		if (title == NULL || !g_str_has_prefix (title, "n6") ||
		    (tree = folder_tree ()) == NULL || waited < 10) {
			return FALSE;
		}
		path = tree_row ("n6");
		if (!tree_selected_is (path)) {
			gtk_tree_path_free (path);
			return FALSE;
		}
		gtk_tree_view_expand_row (tree, path, FALSE);
		gtk_tree_path_free (path);
		break;
	}
	case 1:
		/* The child turns up in the background, then the layout. */
		path = tree_row ("the-child-folder");
		gtk_tree_path_free (path);
		if (path == NULL || waited < 15) {
			return FALSE;
		}
		report (in_view ("n6"), "the deep selected folder's name is in view");
		report (in_view ("the-child-folder"), "and so is its child's");
		click_tree_path (tree_row ("top"));
		break;
	case 2:
		path = tree_row ("top");
		if (!tree_selected_is (path) || !g_str_has_prefix (gtk_window_get_title (GTK_WINDOW (window)), "top") ||
		    waited < 15) {
			gtk_tree_path_free (path);
			return FALSE;
		}
		gtk_tree_path_free (path);
		adjustment = gtk_scrollable_get_hadjustment (GTK_SCROLLABLE (tree));
		report (gtk_adjustment_get_value (adjustment) == 0 && in_view ("n1"),
			"selecting a shallow folder scrolls the tree back to its left edge");
		return TRUE;
	}
	return TRUE;
}

/* Each step waits on what the one before set going, up to ten seconds. */
static gboolean
tick (G_GNUC_UNUSED gpointer data)
{
	if (++waited > 100) {
		char *line = g_strdup_printf ("timed out at step %d", step);

		report (FALSE, line);
		g_free (line);
		finish ();
		return G_SOURCE_REMOVE;
	}
	if (!steps ()) {
		return G_SOURCE_CONTINUE;
	}
	if (step == 2) {
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
	const char *path = g_getenv ("NEMO_TREE_SCROLL_OUT");

	if (path == NULL) {
		return;
	}
	out = fopen (path, "w");
	if (out == NULL) {
		return;
	}
	g_timeout_add (100, tick, NULL);
}
