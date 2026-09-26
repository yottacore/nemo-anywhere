/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */

/* test-eel-clipped-cell.c - when a list cell's tooltip shows its whole value.

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

/* A value cut short by a narrow column comes back whole, for the tooltip. One
 * that fits, or an empty one, gives nothing, so no tooltip repeats what is
 * already on screen. The width is what every renderer in the column wants, so
 * an icon beside the text counts. */

#include <config.h>

#include <gtk/gtk.h>
#include <eel/eel-gtk-extensions.h>

#include "test-check.h"

enum { ROW_LONG, ROW_SHORT, ROW_EMPTY };

static void
settle (void)
{
	int i;

	for (i = 0; i < 20; i++) {
		while (gtk_events_pending ()) {
			gtk_main_iteration ();
		}
		g_usleep (10000);
	}
}

static GtkTreeViewColumn *
add_column (GtkTreeView *tree_view, gboolean with_icon)
{
	GtkTreeViewColumn *column = gtk_tree_view_column_new ();
	GtkCellRenderer *cell;

	if (with_icon) {
		cell = gtk_cell_renderer_pixbuf_new ();
		g_object_set (cell, "icon-name", "folder", "stock-size", GTK_ICON_SIZE_DIALOG, NULL);
		gtk_tree_view_column_pack_start (column, cell, FALSE);
	}
	cell = gtk_cell_renderer_text_new ();
	g_object_set (cell, "ellipsize", PANGO_ELLIPSIZE_END, NULL);
	gtk_tree_view_column_pack_start (column, cell, TRUE);
	gtk_tree_view_column_add_attribute (column, cell, "text", 0);
	gtk_tree_view_column_set_sizing (column, GTK_TREE_VIEW_COLUMN_FIXED);
	gtk_tree_view_append_column (tree_view, column);

	return column;
}

static char *
clipped (GtkTreeViewColumn *column, GtkTreeModel *model, int row)
{
	GtkTreeIter iter;

	gtk_tree_model_iter_nth_child (model, &iter, NULL, row);
	return eel_gtk_tree_view_column_clipped_text (column, model, &iter);
}

int
main (int argc, char *argv[])
{
	GtkWidget *window, *tree_view;
	GtkListStore *store;
	GtkTreeModel *model;
	GtkTreeViewColumn *plain, *iconed, *filler;
	GtkTreeIter iter;
	char *text;

	if (!gtk_init_check (&argc, &argv)) {
		g_print ("SKIP: no display\n");
		return 77;
	}

	store = gtk_list_store_new (1, G_TYPE_STRING);
	gtk_list_store_insert_with_values (store, &iter, ROW_LONG, 0,
					   "A rather long file name that no narrow column holds.txt", -1);
	gtk_list_store_insert_with_values (store, &iter, ROW_SHORT, 0, "ab", -1);
	gtk_list_store_insert_with_values (store, &iter, ROW_EMPTY, 0, "", -1);
	model = GTK_TREE_MODEL (store);

	tree_view = gtk_tree_view_new_with_model (model);
	plain = add_column (GTK_TREE_VIEW (tree_view), FALSE);
	iconed = add_column (GTK_TREE_VIEW (tree_view), TRUE);
	gtk_tree_view_column_set_fixed_width (plain, 120);
	gtk_tree_view_column_set_fixed_width (iconed, 120);

	/* Takes the room left over, which would otherwise go to the last column. */
	filler = gtk_tree_view_column_new ();
	gtk_tree_view_column_set_expand (filler, TRUE);
	gtk_tree_view_append_column (GTK_TREE_VIEW (tree_view), filler);

	window = gtk_window_new (GTK_WINDOW_TOPLEVEL);
	gtk_window_set_default_size (GTK_WINDOW (window), 800, 200);
	gtk_container_add (GTK_CONTAINER (window), tree_view);
	gtk_widget_show_all (window);
	settle ();

	text = clipped (plain, model, ROW_LONG);
	check (g_strcmp0 (text, "A rather long file name that no narrow column holds.txt") == 0);
	g_free (text);

	text = clipped (plain, model, ROW_SHORT);
	check (text == NULL);
	g_free (text);

	text = clipped (plain, model, ROW_EMPTY);
	check (text == NULL);
	g_free (text);

	/* Narrow enough that "ab" still fits on its own, but not beside a big icon. */
	gtk_tree_view_column_set_fixed_width (plain, 40);
	gtk_tree_view_column_set_fixed_width (iconed, 40);
	settle ();
	text = clipped (plain, model, ROW_SHORT);
	check (text == NULL);
	g_free (text);
	text = clipped (iconed, model, ROW_SHORT);
	check (g_strcmp0 (text, "ab") == 0);
	g_free (text);
	check (gtk_tree_view_column_get_width (iconed) == 40);

	/* The icon alone overflows here, but there is no text to show. */
	text = clipped (iconed, model, ROW_EMPTY);
	check (text == NULL);
	g_free (text);

	/* Widened past the whole value, it fits and there is nothing to show. */
	gtk_tree_view_column_set_fixed_width (plain, 600);
	settle ();
	text = clipped (plain, model, ROW_LONG);
	check (text == NULL);
	g_free (text);

	gtk_widget_destroy (window);
	g_object_unref (store);

	if (failures > 0) {
		g_printerr ("%d check(s) failed\n", failures);
		return 1;
	}
	g_print ("PASS\n");
	return 0;
}
