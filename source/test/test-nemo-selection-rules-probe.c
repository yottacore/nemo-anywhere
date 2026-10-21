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
 * and checks each rule of the "Nemo Anywhere v1" selection model. Where the
 * next item over depends on the layout, the icon views work it out from where
 * the icons are. One "ok" or "FAIL" line per check goes to $NEMO_SEL_OUT, and
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
static GtkWidget *window;
static GtkWidget *pane;
static GdkWindow *bin;
static int        step;
static int        waited;
static guint32    stamp;
static gboolean   finished;
static char      *title_before;
static int        last_clicked = -1;
static gint64     last_click_time;

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

static void
want (char *wanted, const char *what)
{
	char *got = selected_list ();
	char *line = g_strdup_printf ("%s (selected %s, want %s)", what, got, wanted);

	report (strcmp (got, wanted) == 0, line);
	g_free (line);
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
click_blank (void)
{
	double x, y;

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
	press_release (bin, 1, x, y, 0);
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

static void
want_tree (GtkTreeView *tree, const char *wanted, const char *what)
{
	char *got = tree_selected (tree);
	char *line = g_strdup_printf ("%s (tree row %s, want %s)", what, got, wanted);

	report (strcmp (got, wanted) == 0, line);
	g_free (line);
	g_free (got);
}

static void
click_tree_row (GtkTreeView *tree, const char *row, guint modifiers)
{
	GtkTreePath *path = gtk_tree_path_new_from_string (row);
	GdkRectangle area;

	gtk_tree_view_get_cell_area (tree, path, gtk_tree_view_get_column (tree, 0), &area);
	gtk_tree_path_free (path);
	stamp += 1000;
	press_release (gtk_tree_view_get_bin_window (tree), 1, area.x + 30, area.y + area.height / 2,
		       modifiers);
}

/* The title can change more than once on the way to a folder, and a key in
   the tree opens its row a moment later, so it has to sit still a while. */
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

/* Each step waits on what the one before set going, up to ten seconds. */
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
		break;
	case 7:
		if (!click_blank ()) {
			report (FALSE, "there is a spot clear of the items");
			break;
		}
		want (g_strdup (""), "a click on empty space clears the selection");
		break;
	case 8:
		close_menu ();
		click (1, 0);
		click (2, GDK_CONTROL_MASK);
		item_point (2, &x, &y);
		stamp += 1000;
		press (bin, 3, x, y, 0);
		send (bin, GDK_BUTTON_RELEASE, 3, x, y, GDK_BUTTON3_MASK);
		break;
	case 9:
		if (shown_menu () == NULL) {
			return FALSE;
		}
		report (TRUE, "a right-click on a selected item opens the menu");
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
		/* The list's keyboard cursor stays on the row Shift+Down reached. */
		want (items (list_mode ? 4 : next_item (1, 0, 1), -1),
		      list_mode ? "and Down then counts from where Shift+Down left the cursor" :
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
		want (g_strdup (""), "Escape clears the selection");
		click (0, 0);
		key (GDK_KEY_End, GDK_SHIFT_MASK);
		want (run_of (0, last, -1), "Shift+End selects the run to the last item");
		key (GDK_KEY_Home, GDK_SHIFT_MASK);
		want (items (0, -1), "and Shift+Home takes it back to the first");
		click (3, 0);
		key (GDK_KEY_End, GDK_CONTROL_MASK);
		want (items (3, -1), "Ctrl+End leaves the selection alone");
		key (GDK_KEY_space, GDK_CONTROL_MASK);
		want (items (3, last, -1), "and Ctrl+Space then adds the last item");
		click (1, 0);
		key (GDK_KEY_Down, GDK_CONTROL_MASK | GDK_SHIFT_MASK);
		want (items (1, list_mode ? -1 : next_item (1, 0, 1), -1),
		      list_mode ? "Ctrl+Shift+Down leaves the list's selection alone" :
				  "Ctrl+Shift+Down adds the next item down");
		click (0, 0);
		key (GDK_KEY_Page_Down, 0);
		/* Every row is in sight, and the icon views don't take the key. */
		want (items (list_mode ? last : 0, -1),
		      list_mode ? "Page Down selects the last row in sight alone" :
				  "Page Down leaves the selection alone");
		click (3, 0);
		key (GDK_KEY_Home, GDK_CONTROL_MASK);
		want (items (3, -1), "Ctrl+Home leaves the selection alone");
		key (GDK_KEY_space, GDK_CONTROL_MASK);
		want (items (0, 3, -1), "and Ctrl+Space then adds the first item");
		if (!list_mode) {
			click (2, 0);
			key (GDK_KEY_Right, GDK_CONTROL_MASK);
			want (items (2, -1), "Ctrl+Right leaves the selection alone");
			key (GDK_KEY_space, GDK_CONTROL_MASK);
			want (items (2, next_item (2, 1, 0), -1), "and Ctrl+Space then adds the next item over");
		}
		click (last, 0);
		key (GDK_KEY_Page_Up, 0);
		want (items (list_mode ? 0 : last, -1),
		      list_mode ? "Page Up selects the first row in sight alone" :
				  "Page Up leaves the selection alone");
		click (2, 0);
		key (GDK_KEY_Tab, 0);
		want (items (2, -1), "Tab leaves the selection alone");
		report (!gtk_widget_has_focus (pane), "and takes the keyboard focus out of the view");
		gtk_widget_grab_focus (pane);
		if (!list_mode) {
			click (2, 0);
			key (GDK_KEY_Right, GDK_SHIFT_MASK);
			want (run_of (2, next_item (2, 1, 0), -1), "Shift+Right selects the run to the next item over");
			key (GDK_KEY_Left, GDK_SHIFT_MASK);
			want (items (2, -1), "and Shift+Left takes it back");
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
		/* The tree has one column, so Left and Right have nowhere to go. */
		GtkTreePath *home = gtk_tree_path_new_from_string ("0");

		if (!title_changed ()) {
			gtk_tree_path_free (home);
			return FALSE;
		}
		key (GDK_KEY_Left, 0);
		want_tree (tree, "0", "Left in the tree changes nothing");
		report (gtk_tree_view_row_expanded (tree, home), "and leaves the row open");
		key (GDK_KEY_Right, 0);
		want_tree (tree, "0", "nor does Right");
		key (GDK_KEY_Escape, 0);
		want_tree (tree, "0", "nor Escape");
		report (gtk_tree_view_row_expanded (tree, home), "and the row is still open");
		gtk_tree_path_free (home);
		finish ();
		return TRUE;
	}
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
	} else if (shown_menu () == NULL && gtk_grab_get_current () != NULL) {
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
	out = fopen (path, "w");
	if (out == NULL) {
		return;
	}
	stamp = (guint32) (g_get_monotonic_time () / 1000);
	g_timeout_add (100, tick, NULL);
}
