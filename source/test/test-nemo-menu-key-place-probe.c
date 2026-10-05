/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */

/* test-nemo-menu-key-place-probe.c - opens menus from inside the program and
   reports where they showed.

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

/* Preloaded into the real program by test-nemo-menu-key-place. Once the
 * window shows $NEMO_MENU_FOLDER it opens the folder view's menu with
 * Shift+F10 and nothing selected, then with an item far from the top left
 * selected, with Shift+F10 and the Menu key. $NEMO_MENU_VIEW says which view
 * the folder opened in. In the list view it also right-clicks a row and opens
 * the tree pane's menu from the keyboard. One "ok" or "FAIL" line per check
 * goes to $NEMO_MENU_OUT, and "done" at the end.
 *
 * The keys go through the widget's own key bindings, the ones a key press
 * runs, and the right click through GTK's event path. No symbol of the
 * program is used, so the item positions come from GTK and from the icon
 * view's accessible objects. */

#include <stdio.h>
#include <stdlib.h>

#include <gtk/gtk.h>
#include <eel/eel-canvas.h>

/* Menu placement is exact to the pixel; this only allows for a theme border. */
#define SLACK 6

static FILE       *out;
static const char *folder_name;
static gboolean    list_mode;
static GtkWidget  *window;
static GtkWidget  *pane;
static GtkWidget  *tree;
static GdkRectangle want;
static int         step;
static int         waited;

static void
report (gboolean passed, const char *what)
{
	fprintf (out, "%s %s\n", passed ? "ok" : "FAIL", what);
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
find_widget (const char *type_name)
{
	Find find = { type_name, NULL };

	find_cb (window, &find);
	return find.found;
}

/* A widget's place on screen. */
static void
screen_rect (GtkWidget *widget, GdkRectangle *rect)
{
	GtkWidget *toplevel = gtk_widget_get_toplevel (widget);
	int x = 0, y = 0;

	gdk_window_get_origin (gtk_widget_get_window (toplevel), &rect->x, &rect->y);
	gtk_widget_translate_coordinates (widget, toplevel, 0, 0, &x, &y);
	rect->x += x;
	rect->y += y;
	rect->width = gtk_widget_get_allocated_width (widget);
	rect->height = gtk_widget_get_allocated_height (widget);
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
menu_rect (GtkWidget *menu, GdkRectangle *rect)
{
	GdkWindow *gdk_window = gtk_widget_get_window (gtk_widget_get_toplevel (menu));

	gdk_window_get_origin (gdk_window, &rect->x, &rect->y);
	rect->width = gdk_window_get_width (gdk_window);
	rect->height = gdk_window_get_height (gdk_window);
}

static gboolean
near (int got, int expected)
{
	return ABS (got - expected) <= SLACK;
}

/* The menu's top left on the item's bottom left, or its bottom left on the
   item's top left when there was no room below. */
static void
check_beside_item (GtkWidget *menu, const char *what)
{
	GdkRectangle got;
	char *line;
	gboolean below, above;

	menu_rect (menu, &got);
	below = near (got.x, want.x) && near (got.y, want.y + want.height);
	above = near (got.x, want.x) && near (got.y + got.height, want.y);
	line = g_strdup_printf ("%s (item %d,%d %dx%d, menu %d,%d %dx%d)", what,
				want.x, want.y, want.width, want.height,
				got.x, got.y, got.width, got.height);
	report (below || above, line);
	g_free (line);
}

static void
check_at (GtkWidget *menu, int x, int y, const char *what)
{
	GdkRectangle got;
	char *line;

	menu_rect (menu, &got);
	line = g_strdup_printf ("%s (want %d,%d, menu %d,%d)", what, x, y, got.x, got.y);
	report (near (got.x, x) && near (got.y, y), line);
	g_free (line);
}

static void
close_menu (GtkWidget *menu)
{
	gtk_menu_shell_deactivate (GTK_MENU_SHELL (menu));
}

static void
press_menu_key (GtkWidget *widget, guint key, GdkModifierType modifiers)
{
	if (!gtk_bindings_activate (G_OBJECT (widget), key, modifiers)) {
		report (FALSE, "the key has a binding");
	}
}

/* The folder's own widget: the list's tree view or the icon container. */
static gboolean
find_pane (void)
{
	GtkWidget *view = find_widget ("NemoView");
	GtkWidget *child = view != NULL ? gtk_bin_get_child (GTK_BIN (view)) : NULL;

	if (child == NULL || !gtk_widget_get_mapped (child)) {
		return FALSE;
	}
	if (list_mode ? !GTK_IS_TREE_VIEW (child) : !is_a (child, "NemoIconContainer")) {
		return FALSE;
	}
	pane = child;
	return TRUE;
}

static int
items_in_pane (void)
{
	if (GTK_IS_TREE_VIEW (pane)) {
		GtkTreeModel *model = gtk_tree_view_get_model (GTK_TREE_VIEW (pane));

		return model != NULL ? gtk_tree_model_iter_n_children (model, NULL) : 0;
	}
	return atk_object_get_n_accessible_children (gtk_widget_get_accessible (pane));
}

static void
clear_selection (void)
{
	if (GTK_IS_TREE_VIEW (pane)) {
		gtk_tree_selection_unselect_all (gtk_tree_view_get_selection (GTK_TREE_VIEW (pane)));
	} else {
		atk_selection_clear_selection (ATK_SELECTION (gtk_widget_get_accessible (pane)));
	}
}

/* A tree row's first cell on screen, the full row high. */
static void
row_screen_rect (GtkTreeView *tree_view, GtkTreePath *path, GdkRectangle *rect)
{
	GdkRectangle cell, row, widget;

	gtk_tree_view_get_cell_area (tree_view, path, gtk_tree_view_get_column (tree_view, 0), &cell);
	gtk_tree_view_get_background_area (tree_view, path, NULL, &row);
	gtk_tree_view_convert_bin_window_to_widget_coords (tree_view, cell.x, row.y, &rect->x, &rect->y);
	screen_rect (GTK_WIDGET (tree_view), &widget);
	rect->x += widget.x;
	rect->y += widget.y;
	rect->width = cell.width;
	rect->height = row.height;
}

/* Selects an item well away from the pane's top left and notes where it is.
   An icon's place is read from the canvas item behind its accessible object. */
static gboolean
select_far_item (void)
{
	if (GTK_IS_TREE_VIEW (pane)) {
		GtkTreePath *path = gtk_tree_path_new_from_indices (7, -1);

		gtk_tree_view_set_cursor (GTK_TREE_VIEW (pane), path, NULL, FALSE);
		row_screen_rect (GTK_TREE_VIEW (pane), path, &want);
		gtk_tree_path_free (path);
	} else {
		AtkObject *accessible = gtk_widget_get_accessible (pane);
		GdkRectangle origin;
		int i, best = -1, best_distance = -1;
		int scroll_x = (int) gtk_adjustment_get_value
			(gtk_scrollable_get_hadjustment (GTK_SCROLLABLE (pane)));
		int scroll_y = (int) gtk_adjustment_get_value
			(gtk_scrollable_get_vadjustment (GTK_SCROLLABLE (pane)));

		screen_rect (pane, &origin);
		for (i = 0; i < atk_object_get_n_accessible_children (accessible); i++) {
			AtkObject *child = atk_object_ref_accessible_child (accessible, i);
			GObject *object = ATK_IS_GOBJECT_ACCESSIBLE (child) ?
				atk_gobject_accessible_get_object (ATK_GOBJECT_ACCESSIBLE (child)) : NULL;
			/* By type name, since the program exports none of its own symbols. */
			EelCanvasItem *item = object != NULL &&
				g_type_is_a (G_OBJECT_TYPE (object), g_type_from_name ("EelCanvasItem")) ?
				(EelCanvasItem *) object : NULL;

			/* Only an icon wholly in sight; one scrolled away gets the top left. */
			if (item != NULL && (int) (item->x1 + item->y1) > best_distance &&
			    item->x1 - scroll_x >= 0 && item->x2 - scroll_x <= origin.width &&
			    item->y1 - scroll_y >= 0 && item->y2 - scroll_y <= origin.height) {
				best = i;
				best_distance = (int) (item->x1 + item->y1);
				want.x = origin.x + (int) item->x1 - scroll_x;
				want.y = origin.y + (int) item->y1 - scroll_y;
				want.width = (int) (item->x2 - item->x1);
				want.height = (int) (item->y2 - item->y1);
			}
			g_object_unref (child);
		}
		if (best < 0 || !atk_selection_add_selection (ATK_SELECTION (accessible), best)) {
			return FALSE;
		}
	}

	return TRUE;
}

static void
finish (void)
{
	fprintf (out, "done\n");
	fflush (out);
}

/* Each step waits for what the one before set going. Up to ten seconds each. */
static gboolean
tick (G_GNUC_UNUSED gpointer data)
{
	GtkWidget *menu;
	GdkRectangle rect;

	if (++waited > 100) {
		char *line = g_strdup_printf ("timed out at step %d", step);

		report (FALSE, line);
		g_free (line);
		finish ();
		return G_SOURCE_REMOVE;
	}

	menu = shown_menu ();
	/* A key goes in only once the menu before is down. */
	if (step > 0 && step % 2 == 0 && menu != NULL) {
		return G_SOURCE_CONTINUE;
	}

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
		if (title == NULL || !g_str_has_prefix (title, folder_name)) {
			return G_SOURCE_CONTINUE;
		}
		/* Icons are placed a moment after they are listed, so give it a second. */
		if (!find_pane () || items_in_pane () < 12 || waited < 10) {
			return G_SOURCE_CONTINUE;
		}
		gtk_widget_grab_focus (pane);
		clear_selection ();
		press_menu_key (pane, GDK_KEY_F10, GDK_SHIFT_MASK);
		break;
	}
	case 1:
		if (menu == NULL) {
			return G_SOURCE_CONTINUE;
		}
		screen_rect (pane, &rect);
		check_at (menu, rect.x + 16, rect.y + 16,
			  "with nothing selected, the menu opens at the pane's top left");
		close_menu (menu);
		break;
	case 2:
		if (!select_far_item ()) {
			report (FALSE, "an item could be selected");
			finish ();
			return G_SOURCE_REMOVE;
		}
		press_menu_key (pane, GDK_KEY_F10, GDK_SHIFT_MASK);
		break;
	case 3:
		if (menu == NULL) {
			return G_SOURCE_CONTINUE;
		}
		check_beside_item (menu, "Shift+F10 opens the menu below the selected item");
		close_menu (menu);
		break;
	case 4:
		press_menu_key (pane, GDK_KEY_Menu, 0);
		break;
	case 5:
		if (menu == NULL) {
			return G_SOURCE_CONTINUE;
		}
		check_beside_item (menu, "the Menu key opens the menu below the selected item");
		close_menu (menu);
		if (!list_mode) {
			finish ();
			return G_SOURCE_REMOVE;
		}
		break;
	case 6: {
		/* A right click on a row, well clear of where the keyboard menu went. */
		GdkWindow *bin = gtk_tree_view_get_bin_window (GTK_TREE_VIEW (pane));
		GdkSeat *seat = gdk_display_get_default_seat (gtk_widget_get_display (pane));
		GdkEvent *event = gdk_event_new (GDK_BUTTON_PRESS);
		GtkTreePath *path = gtk_tree_path_new_from_indices (3, -1);
		int origin_x, origin_y;

		gtk_tree_view_get_background_area (GTK_TREE_VIEW (pane), path, NULL, &rect);
		gtk_tree_path_free (path);
		gdk_window_get_origin (bin, &origin_x, &origin_y);
		event->button.window = g_object_ref (bin);
		event->button.time = (guint32) (g_get_monotonic_time () / 1000);
		/* Near the left, so the menu has room to the right and is not flipped. */
		event->button.x = 60;
		event->button.y = rect.y + rect.height / 2;
		event->button.x_root = origin_x + event->button.x;
		event->button.y_root = origin_y + event->button.y;
		event->button.button = 3;
		gdk_event_set_device (event, gdk_seat_get_pointer (seat));
		want.x = (int) event->button.x_root;
		want.y = (int) event->button.y_root;
		/* GTK places it where the pointer really is, not where the event says. */
		gdk_device_warp (gdk_seat_get_pointer (seat), gtk_widget_get_screen (pane),
				 want.x, want.y);
		gtk_main_do_event (event);
		gdk_event_free (event);
		break;
	}
	case 7:
		if (menu == NULL) {
			return G_SOURCE_CONTINUE;
		}
		check_at (menu, want.x, want.y, "a right click opens the menu at the pointer");
		close_menu (menu);
		break;
	case 8: {
		GtkWidget *tree_pane = find_widget ("FMTreeView");
		GtkTreeSelection *selection;
		GtkTreeModel *model;
		GtkTreeIter iter;
		GtkTreePath *path;

		tree = tree_pane != NULL ? gtk_bin_get_child (GTK_BIN (tree_pane)) : NULL;
		if (!GTK_IS_TREE_VIEW (tree)) {
			report (FALSE, "the tree pane is showing");
			finish ();
			return G_SOURCE_REMOVE;
		}
		/* The open folder's row, a few levels down the tree. */
		selection = gtk_tree_view_get_selection (GTK_TREE_VIEW (tree));
		if (!gtk_tree_selection_get_selected (selection, &model, &iter)) {
			report (FALSE, "the tree shows the open folder");
			finish ();
			return G_SOURCE_REMOVE;
		}
		path = gtk_tree_model_get_path (model, &iter);
		gtk_widget_grab_focus (tree);
		gtk_tree_view_set_cursor (GTK_TREE_VIEW (tree), path, NULL, FALSE);
		row_screen_rect (GTK_TREE_VIEW (tree), path, &want);
		gtk_tree_path_free (path);
		press_menu_key (tree, GDK_KEY_F10, GDK_SHIFT_MASK);
		break;
	}
	case 9:
		if (menu == NULL) {
			return G_SOURCE_CONTINUE;
		}
		check_beside_item (menu, "Shift+F10 in the tree opens the menu below its row");
		close_menu (menu);
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
	const char *path = g_getenv ("NEMO_MENU_OUT");
	const char *view = g_getenv ("NEMO_MENU_VIEW");

	folder_name = g_getenv ("NEMO_MENU_FOLDER");
	if (path == NULL || view == NULL || folder_name == NULL) {
		return;
	}
	list_mode = g_strcmp0 (view, "list-view") == 0;
	out = fopen (path, "w");
	if (out == NULL) {
		return;
	}
	g_timeout_add (100, tick, NULL);
}
