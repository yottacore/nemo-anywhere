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
 * own calls, which this does not drive. On macOS GTK can't read back what it
 * offered, so the reads go through nemo's own. */

#include <config.h>

#include <stdlib.h>
#include <string.h>
#include <gtk/gtk.h>

#include <libnemo-private/nemo-clipboard.h>
#include <libnemo-private/nemo-file.h>
#include <libnemo-private/nemo-global-preferences.h>

#include "test-scratch.h"
#include "test-check.h"

#define COPIED_FILES "x-special/gnome-copied-files"

/* Returns: (transfer full): the uris on the clipboard as nemo reads them */
static GList *
clipboard_files (GtkWidget *widget,
		 gboolean  *cut)
{
	GtkSelectionData *data;
	GdkAtom           atom = gdk_atom_intern_static_string (COPIED_FILES);
	GList            *uris;

	data = nemo_clipboard_wait_for_contents (nemo_clipboard_get (widget), atom);
	if (data == NULL) {
		return NULL;
	}

	uris = nemo_clipboard_get_uri_list_from_selection_data (data, cut, atom);
	gtk_selection_data_free (data);

	return uris;
}

static gboolean
clipboard_holds_files (GtkWidget *widget)
{
	GList   *uris = clipboard_files (widget, NULL);
	gboolean held = uris != NULL;

	g_list_free_full (uris, g_free);

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

/* Each window is its own process, so a paste in another window is this. */
static int
read_as_another_window (void)
{
	GtkWidget *window = gtk_window_new (GTK_WINDOW_TOPLEVEL);
	gboolean   cut = FALSE;
	GList     *uris, *l;

	uris = clipboard_files (window, &cut);
	g_print ("%s\n", cut ? "cut" : "copy");
	for (l = uris; l != NULL; l = l->next) {
		g_print ("%s\n", (char *) l->data);
	}
	g_list_free_full (uris, g_free);
	gtk_widget_destroy (window);

	return EXIT_SUCCESS;
}

static void
reader_done (G_GNUC_UNUSED GPid pid,
	     G_GNUC_UNUSED gint status,
	     gpointer           user_data)
{
	*(gboolean *) user_data = TRUE;
}

static gboolean
reader_too_slow (gpointer user_data)
{
	*(gboolean *) user_data = TRUE;

	return G_SOURCE_REMOVE;
}

/* Returns: (transfer full): what another process read, or NULL */
static char *
another_window_reads (const char *self)
{
	char    *argv[] = { (char *) self, (char *) "--read", NULL };
	GPid     pid;
	gint     out_fd;
	gboolean done = FALSE, late = FALSE;
	guint    timer;
	GIOChannel *out;
	char    *text = NULL;

	if (!g_spawn_async_with_pipes (NULL, argv, NULL, G_SPAWN_DO_NOT_REAP_CHILD,
				       NULL, NULL, &pid, NULL, &out_fd, NULL, NULL)) {
		return NULL;
	}

	/* The reader waits on this process to hand the data over. */
	g_child_watch_add (pid, reader_done, &done);
	timer = g_timeout_add_seconds (20, reader_too_slow, &late);
	while (!done && !late) {
		g_main_context_iteration (NULL, TRUE);
	}
	if (!late) {
		g_source_remove (timer);
	}

	out = g_io_channel_unix_new (out_fd);
	g_io_channel_set_close_on_unref (out, TRUE);
	if (done) {
		g_io_channel_read_to_end (out, &text, NULL, NULL);
	}
	g_io_channel_unref (out);
	g_spawn_close_pid (pid);

	return text;
}

int
main (int argc, char *argv[])
{
	GtkWidget *window;
	GList     *files, *dragged, *held;
	GdkAtom    atom;
	gboolean   cut = FALSE;
	char      *tmp, *copied, *other, *self, *got, *want;

	if (argc > 1 && strcmp (argv[1], "--read") == 0) {
		return gtk_init_check (&argc, &argv) ? read_as_another_window () : 77;
	}

	self = g_strdup (argv[0]);
	tmp = test_scratch_config_home ("nemo-clipboard-XXXXXX");
	if (!gtk_init_check (&argc, &argv)) {
		g_print ("SKIP: no display\n");
		g_free (self);
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
	held = clipboard_files (window, &cut);
	check (held != NULL && held->next == NULL && g_strcmp0 (held->data, copied) == 0);
	check (cut);
	g_list_free_full (held, g_free);

	got = another_window_reads (self);
	want = g_strdup_printf ("cut\n%s\n", copied);
	check (g_strcmp0 (got, want) == 0);
	if (g_strcmp0 (got, want) != 0) {
		g_printerr ("another window read [%s]\n", got != NULL ? got : "(nothing)");
	}
	g_free (want);
	g_free (got);

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
	g_free (self);
	g_free (tmp);

	if (failures == 0) {
		g_print ("nemo-clipboard: all checks passed\n");
	}

	return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
