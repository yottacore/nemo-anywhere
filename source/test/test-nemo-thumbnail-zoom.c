/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */

/* test-nemo-thumbnail-zoom.c - zooming in while a thumbnail is being made.

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

/* A bigger ask that came in while a smaller one was being made was folded
 * into the running job. The job drew the small picture and stored it as the
 * big one, and since the store then said it had that size, nothing ever
 * asked again. The picture is made here by a thumbnailer that waits for a
 * gate, so the zoom always falls inside the render.
 *
 * Re-runs itself as that thumbnailer when handed --gate. */

#include <config.h>

#include <string.h>

#include <glib/gstdio.h>
#include <gtk/gtk.h>

#include <libnemo-private/nemo-cache-db.h>
#include <libnemo-private/nemo-directory.h>
#include <libnemo-private/nemo-file.h>
#include <libnemo-private/nemo-file-attributes.h>
#include <libnemo-private/nemo-file-private.h>
#include <libnemo-private/nemo-global-preferences.h>
#include <libnemo-private/nemo-thumbnails.h>

#include "test-scratch.h"
#include "test-check.h"

/* Says it started, waits for the gate, then makes the picture at the size
 * it was asked for. */
static int
run_gated_thumbnailer (const char *gate_dir, const char *input, const char *output, const char *size)
{
	g_autofree char *started_name = g_strdup_printf ("started-%s", size);
	g_autofree char *started = g_build_filename (gate_dir, started_name, NULL);
	g_autofree char *open = g_build_filename (gate_dir, "open", NULL);
	GdkPixbuf *pixbuf;
	int spins, pixels = atoi (size);
	gboolean saved;

	if (!g_file_set_contents (started, "", 0, NULL)) {
		g_printerr ("could not write %s\n", started);
	}

	for (spins = 0; spins < 60000 && !g_file_test (open, G_FILE_TEST_EXISTS); spins++) {
		g_usleep (1000);
	}

	pixbuf = gdk_pixbuf_new_from_file_at_scale (input, pixels, pixels, TRUE, NULL);
	if (pixbuf == NULL) {
		return 1;
	}
	saved = gdk_pixbuf_save (pixbuf, output, "png", NULL, NULL);
	g_object_unref (pixbuf);

	return saved ? 0 : 1;
}

static gboolean
spin_until_exists (const char *path, int limit_ms)
{
	int spins;

	for (spins = 0; spins < limit_ms && !g_file_test (path, G_FILE_TEST_EXISTS); spins++) {
		g_main_context_iteration (NULL, FALSE);
		g_usleep (1000);
	}

	return g_file_test (path, G_FILE_TEST_EXISTS);
}

static void
spin (int ms)
{
	int spins;

	for (spins = 0; spins < ms; spins++) {
		g_main_context_iteration (NULL, FALSE);
		g_usleep (1000);
	}
}

static gboolean
spin_until_held_at (NemoFile *file, int longest, int limit_ms)
{
	int spins;

	for (spins = 0; spins < limit_ms; spins++) {
		GdkPixbuf *held = file->details->thumbnail;

		if (held != NULL && !file->details->is_thumbnailing &&
		    MAX (gdk_pixbuf_get_width (held), gdk_pixbuf_get_height (held)) >= longest) {
			return TRUE;
		}
		g_main_context_iteration (NULL, FALSE);
		g_usleep (1000);
	}

	return FALSE;
}

static void
test_zoom_during_render (NemoFile *file, const char *gate_dir)
{
	g_autofree char *started_small = g_build_filename (gate_dir, "started-128", NULL);
	g_autofree char *open = g_build_filename (gate_dir, "open", NULL);
	g_autofree char *uri = nemo_file_get_uri (file);
	GList *list = g_list_append (NULL, file);
	NemoThumbnailRecord record = { 0 };
	NemoFileId id;

	check (nemo_file_wants_thumbnail_ahead (file));

	nemo_thumbnail_render_ahead (list, 128);
	nemo_file_set_load_deferred_attrs (file, NEMO_FILE_LOAD_DEFERRED_ATTRS_YES);
	check (spin_until_exists (started_small, 10000));

	/* The view zooms in while the small one is still being made. The feeder
	   is its own thread; give it the moment it needs to take the ask. */
	nemo_thumbnail_render_ahead (list, 512);
	spin (300);

	check (g_file_set_contents (open, "", 0, NULL));

	check (spin_until_held_at (file, 512, 10000));
	spin (300);

	nemo_thumbnail_file_id (file, &id);
	check (nemo_cache_db_thumbnail_lookup (nemo_cache_db_get (), uri, &id, &record, NULL));
	g_print ("  stored size %d, picture %dx%d\n", record.size, record.width, record.height);
	check (record.size == 512);
	check (MAX (record.width, record.height) >= record.size);

	g_list_free (list);
}

int
main (int argc, char **argv)
{
	g_autofree char *tmp = NULL;
	g_autofree char *dir = NULL;
	g_autofree char *path = NULL;
	g_autofree char *uri = NULL;
	g_autofree char *file_uri = NULL;
	g_autofree char *gate_dir = NULL;
	g_autofree char *thumbnailers = NULL;
	g_autofree char *self = NULL;
	g_autofree char *entry = NULL;
	g_autofree char *entry_path = NULL;
	g_autoptr (GFile) location = NULL;
	GdkPixbuf *pixbuf;
	NemoDirectory *directory;
	NemoFile *file;
	int client, i;

	if (argc == 6 && g_strcmp0 (argv[1], "--gate") == 0) {
		return run_gated_thumbnailer (argv[2], argv[3], argv[4], argv[5]);
	}

	/* Set before any glib call that would cache the real ones, so only the
	   gated thumbnailer below is seen. */
	tmp = test_scratch_config_home ("nemo-thumbnail-zoom-XXXXXX");
	if (tmp == NULL) {
		g_printerr ("FAIL could not make a temp dir\n");
		return EXIT_FAILURE;
	}
	g_setenv ("XDG_DATA_HOME", tmp, TRUE);

	gate_dir = g_build_filename (tmp, "gate", NULL);
	thumbnailers = g_build_filename (tmp, "thumbnailers", NULL);
	dir = g_build_filename (tmp, "pictures", NULL);
	check (g_mkdir (gate_dir, 0755) == 0);
	check (g_mkdir (thumbnailers, 0755) == 0);
	check (g_mkdir (dir, 0755) == 0);

	self = g_canonicalize_filename (argv[0], NULL);
	entry = g_strdup_printf ("[Thumbnailer Entry]\nExec=\"%s\" --gate \"%s\" %%i %%o %%s\nMimeType=image/png;\n",
				 self, gate_dir);
	entry_path = g_build_filename (thumbnailers, "gate.thumbnailer", NULL);
	check (g_file_set_contents (entry_path, entry, -1, NULL));

	gtk_init_check (&argc, &argv);
	nemo_global_preferences_init ();

	nemo_config_set_int (nemo_preferences, NEMO_PREFERENCES_DEFERRED_ATTR_PRELOAD_LIMIT, 0);
	nemo_config_set_int (nemo_preferences, NEMO_PREFERENCES_MAX_THUMBNAIL_THREADS, 1);

	/* Bigger than the zoomed size, or the small picture is all there is. */
	path = g_build_filename (dir, "photo.png", NULL);
	pixbuf = gdk_pixbuf_new (GDK_COLORSPACE_RGB, FALSE, 8, 1200, 800);
	gdk_pixbuf_fill (pixbuf, 0x336699ff);
	check (gdk_pixbuf_save (pixbuf, path, "png", NULL, NULL));
	g_object_unref (pixbuf);

	/* Or it is left alone for being changed in the last two seconds. */
	location = g_file_new_for_path (path);
	check (g_file_set_attribute_uint64 (location, G_FILE_ATTRIBUTE_TIME_MODIFIED,
					    (guint64) (g_get_real_time () / G_USEC_PER_SEC) - 3600,
					    G_FILE_QUERY_INFO_NONE, NULL, NULL));

	uri = g_filename_to_uri (dir, NULL, NULL);
	directory = nemo_directory_get_by_uri (uri);
	nemo_directory_file_monitor_add (directory, &client, TRUE,
					 NEMO_FILE_ATTRIBUTES_FOR_ICON, NULL, NULL);

	for (i = 0; i < 5000 && !nemo_directory_are_all_files_seen (directory); i++) {
		g_main_context_iteration (NULL, FALSE);
		g_usleep (2000);
	}
	check (nemo_directory_are_all_files_seen (directory));

	file_uri = g_filename_to_uri (path, NULL, NULL);
	file = nemo_file_get_by_uri (file_uri);

	test_zoom_during_render (file, gate_dir);

	nemo_file_unref (file);
	nemo_directory_file_monitor_remove (directory, &client);
	nemo_directory_unref (directory);

	if (failures == 0)
		g_print ("nemo-thumbnail-zoom: all checks passed\n");

	return failures == 0 ? 0 : 1;
}
