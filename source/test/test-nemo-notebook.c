/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */

/* test-nemo-notebook.c - the tab strip: when it shows, and how wide a tab is.

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

/* A real NemoNotebook with plain widgets for pages. One tab hides the strip
 * unless always-show-tabs says otherwise, and that has to hold after closing
 * down to one tab too, which once went by the tab count alone. Each tab is as
 * wide as its title, held between the two percentages of the strip, except
 * the one in front, which may go past the upper one. */

#include <config.h>

#include <gtk/gtk.h>

#include <libnemo-private/nemo-global-preferences.h>

#include "nemo-notebook.h"
#include "nemo-window.h"
#include "nemo-window-slot.h"
#include "nemo-window-slot-dnd.h"

#include "test-scratch.h"
#include "test-check.h"

/* The notebook reaches these only through tabs made from real slots, and the
   window's type check; neither comes up with plain pages. */
GType
nemo_window_get_type (void)
{
	return GTK_TYPE_WINDOW;
}

GType
nemo_window_slot_get_type (void)
{
	return GTK_TYPE_BOX;
}

void
nemo_drag_slot_proxy_init (G_GNUC_UNUSED GtkWidget *widget, G_GNUC_UNUSED NemoFile *target_file,
			   G_GNUC_UNUSED NemoWindowSlot *target_slot)
{
}

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

static GtkWidget *
tab_label (const char *text, GtkWidget **label_out)
{
	GtkWidget *hbox, *label;

	hbox = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 4);
	label = gtk_label_new (text);
	gtk_label_set_ellipsize (GTK_LABEL (label), PANGO_ELLIPSIZE_END);
	gtk_label_set_single_line_mode (GTK_LABEL (label), TRUE);
	gtk_box_pack_start (GTK_BOX (hbox), label, TRUE, TRUE, 0);
	g_object_set_data (G_OBJECT (hbox), "label", label);
	gtk_widget_show_all (hbox);
	if (label_out != NULL) {
		*label_out = label;
	}
	return hbox;
}

static GtkWidget *
add_page (GtkNotebook *notebook, const char *text, GtkWidget **label_out)
{
	GtkWidget *page = gtk_box_new (GTK_ORIENTATION_VERTICAL, 0);

	gtk_widget_show (page);
	gtk_notebook_append_page (notebook, page, tab_label (text, label_out));
	gtk_container_child_set (GTK_CONTAINER (notebook), page, "tab-expand", FALSE, NULL);
	return page;
}

static void
set_always_show (gboolean on)
{
	nemo_config_set_boolean (nemo_preferences, NEMO_PREFERENCES_ALWAYS_SHOW_TABS, on);
}

static void
test_visibility (void)
{
	GtkNotebook *notebook = GTK_NOTEBOOK (g_object_ref_sink (g_object_new (NEMO_TYPE_NOTEBOOK, NULL)));
	GtkWidget *first, *second;

	set_always_show (FALSE);
	first = add_page (notebook, "one", NULL);
	check (!gtk_notebook_get_show_tabs (notebook));

	/* The setting reaches a notebook already showing. */
	set_always_show (TRUE);
	check (gtk_notebook_get_show_tabs (notebook));
	set_always_show (FALSE);
	check (!gtk_notebook_get_show_tabs (notebook));

	second = add_page (notebook, "two", NULL);
	check (gtk_notebook_get_show_tabs (notebook));

	/* Two tabs always show, whatever the setting says. */
	set_always_show (FALSE);
	check (gtk_notebook_get_show_tabs (notebook));

	gtk_container_remove (GTK_CONTAINER (notebook), second);
	check (!gtk_notebook_get_show_tabs (notebook));

	/* Closing down to one tab keeps the strip when the setting asks for it. */
	set_always_show (TRUE);
	second = add_page (notebook, "two", NULL);
	check (gtk_notebook_get_show_tabs (notebook));
	gtk_container_remove (GTK_CONTAINER (notebook), second);
	check (gtk_notebook_get_show_tabs (notebook));

	gtk_container_remove (GTK_CONTAINER (notebook), first);
	set_always_show (FALSE);
	g_object_unref (notebook);
}

static int
text_width (GtkWidget *label)
{
	PangoLayout *layout;
	int width;

	layout = gtk_widget_create_pango_layout (label, gtk_label_get_text (GTK_LABEL (label)));
	pango_layout_get_pixel_size (layout, &width, NULL);
	g_object_unref (layout);
	return width;
}

static int
requested (GtkWidget *label)
{
	int width;

	gtk_widget_get_size_request (label, &width, NULL);
	return width;
}

static void
test_widths (void)
{
	GtkWidget *window, *notebook;
	GtkWidget *tiny, *medium, *long_back, *long_front;
	GtkAllocation allocation;
	GString *long_text;
	int min_px, max_px, medium_width, i;

	nemo_config_set_int (nemo_preferences, NEMO_PREFERENCES_TAB_WIDTH_MIN_PERCENT, 10);
	nemo_config_set_int (nemo_preferences, NEMO_PREFERENCES_TAB_WIDTH_MAX_PERCENT, 25);

	long_text = g_string_new (NULL);
	for (i = 0; i < 10; i++) {
		g_string_append (long_text, "folder ");
	}

	window = gtk_window_new (GTK_WINDOW_TOPLEVEL);
	gtk_window_set_default_size (GTK_WINDOW (window), 1200, 300);
	notebook = g_object_new (NEMO_TYPE_NOTEBOOK, NULL);
	gtk_container_add (GTK_CONTAINER (window), notebook);

	add_page (GTK_NOTEBOOK (notebook), "a", &tiny);
	add_page (GTK_NOTEBOOK (notebook), "Holiday photos 2024", &medium);
	add_page (GTK_NOTEBOOK (notebook), long_text->str, &long_back);
	add_page (GTK_NOTEBOOK (notebook), long_text->str, &long_front);
	gtk_notebook_set_current_page (GTK_NOTEBOOK (notebook), 3);

	gtk_widget_show_all (window);
	settle ();

	gtk_widget_get_allocation (notebook, &allocation);
	min_px = allocation.width * 10 / 100;
	max_px = allocation.width * 25 / 100;
	medium_width = text_width (medium);
	g_print ("strip %d px: min %d, max %d; tabs asked %d %d(%d) %d %d\n",
		 allocation.width, min_px, max_px, requested (tiny), requested (medium),
		 medium_width, requested (long_back), requested (long_front));

	/* The fixture only means something if the middle title falls between. */
	check (medium_width > min_px && medium_width < max_px);

	check (requested (tiny) == min_px);
	check (requested (medium) == medium_width);
	check (requested (long_back) == max_px);
	check (requested (long_front) > max_px);
	check (requested (long_front) == text_width (long_front));

	/* Bringing another tab to the front hands the exemption over. */
	gtk_notebook_set_current_page (GTK_NOTEBOOK (notebook), 2);
	settle ();
	check (requested (long_back) > max_px);
	check (requested (long_front) == max_px);

	/* A new upper limit reaches the tabs without a resize. */
	nemo_config_set_int (nemo_preferences, NEMO_PREFERENCES_TAB_WIDTH_MAX_PERCENT, 30);
	settle ();
	check (requested (long_front) == allocation.width * 30 / 100);

	nemo_config_reset (nemo_preferences, NEMO_PREFERENCES_TAB_WIDTH_MIN_PERCENT);
	nemo_config_reset (nemo_preferences, NEMO_PREFERENCES_TAB_WIDTH_MAX_PERCENT);
	gtk_widget_destroy (window);
	g_string_free (long_text, TRUE);
}

int
main (int argc, char *argv[])
{
	char *tmp;

	tmp = test_scratch_config_home ("nemo-notebook-test-XXXXXX");

	if (!gtk_init_check (&argc, &argv)) {
		g_print ("SKIP: no display\n");
		g_free (tmp);
		return 77;
	}

	nemo_global_preferences_init ();

	test_visibility ();
	test_widths ();

	g_free (tmp);

	if (failures > 0) {
		g_printerr ("%d check(s) failed\n", failures);
		return 1;
	}
	g_print ("PASS\n");
	return 0;
}
