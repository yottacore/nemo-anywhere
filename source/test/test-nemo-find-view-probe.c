/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */

/* test-nemo-find-view-probe.c - goes into find mode from a folder in icon
   view, picks another view there, and comes back, from inside the program.

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

/* Preloaded into the real program by test-nemo-find-view. Everything goes
 * through the menu items and the search entry a person would use: the folder
 * goes to icon view, then find mode, then compact view for the results, then a
 * new query, then back to the folder. One "ok" or "FAIL" line per check goes
 * to $NEMO_FIND_VIEW_OUT, and "done" at the end. */

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

/* The file name entry is the first one in the bar. */
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

/* Menus hang off their own toplevels, shown or not. */
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
	gtk_menu_item_activate (GTK_MENU_ITEM (menu_item (label)));
}

/* The view as the View menu shows it. */
static gboolean
showing (const char *label)
{
	GtkWidget *item = menu_item (label);

	return GTK_IS_CHECK_MENU_ITEM (item) && gtk_check_menu_item_get_active (GTK_CHECK_MENU_ITEM (item));
}

static const char *
shown_view (void)
{
	return showing ("Icon view") ? "icons" : showing ("List view") ? "list" :
	       showing ("Compact view") ? "compact" : "none";
}

static gboolean
finding (void)
{
	const char *title = window != NULL ? gtk_window_get_title (GTK_WINDOW (window)) : NULL;

	return title != NULL && strstr (title, "Search in") != NULL;
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
query (const char *text)
{
	GtkEntry *entry = search_entry ();

	loads_before = loads;
	gtk_entry_set_text (entry, text);
	g_signal_emit_by_name (entry, "activate");
}

/* A load can switch views once it ends, so a view only counts once it stays put. */
static gboolean
settled (const char *view)
{
	static const char *last;

	steady = last == view ? steady + 1 : 0;
	last = view;
	return steady >= 10;
}

static void
check_view (const char *want, const char *what)
{
	const char *view = shown_view ();

	if (g_strcmp0 (view, want) == 0) {
		report (TRUE, what);
	} else {
		char *line = g_strdup_printf ("%s (shows %s)", what, view);

		report (FALSE, line);
		g_free (line);
	}
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

		report (FALSE, line);
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
		if (window == NULL || menu_item ("_Search for files...") == NULL ||
		    !settled (shown_view ())) {
			return G_SOURCE_CONTINUE;
		}
		pick ("Icon view");
		break;
	}
	case 1:
		if (!showing ("Icon view") || !settled (shown_view ())) {
			return G_SOURCE_CONTINUE;
		}
		g_signal_add_emission_hook (g_signal_lookup ("begin_loading", g_type_from_name ("NemoView")),
					    0, count_load, NULL, NULL);
		pick ("_Search for files...");
		break;
	case 2:
		if (search_entry () == NULL) {
			return G_SOURCE_CONTINUE;
		}
		query ("apple");
		break;
	case 3:
		if (loads == loads_before || !finding () || !settled (shown_view ())) {
			return G_SOURCE_CONTINUE;
		}
		check_view ("list", "find results open in list view");
		pick ("Compact view");
		break;
	case 4:
		if (!showing ("Compact view")) {
			return G_SOURCE_CONTINUE;
		}
		query ("appl");
		break;
	case 5:
		if (loads == loads_before || !settled (shown_view ())) {
			return G_SOURCE_CONTINUE;
		}
		check_view ("compact", "a new query keeps the view picked for the results");
		loads_before = loads;
		pick ("_Search for files...");
		break;
	case 6:
		if (loads == loads_before || finding () || !settled (shown_view ())) {
			return G_SOURCE_CONTINUE;
		}
		check_view ("icons", "the folder gets its own view back");
		finish ();
		return G_SOURCE_REMOVE;
	}

	step++;
	waited = 0;
	steady = 0;
	return G_SOURCE_CONTINUE;
}

__attribute__((constructor)) static void
probe_start (void)
{
	const char *path = g_getenv ("NEMO_FIND_VIEW_OUT");

	if (path == NULL) {
		return;
	}
	out = fopen (path, "w");
	if (out == NULL) {
		return;
	}
	g_timeout_add (100, tick, NULL);
}
