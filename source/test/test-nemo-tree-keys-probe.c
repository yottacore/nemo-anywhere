/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */

/* test-nemo-tree-keys-probe.c - presses Right and Left in the folder tree and
   changes folders there, from inside the program.

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

/* Preloaded into the real program by test-nemo-tree-keys. Once the window
 * shows $NEMO_TREE_KEYS_FOLDER in the list view, it selects everything there,
 * clicks the parent in the tree, and looks at the list's selection. Then it
 * goes back and up with Alt+Up, which still selects the folder just left. Last
 * it presses Right and Left on the folder's tree row, and a second Left to
 * reach its parent, then copies that row with Ctrl+C while the list has
 * everything selected. One "ok" or "FAIL" line per check goes to
 * $NEMO_TREE_KEYS_OUT, and "done" at the end. */

#include <stdio.h>
#include <string.h>

#include <gtk/gtk.h>
#include <gdk/gdkkeysyms.h>

static FILE       *out;
static const char *folder_name;
static GtkWidget  *window;
static GtkTreeView *tree;
static int         step;
static int         waited;
static guint32     stamp;
static char       *title_before;

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

/* The list view's rows, once the folder in the title has loaded. */
static GtkTreeView *
list (void)
{
	GtkWidget *view = find_widget (window, "NemoView");
	GtkWidget *child = view != NULL ? gtk_bin_get_child (GTK_BIN (view)) : NULL;

	return GTK_IS_TREE_VIEW (child) && gtk_widget_get_mapped (child) ? GTK_TREE_VIEW (child) : NULL;
}

static int
list_count (void)
{
	GtkTreeView *view = list ();
	GtkTreeModel *model = view != NULL ? gtk_tree_view_get_model (view) : NULL;

	return model != NULL ? gtk_tree_model_iter_n_children (model, NULL) : 0;
}

static int
list_selected (void)
{
	GtkTreeView *view = list ();

	return view != NULL ? gtk_tree_selection_count_selected_rows (gtk_tree_view_get_selection (view)) : -1;
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
	gtk_tree_view_get_cell_area (tree, path, gtk_tree_view_get_expander_column (tree), &area);
	gtk_tree_path_free (path);
	stamp += 1000;
	send (bin, GDK_BUTTON_PRESS, area.x + 30, area.y + area.height / 2, 0);
	send (bin, GDK_BUTTON_RELEASE, area.x + 30, area.y + area.height / 2, GDK_BUTTON1_MASK);
	return TRUE;
}

static void
note_title (void)
{
	g_free (title_before);
	title_before = g_strdup (gtk_window_get_title (GTK_WINDOW (window)));
}

/* The title can change more than once on the way, so it has to sit still. */
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

/* The folder just left is selected a moment after the list fills. */
static gboolean
home_settled (void)
{
	GtkTreeView *view = list ();
	GtkTreeModel *model = view != NULL ? gtk_tree_view_get_model (view) : NULL;

	return title_changed () && model != NULL && list_count () >= 4 && waited >= 15;
}

static gboolean
folder_row_selected (void)
{
	GtkTreeView *view = list ();
	GtkTreeModel *model = view != NULL ? gtk_tree_view_get_model (view) : NULL;
	GList *rows, *l;
	gboolean found = FALSE;

	if (model == NULL) {
		return FALSE;
	}
	rows = gtk_tree_selection_get_selected_rows (gtk_tree_view_get_selection (view), NULL);
	for (l = rows; l != NULL; l = l->next) {
		GtkTreeIter iter;
		GValue value = G_VALUE_INIT;

		if (gtk_tree_model_get_iter (model, &iter, l->data)) {
			/* The list model's own columns aren't ours to name; find
			   the display name among its strings. */
			int c;

			for (c = 0; c < gtk_tree_model_get_n_columns (model) && !found; c++) {
				if (gtk_tree_model_get_column_type (model, c) != G_TYPE_STRING) {
					continue;
				}
				gtk_tree_model_get_value (model, &iter, c, &value);
				found = G_VALUE_HOLDS_STRING (&value) &&
					g_strcmp0 (g_value_get_string (&value), folder_name) == 0;
				g_value_unset (&value);
			}
		}
	}
	g_list_free_full (rows, (GDestroyNotify) gtk_tree_path_free);
	return found && list_selected () == 1;
}

/* The folder's tree row, once its sub-folders have been found in the
   background. */
static GtkTreePath *
folder_row_ready (void)
{
	GtkTreePath *path = tree_row (folder_name);
	GtkTreeIter iter;

	if (!tree_selected_is (path) ||
	    !gtk_tree_model_get_iter (gtk_tree_view_get_model (tree), &iter, path) ||
	    !gtk_tree_model_iter_has_child (gtk_tree_view_get_model (tree), &iter)) {
		gtk_tree_path_free (path);
		return NULL;
	}
	return path;
}

static gboolean
steps (void)
{
	GtkTreePath *path, *parent;

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
		if (title == NULL || !g_str_has_prefix (title, folder_name) || list_count () < 3 ||
		    (tree = folder_tree ()) == NULL || waited < 10 || (path = folder_row_ready ()) == NULL) {
			return FALSE;
		}
		gtk_tree_selection_select_all (gtk_tree_view_get_selection (list ()));
		report (list_selected () == list_count (), "everything in the folder is selected");
		note_title ();
		gtk_tree_path_up (path);
		click_tree_path (path);
		break;
	}
	case 1:
		if (!home_settled ()) {
			return FALSE;
		}
		report (list_selected () == 0,
			"a click on its parent in the tree leaves nothing selected in the content view");
		note_title ();
		if (!click_tree_path (tree_row (folder_name))) {
			report (FALSE, "the folder is in the tree");
		}
		break;
	case 2:
		if (!title_changed () || list_count () < 3) {
			return FALSE;
		}
		gtk_widget_grab_focus (GTK_WIDGET (list ()));
		note_title ();
		key (GDK_KEY_Up, GDK_MOD1_MASK);
		break;
	case 3:
		if (!home_settled () || (!folder_row_selected () && waited < 30)) {
			return FALSE;
		}
		report (folder_row_selected (), "Alt+Up still selects the folder just left");
		note_title ();
		click_tree_path (tree_row (folder_name));
		break;
	case 4:
		if (!title_changed () || list_count () < 3 || waited < 10 ||
		    (path = folder_row_ready ()) == NULL) {
			return FALSE;
		}
		gtk_widget_grab_focus (GTK_WIDGET (tree));
		gtk_tree_view_collapse_row (tree, path);
		gtk_tree_view_set_cursor (tree, path, NULL, FALSE);
		key (GDK_KEY_Right, 0);
		report (gtk_tree_view_row_expanded (tree, path), "Right in the tree opens the folder's row");
		key (GDK_KEY_Left, 0);
		report (!gtk_tree_view_row_expanded (tree, path), "Left closes it");
		note_title ();
		key (GDK_KEY_Left, 0);
		parent = gtk_tree_path_copy (path);
		gtk_tree_path_up (parent);
		report (tree_selected_is (parent), "and Left on a closed row goes to its parent");
		gtk_tree_path_free (parent);
		gtk_tree_path_free (path);
		break;
	case 5:
		if (!home_settled ()) {
			return FALSE;
		}
		report (list_selected () == 0, "which leaves nothing selected in the content view either");
		gtk_tree_selection_select_all (gtk_tree_view_get_selection (list ()));
		gtk_widget_grab_focus (GTK_WIDGET (tree));
		break;
	case 6:
		/* The content view turns Copy on in an idle after its selection
		   changes, and only then could it take the key from the tree. */
		if (waited < 5) {
			return FALSE;
		}
		key (GDK_KEY_c, GDK_CONTROL_MASK);
		break;
	case 7: {
		gchar **uris = gtk_clipboard_wait_for_uris (gtk_clipboard_get (GDK_SELECTION_CLIPBOARD));
		GFile *home = g_file_new_for_path (g_get_home_dir ());
		GFile *copied = uris != NULL && g_strv_length (uris) == 1 ? g_file_new_for_uri (uris[0]) : NULL;

		report (copied != NULL && g_file_equal (copied, home),
			"Ctrl+C in the tree copies its folder, not the content view's selection");
		g_clear_object (&copied);
		g_object_unref (home);
		g_strfreev (uris);
		return TRUE;
	}
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
	if (step == 7) {
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
	const char *path = g_getenv ("NEMO_TREE_KEYS_OUT");

	folder_name = g_getenv ("NEMO_TREE_KEYS_FOLDER");
	if (path == NULL || folder_name == NULL) {
		return;
	}
	out = fopen (path, "w");
	if (out == NULL) {
		return;
	}
	g_timeout_add (100, tick, NULL);
}
