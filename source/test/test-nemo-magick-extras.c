/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */

/* test-nemo-magick-extras.c - BMP and the like through ImageMagick, where
   gdk-pixbuf has no loader for them.

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

/* gdk-pixbuf reads its list of loaders once, so the whole run uses a copy of
 * it with only the PNG loader left, the one ImageMagick's output needs. Where
 * PNG is built in, the copy is empty. */

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

static char *
png_only_loaders (const char *dir)
{
	const char *real = g_getenv ("GDK_PIXBUF_MODULE_FILE");
	g_autofree char *text = NULL;
	g_auto (GStrv) blocks = NULL;
	g_autoptr (GString) kept = g_string_new (NULL);
	char *path;
	guint i;

	if (real == NULL || *real == '\0') {
		real = PIXBUF_LOADERS_CACHE;
	}
	if (!g_file_get_contents (real, &text, NULL, NULL)) {
		return NULL;
	}

	/* One loader per block, its module path on the first line that is not
	 * a comment. */
	blocks = g_strsplit (text, "\n\n", -1);
	for (i = 0; blocks[i] != NULL; i++) {
		g_auto (GStrv) lines = g_strsplit (blocks[i], "\n", -1);
		guint j;

		for (j = 0; lines[j] != NULL && lines[j][0] == '#'; j++) {
		}
		if (lines[j] != NULL && strstr (lines[j], "png") != NULL) {
			g_string_append_printf (kept, "%s\n\n", blocks[i]);
		}
	}

	path = g_build_filename (dir, "loaders.cache", NULL);
	if (!g_file_set_contents (path, kept->str, -1, NULL)) {
		g_free (path);
		return NULL;
	}

	return path;
}

static void
test_coder (void)
{
	check (g_strcmp0 (nemo_magick_coder ("/a/b.bmp"), "BMP") == 0);
	check (g_strcmp0 (nemo_magick_coder ("/a/b.ICO"), "ICO") == 0);
	check (g_strcmp0 (nemo_magick_coder ("/a/b.cur"), "CUR") == 0);
	check (g_strcmp0 (nemo_magick_coder ("file:///a/b%20c.xpm"), "XPM") == 0);
	check (g_strcmp0 (nemo_magick_coder ("/a/b.xbm"), "XBM") == 0);
	check (g_strcmp0 (nemo_magick_coder ("/a/b.pnm"), "PNM") == 0);
	check (g_strcmp0 (nemo_magick_coder ("/a/b.pbm"), "PBM") == 0);
	check (g_strcmp0 (nemo_magick_coder ("/a/b.pgm"), "PGM") == 0);
	check (g_strcmp0 (nemo_magick_coder ("/a/b.Ppm"), "PPM") == 0);
	check (g_strcmp0 (nemo_magick_coder ("/a/b.jp2"), "JP2") == 0);
	check (nemo_magick_coder ("/a/b.png") == NULL);
	check (nemo_magick_coder ("/a/b.bmp.txt") == NULL);
}

static gboolean
make_picture (const char *program, const char *path, const char *coder)
{
	g_autofree char *out = g_strdup_printf ("%s:%s", coder, path);
	const char *argv[] = { program, "-size", "200x100", "xc:#1e28c8", out, NULL };
	int status = 1;

	return g_spawn_sync (NULL, (char **) argv, NULL, G_SPAWN_STDERR_TO_DEV_NULL | G_SPAWN_STDOUT_TO_DEV_NULL,
			     NULL, NULL, NULL, NULL, &status, NULL) &&
	       status == 0 && g_file_test (path, G_FILE_TEST_EXISTS);
}

static gboolean
blue (GdkPixbuf *pixbuf)
{
	const guint8 *p;

	if (pixbuf == NULL) {
		g_printerr ("  no picture\n");
		return FALSE;
	}
	p = gdk_pixbuf_read_pixels (pixbuf) +
	    (gdk_pixbuf_get_height (pixbuf) / 2) * gdk_pixbuf_get_rowstride (pixbuf) +
	    (gdk_pixbuf_get_width (pixbuf) / 2) * gdk_pixbuf_get_n_channels (pixbuf);

	if (abs (p[0] - 30) > 8 || abs (p[1] - 40) > 8 || abs (p[2] - 200) > 8) {
		g_printerr ("  center is %d %d %d\n", p[0], p[1], p[2]);
		return FALSE;
	}

	return TRUE;
}

/* The way the render thread asks, under the type the system gives the file. */
static void
test_factory (const char *dir, const char *program)
{
	static const struct {
		const char *name;
		const char *coder;
		const char *mime;
	} pictures[] = {
		{ "plain.bmp", "BMP", "image/bmp" },
		{ "plain.ico", "ICO", "image/vnd.microsoft.icon" },
		{ "plain.xpm", "XPM", "image/x-xpixmap" },
		{ "plain.ppm", "PPM", "image/x-portable-pixmap" },
	};
	NemoDesktopThumbnailFactory *factory = nemo_desktop_thumbnail_factory_new (NEMO_DESKTOP_THUMBNAIL_SIZE_LARGE);
	guint i;

	for (i = 0; i < G_N_ELEMENTS (pictures); i++) {
		g_autofree char *path = g_build_filename (dir, pictures[i].name, NULL);
		g_autofree char *uri = g_filename_to_uri (path, NULL, NULL);
		g_autoptr (GdkPixbuf) pixbuf = NULL;

		if (!make_picture (program, path, pictures[i].coder)) {
			g_print ("nemo-magick-extras: ImageMagick cannot write %s, skipped\n", pictures[i].coder);
			continue;
		}
		check (nemo_desktop_thumbnail_factory_can_make (factory, uri, pictures[i].mime));
		pixbuf = nemo_desktop_thumbnail_factory_generate_thumbnail_at_size (factory, uri, pictures[i].mime, 128, NULL);
		if (!blue (pixbuf)) {
			g_printerr ("  for %s\n", pictures[i].name);
			failures++;
		}
	}

	g_object_unref (factory);
}

int
main (void)
{
	g_autofree char *home = test_scratch_config_home ("nemo-magick-extras-XXXXXX");
	g_autofree char *loaders = png_only_loaders (home);
	const char *program;

	if (loaders == NULL) {
		g_print ("nemo-magick-extras: no gdk-pixbuf loader list to copy, skipping\n");
		return 77;
	}
	g_setenv ("GDK_PIXBUF_MODULE_FILE", loaders, TRUE);
	if (!test_pixbuf_reads ("png") || test_pixbuf_reads ("bmp")) {
		g_print ("nemo-magick-extras: could not leave gdk-pixbuf with PNG and no BMP, skipping\n");
		return 77;
	}

	nemo_config_init ();

	test_coder ();

	program = nemo_magick_program ();
	if (program == NULL) {
		g_print ("nemo-magick-extras: no ImageMagick, skipping the rest\n");
		return failures == 0 ? 77 : 1;
	}
	test_factory (home, program);

	if (failures == 0)
		g_print ("nemo-magick-extras: all checks passed\n");

	return failures == 0 ? 0 : 1;
}
