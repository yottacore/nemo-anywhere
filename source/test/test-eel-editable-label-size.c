/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */

/* test-eel-editable-label-size.c - the rename field sized before it is shown.

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

/* EelEditableLabel has a second window over its text, made on realize. Its
 * size code moved that window whether or not it had been made, so a field
 * given its size before it was shown logged a critical. A window is sized
 * before it is realized, so any field put in one before it is shown did it.
 * The text window has to take the field's place when it is made, and follow
 * it after. */

#include <config.h>

#include <stdlib.h>
#include <gtk/gtk.h>
#include <eel/eel-editable-label.h>

#include "test-check.h"

static int complaints;
static GLogFunc old_handler;

static void
count_complaint (const char *domain, GLogLevelFlags level, const char *message, gpointer data)
{
	if (level & (G_LOG_LEVEL_CRITICAL | G_LOG_LEVEL_WARNING)) {
		complaints++;
	}
	old_handler (domain, level, message, data);
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

/* The text window is the field's other window under the same parent. */
static GdkWindow *
text_window (GtkWidget *field)
{
	GdkWindow *found = NULL;
	GList *children, *l;

	children = gdk_window_get_children (gtk_widget_get_parent_window (field));
	for (l = children; l != NULL; l = l->next) {
		GdkWindow *child = l->data;
		gpointer owner = NULL;

		gdk_window_get_user_data (child, &owner);
		if (owner == field && child != gtk_widget_get_window (field)) {
			found = child;
		}
	}
	g_list_free (children);
	return found;
}

static void
check_text_window (GtkWidget *field, const char *when)
{
	GtkAllocation want;
	GdkWindow *text;
	int x, y, width, height;

	gtk_widget_get_allocation (field, &want);
	text = text_window (field);
	if (text == NULL) {
		g_printerr ("FAIL %s: no text window\n", when);
		failures++;
		return;
	}
	gdk_window_get_geometry (text, &x, &y, &width, &height);
	if (x != want.x || y != want.y || width != want.width || height != want.height) {
		g_printerr ("FAIL %s: text window %d,%d %dx%d, field %d,%d %dx%d\n",
			    when, x, y, width, height,
			    want.x, want.y, want.width, want.height);
		failures++;
	}
}

int
main (int argc, char *argv[])
{
	GtkWidget *window, *fixed, *field, *loose;
	GtkAllocation place = { 10, 10, 120, 30 };

	if (!gtk_init_check (&argc, &argv)) {
		g_print ("SKIP: no display\n");
		return 77;
	}
	if (gdk_display_get_n_monitors (gdk_display_get_default ()) == 0) {
		g_print ("SKIP: no monitor\n");
		return 77;
	}

	complaints = 0;
	old_handler = g_log_set_default_handler (count_complaint, NULL);

	/* Sized with no window to go in. GTK skips a hidden widget. */
	loose = g_object_ref_sink (eel_editable_label_new ("loose.txt"));
	gtk_widget_show (loose);
	gtk_widget_get_preferred_size (loose, NULL, NULL);
	gtk_widget_size_allocate (loose, &place);
	check (!gtk_widget_get_realized (loose));

	/* Put in a window before it is shown, the way the window sizes it
	   first and realizes it after. */
	window = gtk_window_new (GTK_WINDOW_TOPLEVEL);
	gtk_window_set_default_size (GTK_WINDOW (window), 400, 200);
	fixed = gtk_fixed_new ();
	gtk_container_add (GTK_CONTAINER (window), fixed);
	field = eel_editable_label_new ("photo.jpg");
	gtk_fixed_put (GTK_FIXED (fixed), field, 40, 30);
	gtk_widget_show_all (window);
	settle ();
	check (gtk_widget_get_realized (field));
	check_text_window (field, "when shown");

	/* Moved and resized after, the text window follows. */
	gtk_widget_set_size_request (field, 200, 60);
	gtk_fixed_move (GTK_FIXED (fixed), field, 70, 50);
	settle ();
	check (gtk_widget_get_allocated_width (field) == 200);
	check_text_window (field, "after a move");

	/* Hidden, unrealized and shown again goes through the same order. */
	gtk_widget_hide (window);
	gtk_widget_unrealize (window);
	check (!gtk_widget_get_realized (field));
	gtk_widget_show_all (window);
	settle ();
	check_text_window (field, "shown again");

	g_log_set_default_handler (old_handler, NULL);
	if (complaints > 0) {
		g_printerr ("FAIL %d warning(s) or critical(s)\n", complaints);
		failures++;
	}

	gtk_widget_destroy (window);
	g_object_unref (loose);

	if (failures > 0) {
		g_printerr ("%d failure(s)\n", failures);
		return EXIT_FAILURE;
	}
	g_print ("OK\n");
	return EXIT_SUCCESS;
}
