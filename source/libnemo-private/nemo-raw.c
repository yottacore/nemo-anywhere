/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */

/* nemo-raw.c - thumbnails of camera raw files.

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

#include "nemo-raw.h"

#include <stdlib.h>
#include <string.h>

/* Past these a file is taken to be damaged rather than unusual. */
#define MAX_ENTRIES   512
#define MAX_IFDS      32
#define MAX_STRIPS    256
#define MAX_PREVIEWS  16
#define MAX_JPEG      (64 * 1024 * 1024)
#define MAX_RGB_SIDE  4096
#define MAX_READS     4096

/* Under this a preview is the camera's index thumbnail, which is often
 * letterboxed to 4:3, so a bigger one is used even for a small draw. */
#define SMALL_PREVIEW 320

typedef struct {
	GInputStream *in;
	GCancellable *cancellable;
	goffset       size;
	guint         reads_left;
} Reader;

static gboolean
read_at (Reader *r, goffset at, void *buf, gsize len)
{
	gsize got = 0;

	if (r->reads_left == 0 || at < 0 || at > r->size || (goffset) len > r->size - at) {
		return FALSE;
	}
	r->reads_left--;
	if (!g_seekable_seek (G_SEEKABLE (r->in), at, G_SEEK_SET, r->cancellable, NULL)) {
		return FALSE;
	}

	return g_input_stream_read_all (r->in, buf, len, &got, r->cancellable, NULL) &&
	       got == len;
}

static guint32
be32 (const guint8 *p)
{
	return ((guint32) p[0] << 24) | ((guint32) p[1] << 16) | ((guint32) p[2] << 8) | p[3];
}

typedef struct {
	gboolean rgb;		/* uncompressed 8 bit RGB, else a JPEG */
	goffset  offset;
	goffset  length;
	guint    width, height;
} Preview;

typedef struct {
	Reader  *r;
	goffset  base;		/* TIFF offsets count from its header */
	gboolean big_endian;
	guint    ifds;		/* read so far, across every chain */
	guint    orientation;	/* 0 until the first directory gives one */
	Preview  found[MAX_PREVIEWS];
	guint    n_found;
} Scan;

static guint
u16 (const Scan *s, const guint8 *p)
{
	return s->big_endian ? (p[0] << 8) | p[1] : (p[1] << 8) | p[0];
}

static guint32
u32 (const Scan *s, const guint8 *p)
{
	if (s->big_endian) {
		return be32 (p);
	}

	return ((guint32) p[3] << 24) | ((guint32) p[2] << 16) | ((guint32) p[1] << 8) | p[0];
}

static void
add (Scan *s, gboolean rgb, goffset offset, goffset length, guint width, guint height)
{
	guint i;

	for (i = 0; i < s->n_found; i++) {
		if (s->found[i].offset == offset) {
			return;
		}
	}
	if (s->n_found < MAX_PREVIEWS) {
		s->found[s->n_found++] = (Preview) { rgb, offset, length, width, height };
	}
}

/* Walks the markers to the frame header for its size. Several cameras store
 * the sensor data itself as a lossless or 12 bit JPEG, which the toolkit
 * cannot decode, so only an ordinary 8 bit frame counts. */
static gboolean
jpeg_size (Reader *r, goffset at, goffset length, guint *width, guint *height)
{
	goffset end = at + length;
	guint8 b[5];
	guint markers;

	if (length < 4 || !read_at (r, at, b, 2) || b[0] != 0xFF || b[1] != 0xD8) {
		return FALSE;
	}
	at += 2;

	for (markers = 0; markers < 64 && at + 4 <= end; markers++) {
		guint type, len;

		if (!read_at (r, at, b, 4) || b[0] != 0xFF) {
			return FALSE;
		}
		type = b[1];
		if (type == 0xFF) {
			at++;
			continue;
		}
		len = (b[2] << 8) | b[3];

		if (type >= 0xC0 && type <= 0xC2) {
			if (len < 8 || at + 9 > end || !read_at (r, at + 4, b, 5)) {
				return FALSE;
			}
			*height = (b[1] << 8) | b[2];
			*width = (b[3] << 8) | b[4];

			return b[0] == 8 && *width > 0 && *height > 0;
		}
		if ((type >= 0xC3 && type <= 0xCF && type != 0xC4 && type != 0xC8 && type != 0xCC) ||
		    type == 0xD9 || type == 0xDA || len < 2) {
			return FALSE;
		}
		at += 2 + len;
	}

	return FALSE;
}

static void
add_jpeg (Scan *s, goffset offset, goffset length)
{
	guint width, height;

	if (length > 0 && length <= MAX_JPEG && offset >= 0 && offset <= s->r->size - length &&
	    jpeg_size (s->r, offset, length, &width, &height)) {
		add (s, FALSE, offset, length, width, height);
	}
}

/* Value number index of a SHORT, LONG or IFD entry. */
static gboolean
entry_value (Scan *s, const guint8 *entry, guint32 index, guint32 *value)
{
	guint32 count = u32 (s, entry + 4);
	guint width;
	guint8 b[4];

	switch (u16 (s, entry + 2)) {
	case 3:
		width = 2;
		break;
	case 4:
	case 13:
		width = 4;
		break;
	default:
		return FALSE;
	}
	if (index >= count) {
		return FALSE;
	}

	if ((guint64) count * width <= 4) {
		memcpy (b, entry + 8 + index * width, width);
	} else if (!read_at (s->r, s->base + u32 (s, entry + 8) + (goffset) index * width, b, width)) {
		return FALSE;
	}
	*value = width == 2 ? u16 (s, b) : u32 (s, b);

	return TRUE;
}

/* The strips as one run of bytes. Previews are written in one piece, so a
 * scattered image is left alone. */
static gboolean
strip_run (Scan *s, const guint8 *offsets, const guint8 *counts, goffset *at, goffset *length)
{
	guint32 n = u32 (s, offsets + 4), i;
	goffset end = 0;

	if (n == 0 || n > MAX_STRIPS || u32 (s, counts + 4) != n) {
		return FALSE;
	}

	for (i = 0; i < n; i++) {
		guint32 offset, len;

		if (!entry_value (s, offsets, i, &offset) || !entry_value (s, counts, i, &len)) {
			return FALSE;
		}
		if (i == 0) {
			*at = end = s->base + offset;
		} else if (s->base + offset != end) {
			return FALSE;
		}
		end += len;
	}
	*length = end - *at;

	return TRUE;
}

/* The first value of one tag in a directory, or for an opaque tag where it
 * starts. */
static gboolean
ifd_tag (Scan *s, guint32 offset, guint tag, guint32 *value)
{
	guint8 head[2], e[12];
	guint n, i;

	if (!read_at (s->r, s->base + offset, head, 2)) {
		return FALSE;
	}
	n = MIN (u16 (s, head), MAX_ENTRIES);

	for (i = 0; i < n; i++) {
		if (!read_at (s->r, s->base + offset + 2 + (goffset) i * 12, e, 12)) {
			return FALSE;
		}
		if (u16 (s, e) == tag) {
			if (u16 (s, e + 2) == 7) {
				*value = u32 (s, e + 8);
				return TRUE;
			}
			return entry_value (s, e, 0, value);
		}
	}

	return FALSE;
}

/* Olympus keeps its preview in the maker note, under a directory of camera
 * settings. Offsets in the note count from the note's own start. */
static void
scan_olympus (Scan *s, goffset at, goffset length)
{
	goffset base = s->base;
	gboolean big_endian = s->big_endian;
	guint32 settings, start, len;
	guint8 b[16];
	guint head;

	if (length < 16 || !read_at (s->r, at, b, 16)) {
		return;
	}
	if (memcmp (b, "OLYMPUS\0", 8) == 0) {
		head = 8;
	} else if (memcmp (b, "OM SYSTEM\0\0\0", 12) == 0) {
		head = 12;
	} else {
		return;
	}
	if (b[head] == 'I' && b[head + 1] == 'I') {
		s->big_endian = FALSE;
	} else if (b[head] == 'M' && b[head + 1] == 'M') {
		s->big_endian = TRUE;
	} else {
		return;
	}
	s->base = at;

	if (ifd_tag (s, head + 4, 0x2020, &settings) &&
	    ifd_tag (s, settings, 0x101, &start) &&
	    ifd_tag (s, settings, 0x102, &len)) {
		add_jpeg (s, at + start, len);
	}

	s->base = base;
	s->big_endian = big_endian;
}

/* Returns the next directory in the chain, or 0. */
static guint32
walk_ifd (Scan *s, guint32 offset, guint depth, gboolean first)
{
	g_autofree guint8 *entries = NULL;
	const guint8 *strips = NULL, *strip_counts = NULL, *subifds = NULL;
	guint32 width = 0, height = 0, bits = 0, compression = 0, photometric = 0;
	guint32 samples = 0, planar = 1, jpeg_at = 0, jpeg_len = 0, exif = 0;
	guint8 head[2];
	guint n, i;

	if (++s->ifds > MAX_IFDS || !read_at (s->r, s->base + offset, head, 2)) {
		return 0;
	}
	n = u16 (s, head);
	if (n == 0 || n > MAX_ENTRIES) {
		return 0;
	}
	entries = g_malloc ((gsize) n * 12 + 4);
	if (!read_at (s->r, s->base + offset + 2, entries, (gsize) n * 12 + 4)) {
		return 0;
	}

	for (i = 0; i < n; i++) {
		const guint8 *e = entries + (gsize) i * 12;
		guint32 v = 0;

		switch (u16 (s, e)) {
		case 0x100:
			entry_value (s, e, 0, &width);
			break;
		case 0x101:
			entry_value (s, e, 0, &height);
			break;
		case 0x102:
			entry_value (s, e, 0, &bits);
			break;
		case 0x103:
			entry_value (s, e, 0, &compression);
			break;
		case 0x106:
			entry_value (s, e, 0, &photometric);
			break;
		case 0x111:
			strips = e;
			break;
		case 0x112:
			/* Only the first directory speaks for the whole file. */
			if (depth == 0 && first && entry_value (s, e, 0, &v)) {
				s->orientation = v;
			}
			break;
		case 0x115:
			entry_value (s, e, 0, &samples);
			break;
		case 0x117:
			strip_counts = e;
			break;
		case 0x11C:
			entry_value (s, e, 0, &planar);
			break;
		case 0x14A:
			subifds = e;
			break;
		case 0x201:
			entry_value (s, e, 0, &jpeg_at);
			break;
		case 0x202:
			entry_value (s, e, 0, &jpeg_len);
			break;
		case 0x8769:
			if (depth == 0) {
				entry_value (s, e, 0, &exif);
			}
			break;
		case 0x927C:
			if (u16 (s, e + 2) == 7) {
				scan_olympus (s, s->base + u32 (s, e + 8), u32 (s, e + 4));
			}
			break;
		case 0x2E:
			/* Panasonic keeps its preview as one opaque tag. */
			if (u16 (s, e + 2) == 7) {
				add_jpeg (s, s->base + u32 (s, e + 8), u32 (s, e + 4));
			}
			break;
		default:
			break;
		}
	}

	if (jpeg_at != 0 && jpeg_len != 0) {
		add_jpeg (s, s->base + jpeg_at, jpeg_len);
	}

	if (strips != NULL && strip_counts != NULL) {
		goffset at = 0, length = 0;

		if (strip_run (s, strips, strip_counts, &at, &length)) {
			if (compression == 6 || compression == 7) {
				add_jpeg (s, at, length);
			} else if (compression == 1 && photometric == 2 && samples == 3 &&
				   bits == 8 && planar == 1 &&
				   width > 0 && height > 0 &&
				   width <= MAX_RGB_SIDE && height <= MAX_RGB_SIDE &&
				   length >= (goffset) width * height * 3 && at <= s->r->size - length) {
				add (s, TRUE, at, length, width, height);
			}
		}
	}

	if (exif != 0) {
		walk_ifd (s, exif, depth + 1, FALSE);
	}

	if (subifds != NULL && depth < 2) {
		guint32 count = MIN (u32 (s, subifds + 4), 8), sub;

		for (i = 0; i < count; i++) {
			if (entry_value (s, subifds, i, &sub) && sub != 0) {
				walk_ifd (s, sub, depth + 1, FALSE);
			}
		}
	}

	return u32 (s, entries + (gsize) n * 12);
}

static void
scan_tiff (Scan *s, goffset base)
{
	guint8 head[8];
	guint32 offset;
	guint magic;
	gboolean first = TRUE;

	if (!read_at (s->r, base, head, 8)) {
		return;
	}
	if (head[0] == 'I' && head[1] == 'I') {
		s->big_endian = FALSE;
	} else if (head[0] == 'M' && head[1] == 'M') {
		s->big_endian = TRUE;
	} else {
		return;
	}
	s->base = base;

	/* 42 is plain TIFF. Panasonic writes 0x55, and Olympus "RO" or "RS". */
	magic = u16 (s, head + 2);
	if (magic != 42 && magic != 0x55 && magic != 0x4F52 && magic != 0x5352) {
		return;
	}

	/* The directory limit ends a chain that loops. */
	for (offset = u32 (s, head + 4); offset != 0; first = FALSE) {
		offset = walk_ifd (s, offset, 0, first);
	}
}

static void
scan_raf (Scan *s)
{
	guint8 b[8];

	if (read_at (s->r, 84, b, 8)) {
		add_jpeg (s, be32 (b), be32 (b + 4));
	}
}

/* Finds a box of the given type from from to to, and for a uuid box, of the
 * given id. The body is what follows the header and any id. */
static gboolean
find_box (Reader *r, goffset from, goffset to, const char *type, const guint8 *uuid,
	  goffset *body, goffset *end)
{
	guint boxes;

	for (boxes = 0; boxes < 64 && from >= 0 && from + 8 <= to; boxes++) {
		guint8 b[16], id[16];
		guint64 size;
		goffset head = 8;

		if (!read_at (r, from, b, 8)) {
			return FALSE;
		}
		size = be32 (b);
		if (size == 1) {
			if (!read_at (r, from + 8, b + 8, 8)) {
				return FALSE;
			}
			size = ((guint64) be32 (b + 8) << 32) | be32 (b + 12);
			head = 16;
		} else if (size == 0) {
			size = to - from;
		}
		if (size < (guint64) head || size > (guint64) (to - from)) {
			return FALSE;
		}

		if (memcmp (b + 4, type, 4) == 0) {
			if (uuid == NULL) {
				*body = from + head;
				*end = from + size;
				return TRUE;
			}
			if (size >= (guint64) head + 16 && read_at (r, from + head, id, 16) &&
			    memcmp (id, uuid, 16) == 0) {
				*body = from + head + 16;
				*end = from + size;
				return TRUE;
			}
		}
		from += size;
	}

	return FALSE;
}

static void
scan_cr3 (Scan *s)
{
	static const guint8 preview_id[16] = {
		0xEA, 0xF4, 0x2B, 0x5E, 0x1C, 0x98, 0x4B, 0x88,
		0xB9, 0xFB, 0xB7, 0xDC, 0x40, 0x6E, 0x4D, 0x16,
	};
	static const guint8 canon_id[16] = {
		0x85, 0xC0, 0xB6, 0x87, 0x82, 0x0F, 0x11, 0xE0,
		0x81, 0x11, 0xF4, 0xCE, 0x46, 0x2B, 0x6A, 0x48,
	};
	goffset body, end, inner, inner_end;
	guint8 b[16];

	/* The preview box sits in a top level box of Canon's own, after eight
	 * bytes of its own. Its JPEG follows sixteen bytes of size fields. */
	if (find_box (s->r, 0, s->r->size, "uuid", preview_id, &body, &end) &&
	    find_box (s->r, body + 8, end, "PRVW", NULL, &inner, &inner_end) &&
	    read_at (s->r, inner, b, 16) && inner_end - inner > 16) {
		add_jpeg (s, inner + 16, MIN ((goffset) be32 (b + 12), inner_end - inner - 16));
	}

	/* The orientation is in a TIFF directory inside the movie box. */
	if (find_box (s->r, 0, s->r->size, "moov", NULL, &body, &end) &&
	    find_box (s->r, body, end, "uuid", canon_id, &inner, &inner_end) &&
	    find_box (s->r, inner, inner_end, "CMT1", NULL, &body, &end)) {
		scan_tiff (s, body);
	}
}

static void
on_size_prepared (GdkPixbufLoader *loader, int width, int height, gpointer data)
{
	int size = GPOINTER_TO_INT (data);
	double scale;

	if (width <= size && height <= size) {
		return;
	}
	scale = (double) size / MAX (width, height);
	gdk_pixbuf_loader_set_size (loader,
				    CLAMP ((int) (width * scale + 0.5), 1, size),
				    CLAMP ((int) (height * scale + 0.5), 1, size));
}

/* Decoded already shrunk, which for a JPEG costs a fraction of a full decode. */
static GdkPixbuf *
decode_jpeg (Reader *r, const Preview *p, int size)
{
	g_autoptr (GdkPixbufLoader) loader = gdk_pixbuf_loader_new_with_type ("jpeg", NULL);
	g_autofree guint8 *buf = NULL;
	goffset left = p->length;
	gboolean ok, closed;
	GdkPixbuf *pixbuf;

	if (loader == NULL) {
		return NULL;
	}
	g_signal_connect (loader, "size-prepared", G_CALLBACK (on_size_prepared), GINT_TO_POINTER (size));

	buf = g_malloc (64 * 1024);
	ok = g_seekable_seek (G_SEEKABLE (r->in), p->offset, G_SEEK_SET, r->cancellable, NULL);
	while (ok && left > 0) {
		gsize want = MIN (left, 64 * 1024), got = 0;

		ok = g_input_stream_read_all (r->in, buf, want, &got, r->cancellable, NULL) &&
		     got == want && gdk_pixbuf_loader_write (loader, buf, got, NULL);
		left -= want;
	}
	closed = gdk_pixbuf_loader_close (loader, NULL);
	if (!ok || !closed) {
		return NULL;
	}

	pixbuf = gdk_pixbuf_loader_get_pixbuf (loader);

	return pixbuf != NULL ? g_object_ref (pixbuf) : NULL;
}

static GdkPixbuf *
decode_rgb (Reader *r, const Preview *p)
{
	GdkPixbuf *pixbuf = gdk_pixbuf_new (GDK_COLORSPACE_RGB, FALSE, 8, p->width, p->height);
	guint8 *pixels;
	int stride;
	guint y;

	if (pixbuf == NULL) {
		return NULL;
	}
	pixels = gdk_pixbuf_get_pixels (pixbuf);
	stride = gdk_pixbuf_get_rowstride (pixbuf);

	for (y = 0; y < p->height; y++) {
		if (!read_at (r, p->offset + (goffset) y * p->width * 3,
			      pixels + (gsize) y * stride, (gsize) p->width * 3)) {
			g_object_unref (pixbuf);
			return NULL;
		}
	}

	return pixbuf;
}

/* The smallest preview that covers the draw comes first, then the rest from
 * the biggest down. */
static guint
rank (const Preview *p, guint target)
{
	guint side = MAX (p->width, p->height);

	return side >= target ? side : 0x10000 + (0xFFFF - MIN (side, 0xFFFF));
}

/* Returns: (transfer full): unref with g_object_unref */
GdkPixbuf *
nemo_raw_load (GInputStream *stream, int size, GCancellable *cancellable)
{
	/* A chain that loops, with every entry pointing at the same maker note or
	 * preview, repeats its reads up to MAX_IFDS times over, and each is a
	 * seek and a read on the file. Real files need under fifty. */
	Reader r = { stream, cancellable, 0, MAX_READS };
	Scan s = { 0 };
	GdkPixbuf *pixbuf = NULL;
	const char *own;
	guint8 head[16];
	guint i, j, target, orientation;

	g_return_val_if_fail (G_IS_SEEKABLE (stream), NULL);

	if (!g_seekable_can_seek (G_SEEKABLE (stream)) ||
	    !g_seekable_seek (G_SEEKABLE (stream), 0, G_SEEK_END, cancellable, NULL)) {
		return NULL;
	}
	r.size = g_seekable_tell (G_SEEKABLE (stream));
	s.r = &r;

	if (!read_at (&r, 0, head, sizeof head)) {
		return NULL;
	}
	if (memcmp (head, "FUJIFILMCCD-RAW", 15) == 0) {
		scan_raf (&s);
	} else if (memcmp (head + 4, "ftypcrx ", 8) == 0) {
		scan_cr3 (&s);
	} else {
		scan_tiff (&s, 0);
	}
	/* Reading the picture found is bounded by its own size. */
	r.reads_left = G_MAXUINT;

	size = CLAMP (size, 1, 4096);
	target = MAX (size, SMALL_PREVIEW);
	for (i = 1; i < s.n_found; i++) {
		Preview p = s.found[i];

		for (j = i; j > 0 && rank (&s.found[j - 1], target) > rank (&p, target); j--) {
			s.found[j] = s.found[j - 1];
		}
		s.found[j] = p;
	}

	/* One that will not decode gives way to the next. */
	for (i = 0; i < s.n_found && pixbuf == NULL; i++) {
		if (g_cancellable_is_cancelled (cancellable)) {
			return NULL;
		}
		pixbuf = s.found[i].rgb ? decode_rgb (&r, &s.found[i]) : decode_jpeg (&r, &s.found[i], size);
	}
	if (pixbuf == NULL) {
		return NULL;
	}

	/* The file's own orientation wins over any the JPEG carries, since the
	 * preview is stored the way the sensor saw it. */
	own = gdk_pixbuf_get_option (pixbuf, "orientation");
	orientation = s.orientation >= 1 && s.orientation <= 8 ? s.orientation :
		      own != NULL ? (guint) atoi (own) : 0;

	if (gdk_pixbuf_get_width (pixbuf) > size || gdk_pixbuf_get_height (pixbuf) > size) {
		int width = gdk_pixbuf_get_width (pixbuf), height = gdk_pixbuf_get_height (pixbuf);
		double scale = (double) size / MAX (width, height);
		GdkPixbuf *scaled;

		scaled = gdk_pixbuf_scale_simple (pixbuf,
						  CLAMP ((int) (width * scale + 0.5), 1, size),
						  CLAMP ((int) (height * scale + 0.5), 1, size),
						  GDK_INTERP_BILINEAR);
		g_object_unref (pixbuf);
		pixbuf = scaled;
		if (pixbuf == NULL) {
			return NULL;
		}
	}

	if (orientation >= 1 && orientation <= 8) {
		char value[2] = { (char) ('0' + orientation), '\0' };

		gdk_pixbuf_remove_option (pixbuf, "orientation");
		gdk_pixbuf_set_option (pixbuf, "orientation", value);
	}

	return pixbuf;
}

/* Returns: (transfer full): unref with g_object_unref */
GdkPixbuf *
nemo_raw_load_uri (const char *uri, int size, GCancellable *cancellable)
{
	g_autoptr (GFile) file = g_file_new_for_uri (uri);
	g_autoptr (GFileInputStream) in = NULL;

	in = g_file_read (file, cancellable, NULL);
	if (in == NULL || !g_seekable_can_seek (G_SEEKABLE (in))) {
		return NULL;
	}

	return nemo_raw_load (G_INPUT_STREAM (in), size, cancellable);
}

gboolean
nemo_raw_type_ok (const char *mime_type)
{
	/* Canon CRW, Minolta MRW and Sigma X3F are other containers, and
	 * Panasonic's .raw name is taken by too much else. */
	static const char *types[] = {
		"image/x-adobe-dng",
		"image/x-canon-cr2",
		"image/x-canon-cr3",
		"image/x-fuji-raf",
		"image/x-kodak-dcr",
		"image/x-kodak-k25",
		"image/x-kodak-kdc",
		"image/x-nikon-nef",
		"image/x-nikon-nrw",
		"image/x-olympus-orf",
		"image/x-panasonic-rw2",
		"image/x-pentax-pef",
		"image/x-samsung-srw",
		"image/x-sony-arw",
		"image/x-sony-sr2",
		"image/x-sony-srf",
	};
	/* On Windows the type is the extension. */
	static const char *extensions[] = {
		".dng", ".cr2", ".cr3", ".raf", ".dcr", ".k25", ".kdc", ".nef",
		".nrw", ".orf", ".rw2", ".rwl", ".pef", ".srw", ".arw", ".sr2",
		".srf", ".erf", ".3fr", ".iiq",
	};
	guint i;

	if (mime_type == NULL) {
		return FALSE;
	}

	for (i = 0; i < G_N_ELEMENTS (extensions); i++) {
		if (g_ascii_strcasecmp (mime_type, extensions[i]) == 0) {
			return TRUE;
		}
	}
	for (i = 0; i < G_N_ELEMENTS (types); i++) {
		if (g_ascii_strcasecmp (mime_type, types[i]) == 0) {
			return TRUE;
		}
	}

	return FALSE;
}
