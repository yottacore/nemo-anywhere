/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */

/* arc-path-list.c - every file the size scan found, and the size totals.

   Copyright © 2026 t00mietum (CryptogID: ปʬϝღถɔ4რఠΔթะ9ƾǝu).

   This program is free software; you can redistribute it and/or
   modify it under the terms of the GNU General Public License as
   published by the Free Software Foundation; version 2 of the
   License.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, see <http://www.gnu.org/licenses/>.
*/

#include <string.h>

#include "arc-path-list.h"

/* Millions of paths, so no allocation per path: the text goes in big chunks,
   the entries in one array, and the index is open addressing over it. */
#define CHUNK_BYTES (1u << 20)

typedef struct {
	const char *path;	/* in a chunk */
	guint64     bytes;
	guint32     hash;
	guint32     length;
	guint16     counted_in;
} Entry;

struct _ArcPathList {
	Entry     *entries;
	guint      n_entries;
	guint      entries_size;
	guint32   *slots;	/* entry index + 1, 0 for empty */
	guint32    slot_mask;
	GPtrArray *chunks;
	char      *chunk_free;
	gsize      chunk_left;
	guint64    totals[ARC_MIX_COUNT];
};

/* A multiply and shift per 8 bytes. Paths share long prefixes, so every word
   has to move every bit of the result. Only ever compared in memory, so the
   byte order doesn't matter. */
static guint32
path_hash (const char *path,
	   gsize       length)
{
	guint64 h = 0x9e3779b97f4a7c15ull ^ length;
	guint64 word;
	gsize i;

	for (i = 0; i + 8 <= length; i += 8) {
		memcpy (&word, path + i, 8);
		h = (h ^ word) * 0xff51afd7ed558ccdull;
		h ^= h >> 32;
	}
	word = 0;
	memcpy (&word, path + i, length - i);
	h = (h ^ word) * 0xc4ceb9fe1a85ec53ull;
	h ^= h >> 29;
	h *= 0xff51afd7ed558ccdull;
	h ^= h >> 32;

	return (guint32) h;
}

/* The mixes that follow every option in needs, one bit per mix. */
static guint16
mixes_letting_in (guint needs)
{
	guint16 mixes = 0;
	guint mix;

	for (mix = 0; mix < ARC_MIX_COUNT; mix++) {
		if ((needs & ~mix) == 0) {
			mixes |= (guint16) (1u << mix);
		}
	}
	return mixes;
}

/* Returns: (transfer full): free with arc_path_list_free */
ArcPathList *
arc_path_list_new (void)
{
	ArcPathList *list = g_new0 (ArcPathList, 1);

	list->entries_size = 1024;
	list->entries = g_new (Entry, list->entries_size);
	list->slot_mask = 2048 - 1;
	list->slots = g_new0 (guint32, list->slot_mask + 1);
	list->chunks = g_ptr_array_new_with_free_func (g_free);

	return list;
}

void
arc_path_list_free (ArcPathList *list)
{
	if (list == NULL) {
		return;
	}
	g_free (list->entries);
	g_free (list->slots);
	g_ptr_array_unref (list->chunks);
	g_free (list);
}

static const char *
keep_text (ArcPathList *list,
	   const char  *path,
	   gsize        length)
{
	char *kept;

	if (length + 1 > list->chunk_left) {
		/* A very long path gets a block to itself, and the current
		   chunk stays open for the next. */
		if (length + 1 > CHUNK_BYTES / 4) {
			kept = g_malloc (length + 1);
			g_ptr_array_add (list->chunks, kept);
			memcpy (kept, path, length + 1);
			return kept;
		}
		list->chunk_free = g_malloc (CHUNK_BYTES);
		list->chunk_left = CHUNK_BYTES;
		g_ptr_array_add (list->chunks, list->chunk_free);
	}
	kept = list->chunk_free;
	memcpy (kept, path, length + 1);
	list->chunk_free += length + 1;
	list->chunk_left -= length + 1;

	return kept;
}

/* The slot holding the path, or the empty one it would go in. */
static guint32
slot_for (const ArcPathList *list,
	  const char        *path,
	  gsize              length,
	  guint32            hash)
{
	guint32 slot = hash & list->slot_mask;

	for (;;) {
		guint32 index = list->slots[slot];
		const Entry *entry;

		if (index == 0) {
			return slot;
		}
		entry = &list->entries[index - 1];
		if (entry->hash == hash && entry->length == length &&
		    memcmp (entry->path, path, length) == 0) {
			return slot;
		}
		slot = (slot + 1) & list->slot_mask;
	}
}

/* Kept at most half full, so a miss ends soon. */
static void
grow_slots (ArcPathList *list)
{
	guint32 size = (list->slot_mask + 1) * 2;
	guint i;

	g_free (list->slots);
	list->slots = g_new0 (guint32, size);
	list->slot_mask = size - 1;

	for (i = 0; i < list->n_entries; i++) {
		guint32 slot = list->entries[i].hash & list->slot_mask;

		while (list->slots[slot] != 0) {
			slot = (slot + 1) & list->slot_mask;
		}
		list->slots[slot] = i + 1;
	}
}

gboolean
arc_path_list_add (ArcPathList *list,
		   const char  *path,
		   guint64      bytes,
		   guint        needs)
{
	gsize length = strlen (path);
	guint32 hash = path_hash (path, length);
	guint32 slot = slot_for (list, path, length, hash);
	gboolean is_new = list->slots[slot] == 0;
	Entry *entry;
	guint16 adds;
	guint mix;

	if (is_new) {
		/* Slots and lengths are 32 bits wide. */
		g_return_val_if_fail (length < G_MAXUINT32 && list->n_entries < G_MAXUINT32 / 4, FALSE);

		if (list->n_entries == list->entries_size) {
			list->entries_size *= 2;
			list->entries = g_renew (Entry, list->entries, list->entries_size);
		}
		entry = &list->entries[list->n_entries];
		entry->path = keep_text (list, path, length);
		entry->bytes = bytes;
		entry->hash = hash;
		entry->length = (guint32) length;
		entry->counted_in = 0;
		list->slots[slot] = ++list->n_entries;

		if (list->n_entries > (list->slot_mask + 1) / 2) {
			grow_slots (list);
		}
	} else {
		entry = &list->entries[list->slots[slot] - 1];
	}

	/* A file counts once in every mix that lets in any path to it, so the
	   order the paths come in doesn't matter. */
	adds = mixes_letting_in (needs & ARC_FOLLOW_ALL) & (guint16) ~entry->counted_in;
	for (mix = 0; adds != 0; mix++, adds >>= 1) {
		if (adds & 1) {
			list->totals[mix] += entry->bytes;
			entry->counted_in |= (guint16) (1u << mix);
		}
	}

	return is_new;
}

gboolean
arc_path_list_find (const ArcPathList *list,
		    const char        *path,
		    guint64           *bytes,
		    guint             *counted_in)
{
	gsize length = strlen (path);
	guint32 slot = slot_for (list, path, length, path_hash (path, length));
	const Entry *entry;

	if (list->slots[slot] == 0) {
		return FALSE;
	}
	entry = &list->entries[list->slots[slot] - 1];
	if (bytes != NULL) {
		*bytes = entry->bytes;
	}
	if (counted_in != NULL) {
		*counted_in = entry->counted_in;
	}
	return TRUE;
}

guint
arc_path_list_count (const ArcPathList *list)
{
	return list->n_entries;
}

guint64
arc_path_list_total (const ArcPathList *list,
		     guint              follow)
{
	return list->totals[follow & ARC_FOLLOW_ALL];
}

guint64
arc_path_list_change (const ArcPathList *list,
		      guint              follow,
		      ArcFollow          option)
{
	follow &= ARC_FOLLOW_ALL;
	if ((follow & option) == 0) {
		return 0;
	}
	return list->totals[follow] - list->totals[follow & ~(guint) option];
}

void
arc_path_list_totals (const ArcPathList *list,
		      guint64            totals[ARC_MIX_COUNT])
{
	memcpy (totals, list->totals, sizeof list->totals);
}
