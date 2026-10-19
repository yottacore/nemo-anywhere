/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */

/* test-nemo-delete-after-dialog-probe.c - a delete key pressed right after
   Properties closes, from inside the program.

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

/* Preloaded into the real program by test-nemo-delete-after-dialog. Once the
 * window shows $NEMO_DELDLG_FOLDER it selects everything (one file), opens
 * Properties with Ctrl+I, closes it with Escape, and presses Shift+Delete as
 * soon as the window has the focus back. The key has to reach the delete
 * question at the first press. The question is then cancelled. One "ok" or
 * "FAIL" line per check goes to $NEMO_DELDLG_OUT, and "done" at the end.
 *
 * Shift+Delete rather than Delete, since there may be no trash where this
 * runs. Both go through the same check on the window's focus.
 *
 * No symbol of the program is used. */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <gtk/gtk.h>

static FILE       *out;
static const char *folder_path;
static GtkWidget  *window;
static GtkWidget  *props;
static GtkWidget  *question;
static gint64      focus_back_at;
static gint64      key_at;
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

static void
send_key (GtkWidget *toplevel, guint keyval, GdkModifierType state)
{
	GdkDisplay *display = gtk_widget_get_display (toplevel);
	GdkKeymapKey *keys = NULL;
	gint n_keys = 0;
	GdkEvent *event;

	event = gdk_event_new (GDK_KEY_PRESS);
	event->key.window = g_object_ref (gtk_widget_get_window (toplevel));
	event->key.send_event = TRUE;
	event->key.time = GDK_CURRENT_TIME;
	event->key.keyval = keyval;
	event->key.state = state;
	if (gdk_keymap_get_entries_for_keyval (gdk_keymap_get_for_display (display), keyval,
					       &keys, &n_keys) && n_keys > 0) {
		event->key.hardware_keycode = (guint16) keys[0].keycode;
		event->key.group = (guint8) keys[0].group;
	}
	g_free (keys);
	gdk_event_set_device (event, gdk_seat_get_keyboard (gdk_display_get_default_seat (display)));
	gtk_main_do_event (event);
	gdk_event_free (event);
}

static gboolean
on_focus_in (G_GNUC_UNUSED GtkWidget *widget, G_GNUC_UNUSED GdkEvent *event,
	     G_GNUC_UNUSED gpointer data)
{
	if (props != NULL && focus_back_at == 0) {
		focus_back_at = g_get_monotonic_time ();
	}
	return GDK_EVENT_PROPAGATE;
}

static GtkWidget *
find_toplevel (const char *type_name, GtkWidget *not_this, GtkWidget *nor_this)
{
	GList *toplevels, *l;
	GtkWidget *found = NULL;

	toplevels = gtk_window_list_toplevels ();
	for (l = toplevels; l != NULL; l = l->next) {
		if (l->data != not_this && l->data != nor_this &&
		    is_a (l->data, type_name) && gtk_widget_get_mapped (l->data)) {
			found = l->data;
		}
	}
	g_list_free (toplevels);

	return found;
}

/* Up to sixty seconds in all. */
static gboolean
tick (G_GNUC_UNUSED gpointer data)
{
	const char *title;
	char *base, *what;
	gboolean focused;

	if (++waited > 600) {
		report (FALSE, "the steps finished in time");
		finish ();
		return G_SOURCE_REMOVE;
	}

	switch (step) {
	case 0:
		window = find_toplevel ("NemoWindow", NULL, NULL);
		title = window != NULL ? gtk_window_get_title (GTK_WINDOW (window)) : NULL;
		base = g_path_get_basename (folder_path);
		focused = title != NULL && g_str_has_prefix (title, base);
		g_free (base);
		if (!focused) {
			return G_SOURCE_CONTINUE;
		}
		g_signal_connect (window, "focus-in-event", G_CALLBACK (on_focus_in), NULL);
		break;
	case 1:
		/* Past the window's own first second, and the files listed. */
		if (waited - step_at < 20) {
			return G_SOURCE_CONTINUE;
		}
		report (gtk_accel_groups_activate (G_OBJECT (window), GDK_KEY_a, GDK_CONTROL_MASK),
			"Ctrl+A selects the file");
		report (gtk_accel_groups_activate (G_OBJECT (window), GDK_KEY_i, GDK_CONTROL_MASK),
			"Ctrl+I opens Properties");
		break;
	case 2:
		props = find_toplevel ("NemoPropertiesWindow", NULL, NULL);
		if (props == NULL || !gtk_window_has_toplevel_focus (GTK_WINDOW (props))) {
			if (props != NULL && (waited - step_at) % 10 == 5) {
				gdk_window_focus (gtk_widget_get_window (props), GDK_CURRENT_TIME);
			}
			if (waited - step_at > 100) {
				report (FALSE, "Properties opened with the focus");
				finish ();
				return G_SOURCE_REMOVE;
			}
			return G_SOURCE_CONTINUE;
		}
		report (TRUE, "Properties opened with the focus");
		break;
	case 3:
		if (waited - step_at < 10) {
			return G_SOURCE_CONTINUE;
		}
		focus_back_at = 0;
		send_key (props, GDK_KEY_Escape, 0);
		break;
	case 4:
		/* The window manager hands the focus back. With none, do what it
		   would. */
		if (focus_back_at == 0) {
			if (waited - step_at == 10) {
				gdk_window_focus (gtk_widget_get_window (window), GDK_CURRENT_TIME);
			}
			if (waited - step_at > 50) {
				report (FALSE, "the window took the focus back");
				finish ();
				return G_SOURCE_REMOVE;
			}
			return G_SOURCE_CONTINUE;
		}
		report (find_toplevel ("NemoPropertiesWindow", NULL, NULL) == NULL,
			"Escape closed Properties");
		send_key (window, GDK_KEY_Delete, GDK_SHIFT_MASK);
		key_at = g_get_monotonic_time ();
		what = g_strdup_printf ("the key came %d ms after the focus did, inside the 1 s grace",
					(int) ((key_at - focus_back_at) / 1000));
		report (key_at - focus_back_at < G_USEC_PER_SEC, what);
		g_free (what);
		break;
	case 5:
		question = find_toplevel ("GtkDialog", window, props);
		if (question == NULL) {
			if (waited - step_at > 30) {
				report (FALSE, "the first Shift+Delete asks the delete question");
				finish ();
				return G_SOURCE_REMOVE;
			}
			return G_SOURCE_CONTINUE;
		}
		report (TRUE, "the first Shift+Delete asks the delete question");
		gtk_dialog_response (GTK_DIALOG (question), GTK_RESPONSE_CANCEL);
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
	const char *path = g_getenv ("NEMO_DELDLG_OUT");

	folder_path = g_strdup (g_getenv ("NEMO_DELDLG_FOLDER"));
	if (path == NULL || folder_path == NULL) {
		return;
	}
	out = fopen (path, "w");
	/* Anything the program starts loads this too, and would empty the file
	   again. */
	g_unsetenv ("NEMO_DELDLG_OUT");
	g_unsetenv ("NEMO_DELDLG_FOLDER");
	if (out == NULL) {
		return;
	}
	g_timeout_add (100, tick, NULL);
}
