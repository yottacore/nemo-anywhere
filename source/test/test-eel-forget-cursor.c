/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */

/* test-eel-forget-cursor.c - what Escape leaves behind in the list view.

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

/* Escape clears the selection and forgets where the cursor was, so the next
 * arrow key starts over at the top, as in a folder just opened. The rows on
 * screen stay put: setting the cursor would otherwise scroll to it. A real
 * tree view, scrolled part way down a long list. */

#include <config.h>

#include <stdlib.h>
#include <gtk/gtk.h>
#include <eel/eel-gtk-extensions.h>

#include "test-check.h"

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

int
main (int argc, char *argv[])
{
	GtkWidget *window, *scrolled, *tree_view;
	GtkListStore *store;
	GtkTreeSelection *selection;
	GtkTreePath *path;
	GtkAdjustment *vadjustment;
	GtkTreeIter iter;
	double before;
	int i;

	if (!gtk_init_check (&argc, &argv)) {
		g_print ("SKIP: no display\n");
		return 77;
	}

	store = gtk_list_store_new (1, G_TYPE_STRING);
	for (i = 0; i < 200; i++) {
		char *name = g_strdup_printf ("file %03d", i);

		gtk_list_store_insert_with_values (store, &iter, -1, 0, name, -1);
		g_free (name);
	}

	tree_view = gtk_tree_view_new_with_model (GTK_TREE_MODEL (store));
	gtk_tree_view_append_column (GTK_TREE_VIEW (tree_view),
				     gtk_tree_view_column_new_with_attributes ("Name",
									       gtk_cell_renderer_text_new (),
									       "text", 0, NULL));
	selection = gtk_tree_view_get_selection (GTK_TREE_VIEW (tree_view));
	gtk_tree_selection_set_mode (selection, GTK_SELECTION_MULTIPLE);

	scrolled = gtk_scrolled_window_new (NULL, NULL);
	gtk_container_add (GTK_CONTAINER (scrolled), tree_view);
	window = gtk_window_new (GTK_WINDOW_TOPLEVEL);
	gtk_window_set_default_size (GTK_WINDOW (window), 300, 200);
	gtk_container_add (GTK_CONTAINER (window), scrolled);
	gtk_widget_show_all (window);
	settle ();

	/* Two rows picked well down the list, with the cursor on the second. */
	{
		GtkTreePath *first = gtk_tree_path_new_from_indices (120, -1);
		GtkTreePath *second = gtk_tree_path_new_from_indices (121, -1);

		gtk_tree_view_set_cursor (GTK_TREE_VIEW (tree_view), second, NULL, FALSE);
		gtk_tree_selection_select_range (selection, first, second);
		gtk_tree_path_free (first);
		gtk_tree_path_free (second);
	}
	settle ();

	vadjustment = gtk_scrollable_get_vadjustment (GTK_SCROLLABLE (tree_view));
	before = gtk_adjustment_get_value (vadjustment);
	check (before > 0);
	check (gtk_tree_selection_count_selected_rows (selection) > 0);

	eel_gtk_tree_view_forget_cursor (GTK_TREE_VIEW (tree_view));
	settle ();

	check (gtk_tree_selection_count_selected_rows (selection) == 0);

	path = NULL;
	gtk_tree_view_get_cursor (GTK_TREE_VIEW (tree_view), &path, NULL);
	check (path != NULL && gtk_tree_path_get_indices (path)[0] == 0);
	if (path != NULL) {
		gtk_tree_path_free (path);
	}

	g_print ("scrolled to %.0f before, %.0f after\n", before, gtk_adjustment_get_value (vadjustment));
	check (gtk_adjustment_get_value (vadjustment) == before);

	/* An empty list has nothing to put the cursor on, and that is fine. */
	gtk_list_store_clear (store);
	eel_gtk_tree_view_forget_cursor (GTK_TREE_VIEW (tree_view));
	check (gtk_tree_selection_count_selected_rows (selection) == 0);

	gtk_widget_destroy (window);
	g_object_unref (store);

	if (failures > 0) {
		g_printerr ("%d check(s) failed\n", failures);
		return EXIT_FAILURE;
	}
	g_print ("PASS\n");
	return EXIT_SUCCESS;
}
