/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */

/* test-nemo-tab-list.c - the tab list with sub-tabs in it.

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

/* Three levels of tabs. Each sub-tab is a row under its parent, indented a
 * step further per level. Every row on the way to the page showing is lit,
 * so a parent always has one sub-tab lit with it. Stepping goes through the
 * pages with nothing under them, in list order, wrapping, and skips what is
 * hidden. Rows follow pages added, hidden and retitled later, and either end
 * can go first without the other tripping on it. */

#include <config.h>

#include <stdlib.h>
#include <gtk/gtk.h>

#include <libnemo-private/nemo-tab-list.h>

#include "test-check.h"

static GtkWidget *
page (GtkWidget *stack, const char *title)
{
	GtkWidget *child = gtk_label_new (title);

	gtk_widget_show (child);
	gtk_stack_add_titled (GTK_STACK (stack), child, title, title);

	return child;
}

static GtkWidget *
sub_stack (GtkWidget *stack, const char *title)
{
	GtkWidget *child = gtk_stack_new ();

	gtk_widget_show (child);
	gtk_stack_add_titled (GTK_STACK (stack), child, title, title);

	return child;
}

static void
collect_rows (GtkWidget *widget, gpointer data)
{
	if (GTK_IS_LIST_BOX_ROW (widget)) {
		g_ptr_array_add (data, widget);
	}
	if (GTK_IS_CONTAINER (widget)) {
		gtk_container_forall (GTK_CONTAINER (widget), collect_rows, data);
	}
}

/* Rows as "title:indent", with a * on lit ones and - on hidden ones. */
static char *
describe (GtkWidget *list)
{
	GPtrArray *rows = g_ptr_array_new ();
	GString *out = g_string_new (NULL);
	int step = 0;
	guint i;

	collect_rows (list, rows);
	/* One step of indent is whatever the first indented row has. */
	for (i = 0; i < rows->len && step == 0; i++) {
		step = gtk_widget_get_margin_start (gtk_bin_get_child (GTK_BIN (rows->pdata[i])));
	}
	for (i = 0; i < rows->len; i++) {
		GtkWidget *row = rows->pdata[i];
		GtkWidget *label = gtk_bin_get_child (GTK_BIN (row));

		if (out->len > 0) {
			g_string_append_c (out, ' ');
		}
		g_string_append_printf (out, "%s%s%s:%d",
					gtk_widget_get_visible (row) ? "" : "-",
					(gtk_widget_get_state_flags (row) & GTK_STATE_FLAG_SELECTED) ? "*" : "",
					gtk_label_get_text (GTK_LABEL (label)),
					step > 0 ? gtk_widget_get_margin_start (label) / step : 0);
	}
	g_ptr_array_free (rows, TRUE);

	return g_string_free (out, FALSE);
}

static void
expect (GtkWidget *list, const char *wanted, int line)
{
	char *got = describe (list);

	if (g_strcmp0 (got, wanted) != 0) {
		g_printerr ("FAIL line %d:\n  got    %s\n  wanted %s\n", line, got, wanted);
		failures++;
	}
	g_free (got);
}

#define EXPECT(list, wanted) expect ((list), (wanted), __LINE__)

static GtkWidget *
row_for (GtkWidget *list, const char *title)
{
	GPtrArray *rows = g_ptr_array_new ();
	GtkWidget *found = NULL;
	guint i;

	collect_rows (list, rows);
	for (i = 0; i < rows->len && found == NULL; i++) {
		if (g_strcmp0 (gtk_label_get_text (GTK_LABEL (gtk_bin_get_child (rows->pdata[i]))), title) == 0) {
			found = rows->pdata[i];
		}
	}
	g_ptr_array_free (rows, TRUE);

	return found;
}

static void
click (GtkWidget *list, const char *title)
{
	GtkWidget *row = row_for (list, title);

	check (row != NULL);
	if (row != NULL) {
		g_signal_emit_by_name (gtk_widget_get_parent (row), "row-activated", row);
	}
}

int
main (int argc, char *argv[])
{
	GtkWidget *window, *box, *root, *list, *b, *b2, *b1, *b2a, *c, *inner, *d;
	GtkWidget *second, *second_list;

	if (!gtk_init_check (&argc, &argv)) {
		g_print ("SKIP: no display\n");
		return 77;
	}

	root = gtk_stack_new ();
	page (root, "A");
	b = sub_stack (root, "B");
	b1 = page (b, "B1");
	b2 = sub_stack (b, "B2");
	inner = gtk_button_new_with_label ("inside B2a");
	b2a = gtk_box_new (GTK_ORIENTATION_VERTICAL, 0);
	gtk_container_add (GTK_CONTAINER (b2a), inner);
	gtk_widget_show_all (b2a);
	gtk_stack_add_titled (GTK_STACK (b2), b2a, "B2a", "B2a");
	page (b2, "B2b");
	c = page (root, "C");

	list = nemo_tab_list_new ();
	nemo_tab_list_set_stack (NEMO_TAB_LIST (list), GTK_STACK (root));
	check (nemo_tab_list_get_stack (NEMO_TAB_LIST (list)) == GTK_STACK (root));

	box = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 0);
	gtk_container_add (GTK_CONTAINER (box), list);
	gtk_container_add (GTK_CONTAINER (box), root);
	window = gtk_window_new (GTK_WINDOW_TOPLEVEL);
	gtk_container_add (GTK_CONTAINER (window), box);
	gtk_widget_show_all (window);

	/* The theme draws it as GtkStackSidebar. */
	check (g_strcmp0 (gtk_widget_class_get_css_name (GTK_WIDGET_GET_CLASS (list)), "stacksidebar") == 0);

	/* Indent: 0, 1 or 2 steps. */
	EXPECT (list, "*A:0 B:0 B1:1 B2:1 B2a:2 B2b:2 C:0");

	/* A parent and its showing sub-tab are lit, at every depth. */
	click (list, "B2b");
	EXPECT (list, "A:0 *B:0 B1:1 *B2:1 B2a:2 *B2b:2 C:0");
	check (gtk_stack_get_visible_child (GTK_STACK (root)) == b);

	/* A parent opens on the sub-tab it last showed. */
	click (list, "C");
	EXPECT (list, "A:0 B:0 B1:1 B2:1 B2a:2 B2b:2 *C:0");
	click (list, "B");
	EXPECT (list, "A:0 *B:0 B1:1 *B2:1 B2a:2 *B2b:2 C:0");

	/* A change made to a stack from outside is shown too. */
	gtk_stack_set_visible_child (GTK_STACK (b), b1);
	EXPECT (list, "A:0 *B:0 *B1:1 B2:1 B2a:2 B2b:2 C:0");

	/* Stepping walks the pages with nothing under them, and wraps. */
	check (nemo_tab_list_step (NEMO_TAB_LIST (list), 1));
	EXPECT (list, "A:0 *B:0 B1:1 *B2:1 *B2a:2 B2b:2 C:0");
	nemo_tab_list_step (NEMO_TAB_LIST (list), 1);
	nemo_tab_list_step (NEMO_TAB_LIST (list), 1);
	EXPECT (list, "A:0 B:0 B1:1 B2:1 B2a:2 B2b:2 *C:0");
	nemo_tab_list_step (NEMO_TAB_LIST (list), 1);
	EXPECT (list, "*A:0 B:0 B1:1 B2:1 B2a:2 B2b:2 C:0");
	nemo_tab_list_step (NEMO_TAB_LIST (list), -1);
	EXPECT (list, "A:0 B:0 B1:1 B2:1 B2a:2 B2b:2 *C:0");
	nemo_tab_list_step (NEMO_TAB_LIST (list), -1);
	EXPECT (list, "A:0 *B:0 B1:1 *B2:1 B2a:2 *B2b:2 C:0");

	/* A hidden page loses its row, and its sub-tabs theirs, and stepping
	   passes it by. */
	gtk_widget_hide (b1);
	gtk_widget_hide (b2);
	EXPECT (list, "A:0 *B:0 -B1:1 -B2:1 -B2a:2 -B2b:2 C:0");
	nemo_tab_list_step (NEMO_TAB_LIST (list), 1);
	EXPECT (list, "A:0 B:0 -B1:1 -B2:1 -B2a:2 -B2b:2 *C:0");
	nemo_tab_list_step (NEMO_TAB_LIST (list), 1);
	nemo_tab_list_step (NEMO_TAB_LIST (list), 1);
	EXPECT (list, "A:0 *B:0 -B1:1 -B2:1 -B2a:2 -B2b:2 C:0");
	gtk_widget_show (b1);
	gtk_widget_show (b2);
	click (list, "B2a");
	EXPECT (list, "A:0 *B:0 B1:1 *B2:1 *B2a:2 B2b:2 C:0");

	/* The label for a widget is its innermost page's. */
	check (nemo_tab_list_get_label (NEMO_TAB_LIST (list), inner) ==
	       gtk_bin_get_child (GTK_BIN (row_for (list, "B2a"))));
	check (nemo_tab_list_get_label (NEMO_TAB_LIST (list), c) ==
	       gtk_bin_get_child (GTK_BIN (row_for (list, "C"))));
	check (nemo_tab_list_get_label (NEMO_TAB_LIST (list), window) == NULL);

	/* Retitled, added and removed later. */
	gtk_container_child_set (GTK_CONTAINER (root), c, "title", "See", NULL);
	EXPECT (list, "A:0 *B:0 B1:1 *B2:1 *B2a:2 B2b:2 See:0");
	d = page (b, "B3");
	EXPECT (list, "A:0 *B:0 B1:1 *B2:1 *B2a:2 B2b:2 B3:1 See:0");
	gtk_widget_destroy (d);
	EXPECT (list, "A:0 *B:0 B1:1 *B2:1 *B2a:2 B2b:2 See:0");

	/* Sub-tabs under a switch they share, from a stack marked as sub-tabs.
	   A stack inside a page that is not marked is the page's own. */
	{
		GtkWidget *root3 = gtk_stack_new ();
		GtkWidget *list3 = nemo_tab_list_new ();
		GtkWidget *y = gtk_box_new (GTK_ORIENTATION_VERTICAL, 0);
		GtkWidget *shared = gtk_check_button_new_with_label ("shared");
		GtkWidget *marked = gtk_stack_new ();
		GtkWidget *z = gtk_box_new (GTK_ORIENTATION_VERTICAL, 0);
		GtkWidget *unmarked = gtk_stack_new ();
		GtkWidget *y2;

		g_object_ref_sink (list3);
		g_object_ref_sink (root3);
		page (root3, "X");
		gtk_style_context_add_class (gtk_widget_get_style_context (marked), NEMO_TAB_LIST_SUB_TABS);
		page (marked, "Y1");
		y2 = page (marked, "Y2");
		gtk_container_add (GTK_CONTAINER (y), shared);
		gtk_container_add (GTK_CONTAINER (y), marked);
		gtk_widget_show_all (y);
		gtk_stack_add_titled (GTK_STACK (root3), y, "Y", "Y");
		page (unmarked, "Z1");
		page (unmarked, "Z2");
		gtk_container_add (GTK_CONTAINER (z), unmarked);
		gtk_widget_show_all (z);
		gtk_stack_add_titled (GTK_STACK (root3), z, "Z", "Z");

		nemo_tab_list_set_stack (NEMO_TAB_LIST (list3), GTK_STACK (root3));
		EXPECT (list3, "*X:0 Y:0 Y1:1 Y2:1 Z:0");
		nemo_tab_list_step (NEMO_TAB_LIST (list3), 1);
		EXPECT (list3, "X:0 *Y:0 *Y1:1 Y2:1 Z:0");
		nemo_tab_list_step (NEMO_TAB_LIST (list3), 1);
		EXPECT (list3, "X:0 *Y:0 Y1:1 *Y2:1 Z:0");
		nemo_tab_list_step (NEMO_TAB_LIST (list3), 1);
		EXPECT (list3, "X:0 Y:0 Y1:1 Y2:1 *Z:0");
		click (list3, "Y");
		EXPECT (list3, "X:0 *Y:0 Y1:1 *Y2:1 Z:0");
		check (nemo_tab_list_get_label (NEMO_TAB_LIST (list3), shared) ==
		       gtk_bin_get_child (GTK_BIN (row_for (list3, "Y"))));
		gtk_widget_hide (y2);
		EXPECT (list3, "X:0 *Y:0 *Y1:1 -Y2:1 Z:0");

		gtk_widget_destroy (list3);
		g_object_unref (list3);
		gtk_widget_destroy (root3);
		g_object_unref (root3);
	}

	/* Nowhere to go with a single page. */
	second = gtk_stack_new ();
	page (second, "Only");
	second_list = nemo_tab_list_new ();
	g_object_ref_sink (second_list);
	nemo_tab_list_set_stack (NEMO_TAB_LIST (second_list), GTK_STACK (second));
	check (!nemo_tab_list_step (NEMO_TAB_LIST (second_list), 1));

	/* The stack goes first, then the list, and the other way round. */
	g_object_ref_sink (second);
	gtk_widget_destroy (second);
	g_object_unref (second);
	check (nemo_tab_list_get_stack (NEMO_TAB_LIST (second_list)) == NULL);
	check (!nemo_tab_list_step (NEMO_TAB_LIST (second_list), 1));
	gtk_widget_destroy (second_list);
	g_object_unref (second_list);

	gtk_widget_destroy (list);
	gtk_stack_set_visible_child (GTK_STACK (root), c);
	gtk_widget_hide (b);
	page (root, "D");
	gtk_widget_destroy (window);

	if (failures > 0) {
		g_printerr ("%d check(s) failed\n", failures);
		return EXIT_FAILURE;
	}
	g_print ("PASS\n");
	return EXIT_SUCCESS;
}
