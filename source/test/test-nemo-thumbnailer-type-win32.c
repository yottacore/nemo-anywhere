/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */

/* test-nemo-thumbnailer-type-win32.c - thumbnailer programs on Windows.

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

/* On Windows a file's type is its extension, ".pdf", while thumbnailers and
 * the list of types not to thumbnail name MIME types, so no thumbnailer was
 * ever found. A stand-in thumbnailer for PDF, this program run with --draw,
 * draws a green 5x4 picture. An OpenDocument file holding a red 5x4 thumbnail
 * gets it from the app's own reader, by its extension too. And the same file
 * reached as a share is not thumbnailed with the default settings. */

#include <config.h>

#include <string.h>

#include <glib.h>
#include <glib/gstdio.h>
#include <gtk/gtk.h>

#include <libnemo-private/nemo-desktop-thumbnail.h>
#include <libnemo-private/nemo-file.h>
#include <libnemo-private/nemo-global-preferences.h>
#include <libnemo-private/nemo-thumbnails.h>

#include "test-scratch.h"
#include "test-check.h"

/* A zip with only what the thumbnailer reads: the type, and the thumbnail. */
static const guchar odt[] = {
	0x50, 0x4b, 0x03, 0x04, 0x14, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x21, 0x5c, 0x5e, 0xc6, 0x32, 0x0c, 0x27, 0x00, 0x00, 0x00, 0x27, 0x00,
	0x00, 0x00, 0x08, 0x00, 0x00, 0x00, 0x6d, 0x69, 0x6d, 0x65, 0x74, 0x79,
	0x70, 0x65, 0x61, 0x70, 0x70, 0x6c, 0x69, 0x63, 0x61, 0x74, 0x69, 0x6f,
	0x6e, 0x2f, 0x76, 0x6e, 0x64, 0x2e, 0x6f, 0x61, 0x73, 0x69, 0x73, 0x2e,
	0x6f, 0x70, 0x65, 0x6e, 0x64, 0x6f, 0x63, 0x75, 0x6d, 0x65, 0x6e, 0x74,
	0x2e, 0x74, 0x65, 0x78, 0x74, 0x50, 0x4b, 0x03, 0x04, 0x14, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x21, 0x5c, 0xef, 0xde, 0x36, 0x99, 0x4a,
	0x00, 0x00, 0x00, 0x4a, 0x00, 0x00, 0x00, 0x18, 0x00, 0x00, 0x00, 0x54,
	0x68, 0x75, 0x6d, 0x62, 0x6e, 0x61, 0x69, 0x6c, 0x73, 0x2f, 0x74, 0x68,
	0x75, 0x6d, 0x62, 0x6e, 0x61, 0x69, 0x6c, 0x2e, 0x70, 0x6e, 0x67, 0x89,
	0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a, 0x00, 0x00, 0x00, 0x0d, 0x49,
	0x48, 0x44, 0x52, 0x00, 0x00, 0x00, 0x05, 0x00, 0x00, 0x00, 0x04, 0x08,
	0x02, 0x00, 0x00, 0x00, 0xc9, 0x51, 0x62, 0x17, 0x00, 0x00, 0x00, 0x11,
	0x49, 0x44, 0x41, 0x54, 0x78, 0xda, 0x63, 0x38, 0x21, 0x27, 0x87, 0x8c,
	0x18, 0x48, 0xe4, 0x03, 0x00, 0x97, 0xa6, 0x14, 0x51, 0xab, 0xd2, 0x5b,
	0x6d, 0x00, 0x00, 0x00, 0x00, 0x49, 0x45, 0x4e, 0x44, 0xae, 0x42, 0x60,
	0x82, 0x50, 0x4b, 0x01, 0x02, 0x14, 0x03, 0x14, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x21, 0x5c, 0x5e, 0xc6, 0x32, 0x0c, 0x27, 0x00, 0x00,
	0x00, 0x27, 0x00, 0x00, 0x00, 0x08, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x80, 0x01, 0x00, 0x00, 0x00, 0x00, 0x6d,
	0x69, 0x6d, 0x65, 0x74, 0x79, 0x70, 0x65, 0x50, 0x4b, 0x01, 0x02, 0x14,
	0x03, 0x14, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x21, 0x5c, 0xef,
	0xde, 0x36, 0x99, 0x4a, 0x00, 0x00, 0x00, 0x4a, 0x00, 0x00, 0x00, 0x18,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x80,
	0x01, 0x4d, 0x00, 0x00, 0x00, 0x54, 0x68, 0x75, 0x6d, 0x62, 0x6e, 0x61,
	0x69, 0x6c, 0x73, 0x2f, 0x74, 0x68, 0x75, 0x6d, 0x62, 0x6e, 0x61, 0x69,
	0x6c, 0x2e, 0x70, 0x6e, 0x67, 0x50, 0x4b, 0x05, 0x06, 0x00, 0x00, 0x00,
	0x00, 0x02, 0x00, 0x02, 0x00, 0x7c, 0x00, 0x00, 0x00, 0xcd, 0x00, 0x00,
	0x00, 0x00, 0x00,
};

#define GREEN 0x20d020
#define RED   0xd02020

static gboolean ready;

/* The stand-in thumbnailer: a 5x4 picture of one color, to @out, whatever
   the input. */
static int
draw (const char *out)
{
	GdkPixbuf *pixbuf = gdk_pixbuf_new (GDK_COLORSPACE_RGB, FALSE, 8, 5, 4);
	gboolean ok;

	gdk_pixbuf_fill (pixbuf, ((guint32) GREEN << 8) | 0xff);
	ok = gdk_pixbuf_save (pixbuf, out, "png", NULL, NULL);
	g_object_unref (pixbuf);
	return ok ? 0 : 1;
}

static void
install_thumbnailer (const char *home, const char *self)
{
	g_autofree char *dir = g_build_filename (home, "thumbnailers", NULL);
	g_autofree char *absolute = g_canonicalize_filename (self, NULL);
	g_autofree char *exe = g_str_has_suffix (absolute, ".exe") ? g_strdup (absolute) : g_strconcat (absolute, ".exe", NULL);
	g_autofree char *entry = g_strdup_printf ("[Thumbnailer Entry]\nTryExec=%s\nExec=\"%s\" --draw %%i %%o\nMimeType=application/pdf;\n",
						  exe, exe);
	g_autofree char *path = g_build_filename (dir, "stand-in.thumbnailer", NULL);

	g_mkdir_with_parents (dir, 0755);
	check (g_file_set_contents (path, entry, -1, NULL));
}

static void
check_picture (GdkPixbuf *pixbuf, int width, int height, guint32 rgb)
{
	const guchar *pixel;

	check (pixbuf != NULL);
	if (pixbuf == NULL) {
		return;
	}
	pixel = gdk_pixbuf_read_pixels (pixbuf);
	check (gdk_pixbuf_get_width (pixbuf) == width && gdk_pixbuf_get_height (pixbuf) == height);
	check (ABS (pixel[0] - (int) ((rgb >> 16) & 0xff)) < 48 && ABS (pixel[1] - (int) ((rgb >> 8) & 0xff)) < 48 &&
	       ABS (pixel[2] - (int) (rgb & 0xff)) < 48);
}

static void
file_ready (G_GNUC_UNUSED NemoFile *file, G_GNUC_UNUSED gpointer data)
{
	ready = TRUE;
}

static NemoFile *
ready_file (const char *path)
{
	g_autoptr (GFile) location = g_file_new_for_path (path);
	NemoFile *file = nemo_file_get (location);
	gint64 give_up = g_get_monotonic_time () + 20 * G_USEC_PER_SEC;

	ready = FALSE;
	nemo_file_call_when_ready (file, NEMO_FILE_ATTRIBUTE_INFO, file_ready, NULL);
	while (!ready && g_get_monotonic_time () < give_up) {
		g_main_context_iteration (NULL, FALSE);
		g_usleep (10000);
	}
	check (ready);
	return file;
}

static void
spin (void)
{
	while (g_main_context_iteration (NULL, FALSE)) {
	}
}

int
main (int argc, char *argv[])
{
	g_autofree char *scratch = NULL, *path = NULL, *uri = NULL, *share = NULL;
	g_autofree char *pdf = NULL, *pdf_uri = NULL;
	g_autofree char *type = NULL, *pdf_type = NULL;
	NemoDesktopThumbnailFactory *factory;
	NemoFile *file, *share_file, *pdf_file;
	GdkPixbuf *pixbuf;
	g_autofree char *share_uri = NULL;
	const char *disable[] = { "application/vnd.oasis.opendocument.text", "application/pdf", NULL };
	const char *none[] = { NULL };

	if (argc == 4 && g_strcmp0 (argv[1], "--draw") == 0) {
		return draw (argv[3]);
	}

	scratch = test_scratch_config_home ("nemo-thumbnailer-type-XXXXXX");
	if (scratch == NULL) {
		g_printerr ("FAIL: no scratch folder\n");
		return 1;
	}
	/* Only the stand-in, none of the box's own. */
	g_setenv ("XDG_DATA_HOME", scratch, TRUE);
	g_setenv ("XDG_DATA_DIRS", scratch, TRUE);
	install_thumbnailer (scratch, argv[0]);

	path = g_build_filename (scratch, "note.odt", NULL);
	check (g_file_set_contents (path, (const char *) odt, sizeof odt, NULL));
	uri = g_filename_to_uri (path, NULL, NULL);
	pdf = g_build_filename (scratch, "paper.pdf", NULL);
	check (g_file_set_contents (pdf, "%PDF-1.4\n%%EOF\n", -1, NULL));
	pdf_uri = g_filename_to_uri (pdf, NULL, NULL);

	if (!gtk_init_check (&argc, &argv)) {
		g_print ("SKIP: no display\n");
		return 77;
	}
	nemo_global_preferences_init ();

	/* What the app hands over. */
	file = ready_file (path);
	type = nemo_file_get_mime_type (file);
	check (g_strcmp0 (type, ".odt") == 0);
	pdf_file = ready_file (pdf);
	pdf_type = nemo_file_get_mime_type (pdf_file);
	check (g_strcmp0 (pdf_type, ".pdf") == 0);

	factory = nemo_desktop_thumbnail_factory_new (NEMO_DESKTOP_THUMBNAIL_SIZE_LARGE);

	g_print ("thumbnailer\n");
	check (nemo_desktop_thumbnail_factory_can_make (factory, pdf_uri, ".pdf"));
	pixbuf = nemo_desktop_thumbnail_factory_generate_thumbnail_at_size (factory, pdf_uri, ".pdf", 128, NULL);
	/* As the thumbnailer drew it. */
	check_picture (pixbuf, 5, 4, GREEN);
	g_clear_object (&pixbuf);
	check (nemo_can_thumbnail (pdf_file));
	check (nemo_file_should_show_thumbnail (pdf_file));

	g_print ("office reader\n");
	check (nemo_desktop_thumbnail_factory_can_make (factory, uri, ".odt"));
	pixbuf = nemo_desktop_thumbnail_factory_generate_thumbnail_at_size (factory, uri, ".odt", 128, NULL);
	/* Scaled up to the size asked for, as the reader does. */
	check_picture (pixbuf, 128, 102, RED);
	g_clear_object (&pixbuf);
	check (nemo_can_thumbnail (file));
	check (nemo_file_should_show_thumbnail (file));

	g_print ("turned off by type\n");
	nemo_config_set_strv (nemo_config_get_group ("thumbnailers"), "disable", disable);
	spin ();
	check (!nemo_desktop_thumbnail_factory_can_make (factory, uri, ".odt"));
	pixbuf = nemo_desktop_thumbnail_factory_generate_thumbnail_at_size (factory, uri, ".odt", 128, NULL);
	check (pixbuf == NULL);
	g_clear_object (&pixbuf);
	check (!nemo_desktop_thumbnail_factory_can_make (factory, pdf_uri, ".pdf"));
	pixbuf = nemo_desktop_thumbnail_factory_generate_thumbnail_at_size (factory, pdf_uri, ".pdf", 128, NULL);
	check (pixbuf == NULL);
	g_clear_object (&pixbuf);
	nemo_config_set_strv (nemo_config_get_group ("thumbnailers"), "disable", none);
	spin ();

	/* The same file through the drive's admin share. Only its path is
	   looked at, so it need not answer. */
	g_print ("share\n");
	share = g_strdup_printf ("\\\\localhost\\%c$%s", g_ascii_toupper (path[0]), path + 2);
	share_uri = g_filename_to_uri (share, NULL, NULL);
	share_file = nemo_file_get_by_uri (share_uri);
	check (nemo_file_is_on_a_share (share_file));
	check (!nemo_file_should_show_thumbnail (share_file));

	nemo_file_unref (share_file);
	nemo_file_unref (pdf_file);
	nemo_file_unref (file);
	g_object_unref (factory);

	if (failures > 0) {
		g_printerr ("%d failure(s)\n", failures);
		return 1;
	}

	g_print ("OK\n");
	return 0;
}
