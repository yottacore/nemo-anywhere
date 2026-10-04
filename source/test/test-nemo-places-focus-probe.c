/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */

/* test-nemo-places-focus-probe.c - clicks in "Places" from inside the program.

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

/* Preloaded into the real program by test-nemo-places-focus. Once the window
 * shows its first folder it clicks the place $NEMO_FOCUS_PLACE names, twice,
 * renames it from its menu, and writes what it saw to $NEMO_FOCUS_OUT, one
 * "ok" or "FAIL" line per check and "done" at the end. The clicks go through
 * GTK's own event path, so the tree view's handlers run as for a mouse. */

#include <stdio.h>

#include <gtk/gtk.h>

static FILE       *out;
static const char *place_uri;
static const char *first_name;
static const char *second_name;
static GtkWidget  *window;
static GtkWidget  *places;
static GtkWidget  *places_pane;
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

static GtkWidget *
focus_widget (void)
{
	return gtk_window_get_focus (GTK_WINDOW (window));
}

static gboolean
focus_in_view (void)
{
	GtkWidget *widget;

	for (widget = focus_widget (); widget != NULL; widget = gtk_widget_get_parent (widget)) {
		if (is_a (widget, "NemoView")) {
			return TRUE;
		}
	}
	return FALSE;
}

static gboolean
focus_in_places (void)
{
	GtkWidget *focus = focus_widget ();

	return focus != NULL && (focus == places_pane || gtk_widget_is_ancestor (focus, places_pane));
}

/* The list view's rows, which is the view a new folder opens in. */
static GtkTreeSelection *
view_selection (void)
{
	GtkWidget *view = find_widget ("NemoView");
	GtkWidget *child = view != NULL ? gtk_bin_get_child (GTK_BIN (view)) : NULL;

	return GTK_IS_TREE_VIEW (child) ? gtk_tree_view_get_selection (GTK_TREE_VIEW (child)) : NULL;
}

static int
rows_in_view (void)
{
	GtkTreeSelection *selection = view_selection ();
	GtkTreeModel *model;

	if (selection == NULL) {
		return 0;
	}
	model = gtk_tree_view_get_model (gtk_tree_selection_get_tree_view (selection));
	return model != NULL ? gtk_tree_model_iter_n_children (model, NULL) : 0;
}

static int
selected_in_view (void)
{
	GtkTreeSelection *selection = view_selection ();

	return selection != NULL ? gtk_tree_selection_count_selected_rows (selection) : -1;
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
		event->button.state = button == 1 ? GDK_BUTTON1_MASK : GDK_BUTTON3_MASK;
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
	if (button == 1) {
		send_button (GDK_BUTTON_RELEASE, button, x, y);
	}
	return TRUE;
}

typedef struct {
	const char *label;
	GtkWidget  *found;
} ItemSearch;

static void
find_item_cb (GtkWidget *widget, gpointer data)
{
	ItemSearch *search = data;
	const char *label;

	if (search->found != NULL) {
		return;
	}
	if (GTK_IS_MENU_ITEM (widget) && gtk_widget_get_visible (widget)) {
		label = gtk_menu_item_get_label (GTK_MENU_ITEM (widget));
		if (g_strcmp0 (label, search->label) == 0) {
			search->found = widget;
			return;
		}
	}
	if (GTK_IS_CONTAINER (widget)) {
		gtk_container_forall (GTK_CONTAINER (widget), find_item_cb, data);
	}
}

static GtkWidget *
find_item (GtkWidget *menu, const char *label)
{
	ItemSearch search = { label, NULL };

	find_item_cb (menu, &search);
	return search.found;
}

/* The menu of "Places" is the one with a plain "Remove" on it. It is not asked
 * to be on screen: with other programs on the display the menu can lose the
 * race for the pointer and stay down, and its items work all the same. */
static GtkWidget *
places_menu_rename_item (void)
{
	GList *toplevels = gtk_window_list_toplevels ();
	GList *l;
	GtkWidget *found = NULL;

	for (l = toplevels; l != NULL && found == NULL; l = l->next) {
		GtkWidget *child = gtk_bin_get_child (GTK_BIN (l->data));

		if (GTK_IS_MENU (child) && find_item (child, "Remove") != NULL) {
			found = find_item (child, "_Rename...");
		}
	}
	g_list_free (toplevels);

	return found;
}

static void
finish (void)
{
	fprintf (out, "done\n");
	fflush (out);
}

/* Each step waits for what the one before set going. Up to ten seconds each. */
static gboolean
tick (gpointer data)
{
	GtkWidget *entry, *item;

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
		places = find_widget ("NemoPlacesTreeView");
		places_pane = find_widget ("NemoPlacesSidebar");
		if (places == NULL || places_pane == NULL) {
			return G_SOURCE_CONTINUE;
		}
		/* Bookmarks are listed a moment after the window shows. */
		if (!click_place (1)) {
			return G_SOURCE_CONTINUE;
		}
		break;
	}
	case 1:
		/* The driver puts three files in each folder. */
		if (!title_starts (second_name) || rows_in_view () < 3) {
			return G_SOURCE_CONTINUE;
		}
		report (focus_in_view (), "a click on a place puts the focus in the folder");
		report (!focus_in_places (), "a click on a place leaves none in Places");
		report (selected_in_view () == 0, "nothing is selected in the folder");

		/* Neither a grab nor Tab nor F6 can put it there. F6 walks the panes
		 * the way Tab walks one pane, so the one check covers both. */
		gtk_widget_grab_focus (places);
		gtk_widget_grab_focus (places_pane);
		report (!focus_in_places (), "Places refuses a grab");
		report (!gtk_widget_child_focus (places_pane, GTK_DIR_TAB_FORWARD) && !focus_in_places (),
			"Tab passes Places by");

		if (view_selection () != NULL) {
			gtk_tree_selection_select_all (view_selection ());
		}
		report (selected_in_view () > 0, "the folder has a selection to clear");
		click_place (1);
		break;
	case 2:
		report (focus_in_view (), "a click on the open folder's place keeps the focus in it");
		report (selected_in_view () == 0, "and clears its selection");
		click_place (3);
		break;
	case 3:
		item = places_menu_rename_item ();
		if (item == NULL) {
			return G_SOURCE_CONTINUE;
		}
		gtk_menu_shell_deactivate (GTK_MENU_SHELL (gtk_widget_get_parent (item)));
		gtk_menu_item_activate (GTK_MENU_ITEM (item));
		break;
	case 4:
		entry = focus_widget ();
		if (!GTK_IS_ENTRY (entry)) {
			return G_SOURCE_CONTINUE;
		}
		report (focus_in_places (), "a rename takes the focus into Places");
		gtk_widget_activate (entry);
		break;
	case 5:
		report (focus_in_view (), "the end of a rename gives the focus back to the folder");
		report (!focus_in_places (), "and Places keeps none");
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
	const char *path = g_getenv ("NEMO_FOCUS_OUT");

	place_uri = g_getenv ("NEMO_FOCUS_PLACE");
	first_name = g_getenv ("NEMO_FOCUS_FIRST");
	second_name = g_getenv ("NEMO_FOCUS_SECOND");
	if (path == NULL || place_uri == NULL || first_name == NULL || second_name == NULL) {
		return;
	}
	out = fopen (path, "w");
	if (out == NULL) {
		return;
	}
	g_timeout_add (100, tick, NULL);
}
