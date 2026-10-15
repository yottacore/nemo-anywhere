/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */

/* test-nemo-long-press-probe.c - holds the left button down in a folder view
   and a side pane from inside the program, and checks the menu it opens.

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

/* Preloaded into the real program by test-nemo-long-press. Once the window
 * shows $NEMO_HOLD_FOLDER, it presses, holds and lets go in the folder view
 * named by $NEMO_HOLD_VIEW, then in the side pane: Places beside the list
 * view, the folder tree beside the others. The button goes through the X
 * server, since the long press asks it whether the button is still down,
 * and the menu takes the pointer as it would for a person. One "ok" or
 * "FAIL" line per check goes to $NEMO_HOLD_OUT, and "done" at the end. */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <gtk/gtk.h>
#include <gdk/gdkx.h>
#include <X11/extensions/XTest.h>
#include <eel/eel-canvas.h>

static FILE           *out;
static const char     *folder_name;
static gboolean        list_mode;
static gboolean        compact_mode;
static GtkWidget      *window;
static GtkWidget      *pane;
static GdkWindow      *bin;
static Display        *xdisplay;
static int             step;
static gint64          step_start;
static int             drags;
static GdkDragContext *drag_context;
static gboolean        finished;

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
	finished = TRUE;
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

static GtkWidget *
shown_menu (void)
{
	GList *toplevels = gtk_window_list_toplevels ();
	GList *l;
	GtkWidget *found = NULL;

	for (l = toplevels; l != NULL && found == NULL; l = l->next) {
		GtkWidget *child = gtk_bin_get_child (GTK_BIN (l->data));

		if (GTK_IS_MENU (child) && gtk_widget_get_mapped (child)) {
			found = child;
		}
	}
	g_list_free (toplevels);

	return found;
}

static void
close_menu (void)
{
	GtkWidget *menu = shown_menu ();

	if (menu != NULL) {
		gtk_menu_shell_deactivate (GTK_MENU_SHELL (menu));
	}
}

static void
pointer_to (GdkWindow *window_for, double x, double y)
{
	int origin_x, origin_y;

	gdk_window_get_origin (window_for, &origin_x, &origin_y);
	XTestFakeMotionEvent (xdisplay, -1, origin_x + (int) x, origin_y + (int) y, CurrentTime);
	XFlush (xdisplay);
}

static void
button (gboolean down)
{
	XTestFakeButtonEvent (xdisplay, 1, down, CurrentTime);
	XFlush (xdisplay);
}

static gint64
elapsed_ms (void)
{
	return (g_get_monotonic_time () - step_start) / 1000;
}

static void
set_long_press_time (int ms)
{
	g_object_set (gtk_widget_get_settings (window), "gtk-long-press-time", ms, NULL);
}

static void
drag_begin_cb (G_GNUC_UNUSED GtkWidget *widget, GdkDragContext *context, G_GNUC_UNUSED gpointer data)
{
	drags++;
	g_set_object (&drag_context, context);
}

static int
item_count (void)
{
	if (list_mode) {
		GtkTreeModel *model = gtk_tree_view_get_model (GTK_TREE_VIEW (pane));

		return model != NULL ? gtk_tree_model_iter_n_children (model, NULL) : 0;
	}
	return atk_object_get_n_accessible_children (gtk_widget_get_accessible (pane));
}

static void
select_items (const int *items)
{
	if (list_mode) {
		GtkTreeSelection *selection = gtk_tree_view_get_selection (GTK_TREE_VIEW (pane));

		gtk_tree_selection_unselect_all (selection);
		for (; *items >= 0; items++) {
			GtkTreePath *path = gtk_tree_path_new_from_indices (*items, -1);

			gtk_tree_selection_select_path (selection, path);
			gtk_tree_path_free (path);
		}
	} else {
		AtkSelection *selection = ATK_SELECTION (gtk_widget_get_accessible (pane));

		atk_selection_clear_selection (selection);
		for (; *items >= 0; items++) {
			atk_selection_add_selection (selection, *items);
		}
	}
}

static gboolean
is_selected (int item)
{
	if (list_mode) {
		GtkTreePath *path = gtk_tree_path_new_from_indices (item, -1);
		gboolean selected = gtk_tree_selection_path_is_selected
			(gtk_tree_view_get_selection (GTK_TREE_VIEW (pane)), path);

		gtk_tree_path_free (path);
		return selected;
	}
	return atk_selection_is_child_selected (ATK_SELECTION (gtk_widget_get_accessible (pane)), item);
}

static char *
selected_list (void)
{
	GString *list = g_string_new (NULL);
	int i;

	for (i = 0; i < item_count (); i++) {
		if (is_selected (i)) {
			g_string_append_printf (list, "%s%d", list->len > 0 ? "," : "", i);
		}
	}
	return g_string_free (list, FALSE);
}

static void
check_selection (const char *want, const char *what)
{
	char *got = selected_list ();
	char *line = g_strdup_printf ("%s (selected %s, want %s)", what, got, want);

	report (strcmp (got, want) == 0, line);
	g_free (line);
	g_free (got);
}

/* On the name, where a second slow click renames. */
static void
item_spot (int item, double *x, double *y)
{
	double left = 0, top = 0;

	if (list_mode) {
		GtkTreePath *path = gtk_tree_path_new_from_indices (item, -1);
		GdkRectangle cell;

		gtk_tree_view_get_cell_area (GTK_TREE_VIEW (pane), path,
					     gtk_tree_view_get_column (GTK_TREE_VIEW (pane), 0), &cell);
		gtk_tree_path_free (path);
		*x = cell.x + cell.width / 2;
		*y = cell.y + cell.height / 2;
	} else {
		AtkObject *child = atk_object_ref_accessible_child (gtk_widget_get_accessible (pane), item);
		EelCanvasItem *canvas_item = (EelCanvasItem *)
			atk_gobject_accessible_get_object (ATK_GOBJECT_ACCESSIBLE (child));

		if (compact_mode) {
			*x = canvas_item->x2 - 10;
			*y = (canvas_item->y1 + canvas_item->y2) / 2;
		} else {
			*x = (canvas_item->x1 + canvas_item->x2) / 2;
			*y = canvas_item->y2 - 6;
		}
		g_object_unref (child);
	}

	/* The button goes to whatever is under the pointer. */
	if (!list_mode) {
		left = gtk_adjustment_get_value (gtk_scrollable_get_hadjustment (GTK_SCROLLABLE (pane)));
		top = gtk_adjustment_get_value (gtk_scrollable_get_vadjustment (GTK_SCROLLABLE (pane)));
	}
	if (*x < left || *x >= left + gtk_widget_get_allocated_width (pane) ||
	    *y < top || *y >= top + gtk_widget_get_allocated_height (pane)) {
		report (FALSE, "the item pressed is in sight");
	}
}

/* Clear of every item: below the rows, or the spot in sight nearest to
   (near_x, near_y) that no icon is near. */
static gboolean
blank_spot (double near_x, double near_y, double *x, double *y)
{
	AtkObject *accessible;
	int n, left, top, width, height, px, py, i;
	double best = -1;

	if (list_mode) {
		GtkTreePath *path = gtk_tree_path_new_from_indices (item_count () - 1, -1);
		GdkRectangle area;

		gtk_tree_view_get_background_area (GTK_TREE_VIEW (pane), path, NULL, &area);
		gtk_tree_path_free (path);
		*x = 40;
		*y = area.y + area.height + 30;
		return *y < gdk_window_get_height (bin);
	}

	accessible = gtk_widget_get_accessible (pane);
	n = atk_object_get_n_accessible_children (accessible);
	left = (int) gtk_adjustment_get_value (gtk_scrollable_get_hadjustment (GTK_SCROLLABLE (pane)));
	top = (int) gtk_adjustment_get_value (gtk_scrollable_get_vadjustment (GTK_SCROLLABLE (pane)));
	width = gtk_widget_get_allocated_width (pane);
	height = gtk_widget_get_allocated_height (pane);
	for (py = top + 8; py < top + height - 8; py += 8) {
		for (px = left + 8; px < left + width - 8; px += 8) {
			gboolean clear = TRUE;
			double distance;

			for (i = 0; i < n && clear; i++) {
				AtkObject *child = atk_object_ref_accessible_child (accessible, i);
				EelCanvasItem *item = (EelCanvasItem *)
					atk_gobject_accessible_get_object (ATK_GOBJECT_ACCESSIBLE (child));

				clear = px < item->x1 - 12 || px > item->x2 + 12 ||
					py < item->y1 - 12 || py > item->y2 + 12;
				g_object_unref (child);
			}
			distance = (px - near_x) * (px - near_x) + (py - near_y) * (py - near_y);
			if (clear && (best < 0 || distance < best)) {
				best = distance;
				*x = px;
				*y = py;
			}
		}
	}

	return best >= 0;
}

static gboolean
renaming (void)
{
	GtkWidget *focus = gtk_window_get_focus (GTK_WINDOW (window));

	return focus != NULL && (GTK_IS_EDITABLE (focus) || is_a (focus, "EelEditableLabel"));
}

static gboolean
title_unchanged (void)
{
	const char *title = gtk_window_get_title (GTK_WINDOW (window));

	return title != NULL && g_str_has_prefix (title, folder_name);
}

static gboolean
find_pane (void)
{
	GtkWidget *view = find_widget (window, "NemoView");
	GtkWidget *child = view != NULL ? gtk_bin_get_child (GTK_BIN (view)) : NULL;

	if (child == NULL || !gtk_widget_get_mapped (child)) {
		return FALSE;
	}
	if (list_mode ? !GTK_IS_TREE_VIEW (child) : !is_a (child, "NemoIconContainer")) {
		return FALSE;
	}
	pane = child;
	bin = list_mode ? gtk_tree_view_get_bin_window (GTK_TREE_VIEW (pane))
			: gtk_layout_get_bin_window (GTK_LAYOUT (pane));
	g_signal_connect (pane, "drag-begin", G_CALLBACK (drag_begin_cb), NULL);
	return TRUE;
}

static GtkTreeView *
side_tree (void)
{
	GtkWidget *side = find_widget (window, list_mode ? "NemoPlacesSidebar" : "FMTreeView");
	GtkWidget *tree = side != NULL ? find_widget (side, "GtkTreeView") : NULL;

	return GTK_IS_TREE_VIEW (tree) ? GTK_TREE_VIEW (tree) : NULL;
}

static const int none[] = { -1 };
static const int second_to_fourth[] = { 1, 2, 3, -1 };
static const int sixth[] = { 5, -1 };

/* The steps share one script, so the views can't drift apart. Each returns
   FALSE to be called again on the next tick, and the tick that has waited
   ten seconds fails it. No two presses in a row hit one item quickly enough
   to be a double click. */
static gboolean
run_step (void)
{
	static double x0, y0, x1, y1;
	GtkTreeView *tree;
	GtkTreePath *path;
	GdkRectangle area;
	int i;

	switch (step) {
	/* A selection of 3, held on the middle one. */
	case 1:
		set_long_press_time (2000);
		select_items (second_to_fourth);
		item_spot (2, &x0, &y0);
		pointer_to (bin, x0, y0);
		button (TRUE);
		return TRUE;
	case 2:
		if (elapsed_ms () < 700) {
			return FALSE;
		}
		report (shown_menu () == NULL,
			"no menu yet at 700 ms, with GTK's long-press time set to 2 s");
		return TRUE;
	case 3:
		if (shown_menu () == NULL) {
			return FALSE;
		}
		report (TRUE, "a long press on a selected item opens the menu while the button is down");
		check_selection ("1,2,3", "and the selection is all 3");
		set_long_press_time (500);
		button (FALSE);
		return TRUE;
	case 4:
		if (elapsed_ms () < 800) {
			return FALSE;
		}
		report (shown_menu () != NULL, "the menu stays up after the release");
		check_selection ("1,2,3", "and the release leaves all 3 selected");
		close_menu ();
		return TRUE;

	/* A second, slow click on a selected item's name, held. Let go at
	   once, it would rename. */
	case 5:
		select_items (sixth);
		item_spot (5, &x0, &y0);
		pointer_to (bin, x0, y0);
		button (TRUE);
		button (FALSE);
		return TRUE;
	case 6:
		if (elapsed_ms () < 700) {
			return FALSE;
		}
		button (TRUE);
		return TRUE;
	case 7:
		if (shown_menu () == NULL) {
			return FALSE;
		}
		return TRUE;
	case 8:
		/* Past the half second in which GTK keeps a menu up on any release. */
		if (elapsed_ms () < 800) {
			return FALSE;
		}
		button (FALSE);
		return TRUE;
	case 9:
		if (elapsed_ms () < 700) {
			return FALSE;
		}
		report (shown_menu () != NULL && !renaming (),
			"a long press as the second slow click, let go late, keeps its menu up and starts no rename");
		check_selection ("5", "and keeps the item selected");
		report (title_unchanged (), "and opens nothing");
		close_menu ();
		return TRUE;

	/* A quick click still only selects, and nothing is left over from the
	   press the menu took. */
	case 10:
		item_spot (7, &x0, &y0);
		pointer_to (bin, x0, y0);
		button (TRUE);
		button (FALSE);
		return TRUE;
	case 11:
		if (elapsed_ms () < 1000) {
			return FALSE;
		}
		report (shown_menu () == NULL, "a quick click opens no menu");
		check_selection ("7", "and selects the item clicked");
		return TRUE;

	/* Empty space. */
	case 12:
		item_spot (7, &x1, &y1);
		if (!blank_spot (x1, y1, &x0, &y0)) {
			report (FALSE, "there is an empty spot");
			finish ();
			return TRUE;
		}
		pointer_to (bin, x0, y0);
		button (TRUE);
		return TRUE;
	case 13:
		if (shown_menu () == NULL) {
			return FALSE;
		}
		report (TRUE, "a long press on empty space opens a menu");
		check_selection ("", "and nothing is selected");
		button (FALSE);
		return TRUE;
	case 14:
		if (elapsed_ms () < 800) {
			return FALSE;
		}
		report (shown_menu () != NULL, "and it stays up after the release");
		close_menu ();
		return TRUE;

	/* A band, held still once it has started. */
	case 15:
		select_items (none);
		pointer_to (bin, x0, y0);
		button (TRUE);
		for (i = 1; i <= 4; i++) {
			pointer_to (bin, x0 + (x1 - x0) * i / 4, y0 + (y1 - y0) * i / 4);
		}
		return TRUE;
	case 16:
		if (elapsed_ms () < 1500) {
			return FALSE;
		}
		report (shown_menu () == NULL, "a band held still opens no menu");
		report (is_selected (7), "and selects what it covers");
		button (FALSE);
		return TRUE;

	/* Out and back before the long-press time is up, then held. */
	case 17:
		if (elapsed_ms () < 500) {
			return FALSE;
		}
		pointer_to (bin, x0, y0);
		button (TRUE);
		for (i = 1; i <= 4; i++) {
			pointer_to (bin, x0 + (x1 - x0) * i / 4, y0 + (y1 - y0) * i / 4);
		}
		return TRUE;
	case 18:
		/* GTK folds motion it hasn't read yet into the last one, so the
		   way out has to be read before the way back. */
		if (elapsed_ms () < 200) {
			return FALSE;
		}
		for (i = 3; i >= 0; i--) {
			pointer_to (bin, x0 + (x1 - x0) * i / 4, y0 + (y1 - y0) * i / 4);
		}
		return TRUE;
	case 19:
		if (elapsed_ms () < 1500) {
			return FALSE;
		}
		report (shown_menu () == NULL, "nor does one that goes out and comes back first");
		button (FALSE);
		return TRUE;

	/* A file drag, held still. */
	case 20:
		if (elapsed_ms () < 500) {
			return FALSE;
		}
		select_items (none);
		drags = 0;
		item_spot (1, &x0, &y0);
		item_spot (4, &x1, &y1);
		pointer_to (bin, x0, y0);
		button (TRUE);
		for (i = 1; i <= 4; i++) {
			pointer_to (bin, x0 + (x1 - x0) * i / 4, y0 + (y1 - y0) * i / 4);
		}
		return TRUE;
	case 21:
		if (drag_context != NULL) {
			gtk_drag_cancel (drag_context);
			g_clear_object (&drag_context);
		}
		if (elapsed_ms () < 1500) {
			return FALSE;
		}
		report (drags == 1, "a press that moves drags files");
		report (shown_menu () == NULL, "and opens no menu, held still after");
		button (FALSE);
		return TRUE;

	/* The side pane. */
	case 22:
		if (elapsed_ms () < 500) {
			return FALSE;
		}
		close_menu ();
		tree = side_tree ();
		if (tree == NULL) {
			report (FALSE, list_mode ? "Places is showing" : "the tree pane is showing");
			finish ();
			return TRUE;
		}
		path = list_mode ? gtk_tree_path_new_from_indices (0, 0, -1)
				 : gtk_tree_path_new_from_indices (0, -1);
		gtk_tree_view_get_background_area (tree, path, NULL, &area);
		gtk_tree_path_free (path);
		pointer_to (gtk_tree_view_get_bin_window (tree), 30, area.y + area.height / 2);
		button (TRUE);
		return TRUE;
	case 23:
		if (shown_menu () == NULL) {
			return FALSE;
		}
		report (TRUE, list_mode ? "a long press on a Places row opens its menu"
					: "a long press on a tree row opens its menu");
		button (FALSE);
		return TRUE;
	case 24:
		if (elapsed_ms () < 800) {
			return FALSE;
		}
		report (shown_menu () != NULL, "and it stays up after the release");
		/* Places opens a row when the button comes up. */
		if (list_mode) {
			report (title_unchanged (), "and the release opens nothing");
		}
		close_menu ();
		finish ();
		return TRUE;
	}

	return TRUE;
}

static gboolean
tick (G_GNUC_UNUSED gpointer data)
{
	if (finished) {
		return G_SOURCE_REMOVE;
	}

	if (step == 0) {
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
		if (title == NULL || !g_str_has_prefix (title, folder_name) ||
		    (pane == NULL && !find_pane ()) || item_count () < 8) {
			step_start = g_get_monotonic_time ();
			return G_SOURCE_CONTINUE;
		}
		/* Icons are placed a moment after they are listed, so give it a second. */
		if (elapsed_ms () < 1000) {
			return G_SOURCE_CONTINUE;
		}
		xdisplay = GDK_DISPLAY_XDISPLAY (gtk_widget_get_display (window));
		gtk_widget_grab_focus (pane);
	} else if (elapsed_ms () > 10000) {
		char *line = g_strdup_printf ("timed out at step %d", step);

		report (FALSE, line);
		g_free (line);
		button (FALSE);
		close_menu ();
		finish ();
		return G_SOURCE_REMOVE;
	} else if (!run_step ()) {
		return G_SOURCE_CONTINUE;
	}

	if (finished) {
		return G_SOURCE_REMOVE;
	}
	step++;
	step_start = g_get_monotonic_time ();
	return G_SOURCE_CONTINUE;
}

__attribute__((constructor)) static void
probe_start (void)
{
	const char *path = g_getenv ("NEMO_HOLD_OUT");
	const char *view = g_getenv ("NEMO_HOLD_VIEW");

	folder_name = g_getenv ("NEMO_HOLD_FOLDER");
	if (path == NULL || view == NULL || folder_name == NULL) {
		return;
	}
	list_mode = g_strcmp0 (view, "list-view") == 0;
	compact_mode = g_strcmp0 (view, "compact-view") == 0;
	out = fopen (path, "w");
	if (out == NULL) {
		return;
	}
	step_start = g_get_monotonic_time ();
	g_timeout_add (100, tick, NULL);
}
