/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */

/* test-nemo-selection-rules-probe.c - clicks and keys in a folder view and
   the tree pane from inside the program, and checks what each does to the
   selection and the focus.

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

/* Preloaded into the real program by test-nemo-selection-rules. Once the
 * window shows $NEMO_SEL_FOLDER, it clicks and types in the folder view named
 * by $NEMO_SEL_VIEW through GTK's own event path, then in the folder tree,
 * then opens folders from home, and checks each rule of the selection model
 * in $NEMO_SEL_MODEL. Where the next item over depends on the layout, the icon views work it out from where
 * the icons are. Each list view check under Nemo Anywhere, and each icon view
 * check, also looks at the checkmarks. One "ok" or "FAIL" line per check goes to $NEMO_SEL_OUT, and
 * "done" at the end. Drags are left to test-nemo-drag-select. */

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <gtk/gtk.h>
#include <gdk/gdkkeysyms.h>
#include <eel/eel-canvas.h>

#define BUTTON_MASK(b) (GDK_BUTTON1_MASK << ((b) - 1))

static FILE      *out;
static const char *folder_name;
static gboolean   list_mode;
static gboolean   compact_mode;
static gboolean   classic;
static gboolean   anywhere;
static GtkWidget *window;
static GtkWidget *pane;
static GdkWindow *bin;
static int        step;
static int        waited;
static guint32    stamp;
static gboolean   finished;
static char      *title_before;
static int        tabs_before;
static int        last_clicked = -1;
static int        home_item = -1;
static gboolean   menu_on_press;
static gint64     last_click_time;
static guint      glib_handler;
static int        glib_criticals;

static void
report (gboolean passed, const char *what)
{
	fprintf (out, "%s %s\n", passed ? "ok" : "FAIL", what);
	fflush (out);
}

/* The tree's key delay once removed a timer that had already run (#2026102113191977). */
static void
count_glib_critical (const gchar *domain, GLogLevelFlags level, const gchar *message,
		     gpointer data)
{
	glib_criticals++;
	g_log_default_handler (domain, level, message, data);
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

/* A closed menu's pointer and keyboard grab lasts until the X server has
   processed the ungrab. On a busy box that can take a while, and a click or
   key sent before then never gets to the view (#2026102413432024). */
static gboolean
grab_held (void)
{
	GdkDisplay *display = gtk_widget_get_display (window);
	GdkSeat *seat = gdk_display_get_default_seat (display);

	return gtk_grab_get_current () != NULL ||
	       gdk_display_device_is_grabbed (display, gdk_seat_get_pointer (seat)) ||
	       gdk_display_device_is_grabbed (display, gdk_seat_get_keyboard (seat));
}

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

/* The icon view sends a press to the item the pointer last moved over. */
static void
press (GdkWindow *window_for, guint button, double x, double y, guint modifiers)
{
	send (window_for, GDK_MOTION_NOTIFY, 0, x, y, modifiers);
	send (window_for, GDK_BUTTON_PRESS, button, x, y, modifiers);
}

static void
press_release (GdkWindow *window_for, guint button, double x, double y, guint modifiers)
{
	press (window_for, button, x, y, modifiers);
	send (window_for, GDK_BUTTON_RELEASE, button, x, y, modifiers | BUTTON_MASK (button));
}

static void
send_key_event (GdkEventType type, guint keyval, guint modifiers)
{
	GdkDisplay *display = gtk_widget_get_display (window);
	GdkEvent *event = gdk_event_new (type);
	GdkKeymapKey *keys = NULL;
	gint n_keys = 0;

	stamp += 20;
	event->key.window = g_object_ref (gtk_widget_get_window (window));
	event->key.send_event = TRUE;
	event->key.time = stamp;
	event->key.keyval = keyval;
	event->key.state = modifiers;
	if (gdk_keymap_get_entries_for_keyval (gdk_keymap_get_for_display (display),
					       gdk_keyval_to_lower (keyval), &keys, &n_keys) && n_keys > 0) {
		event->key.hardware_keycode = keys[0].keycode;
		event->key.group = keys[0].group;
	}
	g_free (keys);
	gdk_event_set_device (event, gdk_seat_get_keyboard (gdk_display_get_default_seat (display)));

	gtk_main_do_event (event);
	gdk_event_free (event);
}

static void
key (guint keyval, guint modifiers)
{
	send_key_event (GDK_KEY_PRESS, keyval, modifiers);
	send_key_event (GDK_KEY_RELEASE, keyval, modifiers);
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

static int
compare_ints (gconstpointer a, gconstpointer b)
{
	return *(const int *) a - *(const int *) b;
}

/* The items given, ending in -1, as selected_list would put them. */
static char *
items (int first, ...)
{
	GArray *list = g_array_new (FALSE, FALSE, sizeof (int));
	GString *text = g_string_new (NULL);
	va_list args;
	guint i;
	int item;

	va_start (args, first);
	for (item = first; item >= 0; item = va_arg (args, int)) {
		g_array_append_val (list, item);
	}
	va_end (args);
	g_array_sort (list, compare_ints);
	for (i = 0; i < list->len; i++) {
		if (i == 0 || g_array_index (list, int, i) != g_array_index (list, int, i - 1)) {
			g_string_append_printf (text, "%s%d", text->len > 0 ? "," : "",
						g_array_index (list, int, i));
		}
	}
	g_array_free (list, TRUE);
	return g_string_free (text, FALSE);
}

/* lo to hi, both included, leaving out skip. */
static char *
run_of (int lo, int hi, int skip)
{
	GString *text = g_string_new (NULL);
	int i;

	for (i = MIN (lo, hi); i <= MAX (lo, hi); i++) {
		if (i != skip) {
			g_string_append_printf (text, "%s%d", text->len > 0 ? "," : "", i);
		}
	}
	return g_string_free (text, FALSE);
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

static EelCanvasItem *canvas_item (int item);

/* The icons with a mark, as selected_list puts them. */
static char *
icon_marked_list (void)
{
	GString *list = g_string_new (NULL);
	int i;

	for (i = 0; i < item_count (); i++) {
		gboolean marked = FALSE;

		g_object_get (canvas_item (i), "dim-checkmark", &marked, NULL);
		if (marked) {
			g_string_append_printf (list, "%s%d", list->len > 0 ? "," : "", i);
		}
	}
	return g_string_free (list, FALSE);
}

/* Under Nemo Anywhere the marks are part of every check in all 3 views, and
   under the other models the icon and compact views have none. */
static char *
marks (void)
{
	if (list_mode) {
		return anywhere ? marked_list () : NULL;
	}
	return icon_marked_list ();
}

static void
want (char *wanted, const char *what)
{
	char *got = selected_list ();
	char *marked = marks ();
	char *line = marked != NULL ?
		g_strdup_printf ("%s (selected %s, marked %s, want %s)", what, got, marked, wanted) :
		g_strdup_printf ("%s (selected %s, want %s)", what, got, wanted);

	report (strcmp (got, wanted) == 0 &&
		(marked == NULL || strcmp (marked, anywhere ? got : "") == 0), line);
	g_free (line);
	g_free (marked);
	g_free (got);
	g_free (wanted);
}

static double
row_y (int row)
{
	GtkTreePath *path = gtk_tree_path_new_from_indices (row, -1);
	GdkRectangle area;

	gtk_tree_view_get_background_area (GTK_TREE_VIEW (pane), path, NULL, &area);
	gtk_tree_path_free (path);
	return area.y + area.height / 2;
}

static EelCanvasItem *
canvas_item (int item)
{
	AtkObject *child = atk_object_ref_accessible_child (gtk_widget_get_accessible (pane), item);
	GObject *object = ATK_IS_GOBJECT_ACCESSIBLE (child) ?
		atk_gobject_accessible_get_object (ATK_GOBJECT_ACCESSIBLE (child)) : NULL;

	g_object_unref (child);
	return (EelCanvasItem *) object;
}

/* The name an item shows: the name column's text in the list view, and the
   accessible name of the icon otherwise. */
static char *
item_name (int item)
{
	char *name = NULL;

	if (list_mode) {
		GtkTreeModel *model = gtk_tree_view_get_model (GTK_TREE_VIEW (pane));
		/* The name column, which isn't the first one under Nemo Anywhere. */
		GtkTreeViewColumn *column = gtk_tree_view_get_expander_column (GTK_TREE_VIEW (pane));
		GtkTreePath *path = gtk_tree_path_new_from_indices (item, -1);
		GtkTreeIter iter;
		GList *cells, *l;

		if (gtk_tree_model_get_iter (model, &iter, path)) {
			gtk_tree_view_column_cell_set_cell_data (column, model, &iter, FALSE, FALSE);
			cells = gtk_cell_layout_get_cells (GTK_CELL_LAYOUT (column));
			for (l = cells; l != NULL && name == NULL; l = l->next) {
				if (GTK_IS_CELL_RENDERER_TEXT (l->data)) {
					g_object_get (l->data, "text", &name, NULL);
				}
			}
			g_list_free (cells);
		}
		gtk_tree_path_free (path);
	} else {
		AtkObject *child = atk_object_ref_accessible_child (gtk_widget_get_accessible (pane), item);

		name = g_strdup (atk_object_get_name (child));
		g_object_unref (child);
	}
	return name;
}

/* The item with this name, or -1. What else is in the home folder depends
   on the box, so its folders are found by name (#2026102821124143). */
static int
item_named (const char *wanted)
{
	int i;

	for (i = 0; i < item_count (); i++) {
		char *name = item_name (i);
		gboolean found = name != NULL && strcmp (name, wanted) == 0;

		g_free (name);
		if (found) {
			return i;
		}
	}
	return -1;
}

static void
item_point (int item, double *x, double *y)
{
	EelCanvasItem *canvas;

	if (list_mode) {
		*x = 40;
		*y = row_y (item);
		return;
	}
	canvas = canvas_item (item);
	*x = (canvas->x1 + canvas->x2) / 2;
	*y = (canvas->y1 + canvas->y2) / 2;
}

/* Rows line up by their tops. Icon view centers each column, and compact
   view lines each one up on the left. */
static double
column_of (EelCanvasItem *canvas)
{
	return compact_mode ? canvas->x1 : (canvas->x1 + canvas->x2) / 2;
}

/* The nearest item one step over, by where the icons are, or -1. dx and dy
   are -1, 0 or 1, one of them 0. */
static int
next_item (int item, int dx, int dy)
{
	EelCanvasItem *from;
	double best = -1;
	int found = -1;
	int i;

	if (list_mode) {
		i = item + dy;
		return dx == 0 && i >= 0 && i < item_count () ? i : -1;
	}

	from = canvas_item (item);
	for (i = 0; i < item_count (); i++) {
		EelCanvasItem *other = canvas_item (i);
		double across = dy != 0 ? column_of (other) - column_of (from) : other->y1 - from->y1;
		double along = dy != 0 ? (other->y1 - from->y1) * dy : (column_of (other) - column_of (from)) * dx;

		if (i == item || ABS (across) > 4 || along < 4) {
			continue;
		}
		if (best < 0 || along < best) {
			best = along;
			found = i;
		}
	}
	return found;
}

static void
click (int item, guint modifiers)
{
	double x, y;

	item_point (item, &x, &y);
	/* The views time a double click by the clock, not by the event, so a
	   second click on one item waits it out. */
	if (item == last_clicked && g_get_monotonic_time () - last_click_time < 800 * 1000) {
		g_usleep ((gulong) (800 * 1000 - (g_get_monotonic_time () - last_click_time)));
	}
	stamp += 1000;
	press_release (bin, 1, x, y, modifiers);
	last_clicked = item;
	last_click_time = g_get_monotonic_time ();
}

/* The spot in sight nearest to (near_x, near_y) that is clear of every icon. */
static gboolean
icon_blank (double near_x, double near_y, double *x, double *y)
{
	int width = gtk_widget_get_allocated_width (pane);
	int height = gtk_widget_get_allocated_height (pane);
	double best = -1;
	int px, py, i;

	for (py = 8; py < height - 8; py += 8) {
		for (px = 8; px < width - 8; px += 8) {
			gboolean clear = TRUE;
			double distance;

			for (i = 0; i < item_count () && clear; i++) {
				EelCanvasItem *item = canvas_item (i);

				clear = px < item->x1 - 8 || px > item->x2 + 8 ||
					py < item->y1 - 8 || py > item->y2 + 8;
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
click_blank (guint modifiers)
{
	double x = 0, y = 0;

	if (list_mode) {
		GtkTreePath *path = gtk_tree_path_new_from_indices (item_count () - 1, -1);
		GdkRectangle area;

		gtk_tree_view_get_background_area (GTK_TREE_VIEW (pane), path, NULL, &area);
		gtk_tree_path_free (path);
		x = 40;
		y = area.y + area.height + 20;
		if (y >= gdk_window_get_height (bin)) {
			return FALSE;
		}
	} else {
		double near_x, near_y;

		item_point (item_count () - 1, &near_x, &near_y);
		if (!icon_blank (near_x, near_y, &x, &y)) {
			return FALSE;
		}
	}
	stamp += 1000;
	press_release (bin, 1, x, y, modifiers);
	return TRUE;
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
	return TRUE;
}

static GtkTreeView *
folder_tree (void)
{
	GtkWidget *side = find_widget (window, "FMTreeView");
	GtkWidget *tree = side != NULL ? find_widget (side, "GtkTreeView") : NULL;

	return GTK_IS_TREE_VIEW (tree) ? GTK_TREE_VIEW (tree) : NULL;
}

/* "0:2", or "" with nothing selected. */
static char *
tree_selected (GtkTreeView *tree)
{
	GtkTreeModel *model;
	GtkTreeIter iter;

	if (!gtk_tree_selection_get_selected (gtk_tree_view_get_selection (tree), &model, &iter)) {
		return g_strdup ("");
	}
	return gtk_tree_model_get_string_from_iter (model, &iter);
}

/* Adds each marked row under parent to marked, as tree_selected names it. */
static void
tree_marked_rows (GtkTreeModel *model, GtkTreeViewColumn *column, GtkTreeIter *parent, GString *marked)
{
	GtkTreeIter iter;
	gboolean more;

	for (more = gtk_tree_model_iter_children (model, &iter, parent); more;
	     more = gtk_tree_model_iter_next (model, &iter)) {
		GList *cells = gtk_cell_layout_get_cells (GTK_CELL_LAYOUT (column));
		char *text = NULL;

		gtk_tree_view_column_cell_set_cell_data (column, model, &iter, FALSE, FALSE);
		if (cells != NULL) {
			g_object_get (cells->data, "text", &text, NULL);
		}
		g_list_free (cells);
		if (text != NULL && text[0] != '\0') {
			char *row = gtk_tree_model_get_string_from_iter (model, &iter);

			g_string_append_printf (marked, "%s%s", marked->len > 0 ? " " : "", row);
			g_free (row);
		}
		g_free (text);
		tree_marked_rows (model, column, &iter, marked);
	}
}

/* Under Nemo Anywhere the tree's first column has the dim checkmark, on the
   selected row only (#2026111013031711). The other models have no column for it. */
static gboolean
tree_marks_follow (GtkTreeView *tree, const char *selected)
{
	GtkTreeViewColumn *marks = gtk_tree_view_get_column (tree, 0);
	GString *marked;
	gboolean right;

	if (!anywhere) {
		return gtk_tree_view_get_n_columns (tree) == 1;
	}
	if (gtk_tree_view_get_n_columns (tree) != 2 || marks == gtk_tree_view_get_expander_column (tree) ||
	    gtk_tree_view_column_get_width (marks) <= 0 || gtk_tree_view_column_get_width (marks) >= 40) {
		return FALSE;
	}
	marked = g_string_new (NULL);
	tree_marked_rows (gtk_tree_view_get_model (tree), marks, NULL, marked);
	right = strcmp (marked->str, selected) == 0;
	if (!right) {
		fprintf (out, "# tree marks on \"%s\", selected \"%s\"\n", marked->str, selected);
	}
	g_string_free (marked, TRUE);
	return right;
}

static void
want_tree (GtkTreeView *tree, const char *wanted, const char *what)
{
	char *got = tree_selected (tree);
	char *line = g_strdup_printf ("%s (tree row %s, want %s)", what, got, wanted);

	report (strcmp (got, wanted) == 0 && tree_marks_follow (tree, got), line);
	g_free (line);
	g_free (got);
}

static void
click_tree_row (GtkTreeView *tree, const char *row, guint modifiers)
{
	GtkTreePath *path = gtk_tree_path_new_from_string (row);
	GdkRectangle area;

	gtk_tree_view_get_cell_area (tree, path, gtk_tree_view_get_expander_column (tree), &area);
	gtk_tree_path_free (path);
	stamp += 1000;
	press_release (gtk_tree_view_get_bin_window (tree), 1, area.x + 30, area.y + area.height / 2,
		       modifiers);
}

/* The title can change more than once on the way to a folder, and a key in
   the tree opens its row a moment later, so it has to sit still a while. */
static int
tab_count (void)
{
	GtkWidget *notebook = find_widget (window, "GtkNotebook");

	return notebook != NULL ? gtk_notebook_get_n_pages (GTK_NOTEBOOK (notebook)) : -1;
}

static gboolean
title_changed (void)
{
	static char *seen;
	static gint64 seen_at;
	const char *title = gtk_window_get_title (GTK_WINDOW (window));

	if (g_strcmp0 (title, seen) != 0) {
		g_free (seen);
		seen = g_strdup (title);
		seen_at = g_get_monotonic_time ();
		return FALSE;
	}
	return g_get_monotonic_time () - seen_at > 800 * 1000 &&
	       g_strcmp0 (title, title_before) != 0;
}

static void
note_title (void)
{
	g_free (title_before);
	title_before = g_strdup (gtk_window_get_title (GTK_WINDOW (window)));
}

static const char *const typed = "file-07";

/* Each step waits on what the one before set going, up to 10 seconds. */
/* The list hides its cursor's outline with these, and an icon says whether
   it draws one. */
static gboolean
outline_shown (void)
{
	GtkStyleContext *context = gtk_widget_get_style_context (pane);
	int i;

	if (list_mode) {
		return !gtk_style_context_has_class (context, "nemo-focus-quiet") &&
		       !gtk_style_context_has_class (context, "nemo-cursor-forgotten");
	}
	for (i = 0; i < item_count (); i++) {
		gboolean outlined = FALSE;

		g_object_get (canvas_item (i), "focus-outline", &outlined, NULL);
		if (outlined) {
			return TRUE;
		}
	}
	return FALSE;
}

/* The icon views' type-ahead is a popup window of its own. */
static gboolean
typeahead_shown (void)
{
	GList *toplevels, *l;
	gboolean shown = FALSE;

	if (list_mode) {
		GtkEntry *entry = gtk_tree_view_get_search_entry (GTK_TREE_VIEW (pane));

		return entry != NULL && gtk_widget_get_mapped (GTK_WIDGET (entry));
	}
	toplevels = gtk_window_list_toplevels ();
	for (l = toplevels; l != NULL && !shown; l = l->next) {
		shown = gtk_window_get_window_type (l->data) == GTK_WINDOW_POPUP &&
			find_widget (l->data, "GtkEntry") != NULL;
	}
	g_list_free (toplevels);
	return shown;
}

/* The far end of an item's column, or of its row in the compact view, which
   scrolls sideways. */
static int
page_end (int item, int toward)
{
	int next;

	while ((next = next_item (item, compact_mode ? toward : 0, compact_mode ? 0 : toward)) >= 0) {
		item = next;
	}
	return item;
}

/* The item one step over, or the same one at an edge. */
static int
step_or_stay (int item, int dx, int dy)
{
	int next = next_item (item, dx, dy);

	return next >= 0 ? next : item;
}

/* Nemo Anywhere's own icon view keys (#2026102412041969). The compact view
   shares them, in its own layout, below. */
static void
anywhere_icon_keys (void)
{
	int b1 = next_item (1, 0, 1);
	int b2 = next_item (2, 0, 1);
	int c1 = next_item (b1, 0, 1);
	int r2 = next_item (1, 1, 0);
	int r3 = next_item (r2, 1, 0);
	int r4 = next_item (r3, 1, 0);

	if (b1 < 0 || b2 < 0 || c1 < 0 || r4 < 0) {
		report (FALSE, "there are 3 rows of 5 icons or more");
		return;
	}
	click (2, 0);
	report (!outline_shown (), "a click shows no focus outline");
	click (b1, 0);
	key (GDK_KEY_Down, GDK_SHIFT_MASK);
	report (!outline_shown (), "nor does Shift+Down");
	key (GDK_KEY_Up, 0);
	want (items (1, -1), "Up counts from the first item of a run");
	click (b1, 0);
	key (GDK_KEY_Up, GDK_SHIFT_MASK);
	key (GDK_KEY_Down, 0);
	want (items (c1, -1), "and Down from the last");
	click (r3, 0);
	key (GDK_KEY_Left, GDK_SHIFT_MASK);
	key (GDK_KEY_Right, 0);
	want (items (r4, -1), "Right counts from the last item of a run");
	click (r2, 0);
	key (GDK_KEY_Right, GDK_SHIFT_MASK);
	key (GDK_KEY_Left, 0);
	want (items (1, -1), "and Left from the first");
	click (b2, 0);
	key (GDK_KEY_Left, GDK_SHIFT_MASK);
	key (GDK_KEY_Page_Down, 0);
	want (items (page_end (b2, 1), -1), "Page Down counts from the last item of a run");
	click (b1, 0);
	key (GDK_KEY_Right, GDK_SHIFT_MASK);
	key (GDK_KEY_Page_Up, 0);
	want (items (1, -1), "and Page Up from the first");

	click (1, 0);
	key (GDK_KEY_Down, GDK_CONTROL_MASK);
	key (GDK_KEY_Down, GDK_CONTROL_MASK);
	want (items (1, -1), "Ctrl+Down twice leaves the selection alone");
	report (outline_shown (), "and the focus outline shows on the icon it went to");
	key (GDK_KEY_Down, 0);
	want (items (step_or_stay (c1, 0, 1), -1), "Down then counts from the focus");
	report (!outline_shown (), "and the outline goes");

	click (1, 0);
	key (GDK_KEY_Right, GDK_CONTROL_MASK);
	report (outline_shown (), "Ctrl+Right shows the focus outline");
	key (GDK_KEY_space, GDK_CONTROL_MASK);
	report (!outline_shown (), "and it goes once Ctrl+Space selects the focused icon");
	key (GDK_KEY_Right, GDK_CONTROL_MASK);
	key (GDK_KEY_Right, GDK_CONTROL_MASK | GDK_SHIFT_MASK);
	want (items (1, r2, r3, r4, -1), "Ctrl+Shift+Right adds the run from the Ctrl+Space icon and keeps the rest");
	key (GDK_KEY_Down, GDK_CONTROL_MASK | GDK_SHIFT_MASK);
	want (run_of (1, next_item (r4, 0, 1), -1), "and Ctrl+Shift+Down runs on from it to the row below");

	key (GDK_KEY_Escape, 0);
	want (g_strdup (""), "Escape clears the selection");
	report (!outline_shown (), "and shows no focus outline");
	key (GDK_KEY_Escape, 0);
	want (g_strdup (""), "a second Escape changes nothing");
	key (GDK_KEY_space, GDK_CONTROL_MASK);
	want (g_strdup (""), "nor does Ctrl+Space with no focus");
	report (!outline_shown (), "and it shows no outline");
	key (GDK_KEY_Right, 0);
	want (items (0, -1), "and Right then starts at the first item");

	click (3, 0);
	key (GDK_KEY_space, 0);
	want (items (3, -1), "Space leaves the selection alone");
	report (!typeahead_shown (), "and starts no type-ahead");
}

/* The icon view's keys in the compact view (#2026102412041971). Items run
   down each column, so a run is in that order: Up and Down step through it,
   and Left, Right and the page keys go a column across. */
static void
anywhere_compact_keys (void)
{
	int r2 = next_item (1, 1, 0);
	int r3 = r2 >= 0 ? next_item (r2, 1, 0) : -1;
	int d1 = r2 >= 0 ? next_item (r2, 0, 1) : -1;
	int d2 = d1 >= 0 ? next_item (d1, 0, 1) : -1;
	int e2 = d2 >= 0 ? next_item (d2, 1, 0) : -1;
	int head = next_item (0, 1, 0);
	int foot = 0, next;
	char *run;

	while ((next = next_item (foot, 0, 1)) >= 0) {
		foot = next;
	}
	/* The first column's foot is the item just before the next one's head. */
	if (r3 < 0 || e2 < 0 || foot < 4 || head != foot + 1) {
		report (FALSE, "there are 3 columns of 5 items or more");
		return;
	}
	click (2, 0);
	report (!outline_shown (), "a click shows no focus outline");
	key (GDK_KEY_Down, GDK_SHIFT_MASK);
	report (!outline_shown (), "nor does Shift+Down");
	key (GDK_KEY_Up, 0);
	want (items (1, -1), "Up counts from the first item of a run");
	click (2, 0);
	key (GDK_KEY_Up, GDK_SHIFT_MASK);
	key (GDK_KEY_Down, 0);
	want (items (3, -1), "and Down from the last");
	click (foot - 1, 0);
	key (GDK_KEY_Down, GDK_SHIFT_MASK);
	key (GDK_KEY_Down, 0);
	want (items (head, -1), "Down from the last item of a run at a column's foot goes on to the next column's head");
	click (head, 0);
	key (GDK_KEY_Down, GDK_SHIFT_MASK);
	key (GDK_KEY_Up, 0);
	want (items (foot, -1), "and Up from the first back to the foot");
	click (r2, 0);
	key (GDK_KEY_Left, GDK_SHIFT_MASK);
	key (GDK_KEY_Right, 0);
	want (items (r3, -1), "Right counts from the last item of a run");
	click (r2, 0);
	key (GDK_KEY_Right, GDK_SHIFT_MASK);
	key (GDK_KEY_Left, 0);
	want (items (1, -1), "and Left from the first");
	click (3, 0);
	key (GDK_KEY_Up, GDK_SHIFT_MASK);
	key (GDK_KEY_Page_Down, 0);
	want (items (page_end (3, 1), -1), "Page Down counts from the last item of a run");
	click (r2, 0);
	key (GDK_KEY_Down, GDK_SHIFT_MASK);
	key (GDK_KEY_Page_Up, 0);
	want (items (page_end (r2, -1), -1), "and Page Up from the first");

	click (1, 0);
	key (GDK_KEY_Down, GDK_CONTROL_MASK);
	key (GDK_KEY_Down, GDK_CONTROL_MASK);
	want (items (1, -1), "Ctrl+Down twice leaves the selection alone");
	report (outline_shown (), "and the focus outline shows on the item it went to");
	key (GDK_KEY_Down, 0);
	want (items (4, -1), "Down then counts from the focus");
	report (!outline_shown (), "and the outline goes");

	click (1, 0);
	key (GDK_KEY_Right, GDK_CONTROL_MASK);
	report (outline_shown (), "Ctrl+Right shows the focus outline");
	key (GDK_KEY_space, GDK_CONTROL_MASK);
	report (!outline_shown (), "and it goes once Ctrl+Space selects the focused item");
	key (GDK_KEY_Down, GDK_CONTROL_MASK);
	key (GDK_KEY_Down, GDK_CONTROL_MASK | GDK_SHIFT_MASK);
	want (items (1, r2, d1, d2, -1), "Ctrl+Shift+Down adds the run from the Ctrl+Space item and keeps the rest");
	key (GDK_KEY_Right, GDK_CONTROL_MASK | GDK_SHIFT_MASK);
	run = run_of (r2, e2, -1);
	want (g_strdup_printf ("1,%s", run), "and Ctrl+Shift+Right runs on from it to the next column");
	g_free (run);

	key (GDK_KEY_Escape, 0);
	want (g_strdup (""), "Escape clears the selection");
	report (!outline_shown (), "and shows no focus outline");
	key (GDK_KEY_Escape, 0);
	want (g_strdup (""), "a second Escape changes nothing");
	key (GDK_KEY_space, GDK_CONTROL_MASK);
	want (g_strdup (""), "nor does Ctrl+Space with no focus");
	report (!outline_shown (), "and it shows no outline");
	key (GDK_KEY_Down, 0);
	want (items (0, -1), "and Down then starts at the first item");

	click (3, 0);
	key (GDK_KEY_space, 0);
	want (items (3, -1), "Space leaves the selection alone");
	report (!typeahead_shown (), "and starts no type-ahead");
}

/* Nemo Anywhere's own list keys (#2026102412041967). */
static void
anywhere_list_keys (void)
{
	click (2, 0);
	report (!outline_shown (), "a click shows no focus outline");
	key (GDK_KEY_Down, GDK_SHIFT_MASK);
	key (GDK_KEY_Down, GDK_SHIFT_MASK);
	key (GDK_KEY_Up, 0);
	want (items (1, -1), "Up counts from the first item of a run");
	click (4, 0);
	key (GDK_KEY_Up, GDK_SHIFT_MASK);
	key (GDK_KEY_Up, GDK_SHIFT_MASK);
	key (GDK_KEY_Down, 0);
	want (items (5, -1), "and Down from the last");

	click (2, 0);
	key (GDK_KEY_Down, GDK_CONTROL_MASK);
	key (GDK_KEY_Down, GDK_CONTROL_MASK);
	want (items (2, -1), "Ctrl+Down twice leaves the selection alone");
	report (outline_shown (), "and the focus outline shows on the row it went to");
	key (GDK_KEY_Down, 0);
	want (items (5, -1), "Down then counts from the focus");
	report (!outline_shown (), "and the outline goes");

	click (5, 0);
	key (GDK_KEY_Down, GDK_CONTROL_MASK);
	key (GDK_KEY_space, GDK_CONTROL_MASK);
	report (!outline_shown (), "the outline goes once Ctrl+Space selects the focused row");
	key (GDK_KEY_Down, GDK_CONTROL_MASK);
	key (GDK_KEY_Down, GDK_CONTROL_MASK | GDK_SHIFT_MASK);
	want (items (5, 6, 7, 8, -1), "Ctrl+Shift+Down adds the run from the Ctrl+Space row and keeps the rest");
	click (3, GDK_SHIFT_MASK);
	want (run_of (3, 6, -1), "and a Shift+click then runs from that row too");

	key (GDK_KEY_Escape, 0);
	want (g_strdup (""), "Escape clears the selection");
	report (!outline_shown (), "and shows no focus outline");
	key (GDK_KEY_Escape, 0);
	want (g_strdup (""), "a second Escape changes nothing");
	key (GDK_KEY_Down, 0);
	want (items (0, -1), "and Down then starts at the first item");

	click (3, 0);
	key (GDK_KEY_space, 0);
	want (items (3, -1), "Space leaves the selection alone");
	report (!typeahead_shown (), "and starts no type-ahead");
}

static gboolean
rules_tick (void)
{
	GtkTreeView *tree = folder_tree ();
	int last = item_count () - 1;
	double x, y;
	int i;

	switch (step) {
	/* Mouse. */
	case 1:
		if (list_mode) {
			GtkTreeViewColumn *marks = checkmark_column ();
			int width = marks != NULL ? gtk_tree_view_column_get_width (marks) : 0;
			char *line = g_strdup_printf ("%s (%d px)",
				anywhere ? "the checkmark column is first and thin, with no expanders in it" :
				"there is no checkmark column", width);

			report ((marks != NULL) == anywhere &&
				(marks == NULL ||
				 (gtk_tree_view_get_expander_column (GTK_TREE_VIEW (pane)) != marks &&
				  width > 0 && width < 40)),
				line);
			g_free (line);
		}
		click (1, 0);
		want (items (1, -1), "a click selects the item alone");
		break;
	case 2:
		click (3, GDK_CONTROL_MASK);
		want (items (1, 3, -1), "Ctrl+click adds the item");
		break;
	case 3:
		click (1, GDK_CONTROL_MASK);
		want (items (3, -1), "Ctrl+click on a selected item takes it out");
		break;
	case 4:
		click (2, 0);
		click (5, GDK_SHIFT_MASK);
		want (run_of (2, 5, -1), "Shift+click selects the run from the last click, in place of the selection");
		break;
	case 5:
		click (0, GDK_SHIFT_MASK);
		want (run_of (0, 2, -1), "a second Shift+click runs from the same item");
		break;
	case 6:
		click (6, 0);
		click (2, GDK_CONTROL_MASK);
		click (4, GDK_CONTROL_MASK | GDK_SHIFT_MASK);
		want (items (2, 3, 4, 6, -1), "Ctrl+Shift+click adds the run from the last Ctrl+click");
		if (anywhere) {
			/* #2026111012335173 */
			click (0, GDK_SHIFT_MASK);
			want (run_of (0, 2, -1), "and a Shift+click then runs from the same item");
			click (6, 0);
			click (2, GDK_CONTROL_MASK);
			click (4, GDK_SHIFT_MASK);
			want (run_of (2, 4, -1), "a Shift+click after a Ctrl+click runs from the Ctrl+clicked item");
		}
		break;
	case 7:
		if (!click_blank (0)) {
			report (FALSE, "there is a spot clear of the items");
			break;
		}
		want (g_strdup (""), "a click on empty space clears the selection");
		/* Upstream's list clears it even with Ctrl, and its icon views keep
		   it with Shift too. */
		click (1, 0);
		click_blank (GDK_CONTROL_MASK);
		if (list_mode && classic) {
			want (g_strdup (""), "and so does a Ctrl+click there");
		} else {
			want (items (1, -1), "a Ctrl+click there keeps it");
		}
		click (1, 0);
		click_blank (GDK_SHIFT_MASK);
		if (!list_mode && classic) {
			want (items (1, -1), "a Shift+click there keeps it");
		} else {
			want (g_strdup (""), "a Shift+click there clears it");
		}
		break;
	case 8:
		close_menu ();
		click (1, 0);
		click (2, GDK_CONTROL_MASK);
		item_point (2, &x, &y);
		stamp += 1000;
		press (bin, 3, x, y, 0);
		menu_on_press = shown_menu () != NULL;
		send (bin, GDK_BUTTON_RELEASE, 3, x, y, GDK_BUTTON3_MASK);
		break;
	case 9:
		if (shown_menu () == NULL) {
			return FALSE;
		}
		report (TRUE, "a right-click on a selected item opens the menu");
		report (menu_on_press == classic, classic ? "on the press" : "on the release");
		want (items (1, 2, -1), "and keeps the selection");
		close_menu ();
		break;
	case 10:
		item_point (4, &x, &y);
		stamp += 1000;
		press (bin, 3, x, y, 0);
		send (bin, GDK_BUTTON_RELEASE, 3, x, y, GDK_BUTTON3_MASK);
		break;
	case 11:
		if (shown_menu () == NULL) {
			return FALSE;
		}
		want (items (4, -1), "a right-click on an item not selected selects it alone first");
		close_menu ();
		break;

	/* Keys. */
	case 12:
		click (2, 0);
		key (GDK_KEY_Down, 0);
		want (items (next_item (2, 0, 1), -1), "Down selects the next item down alone");
		break;
	case 13:
		key (GDK_KEY_Up, 0);
		want (items (2, -1), "Up selects the next item up alone");
		break;
	case 14:
		key (GDK_KEY_Right, 0);
		want (items (list_mode ? 2 : next_item (2, 1, 0), -1),
		      list_mode ? "Right on a file row changes nothing" : "Right selects the next item over alone");
		break;
	case 15:
		key (GDK_KEY_Left, 0);
		want (items (2, -1), list_mode ? "nor does Left" : "Left selects the next item back alone");
		break;
	case 16:
		key (GDK_KEY_End, 0);
		want (items (last, -1), "End selects the last item alone");
		break;
	case 17:
		key (GDK_KEY_Home, 0);
		want (items (0, -1), "Home selects the first item alone");
		break;
	case 18:
		click (2, 0);
		key (GDK_KEY_Down, GDK_SHIFT_MASK);
		key (GDK_KEY_Down, GDK_SHIFT_MASK);
		want (run_of (2, next_item (next_item (2, 0, 1), 0, 1), -1),
		      "Shift+Down twice selects the run from the clicked item");
		break;
	case 19:
		key (GDK_KEY_Up, GDK_SHIFT_MASK);
		want (run_of (2, next_item (2, 0, 1), -1), "Shift+Up takes the run back one");
		break;
	case 20:
		click (5, 0);
		key (GDK_KEY_Down, GDK_CONTROL_MASK);
		want (items (5, -1), "Ctrl+Down leaves the selection alone");
		break;
	case 21:
		key (GDK_KEY_space, GDK_CONTROL_MASK);
		want (items (5, next_item (5, 0, 1), -1), "Ctrl+Space then adds the item Ctrl+Down went to");
		break;
	case 22:
		key (GDK_KEY_space, GDK_CONTROL_MASK);
		want (items (5, -1), "and a second Ctrl+Space takes it out");
		break;
	case 23:
		click (1, 0);
		key (GDK_KEY_Down, GDK_SHIFT_MASK);
		key (GDK_KEY_Down, GDK_SHIFT_MASK);
		click (1, 0);
		want (items (1, -1), "a click on one item of a selection selects it alone");
		key (GDK_KEY_Down, 0);
		/* v1's list keeps its keyboard cursor on the row Shift+Down reached.
		   Nemo Anywhere counts from the selection. */
		want (items (list_mode && !anywhere ? 4 : next_item (1, 0, 1), -1),
		      list_mode && !anywhere ? "and Down then counts from where Shift+Down left the cursor" :
					       "and Down then counts from the clicked item");
		break;
	case 24:
		key (GDK_KEY_a, GDK_CONTROL_MASK);
		want (run_of (0, last, -1), "Ctrl+A selects everything");
		break;
	case 25:
		click (2, 0);
		key (GDK_KEY_I, GDK_CONTROL_MASK | GDK_SHIFT_MASK);
		want (run_of (0, last, 2), "Ctrl+Shift+I inverts the selection");
		break;
	case 26:
		key (GDK_KEY_Escape, 0);
		if (classic) {
			want (run_of (0, last, 2), "Escape leaves the selection alone");
		} else {
			want (g_strdup (""), "Escape clears the selection");
		}
		click (0, 0);
		key (GDK_KEY_End, GDK_SHIFT_MASK);
		want (run_of (0, last, -1), "Shift+End selects the run to the last item");
		key (GDK_KEY_Home, GDK_SHIFT_MASK);
		want (items (0, -1), "and Shift+Home takes it back to the first");
		click (3, 0);
		key (GDK_KEY_End, GDK_CONTROL_MASK);
		if (anywhere) {
			want (items (last, -1), "Ctrl+End selects the last item alone, as End does");
			key (GDK_KEY_space, GDK_CONTROL_MASK);
			want (g_strdup (""), "and Ctrl+Space then takes it out");
		} else {
			want (items (3, -1), "Ctrl+End leaves the selection alone");
			key (GDK_KEY_space, GDK_CONTROL_MASK);
			want (items (3, last, -1), "and Ctrl+Space then adds the last item");
		}
		click (1, 0);
		key (GDK_KEY_Down, GDK_CONTROL_MASK | GDK_SHIFT_MASK);
		/* Upstream's keyboard band flips what it crosses against what
		   was selected when the last mouse band began, item 1 here
		   (step 7). */
		if (list_mode && !anywhere) {
			want (items (1, -1), "Ctrl+Shift+Down leaves the list's selection alone");
		} else if (classic) {
			want (items (next_item (1, 0, 1), -1),
			      "Ctrl+Shift+Down flips the clicked item out and the next one down in");
		} else if (anywhere) {
			want (run_of (1, next_item (1, 0, 1), -1), "Ctrl+Shift+Down adds the run to the next item down");
		} else {
			want (items (1, next_item (1, 0, 1), -1), "Ctrl+Shift+Down adds the next item down");
		}
		click (0, 0);
		key (GDK_KEY_Page_Down, 0);
		/* Every row is in sight, and v1's icon views don't take the key. */
		if (list_mode) {
			want (items (last, -1), "Page Down selects the last row in sight alone");
		} else if (anywhere) {
			want (items (page_end (0, 1), -1), "Page Down selects the farthest item a page on alone");
		} else {
			want (items (0, -1), "Page Down leaves the selection alone");
		}
		click (3, 0);
		key (GDK_KEY_Home, GDK_CONTROL_MASK);
		if (anywhere) {
			want (items (0, -1), "Ctrl+Home selects the first item alone, as Home does");
			key (GDK_KEY_space, GDK_CONTROL_MASK);
			want (g_strdup (""), "and Ctrl+Space then takes it out");
		} else {
			want (items (3, -1), "Ctrl+Home leaves the selection alone");
			key (GDK_KEY_space, GDK_CONTROL_MASK);
			want (items (0, 3, -1), "and Ctrl+Space then adds the first item");
		}
		if (!list_mode) {
			click (2, 0);
			key (GDK_KEY_Right, GDK_CONTROL_MASK);
			want (items (2, -1), "Ctrl+Right leaves the selection alone");
			key (GDK_KEY_space, GDK_CONTROL_MASK);
			want (items (2, next_item (2, 1, 0), -1), "and Ctrl+Space then adds the next item over");
		}
		click (last, 0);
		key (GDK_KEY_Page_Up, 0);
		if (list_mode) {
			want (items (0, -1), "Page Up selects the first row in sight alone");
		} else if (anywhere) {
			want (items (page_end (last, -1), -1), "Page Up selects the farthest item a page back alone");
		} else {
			want (items (last, -1), "Page Up leaves the selection alone");
		}
		click (2, 0);
		key (GDK_KEY_Tab, 0);
		want (items (2, -1), "Tab leaves the selection alone");
		/* Upstream's Tab leaves the focus in the view, since its tab
		   notebook's Ctrl+Tab handler takes every focus move. Ours lets it
		   through in every model (#2026102016594200). */
		report (!gtk_widget_has_focus (pane), "and takes the keyboard focus out of the view");
		gtk_widget_grab_focus (pane);
		if (!list_mode) {
			click (2, 0);
			key (GDK_KEY_Right, GDK_SHIFT_MASK);
			want (run_of (2, next_item (2, 1, 0), -1), "Shift+Right selects the run to the next item over");
			key (GDK_KEY_Left, GDK_SHIFT_MASK);
			want (items (2, -1), "and Shift+Left takes it back");
		}
		if (anywhere && list_mode) {
			anywhere_list_keys ();
		} else if (anywhere && compact_mode) {
			anywhere_compact_keys ();
		} else if (anywhere) {
			anywhere_icon_keys ();
		}
		break;
	case 27:
		click (0, 0);
		for (i = 0; typed[i] != '\0'; i++) {
			key (gdk_unicode_to_keyval ((guint32) typed[i]), 0);
		}
		break;
	case 28:
		if (!is_selected (7) && waited < 20) {
			return FALSE;
		}
		want (items (7, -1), "typing a name selects the first item it matches alone");
		key (GDK_KEY_Escape, 0);
		break;

	/* Focus, then the folder tree. */
	case 29:
		if (tree == NULL) {
			report (FALSE, "the folder tree is showing");
			finish ();
			return TRUE;
		}
		gtk_widget_grab_focus (GTK_WIDGET (tree));
		click (1, 0);
		report (gtk_widget_has_focus (pane), "a click in the view takes the keyboard focus from the tree");
		want (items (1, -1), "and selects as usual");
		break;
	case 30:
		glib_handler = g_log_set_handler ("GLib", G_LOG_LEVEL_CRITICAL, count_glib_critical, NULL);
		note_title ();
		click_tree_row (tree, "0", 0);
		report (gtk_widget_has_focus (GTK_WIDGET (tree)), "a click in the tree takes the keyboard focus");
		want_tree (tree, "0", "and selects the row");
		break;
	case 31:
		if (!title_changed ()) {
			return FALSE;
		}
		report (TRUE, "and opens its folder");
		note_title ();
		key (GDK_KEY_Down, 0);
		want_tree (tree, "0:0", "Down in the tree selects the next row");
		break;
	case 32:
		if (!title_changed ()) {
			return FALSE;
		}
		report (TRUE, "and opens its folder");
		note_title ();
		key (GDK_KEY_Up, 0);
		want_tree (tree, "0", "Up in the tree selects the row above");
		break;
	case 33:
		if (!title_changed ()) {
			return FALSE;
		}
		report (TRUE, "and opens its folder");
		note_title ();
		key (GDK_KEY_Down, GDK_SHIFT_MASK);
		want_tree (tree, "0:0", "Shift+Down in the tree moves the one selected row");
		break;
	case 34:
		if (!title_changed ()) {
			return FALSE;
		}
		report (TRUE, "and opens its folder");
		click_tree_row (tree, "0:0", GDK_CONTROL_MASK);
		want_tree (tree, "", "Ctrl+click on the selected tree row unselects it");
		break;
	case 35:
		note_title ();
		click_tree_row (tree, "0", 0);
		break;
	case 36: {
		/* The tree has one column, so Left and Right have nowhere to go,
		   but Nemo Anywhere's close and open the row (#2026102018234900). */
		GtkTreePath *home = gtk_tree_path_new_from_string ("0");

		if (!title_changed ()) {
			gtk_tree_path_free (home);
			return FALSE;
		}
		key (GDK_KEY_Left, 0);
		want_tree (tree, "0", anywhere ? "Left in the tree keeps the row selected" :
			   "Left in the tree changes nothing");
		report (gtk_tree_view_row_expanded (tree, home) != anywhere,
			anywhere ? "and closes it" : "and leaves the row open");
		key (GDK_KEY_Right, 0);
		want_tree (tree, "0", anywhere ? "and Right opens it again" : "nor does Right");
		key (GDK_KEY_Escape, 0);
		want_tree (tree, "0", "nor Escape");
		report (gtk_tree_view_row_expanded (tree, home), "and the row is still open");
		gtk_tree_path_free (home);
		{
			GtkTreePath *row = gtk_tree_path_new_from_string ("0");
			GdkWindow *tree_bin = gtk_tree_view_get_bin_window (tree);
			GdkRectangle area;

			gtk_tree_view_get_cell_area (tree, row, gtk_tree_view_get_expander_column (tree), &area);
			gtk_tree_path_free (row);
			stamp += 1000;
			press (tree_bin, 3, area.x + 30, area.y + area.height / 2, 0);
			menu_on_press = shown_menu () != NULL;
			send (tree_bin, GDK_BUTTON_RELEASE, 3, area.x + 30, area.y + area.height / 2,
			      GDK_BUTTON3_MASK);
			report (shown_menu () != NULL, "a right-click in the tree opens its menu");
			report (menu_on_press == classic, classic ? "on the press" : "on the release");
			close_menu ();
			want_tree (tree, "0", "and leaves the row selected");
		}
		g_log_remove_handler ("GLib", glib_handler);
		/* Upstream's do, the timer bug Nemo Classic doesn't keep. */
		report (glib_criticals == 0, "the tree steps log no GLib criticals");
		break;
	}

	/* Opening, in the home folder: the config, data and cache folders the
	   test makes, the test folder, whatever else the box adds, then the
	   probe's own output file. A new folder can mean a new view. */
	case 37:
		pane = NULL;
		if (!find_pane () || item_named ("data") < 0 || item_named (folder_name) < 0) {
			return FALSE;
		}
		if (anywhere) {
			/* The tree still has the focus, on the viewed folder, so every
			   item takes the mark (#2026111013031729). */
			char *all = run_of (0, item_count () - 1, -1);
			char *marked = marks ();
			char *line = g_strdup_printf ("with the tree focused on the viewed folder, every item is marked "
						      "(marked %s, want %s)", marked, all);

			report (gtk_widget_has_focus (GTK_WIDGET (tree)) && strcmp (marked, all) == 0, line);
			g_free (line);
			g_free (marked);
			g_free (all);
		}
		note_title ();
		click (item_named ("data"), 0);
		if (anywhere) {
			want (items (item_named ("data"), -1), "a click in the view takes the marks back to its selection");
		}
		gtk_widget_grab_focus (pane);
		key (GDK_KEY_Return, 0);
		break;
	case 38:
		if (!title_changed ()) {
			return FALSE;
		}
		report (g_str_has_prefix (gtk_window_get_title (GTK_WINDOW (window)), "data"),
			"Enter opens the selected folder");
		note_title ();
		/* Nemo Anywhere's tree leaves nothing selected (#2026102018234901),
		   so there the way back is Up. */
		if (anywhere) {
			key (GDK_KEY_Up, GDK_MOD1_MASK);
		} else {
			click_tree_row (tree, "0", 0);
		}
		break;
	case 39:
		if (!title_changed ()) {
			return FALSE;
		}
		pane = NULL;
		if (!find_pane () || (home_item = item_named ("data")) < 0) {
			return FALSE;
		}
		if (!is_selected (home_item) && waited < 30) {
			return FALSE;
		}
		want (items (home_item, -1), "coming back selects the folder just left");
		tabs_before = tab_count ();
		item_point (home_item, &x, &y);
		stamp += 1000;
		press_release (bin, 2, x, y, 0);
		break;
	case 40:
		/* Nothing may come, so give it time to. */
		if (waited < 15) {
			return FALSE;
		}
		report (tab_count () == tabs_before + (list_mode ? 0 : 1),
			list_mode ? "a middle click on a folder opens no tab, that takes two" :
				    "a middle click on a folder opens it in a new tab");
		want (items (home_item, -1), "and leaves it selected");
		click (item_named ("config"), 0);
		tabs_before = tab_count ();
		key (GDK_KEY_Return, GDK_SHIFT_MASK);
		break;
	case 41:
		if (tab_count () != tabs_before + 1) {
			return FALSE;
		}
		report (TRUE, "Shift+Enter opens the selected folder in a new tab");
		click (0, 0);
		key (GDK_KEY_space, 0);
		want (items (0, -1), "Space leaves the selection alone");
		tabs_before = tab_count ();
		key (GDK_KEY_space, GDK_SHIFT_MASK);
		break;
	case 42:
		if (tab_count () != tabs_before + 1) {
			return FALSE;
		}
		report (TRUE, "Shift+Space opens it in a new tab too");
		report (g_str_has_prefix (gtk_window_get_title (GTK_WINDOW (window)), "Home"),
			"and new tabs open behind this one");
		note_title ();
		item_point (item_named (folder_name), &x, &y);
		stamp += 1000;
		press_release (bin, 1, x, y, 0);
		press_release (bin, 1, x, y, 0);
		break;
	case 43:
		if (!title_changed ()) {
			return FALSE;
		}
		report (g_str_has_prefix (gtk_window_get_title (GTK_WINDOW (window)), folder_name),
			"a double-click opens the folder");
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
	} else if (shown_menu () == NULL && grab_held ()) {
		return G_SOURCE_CONTINUE;
	} else if (!rules_tick ()) {
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
	const char *path = g_getenv ("NEMO_SEL_OUT");
	const char *view = g_getenv ("NEMO_SEL_VIEW");

	folder_name = g_getenv ("NEMO_SEL_FOLDER");
	if (path == NULL || view == NULL || folder_name == NULL) {
		return;
	}
	list_mode = g_strcmp0 (view, "list-view") == 0;
	compact_mode = g_strcmp0 (view, "compact-view") == 0;
	classic = g_strcmp0 (g_getenv ("NEMO_SEL_MODEL"), "nemo-classic") == 0;
	anywhere = g_strcmp0 (g_getenv ("NEMO_SEL_MODEL"), "nemo-anywhere") == 0;
	out = fopen (path, "w");
	if (out == NULL) {
		return;
	}
	stamp = (guint32) (g_get_monotonic_time () / 1000);
	g_timeout_add (100, tick, NULL);
}
