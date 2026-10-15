/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */

/* test-ole2-writer.c - a small compound file writer for the tests.

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

#include <string.h>

#include "test-ole2-writer.h"

static void
put16 (GByteArray *a, guint16 v)
{
	guint8 b[2] = { (guint8) (v & 0xff), (guint8) (v >> 8) };

	g_byte_array_append (a, b, 2);
}

static void
put32 (GByteArray *a, guint32 v)
{
	guint8 b[4] = { (guint8) (v & 0xff), (guint8) ((v >> 8) & 0xff), (guint8) ((v >> 16) & 0xff), (guint8) (v >> 24) };

	g_byte_array_append (a, b, 4);
}

static void
put_utf16 (GByteArray *a, const char *utf8)
{
	glong n = 0, i;
	g_autofree gunichar2 *units = g_utf8_to_utf16 (utf8, -1, NULL, &n, NULL);

	for (i = 0; i < n; i++) {
		put16 (a, units[i]);
	}
}

static void
zeros (GByteArray *a, gsize n)
{
	static const guint8 nothing[64] = { 0 };

	while (n > 0) {
		gsize take = MIN (n, sizeof nothing);

		g_byte_array_append (a, nothing, (guint) take);
		n -= take;
	}
}

static guint32
put_chain (GByteArray *body, GArray *table, gsize unit, const guint8 *data, gsize len)
{
	guint32 n = (guint32) ((len + unit - 1) / unit), start = table->len, i;

	if (n == 0) {
		return ENDOFCHAIN;
	}

	for (i = 0; i < n; i++) {
		guint32 next = i + 1 < n ? start + i + 1 : ENDOFCHAIN;
		gsize take = MIN (unit, len - i * unit);

		g_byte_array_append (body, data + i * unit, (guint) take);
		zeros (body, unit - take);
		g_array_append_val (table, next);
	}

	return start;
}

static void
put_dir_entry (GByteArray *dir, const char *name, guint8 type, guint32 right, guint32 child,
	       guint32 start, guint64 size)
{
	gsize begin = dir->len;
	guint16 name_bytes = 0;

	if (name != NULL) {
		gsize units;

		put_utf16 (dir, name);
		units = (dir->len - begin) / 2;
		put16 (dir, 0);
		name_bytes = (guint16) ((units + 1) * 2);
	}
	zeros (dir, 64 - (dir->len - begin));
	put16 (dir, name_bytes);
	g_byte_array_append (dir, &type, 1);
	g_byte_array_append (dir, (const guint8 *) "\001", 1);
	put32 (dir, FREESECT);
	put32 (dir, right);
	put32 (dir, child);
	zeros (dir, 16 + 4 + 16);
	put32 (dir, start);
	put32 (dir, (guint32) (size & 0xffffffffu));
	put32 (dir, (guint32) (size >> 32));
}

/* Returns: (transfer full): unref with g_bytes_unref */
GBytes *
test_ole2_write (const OleEntry *entries, guint n_entries, OleLayout *lay)
{
	gsize ss = (gsize) 1 << lay->shift, per = ss / 4;
	g_autoptr (GByteArray) body = g_byte_array_new ();
	g_autoptr (GByteArray) mini = g_byte_array_new ();
	g_autoptr (GByteArray) minifat_bytes = g_byte_array_new ();
	g_autoptr (GByteArray) dir = g_byte_array_new ();
	g_autoptr (GByteArray) out = g_byte_array_new ();
	g_autoptr (GArray) fat = g_array_new (FALSE, FALSE, sizeof (guint32));
	g_autoptr (GArray) minifat = g_array_new (FALSE, FALSE, sizeof (guint32));
	guint32 mini_start, minifat_start, n_minifat, n_fat, n_difat, first_fat, i, j;
	guint32 first_child[MAX_ENTRIES + 1], next_sibling[MAX_ENTRIES + 1];

	g_assert (n_entries <= MAX_ENTRIES);

	for (i = 0; i < n_entries; i++) {
		gsize len = 0;
		const guint8 *data;

		if (entries[i].data == NULL) {
			lay->start[i] = ENDOFCHAIN;
			continue;
		}
		data = g_bytes_get_data (entries[i].data, &len);
		if (len < 4096) {
			lay->start[i] = put_chain (mini, minifat, 64, data, len);
		} else {
			lay->start[i] = put_chain (body, fat, ss, data, len);
		}
	}

	mini_start = put_chain (body, fat, ss, mini->data, mini->len);
	for (i = 0; i < minifat->len; i++) {
		put32 (minifat_bytes, g_array_index (minifat, guint32, i));
	}
	n_minifat = (guint32) ((minifat_bytes->len + ss - 1) / ss);
	minifat_start = put_chain (body, fat, ss, minifat_bytes->data, minifat_bytes->len);

	/* Each storage's children as a chain through their right links. Index
	   0 is the top storage, entry i is i + 1. */
	for (i = 0; i <= n_entries; i++) {
		first_child[i] = FREESECT;
		next_sibling[i] = FREESECT;
	}
	for (i = n_entries; i-- > 0;) {
		guint parent = entries[i].parent < 0 ? 0 : (guint) entries[i].parent + 1;

		next_sibling[i + 1] = first_child[parent];
		first_child[parent] = i + 1;
	}

	put_dir_entry (dir, "Root Entry", 5, FREESECT, first_child[0], mini->len > 0 ? mini_start : ENDOFCHAIN, mini->len);
	for (i = 0; i < n_entries; i++) {
		gsize len = entries[i].data != NULL ? g_bytes_get_size (entries[i].data) : 0;

		put_dir_entry (dir, entries[i].name, entries[i].data != NULL ? 2 : 1, next_sibling[i + 1],
			       first_child[i + 1], lay->start[i], len);
	}
	while (dir->len % ss != 0) {
		put_dir_entry (dir, NULL, 0, FREESECT, FREESECT, 0, 0);
	}
	lay->dir_start = put_chain (body, fat, ss, dir->data, dir->len);

	/* Enough FAT sectors to list every sector, themselves and the DIFAT's
	   included. */
	n_fat = 1;
	for (;;) {
		n_difat = n_fat > 109 ? (guint32) ((n_fat - 109 + per - 2) / (per - 1)) : 0;
		if (fat->len + n_fat + n_difat <= n_fat * per) {
			break;
		}
		n_fat++;
	}
	g_assert (n_fat <= G_N_ELEMENTS (lay->fat_sector));

	first_fat = fat->len;
	for (i = 0; i < n_fat; i++) {
		guint32 mark = FATSECT;

		lay->fat_sector[i] = first_fat + i;
		g_array_append_val (fat, mark);
	}
	for (i = 0; i < n_difat; i++) {
		guint32 mark = DIFSECT;

		g_array_append_val (fat, mark);
	}
	lay->n_fat = n_fat;
	while (fat->len < n_fat * per) {
		guint32 free_one = FREESECT;

		g_array_append_val (fat, free_one);
	}

	/* The header. */
	g_byte_array_append (out, (const guint8 *) "\xd0\xcf\x11\xe0\xa1\xb1\x1a\xe1", 8);
	zeros (out, 16);
	put16 (out, 0x3e);
	put16 (out, lay->shift == 12 ? 4 : 3);
	put16 (out, 0xfffe);
	put16 (out, (guint16) lay->shift);
	put16 (out, 6);
	zeros (out, 6);
	put32 (out, lay->shift == 12 ? (guint32) (dir->len / ss) : 0);
	put32 (out, n_fat);
	put32 (out, lay->dir_start);
	put32 (out, 0);
	put32 (out, 4096);
	put32 (out, minifat->len > 0 ? minifat_start : ENDOFCHAIN);
	put32 (out, n_minifat);
	put32 (out, n_difat > 0 ? first_fat + n_fat : ENDOFCHAIN);
	put32 (out, n_difat);
	for (i = 0; i < 109; i++) {
		put32 (out, i < n_fat ? first_fat + i : FREESECT);
	}
	zeros (out, ss - out->len);

	g_byte_array_append (out, body->data, body->len);
	for (i = 0; i < n_fat * per; i++) {
		put32 (out, g_array_index (fat, guint32, i));
	}
	for (i = 0; i < n_difat; i++) {
		for (j = 0; j + 1 < per; j++) {
			guint32 k = 109 + i * (guint32) (per - 1) + j;

			put32 (out, k < n_fat ? first_fat + k : FREESECT);
		}
		put32 (out, i + 1 < n_difat ? first_fat + n_fat + i + 1 : ENDOFCHAIN);
	}

	return g_byte_array_free_to_bytes (g_steal_pointer (&out));
}
