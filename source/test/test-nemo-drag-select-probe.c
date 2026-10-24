/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */

/* test-nemo-drag-select-probe.c - presses and drags in a folder view from
   inside the program and reports whether each one dragged files or a band.

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

/* Preloaded into the real program by test-nemo-drag-select. Once the window
 * shows $NEMO_DRAG_FOLDER, it presses and drags in the folder view named by
 * $NEMO_DRAG_VIEW, through GTK's own event path, and checks what each did to
 * the selection, whether a file drag began, and when a menu opened. In the
 * list view it also right-clicks a Places row, and in the others a row of the
 * tree pane. Under Nemo Anywhere each list view check also looks at the
 * checkmarks. One "ok" or "FAIL" line per check goes to $NEMO_DRAG_OUT, and
 * "done" at the end. Every file drag is canceled, so nothing moves. */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <gtk/gtk.h>
#include <eel/eel-canvas.h>

#define BUTTON_MASK(b) (GDK_BUTTON1_MASK << ((b) - 1))

static FILE           *out;
static const char     *folder_name;
static gboolean        list_mode;
static gboolean        classic;
static gboolean        anywhere;
static GtkWidget      *window;
static GtkWidget      *pane;
static GdkWindow      *bin;
static int             step;
static int             waited;
static int             drags;
static gboolean        dragging;
static int             bands;
static GdkDragContext *drag_context;
static guint32         stamp;
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

/* The pointer goes where each event says too, since the icon view's band
   reads where the pointer really is. */
static void
send (GdkWindow *window_for, GdkEventType type, guint button, double x, double y, guint state)
{
	GdkSeat *seat = gdk_display_get_default_seat (gdk_window_get_display (window_for));
	GdkEvent *event = gdk_event_new (type);
	int origin_x, origin_y;

	gdk_window_get_origin (window_for, &origin_x, &origin_y);
	gdk_device_warp (gdk_seat_get_pointer (seat), gdk_window_get_screen (window_for),
			 origin_x + (int) x, origin_y + (int) y);
	stamp += 20;
	if (type == GDK_MOTION_NOTIFY) {
		event->motion.window = g_object_ref (window_for);
		event->motion.time = stamp;
		event->motion.x = x;
		event->motion.y = y;
		event->motion.x_root = origin_x + x;
		event->motion.y_root = origin_y + y;
		event->motion.state = state;
	} else {
		event->button.window = g_object_ref (window_for);
		event->button.time = stamp;
		event->button.x = x;
		event->button.y = y;
		event->button.x_root = origin_x + x;
		event->button.y_root = origin_y + y;
		event->button.button = button;
		event->button.state = state;
	}
	gdk_event_set_device (event, gdk_seat_get_pointer (seat));
	gtk_main_do_event (event);
	gdk_event_free (event);
}

/* The icon view sends a press to the item the pointer last moved over, as
   it always has by then, so the pointer gets there first. */
static void
press (GdkWindow *window_for, guint button, double x, double y, guint modifiers)
{
	send (window_for, GDK_MOTION_NOTIFY, 0, x, y, modifiers);
	send (window_for, GDK_BUTTON_PRESS, button, x, y, modifiers);
}

/* Press, then move well past the drag threshold in a few steps. The button
   stays down. */
static void
press_and_move (GdkWindow *window_for, guint button, guint modifiers,
		double x0, double y0, double x1, double y1)
{
	int i;

	drags = 0;
	bands = 0;
	close_menu ();
	press (window_for, button, x0, y0, modifiers);
	for (i = 1; i <= 4; i++) {
		send (window_for, GDK_MOTION_NOTIFY, 0, x0 + (x1 - x0) * i / 4, y0 + (y1 - y0) * i / 4,
		      modifiers | BUTTON_MASK (button));
	}
}

static void
release (GdkWindow *window_for, guint button, guint modifiers, double x, double y)
{
	send (window_for, GDK_BUTTON_RELEASE, button, x, y, modifiers | BUTTON_MASK (button));
	if (drag_context != NULL) {
		gtk_drag_cancel (drag_context);
		g_clear_object (&drag_context);
	}
}

static void
drag_begin_cb (G_GNUC_UNUSED GtkWidget *widget, GdkDragContext *context, G_GNUC_UNUSED gpointer data)
{
	drags++;
	dragging = TRUE;
	g_set_object (&drag_context, context);
}

static void
drag_end_cb (void)
{
	dragging = FALSE;
}

static void
band_started_cb (void)
{
	bands++;
}

/* Rows by number, or icons by their place among the accessible children. */
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

/* "1,3,4" */
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

/* Nemo Anywhere's checkmarks are the list view's first column, the one with
   no column-id. */
static GtkTreeViewColumn *
checkmark_column (void)
{
	GtkTreeViewColumn *first = gtk_tree_view_get_column (GTK_TREE_VIEW (pane), 0);

	return first != NULL && g_object_get_data (G_OBJECT (first), "column-id") == NULL ? first : NULL;
}

/* The rows with a mark, as selected_list puts them. */
static char *
marked_list (void)
{
	GtkTreeViewColumn *column = checkmark_column ();
	GtkTreeModel *model = gtk_tree_view_get_model (GTK_TREE_VIEW (pane));
	GString *list = g_string_new (NULL);
	GtkTreeIter iter;
	GList *cells;
	int row = 0;

	if (column == NULL || model == NULL || !gtk_tree_model_get_iter_first (model, &iter)) {
		return g_string_free (list, FALSE);
	}
	cells = gtk_cell_layout_get_cells (GTK_CELL_LAYOUT (column));
	do {
		char *text = NULL;

		gtk_tree_view_column_cell_set_cell_data (column, model, &iter, FALSE, FALSE);
		g_object_get (cells->data, "text", &text, NULL);
		if (text != NULL && text[0] != '\0') {
			g_string_append_printf (list, "%s%d", list->len > 0 ? "," : "", row);
		}
		g_free (text);
		row++;
	} while (gtk_tree_model_iter_next (model, &iter));
	g_list_free (cells);
	return g_string_free (list, FALSE);
}

static void
check_selection (const char *want, const char *what)
{
	char *got = selected_list ();
	/* Under Nemo Anywhere the marks are part of every list view check. */
	char *marked = anywhere && list_mode ? marked_list () : g_strdup (got);
	char *line = anywhere && list_mode ?
		g_strdup_printf ("%s (selected %s, marked %s, want %s)", what, got, marked, want) :
		g_strdup_printf ("%s (selected %s, want %s)", what, got, want);

	report (strcmp (got, want) == 0 && strcmp (marked, got) == 0, line);
	g_free (line);
	g_free (marked);
	g_free (got);
}

/* A row's middle, at x. */
static double
row_y (int row)
{
	GtkTreePath *path = gtk_tree_path_new_from_indices (row, -1);
	GdkRectangle area;

	gtk_tree_view_get_background_area (GTK_TREE_VIEW (pane), path, NULL, &area);
	gtk_tree_path_free (path);
	return area.y + area.height / 2;
}

/* Inside the Size column, left of its right-aligned text. */
static double
blank_x (void)
{
	GtkTreeView *tree_view = GTK_TREE_VIEW (pane);
	/* Nemo Anywhere's checkmarks come ahead of Name. */
	GtkTreeViewColumn *column = gtk_tree_view_get_column (tree_view, checkmark_column () != NULL ? 2 : 1);
	GtkTreePath *path = gtk_tree_path_new_from_indices (0, -1);
	GdkRectangle cell;

	gtk_tree_view_get_cell_area (tree_view, path, column, &cell);
	gtk_tree_path_free (path);
	return cell.x + 4;
}

static void
icon_center (int item, double *x, double *y)
{
	AtkObject *child = atk_object_ref_accessible_child (gtk_widget_get_accessible (pane), item);
	GObject *object = ATK_IS_GOBJECT_ACCESSIBLE (child) ?
		atk_gobject_accessible_get_object (ATK_GOBJECT_ACCESSIBLE (child)) : NULL;
	EelCanvasItem *canvas_item = (EelCanvasItem *) object;

	*x = (canvas_item->x1 + canvas_item->x2) / 2;
	*y = (canvas_item->y1 + canvas_item->y2) / 2;
	g_object_unref (child);
}

/* The spot in sight nearest to (near_x, near_y) that is clear of every icon. */
static gboolean
icon_blank (double near_x, double near_y, double *x, double *y)
{
	AtkObject *accessible = gtk_widget_get_accessible (pane);
	int n = atk_object_get_n_accessible_children (accessible);
	int left = (int) gtk_adjustment_get_value (gtk_scrollable_get_hadjustment (GTK_SCROLLABLE (pane)));
	int top = (int) gtk_adjustment_get_value (gtk_scrollable_get_vadjustment (GTK_SCROLLABLE (pane)));
	int width = gtk_widget_get_allocated_width (pane);
	int height = gtk_widget_get_allocated_height (pane);
	double best = -1;
	int px, py, i;

	for (py = top + 8; py < top + height - 8; py += 8) {
		for (px = left + 8; px < left + width - 8; px += 8) {
			gboolean clear = TRUE;
			double distance;

			for (i = 0; i < n && clear; i++) {
				AtkObject *child = atk_object_ref_accessible_child (accessible, i);
				EelCanvasItem *item = (EelCanvasItem *)
					atk_gobject_accessible_get_object (ATK_GOBJECT_ACCESSIBLE (child));

				clear = px < item->x1 - 8 || px > item->x2 + 8 ||
					py < item->y1 - 8 || py > item->y2 + 8;
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
	g_signal_connect (pane, "drag-end", G_CALLBACK (drag_end_cb), NULL);
	if (!list_mode) {
		g_signal_connect (pane, "band-select-started", G_CALLBACK (band_started_cb), NULL);
	}
	return TRUE;
}

/* The side pane's tree: Places in the list view run, the folder tree in
   the others. */
static GtkTreeView *
side_tree (void)
{
	GtkWidget *side = find_widget (window, list_mode ? "NemoPlacesSidebar" : "FMTreeView");
	GtkWidget *tree = side != NULL ? find_widget (side, "GtkTreeView") : NULL;

	return GTK_IS_TREE_VIEW (tree) ? GTK_TREE_VIEW (tree) : NULL;
}

/* The toolbar button behind an action. */
static void
find_action_cb (GtkWidget *widget, gpointer data)
{
	gpointer *find = data;

	if (find[1] != NULL) {
		return;
	}
	if (GTK_IS_BUTTON (widget) && gtk_widget_get_mapped (widget)) {
		G_GNUC_BEGIN_IGNORE_DEPRECATIONS
		GtkAction *action = gtk_activatable_get_related_action (GTK_ACTIVATABLE (widget));

		if (action != NULL && g_strcmp0 (gtk_action_get_name (action), find[0]) == 0) {
			find[1] = widget;
			return;
		}
		G_GNUC_END_IGNORE_DEPRECATIONS
	}
	if (GTK_IS_CONTAINER (widget)) {
		gtk_container_forall (GTK_CONTAINER (widget), find_action_cb, data);
	}
}

static void
find_lists_cb (GtkWidget *widget, gpointer data)
{
	if (is_a (widget, "NemoView") && gtk_widget_get_mapped (widget) &&
	    GTK_IS_TREE_VIEW (gtk_bin_get_child (GTK_BIN (widget)))) {
		*(GList **) data = g_list_append (*(GList **) data, gtk_bin_get_child (GTK_BIN (widget)));
	} else if (GTK_IS_CONTAINER (widget)) {
		gtk_container_forall (GTK_CONTAINER (widget), find_lists_cb, data);
	}
}

/* The list in the other pane, once there are two. */
static GtkWidget *
other_list (void)
{
	GList *lists = NULL, *l;
	GtkWidget *other = NULL;

	find_lists_cb (window, &lists);
	for (l = lists; l != NULL; l = l->next) {
		if (l->data != pane) {
			other = l->data;
		}
	}
	g_list_free (lists);
	return other;
}

static gboolean
pane_is_active (GtkWidget *widget)
{
	GtkWidget *window_pane = gtk_widget_get_ancestor (widget, g_type_from_name ("NemoWindowPane"));

	return window_pane != NULL &&
	       !gtk_style_context_has_class (gtk_widget_get_style_context (window_pane),
					     "nemo-inactive-pane");
}

static const int none[] = { -1 };
static const int first[] = { 0, -1 };
static const int first_and_fourth[] = { 0, 3, -1 };
static const int second_to_fourth[] = { 1, 2, 3, -1 };
static const int second[] = { 1, -1 };

/* Each step waits on what the one before set going, up to ten seconds. No
   two presses in a row hit one item, which would be a double click. */
static gboolean
list_tick (void)
{
	GtkTreeView *tree = side_tree ();
	GdkRectangle area;
	GtkTreePath *path;
	double below;

	switch (step) {
	case 1:
		select_items (none);
		report (gtk_tree_view_is_blank_at_pos (GTK_TREE_VIEW (pane), blank_x (), row_y (2),
						       NULL, NULL, NULL, NULL),
			"the spot pressed in the Size column is blank");
		press_and_move (bin, 1, 0, blank_x (), row_y (2), blank_x (), row_y (4));
		release (bin, 1, 0, blank_x (), row_y (4));
		if (classic) {
			/* From upstream's source: the blank part of the one row the
			   press selected lets GtkTreeView band. Not seen there, as its
			   Size column wasn't blank at this spot. */
			report (drags == 0, "a plain drag from a blank spot on a row not selected bands");
			check_selection ("2,3,4", "from the row pressed");
		} else {
			report (drags == 1, "a plain drag from a blank spot on a row drags files");
			check_selection ("2", "and the row pressed is selected at once");
		}
		break;
	case 2:
		select_items (second_to_fourth);
		press_and_move (bin, 1, 0, 40, row_y (1), 40, row_y (5));
		release (bin, 1, 0, 40, row_y (5));
		report (drags == 1, "a plain drag from a selected row drags files");
		check_selection ("1,2,3", "and keeps every selected row");
		break;
	case 3:
		select_items (first);
		press_and_move (bin, 1, GDK_SHIFT_MASK, blank_x (), row_y (3), blank_x (), row_y (5));
		release (bin, 1, GDK_SHIFT_MASK, blank_x (), row_y (5));
		if (classic) {
			report (drags == 1, "Shift and a drag from a row not selected drags files");
			check_selection ("0,1,2,3", "after picking the run from the cursor at once");
		} else {
			report (drags == 0, "Shift and a drag from a row not selected drags no files");
			check_selection ("3,4,5", "and bands, in place of the selection");
		}
		break;
	case 4:
		select_items (first);
		press_and_move (bin, 1, GDK_CONTROL_MASK, 40, row_y (4), 40, row_y (6));
		release (bin, 1, GDK_CONTROL_MASK, 40, row_y (6));
		if (classic) {
			report (drags == 1, "Ctrl and a drag from a row not selected drags files");
			check_selection ("0,4", "after adding the row at once");
		} else {
			report (drags == 0, "Ctrl and a drag from a row not selected drags no files");
			check_selection ("0,4,5,6", "and bands, adding to the selection");
		}
		break;
	case 5:
		select_items (first_and_fourth);
		press_and_move (bin, 1, GDK_CONTROL_MASK, 40, row_y (3), 40, row_y (6));
		release (bin, 1, GDK_CONTROL_MASK, 40, row_y (6));
		report (drags == 1, "Ctrl and a drag from a selected row drags files");
		check_selection ("0,3", "and keeps the selection");
		break;
	case 6:
		select_items (none);
		path = gtk_tree_path_new_from_indices (item_count () - 1, -1);
		gtk_tree_view_get_background_area (GTK_TREE_VIEW (pane), path, NULL, &area);
		gtk_tree_path_free (path);
		below = area.y + area.height + 20;
		if (below >= gdk_window_get_height (bin)) {
			report (FALSE, "there is room below the rows");
			break;
		}
		press_and_move (bin, 1, 0, 40, below, 40, row_y (item_count () - 3));
		release (bin, 1, 0, 40, row_y (item_count () - 3));
		report (drags == 0, "a drag below the rows drags no files");
		{
			char *want = g_strdup_printf ("%d,%d,%d", item_count () - 3,
						      item_count () - 2, item_count () - 1);

			check_selection (want, "and bands up into them");
			g_free (want);
		}
		break;
	case 7:
		close_menu ();
		select_items (second);
		press (bin, 3, 40, row_y (4), 0);
		break;
	case 8:
		report ((shown_menu () != NULL) == classic,
			classic ? "a right press opens the menu at once" :
				  "a right press opens no menu while the button is down");
		check_selection ("4", "and selects the row it is on");
		send (bin, GDK_BUTTON_RELEASE, 3, 40, row_y (4), GDK_BUTTON3_MASK);
		break;
	case 9:
		if (shown_menu () == NULL) {
			return FALSE;
		}
		report (TRUE, classic ? "and it stays open when the button comes up" :
				       "the menu opens when the button comes up");
		close_menu ();
		break;
	case 10:
		select_items (none);
		press_and_move (bin, 3, 0, 40, row_y (1), 40, row_y (3));
		release (bin, 3, 0, 40, row_y (3));
		break;
	case 11:
		if (classic) {
			report (shown_menu () != NULL && drags == 0, "a right drag opens the menu and drags no files");
			check_selection ("1", "and selects the row pressed alone");
		} else {
			report (shown_menu () == NULL && drags == 0, "a right drag opens no menu and drags no files");
			check_selection ("1,2,3", "and bands");
		}
		close_menu ();
		break;
	case 12:
		if (tree == NULL) {
			report (FALSE, "Places is showing");
			finish ();
			return TRUE;
		}
		path = gtk_tree_path_new_from_indices (0, 0, -1);
		gtk_tree_view_get_background_area (tree, path, NULL, &area);
		gtk_tree_path_free (path);
		press (gtk_tree_view_get_bin_window (tree), 3, 30, area.y + area.height / 2, 0);
		break;
	case 13:
		/* Upstream opens it on the press. Places isn't part of the
		   selection model, so it waits for the release in every one. */
		report (shown_menu () == NULL, "a right press in Places opens no menu while the button is down");
		path = gtk_tree_path_new_from_indices (0, 0, -1);
		gtk_tree_view_get_background_area (tree, path, NULL, &area);
		gtk_tree_path_free (path);
		send (gtk_tree_view_get_bin_window (tree), GDK_BUTTON_RELEASE, 3, 30,
		      area.y + area.height / 2, GDK_BUTTON3_MASK);
		break;
	case 14:
		if (shown_menu () == NULL) {
			return FALSE;
		}
		report (TRUE, "and opens it when the button comes up");
		close_menu ();
		break;
	case 15: {
		gpointer find[2] = { (gpointer) "Show Hide Extra Pane", NULL };

		find_action_cb (window, find);
		if (find[1] == NULL) {
			report (FALSE, "the extra pane button is showing");
			finish ();
			return TRUE;
		}
		gtk_button_clicked (GTK_BUTTON (find[1]));
		break;
	}
	case 16: {
		GtkWidget *other = other_list ();
		GdkEvent *event;
		gboolean handled;

		if (other == NULL || item_count () < 8) {
			return FALSE;
		}
		/* A pane with a selection, made inactive the way a click there does. */
		select_items (second);
		event = gdk_event_new (GDK_FOCUS_CHANGE);
		event->focus_change.window = g_object_ref (gtk_widget_get_window (other));
		event->focus_change.in = TRUE;
		g_signal_emit_by_name (other, "focus-in-event", event, &handled);
		gdk_event_free (event);
		break;
	}
	case 17:
		report (!pane_is_active (pane), "the pane pressed in next is not the active one");
		press_and_move (bin, 1, 0, blank_x (), row_y (5), blank_x (), row_y (7));
		release (bin, 1, 0, blank_x (), row_y (7));
		if (classic) {
			report (drags == 0, "a drag in the pane that wasn't active only wakes it");
			check_selection ("1", "and leaves its selection alone");
		} else {
			report (drags == 1, "a drag in the pane that wasn't active drags files at once");
			check_selection ("5", "and the row pressed is selected");
		}
		report (pane_is_active (pane), "and that pane is the active one now");
		finish ();
		return TRUE;
	}

	return TRUE;
}

static gboolean
icon_tick (void)
{
	GtkTreeView *tree = side_tree ();
	static double x0, y0, x1, y1;
	GdkRectangle area;
	GtkTreePath *path;

	switch (step) {
	case 1:
		select_items (none);
		icon_center (2, &x0, &y0);
		icon_center (5, &x1, &y1);
		press_and_move (bin, 1, 0, x0, y0, x1, y1);
		release (bin, 1, 0, x1, y1);
		report (drags == 1, "a plain drag from an icon drags files");
		check_selection ("2", "and the icon pressed is selected at once");
		break;
	case 2:
		select_items (first);
		icon_center (3, &x0, &y0);
		icon_center (5, &x1, &y1);
		press_and_move (bin, 1, GDK_SHIFT_MASK, x0, y0, x1, y1);
		break;
	case 3:
		/* The band follows the pointer on a timer, so it gets a tick. */
		release (bin, 1, GDK_SHIFT_MASK, x1, y1);
		if (classic) {
			report (drags == 1 && bands == 0, "Shift and a drag from an icon not selected drags files");
			check_selection ("0,1,2,3", "after picking the run at once");
		} else {
			report (drags == 0 && bands == 1, "Shift and a drag from an icon not selected bands");
			report (is_selected (3) && is_selected (5) && !is_selected (0),
				"and the band replaces the selection");
		}
		break;
	case 4:
		select_items (first);
		icon_center (4, &x0, &y0);
		icon_center (6, &x1, &y1);
		press_and_move (bin, 1, GDK_CONTROL_MASK, x0, y0, x1, y1);
		break;
	case 5:
		release (bin, 1, GDK_CONTROL_MASK, x1, y1);
		if (classic) {
			report (drags == 1 && bands == 0, "Ctrl and a drag from an icon not selected drags files");
			check_selection ("0,4", "after adding the icon at once");
		} else {
			report (drags == 0 && bands == 1, "Ctrl and a drag from an icon not selected bands");
			report (is_selected (0) && is_selected (4) && is_selected (6),
				"and the band adds to the selection");
		}
		break;
	case 6:
		select_items (first_and_fourth);
		icon_center (3, &x0, &y0);
		icon_center (6, &x1, &y1);
		press_and_move (bin, 1, GDK_CONTROL_MASK, x0, y0, x1, y1);
		release (bin, 1, GDK_CONTROL_MASK, x1, y1);
		report (drags == 1 && bands == 0, "Ctrl and a drag from a selected icon drags files");
		check_selection ("0,3", "and keeps the selection");
		break;
	case 7:
		close_menu ();
		select_items (second);
		icon_center (6, &x0, &y0);
		press (bin, 3, x0, y0, 0);
		break;
	case 8:
		report ((shown_menu () != NULL) == classic,
			classic ? "a right press opens the menu at once" :
				  "a right press opens no menu while the button is down");
		check_selection ("6", "and selects the icon it is on");
		send (bin, GDK_BUTTON_RELEASE, 3, x0, y0, GDK_BUTTON3_MASK);
		break;
	case 9:
		if (shown_menu () == NULL) {
			return FALSE;
		}
		report (TRUE, classic ? "and it stays open when the button comes up" :
				       "the menu opens when the button comes up");
		close_menu ();
		break;
	case 10:
		select_items (first);
		icon_center (7, &x1, &y1);
		if (!icon_blank (x1, y1, &x0, &y0)) {
			report (FALSE, "there is a spot clear of the icons");
			break;
		}
		press_and_move (bin, 3, 0, x0, y0, x1, y1);
		break;
	case 11:
		release (bin, 3, 0, x1, y1);
		break;
	case 12:
		if (classic) {
			report (shown_menu () != NULL && drags == 0 && bands == 0,
				"a right drag from the background opens the menu and doesn't band");
			check_selection ("", "and clears the selection");
		} else {
			report (shown_menu () == NULL && drags == 0 && bands == 1,
				"a right drag from the background bands and opens no menu");
			report (is_selected (7) && !is_selected (0), "and the band replaces the selection");
		}
		close_menu ();
		break;
	case 13:
		if (tree == NULL) {
			report (FALSE, "the tree pane is showing");
			finish ();
			return TRUE;
		}
		path = gtk_tree_path_new_from_indices (0, -1);
		gtk_tree_view_get_background_area (tree, path, NULL, &area);
		gtk_tree_path_free (path);
		press (gtk_tree_view_get_bin_window (tree), 3, 30, area.y + area.height / 2, 0);
		break;
	case 14:
		report ((shown_menu () != NULL) == classic,
			classic ? "a right press in the tree opens its menu at once" :
				  "a right press in the tree opens no menu while the button is down");
		path = gtk_tree_path_new_from_indices (0, -1);
		gtk_tree_view_get_background_area (tree, path, NULL, &area);
		gtk_tree_path_free (path);
		send (gtk_tree_view_get_bin_window (tree), GDK_BUTTON_RELEASE, 3, 30,
		      area.y + area.height / 2, GDK_BUTTON3_MASK);
		break;
	case 15:
		if (shown_menu () == NULL) {
			return FALSE;
		}
		report (TRUE, "and opens it when the button comes up");
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
	if (++waited > 100) {
		char *line = g_strdup_printf ("timed out at step %d", step);

		report (FALSE, line);
		g_free (line);
		close_menu ();
		finish ();
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
		if (title == NULL || !g_str_has_prefix (title, folder_name)) {
			return G_SOURCE_CONTINUE;
		}
		/* Icons are placed a moment after they are listed, so give it a second. */
		if ((pane == NULL && !find_pane ()) || item_count () < 8 || waited < 10) {
			return G_SOURCE_CONTINUE;
		}
		gtk_widget_grab_focus (pane);
	} else if (shown_menu () == NULL && drag_context == NULL &&
		   (dragging || gtk_grab_get_current () != NULL)) {
		/* A canceled drag takes a moment to let go. */
		return G_SOURCE_CONTINUE;
	} else if (!(list_mode ? list_tick () : icon_tick ())) {
		return G_SOURCE_CONTINUE;
	}

	if (finished) {
		return G_SOURCE_REMOVE;
	}
	step++;
	waited = 0;
	return G_SOURCE_CONTINUE;
}

__attribute__((constructor)) static void
probe_start (void)
{
	const char *path = g_getenv ("NEMO_DRAG_OUT");
	const char *view = g_getenv ("NEMO_DRAG_VIEW");

	classic = g_strcmp0 (g_getenv ("NEMO_DRAG_MODEL"), "nemo-classic") == 0;
	anywhere = g_strcmp0 (g_getenv ("NEMO_DRAG_MODEL"), "nemo-anywhere") == 0;
	folder_name = g_getenv ("NEMO_DRAG_FOLDER");
	if (path == NULL || view == NULL || folder_name == NULL) {
		return;
	}
	list_mode = g_strcmp0 (view, "list-view") == 0;
	out = fopen (path, "w");
	if (out == NULL) {
		return;
	}
	stamp = (guint32) (g_get_monotonic_time () / 1000);
	g_timeout_add (100, tick, NULL);
}
