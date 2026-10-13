/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */

/* test-eel-editable-label-rewidth.c - the rename field takes a new width
   while it is hidden.

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

/* The icon view keeps one rename field and hides it between renames. Each
 * rename gives it the width of a name under an icon, which follows the icon
 * size, then the name, then shows it. GTK does not re-ask a hidden widget for
 * its size, and the same name again changes nothing, so a second rename of a
 * file after a zoom got the field at its old width. The name, centered in the
 * new width, sat partly or wholly outside it, out of sight. A folder of
 * pictures grows its icons by itself once it has loaded, so there it took
 * only a rename cut short by that and a second F2.
 *
 * The field sits in a GtkLayout, as in the icon view, and goes through the
 * same steps, growing and then shrinking. */

#include <config.h>

#include <stdlib.h>
#include <gtk/gtk.h>
#include <eel/eel-editable-label.h>

#include "test-check.h"

#define PAD 1

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

/* What the icon view does on F2, less the placing. */
static void
start_rename (GtkWidget *field, int width, const char *name)
{
	PangoContext *context = gtk_widget_get_pango_context (field);
	PangoFontDescription *desc = pango_font_description_copy (pango_context_get_font_description (context));

	eel_editable_label_set_font_description (EEL_EDITABLE_LABEL (field), desc);
	pango_font_description_free (desc);
	gtk_widget_set_size_request (field, width, -1);
	eel_editable_label_set_text (EEL_EDITABLE_LABEL (field), name);
	gtk_widget_show (field);
	settle ();
}

static void
check_width (GtkWidget *field, int width, const char *when)
{
	PangoLayout *layout = eel_editable_label_get_layout (EEL_EDITABLE_LABEL (field));
	PangoRectangle ink;
	int got = gtk_widget_get_allocated_width (field);
	int x;

	if (got != width + 2 * PAD) {
		g_printerr ("FAIL %s: field %d wide, asked for %d\n", when, got, width + 2 * PAD);
		failures++;
	}

	/* The name has to be inside the field to be seen. */
	pango_layout_get_pixel_extents (layout, &ink, NULL);
	eel_editable_label_get_layout_offsets (EEL_EDITABLE_LABEL (field), &x, NULL);
	x += ink.x;
	if (x < 0 || x + ink.width > got) {
		g_printerr ("FAIL %s: name at %d to %d, field is %d wide\n",
			    when, x, x + ink.width, got);
		failures++;
	}
}

int
main (int argc, char *argv[])
{
	GtkWidget *window, *layout, *field;

	if (!gtk_init_check (&argc, &argv)) {
		g_print ("SKIP: no display\n");
		return 77;
	}
	if (gdk_display_get_n_monitors (gdk_display_get_default ()) == 0) {
		g_print ("SKIP: no monitor\n");
		return 77;
	}

	window = gtk_window_new (GTK_WINDOW_TOPLEVEL);
	gtk_window_set_default_size (GTK_WINDOW (window), 900, 300);
	layout = gtk_layout_new (NULL, NULL);
	gtk_container_add (GTK_CONTAINER (window), layout);
	gtk_widget_show_all (window);

	field = eel_editable_label_new ("Test text");
	eel_editable_label_set_line_wrap (EEL_EDITABLE_LABEL (field), TRUE);
	eel_editable_label_set_line_wrap_mode (EEL_EDITABLE_LABEL (field), PANGO_WRAP_WORD_CHAR);
	eel_editable_label_set_draw_outline (EEL_EDITABLE_LABEL (field), TRUE);
	eel_editable_label_set_justify (EEL_EDITABLE_LABEL (field), GTK_JUSTIFY_CENTER);
	gtk_misc_set_padding (GTK_MISC (field), PAD, PAD);
	gtk_layout_put (GTK_LAYOUT (layout), field, 0, 0);
	settle ();

	/* A plain folder first: the floor width under a small icon. */
	start_rename (field, 110, "photo.jpg");
	check_width (field, 110, "first rename");
	gtk_widget_hide (field);
	settle ();

	/* Same file, bigger icons. */
	start_rename (field, 640, "photo.jpg");
	check_width (field, 640, "same name, wider");
	gtk_widget_hide (field);
	settle ();

	/* And back down. */
	start_rename (field, 320, "photo.jpg");
	check_width (field, 320, "same name, narrower");
	gtk_widget_hide (field);
	settle ();

	/* Another name was always fine; keep it that way. */
	start_rename (field, 448, "another photo.jpg");
	check_width (field, 448, "another name");

	gtk_widget_destroy (window);

	if (failures > 0) {
		g_printerr ("%d failure(s)\n", failures);
		return EXIT_FAILURE;
	}
	g_print ("OK\n");
	return EXIT_SUCCESS;
}
