/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */

/* test-nemo-places-drop-line.c - the line that shows where a dragged
   bookmark goes in "Places".

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

/* While a bookmark is dragged in Places, where it would go is drawn
 * as a line between 2 rows. Themes draw that line 1px wide in their own drop
 * color, which in some is nearly the background, so it couldn't be seen. The
 * app's sheet draws it instead. Each bundled theme, light and dark, is
 * loaded the way the app loads it, under the app's own sheet, and a Places
 * tree view is drawn with and without a drop spot. The rows that changed
 * have to make a line at least 2px thick, on the right edge of the right row,
 * with at least 3:1 contrast to what was there before. */

#include <config.h>

#include <math.h>
#include <string.h>

#include <gtk/gtk.h>

#include <libnemo-private/nemo-places-tree-view.h>

#include "test-check.h"

/* What the app loads its drop-in and bundled themes at. */
#define THEME_PRIORITY GTK_STYLE_PROVIDER_PRIORITY_SETTINGS

#define ROWS 4

typedef struct {
	const char *name;
	const char *sheet;
	gboolean    dark;
} Theme;

/* Adwaita is GTK's own, under every other theme as in the app. */
static const Theme themes[] = {
	{ "Adwaita",         NULL,                                FALSE },
	{ "Adwaita-dark",    NULL,                                TRUE  },
	{ "Fluent",          "Fluent/gtk-3.0/gtk.css",            FALSE },
	{ "Fluent-dark",     "Fluent/gtk-3.0/gtk-dark.css",       TRUE  },
	{ "Windows-10",      "Windows-10/gtk-3.0/gtk.css",        FALSE },
	{ "Windows-10-dark", "Windows-10-dark/gtk-3.0/gtk.css",   TRUE  },
	{ "Windows-7",       "Windows-7/gtk-3.0/gtk.css",         FALSE },
	{ "Windows-XP",      "Windows-XP/gtk-3.0/gtk.css",        FALSE },
	{ "Windows-XP-dark", "Windows-XP-dark/gtk-3.0/gtk.css",   TRUE  },
	{ "macOS",           "macOS/gtk-3.0/gtk.css",             FALSE },
	{ "macOS-dark",      "macOS-dark/gtk-3.0/gtk.css",        TRUE  },
};

static void
settle (void)
{
	int i;

	/* The tree view sizes its rows in idles. */
	for (i = 0; i < 20; i++) {
		while (gtk_events_pending ()) {
			gtk_main_iteration ();
		}
		g_usleep (5 * 1000);
	}
}

static cairo_surface_t *
render (GtkWidget *widget)
{
	cairo_surface_t *surface;
	cairo_t *cr;

	surface = cairo_image_surface_create (CAIRO_FORMAT_RGB24,
					      gtk_widget_get_allocated_width (widget),
					      gtk_widget_get_allocated_height (widget));
	cr = cairo_create (surface);
	/* The window's background goes underneath, as on screen. Some themes
	 * select in a see-through color, which over nothing is black. The view
	 * fills the window from its corner, so the two line up. */
	gtk_widget_draw (gtk_widget_get_toplevel (widget), cr);
	cairo_destroy (cr);
	cairo_surface_flush (surface);
	return surface;
}

static guint32
pixel (cairo_surface_t *surface, int x, int y)
{
	const guint8 *data = cairo_image_surface_get_data (surface);
	int stride = cairo_image_surface_get_stride (surface);

	return *(const guint32 *) (data + y * stride + x * 4) & 0xffffff;
}

static double
luminance (guint32 rgb)
{
	double c[3];
	int i;

	c[0] = ((rgb >> 16) & 0xff) / 255.0;
	c[1] = ((rgb >> 8) & 0xff) / 255.0;
	c[2] = (rgb & 0xff) / 255.0;
	for (i = 0; i < 3; i++) {
		c[i] = c[i] <= 0.04045 ? c[i] / 12.92 : pow ((c[i] + 0.055) / 1.055, 2.4);
	}
	return 0.2126 * c[0] + 0.7152 * c[1] + 0.0722 * c[2];
}

static double
contrast (guint32 a, guint32 b)
{
	double la = luminance (a), lb = luminance (b);

	return la > lb ? (la + 0.05) / (lb + 0.05) : (lb + 0.05) / (la + 0.05);
}

/* Draws the view with a drop spot on row, and checks what changed. */
static void
check_line (const char *theme, GtkWidget *view, cairo_surface_t *plain,
	    int row, GtkTreeViewDropPosition pos)
{
	GtkTreePath *path = gtk_tree_path_new_from_indices (row, -1);
	cairo_surface_t *marked;
	GdkRectangle area;
	int width = cairo_image_surface_get_width (plain);
	int height = cairo_image_surface_get_height (plain);
	int y, x, top, bottom, seen = 0, first = -1, last = -1;
	gboolean stray = FALSE;
	double weakest = 100.0;
	const char *where = pos == GTK_TREE_VIEW_DROP_BEFORE ? "before" : "after";

	gtk_tree_view_get_background_area (GTK_TREE_VIEW (view), path, NULL, &area);
	gtk_tree_view_convert_bin_window_to_widget_coords (GTK_TREE_VIEW (view),
							   area.x, area.y, &x, &top);
	bottom = top + area.height;

	gtk_tree_view_set_drag_dest_row (GTK_TREE_VIEW (view), path, pos);
	settle ();
	marked = render (view);
	gtk_tree_view_set_drag_dest_row (GTK_TREE_VIEW (view), NULL, GTK_TREE_VIEW_DROP_BEFORE);
	settle ();

	for (y = 0; y < height; y++) {
		int changed = 0;
		double strength;

		for (x = 0; x < width; x++) {
			changed += pixel (plain, x, y) != pixel (marked, x, y);
		}
		/* A line goes across; text or an icon redrawn doesn't. */
		if (changed < width / 2) {
			continue;
		}
		/* Right of any text, so the row's own background is underneath. */
		x = width - width / 8;
		strength = contrast (pixel (plain, x, y), pixel (marked, x, y));
		if (strength < 3.0) {
			continue;
		}
		if (y < top || y >= bottom) {
			stray = TRUE;
		}
		weakest = MIN (weakest, strength);
		if (first < 0) {
			first = y;
		}
		last = y;
		seen++;
	}

	g_print ("%-16s %s row %d (%d-%d): line %d-%d, %d px, contrast %.2f\n", theme, where,
		 row, top, bottom - 1, first, last, seen, seen > 0 ? weakest : 0.0);

	if (seen < 2) {
		g_printerr ("FAIL %s: no line %s row %d at least 2px thick with 3:1 contrast\n",
			    theme, where, row);
		failures++;
	} else if (stray || last - first + 1 != seen) {
		g_printerr ("FAIL %s: the line %s row %d isn't one band inside the row\n",
			    theme, where, row);
		failures++;
	} else if (pos == GTK_TREE_VIEW_DROP_BEFORE && first > top + 1) {
		g_printerr ("FAIL %s: the line before row %d isn't on its top edge\n", theme, row);
		failures++;
	} else if (pos == GTK_TREE_VIEW_DROP_AFTER && last < bottom - 2) {
		g_printerr ("FAIL %s: the line after row %d isn't on its bottom edge\n", theme, row);
		failures++;
	}

	cairo_surface_destroy (marked);
	gtk_tree_path_free (path);
}

static void
check_theme (const Theme *theme)
{
	GtkCssProvider *provider = NULL;
	GtkWidget *window, *view;
	GtkListStore *store;
	GtkTreeIter iter;
	cairo_surface_t *plain;
	int i;

	g_object_set (gtk_settings_get_default (),
		      "gtk-theme-name", "Adwaita",
		      "gtk-application-prefer-dark-theme", theme->dark,
		      NULL);
	if (theme->sheet != NULL) {
		char *path = g_build_filename (VENDOR_THEMES_DIR, theme->sheet, NULL);
		GError *error = NULL;

		provider = gtk_css_provider_new ();
		if (!gtk_css_provider_load_from_path (provider, path, &error)) {
			g_printerr ("FAIL %s: %s\n", theme->name, error->message);
			g_clear_error (&error);
			failures++;
			g_free (path);
			g_object_unref (provider);
			return;
		}
		g_free (path);
		gtk_style_context_add_provider_for_screen (gdk_screen_get_default (),
							   GTK_STYLE_PROVIDER (provider),
							   THEME_PRIORITY);
	}

	store = gtk_list_store_new (1, G_TYPE_STRING);
	for (i = 0; i < ROWS; i++) {
		gtk_list_store_insert_with_values (store, &iter, -1, 0, "Folder", -1);
	}
	view = nemo_places_tree_view_new ();
	gtk_tree_view_set_model (GTK_TREE_VIEW (view), GTK_TREE_MODEL (store));
	gtk_tree_view_set_headers_visible (GTK_TREE_VIEW (view), FALSE);
	gtk_tree_view_insert_column_with_attributes (GTK_TREE_VIEW (view), -1, NULL,
						     gtk_cell_renderer_text_new (),
						     "text", 0, NULL);
	g_object_unref (store);

	window = gtk_offscreen_window_new ();
	gtk_window_set_default_size (GTK_WINDOW (window), 240, 200);
	gtk_container_add (GTK_CONTAINER (window), view);
	gtk_widget_show_all (window);
	settle ();

	plain = render (view);
	check_line (theme->name, view, plain, 1, GTK_TREE_VIEW_DROP_BEFORE);
	check_line (theme->name, view, plain, 1, GTK_TREE_VIEW_DROP_AFTER);
	check_line (theme->name, view, plain, 0, GTK_TREE_VIEW_DROP_BEFORE);
	check_line (theme->name, view, plain, ROWS - 1, GTK_TREE_VIEW_DROP_AFTER);
	cairo_surface_destroy (plain);

	gtk_widget_destroy (window);
	if (provider != NULL) {
		gtk_style_context_remove_provider_for_screen (gdk_screen_get_default (),
							      GTK_STYLE_PROVIDER (provider));
		g_object_unref (provider);
	}
}

int
main (int argc, char *argv[])
{
	GtkCssProvider *app;
	guint i;

	if (!gtk_init_check (&argc, &argv)) {
		g_print ("SKIP: no display\n");
		return 77;
	}

	/* The app's own sheet, as the app adds it. */
	app = gtk_css_provider_new ();
	gtk_css_provider_load_from_resource (app, "/org/nemo/nemo-style-application.css");
	gtk_style_context_add_provider_for_screen (gdk_screen_get_default (),
						   GTK_STYLE_PROVIDER (app),
						   GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);

	for (i = 0; i < G_N_ELEMENTS (themes); i++) {
		check_theme (&themes[i]);
	}

	gtk_style_context_remove_provider_for_screen (gdk_screen_get_default (),
						      GTK_STYLE_PROVIDER (app));
	g_object_unref (app);

	if (failures > 0) {
		g_printerr ("%d check(s) failed\n", failures);
		return 1;
	}
	g_print ("PASS\n");
	return 0;
}
