/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */

/* test-nemo-thumbnail-order.c - a folder's thumbnails are made in its order.

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

/* Scrolling to the bottom of a folder of pictures used to get the bottom
 * ones made first, and scrolling back up the ones there next. A file that
 * came into view jumped the queue. Now the folder goes top down whatever is
 * looked at, and a picture stored on an earlier visit waits its turn too.
 * One render thread, so the order things finish in is the queue's. */

#include <config.h>

#include <string.h>

#include <glib/gstdio.h>
#include <gtk/gtk.h>

#include <libnemo-private/nemo-directory.h>
#include <libnemo-private/nemo-file.h>
#include <libnemo-private/nemo-file-attributes.h>
#include <libnemo-private/nemo-file-private.h>
#include <libnemo-private/nemo-global-preferences.h>
#include <libnemo-private/nemo-thumbnails.h>

#include "test-scratch.h"
#include "test-check.h"

#define COUNT 16

static NemoFile *files[COUNT];
static int finished[COUNT];
static int finished_count;
static gboolean counting;

/* BMP, or a PNG with no compression where gdk-pixbuf has no BMP writer, as
   on FreeBSD. Either is the same size for any picture of the same
   dimensions. */
static const char *twin_ext = "bmp";

static gboolean
can_write_bmp (void)
{
	GSList *formats = gdk_pixbuf_get_formats ();
	GSList *l;
	gboolean found = FALSE;

	for (l = formats; l != NULL && !found; l = l->next) {
		g_autofree char *name = gdk_pixbuf_format_get_name (l->data);

		found = g_strcmp0 (name, "bmp") == 0 && gdk_pixbuf_format_is_writable (l->data);
	}
	g_slist_free (formats);

	return found;
}

static void
file_changed (NemoFile *file, gpointer data)
{
	int i, n = GPOINTER_TO_INT (data);

	if (!counting || file->details->thumbnail == NULL || file->details->is_thumbnailing) {
		return;
	}

	for (i = 0; i < finished_count; i++) {
		if (finished[i] == n) {
			return;
		}
	}

	if (finished_count < COUNT) {
		finished[finished_count++] = n;
	}
}

static void
start_counting (void)
{
	finished_count = 0;
	counting = TRUE;
}

static gboolean
wait_for_all (void)
{
	int spins;

	for (spins = 0; spins < 10000 && finished_count < COUNT; spins++) {
		g_main_context_iteration (NULL, FALSE);
		g_usleep (1000);
	}
	counting = FALSE;

	return finished_count == COUNT;
}

static gboolean
finished_in_order (void)
{
	int i;

	for (i = 0; i < finished_count; i++) {
		if (finished[i] != i) {
			g_printerr ("  finished %d at %d\n", finished[i], i);
			return FALSE;
		}
	}

	return finished_count == COUNT;
}

/* Big enough that making one takes a moment, and each one different, or the
 * store takes them for copies of one file. */
static void
write_image (const char *path, int n)
{
	GdkPixbuf *pixbuf = gdk_pixbuf_new (GDK_COLORSPACE_RGB, FALSE, 8, 600 + n, 400);

	gdk_pixbuf_fill (pixbuf, 0x336699ff + (n << 8));
	check (gdk_pixbuf_save (pixbuf, path, "png", NULL, NULL));
	g_object_unref (pixbuf);
}

static GList *
all_files (void)
{
	GList *list = NULL;
	int i;

	for (i = COUNT - 1; i >= 0; i--) {
		list = g_list_prepend (list, files[i]);
	}

	return list;
}

/* The last files come into view while the folder is still being made, and
 * ask for their thumbnails, bottom one first. */
static void
test_scrolled_to_bottom (void)
{
	GList *list = all_files ();
	int i;

	for (i = 0; i < COUNT; i++) {
		check (nemo_file_wants_thumbnail_ahead (files[i]));
	}

	start_counting ();
	nemo_thumbnail_render_ahead (list, 128);
	g_list_free (list);

	for (i = 0; i < COUNT; i++) {
		nemo_file_set_load_deferred_attrs (files[i], NEMO_FILE_LOAD_DEFERRED_ATTRS_YES);
	}
	for (i = COUNT - 1; i >= COUNT - 4; i--) {
		nemo_create_thumbnail (files[i], 128);
	}

	check (wait_for_all ());
	check (finished_in_order ());
}

/* Zoomed in with every picture already drawn: each one asks for a bigger
 * one as it is drawn again, in whatever order that happens. They are made in
 * the folder's order, which placing the folder again set. */
static void
test_zoomed_in (void)
{
	static const int asked[COUNT] = { 0, 9, 3, 15, 1, 12, 7, 5, 14, 2, 11, 6, 13, 4, 10, 8 };
	GList *list = all_files ();
	int i;

	nemo_thumbnail_render_ahead (list, 256);
	g_list_free (list);

	for (i = 0; i < COUNT; i++) {
		check (!nemo_file_is_thumbnailing (files[i]));
	}

	start_counting ();
	for (i = 0; i < COUNT; i++) {
		nemo_create_thumbnail (files[asked[i]], 256);
	}

	check (wait_for_all ());
	check (finished_in_order ());
}

static gboolean
spin_until_thumbnail (NemoFile *file, int limit_ms)
{
	int spins;

	for (spins = 0; spins < limit_ms; spins++) {
		if (file->details->thumbnail != NULL) {
			return TRUE;
		}
		g_main_context_iteration (NULL, FALSE);
		g_usleep (1000);
	}

	return file->details->thumbnail != NULL;
}

/* Stored on an earlier visit, but still waiting in the queue: scrolling to
 * it does not read it out of turn. */
static void
test_stored_waits_its_turn (void)
{
	NemoFile *file = files[COUNT - 1];

	nemo_file_forget_held_thumbnail (file);
	check (file->details->thumbnail == NULL);

	nemo_file_set_is_thumbnailing (file, TRUE);
	nemo_file_invalidate_attributes (file, NEMO_FILE_ATTRIBUTE_THUMBNAIL);
	check (!spin_until_thumbnail (file, 300));

	/* Its turn came. */
	nemo_file_set_is_thumbnailing (file, FALSE);
	nemo_file_invalidate_attributes (file, NEMO_FILE_ATTRIBUTE_THUMBNAIL);
	check (spin_until_thumbnail (file, 5000));
}

static gboolean
wait_for_thumbnail (NemoFile *file)
{
	int spins;

	for (spins = 0; spins < 5000; spins++) {
		if (file->details->thumbnail != NULL && !file->details->is_thumbnailing) {
			return TRUE;
		}
		g_main_context_iteration (NULL, FALSE);
		g_usleep (1000);
	}

	return FALSE;
}

static guint8
red_of (NemoFile *file)
{
	return gdk_pixbuf_read_pixels (file->details->thumbnail)[0];
}

/* Two pictures the same size to the byte and changed at the same moment,
 * which is only a guess that they are one file. A queued folder asks the
 * store about every file in it, and the second one must be made from its own
 * pixels rather than handed the first one's. */
static void
test_same_size_and_time (const char *dir_uri)
{
	g_autofree char *uri_red = g_strdup_printf ("%s/twin-a.%s", dir_uri, twin_ext);
	g_autofree char *uri_blue = g_strdup_printf ("%s/twin-b.%s", dir_uri, twin_ext);
	NemoFile *red = nemo_file_get_by_uri (uri_red);
	NemoFile *blue = nemo_file_get_by_uri (uri_blue);
	GList *list;

	check (red->details->size == blue->details->size);
	check (red->details->mtime == blue->details->mtime);

	/* In view once queued, so what is made is held where it can be seen. */
	list = g_list_append (NULL, red);
	nemo_thumbnail_render_ahead (list, 128);
	g_list_free (list);
	nemo_file_set_load_deferred_attrs (red, NEMO_FILE_LOAD_DEFERRED_ATTRS_YES);
	check (wait_for_thumbnail (red));

	list = g_list_append (NULL, blue);
	nemo_thumbnail_render_ahead (list, 128);
	g_list_free (list);
	nemo_file_set_load_deferred_attrs (blue, NEMO_FILE_LOAD_DEFERRED_ATTRS_YES);
	check (wait_for_thumbnail (blue));

	check (red->details->thumbnail != NULL && red_of (red) > 200);
	check (blue->details->thumbnail != NULL && red_of (blue) < 50);

	nemo_file_unref (red);
	nemo_file_unref (blue);
}

int
main (int argc, char **argv)
{
	g_autofree char *tmp = NULL;
	g_autofree char *dir = NULL;
	g_autofree char *uri = NULL;
	NemoDirectory *directory;
	guint64 twin_time;
	int client, i;

	gtk_init_check (&argc, &argv);

	tmp = test_scratch_config_home ("nemo-thumbnail-order-XXXXXX");
	if (tmp == NULL) {
		g_printerr ("FAIL could not make a temp dir\n");
		return EXIT_FAILURE;
	}

	nemo_global_preferences_init ();

	/* Or the first files are read before anything is drawn. */
	nemo_config_set_int (nemo_preferences, NEMO_PREFERENCES_DEFERRED_ATTR_PRELOAD_LIMIT, 0);
	nemo_config_set_int (nemo_preferences, NEMO_PREFERENCES_MAX_THUMBNAIL_THREADS, 1);

	dir = g_build_filename (tmp, "pictures", NULL);
	check (g_mkdir (dir, 0755) == 0);

	/* Back-dated, or a picture changed in the last two seconds is left
	   alone in case it is still being written. */
	for (i = 0; i < COUNT; i++) {
		g_autofree char *name = g_strdup_printf ("shot-%02d.png", i);
		g_autofree char *path = g_build_filename (dir, name, NULL);
		g_autoptr (GFile) location = g_file_new_for_path (path);

		write_image (path, i);
		check (g_file_set_attribute_uint64 (location, G_FILE_ATTRIBUTE_TIME_MODIFIED,
						    (guint64) (g_get_real_time () / G_USEC_PER_SEC) - 3600 - i,
						    G_FILE_QUERY_INFO_NONE, NULL, NULL));
	}

	/* Both get one time read up front, or a second can tick over between
	   them. */
	if (!can_write_bmp ()) {
		twin_ext = "png";
	}
	twin_time = (guint64) (g_get_real_time () / G_USEC_PER_SEC) - 7200;
	for (i = 0; i < 2; i++) {
		g_autofree char *name = g_strdup_printf ("twin-%c.%s", i == 0 ? 'a' : 'b', twin_ext);
		g_autofree char *path = g_build_filename (dir, name, NULL);
		g_autoptr (GFile) location = g_file_new_for_path (path);
		GdkPixbuf *pixbuf = gdk_pixbuf_new (GDK_COLORSPACE_RGB, FALSE, 8, 300, 200);

		gdk_pixbuf_fill (pixbuf, i == 0 ? 0xff0000ff : 0x0000ffff);
		if (strcmp (twin_ext, "bmp") == 0) {
			check (gdk_pixbuf_save (pixbuf, path, "bmp", NULL, NULL));
		} else {
			check (gdk_pixbuf_save (pixbuf, path, "png", NULL, "compression", "0", NULL));
		}
		g_object_unref (pixbuf);
		check (g_file_set_attribute_uint64 (location, G_FILE_ATTRIBUTE_TIME_MODIFIED, twin_time,
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

	for (i = 0; i < COUNT; i++) {
		g_autofree char *name = g_strdup_printf ("%s/shot-%02d.png", uri, i);

		files[i] = nemo_file_get_by_uri (name);
		g_signal_connect (files[i], "changed", G_CALLBACK (file_changed), GINT_TO_POINTER (i));
	}

	test_scrolled_to_bottom ();
	test_zoomed_in ();
	test_stored_waits_its_turn ();
	test_same_size_and_time (uri);

	for (i = 0; i < COUNT; i++) {
		g_signal_handlers_disconnect_by_func (files[i], file_changed, GINT_TO_POINTER (i));
		nemo_file_unref (files[i]);
	}
	nemo_directory_file_monitor_remove (directory, &client);
	nemo_directory_unref (directory);

	if (failures == 0)
		g_print ("nemo-thumbnail-order: all checks passed\n");

	return failures == 0 ? 0 : 1;
}
