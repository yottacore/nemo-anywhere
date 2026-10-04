/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */

/* test-nemo-icon-atk-probe.c - asks the icons' accessible objects where they
   are, from inside the program.

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

/* Preloaded into the real program by test-nemo-icon-atk. Once the window
 * shows $NEMO_ATK_FOLDER and its icons are placed, it asks every icon in
 * sight for its extents, on screen and in the window, and for its picture's
 * place and size. With $NEMO_ATK_SCROLL set it scrolls part way down first. One "ok" or "FAIL" line per check goes to $NEMO_ATK_OUT,
 * and "done" at the end.
 *
 * No symbol of the program is used. Where an icon really is comes from the
 * canvas item behind its accessible object. */

#include <stdio.h>
#include <stdlib.h>

#include <gtk/gtk.h>
#include <eel/eel-canvas.h>

/* Item bounds are doubles rounded two ways; allow for that. */
#define SLACK 1

static FILE       *out;
static const char *folder_name;
static gboolean    compact;
static gboolean    scroll;
static int         scrolled_at;
static GtkWidget  *window;
static GtkWidget  *pane;
static int         waited;
static int         complaints;
static GLogFunc    old_handler;
static gpointer    old_handler_data;

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
near (int got, int expected)
{
	return ABS (got - expected) <= SLACK;
}

static gboolean
same_rect (const GdkRectangle *got, const GdkRectangle *expected)
{
	return near (got->x, expected->x) && near (got->y, expected->y) &&
		near (got->width, expected->width) && near (got->height, expected->height);
}

static gboolean
inside (const GdkRectangle *inner, const GdkRectangle *outer)
{
	return inner->width > 0 && inner->height > 0 &&
		inner->x >= outer->x - SLACK && inner->y >= outer->y - SLACK &&
		inner->x + inner->width <= outer->x + outer->width + SLACK &&
		inner->y + inner->height <= outer->y + outer->height + SLACK;
}

/* In the icon view the picture sits centered at the top, with the label
   below. In the compact view it sits at the left, with the label beside. */
static gboolean
picture_placed (const GdkRectangle *image, const GdkRectangle *icon)
{
	if (compact) {
		return near (image->x, icon->x);
	}
	return near (image->y, icon->y) &&
		near (image->x + image->width / 2, icon->x + icon->width / 2);
}

static void
count_complaint (const char *domain, GLogLevelFlags level, const char *message, gpointer data)
{
	if (level & (G_LOG_LEVEL_CRITICAL | G_LOG_LEVEL_WARNING)) {
		complaints++;
	}
	old_handler (domain, level, message, old_handler_data);
}

static gboolean
find_pane (void)
{
	GtkWidget *view = find_widget ("NemoView");
	GtkWidget *child = view != NULL ? gtk_bin_get_child (GTK_BIN (view)) : NULL;

	if (child == NULL || !gtk_widget_get_mapped (child) || !is_a (child, "NemoIconContainer")) {
		return FALSE;
	}
	pane = child;
	return TRUE;
}

static void
note_first (char **line, const char *what, int i, const GdkRectangle *got,
	    const GdkRectangle *expected)
{
	if (*line == NULL) {
		*line = g_strdup_printf ("%s (icon %d: got %d,%d %dx%d, expected %d,%d %dx%d)",
					 what, i, got->x, got->y, got->width, got->height,
					 expected->x, expected->y, expected->width, expected->height);
	}
}

static void
check_icons (void)
{
	AtkObject *accessible = gtk_widget_get_accessible (pane);
	GtkWidget *toplevel = gtk_widget_get_toplevel (pane);
	GdkRectangle origin;
	int top_x, top_y;
	int scroll_x = (int) gtk_adjustment_get_value
		(gtk_scrollable_get_hadjustment (GTK_SCROLLABLE (pane)));
	int scroll_y = (int) gtk_adjustment_get_value
		(gtk_scrollable_get_vadjustment (GTK_SCROLLABLE (pane)));
	char *screen_bad = NULL, *window_bad = NULL, *image_bad = NULL, *line;
	int i, checked = 0;

	screen_rect (pane, &origin);
	gdk_window_get_origin (gtk_widget_get_window (toplevel), &top_x, &top_y);

	complaints = 0;
	old_handler = g_log_set_default_handler (count_complaint, NULL);

	for (i = 0; i < atk_object_get_n_accessible_children (accessible); i++) {
		AtkObject *child = atk_object_ref_accessible_child (accessible, i);
		GObject *object = ATK_IS_GOBJECT_ACCESSIBLE (child) ?
			atk_gobject_accessible_get_object (ATK_GOBJECT_ACCESSIBLE (child)) : NULL;
		EelCanvasItem *item = object != NULL &&
			g_type_is_a (G_OBJECT_TYPE (object), g_type_from_name ("EelCanvasItem")) ?
			(EelCanvasItem *) object : NULL;
		GdkRectangle expected, got = { -1, -1, -1, -1 }, image = { -1, -1, -1, -1 };

		/* Only an icon wholly in sight; one scrolled away has no place. */
		if (item == NULL ||
		    item->x1 - scroll_x < 0 || item->x2 - scroll_x > origin.width ||
		    item->y1 - scroll_y < 0 || item->y2 - scroll_y > origin.height) {
			g_object_unref (child);
			continue;
		}
		checked++;
		expected.x = origin.x + (int) (item->x1 + .5) - scroll_x;
		expected.y = origin.y + (int) (item->y1 + .5) - scroll_y;
		expected.width = (int) (item->x2 + .5) - (int) (item->x1 + .5);
		expected.height = (int) (item->y2 + .5) - (int) (item->y1 + .5);

		atk_component_get_extents (ATK_COMPONENT (child), &got.x, &got.y,
					   &got.width, &got.height, ATK_XY_SCREEN);
		if (!same_rect (&got, &expected)) {
			note_first (&screen_bad, "extents on screen", i, &got, &expected);
		}

		expected.x -= top_x;
		expected.y -= top_y;
		got.x = got.y = got.width = got.height = -1;
		atk_component_get_extents (ATK_COMPONENT (child), &got.x, &got.y,
					   &got.width, &got.height, ATK_XY_WINDOW);
		if (!same_rect (&got, &expected)) {
			note_first (&window_bad, "extents in the window", i, &got, &expected);
		}

		got.x = got.y = got.width = got.height = -1;
		atk_component_get_extents (ATK_COMPONENT (child), &got.x, &got.y,
					   &got.width, &got.height, ATK_XY_SCREEN);
		atk_image_get_image_position (ATK_IMAGE (child), &image.x, &image.y, ATK_XY_SCREEN);
		atk_image_get_image_size (ATK_IMAGE (child), &image.width, &image.height);
		if (!inside (&image, &got) || !picture_placed (&image, &got)) {
			note_first (&image_bad, "picture inside the icon", i, &image, &got);
		}

		g_object_unref (child);
	}

	/* The first icon is well above the view once scrolled. */
	if (scroll) {
		AtkObject *child = atk_object_ref_accessible_child (accessible, 0);
		GdkRectangle got = { -1, -1, -1, -1 }, image = { -1, -1, -1, -1 };

		atk_component_get_extents (ATK_COMPONENT (child), &got.x, &got.y,
					   &got.width, &got.height, ATK_XY_SCREEN);
		atk_image_get_image_position (ATK_IMAGE (child), &image.x, &image.y, ATK_XY_SCREEN);
		line = g_strdup_printf ("an icon out of sight has no place (extents %d,%d, picture %d,%d)",
					got.x, got.y, image.x, image.y);
		report (got.x == G_MININT && got.y == G_MININT &&
			image.x == G_MININT && image.y == G_MININT, line);
		g_free (line);
		g_object_unref (child);
	}

	g_log_set_default_handler (old_handler, old_handler_data);

	line = g_strdup_printf ("icons in sight to check (%d, scrolled %d)", checked, scroll_y);
	report (checked >= 4 && (!scroll || scroll_y > 0), line);
	g_free (line);
	report (screen_bad == NULL, screen_bad != NULL ? screen_bad :
		"each icon's extents on screen are where it is drawn");
	report (window_bad == NULL, window_bad != NULL ? window_bad :
		"each icon's extents in the window are where it is drawn");
	report (image_bad == NULL, image_bad != NULL ? image_bad :
		"each icon's picture is where it is drawn");
	line = g_strdup_printf ("no warning or critical while asking (%d)", complaints);
	report (complaints == 0, line);
	g_free (line);

	g_free (screen_bad);
	g_free (window_bad);
	g_free (image_bad);
}

/* Up to ten seconds for the window and its icons. */
static gboolean
tick (gpointer data)
{
	GList *toplevels, *l;
	const char *title;

	if (++waited > 100) {
		report (FALSE, "the folder showed its icons in time");
		fprintf (out, "done\n");
		fflush (out);
		return G_SOURCE_REMOVE;
	}

	toplevels = gtk_window_list_toplevels ();
	for (l = toplevels; l != NULL; l = l->next) {
		if (is_a (l->data, "NemoWindow") && gtk_widget_get_mapped (l->data)) {
			window = l->data;
		}
	}
	g_list_free (toplevels);
	title = window != NULL ? gtk_window_get_title (GTK_WINDOW (window)) : NULL;
	if (title == NULL || !g_str_has_prefix (title, folder_name)) {
		return G_SOURCE_CONTINUE;
	}
	/* Icons are placed a moment after they are listed, so give it a second. */
	if (!find_pane () ||
	    atk_object_get_n_accessible_children (gtk_widget_get_accessible (pane)) < 12 ||
	    waited < 10) {
		return G_SOURCE_CONTINUE;
	}
	/* Scrolled part way down, so an icon's place has to allow for it. */
	if (scroll && scrolled_at == 0) {
		GtkAdjustment *adjustment = gtk_scrollable_get_vadjustment (GTK_SCROLLABLE (pane));

		gtk_adjustment_set_value (adjustment, (gtk_adjustment_get_upper (adjustment) -
						       gtk_adjustment_get_page_size (adjustment)) / 2);
		scrolled_at = waited;
		return G_SOURCE_CONTINUE;
	}
	if (scroll && waited < scrolled_at + 5) {
		return G_SOURCE_CONTINUE;
	}

	check_icons ();
	fprintf (out, "done\n");
	fflush (out);
	return G_SOURCE_REMOVE;
}

__attribute__((constructor)) static void
probe_start (void)
{
	const char *path = g_getenv ("NEMO_ATK_OUT");

	folder_name = g_getenv ("NEMO_ATK_FOLDER");
	compact = g_strcmp0 (g_getenv ("NEMO_ATK_VIEW"), "compact-view") == 0;
	scroll = g_getenv ("NEMO_ATK_SCROLL") != NULL;
	if (path == NULL || folder_name == NULL) {
		return;
	}
	out = fopen (path, "w");
	if (out == NULL) {
		return;
	}
	g_timeout_add (100, tick, NULL);
}
