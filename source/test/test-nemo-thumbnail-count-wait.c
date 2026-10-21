/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */

/* test-nemo-thumbnail-count-wait.c - a slow sub-folder count holds back no
   picture's thumbnail lookup.

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

/* A folder's work queue used to stop at a sub-folder still being counted, so
 * the pictures behind it had no thumbnail lookup until the count was in, and
 * the view holds a picture's row for its lookup. The sub-folder's listing is
 * held here for as long as the test likes, and the pictures queued after it
 * must still be looked up. */

/* RTLD_NEXT. */
#define _GNU_SOURCE

#include <config.h>

#include <dlfcn.h>

#include <glib/gstdio.h>
#include <gtk/gtk.h>

#include <libnemo-private/nemo-directory.h>
#include <libnemo-private/nemo-file.h>
#include <libnemo-private/nemo-file-attributes.h>
#include <libnemo-private/nemo-global-preferences.h>

#include "test-scratch.h"
#include "test-check.h"

#define PICTURES 4

typedef void (*NextFilesFunc) (GFileEnumerator *, int, int, GCancellable *,
			       GAsyncReadyCallback, gpointer);

static NextFilesFunc real_next_files;
static gboolean holding;
static gboolean held;
static GFileEnumerator *held_enumerator;
static int held_num_files;
static int held_priority;
static GCancellable *held_cancellable;
static GAsyncReadyCallback held_callback;
static gpointer held_user_data;

static int thumbnails_in;
static gboolean count_in;

/* Linked ahead of GIO, so the count's own calls come here. Only the listing
   of "slow" is held, and only while the test says so. */
void
g_file_enumerator_next_files_async (GFileEnumerator *enumerator, int num_files, int io_priority,
				    GCancellable *cancellable, GAsyncReadyCallback callback,
				    gpointer user_data)
{
	g_autofree char *name = NULL;

	if (real_next_files == NULL) {
		real_next_files = (NextFilesFunc) dlsym (RTLD_NEXT, "g_file_enumerator_next_files_async");
	}
	name = g_file_get_basename (g_file_enumerator_get_container (enumerator));
	if (holding && !held && g_strcmp0 (name, "slow") == 0) {
		held = TRUE;
		held_enumerator = g_object_ref (enumerator);
		held_num_files = num_files;
		held_priority = io_priority;
		held_cancellable = cancellable != NULL ? g_object_ref (cancellable) : NULL;
		held_callback = callback;
		held_user_data = user_data;
		return;
	}
	real_next_files (enumerator, num_files, io_priority, cancellable, callback, user_data);
}

static void
release_held (void)
{
	holding = FALSE;
	if (held_enumerator == NULL) {
		return;
	}
	real_next_files (held_enumerator, held_num_files, held_priority, held_cancellable,
			 held_callback, held_user_data);
	g_clear_object (&held_enumerator);
	g_clear_object (&held_cancellable);
}

static void
thumbnail_ready (G_GNUC_UNUSED NemoFile *file, G_GNUC_UNUSED gpointer data)
{
	thumbnails_in++;
}

static void
count_ready (G_GNUC_UNUSED NemoFile *file, G_GNUC_UNUSED gpointer data)
{
	count_in = TRUE;
}

static gboolean
spin_until (gboolean *flag, int *counter, int wanted, int limit_ms)
{
	int spins;

	for (spins = 0; spins < limit_ms; spins++) {
		if ((flag != NULL && *flag) || (counter != NULL && *counter >= wanted)) {
			return TRUE;
		}
		g_main_context_iteration (NULL, FALSE);
		g_usleep (1000);
	}
	return (flag != NULL && *flag) || (counter != NULL && *counter >= wanted);
}

/* Each one different, or the store takes them for copies of one file. */
static void
write_image (const char *path, int n)
{
	GdkPixbuf *pixbuf = gdk_pixbuf_new (GDK_COLORSPACE_RGB, FALSE, 8, 64 + n, 48);
	g_autoptr (GFile) location = g_file_new_for_path (path);

	gdk_pixbuf_fill (pixbuf, 0x336699ff + (n << 8));
	check (gdk_pixbuf_save (pixbuf, path, "png", NULL, NULL));
	g_object_unref (pixbuf);

	/* Back-dated, or a picture changed in the last two seconds is left
	   alone in case it's still being written. */
	check (g_file_set_attribute_uint64 (location, G_FILE_ATTRIBUTE_TIME_MODIFIED,
					    (guint64) (g_get_real_time () / G_USEC_PER_SEC) - 3600 - n,
					    G_FILE_QUERY_INFO_NONE, NULL, NULL));
}

int
main (int argc, char **argv)
{
	g_autofree char *tmp = NULL;
	g_autofree char *dir = NULL;
	g_autofree char *slow_dir = NULL;
	g_autofree char *inner = NULL;
	g_autofree char *uri = NULL;
	g_autofree char *slow_uri = NULL;
	NemoFile *pictures[PICTURES];
	NemoDirectory *directory;
	NemoFile *slow;
	guint count = 0;
	gboolean unreadable = FALSE;
	int client, i;

	gtk_init_check (&argc, &argv);

	tmp = test_scratch_config_home ("nemo-thumbnail-count-wait-XXXXXX");
	if (tmp == NULL) {
		g_printerr ("FAIL could not make a temp dir\n");
		return EXIT_FAILURE;
	}

	nemo_global_preferences_init ();

	dir = g_build_filename (tmp, "box", NULL);
	slow_dir = g_build_filename (dir, "slow", NULL);
	inner = g_build_filename (slow_dir, "one.txt", NULL);
	check (g_mkdir_with_parents (slow_dir, 0755) == 0);
	check (g_file_set_contents (inner, "1", -1, NULL));
	for (i = 0; i < PICTURES; i++) {
		g_autofree char *name = g_strdup_printf ("pic-%d.png", i);
		g_autofree char *path = g_build_filename (dir, name, NULL);

		write_image (path, i);
	}

	uri = g_filename_to_uri (dir, NULL, NULL);
	directory = nemo_directory_get_by_uri (uri);
	nemo_directory_file_monitor_add (directory, &client, TRUE, NEMO_FILE_ATTRIBUTE_INFO, NULL, NULL);
	for (i = 0; i < 5000 && !nemo_directory_are_all_files_seen (directory); i++) {
		g_main_context_iteration (NULL, FALSE);
		g_usleep (1000);
	}
	check (nemo_directory_are_all_files_seen (directory));

	slow_uri = g_filename_to_uri (slow_dir, NULL, NULL);
	slow = nemo_file_get_by_uri (slow_uri);
	check (nemo_file_is_directory (slow));
	for (i = 0; i < PICTURES; i++) {
		g_autofree char *name = g_strdup_printf ("%s/pic-%d.png", uri, i);

		pictures[i] = nemo_file_get_by_uri (name);
		check (!nemo_file_check_if_ready (pictures[i], NEMO_FILE_ATTRIBUTE_THUMBNAIL));
	}

	/* The count goes on the queue first and sticks in its listing. */
	holding = TRUE;
	nemo_file_call_when_ready (slow, NEMO_FILE_ATTRIBUTE_DIRECTORY_ITEM_COUNT, count_ready, NULL);
	check (spin_until (&held, NULL, 0, 5000));

	for (i = 0; i < PICTURES; i++) {
		nemo_file_call_when_ready (pictures[i], NEMO_FILE_ATTRIBUTE_THUMBNAIL, thumbnail_ready, NULL);
	}

	/* The first lookup in a process opens the store, so up to a second or
	   so on a busy box. Nothing's looked up at all when they wait. */
	if (!spin_until (NULL, &thumbnails_in, PICTURES, 10000 * (int) test_slowness ())) {
		g_printerr ("  %d of %d pictures looked up while the count was held\n",
			    thumbnails_in, PICTURES);
	}
	check (thumbnails_in == PICTURES);
	check (!count_in);

	/* And the count still comes in once its listing does. */
	release_held ();
	check (spin_until (&count_in, NULL, 0, 5000));
	check (nemo_file_get_directory_item_count (slow, &count, &unreadable));
	check (count == 1 && !unreadable);

	for (i = 0; i < PICTURES; i++) {
		if (thumbnails_in < PICTURES) {
			nemo_file_cancel_call_when_ready (pictures[i], thumbnail_ready, NULL);
		}
		nemo_file_unref (pictures[i]);
	}
	if (!count_in) {
		nemo_file_cancel_call_when_ready (slow, count_ready, NULL);
	}
	nemo_file_unref (slow);
	nemo_directory_file_monitor_remove (directory, &client);
	nemo_directory_unref (directory);

	if (failures == 0)
		g_print ("nemo-thumbnail-count-wait: all checks passed\n");

	return failures == 0 ? 0 : 1;
}
