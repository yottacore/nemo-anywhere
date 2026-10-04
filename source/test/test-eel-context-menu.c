/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */

/* test-eel-context-menu.c - where a menu asked for from the keyboard opens.

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

/* The menu key and Ctrl+F10 have no pointer behind them. The menu once opened
 * at the pointer anyway, which can be anywhere, and read as the key doing
 * nothing. With no event it has to open against the focused widget, or
 * against the view itself when the focus is elsewhere. Given the item the menu
 * is for, it opens just below that item, unless the item is out of sight. */

#include <config.h>

#include <stdlib.h>
#include <gtk/gtk.h>
#include <eel/eel-gtk-extensions.h>

#include "test-check.h"
#include "test-scratch.h"

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
new_menu (void)
{
	GtkWidget *menu = gtk_menu_new ();
	GtkWidget *item = gtk_menu_item_new_with_label ("Open");

	gtk_widget_show (item);
	gtk_menu_shell_append (GTK_MENU_SHELL (menu), item);
	return menu;
}

/* Screen position of a widget's top left corner. */
static void
screen_origin (GtkWidget *widget, int *x, int *y)
{
	GtkWidget *toplevel = gtk_widget_get_toplevel (widget);
	int wx = 0, wy = 0;

	gdk_window_get_origin (gtk_widget_get_window (toplevel), x, y);
	gtk_widget_translate_coordinates (widget, toplevel, 0, 0, &wx, &wy);
	*x += wx;
	*y += wy;
}

/* Pops the menu with no event and reports where its window opened. */
static gboolean
pop_up_at_item (GtkWidget *view, const GdkRectangle *item, int *x, int *y)
{
	GtkWidget *menu = new_menu ();
	GtkWidget *menu_window;
	gboolean shown;

	g_object_ref_sink (menu);
	if (item != NULL) {
		eel_pop_up_context_menu_at_item (GTK_MENU (menu), NULL, view, item);
	} else {
		eel_pop_up_context_menu (GTK_MENU (menu), NULL, view);
	}
	settle ();

	menu_window = gtk_widget_get_toplevel (menu);
	shown = gtk_widget_get_mapped (menu);
	if (shown) {
		gdk_window_get_origin (gtk_widget_get_window (menu_window), x, y);
	}
	gtk_menu_popdown (GTK_MENU (menu));
	settle ();
	gtk_widget_destroy (menu);
	g_object_unref (menu);

	return shown;
}

static gboolean
pop_up (GtkWidget *view, int *x, int *y)
{
	return pop_up_at_item (view, NULL, x, y);
}

static gboolean
near (int got, int want)
{
	return ABS (got - want) <= 20;
}

int
main (int argc, char *argv[])
{
	GtkWidget *window, *fixed, *view, *focused;
	GdkDevice *pointer;
	int x = 0, y = 0, want_x, want_y;

	/* The pointer is moved, and a menu open in another test holds it. */
	test_own_display (argc, argv, NULL);

	if (!gtk_init_check (&argc, &argv)) {
		g_print ("SKIP: no display\n");
		return 77;
	}
	/* A session with no monitor, such as a service session on Windows, has
	   nothing to place a menu or size a dialog against. */
	if (gdk_display_get_n_monitors (gdk_display_get_default ()) == 0) {
		g_print ("SKIP: no monitor\n");
		return 77;
	}

	window = gtk_window_new (GTK_WINDOW_TOPLEVEL);
	gtk_window_set_default_size (GTK_WINDOW (window), 600, 400);
	gtk_window_move (GTK_WINDOW (window), 100, 100);
	fixed = gtk_fixed_new ();
	gtk_container_add (GTK_CONTAINER (window), fixed);

	/* The view the menu belongs to, and a widget far from it holding the focus. */
	view = gtk_button_new_with_label ("view");
	gtk_widget_set_size_request (view, 200, 100);
	gtk_fixed_put (GTK_FIXED (fixed), view, 20, 20);
	focused = gtk_button_new_with_label ("focused");
	gtk_widget_set_size_request (focused, 120, 60);
	gtk_fixed_put (GTK_FIXED (fixed), focused, 400, 300);

	gtk_widget_show_all (window);
	settle ();

	/* The pointer as far from both as the screen allows. */
	pointer = gdk_seat_get_pointer (gdk_display_get_default_seat (gdk_display_get_default ()));
	gdk_device_warp (pointer, gdk_screen_get_default (), 1, 1);
	settle ();

	gtk_widget_grab_focus (focused);
	settle ();
	check (pop_up (view, &x, &y));
	screen_origin (focused, &want_x, &want_y);
	g_print ("focused at %d,%d: menu at %d,%d\n", want_x, want_y, x, y);
	check (near (x, want_x + 16) && near (y, want_y + 16));

	/* No focus in the window: the view itself. */
	gtk_window_set_focus (GTK_WINDOW (window), NULL);
	settle ();
	check (pop_up (view, &x, &y));
	screen_origin (view, &want_x, &want_y);
	g_print ("view at %d,%d: menu at %d,%d\n", want_x, want_y, x, y);
	check (near (x, want_x + 16) && near (y, want_y + 16));

	/* An item in sight: just below it, at its left edge. */
	{
		GdkRectangle item = { 40, 30, 80, 20 };

		check (pop_up_at_item (view, &item, &x, &y));
		screen_origin (view, &want_x, &want_y);
		g_print ("item at %d,%d: menu at %d,%d\n", want_x + 40, want_y + 50, x, y);
		check (near (x, want_x + 40) && near (y, want_y + 50));
	}

	/* An item scrolled out of sight: back to the top left, as with none. */
	{
		GdkRectangle item = { 40, 500, 80, 20 };

		check (pop_up_at_item (view, &item, &x, &y));
		screen_origin (view, &want_x, &want_y);
		g_print ("item out of sight: menu at %d,%d\n", x, y);
		check (near (x, want_x + 16) && near (y, want_y + 16));
	}

	gtk_widget_destroy (window);

	if (failures > 0) {
		g_printerr ("%d check(s) failed\n", failures);
		return EXIT_FAILURE;
	}
	g_print ("PASS\n");
	return EXIT_SUCCESS;
}
