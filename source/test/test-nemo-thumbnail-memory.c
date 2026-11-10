/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */

/* test-nemo-thumbnail-memory.c - the limit on pictures held ready to draw.

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

/* Past the limit, the picture used longest ago goes first, and one in a
 * folder no view shows goes before any other. A folder that is left keeps
 * its pictures for a while, and opening another folder of pictures lets
 * them go at once. */

#include <config.h>

#include <gtk/gtk.h>

#include <libnemo-private/nemo-directory.h>
#include <libnemo-private/nemo-file.h>
#include <libnemo-private/nemo-file-private.h>
#include <libnemo-private/nemo-global-preferences.h>
#include <libnemo-private/nemo-thumbnail-memory.h>

#include "test-scratch.h"
#include "test-check.h"

/* 100 by 100 with no alpha is 30,000 bytes. */
#define PICTURE_BYTES 30000

static char *dir_a;
static char *dir_b;

static void
set_room_for (double pictures)
{
	nemo_config_set_double (nemo_config_get_group (NEMO_FILE_CACHE_GROUP),
				NEMO_FILE_CACHE_MEMORY_GIB,
				pictures * PICTURE_BYTES / (1024.0 * 1024 * 1024));
}

static NemoFile *
file_in (const char *dir_uri, const char *name)
{
	g_autofree char *uri = g_strdup_printf ("%s/%s", dir_uri, name);

	return nemo_file_get_by_uri (uri);
}

static void
hold (NemoFile *file)
{
	NemoThumbnailLoaded loaded = { 0 };

	loaded.pixbuf = gdk_pixbuf_new (GDK_COLORSPACE_RGB, FALSE, 8, 100, 100);
	loaded.from_store = TRUE;
	loaded.stored_size = 128;
	nemo_file_take_thumbnail (file, &loaded);
	g_object_unref (loaded.pixbuf);

	/* Each one used after the last, whatever the clock's grain. */
	g_usleep (2000);
}

static gboolean
holds (NemoFile *file)
{
	return file->details->thumbnail != NULL;
}

static void
spin (int ms)
{
	gint64 until = g_get_monotonic_time () + ms * 1000;

	while (g_get_monotonic_time () < until) {
		g_main_context_iteration (NULL, FALSE);
		g_usleep (1000);
	}
}

static void
test_used_longest_ago_goes (void)
{
	NemoDirectory *folder = nemo_directory_get_by_uri (dir_a);
	NemoFile *files[6];
	int i;

	nemo_thumbnail_memory_folder_shown (folder);
	set_room_for (4.5);

	for (i = 0; i < 4; i++) {
		g_autofree char *name = g_strdup_printf ("f%d.jpg", i);

		files[i] = file_in (dir_a, name);
		hold (files[i]);
	}
	check (nemo_thumbnail_memory_bytes () == 4 * PICTURE_BYTES);
	check (!nemo_thumbnail_memory_full ());

	/* A fifth is over the limit, and the first one held makes room. */
	files[4] = file_in (dir_a, "f4.jpg");
	hold (files[4]);
	check (!holds (files[0]));
	check (files[0]->details->thumbnail_in_store);
	check (nemo_file_wants_thumbnail_near_view (files[0]));
	for (i = 1; i < 5; i++) {
		check (holds (files[i]));
	}

	/* The second was drawn again, so the third goes in its place. */
	nemo_thumbnail_memory_used (files[1]);
	files[5] = file_in (dir_a, "f5.jpg");
	hold (files[5]);
	check (holds (files[1]));
	check (!holds (files[2]));
	check (nemo_thumbnail_memory_bytes () <= 4 * PICTURE_BYTES);
	check (!nemo_thumbnail_memory_has_room (files[1]->details->thumbnail));

	for (i = 0; i < 6; i++) {
		nemo_file_forget_held_thumbnail (files[i]);
		nemo_file_unref (files[i]);
	}
	check (nemo_thumbnail_memory_bytes () == 0);

	nemo_thumbnail_memory_folder_hidden (folder);
	nemo_directory_unref (folder);
}

/* One in a folder nobody is looking at goes first, even the newest. */
static void
test_hidden_folder_goes_first (void)
{
	NemoDirectory *folder = nemo_directory_get_by_uri (dir_a);
	NemoFile *shown_old = file_in (dir_a, "g0.jpg");
	NemoFile *shown_new = file_in (dir_a, "g1.jpg");
	NemoFile *hidden_newest = file_in (dir_b, "g2.jpg");
	NemoFile *last = file_in (dir_a, "g3.jpg");

	nemo_thumbnail_memory_folder_shown (folder);
	set_room_for (3.5);

	hold (shown_old);
	hold (shown_new);
	hold (hidden_newest);
	hold (last);

	check (!holds (hidden_newest));
	check (holds (shown_old));
	check (holds (shown_new));
	check (holds (last));

	nemo_file_forget_held_thumbnail (shown_old);
	nemo_file_forget_held_thumbnail (shown_new);
	nemo_file_forget_held_thumbnail (last);
	nemo_file_unref (shown_old);
	nemo_file_unref (shown_new);
	nemo_file_unref (hidden_newest);
	nemo_file_unref (last);

	nemo_thumbnail_memory_folder_hidden (folder);
	nemo_directory_unref (folder);
}

/* Nothing else keeps a ref on the file, so without the folder's minute it would go at
 * once, picture and all. */
static void
test_left_folder_kept (void)
{
	NemoDirectory *folder = nemo_directory_get_by_uri (dir_b);
	NemoFile *file = file_in (dir_b, "k0.jpg");
	NemoFile *weak = file;

	set_room_for (100);
	nemo_thumbnail_memory_set_times (100, 0);

	nemo_thumbnail_memory_folder_shown (folder);
	hold (file);
	nemo_thumbnail_memory_folder_hidden (folder);
	g_object_add_weak_pointer (G_OBJECT (file), (gpointer *) &weak);
	nemo_file_unref (file);

	check (weak != NULL && holds (weak));

	spin (400);
	check (weak == NULL);
	check (nemo_thumbnail_memory_bytes () == 0);

	/* Back before the time is up: the picture is still there, and stays. */
	file = file_in (dir_b, "k1.jpg");
	nemo_thumbnail_memory_folder_shown (folder);
	hold (file);
	nemo_thumbnail_memory_folder_hidden (folder);
	nemo_thumbnail_memory_folder_shown (folder);
	spin (400);
	check (holds (file));
	nemo_thumbnail_memory_folder_hidden (folder);
	nemo_file_unref (file);
	spin (400);
	check (nemo_thumbnail_memory_bytes () == 0);

	/* Another folder of pictures opened: the left one lets go now. */
	nemo_thumbnail_memory_set_times (60 * 1000, 0);
	file = file_in (dir_b, "k2.jpg");
	weak = file;
	g_object_add_weak_pointer (G_OBJECT (file), (gpointer *) &weak);
	nemo_thumbnail_memory_folder_shown (folder);
	hold (file);
	nemo_thumbnail_memory_folder_hidden (folder);
	nemo_file_unref (file);
	check (weak != NULL && holds (weak));

	nemo_thumbnail_memory_flush_hidden ();
	check (weak == NULL);
	check (nemo_thumbnail_memory_bytes () == 0);

	nemo_directory_unref (folder);
}

int
main (int argc, char **argv)
{
	g_autofree char *tmp = NULL;
	g_autofree char *a = NULL;
	g_autofree char *b = NULL;

	gtk_init_check (&argc, &argv);

	tmp = test_scratch_config_home ("nemo-thumbnail-memory-XXXXXX");
	if (tmp == NULL) {
		g_printerr ("FAIL could not make a temp dir\n");
		return EXIT_FAILURE;
	}

	nemo_global_preferences_init ();
	nemo_thumbnail_memory_set_times (60 * 1000, 0);

	a = g_build_filename (tmp, "a", NULL);
	b = g_build_filename (tmp, "b", NULL);
	dir_a = g_filename_to_uri (a, NULL, NULL);
	dir_b = g_filename_to_uri (b, NULL, NULL);

	test_used_longest_ago_goes ();
	test_hidden_folder_goes_first ();
	test_left_folder_kept ();

	g_free (dir_a);
	g_free (dir_b);

	if (failures == 0)
		g_print ("nemo-thumbnail-memory: all checks passed\n");

	return failures == 0 ? 0 : 1;
}
