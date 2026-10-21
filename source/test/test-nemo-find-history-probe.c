/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */

/* test-nemo-find-history-probe.c - goes back and forth over a search in the
   history, from inside the program.

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

/* Preloaded into the real program by test-nemo-find-history. It searches the
 * folder, goes Back and Forward, leaves find mode, goes Back into the search,
 * leaves again, searches for something else, and goes from that search to the
 * first through the Back button's history menu. What the window shows after
 * each step is one "ok" or "FAIL" line in $NEMO_FIND_HISTORY_OUT, and "done"
 * goes at the end. */

#include <stdio.h>
#include <string.h>

#include <gtk/gtk.h>

static FILE      *out;
static GtkWidget *window;
static int        step;
static int        waited;
static int        steady;
static guint      loads;
static guint      loads_before;
static guint      glib_handler;
static int        glib_criticals;

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
find_in (GtkWidget *top, const char *type_name)
{
	Find find = { type_name, NULL };

	if (top != NULL) {
		find_cb (top, &find);
	}
	return find.found;
}

static GtkEntry *
search_entry (void)
{
	GtkWidget *entry = find_in (find_in (window, "NemoQueryEditor"), "GtkEntry");

	return entry != NULL ? GTK_ENTRY (entry) : NULL;
}

typedef struct {
	const char *label;
	GtkWidget  *found;
} ItemSearch;

static void
find_item_cb (GtkWidget *widget, gpointer data)
{
	ItemSearch *search = data;

	if (search->found != NULL) {
		return;
	}
	if (GTK_IS_MENU_ITEM (widget) && gtk_widget_get_visible (widget) &&
	    g_strcmp0 (gtk_menu_item_get_label (GTK_MENU_ITEM (widget)), search->label) == 0) {
		search->found = widget;
		return;
	}
	if (GTK_IS_CONTAINER (widget)) {
		gtk_container_forall (GTK_CONTAINER (widget), find_item_cb, data);
	}
}

static GtkWidget *
menu_item (const char *label)
{
	GList *toplevels = gtk_window_list_toplevels ();
	GList *l;
	ItemSearch search = { label, NULL };

	for (l = toplevels; l != NULL && search.found == NULL; l = l->next) {
		find_item_cb (l->data, &search);
	}
	g_list_free (toplevels);

	return search.found;
}

static void
pick (const char *label)
{
	loads_before = loads;
	gtk_menu_item_activate (GTK_MENU_ITEM (menu_item (label)));
}

static const char *
title (void)
{
	const char *text = window != NULL ? gtk_window_get_title (GTK_WINDOW (window)) : NULL;

	return text != NULL ? text : "";
}

static gboolean
finding (void)
{
	return strstr (title (), "Search") != NULL;
}

/* Rows in the list view, or -1 with none showing. */
static int
rows (void)
{
	GtkWidget *tree = find_in (find_in (window, "NemoListView"), "GtkTreeView");
	GtkTreeModel *model = tree != NULL ? gtk_tree_view_get_model (GTK_TREE_VIEW (tree)) : NULL;

	return model != NULL ? gtk_tree_model_iter_n_children (model, NULL) : -1;
}

static char *
state (void)
{
	GtkEntry *entry = search_entry ();

	return g_strdup_printf ("title \"%s\", %d rows, query \"%s\"", title (), rows (),
				entry != NULL ? gtk_entry_get_text (entry) : "");
}

/* Searches fill in as they go, so what shows only counts once it stays put. */
static gboolean
settled (void)
{
	static char *last;
	char *now = state ();

	steady = g_strcmp0 (last, now) == 0 ? steady + 1 : 0;
	g_free (last);
	last = now;
	return steady >= 10;
}

static void
query (const char *text)
{
	GtkEntry *entry = search_entry ();

	loads_before = loads;
	gtk_entry_set_text (entry, text);
	g_signal_emit_by_name (entry, "activate");
}

static gboolean
count_load (G_GNUC_UNUSED GSignalInvocationHint *hint,
	    G_GNUC_UNUSED guint n_params,
	    G_GNUC_UNUSED const GValue *params,
	    G_GNUC_UNUSED gpointer data)
{
	loads++;
	return TRUE;
}

static void
check (gboolean passed, const char *what)
{
	char *now = state ();
	char *line = g_strdup_printf ("%s (%s)", what, now);

	report (passed, passed ? what : line);
	g_free (line);
	g_free (now);
}

static gboolean
shows_search (const char *text, int count)
{
	GtkEntry *entry = search_entry ();

	return strstr (title (), "Search in") != NULL && strstr (title (), "findme") != NULL &&
	       entry != NULL && g_strcmp0 (gtk_entry_get_text (entry), text) == 0 &&
	       rows () == count;
}

static gboolean
shows_folder (void)
{
	return !finding () && strstr (title (), "findme") != NULL && rows () == 4;
}

typedef struct {
	const char *action;
	GtkWidget  *found;
} ButtonSearch;

static void
find_button_cb (GtkWidget *widget, gpointer data)
{
	ButtonSearch *search = data;
	GtkAction *action;

	if (search->found != NULL) {
		return;
	}
	if (GTK_IS_BUTTON (widget) && gtk_widget_get_mapped (widget)) {
		G_GNUC_BEGIN_IGNORE_DEPRECATIONS
		action = gtk_activatable_get_related_action (GTK_ACTIVATABLE (widget));
		if (action != NULL && g_strcmp0 (gtk_action_get_name (action), search->action) == 0) {
			search->found = widget;
			return;
		}
		G_GNUC_END_IGNORE_DEPRECATIONS
	}
	if (GTK_IS_CONTAINER (widget)) {
		gtk_container_forall (GTK_CONTAINER (widget), find_button_cb, data);
	}
}

/* A right click on the toolbar's Back button pops its history up, and so
   does holding the left button down a while. */
static void
press_back (guint button)
{
	ButtonSearch search = { "Back", NULL };
	GdkEvent *event;
	gboolean handled = FALSE;

	find_button_cb (window, &search);
	if (search.found == NULL) {
		return;
	}
	event = gdk_event_new (GDK_BUTTON_PRESS);
	event->button.window = g_object_ref (gtk_widget_get_window (search.found));
	event->button.button = button;
	event->button.time = GDK_CURRENT_TIME;
	gdk_event_set_device (event, gdk_seat_get_pointer (gdk_display_get_default_seat (gdk_display_get_default ())));
	g_signal_emit_by_name (search.found, "button-press-event", event, &handled);
	gdk_event_free (event);
}

static void
menu_items_cb (GtkWidget *widget, gpointer data)
{
	GList **items = data;

	if (GTK_IS_MENU (widget)) {
		GList *children = gtk_container_get_children (GTK_CONTAINER (widget));

		if (*items == NULL) {
			*items = children;
		} else {
			g_list_free (children);
		}
	} else if (GTK_IS_CONTAINER (widget)) {
		gtk_container_forall (GTK_CONTAINER (widget), menu_items_cb, data);
	}
}

/* The items of the popup menu that is up, in order. */
static GList *
shown_menu_items (void)
{
	GList *toplevels = gtk_window_list_toplevels ();
	GList *l, *items = NULL;

	for (l = toplevels; l != NULL && items == NULL; l = l->next) {
		if (gtk_window_get_window_type (l->data) == GTK_WINDOW_POPUP &&
		    gtk_widget_get_visible (l->data)) {
			menu_items_cb (l->data, &items);
		}
	}
	g_list_free (toplevels);

	return items;
}

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
}

/* Each step waits for what the one before set going. Up to ten seconds each. */
static gboolean
tick (G_GNUC_UNUSED gpointer data)
{
	if (++waited > 100) {
		char *line = g_strdup_printf ("timed out at step %d", step);

		check (FALSE, line);
		g_free (line);
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
		if (window == NULL || menu_item ("_Search for files...") == NULL || !settled ()) {
			return G_SOURCE_CONTINUE;
		}
		g_signal_add_emission_hook (g_signal_lookup ("begin_loading", g_type_from_name ("NemoView")),
					    0, count_load, NULL, NULL);
		pick ("_Search for files...");
		break;
	}
	case 1:
		if (search_entry () == NULL) {
			return G_SOURCE_CONTINUE;
		}
		query ("apple");
		break;
	case 2:
		if (loads == loads_before || !settled ()) {
			return G_SOURCE_CONTINUE;
		}
		check (shows_search ("apple", 2), "the search shows its 2 results");
		pick ("_Back");
		break;
	case 3:
		if (loads == loads_before || !settled ()) {
			return G_SOURCE_CONTINUE;
		}
		check (shows_folder (), "Back shows the folder");
		pick ("_Forward");
		break;
	case 4:
		if (loads == loads_before || !settled ()) {
			return G_SOURCE_CONTINUE;
		}
		check (shows_search ("apple", 2), "Forward shows the search it left");
		pick ("_Search for files...");
		break;
	case 5:
		if (loads == loads_before || !settled ()) {
			return G_SOURCE_CONTINUE;
		}
		check (shows_folder (), "leaving find mode shows the folder");
		pick ("_Back");
		break;
	case 6:
		if (loads == loads_before || !settled ()) {
			return G_SOURCE_CONTINUE;
		}
		check (shows_search ("apple", 2), "Back after leaving shows the search");
		pick ("_Search for files...");
		break;
	case 7:
		if (loads == loads_before || !settled ()) {
			return G_SOURCE_CONTINUE;
		}
		check (shows_folder (), "leaving again shows the folder");
		pick ("_Search for files...");
		break;
	case 8:
		if (search_entry () == NULL) {
			return G_SOURCE_CONTINUE;
		}
		query ("pear");
		break;
	case 9:
		if (loads == loads_before || !settled ()) {
			return G_SOURCE_CONTINUE;
		}
		check (shows_search ("pear", 1), "a new search looks in the folder");
		press_back (3);
		break;
	case 10: {
		/* Back's history is the folder, the first search and the folder. */
		GList *items = shown_menu_items ();

		if (g_list_length (items) != 3) {
			g_list_free (items);
			return G_SOURCE_CONTINUE;
		}
		loads_before = loads;
		gtk_menu_item_activate (GTK_MENU_ITEM (g_list_nth_data (items, 1)));
		g_list_free (items);
		break;
	}
	case 11:
		if (loads == loads_before || !settled ()) {
			return G_SOURCE_CONTINUE;
		}
		check (shows_search ("apple", 2), "the history menu goes from one search to the other");
		pick ("_Search for files...");
		break;
	case 12:
		if (loads == loads_before || !settled ()) {
			return G_SOURCE_CONTINUE;
		}
		check (shows_folder (), "leaving that one shows the folder");
		glib_handler = g_log_set_handler ("GLib", G_LOG_LEVEL_CRITICAL, count_glib_critical, NULL);
		press_back (1);
		break;
	case 13: {
		/* Its timer has run once the menu is up, so the next press must not
		   remove it again (#2026102113191977). */
		GList *items;

		if (waited < 20) {
			return G_SOURCE_CONTINUE;
		}
		items = shown_menu_items ();
		if (items == NULL) {
			return G_SOURCE_CONTINUE;
		}
		gtk_menu_shell_deactivate (GTK_MENU_SHELL (gtk_widget_get_parent (items->data)));
		g_list_free (items);
		press_back (1);
		g_log_remove_handler ("GLib", glib_handler);
		check (glib_criticals == 0, "holding Back down twice logs no GLib criticals");
		finish ();
		return G_SOURCE_REMOVE;
	}
	}

	step++;
	waited = 0;
	steady = 0;
	return G_SOURCE_CONTINUE;
}

__attribute__((constructor)) static void
probe_start (void)
{
	const char *path = g_getenv ("NEMO_FIND_HISTORY_OUT");

	if (path == NULL) {
		return;
	}
	out = fopen (path, "w");
	if (out == NULL) {
		return;
	}
	g_timeout_add (100, tick, NULL);
}
