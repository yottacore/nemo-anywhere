/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */

/* test-nemo-magick.c - thumbnails handed to ImageMagick.

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

/* The list and the command are checked everywhere. The rest needs a real
 * ImageMagick that can write JPEG 2000, and is skipped without one. */

#include <config.h>

#include <stdlib.h>
#include <string.h>

#include <glib.h>
#include <gio/gio.h>

#include <libnemo-private/nemo-magick.h>
#include <libnemo-private/nemo-config.h>
#include <libnemo-private/nemo-desktop-thumbnail.h>

#include "test-scratch.h"
#include "test-check.h"

static gboolean
pixbuf_has (const char *name)
{
	GSList *formats = gdk_pixbuf_get_formats ();
	gboolean found = FALSE;
	GSList *l;

	for (l = formats; l != NULL && !found; l = l->next) {
		found = g_strcmp0 (gdk_pixbuf_format_get_name (l->data), name) == 0;
	}
	g_slist_free (formats);

	return found;
}

static void
test_coder (void)
{
	check (g_strcmp0 (nemo_magick_coder ("/a/b.jp2"), "JP2") == 0);
	check (g_strcmp0 (nemo_magick_coder ("/a/b.J2K"), "J2K") == 0);
	check (g_strcmp0 (nemo_magick_coder ("file:///a/b%20c.heic"), "HEIC") == 0);
	check (g_strcmp0 (nemo_magick_coder ("x.x3f"), "X3F") == 0);
	check (nemo_magick_coder ("/a/b.png") == NULL);
	check (nemo_magick_coder ("/a/b.svg") == NULL);
	check (nemo_magick_coder ("/a/b.txt") == NULL);
	check (nemo_magick_coder ("/a/b.pdf") == NULL);
	check (nemo_magick_coder ("/a/b.jp2.txt") == NULL);
	check (nemo_magick_coder ("/a.jp2/b") == NULL);
	check (nemo_magick_coder ("/a/b") == NULL);
	check (nemo_magick_coder (NULL) == NULL);

	/* Handed over only while gdk-pixbuf has no loader of its own. */
	check ((nemo_magick_coder ("/a/b.bmp") == NULL) == pixbuf_has ("bmp"));
	check ((nemo_magick_coder ("/a/b.xpm") == NULL) == pixbuf_has ("xpm"));

	check (!nemo_magick_type_ok ("sftp://host/a/b.jp2"));
	check (!nemo_magick_type_ok ("file:///a/b.svg"));
	check (!nemo_magick_type_ok (NULL));
}

/* The file's name must never reach the command. */
static void
test_argv (void)
{
	g_auto (GStrv) argv = nemo_magick_argv ("magick", "JP2", 128);
	guint n = g_strv_length (argv);

	check (g_strcmp0 (argv[0], "magick") == 0);
	check (g_strv_contains ((const char * const *) argv, "JP2:-[0]"));
	check (g_strv_contains ((const char * const *) argv, "128x128>"));
	check (n > 0 && g_strcmp0 (argv[n - 1], "png:-") == 0);
}

static gboolean
make_jp2 (const char *program, const char *path, const char *color)
{
	g_autofree char *fill = g_strdup_printf ("xc:%s", color);
	g_autofree char *out = g_strdup_printf ("jp2:%s", path);
	const char *argv[] = { program, "-size", "200x100", fill, out, NULL };
	int status = 1;

	return g_spawn_sync (NULL, (char **) argv, NULL, G_SPAWN_STDERR_TO_DEV_NULL | G_SPAWN_STDOUT_TO_DEV_NULL,
			     NULL, NULL, NULL, NULL, &status, NULL) &&
	       status == 0 && g_file_test (path, G_FILE_TEST_EXISTS);
}

static GdkPixbuf *
load (const char *path, int size)
{
	g_autofree char *uri = g_filename_to_uri (path, NULL, NULL);

	return nemo_magick_load_uri (uri, size, NULL);
}

static gboolean
color_is (GdkPixbuf *pixbuf, int r, int g, int b)
{
	const guint8 *p;

	if (pixbuf == NULL) {
		g_printerr ("  no picture\n");
		return FALSE;
	}
	p = gdk_pixbuf_read_pixels (pixbuf) +
	    (gdk_pixbuf_get_height (pixbuf) / 2) * gdk_pixbuf_get_rowstride (pixbuf) +
	    (gdk_pixbuf_get_width (pixbuf) / 2) * gdk_pixbuf_get_n_channels (pixbuf);

	if (abs (p[0] - r) > 8 || abs (p[1] - g) > 8 || abs (p[2] - b) > 8) {
		g_printerr ("  center is %d %d %d\n", p[0], p[1], p[2]);
		return FALSE;
	}

	return TRUE;
}

/* Names ImageMagick would read something into, were it ever given one. */
static void
test_names (const char *dir, const char *plain)
{
	static const char *names[] = {
		"pct%d.jp2",
		"100%.jp2",
		"@at.jp2",
		"-dash.jp2",
		"brack[1].jp2",
		"with space.jp2",
#ifndef G_OS_WIN32
		"msl:x.jp2",
		"jp2:x.jp2",
#endif
	};
	guint i;

	for (i = 0; i < G_N_ELEMENTS (names); i++) {
		g_autofree char *path = g_build_filename (dir, names[i], NULL);
		g_autofree char *bytes = NULL;
		g_autoptr (GdkPixbuf) pixbuf = NULL;
		gsize len = 0;

		check (g_file_get_contents (plain, &bytes, &len, NULL) &&
		       g_file_set_contents (path, bytes, len, NULL));
		pixbuf = load (path, 64);
		if (!color_is (pixbuf, 30, 40, 200)) {
			g_printerr ("  for %s\n", names[i]);
			failures++;
		}
	}
}

static void
test_real (const char *dir)
{
	g_autofree char *plain = g_build_filename (dir, "plain.jp2", NULL);
	g_autofree char *png = g_build_filename (dir, "really-png.jp2", NULL);
	g_autofree char *text = g_build_filename (dir, "text.jp2", NULL);
	g_autoptr (GdkPixbuf) pixbuf = NULL;
	g_autoptr (GdkPixbuf) small = NULL;
	g_autoptr (GdkPixbuf) other = NULL;
	g_autoptr (GdkPixbuf) junk = NULL;

	pixbuf = load (plain, 256);
	check (pixbuf != NULL && gdk_pixbuf_get_width (pixbuf) == 200 && gdk_pixbuf_get_height (pixbuf) == 100);
	check (color_is (pixbuf, 30, 40, 200));

	small = load (plain, 64);
	check (small != NULL && gdk_pixbuf_get_width (small) == 64 && gdk_pixbuf_get_height (small) == 32);

	/* The format is named, not guessed from the contents. */
	{
		g_autoptr (GdkPixbuf) source = gdk_pixbuf_new (GDK_COLORSPACE_RGB, FALSE, 8, 20, 10);

		gdk_pixbuf_fill (source, 0xFF0000FF);
		check (gdk_pixbuf_save (source, png, "png", NULL, NULL));
	}
	other = load (png, 64);
	check (other == NULL);

	check (g_file_set_contents (text, "push graphic-context\nviewbox 0 0 10 10\n", -1, NULL));
	junk = load (text, 64);
	check (junk == NULL);

	test_names (dir, plain);
}

/* The thumbnail factory is where the render thread asks. */
static void
test_factory (const char *dir)
{
	g_autofree char *path = g_build_filename (dir, "plain.jp2", NULL);
	g_autofree char *uri = g_filename_to_uri (path, NULL, NULL);
	NemoDesktopThumbnailFactory *factory;
	g_autoptr (GdkPixbuf) pixbuf = NULL;

	factory = nemo_desktop_thumbnail_factory_new (NEMO_DESKTOP_THUMBNAIL_SIZE_LARGE);
	check (nemo_desktop_thumbnail_factory_can_make (factory, uri, "image/jp2"));

	pixbuf = nemo_desktop_thumbnail_factory_generate_thumbnail_at_size (factory, uri, "image/jp2", 128, NULL);
	check (color_is (pixbuf, 30, 40, 200));

	g_object_unref (factory);
}

int
main (void)
{
	g_autofree char *home = test_scratch_config_home ("nemo-magick-XXXXXX");
	g_autofree char *plain = g_build_filename (home, "plain.jp2", NULL);
	const char *program;

	nemo_config_init ();

	test_coder ();
	test_argv ();

	program = nemo_magick_program ();
	if (program == NULL || !make_jp2 (program, plain, "#1e28c8")) {
		g_print ("nemo-magick: no ImageMagick that writes JPEG 2000, skipping the rest\n");
		return failures == 0 ? 77 : 1;
	}

	test_real (home);
	test_factory (home);

	if (failures == 0)
		g_print ("nemo-magick: all checks passed\n");

	return failures == 0 ? 0 : 1;
}
