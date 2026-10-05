/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */

/* nemo-psd.c - thumbnails of Photoshop files.

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

#include <config.h>

#include "nemo-psd.h"

#include <string.h>

#define MODE_GRAY     1
#define MODE_INDEXED  2
#define MODE_RGB      3
#define MODE_CMYK     4
#define MODE_DUOTONE  8

/* The limits the format itself sets, for .psd and .psb. */
#define PSD_MAX_SIDE  30000
#define PSB_MAX_SIDE  300000
#define MAX_CHANNELS  56

typedef struct {
	GInputStream *in;
	GCancellable *cancellable;
	gboolean      big;	/* .psb: wider lengths and row counts */
} Reader;

static gboolean
get (Reader *r, void *buf, gsize len)
{
	gsize got = 0;

	return g_input_stream_read_all (r->in, buf, len, &got, r->cancellable, NULL) &&
	       got == len;
}

static gboolean
skip (Reader *r, guint64 len)
{
	while (len > 0) {
		gssize n = g_input_stream_skip (r->in, MIN (len, G_MAXSSIZE), r->cancellable, NULL);

		if (n <= 0) {
			return FALSE;
		}
		len -= n;
	}

	return TRUE;
}

static gboolean
get_u16 (Reader *r, guint *value)
{
	guint8 b[2];

	if (!get (r, b, 2)) {
		return FALSE;
	}
	*value = (b[0] << 8) | b[1];

	return TRUE;
}

static gboolean
get_u32 (Reader *r, guint32 *value)
{
	guint8 b[4];

	if (!get (r, b, 4)) {
		return FALSE;
	}
	*value = ((guint32) b[0] << 24) | ((guint32) b[1] << 16) | ((guint32) b[2] << 8) | b[3];

	return TRUE;
}

/* A section length, which .psb doubles for the layer section only. */
static gboolean
get_len (Reader *r, gboolean wide, guint64 *value)
{
	guint32 hi = 0, lo;

	if (wide && !get_u32 (r, &hi)) {
		return FALSE;
	}
	if (!get_u32 (r, &lo)) {
		return FALSE;
	}
	*value = ((guint64) hi << 32) | lo;

	return TRUE;
}

/* PackBits. A row that runs out early is padded rather than refused, since a
 * thumbnail with one bad row still beats no thumbnail. One too short to ever
 * fill is refused before this, in load. */
static void
unpack_row (const guint8 *in, gsize in_len, guint8 *out, gsize out_len)
{
	gsize i = 0, o = 0;

	while (i < in_len && o < out_len) {
		gint8 n = (gint8) in[i++];

		if (n >= 0) {
			gsize run = MIN ((gsize) n + 1, MIN (in_len - i, out_len - o));

			memcpy (out + o, in + i, run);
			i += run;
			o += run;
		} else if (n != -128 && i < in_len) {
			gsize run = MIN ((gsize) (1 - n), out_len - o);

			memset (out + o, in[i++], run);
			o += run;
		}
	}

	memset (out + o, 0, out_len - o);
}

typedef struct {
	guint    width, height;
	guint    depth_bytes;
	guint    factor;	/* each cell averages factor x factor pixels */
	guint    cells_w, cells_h;
	gboolean nearest;	/* palette indexes cannot be averaged */
	guint32 *sums;
} Shrink;

static void
shrink_row (Shrink *s, const guint8 *line, guint y, guint8 *plane)
{
	guint cy = y / s->factor;
	guint x;

	if (s->nearest) {
		if (y % s->factor == 0) {
			for (x = 0; x < s->cells_w; x++) {
				plane[cy * s->cells_w + x] = line[(gsize) x * s->factor * s->depth_bytes];
			}
		}
		return;
	}

	guint32 *row = s->sums + (gsize) cy * s->cells_w;

	/* The high byte of a 16 bit sample comes first. */
	for (x = 0; x < s->width; x++) {
		row[x / s->factor] += line[(gsize) x * s->depth_bytes];
	}
}

static void
shrink_finish (Shrink *s, guint8 *plane)
{
	guint cx, cy;

	if (s->nearest) {
		return;
	}

	for (cy = 0; cy < s->cells_h; cy++) {
		guint rows = MIN (s->factor, s->height - cy * s->factor);

		for (cx = 0; cx < s->cells_w; cx++) {
			guint cols = MIN (s->factor, s->width - cx * s->factor);
			gsize at = (gsize) cy * s->cells_w + cx;

			plane[at] = s->sums[at] / (rows * cols);
			s->sums[at] = 0;
		}
	}
}

static GdkPixbuf *
to_pixbuf (guint mode, guint8 **planes, gboolean alpha, const guint8 *palette, Shrink *s)
{
	GdkPixbuf *pixbuf;
	guint8 *pixels;
	int stride;
	guint x, y;

	pixbuf = gdk_pixbuf_new (GDK_COLORSPACE_RGB, alpha, 8, s->cells_w, s->cells_h);
	if (pixbuf == NULL) {
		return NULL;
	}

	pixels = gdk_pixbuf_get_pixels (pixbuf);
	stride = gdk_pixbuf_get_rowstride (pixbuf);

	for (y = 0; y < s->cells_h; y++) {
		guint8 *p = pixels + (gsize) y * stride;

		for (x = 0; x < s->cells_w; x++) {
			gsize at = (gsize) y * s->cells_w + x;
			guint alpha_plane = 1;

			switch (mode) {
			case MODE_INDEXED:
				p[0] = palette[planes[0][at]];
				p[1] = palette[256 + planes[0][at]];
				p[2] = palette[512 + planes[0][at]];
				break;
			case MODE_RGB:
				p[0] = planes[0][at];
				p[1] = planes[1][at];
				p[2] = planes[2][at];
				alpha_plane = 3;
				break;
			case MODE_CMYK:
				/* Stored as 255 for no ink. */
				p[0] = planes[0][at] * planes[3][at] / 255;
				p[1] = planes[1][at] * planes[3][at] / 255;
				p[2] = planes[2][at] * planes[3][at] / 255;
				alpha_plane = 4;
				break;
			default:
				p[0] = p[1] = p[2] = planes[0][at];
				break;
			}

			if (alpha) {
				p[3] = planes[alpha_plane][at];
				p += 4;
			} else {
				p += 3;
			}
		}
	}

	return pixbuf;
}

static GdkPixbuf *
load (Reader *r, int size)
{
	guint8 sig[4], reserved[6];
	guint version, channels, depth, mode, compression;
	guint32 height, width, len32;
	guint64 len, info_len;
	gint16 layer_count = 0;
	guint8 palette[768] = { 0 };
	guint base, used, c, y;
	gboolean alpha;
	Shrink s = { 0 };
	g_autofree guint8 *line = NULL;
	g_autofree guint8 *packed = NULL;
	g_autofree guint32 *counts = NULL;
	guint8 *planes[5] = { NULL };
	gsize row_bytes, packed_max = 0;
	GdkPixbuf *pixbuf = NULL;

	if (!get (r, sig, 4) || memcmp (sig, "8BPS", 4) != 0 ||
	    !get_u16 (r, &version) || (version != 1 && version != 2) ||
	    !get (r, reserved, 6) ||
	    !get_u16 (r, &channels) || !get_u32 (r, &height) || !get_u32 (r, &width) ||
	    !get_u16 (r, &depth) || !get_u16 (r, &mode)) {
		return NULL;
	}
	r->big = version == 2;

	if (channels < 1 || channels > MAX_CHANNELS ||
	    width < 1 || height < 1 ||
	    width > (r->big ? PSB_MAX_SIDE : PSD_MAX_SIDE) ||
	    height > (r->big ? PSB_MAX_SIDE : PSD_MAX_SIDE) ||
	    (depth != 8 && depth != 16)) {
		return NULL;
	}

	switch (mode) {
	case MODE_GRAY:
	case MODE_DUOTONE:
	case MODE_INDEXED:
		base = 1;
		break;
	case MODE_RGB:
		base = 3;
		break;
	case MODE_CMYK:
		base = 4;
		break;
	default:
		return NULL;
	}
	if (channels < base) {
		return NULL;
	}

	/* Color mode data: the palette, for an indexed file. */
	if (!get_u32 (r, &len32)) {
		return NULL;
	}
	if (mode == MODE_INDEXED) {
		if (len32 < sizeof palette || !get (r, palette, sizeof palette) ||
		    !skip (r, len32 - sizeof palette)) {
			return NULL;
		}
	} else if (!skip (r, len32)) {
		return NULL;
	}

	/* Image resources. */
	if (!get_u32 (r, &len32) || !skip (r, len32)) {
		return NULL;
	}

	/* Layers and masks. The layer count is read for its sign alone: negative
	 * means the first extra channel of the flattened copy is its
	 * transparency, rather than a saved selection. */
	if (!get_len (r, r->big, &len)) {
		return NULL;
	}
	if (len > 0) {
		guint64 used_len = r->big ? 8 : 4;
		guint count;

		if (len < used_len || !get_len (r, r->big, &info_len)) {
			return NULL;
		}
		if (info_len >= 2) {
			if (len < used_len + 2 || !get_u16 (r, &count)) {
				return NULL;
			}
			layer_count = (gint16) count;
			used_len += 2;
		}
		if (!skip (r, len - used_len)) {
			return NULL;
		}
	}

	if (!get_u16 (r, &compression) || compression > 1) {
		return NULL;
	}

	alpha = layer_count < 0 && channels > base;
	used = base + (alpha ? 1 : 0);

	size = CLAMP (size, 1, 4096);
	s.width = width;
	s.height = height;
	s.depth_bytes = depth / 8;
	s.factor = MAX (1, MAX (width, height) / size);
	s.cells_w = (width + s.factor - 1) / s.factor;
	s.cells_h = (height + s.factor - 1) / s.factor;
	s.nearest = mode == MODE_INDEXED;

	row_bytes = (gsize) width * s.depth_bytes;
	line = g_malloc (row_bytes);

	if (compression == 1) {
		gsize entries = (gsize) used * height;
		/* PackBits gets at most 128 bytes out of 2. A row shorter than that
		 * cannot be whole, and padding it out would let a small file cost
		 * as much work as its declared size. */
		gsize packed_min = (row_bytes + 127) / 128 * 2;
		gsize i;

		counts = g_new (guint32, entries);
		for (i = 0; i < entries; i++) {
			guint narrow;

			if (r->big) {
				if (!get_u32 (r, &counts[i])) {
					return NULL;
				}
			} else {
				if (!get_u16 (r, &narrow)) {
					return NULL;
				}
				counts[i] = narrow;
			}
			if (counts[i] < packed_min) {
				return NULL;
			}
			packed_max = MAX (packed_max, counts[i]);
		}
		/* Worst case PackBits grows a row by one byte in 128. */
		if (packed_max > row_bytes + row_bytes / 64 + 16) {
			return NULL;
		}
		if (!skip (r, (guint64) (channels - used) * height * (r->big ? 4 : 2))) {
			return NULL;
		}
		packed = g_malloc (MAX (packed_max, 1));
	}

	if (!s.nearest) {
		s.sums = g_new0 (guint32, (gsize) s.cells_w * s.cells_h);
	}

	for (c = 0; c < used; c++) {
		planes[c] = g_malloc0 ((gsize) s.cells_w * s.cells_h);

		for (y = 0; y < height; y++) {
			/* A read off the buffer never looks at the cancel, and the
			   decoding between reads is most of the time. */
			if (g_cancellable_is_cancelled (r->cancellable)) {
				goto out;
			}
			if (compression == 1) {
				guint32 n = counts[(gsize) c * height + y];

				if (!get (r, packed, n)) {
					goto out;
				}
				unpack_row (packed, n, line, row_bytes);
			} else if (!get (r, line, row_bytes)) {
				goto out;
			}
			shrink_row (&s, line, y, planes[c]);
		}
		shrink_finish (&s, planes[c]);
	}

	pixbuf = to_pixbuf (mode, planes, alpha, palette, &s);

	if (pixbuf != NULL && (s.cells_w > (guint) size || s.cells_h > (guint) size)) {
		double scale = (double) size / MAX (s.cells_w, s.cells_h);
		GdkPixbuf *scaled;

		scaled = gdk_pixbuf_scale_simple (pixbuf,
						  CLAMP ((int) (s.cells_w * scale + 0.5), 1, size),
						  CLAMP ((int) (s.cells_h * scale + 0.5), 1, size),
						  GDK_INTERP_BILINEAR);
		g_object_unref (pixbuf);
		pixbuf = scaled;
	}

out:
	for (c = 0; c < G_N_ELEMENTS (planes); c++) {
		g_free (planes[c]);
	}
	g_free (s.sums);

	return pixbuf;
}

GdkPixbuf *
nemo_psd_load (GInputStream *stream, int size, GCancellable *cancellable)
{
	Reader r = { stream, cancellable, FALSE };

	g_return_val_if_fail (G_IS_INPUT_STREAM (stream), NULL);

	return load (&r, size);
}

GdkPixbuf *
nemo_psd_load_uri (const char *uri, int size, GCancellable *cancellable)
{
	g_autoptr (GFile) file = g_file_new_for_uri (uri);
	g_autoptr (GFileInputStream) in = NULL;
	g_autoptr (GInputStream) buffered = NULL;

	in = g_file_read (file, cancellable, NULL);
	if (in == NULL) {
		return NULL;
	}

	buffered = g_buffered_input_stream_new_sized (G_INPUT_STREAM (in), 64 * 1024);

	return nemo_psd_load (buffered, size, cancellable);
}

gboolean
nemo_psd_type_ok (const char *mime_type)
{
	static const char *types[] = {
		"image/vnd.adobe.photoshop",
		"image/x-psd",
		"image/psd",
		"image/x-photoshop",
		"application/photoshop",
		"application/x-photoshop",
	};
	guint i;

	if (mime_type == NULL) {
		return FALSE;
	}

	/* On Windows the type is the extension, and with no Photoshop installed
	 * nothing maps it to a mime type. */
	if (g_ascii_strcasecmp (mime_type, ".psd") == 0 ||
	    g_ascii_strcasecmp (mime_type, ".psb") == 0) {
		return TRUE;
	}

	for (i = 0; i < G_N_ELEMENTS (types); i++) {
		if (g_ascii_strcasecmp (mime_type, types[i]) == 0) {
			return TRUE;
		}
	}

	return FALSE;
}
