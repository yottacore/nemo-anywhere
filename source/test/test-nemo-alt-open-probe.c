/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */

/* test-nemo-alt-open-probe.c - opens a folder with Shift+Return from outside
   any event.

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

/* Preloaded into the real program by test-nemo-alt-open. Once the icon view
 * shows, it selects all and hands the icon container a Shift+Return straight
 * from a timeout, so GTK has no current event while the alternate open runs.
 * Then the window has to show the one folder there, $NEMO_ALTOPEN_NAME. One
 * "ok" or "FAIL" line goes to $NEMO_ALTOPEN_OUT, and "done" at the end. */

#include <stdio.h>
#include <string.h>

#include <gtk/gtk.h>
#include <gdk/gdkkeysyms.h>

static FILE       *out;
static const char *name;
static GtkWidget  *window;
static int         step;
static int         waited;

static gboolean
is_a (GtkWidget *widget, const char *type_name)
{
	GType type = g_type_from_name (type_name);

	return type != 0 && widget != NULL && g_type_is_a (G_OBJECT_TYPE (widget), type);
}

static void
find_container_cb (GtkWidget *widget, gpointer data)
{
	GtkWidget **found = data;

	if (*found != NULL) {
		return;
	}
	if (is_a (widget, "NemoIconContainer") && gtk_widget_get_mapped (widget)) {
		*found = widget;
	} else if (GTK_IS_CONTAINER (widget)) {
		gtk_container_forall (GTK_CONTAINER (widget), find_container_cb, data);
	}
}

static void
finish (gboolean passed, const char *what)
{
	fprintf (out, "%s %s\ndone\n", passed ? "ok" : "FAIL", what);
	fflush (out);
}

static void
shift_return (GtkWidget *container)
{
	GdkEvent *event = gdk_event_new (GDK_KEY_PRESS);

	event->key.window = g_object_ref (gtk_widget_get_window (container));
	event->key.send_event = TRUE;
	event->key.keyval = GDK_KEY_Return;
	event->key.state = GDK_SHIFT_MASK;
	event->key.time = GDK_CURRENT_TIME;
	gdk_event_set_device (event, gdk_seat_get_keyboard (gdk_display_get_default_seat (gdk_display_get_default ())));
	gtk_widget_event (container, event);
	gdk_event_free (event);
}

static gboolean
tick (G_GNUC_UNUSED gpointer data)
{
	const char *title;

	waited++;
	if (waited > 100) {
		finish (FALSE, step == 0 ? "timed out waiting for the icon view" : "the folder never opened");
		return G_SOURCE_REMOVE;
	}

	if (step == 0) {
		GList *toplevels = gtk_window_list_toplevels ();
		GList *l;
		GtkWidget *container = NULL;

		for (l = toplevels; l != NULL; l = l->next) {
			if (is_a (l->data, "NemoWindow") && gtk_widget_get_mapped (l->data)) {
				window = l->data;
			}
		}
		g_list_free (toplevels);
		if (window == NULL) {
			return G_SOURCE_CONTINUE;
		}
		find_container_cb (window, &container);
		/* Let the first layout settle. */
		if (container == NULL || waited < 20) {
			return G_SOURCE_CONTINUE;
		}
		gtk_accel_groups_activate (G_OBJECT (window), GDK_KEY_a, GDK_CONTROL_MASK);
		shift_return (container);
		step++;
		waited = 0;
		return G_SOURCE_CONTINUE;
	}

	title = gtk_window_get_title (GTK_WINDOW (window));
	if (title == NULL || strstr (title, name) == NULL) {
		return G_SOURCE_CONTINUE;
	}
	finish (TRUE, "Shift+Return with no current event opens the folder");
	return G_SOURCE_REMOVE;
}

__attribute__((constructor)) static void
probe_start (void)
{
	const char *path = g_getenv ("NEMO_ALTOPEN_OUT");

	name = g_getenv ("NEMO_ALTOPEN_NAME");
	if (path == NULL || name == NULL) {
		return;
	}
	// codeql[cpp/world-writable-file-creation] a test's own scratch file
	out = fopen (path, "w");
	if (out == NULL) {
		return;
	}
	g_timeout_add (100, tick, NULL);
}
