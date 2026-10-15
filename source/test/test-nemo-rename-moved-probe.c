/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */

/* test-nemo-rename-moved-probe.c - lays the icons out again while a picture
   is being renamed, from inside the program.

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

/* Preloaded into the real program by test-nemo-rename-photo, in place of
 * test-nemo-rename-photo-probe. A thumbnail of another shape coming in for the
 * row of a picture being renamed lays the icons out again. The picture moved
 * under the rename, and a move ended it, saving whatever had been typed so
 * far. A wider window lays the icons out again the same way, on demand.
 *
 * Once the window shows $NEMO_RENAME_FOLDER at the picture size and the
 * thumbnails have had time to come in, the probe selects the second picture,
 * presses F2, types part of a name, and widens the window. The rename has to
 * stay open, go with the picture and keep the text, and nothing may be saved
 * until Escape, which keeps the old name. One "ok" or "FAIL" line per check
 * goes to $NEMO_RENAME_OUT, and "done" at the end.
 *
 * No symbol of the program is used. Keys go through the window's own
 * shortcuts, and the file is picked through the view's accessible object. */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <gtk/gtk.h>
#include <eel/eel-canvas.h>

#define PICTURE 1
#define TYPED   "half typ"

static FILE       *out;
static const char *folder_name;
static char       *folder_path;
static char       *old_path;
static char       *typed_path;
static GtkWidget  *window;
static GtkWidget  *pane;
static int         waited;
static int         step;
static int         step_at;
static int         item_x, item_y;

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

/* The picture's canvas item, and its file's name. */
static EelCanvasItem *
picture_item (char **name)
{
	AtkObject *child = atk_object_ref_accessible_child (gtk_widget_get_accessible (pane), PICTURE);
	GObject *object = ATK_IS_GOBJECT_ACCESSIBLE (child) ?
		atk_gobject_accessible_get_object (ATK_GOBJECT_ACCESSIBLE (child)) : NULL;

	if (name != NULL) {
		*name = g_strdup (atk_object_get_name (child));
	}
	g_object_unref (child);
	return object != NULL && g_type_is_a (G_OBJECT_TYPE (object), g_type_from_name ("EelCanvasItem")) ?
		(EelCanvasItem *) object : NULL;
}

static gboolean
press (guint key, GdkModifierType mods)
{
	return gtk_accel_groups_activate (G_OBJECT (window), key, mods);
}

static void
send_escape (void)
{
	GdkEvent *event = gdk_event_new (GDK_KEY_PRESS);

	event->key.window = g_object_ref (gtk_widget_get_window (pane));
	event->key.send_event = TRUE;
	event->key.time = GDK_CURRENT_TIME;
	event->key.keyval = GDK_KEY_Escape;
	gdk_event_set_device (event, gdk_seat_get_keyboard (gdk_display_get_default_seat (gdk_display_get_default ())));
	gtk_widget_event (pane, event);
	gdk_event_free (event);
}

/* The box sits under the middle of the picture, on its item. Both are in
   the canvas's pixels. */
static void
check_place (GtkWidget *field, EelCanvasItem *item, const char *when)
{
	GtkAllocation place;
	int field_middle, item_middle;
	char *line;

	gtk_widget_get_allocation (field, &place);
	field_middle = place.x + place.width / 2;
	item_middle = (int) (item->x1 + item->x2) / 2;
	line = g_strdup_printf ("%s: the field is under its picture (middle %d, picture %d; top %d, picture %d to %d)",
				when, field_middle, item_middle, place.y, (int) item->y1, (int) item->y2);
	report (ABS (field_middle - item_middle) <= 6 && place.y >= item->y1 && place.y <= item->y2, line);
	g_free (line);
}

static void
widen (int extra)
{
	int width, height;

	gtk_window_get_size (GTK_WINDOW (window), &width, &height);
	gtk_window_resize (GTK_WINDOW (window), 1240 + extra, height);
}

/* Up to forty seconds in all. */
static gboolean
tick (G_GNUC_UNUSED gpointer data)
{
	AtkSelection *selection;
	GtkWidget *field;
	EelCanvasItem *item;
	GList *toplevels, *l;
	const char *title;
	char *name, *text, *line;
	int since, position;

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
		if (first_icon_height () < 300) {
			return G_SOURCE_CONTINUE;
		}
		step = 1;
		step_at = waited;
		return G_SOURCE_CONTINUE;
	}

	selection = ATK_SELECTION (gtk_widget_get_accessible (pane));
	field = find_widget (pane, "EelEditableLabel", TRUE);
	since = waited - step_at;
	switch (step) {
	case 1:
		/* Lets the thumbnails come in first, so only the wider window moves
		   the picture. */
		if (since < 20) {
			return G_SOURCE_CONTINUE;
		}
		picture_item (&name);
		old_path = g_build_filename (folder_path, name, NULL);
		typed_path = g_build_filename (folder_path, TYPED, NULL);
		g_free (name);
		atk_selection_clear_selection (selection);
		atk_selection_add_selection (selection, PICTURE);
		break;
	case 2:
		if (since < 5) {
			return G_SOURCE_CONTINUE;
		}
		report (press (GDK_KEY_F2, 0), "F2 is a shortcut in the window");
		break;
	case 3:
		if (since < 3) {
			return G_SOURCE_CONTINUE;
		}
		report (field != NULL, "the rename field shows");
		if (field == NULL) {
			finish ();
			return G_SOURCE_REMOVE;
		}
		gtk_editable_delete_text (GTK_EDITABLE (field), 0, -1);
		position = 0;
		gtk_editable_insert_text (GTK_EDITABLE (field), TYPED, -1, &position);
		item = picture_item (NULL);
		item_x = (int) item->x1;
		item_y = (int) item->y1;
		check_place (field, item, "before");
		widen (0);
		break;
	case 4:
		/* GTK lets one size change go by while a rename is open. */
		if (since < 3) {
			return G_SOURCE_CONTINUE;
		}
		widen (30);
		break;
	case 5:
		if (since < 5) {
			return G_SOURCE_CONTINUE;
		}
		item = picture_item (NULL);
		line = g_strdup_printf ("the picture moved (%d,%d to %d,%d)",
					item_x, item_y, (int) item->x1, (int) item->y1);
		report ((int) item->x1 != item_x || (int) item->y1 != item_y, line);
		g_free (line);

		report (field != NULL, "the rename field still shows");
		if (field != NULL) {
			check_place (field, item, "after");

			text = gtk_editable_get_chars (GTK_EDITABLE (field), 0, -1);
			line = g_strdup_printf ("the field kept what was typed (%s)", text);
			report (g_strcmp0 (text, TYPED) == 0, line);
			g_free (line);
			g_free (text);
		} else {
			report (FALSE, "after: the field is under its picture");
			report (FALSE, "the field kept what was typed");
		}
		report (g_file_test (old_path, G_FILE_TEST_EXISTS) && !g_file_test (typed_path, G_FILE_TEST_EXISTS),
			"nothing was saved while the rename was open");
		send_escape ();
		break;
	case 6:
		if (since < 5) {
			return G_SOURCE_CONTINUE;
		}
		report (field == NULL && g_file_test (old_path, G_FILE_TEST_EXISTS) &&
			!g_file_test (typed_path, G_FILE_TEST_EXISTS),
			"Escape ended the rename and kept the old name");
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
	folder_path = g_strdup (g_getenv ("NEMO_RENAME_PATH"));
	if (path == NULL || folder_name == NULL || folder_path == NULL) {
		return;
	}
	out = fopen (path, "w");
	/* A thumbnailer the program starts loads this too, and would empty the
	   file again. */
	g_unsetenv ("NEMO_RENAME_OUT");
	g_unsetenv ("NEMO_RENAME_FOLDER");
	g_unsetenv ("NEMO_RENAME_PATH");
	if (out == NULL) {
		return;
	}
	g_timeout_add (100, tick, NULL);
}
