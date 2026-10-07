/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */

/* test-nemo-reset-sort-probe.c - sorts a folder by size, then resets the view,
   from inside the program.

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

/* Preloaded into the real program by test-nemo-reset-sort. Once the folder
 * shows, it sorts by Size, largest first in the list view, through the same
 * column header click or menu item a person would use. Then it picks "Reset
 * view to defaults" from the View menu and waits for the folder to come back
 * sorted by Name, smallest first. One "ok" or "FAIL" line per check goes to
 * $NEMO_RESET_OUT, and "done" at the end. */

#include <stdio.h>
#include <string.h>

#include <gtk/gtk.h>

static FILE       *out;
static gboolean    list_view;
static GtkWidget  *window;
static int         step;
static int         waited;
static int         steady;

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

static GtkTreeView *
tree_view (void)
{
	GtkWidget *view = find_widget ("NemoView");
	GtkWidget *child = view != NULL ? gtk_bin_get_child (GTK_BIN (view)) : NULL;

	return GTK_IS_TREE_VIEW (child) ? GTK_TREE_VIEW (child) : NULL;
}

static GtkTreeViewColumn *
column_titled (const char *title)
{
	GtkTreeView *tree = tree_view ();
	GtkTreeViewColumn *found = NULL;
	GList *columns, *l;

	if (tree == NULL) {
		return NULL;
	}
	columns = gtk_tree_view_get_columns (tree);
	for (l = columns; l != NULL && found == NULL; l = l->next) {
		if (g_strcmp0 (gtk_tree_view_column_get_title (l->data), title) == 0) {
			found = l->data;
		}
	}
	g_list_free (columns);

	return found;
}

/* The column showing the sort arrow, as a person sees it. */
static gboolean
list_sorted_by (const char *title, GtkSortType order)
{
	GtkTreeViewColumn *column = column_titled (title);

	return column != NULL && gtk_tree_view_column_get_sort_indicator (column) &&
	       gtk_tree_view_column_get_sort_order (column) == order;
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

static gboolean
icon_sorted_by (const char *label)
{
	GtkWidget *item = menu_item (label);

	return GTK_IS_CHECK_MENU_ITEM (item) && gtk_check_menu_item_get_active (GTK_CHECK_MENU_ITEM (item));
}

static gboolean
by_size (void)
{
	return list_view ? list_sorted_by ("Size", GTK_SORT_DESCENDING) : icon_sorted_by ("By _size");
}

static gboolean
by_name (void)
{
	return list_view ? list_sorted_by ("Name", GTK_SORT_ASCENDING) : icon_sorted_by ("By _name");
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
		report (FALSE, step == 2 ? "the reset goes back to Name" :
			step == 1 ? "sorted by Size" : "timed out waiting for the folder");
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
		if (window == NULL || menu_item ("Reset view to _defaults") == NULL) {
			return G_SOURCE_CONTINUE;
		}
		if (list_view ? column_titled ("Size") == NULL : menu_item ("By _size") == NULL) {
			return G_SOURCE_CONTINUE;
		}
		report (by_name (), "a new folder sorts by Name");
		if (list_view) {
			/* The first click sorts smallest first, the second turns it. */
			gtk_tree_view_column_clicked (column_titled ("Size"));
			gtk_tree_view_column_clicked (column_titled ("Size"));
		} else {
			gtk_menu_item_activate (GTK_MENU_ITEM (menu_item ("By _size")));
		}
		break;
	}
	case 1:
		if (!by_size ()) {
			return G_SOURCE_CONTINUE;
		}
		report (TRUE, "sorted by Size");
		gtk_menu_item_activate (GTK_MENU_ITEM (menu_item ("Reset view to _defaults")));
		break;
	case 2:
		/* The reset reloads the folder, so it must hold, not just pass by. */
		steady = by_name () ? steady + 1 : 0;
		if (steady < 10) {
			return G_SOURCE_CONTINUE;
		}
		report (TRUE, "the reset goes back to Name");
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
	const char *path = g_getenv ("NEMO_RESET_OUT");

	if (path == NULL) {
		return;
	}
	list_view = g_strcmp0 (g_getenv ("NEMO_RESET_VIEW"), "list-view") == 0;
	out = fopen (path, "w");
	if (out == NULL) {
		return;
	}
	g_timeout_add (100, tick, NULL);
}
