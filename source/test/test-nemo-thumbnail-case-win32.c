/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */

/* test-nemo-thumbnail-case-win32.c - thumbnails for an upper-case extension.

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

/* A picture gets a thumbnail whatever the case of its extension, as cameras
 * name them IMG_1001.JPG. On Windows a file's type is its extension as
 * written, so ".JPG" and ".jpg" are two types there, and only the MIME type
 * behind them is the same.
 *
 * The data dirs point at an empty folder, as in the bundle, which has no
 * gdk-pixbuf-thumbnailer descriptor. A build tree or an MSYS2 install has one,
 * and it answers for image/jpeg in any case, which hides the bug.
 *
 * Windows-only. */

#include <config.h>

#include <stdlib.h>
#include <gio/gio.h>
#include <glib/gstdio.h>
#include <gtk/gtk.h>

#include <libnemo-private/nemo-file.h>
#include <libnemo-private/nemo-file-private.h>
#include <libnemo-private/nemo-thumbnails.h>

#include "test-scratch.h"
#include "test-check.h"

static NemoFile *
seen_file (const char *path)
{
	GFile *location = g_file_new_for_path (path);
	NemoFile *file = nemo_file_get (location);
	GFileInfo *info;

	info = g_file_query_info (location, "standard::*",
				  G_FILE_QUERY_INFO_NOFOLLOW_SYMLINKS, NULL, NULL);
	g_object_unref (location);

	if (info == NULL) {
		nemo_file_unref (file);
		return NULL;
	}

	nemo_file_update_info (file, info);
	g_object_unref (info);
	return file;
}

static gboolean
write_image (const char *path, const char *format)
{
	GdkPixbuf *pixbuf = gdk_pixbuf_new (GDK_COLORSPACE_RGB, FALSE, 8, 4, 4);
	gboolean ok;

	gdk_pixbuf_fill (pixbuf, 0x336699ff);
	ok = gdk_pixbuf_save (pixbuf, path, format, NULL, NULL);
	g_object_unref (pixbuf);

	return ok;
}

static void
expect (const char *dir, const char *name, const char *format, gboolean want)
{
	g_autofree char *path = g_build_filename (dir, name, NULL);
	NemoFile *file;

	if (format != NULL) {
		check (write_image (path, format));
	} else {
		check (g_file_set_contents (path, "not a picture", -1, NULL));
	}

	file = seen_file (path);
	check (file != NULL);
	if (file != NULL) {
		g_autofree char *type = nemo_file_get_mime_type (file);
		gboolean can = nemo_can_thumbnail (file);

		if (can != want) {
			g_printerr ("FAIL %s (type [%s]): can_thumbnail %d, want %d\n",
				    name, type, can, want);
			failures++;
		}
		nemo_file_unref (file);
	}
	g_unlink (path);
}

int
main (int argc, char *argv[])
{
	char *home, *dir;

	home = test_scratch_config_home ("nemo-thumbcase-XXXXXX");
	g_assert (home != NULL);
	g_setenv ("XDG_DATA_HOME", home, TRUE);
	g_setenv ("XDG_DATA_DIRS", home, TRUE);

	gtk_init_check (&argc, &argv);

	dir = g_build_filename (home, "files", NULL);
	check (g_mkdir_with_parents (dir, 0755) == 0);

	expect (dir, "IMG_1000.jpg", "jpeg", TRUE);
	expect (dir, "IMG_1001.JPG", "jpeg", TRUE);
	expect (dir, "IMG_1002.Jpg", "jpeg", TRUE);
	expect (dir, "IMG_1003.JPEG", "jpeg", TRUE);
	expect (dir, "photo.jpeg", "jpeg", TRUE);
	expect (dir, "SCAN_0001.PNG", "png", TRUE);

	/* The other side, so a check that says yes to anything fails. */
	expect (dir, "NOTES.TXT", NULL, FALSE);
	expect (dir, "MYSTERY.ZZZ", NULL, FALSE);

	g_rmdir (dir);
	g_free (dir);
	g_free (home);

	if (failures == 0) {
		g_print ("thumbnail-case-win32: all checks passed\n");
	}
	return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
