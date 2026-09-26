/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */

/* test-nemo-clipboard.c - a drag that leaves a cut or copy alone.

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

/* A drag of files that are also on the clipboard clears it, since a cut of
 * files that have just moved would paste nothing. The check compared the
 * dragged list against itself, so every drag cleared the clipboard. It has to
 * look at what is on the clipboard. Needs a display and skips without one.
 * POSIX only: on Windows the files go on the clipboard through the system's
 * own calls, which this does not drive. */

#include <config.h>

#include <stdlib.h>
#include <gtk/gtk.h>

#include <libnemo-private/nemo-clipboard.h>
#include <libnemo-private/nemo-file.h>
#include <libnemo-private/nemo-global-preferences.h>

#include "test-scratch.h"
#include "test-check.h"

#define COPIED_FILES "x-special/gnome-copied-files"

static gboolean
clipboard_holds_files (GtkWidget *widget)
{
	GtkSelectionData *data;
	gboolean          held;

	data = gtk_clipboard_wait_for_contents (nemo_clipboard_get (widget),
						gdk_atom_intern_static_string (COPIED_FILES));
	held = data != NULL && gtk_selection_data_get_length (data) > 0;
	if (data != NULL) {
		gtk_selection_data_free (data);
	}

	return held;
}

static char *
make_file (const char *dir, const char *name)
{
	char *path = g_build_filename (dir, name, NULL);
	char *uri;

	check (g_file_set_contents (path, "x", -1, NULL));
	uri = g_filename_to_uri (path, NULL, NULL);
	g_free (path);

	return uri;
}

int
main (int argc, char *argv[])
{
	GtkWidget *window;
	GList     *files, *dragged;
	GdkAtom    atom;
	char      *tmp, *copied, *other;

	tmp = test_scratch_config_home ("nemo-clipboard-XXXXXX");
	if (!gtk_init_check (&argc, &argv)) {
		g_print ("SKIP: no display\n");
		g_free (tmp);
		return 77;
	}
	nemo_global_preferences_init ();

	window = gtk_window_new (GTK_WINDOW_TOPLEVEL);
	atom = gdk_atom_intern_static_string (COPIED_FILES);
	copied = make_file (tmp, "copied.txt");
	other = make_file (tmp, "other.txt");

	files = g_list_append (NULL, nemo_file_get_by_uri (copied));
	nemo_clipboard_set_files (window, files, TRUE);
	check (clipboard_holds_files (window));

	/* Dragging something else leaves the cut where it is. */
	dragged = g_list_append (NULL, other);
	nemo_clipboard_clear_if_colliding_uris (window, dragged, atom);
	check (clipboard_holds_files (window));
	g_list_free (dragged);

	/* Dragging the file that was cut clears it. */
	dragged = g_list_append (NULL, copied);
	nemo_clipboard_clear_if_colliding_uris (window, dragged, atom);
	check (!clipboard_holds_files (window));
	g_list_free (dragged);

	nemo_file_list_free (files);
	gtk_widget_destroy (window);
	g_free (other);
	g_free (copied);
	g_free (tmp);

	if (failures == 0) {
		g_print ("nemo-clipboard: all checks passed\n");
	}

	return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
