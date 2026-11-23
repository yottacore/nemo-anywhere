/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */

/* test-nemo-zoom-window-probe.c - zooms with the keyboard from inside the
   program and watches the window size.

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

/* Preloaded into the real program by test-nemo-zoom-window. Once the folder
 * shows, it presses Ctrl+plus through the window's own shortcuts, then
 * Ctrl+minus. With $NEMO_ZOOMWIN_EXPECT at "grow" the window has to get
 * bigger and then come back to where it was. At "stay" it has to keep its
 * size. One "ok" or "FAIL" line per check goes to $NEMO_ZOOMWIN_OUT, and
 * "done" at the end. */

#include <stdio.h>
#include <string.h>

#include <gtk/gtk.h>
#include <gdk/gdkkeysyms.h>

static FILE       *out;
static gboolean    expect_grow;
static GtkWidget  *window;
static int         step;
static int         waited;
static int         start_width, start_height;
static int         big_width, big_height;

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

static void
find_view_cb (GtkWidget *widget, gpointer data)
{
	GtkWidget **found = data;

	if (*found != NULL) {
		return;
	}
	if (is_a (widget, "NemoView") && gtk_widget_get_mapped (widget)) {
		*found = widget;
	} else if (GTK_IS_CONTAINER (widget)) {
		gtk_container_forall (GTK_CONTAINER (widget), find_view_cb, data);
	}
}

static void
finish (void)
{
	fprintf (out, "done\n");
	fflush (out);
}

static void
press (guint key)
{
	gtk_accel_groups_activate (G_OBJECT (window), key, GDK_CONTROL_MASK);
}

/* Each step waits for what the one before set going. Up to 10 seconds each,
   and a window that should keep its size is watched for 3. */
static gboolean
tick (G_GNUC_UNUSED gpointer data)
{
	int width, height;

	waited++;
	if (!expect_grow && step > 0 && waited >= 30) {
		gtk_window_get_size (GTK_WINDOW (window), &width, &height);
		report (width == start_width && height == start_height,
			step == 1 ? "zooming in keeps the size" : "zooming out keeps the size");
		if (step == 2) {
			finish ();
			return G_SOURCE_REMOVE;
		}
		press (GDK_KEY_minus);
		step++;
		waited = 0;
		return G_SOURCE_CONTINUE;
	}
	if (waited > 100) {
		if (window != NULL && step > 0) {
			gtk_window_get_size (GTK_WINDOW (window), &width, &height);
			fprintf (out, "size %dx%d, then %dx%d, now %dx%d\n", start_width, start_height,
				 big_width, big_height, width, height);
		}
		report (FALSE, step == 2 ? "zooming back out goes back to the size it had" :
			step == 1 ? "zooming in grows the window" : "timed out waiting for the folder");
		finish ();
		return G_SOURCE_REMOVE;
	}

	switch (step) {
	case 0: {
		GList *toplevels = gtk_window_list_toplevels ();
		GList *l;
		GtkWidget *view = NULL;

		for (l = toplevels; l != NULL; l = l->next) {
			if (is_a (l->data, "NemoWindow") && gtk_widget_get_mapped (l->data)) {
				window = l->data;
			}
		}
		g_list_free (toplevels);
		if (window == NULL) {
			return G_SOURCE_CONTINUE;
		}
		find_view_cb (window, &view);
		/* Let the first layout settle. */
		if (view == NULL || waited < 20) {
			return G_SOURCE_CONTINUE;
		}
		gtk_window_get_size (GTK_WINDOW (window), &start_width, &start_height);
		press (GDK_KEY_plus);
		break;
	}
	case 1:
		if (!expect_grow) {
			return G_SOURCE_CONTINUE;
		}
		gtk_window_get_size (GTK_WINDOW (window), &width, &height);
		/* A compact view grows only its rows. */
		if (width < start_width || height <= start_height) {
			return G_SOURCE_CONTINUE;
		}
		big_width = width;
		big_height = height;
		report (TRUE, "zooming in grows the window");
		press (GDK_KEY_minus);
		break;
	case 2:
		if (!expect_grow) {
			return G_SOURCE_CONTINUE;
		}
		gtk_window_get_size (GTK_WINDOW (window), &width, &height);
		if (width != start_width || height != start_height) {
			return G_SOURCE_CONTINUE;
		}
		report (big_height > start_height, "zooming back out goes back to the size it had");
		finish ();
		return G_SOURCE_REMOVE;
	}

	step++;
	waited = 0;
	return G_SOURCE_CONTINUE;
}

__attribute__((constructor)) static void
probe_start (void)
{
	const char *path = g_getenv ("NEMO_ZOOMWIN_OUT");

	if (path == NULL) {
		return;
	}
	expect_grow = g_strcmp0 (g_getenv ("NEMO_ZOOMWIN_EXPECT"), "grow") == 0;
	out = fopen (path, "w");
	if (out == NULL) {
		return;
	}
	g_timeout_add (100, tick, NULL);
}
