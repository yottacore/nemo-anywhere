/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */

/* test-nemo-thumbnail-hold.c - what shows while a thumbnail is on its way.

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

/* Between the type icon and the picture there used to be a third icon, the
 * theme's "image-loading", which few themes have. So a file flashed through a
 * stand-in on its way to a thumbnail, and an edited file dropped its old picture
 * for the type icon before the new one was made. And a folder rendered ahead of
 * time holds its pictures only while the memory for them lasts; past that, one
 * is read back once it is near the view. */

#include <config.h>

#include <string.h>

#include <glib/gstdio.h>
#include <gtk/gtk.h>

#include <libnemo-private/nemo-cache-db.h>
#include <libnemo-private/nemo-directory.h>
#include <libnemo-private/nemo-file.h>
#include <libnemo-private/nemo-file-private.h>
#include <libnemo-private/nemo-global-preferences.h>
#include <libnemo-private/nemo-icon-info.h>
#include <libnemo-private/nemo-thumbnail-memory.h>
#include <libnemo-private/nemo-thumbnails.h>

#include "test-scratch.h"
#include "test-check.h"

static gboolean
wait_until (gboolean (*done) (NemoFile *), NemoFile *file)
{
	int spins;

	for (spins = 0; spins < 5000; spins++) {
		if (done (file)) {
			return TRUE;
		}
		g_main_context_iteration (NULL, FALSE);
		g_usleep (2000);
	}

	return FALSE;
}

static gboolean
not_thumbnailing (NemoFile *file)
{
	return !nemo_file_is_thumbnailing (file);
}

static gboolean
has_thumbnail (NemoFile *file)
{
	return file->details->thumbnail != NULL;
}

/* Each one different, or the store takes them for copies of one file. */
static void
write_image (const char *path, int n)
{
	GdkPixbuf *pixbuf = gdk_pixbuf_new (GDK_COLORSPACE_RGB, FALSE, 8, 40 + n, 30);

	gdk_pixbuf_fill (pixbuf, 0x336699ff + (n << 8));
	check (gdk_pixbuf_save (pixbuf, path, "png", NULL, NULL));
	g_object_unref (pixbuf);
}

static char *
icon_name (NemoFile *file, NemoFileIconFlags flags)
{
	NemoIconInfo *info = nemo_file_get_icon (file, 64, 64, 1, flags);
	char *name = g_strdup (nemo_icon_info_get_used_name (info));

	nemo_icon_info_unref (info);
	return name;
}

static void
test_icon_while_made (NemoFile *file)
{
	NemoFileIconFlags thumbs = NEMO_FILE_ICON_FLAGS_USE_THUMBNAILS |
				   NEMO_FILE_ICON_FLAGS_FORCE_THUMBNAIL_SIZE;
	g_autofree char *plain = icon_name (file, NEMO_FILE_ICON_FLAGS_FORCE_THUMBNAIL_SIZE);
	g_autofree char *waiting = NULL;

	nemo_file_set_is_thumbnailing (file, TRUE);
	waiting = icon_name (file, thumbs);
	nemo_file_set_is_thumbnailing (file, FALSE);

	check (plain != NULL);
	check (g_strcmp0 (plain, waiting) == 0);
}

static void
test_old_picture_kept (NemoFile *file)
{
	NemoFileDetails *details = file->details;
	NemoThumbnailLoaded nothing = { 0 };
	NemoThumbnailLoaded failed = { 0 };
	GdkPixbuf *old = gdk_pixbuf_new (GDK_COLORSPACE_RGB, FALSE, 8, 8, 8);

	/* A picture of the file as it was a second ago. The file takes one
	   reference, and the one kept here is what makes the comparisons below
	   safe once the file lets go. */
	details->thumbnail = g_object_ref (old);
	details->thumbnail_mtime = details->mtime - 1;
	details->thumbnail_is_up_to_date = FALSE;

	/* The store knows nothing of the new version. */
	nemo_file_take_thumbnail (file, &nothing);
	check (details->thumbnail == old);
	check (details->thumbnail_is_up_to_date);

	/* The next draw shows the old one and asks for a new one. */
	check (!nemo_file_is_thumbnailing (file));
	g_free (icon_name (file, NEMO_FILE_ICON_FLAGS_USE_THUMBNAILS |
				 NEMO_FILE_ICON_FLAGS_FORCE_THUMBNAIL_SIZE));
	check (nemo_file_is_thumbnailing (file));
	check (wait_until (not_thumbnailing, file));
	check (details->thumbnail != NULL && details->thumbnail != old);
	check (details->thumbnail_mtime == details->mtime);

	/* A render that failed does not leave a picture of something else. */
	failed.failed = TRUE;
	nemo_file_take_thumbnail (file, &failed);
	check (details->thumbnail == NULL);
	details->thumbnailing_failed = FALSE;

	g_object_unref (old);
}

static gboolean
stored (NemoFile *file)
{
	g_autofree char *uri = nemo_file_get_uri (file);
	NemoFileId id;
	NemoThumbnailRecord record;

	nemo_thumbnail_file_id (file, &id);

	return nemo_cache_db_thumbnail_lookup (nemo_cache_db_get (), uri, &id, &record, NULL) &&
	       record.width > 0;
}

static void
set_memory_gib (double gib)
{
	nemo_config_set_double (nemo_config_get_group (NEMO_FILE_CACHE_GROUP),
				NEMO_FILE_CACHE_MEMORY_GIB, gib);
}

/* With no memory for pictures, only the one in view holds its picture. */
static void
test_ahead (NemoFile *off_screen, NemoFile *scrolled_in)
{
	GList *files = NULL;

	set_memory_gib (0);

	check (nemo_file_wants_thumbnail_ahead (off_screen));
	check (nemo_file_wants_thumbnail_ahead (scrolled_in));

	files = g_list_append (files, off_screen);
	files = g_list_append (files, scrolled_in);
	nemo_thumbnail_render_ahead (files, 128);
	g_list_free (files);

	check (!nemo_file_wants_thumbnail_ahead (off_screen));

	/* One of the two comes into view while it waits. */
	nemo_file_set_load_deferred_attrs (scrolled_in, NEMO_FILE_LOAD_DEFERRED_ATTRS_YES);

	check (wait_until (not_thumbnailing, off_screen));
	check (wait_until (not_thumbnailing, scrolled_in));
	check (wait_until (has_thumbnail, scrolled_in));

	check (stored (off_screen));
	check (off_screen->details->thumbnail == NULL);
	check (nemo_file_wants_thumbnail_near_view (off_screen));

	/* Near the view now: read back and held, memory or not. */
	files = g_list_append (NULL, off_screen);
	nemo_thumbnail_load_near_view (files, 128);
	g_list_free (files);
	check (wait_until (has_thumbnail, off_screen));
	check (!nemo_file_wants_thumbnail_near_view (off_screen));
	nemo_file_forget_held_thumbnail (off_screen);
}

static gboolean
holds_picture (NemoFile *file)
{
	return file->details->thumbnail != NULL && !nemo_file_is_thumbnailing (file);
}

/* With room, a file never on screen holds its picture as soon as it is made,
 * and one made on an earlier visit is read back in its turn. So scrolling
 * finds both drawn already. */
static void
test_ahead_held (NemoFile *made_now, NemoFile *made_before)
{
	GList *files = NULL;

	set_memory_gib (1);

	check (nemo_file_wants_thumbnail_ahead (made_now));
	check (nemo_file_wants_thumbnail_ahead (made_before));
	check (stored (made_before));

	files = g_list_append (files, made_before);
	files = g_list_append (files, made_now);
	nemo_thumbnail_render_ahead (files, 128);
	g_list_free (files);

	check (wait_until (holds_picture, made_before));
	check (wait_until (holds_picture, made_now));
	check (nemo_file_get_load_deferred_attrs (made_now) == NEMO_FILE_LOAD_DEFERRED_ATTRS_NO);
	check (nemo_thumbnail_memory_bytes () > 0);
}

/* The limit offers sizes up to 64 GB. Read at the width of an int, 8 GB
 * truncated to 0 and turned every thumbnail off, and 2 GB went negative and
 * turned the limit off. The file here is a few hundred bytes, so a limit of one
 * byte refuses it and 8 GB must not. It takes the store's answer first, since
 * the limit only applies once the store is known to hold nothing. */
static void
test_size_limit (NemoFile *file)
{
	NemoThumbnailLoaded nothing = { 0 };

	nemo_file_take_thumbnail (file, &nothing);
	check (file->details->thumbnail == NULL && file->details->thumbnail_path == NULL);

	nemo_config_set_int64 (nemo_preferences, NEMO_PREFERENCES_IMAGE_FILE_THUMBNAIL_LIMIT, 1);
	check (!nemo_file_should_show_thumbnail (file));

	nemo_config_set_int64 (nemo_preferences, NEMO_PREFERENCES_IMAGE_FILE_THUMBNAIL_LIMIT,
			       G_GINT64_CONSTANT (8) * 1024 * 1024 * 1024);
	check (nemo_file_should_show_thumbnail (file));

	nemo_config_reset (nemo_preferences, NEMO_PREFERENCES_IMAGE_FILE_THUMBNAIL_LIMIT);
}

int
main (int argc, char **argv)
{
	g_autofree char *tmp = NULL;
	g_autofree char *dir = NULL;
	g_autofree char *uri = NULL;
	NemoDirectory *directory;
	NemoFile *files[4];
	NemoFile *limited;
	int client, i;

	gtk_init_check (&argc, &argv);

	tmp = test_scratch_config_home ("nemo-thumbnail-hold-XXXXXX");
	if (tmp == NULL) {
		g_printerr ("FAIL could not make a temp dir\n");
		return EXIT_FAILURE;
	}

	nemo_global_preferences_init ();

	/* Or the first files in a folder are asked about before anything is
	   drawn, and none of them count as off screen. */
	nemo_config_set_int (nemo_preferences, NEMO_PREFERENCES_DEFERRED_ATTR_PRELOAD_LIMIT, 0);

	dir = g_build_filename (tmp, "pictures", NULL);
	check (g_mkdir (dir, 0755) == 0);
	for (i = 0; i < 5; i++) {
		g_autofree char *name = g_strdup_printf ("shot-%d.png", i);
		g_autofree char *path = g_build_filename (dir, name, NULL);

		write_image (path, i);
	}

	/* A picture changed in the last two seconds is left alone in case it is
	   still being written. Back-dating them skips the wait. */
	for (i = 0; i < 4; i++) {
		g_autofree char *name = g_strdup_printf ("shot-%d.png", i);
		g_autofree char *path = g_build_filename (dir, name, NULL);
		g_autoptr (GFile) location = g_file_new_for_path (path);

		check (g_file_set_attribute_uint64 (location, G_FILE_ATTRIBUTE_TIME_MODIFIED,
						    (guint64) (g_get_real_time () / G_USEC_PER_SEC) - 3600 - i,
						    G_FILE_QUERY_INFO_NONE, NULL, NULL));
	}

	uri = g_filename_to_uri (dir, NULL, NULL);
	directory = nemo_directory_get_by_uri (uri);
	nemo_directory_file_monitor_add (directory, &client, TRUE,
					 NEMO_FILE_ATTRIBUTES_FOR_ICON, NULL, NULL);

	for (i = 0; i < 5000 && !nemo_directory_are_all_files_seen (directory); i++) {
		g_main_context_iteration (NULL, FALSE);
		g_usleep (2000);
	}
	check (nemo_directory_are_all_files_seen (directory));

	for (i = 0; i < 4; i++) {
		g_autofree char *name = g_strdup_printf ("%s/shot-%d.png", uri, i);

		files[i] = nemo_file_get_by_uri (name);
	}

	test_icon_while_made (files[0]);
	test_old_picture_kept (files[0]);
	test_ahead (files[1], files[2]);
	test_ahead_held (files[3], files[1]);

	{
		g_autofree char *name = g_strdup_printf ("%s/shot-4.png", uri);

		limited = nemo_file_get_by_uri (name);
		test_size_limit (limited);
		nemo_file_unref (limited);
	}

	for (i = 0; i < 4; i++) {
		nemo_file_unref (files[i]);
	}
	nemo_directory_file_monitor_remove (directory, &client);
	nemo_directory_unref (directory);

	if (failures == 0)
		g_print ("nemo-thumbnail-hold: all checks passed\n");

	return failures == 0 ? 0 : 1;
}
