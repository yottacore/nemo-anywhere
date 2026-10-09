/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */

/* nemo-ole2.c - streams out of an OLE2 compound file.

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

#include "nemo-ole2.h"

#include <string.h>

#define HEADER_LEN      512
#define HEADER_FATS     109
#define DIR_ENTRY_LEN   128
#define MINI_CUTOFF     4096
#define MINI_SHIFT      6
#define MAX_SECTORS     (4u * 1024 * 1024)	/* 2 GB of 512 byte sectors */
#define MAX_DIR_ENTRIES 32768
#define MAX_MINI_STREAM (64u * 1024 * 1024)

#define ENTRY_STREAM    2
#define ENTRY_ROOT      5

static const guint8 ole2_magic[8] = { 0xd0, 0xcf, 0x11, 0xe0, 0xa1, 0xb1, 0x1a, 0xe1 };

struct _NemoOle2 {
	GInputStream *in;
	GCancellable *cancellable;
	guint         shift;
	gboolean      v4;
	guint32       n_sectors;	/* in the file, after the header */
	guint32      *fat;		/* n_sectors of them. A number past the file ends a chain. */
	guint8       *dir;
	guint32       n_dir;
	guint32       minifat_start;
	guint32       minifat_count;
	gboolean      mini_tried;
	guint32      *minifat;
	guint32       n_minifat;
	guint32      *mini_chain;	/* the root's sectors, where the small streams are */
	guint32       n_mini_chain;
};

static guint16
rd16 (const guint8 *p)
{
	return (guint16) (p[0] | (p[1] << 8));
}

static guint32
rd32 (const guint8 *p)
{
	return (guint32) p[0] | ((guint32) p[1] << 8) | ((guint32) p[2] << 16) | ((guint32) p[3] << 24);
}

gboolean
nemo_ole2_magic (const guint8 *p, gsize len)
{
	return len >= sizeof ole2_magic && memcmp (p, ole2_magic, sizeof ole2_magic) == 0;
}

/* A last sector cut short in the file reads as zeros past the end. */
static gboolean
read_at (NemoOle2 *ole, guint64 offset, guint8 *buf, gsize len)
{
	gsize got = 0;

	if (g_cancellable_is_cancelled (ole->cancellable) ||
	    !g_seekable_seek (G_SEEKABLE (ole->in), (goffset) offset, G_SEEK_SET, ole->cancellable, NULL) ||
	    !g_input_stream_read_all (ole->in, buf, len, &got, ole->cancellable, NULL)) {
		return FALSE;
	}

	memset (buf + got, 0, len - got);
	return TRUE;
}

static guint64
sector_offset (const NemoOle2 *ole, guint32 sector)
{
	return ((guint64) sector + 1) << ole->shift;
}

/* At most @want links of a chain. A number off the table ends it, so a loop
   is bounded by @want, which callers keep under the table's size.
   Returns: (transfer full): free with g_free */
static guint32 *
walk_chain (const guint32 *table, guint32 n_table, guint32 start, guint32 want, guint32 *n_out)
{
	guint32 *links = g_new (guint32, MAX (want, 1));
	guint32 n = 0, at = start;

	while (n < want && at < n_table) {
		links[n++] = at;
		at = table[at];
	}

	*n_out = n;
	return links;
}

/* Up to @len bytes from a list of sectors, a run of neighbors at a time.
   *done says how many there were. */
static gboolean
read_sectors (NemoOle2 *ole, const guint32 *sectors, guint32 n, guint8 *buf, gsize len, gsize *done)
{
	guint32 i = 0;

	*done = 0;
	while (i < n && *done < len) {
		guint32 run = 1;
		gsize take;

		while (i + run < n && sectors[i + run] == sectors[i] + run) {
			run++;
		}

		take = MIN ((gsize) run << ole->shift, len - *done);
		if (!read_at (ole, sector_offset (ole, sectors[i]), buf + *done, take)) {
			return FALSE;
		}
		*done += take;
		i += run;
	}

	return TRUE;
}

static guint64
entry_size (const NemoOle2 *ole, const guint8 *entry)
{
	guint64 low = rd32 (entry + 0x78);

	/* Version 3 leaves junk in the high half. */
	return ole->v4 ? low | ((guint64) rd32 (entry + 0x7c) << 32) : low;
}

static gboolean
load_fat (NemoOle2 *ole, const guint8 *header)
{
	guint32 per_sector = 1u << (ole->shift - 2);
	guint32 wanted = MIN (rd32 (header + 0x2c), (ole->n_sectors + per_sector - 1) / per_sector);
	guint32 *fat_sectors = g_new (guint32, MAX (wanted, 1));
	guint32 n = 0, difat = rd32 (header + 0x44), steps = 0, i;
	g_autofree guint8 *buf = NULL;
	gsize done = 0;

	for (i = 0; i < HEADER_FATS && n < wanted; i++) {
		guint32 s = rd32 (header + 0x4c + i * 4);

		if (s >= ole->n_sectors) {
			break;
		}
		fat_sectors[n++] = s;
	}

	/* The rest are listed in a chain of their own, the last slot of each
	   sector pointing at the next. */
	if (n == HEADER_FATS && n < wanted) {
		g_autofree guint8 *block = g_malloc ((gsize) 1 << ole->shift);

		while (n < wanted && difat < ole->n_sectors && steps++ < wanted) {
			guint32 j;

			if (!read_at (ole, sector_offset (ole, difat), block, (gsize) 1 << ole->shift)) {
				g_free (fat_sectors);
				return FALSE;
			}
			for (j = 0; j + 1 < per_sector && n < wanted; j++) {
				guint32 s = rd32 (block + j * 4);

				if (s >= ole->n_sectors) {
					break;
				}
				fat_sectors[n++] = s;
			}
			if (j + 1 < per_sector && n < wanted) {
				break;
			}
			difat = rd32 (block + (per_sector - 1) * 4);
		}
	}

	buf = g_malloc (MAX ((gsize) n << ole->shift, 1));
	if (!read_sectors (ole, fat_sectors, n, buf, (gsize) n << ole->shift, &done)) {
		g_free (fat_sectors);
		return FALSE;
	}
	g_free (fat_sectors);

	ole->fat = g_new (guint32, ole->n_sectors);
	for (i = 0; i < ole->n_sectors; i++) {
		ole->fat[i] = (gsize) i * 4 + 4 <= done ? rd32 (buf + (gsize) i * 4) : G_MAXUINT32;
	}

	return TRUE;
}

static gboolean
load_dir (NemoOle2 *ole, guint32 start)
{
	guint32 want = MIN (ole->n_sectors, (guint32) (((guint64) MAX_DIR_ENTRIES * DIR_ENTRY_LEN) >> ole->shift));
	guint32 n = 0;
	g_autofree guint32 *sectors = walk_chain (ole->fat, ole->n_sectors, start, want, &n);
	gsize len = (gsize) n << ole->shift, done = 0;

	ole->dir = g_malloc (MAX (len, 1));
	if (!read_sectors (ole, sectors, n, ole->dir, len, &done)) {
		return FALSE;
	}

	ole->n_dir = (guint32) (done / DIR_ENTRY_LEN);
	return ole->n_dir > 0 && ole->dir[0x42] == ENTRY_ROOT;
}

NemoOle2 *
nemo_ole2_open (GInputStream *stream, GCancellable *cancellable, GError **error)
{
	g_autoptr (NemoOle2) ole = NULL;
	guint8 header[HEADER_LEN];
	goffset size;
	guint64 sectors;

	if (!G_IS_SEEKABLE (stream) || !g_seekable_can_seek (G_SEEKABLE (stream)) ||
	    !g_seekable_seek (G_SEEKABLE (stream), 0, G_SEEK_END, cancellable, NULL)) {
		g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_NOT_SUPPORTED, "Can't seek in this file");
		return NULL;
	}
	size = g_seekable_tell (G_SEEKABLE (stream));

	ole = g_new0 (NemoOle2, 1);
	ole->in = stream;
	ole->cancellable = cancellable;

	if (size < HEADER_LEN || !read_at (ole, 0, header, sizeof header) || !nemo_ole2_magic (header, sizeof header) ||
	    rd16 (header + 0x1c) != 0xfffe || rd16 (header + 0x20) != MINI_SHIFT) {
		goto bad;
	}

	ole->shift = rd16 (header + 0x1e);
	ole->v4 = rd16 (header + 0x1a) == 4;
	if (ole->shift != 9 && ole->shift != 12) {
		goto bad;
	}

	/* The header has a whole sector to itself. */
	sectors = ((guint64) size + ((guint64) 1 << ole->shift) - 1) >> ole->shift;
	if (sectors < 2) {
		goto bad;
	}
	ole->n_sectors = (guint32) MIN (sectors - 1, MAX_SECTORS);

	ole->minifat_start = rd32 (header + 0x3c);
	ole->minifat_count = rd32 (header + 0x40);

	if (!load_fat (ole, header) || !load_dir (ole, rd32 (header + 0x30))) {
		goto bad;
	}

	return g_steal_pointer (&ole);

bad:
	if (!g_cancellable_set_error_if_cancelled (cancellable, error)) {
		g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA, "Not an OLE2 file");
	}
	return NULL;
}

/* Matched against plain ASCII names only, which is all that's looked for. */
static gboolean
name_is (const guint8 *entry, const char *name)
{
	guint16 bytes = rd16 (entry + 0x40);
	gsize units, i;

	if (bytes < 2 || bytes > 64 || bytes % 2 != 0) {
		return FALSE;
	}
	units = bytes / 2 - 1;
	if (strlen (name) != units) {
		return FALSE;
	}

	for (i = 0; i < units; i++) {
		guint16 u = rd16 (entry + i * 2);

		if (u > 0x7f || g_ascii_tolower ((char) u) != g_ascii_tolower (name[i])) {
			return FALSE;
		}
	}

	return TRUE;
}

/* The top storage's children are a tree through their sibling links. Walked
   with a stack and a seen mark, so a loop in it or a deep one costs nothing
   extra. */
static const guint8 *
find_stream (NemoOle2 *ole, const char *name)
{
	g_autofree guint32 *stack = g_new (guint32, (gsize) ole->n_dir * 2 + 1);
	g_autofree guint8 *seen = g_new0 (guint8, ole->n_dir / 8 + 1);
	guint32 top = 0, child = rd32 (ole->dir + 0x4c);

	if (child < ole->n_dir) {
		stack[top++] = child;
	}

	while (top > 0) {
		guint32 id = stack[--top];
		const guint8 *entry = ole->dir + (gsize) id * DIR_ENTRY_LEN;
		guint32 side[2];
		guint k;

		if (seen[id / 8] & (1u << (id % 8))) {
			continue;
		}
		seen[id / 8] |= (guint8) (1u << (id % 8));

		if (entry[0x42] == ENTRY_STREAM && name_is (entry, name)) {
			return entry;
		}

		side[0] = rd32 (entry + 0x44);
		side[1] = rd32 (entry + 0x48);
		for (k = 0; k < 2; k++) {
			if (side[k] < ole->n_dir && !(seen[side[k] / 8] & (1u << (side[k] % 8)))) {
				stack[top++] = side[k];
			}
		}
	}

	return NULL;
}

static void
load_mini (NemoOle2 *ole)
{
	guint64 root_size = entry_size (ole, ole->dir);
	guint32 want, n = 0;
	g_autofree guint32 *sectors = NULL;
	g_autofree guint8 *buf = NULL;
	gsize done = 0, i;

	if (ole->mini_tried) {
		return;
	}
	ole->mini_tried = TRUE;

	root_size = MIN (root_size, MAX_MINI_STREAM);
	want = (guint32) MIN ((root_size + ((guint64) 1 << ole->shift) - 1) >> ole->shift, ole->n_sectors);
	ole->mini_chain = walk_chain (ole->fat, ole->n_sectors, rd32 (ole->dir + 0x74), want, &ole->n_mini_chain);

	/* No more table than the small streams' space needs. */
	want = (guint32) MIN ((guint64) ole->minifat_count, ole->n_sectors);
	want = MIN (want, (guint32) ((((guint64) ole->n_mini_chain << (ole->shift - MINI_SHIFT)) * 4 +
				      ((guint64) 1 << ole->shift) - 1) >> ole->shift));
	sectors = walk_chain (ole->fat, ole->n_sectors, ole->minifat_start, want, &n);
	buf = g_malloc (MAX ((gsize) n << ole->shift, 1));
	if (!read_sectors (ole, sectors, n, buf, (gsize) n << ole->shift, &done)) {
		return;
	}

	ole->n_minifat = (guint32) MIN (done / 4, (gsize) ole->n_mini_chain << (ole->shift - MINI_SHIFT));
	ole->minifat = g_new (guint32, MAX (ole->n_minifat, 1));
	for (i = 0; i < ole->n_minifat; i++) {
		ole->minifat[i] = rd32 (buf + i * 4);
	}
}

static GBytes *
read_mini (NemoOle2 *ole, guint32 start, gsize len)
{
	guint32 n = 0, i;
	g_autofree guint32 *links = NULL;
	guint8 *buf;
	gsize done = 0;

	load_mini (ole);
	links = walk_chain (ole->minifat, ole->n_minifat, start,
			    (guint32) MIN ((len + (1u << MINI_SHIFT) - 1) >> MINI_SHIFT, ole->n_minifat), &n);
	buf = g_malloc (MAX (len, 1));

	for (i = 0; i < n && done < len; i++) {
		guint64 at = (guint64) links[i] << MINI_SHIFT;
		guint32 sector = ole->mini_chain[at >> ole->shift];
		gsize take = MIN (len - done, (gsize) 1 << MINI_SHIFT);

		if (!read_at (ole, sector_offset (ole, sector) + (at & (((guint64) 1 << ole->shift) - 1)), buf + done, take)) {
			g_free (buf);
			return NULL;
		}
		done += take;
	}

	return g_bytes_new_take (buf, done);
}

GBytes *
nemo_ole2_read (NemoOle2 *ole, const char *name, gsize cap)
{
	const guint8 *entry = find_stream (ole, name);
	guint64 size;
	guint32 start, n = 0;
	g_autofree guint32 *sectors = NULL;
	gsize len, done = 0;
	guint8 *buf;

	if (entry == NULL) {
		return NULL;
	}

	size = entry_size (ole, entry);
	start = rd32 (entry + 0x74);
	len = (gsize) MIN (size, (guint64) cap);

	if (size < MINI_CUTOFF) {
		return read_mini (ole, start, len);
	}

	sectors = walk_chain (ole->fat, ole->n_sectors, start,
			      (guint32) MIN (((guint64) len + ((guint64) 1 << ole->shift) - 1) >> ole->shift, ole->n_sectors), &n);
	len = MIN (len, (gsize) n << ole->shift);
	buf = g_malloc (MAX (len, 1));

	if (!read_sectors (ole, sectors, n, buf, len, &done)) {
		g_free (buf);
		return NULL;
	}

	return g_bytes_new_take (buf, done);
}

void
nemo_ole2_free (NemoOle2 *ole)
{
	if (ole == NULL) {
		return;
	}

	g_free (ole->fat);
	g_free (ole->dir);
	g_free (ole->minifat);
	g_free (ole->mini_chain);
	g_free (ole);
}
