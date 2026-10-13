/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */

/* test-nemo-new-folder-path-probe.c - New folder, a path typed as its name,
   then undo, from inside the program.

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

/* Preloaded into the real program by test-nemo-new-folder-path-gui. Once the
 * window shows $NEMO_NEST_FOLDER it presses Ctrl+Shift+N, types
 * "alpha/beta/gamma" into the rename box and presses Enter. The 3 folders have
 * to turn up nested, with "alpha" selected and no error dialog, and Ctrl+Z
 * has to take them all away again. One "ok" or "FAIL" line per check goes to
 * $NEMO_NEST_OUT, and "done" at the end.
 *
 * No symbol of the program is used. Keys go through the window's own
 * shortcuts or as key events to the window. */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <gtk/gtk.h>

static FILE       *out;
static const char *folder_path;
static GtkWidget  *window;
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
is_a (gpointer object, const char *type_name)
{
	GType type = g_type_from_name (type_name);

	return type != 0 && object != NULL && g_type_is_a (G_OBJECT_TYPE (object), type);
}

static gboolean
press (guint key, GdkModifierType mods)
{
	return gtk_accel_groups_activate (G_OBJECT (window), key, mods);
}

static void
send_key (guint keyval)
{
	GdkDisplay *display = gtk_widget_get_display (window);
	GdkKeymapKey *keys = NULL;
	gint n_keys = 0;
	GdkEvent *event;

	event = gdk_event_new (GDK_KEY_PRESS);
	event->key.window = g_object_ref (gtk_widget_get_window (window));
	event->key.send_event = TRUE;
	event->key.time = GDK_CURRENT_TIME;
	event->key.keyval = keyval;
	if (gdk_keymap_get_entries_for_keyval (gdk_keymap_get_for_display (display), keyval,
					       &keys, &n_keys) && n_keys > 0) {
		event->key.hardware_keycode = keys[0].keycode;
	}
	g_free (keys);
	gdk_event_set_device (event, gdk_seat_get_keyboard (gdk_display_get_default_seat (display)));
	gtk_main_do_event (event);
	gdk_event_free (event);
}

static gboolean
is_dir (const char *relative)
{
	char *path = g_build_filename (folder_path, relative, NULL);
	gboolean yes = g_file_test (path, G_FILE_TEST_IS_DIR);

	g_free (path);
	return yes;
}

static gboolean
exists (const char *relative)
{
	char *path = g_build_filename (folder_path, relative, NULL);
	gboolean yes = g_file_test (path, G_FILE_TEST_EXISTS);

	g_free (path);
	return yes;
}

/* Any other toplevel showing would be an error dialog. */
static gboolean
dialog_showing (void)
{
	GList *toplevels = gtk_window_list_toplevels ();
	GList *l;
	gboolean found = FALSE;

	for (l = toplevels; l != NULL; l = l->next) {
		if (GTK_IS_DIALOG (l->data) && gtk_widget_get_mapped (l->data)) {
			found = TRUE;
		}
	}
	g_list_free (toplevels);
	return found;
}

typedef struct {
	const char *needle;
	gboolean    found;
} FindLabel;

static void
find_label_cb (GtkWidget *widget, gpointer data)
{
	FindLabel *find = data;

	if (find->found) {
		return;
	}
	if (GTK_IS_LABEL (widget) && gtk_widget_get_mapped (widget) &&
	    strstr (gtk_label_get_text (GTK_LABEL (widget)), find->needle) != NULL) {
		find->found = TRUE;
	} else if (GTK_IS_CONTAINER (widget)) {
		gtk_container_forall (GTK_CONTAINER (widget), find_label_cb, data);
	}
}

/* The status bar names a single selected item. */
static gboolean
shows_selected (const char *name)
{
	char *needle = g_strdup_printf ("\"%s\" selected", name);
	FindLabel find = { needle, FALSE };

	find_label_cb (window, &find);
	g_free (needle);
	return find.found;
}

static gboolean
box_is_open (GtkWidget *focus)
{
	char *text;
	gboolean open;

	if (focus == NULL || !GTK_IS_EDITABLE (focus)) {
		return FALSE;
	}
	text = gtk_editable_get_chars (GTK_EDITABLE (focus), 0, -1);
	open = text != NULL && strstr (text, "Untitled") != NULL;
	g_free (text);
	return open;
}

/* Up to sixty seconds in all. */
static gboolean
tick (G_GNUC_UNUSED gpointer data)
{
	GList *toplevels, *l;
	GtkWidget *focus;
	const char *title;
	char *base;
	int since;

	if (++waited > 600) {
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
		base = g_path_get_basename (folder_path);
		if (title == NULL || !g_str_has_prefix (title, base)) {
			g_free (base);
			return G_SOURCE_CONTINUE;
		}
		g_free (base);
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
		report (press (GDK_KEY_n, GDK_CONTROL_MASK | GDK_SHIFT_MASK),
			"Ctrl+Shift+N is a shortcut in the window");
		break;
	case 2:
		/* The rename box opens once the folder shows. */
		focus = gtk_window_get_focus (GTK_WINDOW (window));
		if (!box_is_open (focus)) {
			if (since > 100) {
				report (FALSE, "a rename box opened on the new folder");
				finish ();
				return G_SOURCE_REMOVE;
			}
			return G_SOURCE_CONTINUE;
		}
		report (TRUE, "a rename box opened on the new folder");
		{
			int position = 0;

			gtk_editable_delete_text (GTK_EDITABLE (focus), 0, -1);
			gtk_editable_insert_text (GTK_EDITABLE (focus), "alpha/beta/gamma", -1, &position);
		}
		send_key (GDK_KEY_Return);
		break;
	case 3:
		if (!is_dir ("alpha/beta/gamma") && since < 100) {
			return G_SOURCE_CONTINUE;
		}
		/* A moment for the view to catch up with the selection. */
		if (since < 15) {
			return G_SOURCE_CONTINUE;
		}
		report (is_dir ("alpha/beta/gamma"), "the 3 folders were made, nested");
		report (!exists ("Untitled Folder"), "no Untitled Folder is left");
		report (!dialog_showing (), "no error dialog");
		report (shows_selected ("alpha"), "the top folder is selected");
		report (press (GDK_KEY_z, GDK_CONTROL_MASK), "Ctrl+Z is a shortcut in the window");
		break;
	case 4:
		if (exists ("alpha") && since < 100) {
			return G_SOURCE_CONTINUE;
		}
		report (!exists ("alpha"), "undo took all 3 away");
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
	const char *path = g_getenv ("NEMO_NEST_OUT");

	folder_path = g_strdup (g_getenv ("NEMO_NEST_FOLDER"));
	if (path == NULL || folder_path == NULL) {
		return;
	}
	out = fopen (path, "w");
	/* Anything the program starts loads this too, and would empty the file
	   again. */
	g_unsetenv ("NEMO_NEST_OUT");
	g_unsetenv ("NEMO_NEST_FOLDER");
	if (out == NULL) {
		return;
	}
	g_timeout_add (100, tick, NULL);
}
