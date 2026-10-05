/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */

/* test-nemo-raw.c - finding and drawing the preview inside a camera raw file.

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

/* The containers are built here around JPEGs made on the spot, since no
 * camera file can be checked in. Each preview is one flat color, so which one
 * got drawn shows in any pixel. */

#include <config.h>

#include <stdlib.h>
#include <string.h>

#include <glib.h>
#include <gio/gio.h>

#include <libnemo-private/nemo-raw.h>
#include <libnemo-private/nemo-config.h>
#include <libnemo-private/nemo-desktop-thumbnail.h>

#include "test-scratch.h"
#include "test-check.h"

typedef struct {
	GByteArray *out;
	gboolean    big;
} W;

static void
w16 (W *w, guint v)
{
	guint8 b[2] = { w->big ? v >> 8 : v, w->big ? v : v >> 8 };

	g_byte_array_append (w->out, b, 2);
}

static void
put32 (const W *w, guint8 *b, guint32 v)
{
	if (w->big) {
		b[0] = v >> 24; b[1] = v >> 16; b[2] = v >> 8; b[3] = v;
	} else {
		b[0] = v; b[1] = v >> 8; b[2] = v >> 16; b[3] = v >> 24;
	}
}

static void
w32 (W *w, guint32 v)
{
	guint8 b[4];

	put32 (w, b, v);
	g_byte_array_append (w->out, b, 4);
}

/* In place: GLib may clear the tail of a shrunk array (G_DEBUG=gc-friendly). */
static void
patch32 (W *w, guint at, guint32 v)
{
	g_assert (at + 4 <= w->out->len);
	put32 (w, w->out->data + at, v);
}

static guint32
blob (W *w, const void *data, gsize len)
{
	guint32 at = w->out->len;

	g_byte_array_append (w->out, data, len);
	if (w->out->len % 2 != 0) {
		g_byte_array_append (w->out, (const guint8 *) "", 1);
	}

	return at;
}

typedef struct {
	guint16 tag, type;
	guint32 count, value;
} Entry;

static guint32
ifd (W *w, const Entry *e, guint n, guint32 next)
{
	guint32 at = w->out->len;
	guint i;

	w16 (w, n);
	for (i = 0; i < n; i++) {
		w16 (w, e[i].tag);
		w16 (w, e[i].type);
		w32 (w, e[i].count);
		if (e[i].type == 3 && e[i].count == 1) {
			w16 (w, e[i].value);
			w16 (w, 0);
		} else {
			w32 (w, e[i].value);
		}
	}
	w32 (w, next);

	return at;
}

/* Starts a TIFF with its first directory offset left to patch at 4. */
static void
tiff_head (W *w, guint magic)
{
	g_byte_array_append (w->out, (const guint8 *) (w->big ? "MM" : "II"), 2);
	w16 (w, magic);
	w32 (w, 0);
}

static GBytes *
jpeg (int width, int height, guint8 r, guint8 g, guint8 b)
{
	g_autoptr (GdkPixbuf) pixbuf = gdk_pixbuf_new (GDK_COLORSPACE_RGB, FALSE, 8, width, height);
	gchar *buf = NULL;
	gsize len = 0;

	gdk_pixbuf_fill (pixbuf, ((guint32) r << 24) | ((guint32) g << 16) | ((guint32) b << 8) | 0xFF);
	if (!gdk_pixbuf_save_to_buffer (pixbuf, &buf, &len, "jpeg", NULL, "quality", "95", NULL)) {
		return NULL;
	}

	return g_bytes_new_take (buf, len);
}

static guint32
put_jpeg (W *w, GBytes *bytes)
{
	return blob (w, g_bytes_get_data (bytes, NULL), g_bytes_get_size (bytes));
}

static GdkPixbuf *
read_array (GByteArray *array, int size)
{
	g_autoptr (GBytes) bytes = g_bytes_new (array->data, array->len);
	g_autoptr (GInputStream) in = g_memory_input_stream_new_from_bytes (bytes);

	return nemo_raw_load (in, size, NULL);
}

static gboolean
near (int a, int b)
{
	return abs (a - b) <= 8;
}

static gboolean
color_is (GdkPixbuf *pixbuf, int r, int g, int b)
{
	const guint8 *p;
	int x, y;

	if (pixbuf == NULL) {
		g_printerr ("  no picture\n");
		return FALSE;
	}

	x = gdk_pixbuf_get_width (pixbuf) / 2;
	y = gdk_pixbuf_get_height (pixbuf) / 2;
	p = gdk_pixbuf_read_pixels (pixbuf) + y * gdk_pixbuf_get_rowstride (pixbuf) +
	    x * gdk_pixbuf_get_n_channels (pixbuf);

	if (!near (p[0], r) || !near (p[1], g) || !near (p[2], b)) {
		g_printerr ("  center is %d %d %d\n", p[0], p[1], p[2]);
		return FALSE;
	}

	return TRUE;
}

static gboolean
orientation_is (GdkPixbuf *pixbuf, const char *want)
{
	const char *have = pixbuf != NULL ? gdk_pixbuf_get_option (pixbuf, "orientation") : NULL;

	return g_strcmp0 (have, want) == 0;
}

static gboolean
fits (GdkPixbuf *pixbuf, int size)
{
	return pixbuf != NULL &&
	       MAX (gdk_pixbuf_get_width (pixbuf), gdk_pixbuf_get_height (pixbuf)) <= size;
}

/* Laid out like a DNG: a small uncompressed thumbnail in the first
 * directory, the real previews and the sensor data under it. The sensor data
 * is a lossless JPEG, bigger than any preview. */
static GByteArray *
build_dng (gboolean big)
{
	static const guint8 lossless[] = {
		0xFF, 0xD8, 0xFF, 0xC3, 0x00, 0x0B, 0x10, 0x0B, 0xB8, 0x0F, 0xA0,
		0x01, 0x01, 0x11, 0x00, 0xFF, 0xD9,
	};
	W w = { g_byte_array_new (), big };
	g_autoptr (GBytes) small = jpeg (400, 300, 200, 30, 30);
	g_autoptr (GBytes) large = jpeg (1200, 800, 30, 30, 200);
	guint8 rgb[4 * 2 * 3];
	guint32 rgb_at, small_at, large_at, lossless_at, sub_small, sub_large, sub_raw, subs, ifd0;
	guint i;

	for (i = 0; i < sizeof rgb; i += 3) {
		rgb[i] = 10;
		rgb[i + 1] = 220;
		rgb[i + 2] = 10;
	}

	tiff_head (&w, 42);
	rgb_at = blob (&w, rgb, sizeof rgb);
	small_at = put_jpeg (&w, small);
	large_at = put_jpeg (&w, large);
	lossless_at = blob (&w, lossless, sizeof lossless);

	{
		Entry e[] = {
			{ 0x103, 3, 1, 7 },
			{ 0x111, 4, 1, small_at },
			{ 0x117, 4, 1, g_bytes_get_size (small) },
		};
		sub_small = ifd (&w, e, G_N_ELEMENTS (e), 0);
	}
	{
		Entry e[] = {
			{ 0x103, 3, 1, 7 },
			{ 0x111, 4, 1, large_at },
			{ 0x117, 4, 1, g_bytes_get_size (large) },
		};
		sub_large = ifd (&w, e, G_N_ELEMENTS (e), 0);
	}
	{
		Entry e[] = {
			{ 0x103, 3, 1, 7 },
			{ 0x111, 4, 1, lossless_at },
			{ 0x117, 4, 1, sizeof lossless },
		};
		sub_raw = ifd (&w, e, G_N_ELEMENTS (e), 0);
	}
	subs = w.out->len;
	w32 (&w, sub_raw);
	w32 (&w, sub_small);
	w32 (&w, sub_large);
	{
		Entry e[] = {
			{ 0x100, 3, 1, 4 },
			{ 0x101, 3, 1, 2 },
			{ 0x102, 3, 1, 8 },
			{ 0x103, 3, 1, 1 },
			{ 0x106, 3, 1, 2 },
			{ 0x111, 4, 1, rgb_at },
			{ 0x112, 3, 1, 6 },
			{ 0x115, 3, 1, 3 },
			{ 0x117, 4, 1, sizeof rgb },
			{ 0x14A, 4, 3, subs },
		};
		ifd0 = ifd (&w, e, G_N_ELEMENTS (e), 0);
	}
	patch32 (&w, 4, ifd0);

	return w.out;
}

static void
test_dng (void)
{
	g_autoptr (GByteArray) le = build_dng (FALSE);
	g_autoptr (GByteArray) be = build_dng (TRUE);
	g_autoptr (GdkPixbuf) small = read_array (le, 128);
	g_autoptr (GdkPixbuf) mid = read_array (le, 512);
	g_autoptr (GdkPixbuf) big = read_array (be, 2048);

	/* A small draw takes the smallest preview that is not a thumbnail. */
	check (color_is (small, 200, 30, 30));
	check (fits (small, 128));
	check (orientation_is (small, "6"));

	check (color_is (mid, 30, 30, 200));
	check (fits (mid, 512));

	/* Nothing covers it, so the biggest. Never the sensor data. */
	check (color_is (big, 30, 30, 200));
	check (gdk_pixbuf_get_width (big) == 1200);
	check (orientation_is (big, "6"));
}

/* With no JPEG at all, the uncompressed thumbnail is still something. */
static void
test_rgb_only (void)
{
	W w = { g_byte_array_new (), FALSE };
	g_autoptr (GByteArray) out = w.out;
	g_autoptr (GdkPixbuf) pixbuf = NULL;
	guint8 rgb[6 * 4 * 3];
	guint32 rgb_at, ifd0;
	guint i;

	for (i = 0; i < sizeof rgb; i += 3) {
		rgb[i] = 90;
		rgb[i + 1] = 80;
		rgb[i + 2] = 70;
	}

	tiff_head (&w, 42);
	rgb_at = blob (&w, rgb, sizeof rgb);
	{
		/* Bits per sample as three values, the usual way. */
		guint32 bits = w.out->len;
		Entry e[] = {
			{ 0x100, 4, 1, 6 },
			{ 0x101, 4, 1, 4 },
			{ 0x102, 3, 3, bits },
			{ 0x103, 3, 1, 1 },
			{ 0x106, 3, 1, 2 },
			{ 0x111, 4, 2, 0 },
			{ 0x115, 3, 1, 3 },
			{ 0x117, 4, 2, 0 },
		};
		guint32 offsets, counts;

		w16 (&w, 8);
		w16 (&w, 8);
		w16 (&w, 8);
		w16 (&w, 0);
		/* Two strips of two rows each, back to back. */
		offsets = w.out->len;
		w32 (&w, rgb_at);
		w32 (&w, rgb_at + sizeof rgb / 2);
		counts = w.out->len;
		w32 (&w, sizeof rgb / 2);
		w32 (&w, sizeof rgb / 2);
		e[5].value = offsets;
		e[7].value = counts;
		ifd0 = ifd (&w, e, G_N_ELEMENTS (e), 0);
	}
	patch32 (&w, 4, ifd0);

	pixbuf = read_array (out, 128);
	check (pixbuf != NULL && gdk_pixbuf_get_width (pixbuf) == 6 && gdk_pixbuf_get_height (pixbuf) == 4);
	check (color_is (pixbuf, 90, 80, 70));
	check (orientation_is (pixbuf, NULL));
}

/* An uncompressed preview is read a row at a time, more reads than the
 * directories are allowed. */
static void
test_tall_rgb (void)
{
	W w = { g_byte_array_new (), FALSE };
	g_autoptr (GByteArray) out = w.out;
	g_autoptr (GdkPixbuf) pixbuf = NULL;
	g_autofree guint8 *rgb = g_malloc (4096 * 3);
	guint32 rgb_at, ifd0;
	guint i;

	for (i = 0; i < 4096 * 3; i += 3) {
		rgb[i] = 40;
		rgb[i + 1] = 90;
		rgb[i + 2] = 160;
	}

	tiff_head (&w, 42);
	rgb_at = blob (&w, rgb, 4096 * 3);
	{
		Entry e[] = {
			{ 0x100, 3, 1, 1 },
			{ 0x101, 3, 1, 4096 },
			{ 0x102, 3, 1, 8 },
			{ 0x103, 3, 1, 1 },
			{ 0x106, 3, 1, 2 },
			{ 0x111, 4, 1, rgb_at },
			{ 0x115, 3, 1, 3 },
			{ 0x117, 4, 1, 4096 * 3 },
		};
		ifd0 = ifd (&w, e, G_N_ELEMENTS (e), 0);
	}
	patch32 (&w, 4, ifd0);

	pixbuf = read_array (out, 128);
	check (pixbuf != NULL && gdk_pixbuf_get_height (pixbuf) == 128);
	check (color_is (pixbuf, 40, 90, 160));
}

/* NEF and ARW style: the preview named by JPEG offset and length in the
 * second directory of the chain. */
static void
test_chain (void)
{
	W w = { g_byte_array_new (), TRUE };
	g_autoptr (GByteArray) out = w.out;
	g_autoptr (GBytes) preview = jpeg (640, 424, 250, 200, 0);
	g_autoptr (GdkPixbuf) pixbuf = NULL;
	guint32 at, ifd1, ifd0;

	tiff_head (&w, 42);
	at = put_jpeg (&w, preview);
	{
		Entry e[] = {
			{ 0x201, 4, 1, at },
			{ 0x202, 4, 1, g_bytes_get_size (preview) },
		};
		ifd1 = ifd (&w, e, G_N_ELEMENTS (e), 0);
	}
	{
		Entry e[] = {
			{ 0x112, 3, 1, 8 },
		};
		ifd0 = ifd (&w, e, G_N_ELEMENTS (e), ifd1);
	}
	patch32 (&w, 4, ifd0);

	pixbuf = read_array (out, 256);
	check (color_is (pixbuf, 250, 200, 0));
	check (fits (pixbuf, 256));
	check (orientation_is (pixbuf, "8"));
}

/* Panasonic's magic number, and its preview in one opaque tag. */
static void
test_rw2 (void)
{
	W w = { g_byte_array_new (), FALSE };
	g_autoptr (GByteArray) out = w.out;
	g_autoptr (GBytes) preview = jpeg (500, 375, 0, 180, 180);
	g_autoptr (GdkPixbuf) pixbuf = NULL;
	guint32 at, ifd0;

	tiff_head (&w, 0x55);
	at = put_jpeg (&w, preview);
	{
		Entry e[] = {
			{ 0x2E, 7, g_bytes_get_size (preview), at },
		};
		ifd0 = ifd (&w, e, G_N_ELEMENTS (e), 0);
	}
	patch32 (&w, 4, ifd0);

	pixbuf = read_array (out, 128);
	check (color_is (pixbuf, 0, 180, 180));
}

/* Olympus: the preview is in the maker note, found through the EXIF
 * directory, with offsets from the note's own start. */
static void
test_orf (void)
{
	W w = { g_byte_array_new (), FALSE };
	g_autoptr (GByteArray) out = w.out;
	g_autoptr (GByteArray) note_bytes = g_byte_array_new ();
	g_autoptr (GBytes) preview = jpeg (640, 480, 230, 120, 20);
	g_autoptr (GdkPixbuf) pixbuf = NULL;
	W note = { note_bytes, FALSE };
	guint32 note_at, exif, ifd0;

	g_byte_array_append (note_bytes, (const guint8 *) "OLYMPUS\0II\3\0", 12);
	{
		Entry e[] = {
			{ 0x2020, 13, 1, 30 },
		};
		ifd (&note, e, G_N_ELEMENTS (e), 0);
	}
	{
		Entry e[] = {
			{ 0x101, 4, 1, 60 },
			{ 0x102, 4, 1, g_bytes_get_size (preview) },
		};
		ifd (&note, e, G_N_ELEMENTS (e), 0);
	}
	put_jpeg (&note, preview);

	g_byte_array_append (out, (const guint8 *) "IIRO", 4);
	w32 (&w, 0);
	note_at = blob (&w, note_bytes->data, note_bytes->len);
	{
		Entry e[] = {
			{ 0x927C, 7, note_bytes->len, note_at },
		};
		exif = ifd (&w, e, G_N_ELEMENTS (e), 0);
	}
	{
		Entry e[] = {
			{ 0x112, 3, 1, 6 },
			{ 0x8769, 4, 1, exif },
		};
		ifd0 = ifd (&w, e, G_N_ELEMENTS (e), 0);
	}
	patch32 (&w, 4, ifd0);

	pixbuf = read_array (out, 128);
	check (color_is (pixbuf, 230, 120, 20));
	check (orientation_is (pixbuf, "6"));
}

static void
test_raf (void)
{
	g_autoptr (GByteArray) out = g_byte_array_new ();
	g_autoptr (GBytes) preview = jpeg (480, 320, 120, 0, 160);
	g_autoptr (GdkPixbuf) pixbuf = NULL;
	W w = { out, TRUE };
	guint8 head[92] = { 0 };

	memcpy (head, "FUJIFILMCCD-RAW 0201", 20);
	g_byte_array_append (out, head, 84);
	w32 (&w, sizeof head);
	w32 (&w, g_bytes_get_size (preview));
	put_jpeg (&w, preview);

	pixbuf = read_array (out, 128);
	check (color_is (pixbuf, 120, 0, 160));
}

static guint
box_begin (W *w, const char *type)
{
	guint at = w->out->len;

	w32 (w, 0);
	g_byte_array_append (w->out, (const guint8 *) type, 4);

	return at;
}

static void
box_end (W *w, guint at)
{
	patch32 (w, at, w->out->len - at);
}

static void
test_cr3 (void)
{
	static const guint8 preview_id[16] = {
		0xEA, 0xF4, 0x2B, 0x5E, 0x1C, 0x98, 0x4B, 0x88,
		0xB9, 0xFB, 0xB7, 0xDC, 0x40, 0x6E, 0x4D, 0x16,
	};
	static const guint8 canon_id[16] = {
		0x85, 0xC0, 0xB6, 0x87, 0x82, 0x0F, 0x11, 0xE0,
		0x81, 0x11, 0xF4, 0xCE, 0x46, 0x2B, 0x6A, 0x48,
	};
	g_autoptr (GByteArray) out = g_byte_array_new ();
	g_autoptr (GBytes) preview = jpeg (1620, 1080, 60, 160, 60);
	g_autoptr (GdkPixbuf) pixbuf = NULL;
	W w = { out, TRUE };
	guint ftyp, moov, uuid, cmt1, prvw;

	ftyp = box_begin (&w, "ftyp");
	g_byte_array_append (out, (const guint8 *) "crx \0\0\0\1crx isom", 16);
	box_end (&w, ftyp);

	moov = box_begin (&w, "moov");
	uuid = box_begin (&w, "uuid");
	g_byte_array_append (out, canon_id, 16);
	cmt1 = box_begin (&w, "CMT1");
	{
		/* Offsets in here count from the start of the TIFF. */
		guint base = out->len;
		W t = { out, FALSE };
		Entry e[] = {
			{ 0x112, 3, 1, 6 },
		};
		guint32 ifd0;

		tiff_head (&t, 42);
		ifd0 = out->len - base;
		ifd (&t, e, G_N_ELEMENTS (e), 0);
		patch32 (&t, base + 4, ifd0);
	}
	box_end (&w, cmt1);
	box_end (&w, uuid);
	box_end (&w, moov);

	uuid = box_begin (&w, "uuid");
	g_byte_array_append (out, preview_id, 16);
	w32 (&w, 0);
	w32 (&w, 1);
	prvw = box_begin (&w, "PRVW");
	w32 (&w, 0);
	w16 (&w, 1);
	w16 (&w, 1620);
	w16 (&w, 1080);
	w16 (&w, 1);
	w32 (&w, g_bytes_get_size (preview));
	g_byte_array_append (out, g_bytes_get_data (preview, NULL), g_bytes_get_size (preview));
	box_end (&w, prvw);
	box_end (&w, uuid);

	box_end (&w, box_begin (&w, "mdat"));

	pixbuf = read_array (out, 256);
	check (color_is (pixbuf, 60, 160, 60));
	check (fits (pixbuf, 256));
	check (orientation_is (pixbuf, "6"));
}

/* Cut short, and with every byte of the directories spoiled. The directories
 * come last, so no cut leaves a readable file. A spoiled one only has to come
 * back at all. */
static void
test_damaged (void)
{
	g_autoptr (GByteArray) whole = build_dng (FALSE);
	gsize cut, at;
	guint answered = 0;

	for (cut = 0; cut < whole->len; cut += cut < 64 ? 1 : 97) {
		g_autoptr (GByteArray) part = g_byte_array_new ();
		g_autoptr (GdkPixbuf) pixbuf = NULL;

		g_byte_array_append (part, whole->data, cut);
		pixbuf = read_array (part, 128);
		answered += pixbuf != NULL;
	}
	check (answered == 0);

	for (at = whole->len - 200; at < whole->len; at++) {
		guint8 keep = whole->data[at];
		guint8 spoil[] = { 0x00, 0xFF, 0x7F, 0x80 };
		guint i;

		for (i = 0; i < G_N_ELEMENTS (spoil); i++) {
			g_autoptr (GdkPixbuf) pixbuf = NULL;

			whole->data[at] = spoil[i];
			pixbuf = read_array (whole, 128);
		}
		whole->data[at] = keep;
	}

	{
		g_autoptr (GByteArray) junk = g_byte_array_new ();
		g_autoptr (GdkPixbuf) pixbuf = NULL;

		g_byte_array_append (junk, (const guint8 *) "II*\0\x08\0\0\0\0\0", 10);
		pixbuf = read_array (junk, 128);
		check (pixbuf == NULL);
	}
}

/* A directory that names itself as the next one. */
static void
test_loop (void)
{
	W w = { g_byte_array_new (), FALSE };
	g_autoptr (GByteArray) out = w.out;
	g_autoptr (GdkPixbuf) pixbuf = NULL;
	Entry e[] = {
		{ 0x112, 3, 1, 1 },
	};

	tiff_head (&w, 42);
	patch32 (&w, 4, ifd (&w, e, G_N_ELEMENTS (e), 8));
	pixbuf = read_array (out, 128);
	check (pixbuf == NULL);
}

#define LOOP_ENTRIES 512

/* A first directory that names itself as the next, so it is read again as
 * often as the reader allows. Every one of its entries is the one given. */
static void
looping_ifd (W *w, guint16 tag, guint16 type, guint32 count, guint32 value)
{
	Entry e[LOOP_ENTRIES];
	guint32 at = w->out->len;
	guint i;

	for (i = 0; i < LOOP_ENTRIES; i++) {
		e[i] = (Entry) { tag, type, count, value };
	}
	ifd (w, e, LOOP_ENTRIES, at);
	patch32 (w, 4, at);
}

/* An Olympus note whose directories are full and lack what is looked for:
 * the settings tag, or with settings, where the preview starts. */
static GByteArray *
build_olympus_loop (gboolean settings)
{
	W w = { g_byte_array_new (), FALSE };
	g_autoptr (GByteArray) note_bytes = g_byte_array_new ();
	W note = { note_bytes, FALSE };
	Entry e[LOOP_ENTRIES];
	guint32 note_at;
	guint i;

	for (i = 0; i < LOOP_ENTRIES; i++) {
		e[i] = (Entry) { 0x1234, 3, 1, 0 };
	}
	g_byte_array_append (note_bytes, (const guint8 *) "OLYMPUS\0II\3\0", 12);
	if (settings) {
		e[LOOP_ENTRIES - 1] = (Entry) { 0x2020, 13, 1, 12 + 2 + LOOP_ENTRIES * 12 + 4 };
		ifd (&note, e, LOOP_ENTRIES, 0);
		e[LOOP_ENTRIES - 1] = e[0];
	}
	ifd (&note, e, LOOP_ENTRIES, 0);

	tiff_head (&w, 0x4F52);
	note_at = blob (&w, note_bytes->data, note_bytes->len);
	looping_ifd (&w, 0x927C, 7, note_bytes->len, note_at);

	return w.out;
}

/* Panasonic's preview tag, at a JPEG that never gets to its frame header. */
static GByteArray *
build_rw2_loop (void)
{
	W w = { g_byte_array_new (), FALSE };
	g_autoptr (GByteArray) markers = g_byte_array_new ();
	guint32 at;
	guint i;

	g_byte_array_append (markers, (const guint8 *) "\xFF\xD8", 2);
	for (i = 0; i < 64; i++) {
		g_byte_array_append (markers, (const guint8 *) "\xFF\xE0\x00\x02", 4);
	}
	g_byte_array_append (markers, (const guint8 *) "\xFF\xD9", 2);

	tiff_head (&w, 0x55);
	at = blob (&w, markers->data, markers->len);
	looping_ifd (&w, 0x2E, 7, markers->len, at);

	return w.out;
}

/* Each read is a seek and a read on the file, so the work is read from disk
 * the way the thumbnailer reads it. A memory stream hides most of the cost.
 * Before the fix they took 8 s, 12 s and 0.8 s. */
static void
test_loop_work (const char *dir)
{
	GByteArray *files[] = {
		build_olympus_loop (FALSE),
		build_olympus_loop (TRUE),
		build_rw2_loop (),
	};
	guint i;

	for (i = 0; i < G_N_ELEMENTS (files); i++) {
		g_autofree char *name = g_strdup_printf ("loop-%u.orf", i);
		g_autofree char *path = g_build_filename (dir, name, NULL);
		g_autofree char *uri = g_filename_to_uri (path, NULL, NULL);
		g_autoptr (GdkPixbuf) pixbuf = NULL;
		gint64 start;
		double secs;

		check (g_file_set_contents (path, (const char *) files[i]->data, files[i]->len, NULL));
		start = g_get_monotonic_time ();
		pixbuf = nemo_raw_load_uri (uri, 128, NULL);
		secs = (g_get_monotonic_time () - start) / 1e6;
		check (pixbuf == NULL);
		if (secs >= 0.25) {
			g_printerr ("  %s took %.2f s\n", name, secs);
		}
		check (secs < 0.25);
		g_byte_array_unref (files[i]);
	}
}

static void
test_types (void)
{
	check (nemo_raw_type_ok ("image/x-adobe-dng"));
	check (nemo_raw_type_ok ("image/x-canon-cr3"));
	check (nemo_raw_type_ok ("image/x-nikon-nef"));
	check (nemo_raw_type_ok (".NEF"));
	check (nemo_raw_type_ok (".dng"));
	check (!nemo_raw_type_ok (".raw"));
	check (!nemo_raw_type_ok ("image/x-canon-crw"));
	check (!nemo_raw_type_ok ("image/tiff"));
	check (!nemo_raw_type_ok (NULL));
}

/* The thumbnail factory is where the render thread asks, so a reader nothing
 * calls would pass everything above and still draw no thumbnail. */
static void
test_factory (const char *dir)
{
	g_autoptr (GByteArray) dng = build_dng (FALSE);
	g_autofree char *path = g_build_filename (dir, "shot.dng", NULL);
	g_autofree char *uri = g_filename_to_uri (path, NULL, NULL);
	NemoDesktopThumbnailFactory *factory;
	g_autoptr (GdkPixbuf) pixbuf = NULL;

	check (g_file_set_contents (path, (const char *) dng->data, dng->len, NULL));

	factory = nemo_desktop_thumbnail_factory_new (NEMO_DESKTOP_THUMBNAIL_SIZE_LARGE);
	check (nemo_desktop_thumbnail_factory_can_make (factory, uri, "image/x-adobe-dng"));

	pixbuf = nemo_desktop_thumbnail_factory_generate_thumbnail_at_size (factory, uri,
									    "image/x-adobe-dng", 128, NULL);
	check (color_is (pixbuf, 200, 30, 30));
	/* Turned upright by the factory: 400 by 300 on its side. */
	check (pixbuf != NULL && gdk_pixbuf_get_height (pixbuf) > gdk_pixbuf_get_width (pixbuf));

	g_object_unref (factory);
}

int
main (void)
{
	g_autofree char *home = test_scratch_config_home ("nemo-raw-XXXXXX");

	nemo_config_init ();

	test_dng ();
	test_rgb_only ();
	test_tall_rgb ();
	test_chain ();
	test_rw2 ();
	test_orf ();
	test_raf ();
	test_cr3 ();
	test_damaged ();
	test_loop ();
	test_loop_work (home);
	test_types ();
	test_factory (home);

	if (failures == 0)
		g_print ("nemo-raw: all checks passed\n");

	return failures == 0 ? 0 : 1;
}
