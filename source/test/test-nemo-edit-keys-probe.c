/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */

/* test-nemo-edit-keys-probe.c - what a delete leaves selected, and where
   Ctrl+Shift+V pastes, from inside the program.

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

/* Preloaded into the program, which opens a folder containing sub/, a.txt,
   b.txt, and c.txt. sub/ has a file in it, so it stays expanded. Deleting b.txt for good must leave a.txt, the one before it,
   selected, in the list view and in the icon view (NEMO_EDITKEYS_VIEW). In
   the list view it then copies c.txt, selects sub and expands it, and checks
   that Ctrl+V goes into sub while Ctrl+Shift+V goes into the viewed folder.
   Writes "ok"/"FAIL" lines and "done" to NEMO_EDITKEYS_OUT. */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <gtk/gtk.h>

static FILE       *out;
static const char *folder_path;
static gboolean    list_mode;
static GtkWidget  *window;
static GtkWidget  *pane;
static int         waited;
static int         step;
static int         step_at;

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
is_a (gpointer object, const char *type_name)
{
	GType type = g_type_from_name (type_name);

	return type != 0 && object != NULL && g_type_is_a (G_OBJECT_TYPE (object), type);
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
find_toplevel (const char *type_name)
{
	GList *toplevels, *l;
	GtkWidget *found = NULL;

	toplevels = gtk_window_list_toplevels ();
	for (l = toplevels; l != NULL; l = l->next) {
		if (l->data != window && is_a (l->data, type_name) && gtk_widget_get_mapped (l->data)) {
			found = l->data;
		}
	}
	g_list_free (toplevels);

	return found;
}

static void
send_key_event (GdkEventType type, guint keyval, GdkModifierType state)
{
	GdkDisplay *display = gtk_widget_get_display (window);
	GdkKeymapKey *keys = NULL;
	gint n_keys = 0;
	GdkEvent *event;

	event = gdk_event_new (type);
	event->key.window = g_object_ref (gtk_widget_get_window (window));
	event->key.send_event = TRUE;
	event->key.time = GDK_CURRENT_TIME;
	event->key.keyval = keyval;
	event->key.state = state;
	if (gdk_keymap_get_entries_for_keyval (gdk_keymap_get_for_display (display), keyval,
					       &keys, &n_keys) && n_keys > 0) {
		event->key.hardware_keycode = (guint16) keys[0].keycode;
		event->key.group = (guint8) keys[0].group;
	}
	g_free (keys);
	gdk_event_set_device (event, gdk_seat_get_keyboard (gdk_display_get_default_seat (display)));
	gtk_main_do_event (event);
	gdk_event_free (event);
}

static void
key (guint keyval, GdkModifierType state)
{
	send_key_event (GDK_KEY_PRESS, keyval, state);
	send_key_event (GDK_KEY_RELEASE, keyval, state);
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
	return TRUE;
}

/* Top-level items only, in view order. */
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

static int
selected_count (void)
{
	int i, n = 0;

	for (i = 0; i < item_count (); i++) {
		n += is_selected (i) ? 1 : 0;
	}
	return n;
}

/* The list view's cursor goes where it's put; the icon view takes keys. */
static void
put_cursor (int item)
{
	if (list_mode) {
		GtkTreePath *path = gtk_tree_path_new_from_indices (item, -1);

		gtk_tree_view_set_cursor (GTK_TREE_VIEW (pane), path, NULL, FALSE);
		gtk_tree_path_free (path);
	} else {
		int i;

		key (GDK_KEY_Home, 0);
		for (i = 0; i < item; i++) {
			key (GDK_KEY_Right, 0);
		}
	}
}

static gboolean
row_expanded (int item)
{
	GtkTreePath *path = gtk_tree_path_new_from_indices (item, -1);
	gboolean expanded = gtk_tree_view_row_expanded (GTK_TREE_VIEW (pane), path);

	gtk_tree_path_free (path);
	return expanded;
}

static int
entries_in (const char *dir)
{
	GDir *d = g_dir_open (dir, 0, NULL);
	int n = 0;

	if (d == NULL) {
		return -1;
	}
	while (g_dir_read_name (d) != NULL) {
		n++;
	}
	g_dir_close (d);
	return n;
}

static gboolean
in_folder (const char *name)
{
	char *path = g_build_filename (folder_path, name, NULL);
	gboolean there = g_file_test (path, G_FILE_TEST_EXISTS);

	g_free (path);
	return there;
}

/* The delete question, once asked, answered with its last button. */
static gboolean
answer_delete_question (void)
{
	GtkWidget *dialog = find_toplevel ("GtkMessageDialog");
	GtkWidget *area;
	GList *buttons;
	char *text = NULL;
	gboolean asked;

	if (dialog == NULL) {
		return FALSE;
	}
	g_object_get (dialog, "text", &text, NULL);
	asked = text != NULL && g_str_has_prefix (text, "Are you sure you want to permanently delete");
	g_free (text);
	if (!asked) {
		return FALSE;
	}
	area = gtk_dialog_get_action_area (GTK_DIALOG (dialog));
	buttons = gtk_container_get_children (GTK_CONTAINER (area));
	if (buttons != NULL) {
		gtk_button_clicked (GTK_BUTTON (g_list_last (buttons)->data));
	}
	g_list_free (buttons);
	return TRUE;
}

/* Up to ninety seconds in all. */
static gboolean
tick (G_GNUC_UNUSED gpointer data)
{
	const char *title;
	char *base, *sub;
	gboolean found;

	if (++waited > 900) {
		report (FALSE, "the steps finished in time");
		finish ();
		return G_SOURCE_REMOVE;
	}

	switch (step) {
	case 0:
		window = find_toplevel ("NemoWindow");
		title = window != NULL ? gtk_window_get_title (GTK_WINDOW (window)) : NULL;
		base = g_path_get_basename (folder_path);
		found = title != NULL && g_str_has_prefix (title, base);
		g_free (base);
		if (!found) {
			return G_SOURCE_CONTINUE;
		}
		break;
	case 1:
		/* The window's own first second, and the 4 items listed. */
		if (waited - step_at < 20 || !find_pane () || item_count () < 4) {
			if (waited - step_at > 200) {
				report (FALSE, "the folder listed its 4 items");
				finish ();
				return G_SOURCE_REMOVE;
			}
			return G_SOURCE_CONTINUE;
		}
		report (TRUE, "the folder listed its 4 items");
		gtk_widget_grab_focus (pane);
		break;
	case 2:
		/* sub, a.txt, b.txt, c.txt: folders first, so b.txt is third. */
		put_cursor (2);
		break;
	case 3:
		if (waited - step_at < 5) {
			return G_SOURCE_CONTINUE;
		}
		report (is_selected (2) && selected_count () == 1, "b.txt is selected");
		key (GDK_KEY_Delete, GDK_SHIFT_MASK);
		break;
	case 4:
		if (!answer_delete_question ()) {
			if (waited - step_at > 50) {
				report (FALSE, "Shift+Delete asks the delete question");
				finish ();
				return G_SOURCE_REMOVE;
			}
			return G_SOURCE_CONTINUE;
		}
		report (TRUE, "Shift+Delete asks the delete question");
		break;
	case 5:
		if (item_count () != 3) {
			if (waited - step_at > 100) {
				report (FALSE, "b.txt left the view");
				finish ();
				return G_SOURCE_REMOVE;
			}
			return G_SOURCE_CONTINUE;
		}
		report (TRUE, "b.txt left the view");
		break;
	case 6:
		/* A moment for the view to finish the change. */
		if (waited - step_at < 5) {
			return G_SOURCE_CONTINUE;
		}
		report (is_selected (1) && selected_count () == 1,
			"the delete left a.txt, the item before it, selected");
		if (!list_mode) {
			finish ();
			return G_SOURCE_REMOVE;
		}
		/* Copy c.txt, then select sub and expand it, so a plain paste
		   has somewhere other than the viewed folder to go. */
		key (GDK_KEY_End, 0);
		break;
	case 7:
		if (waited - step_at < 5) {
			return G_SOURCE_CONTINUE;
		}
		report (is_selected (2), "End selects c.txt");
		report (gtk_accel_groups_activate (G_OBJECT (window), GDK_KEY_c, GDK_CONTROL_MASK),
			"Ctrl+C copies it");
		put_cursor (0);
		key (GDK_KEY_Right, 0);
		break;
	case 8:
		if (waited - step_at < 10) {
			return G_SOURCE_CONTINUE;
		}
		report (is_selected (0) && row_expanded (0), "sub is selected and Right expands it");
		report (gtk_accel_groups_activate (G_OBJECT (window), GDK_KEY_v, GDK_CONTROL_MASK),
			"Ctrl+V is bound");
		break;
	case 9:
		if (!in_folder ("sub/c.txt")) {
			if (waited - step_at > 100) {
				report (FALSE, "Ctrl+V pasted into the selected expanded folder");
				finish ();
				return G_SOURCE_REMOVE;
			}
			return G_SOURCE_CONTINUE;
		}
		report (TRUE, "Ctrl+V pasted into the selected expanded folder");
		break;
	case 10:
		/* The paste left its copy selected, inside sub, so the plain
		   target is still sub. */
		if (waited - step_at < 10) {
			return G_SOURCE_CONTINUE;
		}
		report (gtk_accel_groups_activate (G_OBJECT (window), GDK_KEY_v,
						   GDK_CONTROL_MASK | GDK_SHIFT_MASK),
			"Ctrl+Shift+V is bound");
		break;
	case 11:
		if (entries_in (folder_path) < 4) {
			if (waited - step_at > 100) {
				report (FALSE, "Ctrl+Shift+V pasted into the viewed folder");
				finish ();
				return G_SOURCE_REMOVE;
			}
			return G_SOURCE_CONTINUE;
		}
		report (TRUE, "Ctrl+Shift+V pasted into the viewed folder");
		sub = g_build_filename (folder_path, "sub", NULL);
		report (entries_in (sub) == 2, "Ctrl+Shift+V left sub alone");
		g_free (sub);
		finish ();
		return G_SOURCE_REMOVE;
	}

	step++;
	step_at = waited;
	return G_SOURCE_CONTINUE;
}

__attribute__((constructor)) static void
start (void)
{
	const char *path = g_getenv ("NEMO_EDITKEYS_OUT");
	const char *view = g_getenv ("NEMO_EDITKEYS_VIEW");

	folder_path = g_getenv ("NEMO_EDITKEYS_FOLDER");
	if (path == NULL || folder_path == NULL) {
		return;
	}
	out = fopen (path, "w");
	if (out == NULL) {
		return;
	}
	list_mode = g_strcmp0 (view, "icon-view") != 0;
	g_timeout_add (100, tick, NULL);
}
