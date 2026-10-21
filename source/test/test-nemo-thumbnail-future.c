/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */

/* test-nemo-thumbnail-future.c - a picture dated ahead of the clock gets a thumbnail.

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

/* A file changed in the last 2 seconds isn't thumbnailed yet, and is looked
 * at again 2 seconds later. A date in the future always read as just changed,
 * so such a picture was looked at every 2 seconds for as long as it was shown
 * and never got a thumbnail (2026101419191455). One dated next year has to be
 * stored, and one written this second still has to wait. */

#include <config.h>

#include <glib/gstdio.h>
#include <gtk/gtk.h>

#include <libnemo-private/nemo-cache-db.h>
#include <libnemo-private/nemo-directory.h>
#include <libnemo-private/nemo-file.h>
#include <libnemo-private/nemo-file-attributes.h>
#include <libnemo-private/nemo-global-preferences.h>
#include <libnemo-private/nemo-thumbnails.h>

#include "test-scratch.h"
#include "test-check.h"

static gboolean
save_picture (const char *path, guint32 fill)
{
	GdkPixbuf *pixbuf = gdk_pixbuf_new (GDK_COLORSPACE_RGB, FALSE, 8, 300, 200);
	gboolean saved;

	gdk_pixbuf_fill (pixbuf, fill);
	saved = gdk_pixbuf_save (pixbuf, path, "png", NULL, NULL);
	g_object_unref (pixbuf);

	return saved;
}

static gboolean
set_mtime (const char *path, gint64 secs_from_now)
{
	g_autoptr (GFile) location = g_file_new_for_path (path);

	return g_file_set_attribute_uint64 (location, G_FILE_ATTRIBUTE_TIME_MODIFIED,
					    (guint64) (g_get_real_time () / G_USEC_PER_SEC + secs_from_now),
					    G_FILE_QUERY_INFO_NONE, NULL, NULL);
}

/* The thumbnail thread alone, never the main loop, so a job put off for being
   new isn't tried again in here. */
static gboolean
wait_until_stored (NemoCacheDb *db, const char *uri, int limit_ms)
{
	for (int waited = 0; waited < limit_ms; waited += 10) {
		if (nemo_cache_db_thumbnail_stats (db, uri, NULL, NULL)) {
			return TRUE;
		}
		g_usleep (10 * 1000);
	}

	return FALSE;
}

static NemoFile *
loaded_file (const char *path)
{
	g_autofree char *uri = g_filename_to_uri (path, NULL, NULL);

	return nemo_file_get_by_uri (uri);
}

int
main (int argc, char **argv)
{
	g_autofree char *tmp = NULL;
	g_autofree char *dir = NULL;
	g_autofree char *dir_uri = NULL;
	g_autofree char *ahead = NULL;
	g_autofree char *fresh = NULL;
	g_autofree char *ahead_uri = NULL;
	g_autofree char *fresh_uri = NULL;
	NemoDirectory *directory;
	NemoFile *ahead_file, *fresh_file;
	NemoCacheDb *db;
	int client, i;

	tmp = test_scratch_config_home ("nemo-thumbnail-future-XXXXXX");
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
	ahead = g_build_filename (dir, "next year.png", NULL);
	fresh = g_build_filename (dir, "just saved.png", NULL);
	check (save_picture (ahead, 0x336699ff));
	check (save_picture (fresh, 0x993366ff));
	check (set_mtime (ahead, 365 * 24 * 3600));

	dir_uri = g_filename_to_uri (dir, NULL, NULL);
	directory = nemo_directory_get_by_uri (dir_uri);
	nemo_directory_file_monitor_add (directory, &client, TRUE,
					 NEMO_FILE_ATTRIBUTES_FOR_ICON, NULL, NULL);
	for (i = 0; i < 5000 && !nemo_directory_are_all_files_seen (directory); i++) {
		g_main_context_iteration (NULL, FALSE);
		g_usleep (2000);
	}
	check (nemo_directory_are_all_files_seen (directory));

	ahead_file = loaded_file (ahead);
	fresh_file = loaded_file (fresh);
	ahead_uri = nemo_file_get_uri (ahead_file);
	fresh_uri = nemo_file_get_uri (fresh_file);
	check (nemo_can_thumbnail_internally (ahead_file));

	db = nemo_cache_db_get ();
	check (db != NULL);
	if (db != NULL) {
		/* Fresh goes first, so by the time next year's is stored the
		   thread has already passed it over. */
		nemo_create_thumbnail (fresh_file, 128);
		nemo_create_thumbnail (ahead_file, 128);
		check (wait_until_stored (db, ahead_uri, 20000));
		check (!nemo_cache_db_thumbnail_stats (db, fresh_uri, NULL, NULL));
	}

	nemo_file_unref (fresh_file);
	nemo_file_unref (ahead_file);
	nemo_directory_file_monitor_remove (directory, &client);
	nemo_directory_unref (directory);

	if (failures > 0) {
		g_printerr ("%d check(s) failed\n", failures);
		return 1;
	}

	g_print ("OK\n");
	return 0;
}
