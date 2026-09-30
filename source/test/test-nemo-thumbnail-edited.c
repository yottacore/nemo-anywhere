/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */

/* test-nemo-thumbnail-edited.c - a picture edited before its thumbnail is made.

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

/* A thumbnail job carries the size and time the view last saw. When the file
 * changed after that, the job checksummed the new contents and stored the
 * checksum under the old size. Only the checksum is unique, so every later
 * store of those contents, under any name, was refused, and the picture was
 * made again on every visit.
 *
 * The main loop is not run between the edit and the store, so the view never
 * hears of the edit and the job keeps the old size and time. */

#include <config.h>

#include <string.h>

#include <glib/gstdio.h>
#include <gtk/gtk.h>

#include <libnemo-private/nemo-cache-db.h>
#include <libnemo-private/nemo-directory.h>
#include <libnemo-private/nemo-file.h>
#include <libnemo-private/nemo-file-attributes.h>
#include <libnemo-private/nemo-file-digest.h>
#include <libnemo-private/nemo-file-private.h>
#include <libnemo-private/nemo-global-preferences.h>
#include <libnemo-private/nemo-thumbnails.h>

#include "test-scratch.h"
#include "test-check.h"

static gboolean
save_picture (const char *path, int width, int height, guint32 fill)
{
	GdkPixbuf *pixbuf = gdk_pixbuf_new (GDK_COLORSPACE_RGB, FALSE, 8, width, height);
	gboolean saved;

	gdk_pixbuf_fill (pixbuf, fill);
	saved = gdk_pixbuf_save (pixbuf, path, "png", NULL, NULL);
	g_object_unref (pixbuf);

	return saved;
}

/* Back far enough that the job does not wait out the just-changed delay. */
static gboolean
set_age (GFile *location, gint64 secs_ago)
{
	return g_file_set_attribute_uint64 (location, G_FILE_ATTRIBUTE_TIME_MODIFIED,
					    (guint64) (g_get_real_time () / G_USEC_PER_SEC - secs_ago),
					    G_FILE_QUERY_INFO_NONE, NULL, NULL);
}

static gboolean
stat_id (GFile *location, NemoFileId *id)
{
	g_autoptr (GFileInfo) info = g_file_query_info (location,
							G_FILE_ATTRIBUTE_TIME_MODIFIED ","
							G_FILE_ATTRIBUTE_TIME_MODIFIED_USEC ","
							G_FILE_ATTRIBUTE_STANDARD_SIZE,
							G_FILE_QUERY_INFO_NONE, NULL, NULL);

	memset (id, 0, sizeof (*id));
	if (info == NULL)
		return FALSE;

	id->bytes = g_file_info_get_size (info);
	id->mtime = (gint64) g_file_info_get_attribute_uint64 (info, G_FILE_ATTRIBUTE_TIME_MODIFIED) * G_USEC_PER_SEC
		    + g_file_info_get_attribute_uint32 (info, G_FILE_ATTRIBUTE_TIME_MODIFIED_USEC);

	return TRUE;
}

/* Waits on the thumbnail thread alone, never the main loop. */
static gboolean
wait_until_stored (NemoCacheDb *db, const char *uri, int limit_ms)
{
	int waited;

	for (waited = 0; waited < limit_ms; waited += 10) {
		if (nemo_cache_db_thumbnail_stats (db, uri, NULL, NULL))
			return TRUE;
		g_usleep (10 * 1000);
	}

	return FALSE;
}

static void
test_edited_before_made (NemoFile *file, const char *path)
{
	g_autofree char *uri = nemo_file_get_uri (file);
	g_autofree char *copy_uri = NULL;
	g_autoptr (GFile) location = g_file_new_for_path (path);
	g_autoptr (GBytes) image = g_bytes_new_static ("stand-in", 8);
	NemoCacheDb *db = nemo_cache_db_get ();
	NemoThumbnailRecord record = { 0 };
	NemoFileId seen, now;
	guint8 back[NEMO_CACHE_DIGEST_LEN];

	check (db != NULL);
	if (db == NULL)
		return;

	nemo_thumbnail_file_id (file, &seen);
	check (seen.bytes > 0 && seen.mtime > 0);

	/* A bigger picture, so the size moves as well as the time. */
	check (save_picture (path, 900, 700, 0x993366ff));
	check (set_age (location, 1800));
	check (stat_id (location, &now));
	check (now.bytes != seen.bytes);
	check (now.mtime != seen.mtime);

	nemo_create_thumbnail (file, 128);
	check (wait_until_stored (db, uri, 20000));

	/* No checksum may stand for the size and time the job was queued with,
	 * since that is not what was read. */
	check (!nemo_cache_db_lookup_digest (db, uri, seen.bytes, seen.mtime, back));

	/* What the user sees: the new contents, found at their real size under
	 * another name, go into the store. */
	check (nemo_file_digest_file (location, now.digest, NULL, NULL));
	now.has_digest = TRUE;
	copy_uri = g_strconcat (uri, ".copy", NULL);
	record.size = record.width = record.height = 128;
	record.format = NEMO_THUMBNAIL_FORMAT_PNG;
	check (nemo_cache_db_thumbnail_store (db, copy_uri, &now, &record, image));
}

int
main (int argc, char **argv)
{
	g_autofree char *tmp = NULL;
	g_autofree char *dir = NULL;
	g_autofree char *path = NULL;
	g_autofree char *uri = NULL;
	g_autofree char *file_uri = NULL;
	g_autoptr (GFile) location = NULL;
	NemoDirectory *directory;
	NemoFile *file;
	int client, i;

	tmp = test_scratch_config_home ("nemo-thumbnail-edited-XXXXXX");
	if (tmp == NULL) {
		g_printerr ("could not make a scratch dir\n");
		return 77;
	}

	gtk_init_check (&argc, &argv);
	nemo_global_preferences_init ();

	nemo_config_set_int (nemo_preferences, NEMO_PREFERENCES_DEFERRED_ATTR_PRELOAD_LIMIT, 0);
	nemo_config_set_int (nemo_preferences, NEMO_PREFERENCES_MAX_THUMBNAIL_THREADS, 1);

	dir = g_build_filename (tmp, "pictures", NULL);
	check (g_mkdir (dir, 0755) == 0);

	path = g_build_filename (dir, "photo.png", NULL);
	check (save_picture (path, 300, 200, 0x336699ff));
	location = g_file_new_for_path (path);
	check (set_age (location, 3600));

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

	/* The job has to take the size and time from the view, not read them
	 * itself, or it would see the edit. */
	check (file->details->got_file_info && file->details->file_info_is_up_to_date);
	check (nemo_can_thumbnail_internally (file));

	test_edited_before_made (file, path);

	nemo_file_unref (file);
	nemo_directory_file_monitor_remove (directory, &client);
	nemo_directory_unref (directory);

	if (failures > 0) {
		g_printerr ("%d check(s) failed\n", failures);
		return 1;
	}

	g_print ("OK\n");
	return 0;
}
