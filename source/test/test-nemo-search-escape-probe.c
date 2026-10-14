/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */

/* test-nemo-search-escape-probe.c - presses Escape in find mode, from inside
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

/* Preloaded into the real program by test-nemo-search-escape. Once the
 * window shows $NEMO_ESC_FOLDER it searches for "note" 3 times. The first
 * time Escape goes to the search box. The second time it goes to the
 * results with one selected: the first Escape clears the selection, the
 * second goes back to the folder. The third time F2 renames a result, and
 * Escape ends the rename and stays in the results. One "ok" or "FAIL" line
 * per check goes to $NEMO_ESC_OUT, and "done" at the end.
 *
 * No symbol of the program is used. Keys go to the window the way a key
 * press would, or through its shortcuts. */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <gtk/gtk.h>

#define FOLDER_ITEMS 4
#define FOUND        2

static FILE       *out;
static const char *folder_name;
static GtkWidget  *window;
static int         waited;
static int         step;
static int         step_at;

static int item_count (void);
static gboolean find_bar_shows (void);
static int selected_count (void);

/* A failure says where the window was. */
static void
report (gboolean passed, const char *what)
{
	if (passed) {
		fprintf (out, "ok %s\n", what);
	} else {
		const char *title = window != NULL ? gtk_window_get_title (GTK_WINDOW (window)) : NULL;

		fprintf (out, "FAIL %s (title \"%s\", %d items, %d selected)\n", what,
			 title != NULL ? title : "", item_count (), selected_count ());
	}
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
	GType       type;
	GtkWidget  *found;
} Find;

static void
find_cb (GtkWidget *widget, gpointer data)
{
	Find *find = data;

	if (find->found != NULL) {
		return;
	}
	if ((find->type_name != NULL && is_a (widget, find->type_name)) ||
	    (find->type != 0 && g_type_is_a (G_OBJECT_TYPE (widget), find->type))) {
		find->found = widget;
	} else if (GTK_IS_CONTAINER (widget)) {
		gtk_container_forall (GTK_CONTAINER (widget), find_cb, data);
	}
}

static GtkWidget *
find_widget (GtkWidget *top, const char *type_name, GType type)
{
	Find find = { type_name, type, NULL };

	if (top != NULL) {
		find_cb (top, &find);
	}
	return find.found;
}

/* The tree view or the icon container of the view in front. */
static GtkWidget *
pane (void)
{
	GtkWidget *view = find_widget (window, "NemoView", 0);
	GtkWidget *child = view != NULL ? gtk_bin_get_child (GTK_BIN (view)) : NULL;

	return child != NULL && gtk_widget_get_mapped (child) ? child : NULL;
}

static GtkWidget *
search_entry (void)
{
	return find_widget (find_widget (window, "NemoQueryEditor", 0), NULL, GTK_TYPE_ENTRY);
}

static gboolean
find_bar_shows (void)
{
	GtkWidget *entry = search_entry ();

	return entry != NULL && gtk_widget_get_mapped (entry);
}

static gboolean
title_starts (const char *prefix)
{
	const char *title = gtk_window_get_title (GTK_WINDOW (window));

	return title != NULL && g_str_has_prefix (title, prefix);
}

/* The title is the query's own name, in quotes. */
static gboolean
in_search (void)
{
	const char *title = gtk_window_get_title (GTK_WINDOW (window));

	return title != NULL && strstr (title, "Search in") != NULL;
}

static gboolean
in_folder (void)
{
	return title_starts (folder_name) && item_count () == FOLDER_ITEMS && !find_bar_shows ();
}

static int
item_count (void)
{
	GtkWidget *items = pane ();

	if (items == NULL) {
		return -1;
	}
	if (GTK_IS_TREE_VIEW (items)) {
		GtkTreeModel *model = gtk_tree_view_get_model (GTK_TREE_VIEW (items));

		return model != NULL ? gtk_tree_model_iter_n_children (model, NULL) : -1;
	}
	return atk_object_get_n_accessible_children (gtk_widget_get_accessible (items));
}

static int
selected_count (void)
{
	GtkWidget *items = pane ();

	if (items == NULL) {
		return -1;
	}
	if (GTK_IS_TREE_VIEW (items)) {
		return gtk_tree_selection_count_selected_rows
			(gtk_tree_view_get_selection (GTK_TREE_VIEW (items)));
	}
	return atk_selection_get_selection_count (ATK_SELECTION (gtk_widget_get_accessible (items)));
}

static void
select_first (void)
{
	GtkWidget *items = pane ();

	gtk_widget_grab_focus (items);
	if (GTK_IS_TREE_VIEW (items)) {
		GtkTreePath *first = gtk_tree_path_new_first ();

		gtk_tree_selection_select_path (gtk_tree_view_get_selection (GTK_TREE_VIEW (items)), first);
		gtk_tree_path_free (first);
	} else {
		atk_selection_add_selection (ATK_SELECTION (gtk_widget_get_accessible (items)), 0);
	}
}

/* A rename field in the view has the keys. */
static gboolean
renaming (void)
{
	GtkWidget *focus = gtk_window_get_focus (GTK_WINDOW (window));
	GtkWidget *items = pane ();

	return focus != NULL && items != NULL && GTK_IS_EDITABLE (focus) &&
	       gtk_widget_is_ancestor (focus, items);
}

static gboolean
shortcut (guint key, GdkModifierType mods)
{
	return gtk_accel_groups_activate (G_OBJECT (window), key, mods);
}

static void
press_escape (void)
{
	GdkDisplay *display = gtk_widget_get_display (window);
	GdkEvent *event = gdk_event_new (GDK_KEY_PRESS);
	GdkKeymapKey *keys = NULL;
	gint n_keys = 0;

	event->key.window = g_object_ref (gtk_widget_get_window (window));
	event->key.send_event = TRUE;
	event->key.time = GDK_CURRENT_TIME;
	event->key.keyval = GDK_KEY_Escape;
	if (gdk_keymap_get_entries_for_keyval (gdk_keymap_get_for_display (display),
					       GDK_KEY_Escape, &keys, &n_keys) && n_keys > 0) {
		event->key.hardware_keycode = keys[0].keycode;
		event->key.group = keys[0].group;
	}
	g_free (keys);
	gdk_event_set_device (event, gdk_seat_get_keyboard (gdk_display_get_default_seat (display)));

	gtk_main_do_event (event);
	gdk_event_free (event);
}

static void
search_for_note (void)
{
	GtkWidget *entry = search_entry ();

	gtk_entry_set_text (GTK_ENTRY (entry), "note");
	g_signal_emit_by_name (entry, "activate");
}

static gboolean
results_shown (void)
{
	return in_search () && item_count () == FOUND;
}

/* Up to a minute in all. Each step waits for its own sign, with a limit. */
static gboolean
tick (G_GNUC_UNUSED gpointer data)
{
	GList *toplevels, *l;
	int since;

	if (++waited > 600) {
		char *line = g_strdup_printf ("the steps finished in time (stuck at step %d)", step);

		report (FALSE, line);
		g_free (line);
		finish ();
		return G_SOURCE_REMOVE;
	}

	if (step == 0) {
		toplevels = gtk_window_list_toplevels ();
		for (l = toplevels; l != NULL; l = l->next) {
			if (is_a (l->data, "NemoWindow") && gtk_widget_get_mapped (l->data)) {
				window = l->data;
			}
		}
		g_list_free (toplevels);
		if (window == NULL || !title_starts (folder_name) || item_count () != FOLDER_ITEMS) {
			return G_SOURCE_CONTINUE;
		}
		step = 1;
		step_at = waited;
		return G_SOURCE_CONTINUE;
	}

	since = waited - step_at;
	switch (step) {
	case 1:
		if (since < 5) {
			return G_SOURCE_CONTINUE;
		}
		shortcut (GDK_KEY_f, GDK_CONTROL_MASK);
		break;
	case 2:
		if (!find_bar_shows () && since < 50) {
			return G_SOURCE_CONTINUE;
		}
		report (find_bar_shows (), "Ctrl+F shows the find bar");
		search_for_note ();
		break;
	case 3:
		if (!results_shown () && since < 150) {
			return G_SOURCE_CONTINUE;
		}
		report (results_shown (), "the search finds 2 files");
		report (gtk_widget_has_focus (search_entry ()), "the search box has the keys");
		press_escape ();
		break;
	case 4:
		if (!in_folder () && since < 50) {
			return G_SOURCE_CONTINUE;
		}
		report (title_starts (folder_name) && item_count () == FOLDER_ITEMS,
			"Escape in the search box goes back to the folder");
		report (!find_bar_shows (), "and the find bar closes");
		shortcut (GDK_KEY_f, GDK_CONTROL_MASK);
		break;
	case 5:
		if (!find_bar_shows () && since < 50) {
			return G_SOURCE_CONTINUE;
		}
		search_for_note ();
		break;
	case 6:
		if (!results_shown () && since < 150) {
			return G_SOURCE_CONTINUE;
		}
		select_first ();
		break;
	case 7:
		if (since < 5) {
			return G_SOURCE_CONTINUE;
		}
		report (results_shown () && selected_count () == 1 &&
			gtk_widget_has_focus (pane ()), "a result is selected and the results have the keys");
		press_escape ();
		break;
	case 8:
		if (since < 10) {
			return G_SOURCE_CONTINUE;
		}
		report (results_shown () && selected_count () == 0,
			"the first Escape in the results clears the selection and stays");
		press_escape ();
		break;
	case 9:
		if (!in_folder () && since < 50) {
			return G_SOURCE_CONTINUE;
		}
		report (in_folder (),
			"the next Escape in the results goes back to the folder");
		shortcut (GDK_KEY_f, GDK_CONTROL_MASK);
		break;
	case 10:
		if (!find_bar_shows () && since < 50) {
			return G_SOURCE_CONTINUE;
		}
		search_for_note ();
		break;
	case 11:
		if (!results_shown () && since < 150) {
			return G_SOURCE_CONTINUE;
		}
		select_first ();
		break;
	case 12:
		/* Rename turns on from an idle after the selection changes. */
		if (since < 5) {
			return G_SOURCE_CONTINUE;
		}
		shortcut (GDK_KEY_F2, 0);
		break;
	case 13:
		if (!renaming () && since < 30) {
			return G_SOURCE_CONTINUE;
		}
		report (renaming (), "F2 renames a result");
		press_escape ();
		break;
	case 14:
		if (since < 10) {
			return G_SOURCE_CONTINUE;
		}
		report (!renaming () && results_shown () && find_bar_shows (),
			"Escape ends the rename and stays in the results");
		finish ();
		return G_SOURCE_REMOVE;
	}

	step++;
	step_at = waited;
	return G_SOURCE_CONTINUE;
}

__attribute__((constructor)) static void
probe_start (void)
{
	const char *path = g_getenv ("NEMO_ESC_OUT");

	folder_name = g_strdup (g_getenv ("NEMO_ESC_FOLDER"));
	if (path == NULL || folder_name == NULL) {
		return;
	}
	out = fopen (path, "w");
	/* Anything the program starts loads this too, and would empty the file
	   again. */
	g_unsetenv ("NEMO_ESC_OUT");
	g_unsetenv ("NEMO_ESC_FOLDER");
	if (out == NULL) {
		return;
	}
	g_timeout_add (100, tick, NULL);
}
