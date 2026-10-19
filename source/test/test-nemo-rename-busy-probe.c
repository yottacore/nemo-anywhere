/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */

/* test-nemo-rename-busy-probe.c - presses F2 on a picture while the folder
   keeps changing, from inside the program.

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
 * test-nemo-rename-photo-probe. A folder of photos seen for the first time
 * gets a thumbnail done many times a second, and each one is a change to a
 * file in the folder. Each change put off the menu update a new selection
 * asks for. With nothing selected before, Rename stayed off, and F2 did
 * nothing until the thumbnails were all made. Here a file in
 * $NEMO_RENAME_PATH has its mode flipped every 15 ms in their place. A file
 * coming and going would move icons, and a moved icon updates the menus by
 * another way, so it only failed now and then.
 *
 * Once the window shows the folder at the picture size, each try starts with
 * nothing selected and the menus caught up. Then the folder starts changing,
 * a picture is selected, and F2 is pressed half a second later. The first
 * try keeps the folder still, to show F2 works here at all. One "ok" or
 * "FAIL" line per check goes to $NEMO_RENAME_OUT, and "done" at the end.
 *
 * No symbol of the program is used. Keys go through the window's own
 * shortcuts, the file is picked through the view's accessible object, and
 * the changes are counted on the folder object's signals. */

#include <stdio.h>
#include <stdlib.h>

#include <glib/gstdio.h>
#include <gtk/gtk.h>
#include <eel/eel-canvas.h>

#define TRIES 4

static FILE       *out;
static const char *folder_name;
static char       *noise_path;
static GtkWidget  *window;
static GtkWidget  *pane;
static int         waited;
static int         step;
static int         step_at;
static int         try_number;
static gboolean    churning;
static int         churned;
static int         changes_heard;

static void
report (gboolean passed, const char *what)
{
	fprintf (out, "%s %s\n", passed ? "ok" : "FAIL", what);
	fflush (out);
}

static void
finish (void)
{
	churning = FALSE;
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

/* Stands in for thumbnails being made: the view hears of a change to a
   file far more often than its menu update waits. */
static gboolean
churn (G_GNUC_UNUSED gpointer data)
{
	if (!churning) {
		return G_SOURCE_REMOVE;
	}
	g_chmod (noise_path, churned % 2 == 0 ? 0600 : 0644);
	churned++;
	return G_SOURCE_CONTINUE;
}

static void
start_churn (void)
{
	if (!churning) {
		churning = TRUE;
		g_timeout_add (15, churn, NULL);
	}
}

static gboolean
heard_change (G_GNUC_UNUSED GSignalInvocationHint *hint,
	      G_GNUC_UNUSED guint                  n_values,
	      G_GNUC_UNUSED const GValue          *values,
	      G_GNUC_UNUSED gpointer               data)
{
	changes_heard++;
	return TRUE;
}

/* What a view listens to for changes in its folder. */
static void
listen_to_folder (void)
{
	static const char *const names[] = { "files_added", "files_changed" };
	GType type = g_type_from_name ("NemoDirectory");
	guint i;

	if (type == 0) {
		return;
	}
	g_type_class_ref (type);
	for (i = 0; i < G_N_ELEMENTS (names); i++) {
		guint id = g_signal_lookup (names[i], type);

		if (id != 0) {
			g_signal_add_emission_hook (id, 0, heard_change, NULL, NULL);
		}
	}
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

/* Up to forty seconds in all. */
static gboolean
tick (G_GNUC_UNUSED gpointer data)
{
	AtkSelection *selection;
	GList *toplevels, *l;
	const char *title;
	char *line;
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
		if (first_icon_height () < 300) {
			return G_SOURCE_CONTINUE;
		}
		listen_to_folder ();
		step = 1;
		step_at = waited;
		return G_SOURCE_CONTINUE;
	}

	selection = ATK_SELECTION (gtk_widget_get_accessible (pane));
	since = waited - step_at;
	switch (step) {
	case 1:
		/* Nothing selected, so Rename is off once the menus catch up. */
		if (since < 10) {
			return G_SOURCE_CONTINUE;
		}
		churning = FALSE;
		atk_selection_clear_selection (selection);
		break;
	case 2:
		if (since < 5) {
			return G_SOURCE_CONTINUE;
		}
		if (try_number > 0) {
			start_churn ();
		}
		break;
	case 3:
		if (since < 2) {
			return G_SOURCE_CONTINUE;
		}
		changes_heard = 0;
		atk_selection_add_selection (selection, try_number);
		break;
	case 4:
		/* Ten times what the menu update after a selection asks for. */
		if (since < 5) {
			return G_SOURCE_CONTINUE;
		}
		if (try_number > 0) {
			line = g_strdup_printf ("picture %d: the folder kept changing (%d changes)",
						try_number + 1, changes_heard);
			report (changes_heard >= 10, line);
			g_free (line);
		}
		line = g_strdup_printf ("picture %d: F2 is a shortcut in the window", try_number + 1);
		report (press (GDK_KEY_F2, 0), line);
		g_free (line);
		break;
	case 5:
		if (since < 3) {
			return G_SOURCE_CONTINUE;
		}
		line = g_strdup_printf ("picture %d: the rename field shows", try_number + 1);
		report (find_widget (pane, "EelEditableLabel", TRUE) != NULL, line);
		g_free (line);
		send_escape ();
		if (++try_number < TRIES) {
			step = 1;
			step_at = waited - 5;
			return G_SOURCE_CONTINUE;
		}
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
	const char *folder = g_getenv ("NEMO_RENAME_PATH");

	folder_name = g_strdup (g_getenv ("NEMO_RENAME_FOLDER"));
	if (path == NULL || folder_name == NULL || folder == NULL) {
		return;
	}
	/* Sorts last, so the pictures keep their places. */
	noise_path = g_build_filename (folder, "zz noise.txt", NULL);
	/* No output then, which the test reads as a failure. */
	if (!g_file_set_contents (noise_path, "x", 1, NULL)) {
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
