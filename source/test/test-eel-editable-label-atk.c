/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */

/* test-eel-editable-label-atk.c - the rename field's accessible object.

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

/* The rename field in the icon views is an EelEditableLabel. Its accessible
 * object was built on the do-nothing object, so every text call failed a
 * type check and gave nothing back, and it had no place on screen. It has to
 * give its name and text, take an edit, and say where the field and each
 * character are, with no warning or critical along the way. */

#include <config.h>

#include <stdlib.h>
#include <string.h>
#include <gtk/gtk.h>
#include <eel/eel-editable-label.h>

#include "test-check.h"
#include "test-scratch.h"

#define FIELD_X 40
#define FIELD_Y 30

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

static void
screen_rect (GtkWidget *widget, GdkRectangle *rect)
{
	GtkWidget *toplevel = gtk_widget_get_toplevel (widget);
	int x = 0, y = 0;

	gdk_window_get_origin (gtk_widget_get_window (toplevel), &rect->x, &rect->y);
	gtk_widget_translate_coordinates (widget, toplevel, 0, 0, &x, &y);
	rect->x += x;
	rect->y += y;
	rect->width = gtk_widget_get_allocated_width (widget);
	rect->height = gtk_widget_get_allocated_height (widget);
}

static gboolean
text_is (AtkObject *accessible, const char *want)
{
	char *got = atk_text_get_text (ATK_TEXT (accessible), 0, -1);
	gboolean same = g_strcmp0 (got, want) == 0;

	if (!same) {
		g_printerr ("  text \"%s\", wanted \"%s\"\n", got != NULL ? got : "(null)", want);
	}
	g_free (got);
	return same;
}

static void
check_place (AtkObject *accessible, GtkWidget *field)
{
	GdkRectangle want, got = { -1, -1, -1, -1 }, glyph = { -1, -1, -1, -1 };
	int top_x, top_y;

	screen_rect (field, &want);
	atk_component_get_extents (ATK_COMPONENT (accessible), &got.x, &got.y,
				   &got.width, &got.height, ATK_XY_SCREEN);
	if (got.x != want.x || got.y != want.y ||
	    got.width != want.width || got.height != want.height) {
		g_printerr ("FAIL field on screen: got %d,%d %dx%d, wanted %d,%d %dx%d\n",
			    got.x, got.y, got.width, got.height,
			    want.x, want.y, want.width, want.height);
		failures++;
	}

	gdk_window_get_origin (gtk_widget_get_window (gtk_widget_get_toplevel (field)),
			       &top_x, &top_y);
	got.x = got.y = got.width = got.height = -1;
	atk_component_get_extents (ATK_COMPONENT (accessible), &got.x, &got.y,
				   &got.width, &got.height, ATK_XY_WINDOW);
	if (got.x != want.x - top_x || got.y != want.y - top_y) {
		g_printerr ("FAIL field in the window: got %d,%d, wanted %d,%d\n",
			    got.x, got.y, want.x - top_x, want.y - top_y);
		failures++;
	}

	/* The first character sits inside the field, and asking at its middle
	   finds it again. */
	atk_text_get_character_extents (ATK_TEXT (accessible), 0, &glyph.x, &glyph.y,
					&glyph.width, &glyph.height, ATK_XY_SCREEN);
	if (glyph.width <= 0 || glyph.height <= 0 ||
	    glyph.x < want.x || glyph.y < want.y ||
	    glyph.x + glyph.width > want.x + want.width ||
	    glyph.y + glyph.height > want.y + want.height) {
		g_printerr ("FAIL first character: got %d,%d %dx%d, field %d,%d %dx%d\n",
			    glyph.x, glyph.y, glyph.width, glyph.height,
			    want.x, want.y, want.width, want.height);
		failures++;
	} else {
		check (atk_text_get_offset_at_point (ATK_TEXT (accessible),
						     glyph.x + glyph.width / 2,
						     glyph.y + glyph.height / 2,
						     ATK_XY_SCREEN) == 0);
	}
}

int
main (int argc, char *argv[])
{
	GtkWidget *window, *fixed, *field;
	AtkObject *accessible;
	AtkStateSet *states;

	test_own_display (argc, argv, NULL);

	if (!gtk_init_check (&argc, &argv)) {
		g_print ("SKIP: no display\n");
		return 77;
	}
	if (gdk_display_get_n_monitors (gdk_display_get_default ()) == 0) {
		g_print ("SKIP: no monitor\n");
		return 77;
	}

	window = gtk_window_new (GTK_WINDOW_TOPLEVEL);
	gtk_window_set_default_size (GTK_WINDOW (window), 400, 200);
	gtk_window_move (GTK_WINDOW (window), 100, 100);
	fixed = gtk_fixed_new ();
	gtk_container_add (GTK_CONTAINER (window), fixed);
	gtk_widget_show_all (window);
	settle ();
	/* Into a window already on screen, as a rename puts it. */
	field = eel_editable_label_new ("photo.jpg");
	gtk_fixed_put (GTK_FIXED (fixed), field, FIELD_X, FIELD_Y);
	gtk_widget_show (field);
	settle ();

	complaints = 0;
	old_handler = g_log_set_default_handler (count_complaint, NULL);

	accessible = gtk_widget_get_accessible (field);
	check (ATK_IS_TEXT (accessible));
	check (ATK_IS_EDITABLE_TEXT (accessible));
	check (ATK_IS_COMPONENT (accessible));
	if (failures > 0) {
		g_printerr ("%d failure(s)\n", failures);
		return EXIT_FAILURE;
	}
	check (GTK_IS_ACCESSIBLE (accessible) &&
	       gtk_accessible_get_widget (GTK_ACCESSIBLE (accessible)) == field);
	check (atk_object_get_role (accessible) == ATK_ROLE_TEXT);
	check (g_strcmp0 (atk_object_get_name (accessible), "photo.jpg") == 0);

	states = atk_object_ref_state_set (accessible);
	check (atk_state_set_contains_state (states, ATK_STATE_EDITABLE));
	check (atk_state_set_contains_state (states, ATK_STATE_SHOWING));
	g_object_unref (states);

	check (text_is (accessible, "photo.jpg"));
	check (atk_text_get_character_count (ATK_TEXT (accessible)) == 9);
	check (atk_text_get_character_at_offset (ATK_TEXT (accessible), 5) == '.');

	check_place (accessible, field);

	/* An edit through the accessible object reaches the field, and the
	   text it then reports follows. */
	atk_editable_text_set_text_contents (ATK_EDITABLE_TEXT (accessible), "other.png");
	settle ();
	check (strcmp (eel_editable_label_get_text (EEL_EDITABLE_LABEL (field)), "other.png") == 0);
	check (text_is (accessible, "other.png"));

	g_log_set_default_handler (old_handler, NULL);
	if (complaints > 0) {
		g_printerr ("FAIL %d warning(s) or critical(s) while asking\n", complaints);
		failures++;
	}

	gtk_widget_destroy (window);

	if (failures > 0) {
		g_printerr ("%d failure(s)\n", failures);
		return EXIT_FAILURE;
	}
	g_print ("OK\n");
	return EXIT_SUCCESS;
}
