/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */

/* test-nemo-rename-photo-probe.c - presses F2 on a picture in the icon view
   and looks at the field, from inside the program.

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

/* Preloaded into the real program by test-nemo-rename-photo. Once the window
 * shows $NEMO_RENAME_FOLDER at the picture size, it selects the first file
 * and presses F2, zooms in twice, and presses F2 again on the same file. Each
 * time the field has to be as wide as it asked to be and be in sight. One
 * "ok" or "FAIL" line per check goes to $NEMO_RENAME_OUT, and "done" at the
 * end.
 *
 * No symbol of the program is used. Keys go through the window's own
 * shortcuts, and the file is picked through the view's accessible object. */

#include <stdio.h>
#include <stdlib.h>

#include <gtk/gtk.h>
#include <eel/eel-canvas.h>

/* The field is a pixel of padding wider than the width it asks for, each side. */
#define PAD 1

static FILE       *out;
static const char *folder_name;
static GtkWidget  *window;
static GtkWidget  *pane;
static int         waited;
static int         step;
static int         step_at;

static void
report (gboolean passed, const char *what)
{
	fprintf (out, "%s %s\n", passed ? "ok" : "FAIL", what);
	fflush (out);
}

static void
finish (void)
{
	fprintf (out, "done\n");
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
	gboolean    visible_only;
	GtkWidget  *found;
} Find;

static void
find_cb (GtkWidget *widget, gpointer data)
{
	Find *find = data;

	if (find->found != NULL) {
		return;
	}
	if (is_a (widget, find->type_name) &&
	    (!find->visible_only || gtk_widget_get_visible (widget))) {
		find->found = widget;
	} else if (GTK_IS_CONTAINER (widget)) {
		gtk_container_forall (GTK_CONTAINER (widget), find_cb, data);
	}
}

static GtkWidget *
find_widget (GtkWidget *top, const char *type_name, gboolean visible_only)
{
	Find find = { type_name, visible_only, NULL };

	find_cb (top, &find);
	return find.found;
}

static gboolean
find_pane (void)
{
	GtkWidget *view = find_widget (window, "NemoView", FALSE);
	GtkWidget *child = view != NULL ? gtk_bin_get_child (GTK_BIN (view)) : NULL;

	if (child == NULL || !gtk_widget_get_mapped (child) || !is_a (child, "NemoIconContainer")) {
		return FALSE;
	}
	pane = child;
	return TRUE;
}

/* Height of the first icon, picture and name. A folder of pictures moves to
   its own size once it has loaded, so this says when that has happened. */
static int
first_icon_height (void)
{
	AtkObject *accessible = gtk_widget_get_accessible (pane);
	AtkObject *child;
	GObject *object;
	int height = 0;

	if (atk_object_get_n_accessible_children (accessible) < 1) {
		return 0;
	}
	child = atk_object_ref_accessible_child (accessible, 0);
	object = ATK_IS_GOBJECT_ACCESSIBLE (child) ?
		atk_gobject_accessible_get_object (ATK_GOBJECT_ACCESSIBLE (child)) : NULL;
	if (object != NULL && g_type_is_a (G_OBJECT_TYPE (object), g_type_from_name ("EelCanvasItem"))) {
		EelCanvasItem *item = (EelCanvasItem *) object;

		height = (int) (item->y2 - item->y1);
	}
	g_object_unref (child);
	return height;
}

static void
check_field (const char *when)
{
	GtkWidget *field = find_widget (pane, "EelEditableLabel", TRUE);
	GtkAdjustment *vadj = gtk_scrollable_get_vadjustment (GTK_SCROLLABLE (pane));
	GtkAllocation place;
	int asked = -1, top, bottom;
	char *line;

	if (field == NULL) {
		line = g_strdup_printf ("%s: the rename field shows", when);
		report (FALSE, line);
		g_free (line);
		return;
	}

	gtk_widget_get_allocation (field, &place);
	gtk_widget_get_size_request (field, &asked, NULL);
	line = g_strdup_printf ("%s: the field is as wide as asked (%d, asked %d)",
				when, place.width, asked + 2 * PAD);
	report (asked > 0 && place.width == asked + 2 * PAD, line);
	g_free (line);

	/* A layout places its children where they are on the whole canvas. */
	top = (int) gtk_adjustment_get_value (vadj);
	bottom = top + gtk_widget_get_allocated_height (pane);
	line = g_strdup_printf ("%s: the field is in sight (%d to %d, sight %d to %d)",
				when, place.y, place.y + place.height, top, bottom);
	report (place.y >= top && place.y + place.height <= bottom, line);
	g_free (line);
}

static gboolean
press (guint key, GdkModifierType mods)
{
	return gtk_accel_groups_activate (G_OBJECT (window), key, mods);
}

/* Up to forty seconds in all. */
static gboolean
tick (G_GNUC_UNUSED gpointer data)
{
	GList *toplevels, *l;
	const char *title;
	int since;

	if (++waited > 400) {
		report (FALSE, "the steps finished in time");
		finish ();
		return G_SOURCE_REMOVE;
	}

	if (step == 0) {
		toplevels = gtk_window_list_toplevels ();
		for (l = toplevels; l != NULL; l = l->next) {
			if (is_a (l->data, "NemoWindow") && gtk_widget_get_mapped (l->data)) {
				window = l->data;
			}
		}
		g_list_free (toplevels);
		title = window != NULL ? gtk_window_get_title (GTK_WINDOW (window)) : NULL;
		if (title == NULL || !g_str_has_prefix (title, folder_name) || !find_pane ()) {
			return G_SOURCE_CONTINUE;
		}
		/* The picture size, 320 pixels, and then a moment for the layout. */
		if (first_icon_height () < 300) {
			return G_SOURCE_CONTINUE;
		}
		step = 1;
		step_at = waited;
		return G_SOURCE_CONTINUE;
	}

	since = waited - step_at;
	switch (step) {
	case 1:
		if (since < 10) {
			return G_SOURCE_CONTINUE;
		}
		atk_selection_add_selection (ATK_SELECTION (gtk_widget_get_accessible (pane)), 0);
		break;
	case 2:
		/* Rename turns on from an idle after the selection changes. */
		if (since < 5) {
			return G_SOURCE_CONTINUE;
		}
		report (press (GDK_KEY_F2, 0), "F2 is a shortcut in the window");
		break;
	case 3:
		if (since < 10) {
			return G_SOURCE_CONTINUE;
		}
		check_field ("first F2");
		/* Zooming ends the rename, as it always has. */
		press (GDK_KEY_equal, GDK_CONTROL_MASK);
		break;
	case 4:
		if (since < 10) {
			return G_SOURCE_CONTINUE;
		}
		press (GDK_KEY_equal, GDK_CONTROL_MASK);
		break;
	case 5:
		if (since < 20) {
			return G_SOURCE_CONTINUE;
		}
		report (find_widget (pane, "EelEditableLabel", TRUE) == NULL,
			"zooming ended the first rename");
		press (GDK_KEY_F2, 0);
		break;
	case 6:
		if (since < 10) {
			return G_SOURCE_CONTINUE;
		}
		check_field ("same file again after zooming in");
		finish ();
		return G_SOURCE_REMOVE;
	}

	step++;
	step_at = waited;
	return G_SOURCE_CONTINUE;
}

__attribute__((constructor)) static void
probe_start (void)
{
	const char *path = g_getenv ("NEMO_RENAME_OUT");

	folder_name = g_strdup (g_getenv ("NEMO_RENAME_FOLDER"));
	if (path == NULL || folder_name == NULL) {
		return;
	}
	out = fopen (path, "w");
	/* A thumbnailer the program starts loads this too, and would empty the
	   file again. */
	g_unsetenv ("NEMO_RENAME_OUT");
	g_unsetenv ("NEMO_RENAME_FOLDER");
	if (out == NULL) {
		return;
	}
	g_timeout_add (100, tick, NULL);
}
