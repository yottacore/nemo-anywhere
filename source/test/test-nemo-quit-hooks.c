/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */

/* test-nemo-quit-hooks.c - work a quit has to finish, set up from inside.

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

/* Preloaded into the real program by test-nemo-quit-saves. Once a window is
 * up it changes a keyboard shortcut, which the program saves 30 s later, and
 * puts up the "still unmounting" notice the way an unfinished unmount does.
 * Then it writes "changed" to $NEMO_QUIT_OUT. With $NEMO_QUIT_CHECK set it
 * changes nothing, and writes what the shortcut is on this start instead. */

#include <stdio.h>

#include <gtk/gtk.h>

#define PROBE_PATH "<nemo-test>/Probe"

/* NEMO_NOTIFICATION_UNMOUNT_ID_PENDING in nemo-main-application.c */
#define PENDING_ID "unmount-pending"

static gboolean
window_up (void)
{
	GList *toplevels = gtk_window_list_toplevels ();
	GList *l;
	gboolean up = FALSE;

	for (l = toplevels; l != NULL && !up; l = l->next) {
		up = GTK_IS_APPLICATION_WINDOW (l->data) && gtk_widget_get_mapped (l->data);
	}
	g_list_free (toplevels);

	return up;
}

static gboolean
poke (gpointer user_data)
{
	GApplication *app = g_application_get_default ();
	char *text;

	if (app == NULL || !g_application_get_is_registered (app) || !window_up ()) {
		return G_SOURCE_CONTINUE;
	}

	if (g_getenv ("NEMO_QUIT_CHECK") != NULL) {
		GtkAccelKey key = { 0 };
		char *name = NULL;

		if (gtk_accel_map_lookup_entry (PROBE_PATH, &key) && key.accel_key != 0) {
			name = gtk_accelerator_name (key.accel_key, key.accel_mods);
		}
		text = g_strdup_printf ("%s\n", name != NULL ? name : "none");
		g_free (name);
	} else {
		GNotification *notice = g_notification_new ("Still unmounting");

		gtk_accel_map_add_entry (PROBE_PATH, 0, 0);
		gtk_accel_map_change_entry (PROBE_PATH, GDK_KEY_F12, GDK_CONTROL_MASK, TRUE);

		g_application_send_notification (app, PENDING_ID, notice);
		g_object_unref (notice);

		text = g_strdup ("changed\n");
	}

	g_file_set_contents (g_getenv ("NEMO_QUIT_OUT"), text, -1, NULL);
	g_free (text);

	return G_SOURCE_REMOVE;
}

__attribute__ ((constructor)) static void
start (void)
{
	if (g_getenv ("NEMO_QUIT_OUT") != NULL) {
		g_timeout_add (100, poke, NULL);
	}
}
