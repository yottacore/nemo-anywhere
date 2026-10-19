/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */

/* test-nemo-sort-views-probe.c - one sort for the list and icon views, from
   inside the program.

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

/* Preloaded into the real program by test-nemo-sort-views. The folder opens
 * in the list view, and the probe sorts it by Size, largest first. Then:
 *
 * - It opens the picture folder inside, which switches itself to icons. Those
 *   are sorted by size, reversed, whether the window holds the sort or the
 *   picture folder takes the parent's settings.
 * - There it sorts by type and goes back. With per-folder settings off the
 *   window's sort follows, so the list is by Type. With them on the parent
 *   keeps its own, by Size.
 * - It switches the parent to icons by hand, which keep that sort.
 *
 * $NEMO_SORTSW_REMEMBER says which. One "ok" or "FAIL" line per check goes to
 * $NEMO_SORTSW_OUT, and "done" at the end.
 *
 * No symbol of the program is used. */

#include <stdio.h>
#include <string.h>

#include <gtk/gtk.h>

static FILE       *out;
static gboolean    remember;
static GtkWidget  *window;
static int         step;
static int         waited;
static int         settled;

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
find_widget (const char *type_name)
{
	Find find = { type_name, NULL };

	find_cb (window, &find);
	return find.found;
}

static GtkTreeView *
tree_view (void)
{
	GtkWidget *view = find_widget ("NemoListView");
	GtkWidget *child = view != NULL ? gtk_bin_get_child (GTK_BIN (view)) : NULL;

	return GTK_IS_TREE_VIEW (child) ? GTK_TREE_VIEW (child) : NULL;
}

static gboolean
showing_icons (void)
{
	return tree_view () == NULL && find_widget ("NemoIconView") != NULL;
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
ticked (const char *label)
{
	GtkWidget *item = menu_item (label);

	return GTK_IS_CHECK_MENU_ITEM (item) && gtk_check_menu_item_get_active (GTK_CHECK_MENU_ITEM (item));
}

static gboolean
pick (const char *label)
{
	GtkWidget *item = menu_item (label);

	if (item == NULL) {
		return FALSE;
	}
	gtk_menu_item_activate (GTK_MENU_ITEM (item));
	return TRUE;
}

static gboolean
icons_sorted_by (const char *label)
{
	return showing_icons () && ticked (label) && ticked ("Re_versed order");
}

static gboolean
in_folder (const char *name)
{
	const char *title = gtk_window_get_title (GTK_WINDOW (window));

	return title != NULL && g_str_has_prefix (title, name) && title[strlen (name)] == ' ';
}

/* A view settles a moment after it shows, so a check holds for a second
   before it counts. Gives up after ten. */
static gboolean
holds (gboolean now)
{
	settled = now ? settled + 1 : 0;
	return settled >= 10;
}

static gboolean
open_first_row (void)
{
	GtkTreeView *tree = tree_view ();
	GtkTreePath *path;

	if (tree == NULL) {
		return FALSE;
	}
	path = gtk_tree_path_new_first ();
	gtk_tree_view_set_cursor (tree, path, NULL, FALSE);
	gtk_tree_view_row_activated (tree, path, gtk_tree_view_get_column (tree, 0));
	gtk_tree_path_free (path);
	return TRUE;
}

static gboolean
tick (G_GNUC_UNUSED gpointer data)
{
	static const char *const timeouts[] = {
		"timed out waiting for the folder",
		"the list sorts by Size, largest first",
		"the picture folder shows icons by size, reversed",
		"icons pick by type",
		"back in the list, the sort is the one expected",
		"icons picked by hand keep the list's sort",
	};
	gboolean ready;

	if (++waited > 100) {
		report (FALSE, timeouts[step]);
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
		if (window == NULL || !in_folder ("sortme") || column_titled ("Size") == NULL) {
			return G_SOURCE_CONTINUE;
		}
		/* The first click sorts smallest first, the second turns it. */
		gtk_tree_view_column_clicked (column_titled ("Size"));
		gtk_tree_view_column_clicked (column_titled ("Size"));
		break;
	}
	case 1:
		if (!holds (list_sorted_by ("Size", GTK_SORT_DESCENDING))) {
			return G_SOURCE_CONTINUE;
		}
		report (TRUE, timeouts[step]);
		/* Folders come first, and there is one. */
		open_first_row ();
		break;
	case 2:
		ready = in_folder ("pics") && showing_icons ();
		if (!ready) {
			settled = 0;
			return G_SOURCE_CONTINUE;
		}
		if (!holds (icons_sorted_by ("By _size")) && waited < 100) {
			return G_SOURCE_CONTINUE;
		}
		report (icons_sorted_by ("By _size"), timeouts[step]);
		pick ("By _type");
		break;
	case 3:
		if (!holds (icons_sorted_by ("By _type"))) {
			return G_SOURCE_CONTINUE;
		}
		report (TRUE, timeouts[step]);
		pick ("_Back");
		break;
	case 4:
		ready = in_folder ("sortme") && tree_view () != NULL;
		if (!ready) {
			settled = 0;
			return G_SOURCE_CONTINUE;
		}
		if (!holds (list_sorted_by (remember ? "Size" : "Type", GTK_SORT_DESCENDING)) && waited < 100) {
			return G_SOURCE_CONTINUE;
		}
		report (list_sorted_by (remember ? "Size" : "Type", GTK_SORT_DESCENDING),
			remember ? "back in the list, the parent keeps Size, largest first" :
				   "back in the list, the window's sort is Type, largest first");
		pick ("Icon view");
		break;
	case 5:
		if (!showing_icons ()) {
			settled = 0;
			return G_SOURCE_CONTINUE;
		}
		if (!holds (icons_sorted_by (remember ? "By _size" : "By _type")) && waited < 100) {
			return G_SOURCE_CONTINUE;
		}
		report (icons_sorted_by (remember ? "By _size" : "By _type"), timeouts[step]);
		finish ();
		return G_SOURCE_REMOVE;
	}

	step++;
	waited = 0;
	settled = 0;
	return G_SOURCE_CONTINUE;
}

__attribute__((constructor)) static void
probe_start (void)
{
	const char *path = g_getenv ("NEMO_SORTSW_OUT");

	if (path == NULL) {
		return;
	}
	remember = g_strcmp0 (g_getenv ("NEMO_SORTSW_REMEMBER"), "true") == 0;
	out = fopen (path, "w");
	g_unsetenv ("NEMO_SORTSW_OUT");
	if (out == NULL) {
		return;
	}
	g_timeout_add (100, tick, NULL);
}
