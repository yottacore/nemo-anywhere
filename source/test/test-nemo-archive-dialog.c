/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */

/* test-nemo-archive-dialog.c - the Compress dialog on a short screen.

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

/* Opening Options once pushed the dialog's buttons off a 540 pixel screen.
 * The options now scroll in whatever room the screen leaves. The real dialog
 * gives the closed height and what the options want; the room for those is
 * worked out for 540, 768 and 1080 pixel work areas, and the dialog is opened
 * for real on the test screen, where nothing should be cut. */

#include <config.h>

#include <stdlib.h>
#include <gtk/gtk.h>

#include <libnemo-private/nemo-archive.h>
#include <libnemo-private/nemo-global-preferences.h>

#include "nemo-archive-dialog.h"

#include "test-scratch.h"
#include "test-check.h"

typedef struct {
	GtkWidget *dialog;
	GtkWidget *expander;
	int closed_height;
	int natural;
	gboolean ran;
} Probe;

static void
settle (void)
{
	int i;

	for (i = 0; i < 20; i++) {
		while (gtk_events_pending ()) {
			gtk_main_iteration ();
		}
		g_usleep (10000);
	}
}

static GtkWidget *
find_type (GtkWidget *widget, GType type)
{
	GList *children, *l;
	GtkWidget *found = NULL;

	if (G_TYPE_CHECK_INSTANCE_TYPE (widget, type)) {
		return widget;
	}
	if (!GTK_IS_CONTAINER (widget)) {
		return NULL;
	}

	children = gtk_container_get_children (GTK_CONTAINER (widget));
	for (l = children; l != NULL && found == NULL; l = l->next) {
		found = find_type (l->data, type);
	}
	g_list_free (children);

	return found;
}

static GtkWidget *
find_dialog (void)
{
	GList *windows, *l;
	GtkWidget *dialog = NULL;

	windows = gtk_window_list_toplevels ();
	for (l = windows; l != NULL; l = l->next) {
		if (GTK_IS_DIALOG (l->data) && gtk_widget_get_visible (l->data)) {
			dialog = l->data;
		}
	}
	g_list_free (windows);

	return dialog;
}

static void
check_screen (const Probe *probe, int work_height)
{
	int room = nemo_archive_options_room (work_height, probe->closed_height);
	int shown = MIN (probe->natural, room);

	g_print ("%d px screen: options get %d of %d\n", work_height, shown, probe->natural);

	/* The buttons stay on screen. */
	check (probe->closed_height + shown <= work_height);

	/* Where they fit whole, they are not cut. */
	if (probe->closed_height + probe->natural <= work_height) {
		check (shown == probe->natural);
	}
}

static gboolean
probe_dialog (gpointer data)
{
	Probe *probe = data;
	GtkScrolledWindow *scroll;
	GdkRectangle work;
	int width, room;

	probe->ran = TRUE;
	probe->dialog = find_dialog ();
	check (probe->dialog != NULL);
	if (probe->dialog == NULL) {
		return G_SOURCE_REMOVE;
	}

	settle ();
	gtk_window_get_size (GTK_WINDOW (probe->dialog), &width, &probe->closed_height);
	probe->expander = find_type (probe->dialog, GTK_TYPE_EXPANDER);
	check (probe->expander != NULL);
	if (probe->expander == NULL) {
		gtk_dialog_response (GTK_DIALOG (probe->dialog), GTK_RESPONSE_CANCEL);
		return G_SOURCE_REMOVE;
	}

	scroll = GTK_SCROLLED_WINDOW (gtk_bin_get_child (GTK_BIN (probe->expander)));
	gtk_widget_get_preferred_height (gtk_bin_get_child (GTK_BIN (scroll)), NULL, &probe->natural);

	/* The fixture only means something if the options would not fit whole on
	   the short screen. */
	check (probe->closed_height + probe->natural > 540);

	check_screen (probe, 540);
	check_screen (probe, 768);
	check_screen (probe, 1080);

	/* Opened for real, on a screen tall enough to hold everything. */
	gdk_monitor_get_workarea (gdk_display_get_monitor_at_window (gtk_widget_get_display (probe->dialog),
								     gtk_widget_get_window (probe->dialog)),
				  &work);
	gtk_expander_set_expanded (GTK_EXPANDER (probe->expander), TRUE);
	settle ();
	room = nemo_archive_options_room (work.height, probe->closed_height);
	g_print ("opened on %d px: scroll %d to %d, options want %d\n", work.height,
		 gtk_scrolled_window_get_min_content_height (scroll),
		 gtk_scrolled_window_get_max_content_height (scroll), probe->natural);
	check (gtk_scrolled_window_get_max_content_height (scroll) == room);
	check (gtk_scrolled_window_get_min_content_height (scroll) == MIN (probe->natural, room));

	gtk_dialog_response (GTK_DIALOG (probe->dialog), GTK_RESPONSE_CANCEL);
	return G_SOURCE_REMOVE;
}

int
main (int argc, char *argv[])
{
	g_autofree char *path = NULL;
	GFile *dir, *file;
	GList *files;
	Probe probe = { 0 };
	char *tmp;

	tmp = test_scratch_config_home ("nemo-archive-dialog-test-XXXXXX");

	if (!gtk_init_check (&argc, &argv)) {
		g_print ("SKIP: no display\n");
		g_free (tmp);
		return 77;
	}

	nemo_global_preferences_init ();

	path = g_build_filename (tmp, "photo.jpg", NULL);
	check (g_file_set_contents (path, "x", -1, NULL));
	dir = g_file_new_for_path (tmp);
	file = g_file_new_for_path (path);
	files = g_list_prepend (NULL, file);

	g_timeout_add (300, probe_dialog, &probe);
	nemo_archive_dialog_show (NULL, files, dir, FALSE);
	check (probe.ran);

	g_list_free (files);
	g_object_unref (file);
	g_object_unref (dir);
	g_free (tmp);

	if (failures > 0) {
		g_printerr ("%d check(s) failed\n", failures);
		return EXIT_FAILURE;
	}
	g_print ("PASS\n");
	return EXIT_SUCCESS;
}
