/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */

/* test-nemo-psd.c - reading the flattened picture out of a Photoshop file.

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

/* The files are built here byte by byte, since no Photoshop file can be made
 * on the build boxes. Each one is small enough to reason about by hand. */

#include <config.h>

#include <string.h>

#include <glib.h>
#include <gio/gio.h>

#include <libnemo-private/nemo-psd.h>
#include <libnemo-private/nemo-config.h>
#include <libnemo-private/nemo-desktop-thumbnail.h>

#include "test-scratch.h"
#include "test-check.h"

typedef struct {
	guint    version;	/* 1 psd, 2 psb */
	guint    channels;
	guint    width, height;
	guint    depth;
	guint    mode;
	gint16   layer_count;
	gboolean packed;
	const guint8 *palette;	/* 768 bytes, indexed only */
	/* One sample per channel per pixel, 8 bit, planar. A 16 bit file
	   repeats each byte, so the high byte is the same value. */
	const guint8 *planes;
} Spec;

static void
put16 (GByteArray *out, guint v)
{
	guint8 b[2] = { v >> 8, v };

	g_byte_array_append (out, b, 2);
}

static void
put32 (GByteArray *out, guint32 v)
{
	guint8 b[4] = { v >> 24, v >> 16, v >> 8, v };

	g_byte_array_append (out, b, 4);
}

/* PackBits, the plain way: runs of literals of at most 128. */
static GByteArray *
pack (const guint8 *row, gsize len)
{
	GByteArray *out = g_byte_array_new ();
	gsize i = 0;

	while (i < len) {
		gsize run = 1;

		while (i + run < len && run < 128 && row[i + run] == row[i]) {
			run++;
		}

		if (run >= 2) {
			guint8 n = (guint8) (1 - (int) run);

			g_byte_array_append (out, &n, 1);
			g_byte_array_append (out, &row[i], 1);
			i += run;
		} else {
			guint8 n = 0;

			g_byte_array_append (out, &n, 1);
			g_byte_array_append (out, &row[i], 1);
			i++;
		}
	}

	return out;
}

static GBytes *
build (const Spec *spec)
{
	GByteArray *out = g_byte_array_new ();
	guint bps = spec->depth / 8;
	gsize row_bytes = (gsize) spec->width * bps;
	g_autofree guint8 *row = g_malloc (row_bytes);
	guint c, y, x;

	g_byte_array_append (out, (const guint8 *) "8BPS", 4);
	put16 (out, spec->version);
	g_byte_array_append (out, (const guint8 *) "\0\0\0\0\0\0", 6);
	put16 (out, spec->channels);
	put32 (out, spec->height);
	put32 (out, spec->width);
	put16 (out, spec->depth);
	put16 (out, spec->mode);

	if (spec->palette != NULL) {
		put32 (out, 768);
		g_byte_array_append (out, spec->palette, 768);
	} else {
		put32 (out, 0);
	}

	/* One resource-shaped blob, to be skipped. */
	put32 (out, 6);
	g_byte_array_append (out, (const guint8 *) "junk!!", 6);

	/* Layer section: lengths, the count, and a few bytes of layer records
	   that must be skipped unread. */
	if (spec->version == 2) {
		put32 (out, 0);
	}
	put32 (out, (spec->version == 2 ? 8 : 4) + 2 + 5);
	if (spec->version == 2) {
		put32 (out, 0);
	}
	put32 (out, 2 + 5);
	put16 (out, (guint16) spec->layer_count);
	g_byte_array_append (out, (const guint8 *) "layer", 5);

	put16 (out, spec->packed ? 1 : 0);

	if (spec->packed) {
		GPtrArray *rows = g_ptr_array_new_with_free_func ((GDestroyNotify) g_byte_array_unref);

		for (c = 0; c < spec->channels; c++) {
			for (y = 0; y < spec->height; y++) {
				for (x = 0; x < spec->width; x++) {
					guint8 v = spec->planes[((gsize) c * spec->height + y) * spec->width + x];

					row[x * bps] = v;
					if (bps == 2) {
						row[x * bps + 1] = v;
					}
				}
				g_ptr_array_add (rows, pack (row, row_bytes));
			}
		}
		for (c = 0; c < rows->len; c++) {
			GByteArray *packed = rows->pdata[c];

			if (spec->version == 2) {
				put32 (out, packed->len);
			} else {
				put16 (out, packed->len);
			}
		}
		for (c = 0; c < rows->len; c++) {
			GByteArray *packed = rows->pdata[c];

			g_byte_array_append (out, packed->data, packed->len);
		}
		g_ptr_array_unref (rows);
	} else {
		for (c = 0; c < spec->channels; c++) {
			for (y = 0; y < spec->height; y++) {
				for (x = 0; x < spec->width; x++) {
					guint8 v = spec->planes[((gsize) c * spec->height + y) * spec->width + x];

					row[x * bps] = v;
					if (bps == 2) {
						row[x * bps + 1] = v;
					}
				}
				g_byte_array_append (out, row, row_bytes);
			}
		}
	}

	return g_byte_array_free_to_bytes (out);
}

static GdkPixbuf *
read_bytes (GBytes *bytes, int size)
{
	g_autoptr (GInputStream) in = g_memory_input_stream_new_from_bytes (bytes);

	return nemo_psd_load (in, size, NULL);
}

static GdkPixbuf *
read_spec (const Spec *spec, int size)
{
	g_autoptr (GBytes) bytes = build (spec);

	return read_bytes (bytes, size);
}

static gboolean
pixel_is (GdkPixbuf *pixbuf, int x, int y, int r, int g, int b, int a)
{
	const guint8 *p;
	int n;

	if (pixbuf == NULL) {
		return FALSE;
	}

	n = gdk_pixbuf_get_n_channels (pixbuf);
	p = gdk_pixbuf_read_pixels (pixbuf) + y * gdk_pixbuf_get_rowstride (pixbuf) + x * n;

	if (p[0] != r || p[1] != g || p[2] != b) {
		g_printerr ("  pixel %d,%d is %d %d %d\n", x, y, p[0], p[1], p[2]);
		return FALSE;
	}

	return a < 0 ? n == 3 : (n == 4 && p[3] == a);
}

/* Two by two, RGB plus a fourth channel. */
static const guint8 rgb_planes[] = {
	255, 0,   10,  20,	/* red */
	0,   255, 30,  40,	/* green */
	0,   0,   255, 60,	/* blue */
	255, 128, 0,   255,	/* extra */
};

static void
test_rgb (void)
{
	Spec spec = { 1, 3, 2, 2, 8, 3, 1, FALSE, NULL, rgb_planes };
	g_autoptr (GdkPixbuf) raw = NULL;
	g_autoptr (GdkPixbuf) packed = NULL;
	g_autoptr (GdkPixbuf) wide = NULL;

	raw = read_spec (&spec, 64);
	check (raw != NULL && gdk_pixbuf_get_width (raw) == 2 && gdk_pixbuf_get_height (raw) == 2);
	check (pixel_is (raw, 0, 0, 255, 0, 0, -1));
	check (pixel_is (raw, 1, 0, 0, 255, 0, -1));
	check (pixel_is (raw, 0, 1, 10, 30, 255, -1));
	check (pixel_is (raw, 1, 1, 20, 40, 60, -1));

	spec.packed = TRUE;
	packed = read_spec (&spec, 64);
	check (pixel_is (packed, 1, 1, 20, 40, 60, -1));

	spec.version = 2;
	spec.depth = 16;
	wide = read_spec (&spec, 64);
	check (pixel_is (wide, 0, 1, 10, 30, 255, -1));
}

/* A fourth channel is the transparency only when the layer count says so;
 * otherwise it is a saved selection and must not punch holes in the picture. */
static void
test_alpha (void)
{
	Spec spec = { 1, 4, 2, 2, 8, 3, -1, TRUE, NULL, rgb_planes };
	g_autoptr (GdkPixbuf) see_through = NULL;
	g_autoptr (GdkPixbuf) selection = NULL;

	see_through = read_spec (&spec, 64);
	check (pixel_is (see_through, 1, 0, 0, 255, 0, 128));
	check (pixel_is (see_through, 0, 1, 10, 30, 255, 0));

	spec.layer_count = 2;
	selection = read_spec (&spec, 64);
	check (pixel_is (selection, 0, 1, 10, 30, 255, -1));
}

static void
test_other_modes (void)
{
	static const guint8 gray_planes[] = { 0, 100, 200, 255 };
	/* Stored as 255 for no ink: no cyan and no yellow at all, full
	   magenta, no black. That is magenta. */
	static const guint8 cmyk_planes[] = { 255, 0, 255, 255 };
	static const guint8 index_planes[] = { 0, 1, 2, 1 };
	guint8 palette[768] = { 0 };
	Spec gray = { 1, 1, 2, 2, 16, 1, 0, TRUE, NULL, gray_planes };
	Spec cmyk = { 1, 4, 1, 1, 8, 4, 0, FALSE, NULL, cmyk_planes };
	Spec indexed = { 1, 1, 2, 2, 8, 2, 0, FALSE, palette, index_planes };
	Spec lab = { 1, 3, 2, 2, 8, 9, 0, FALSE, NULL, rgb_planes };
	g_autoptr (GdkPixbuf) g = NULL;
	g_autoptr (GdkPixbuf) k = NULL;
	g_autoptr (GdkPixbuf) i = NULL;
	g_autoptr (GdkPixbuf) l = NULL;

	palette[1] = 200;		/* index 1 red */
	palette[256 + 2] = 150;		/* index 2 green */

	g = read_spec (&gray, 64);
	check (pixel_is (g, 1, 0, 100, 100, 100, -1));
	check (pixel_is (g, 1, 1, 255, 255, 255, -1));

	k = read_spec (&cmyk, 64);
	check (pixel_is (k, 0, 0, 255, 0, 255, -1));

	i = read_spec (&indexed, 64);
	check (pixel_is (i, 0, 0, 0, 0, 0, -1));
	check (pixel_is (i, 1, 0, 200, 0, 0, -1));
	check (pixel_is (i, 0, 1, 0, 150, 0, -1));

	l = read_spec (&lab, 64);
	check (l == NULL);
}

/* 40 by 20, left half red and right half blue, asked for at 10. Each output
 * pixel stands for a 4 by 4 block, and the one on the seam between the halves
 * is an average of both. */
static void
test_shrink (void)
{
	const guint width = 40, height = 20;
	g_autofree guint8 *planes = g_malloc0 (3 * width * height);
	Spec spec = { 1, 3, width, height, 8, 3, 0, TRUE, NULL, planes };
	g_autoptr (GdkPixbuf) small = NULL;
	guint x, y;

	for (y = 0; y < height; y++) {
		for (x = 0; x < width; x++) {
			gboolean left = x < 18;

			planes[y * width + x] = left ? 255 : 0;
			planes[2 * width * height + y * width + x] = left ? 0 : 255;
		}
	}

	small = read_spec (&spec, 10);
	check (small != NULL && gdk_pixbuf_get_width (small) == 10 && gdk_pixbuf_get_height (small) == 5);
	check (pixel_is (small, 0, 0, 255, 0, 0, -1));
	check (pixel_is (small, 9, 4, 0, 0, 255, -1));
	/* x 16 to 19: two red columns and two blue ones. */
	check (pixel_is (small, 4, 2, 127, 0, 127, -1));
}

/* Every cut short of the whole file answers NULL, and none of them reads past
 * what it was given. The corpus test under the sanitizer is what sees an
 * over-read; this sees a wrong answer. */
static void
test_truncated (void)
{
	Spec spec = { 1, 4, 2, 2, 8, 3, -1, TRUE, NULL, rgb_planes };
	g_autoptr (GBytes) whole = build (&spec);
	gsize len = g_bytes_get_size (whole), cut;
	guint answered = 0;

	for (cut = 0; cut < len; cut++) {
		g_autoptr (GBytes) part = g_bytes_new_from_bytes (whole, 0, cut);
		g_autoptr (GdkPixbuf) pixbuf = read_bytes (part, 64);

		answered += pixbuf != NULL;
	}
	check (answered == 0);
}

/* Square, RGB, packed, every row row_len bytes of runs of 128. */
static GBytes *
build_rows (guint version, guint side, guint row_len, guint8 value)
{
	GByteArray *out = g_byte_array_new ();
	guint rows = 3 * side, i, j;

	g_byte_array_append (out, (const guint8 *) "8BPS", 4);
	put16 (out, version);
	g_byte_array_append (out, (const guint8 *) "\0\0\0\0\0\0", 6);
	put16 (out, 3);
	put32 (out, side);
	put32 (out, side);
	put16 (out, 8);
	put16 (out, 3);
	put32 (out, 0);
	put32 (out, 0);
	if (version == 2) {
		put32 (out, 0);
	}
	put32 (out, 0);
	put16 (out, 1);

	for (i = 0; i < rows; i++) {
		if (version == 2) {
			put32 (out, row_len);
		} else {
			put16 (out, row_len);
		}
	}
	for (i = 0; i < rows; i++) {
		for (j = 0; j < row_len; j++) {
			guint8 b = j % 2 == 0 ? 0x81 : value;

			g_byte_array_append (out, &b, 1);
		}
	}

	return g_byte_array_free_to_bytes (out);
}

/* PackBits needs two bytes for every 128 of a row. A row shorter than that
 * was padded out, so a file of zero-length rows cost its declared size in
 * work: 180 KB took seconds. Rows at the least a whole row can take are read,
 * and one byte less is refused. */
static void
test_short_rows (void)
{
	guint version;

	for (version = 1; version <= 2; version++) {
		g_autoptr (GBytes) whole = build_rows (version, 256, 4, 200);
		g_autoptr (GBytes) short_by_one = build_rows (version, 256, 3, 200);
		g_autoptr (GBytes) empty_rows = build_rows (version, version == 1 ? 30000 : 60000, 0, 0);
		g_autoptr (GdkPixbuf) read = read_bytes (whole, 64);
		g_autoptr (GdkPixbuf) refused = read_bytes (short_by_one, 64);
		g_autoptr (GdkPixbuf) bomb = NULL;
		gint64 start;
		double secs;

		check (pixel_is (read, 10, 10, 200, 200, 200, -1));
		check (refused == NULL);

		start = g_get_monotonic_time ();
		bomb = read_bytes (empty_rows, 256);
		secs = (g_get_monotonic_time () - start) / 1e6;
		check (bomb == NULL);
		if (secs >= 1.0) {
			g_printerr ("  empty rows took %.2f s\n", secs);
		}
		check (secs < 1.0);
	}
}

/* A memory stream never looks at the cancel, so this is the reader's own
 * check between rows. A thumbnail nothing wants any more stops there. */
static void
test_cancelled (void)
{
	g_autoptr (GBytes) whole = build_rows (1, 256, 4, 200);
	g_autoptr (GInputStream) in = g_memory_input_stream_new_from_bytes (whole);
	g_autoptr (GCancellable) cancellable = g_cancellable_new ();
	g_autoptr (GdkPixbuf) read = NULL;
	g_autoptr (GdkPixbuf) stopped = NULL;

	read = read_bytes (whole, 64);
	check (read != NULL);

	g_cancellable_cancel (cancellable);
	stopped = nemo_psd_load (in, 64, cancellable);
	check (stopped == NULL);
}

static void
test_types (void)
{
	check (nemo_psd_type_ok ("image/vnd.adobe.photoshop"));
	check (nemo_psd_type_ok ("image/x-psd"));
	check (nemo_psd_type_ok (".PSD"));
	check (nemo_psd_type_ok (".psb"));
	check (!nemo_psd_type_ok ("image/png"));
	check (!nemo_psd_type_ok (NULL));
}

/* The thumbnail factory is where the render thread asks, so a reader nothing
 * calls would pass everything above and still draw no thumbnail. */
static void
test_factory (const char *dir)
{
	Spec spec = { 1, 3, 2, 2, 8, 3, 0, TRUE, NULL, rgb_planes };
	g_autoptr (GBytes) bytes = build (&spec);
	g_autofree char *path = g_build_filename (dir, "art.psd", NULL);
	g_autofree char *uri = g_filename_to_uri (path, NULL, NULL);
	NemoDesktopThumbnailFactory *factory;
	g_autoptr (GdkPixbuf) pixbuf = NULL;

	check (g_file_set_contents (path, g_bytes_get_data (bytes, NULL), g_bytes_get_size (bytes), NULL));

	factory = nemo_desktop_thumbnail_factory_new (NEMO_DESKTOP_THUMBNAIL_SIZE_LARGE);
	check (nemo_desktop_thumbnail_factory_can_make (factory, uri, "image/vnd.adobe.photoshop"));

	pixbuf = nemo_desktop_thumbnail_factory_generate_thumbnail_at_size (factory, uri,
									    "image/vnd.adobe.photoshop", 128, NULL);
	check (pixel_is (pixbuf, 1, 1, 20, 40, 60, -1));

	g_object_unref (factory);
}

int
main (void)
{
	g_autofree char *home = test_scratch_config_home ("nemo-psd-XXXXXX");

	nemo_config_init ();

	test_rgb ();
	test_alpha ();
	test_other_modes ();
	test_shrink ();
	test_truncated ();
	test_short_rows ();
	test_cancelled ();
	test_types ();
	test_factory (home);

	if (failures == 0)
		g_print ("nemo-psd: all checks passed\n");

	return failures == 0 ? 0 : 1;
}
