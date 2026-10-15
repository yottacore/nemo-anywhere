/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */

/* nemo-thumbnail-memory.c - how many thumbnails are held ready to draw.

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

#include "nemo-thumbnail-memory.h"

#include "nemo-file-private.h"
#include "nemo-global-preferences.h"

typedef struct {
	gsize bytes;
	gint64 used;
} Held;

/* A folder no view shows. Its files are reffed here, since the folder lets go
   of them the moment nothing watches it, and the pictures go with them. */
typedef struct {
	GList *files;
	guint timeout_id;
} Kept;

static GHashTable *held;	/* NemoFile -> Held, not reffed */
static gsize held_bytes;
static gint full;
static GHashTable *shown;	/* NemoDirectory -> how many views show it */
static GHashTable *kept;	/* NemoDirectory -> Kept */
static guint keep_ms = 60 * 1000;
/* Drawn this recently means on screen, or it was a moment ago. Taking its
   picture away would only have it read straight back. */
static gint64 recent_us = 2 * G_USEC_PER_SEC;

static gsize
budget (void)
{
	gdouble gib = nemo_config_get_double (nemo_config_get_group (NEMO_FILE_CACHE_GROUP),
					      NEMO_FILE_CACHE_MEMORY_GIB);

	if (gib <= 0) {
		return 0;
	}

	return (gsize) (MIN (gib, 1024.0) * 1024 * 1024 * 1024);
}

static void
update_full (void)
{
	g_atomic_int_set (&full, held_bytes >= budget ());
}

static void
budget_changed (G_GNUC_UNUSED NemoConfigGroup *group, G_GNUC_UNUSED const char *key, G_GNUC_UNUSED gpointer data)
{
	update_full ();
}

/* A render thread reads the flag rather than the setting, so it has to move
   when the setting does. Connected for as long as the process runs, as the
   group is. */
static void
watch_budget (void)
{
	static gboolean watching;

	if (watching) {
		return;
	}

	watching = TRUE;
	g_signal_connect (nemo_config_get_group (NEMO_FILE_CACHE_GROUP),
			  "changed::" NEMO_FILE_CACHE_MEMORY_GIB,
			  G_CALLBACK (budget_changed), NULL);
	update_full ();
}

static gboolean
folder_is_shown (NemoDirectory *directory)
{
	return shown != NULL && directory != NULL && g_hash_table_contains (shown, directory);
}

typedef struct {
	NemoFile *file;
	gint64 used;
	gboolean hidden;
} Candidate;

/* A folder nobody is looking at goes before one somebody is. */
static gint
candidate_compare (gconstpointer a, gconstpointer b)
{
	const Candidate *x = a;
	const Candidate *y = b;

	if (x->hidden != y->hidden) {
		return x->hidden ? -1 : 1;
	}

	return (x->used > y->used) - (x->used < y->used);
}

static void
make_room (NemoFile *keep)
{
	gsize limit = budget ();
	gsize target;
	gint64 recent;
	GArray *candidates;
	GHashTableIter iter;
	gpointer key, value;
	guint i;

	if (held_bytes <= limit) {
		return;
	}

	/* A tenth under, or every picture after this one goes through it all
	   again. */
	target = limit - limit / 10;
	recent = g_get_monotonic_time () - recent_us;

	candidates = g_array_new (FALSE, FALSE, sizeof (Candidate));
	g_hash_table_iter_init (&iter, held);
	while (g_hash_table_iter_next (&iter, &key, &value)) {
		NemoFile *file = key;
		Held *entry = value;
		Candidate candidate;

		if (file == keep || entry->used > recent) {
			continue;
		}

		candidate.file = file;
		candidate.used = entry->used;
		candidate.hidden = !folder_is_shown (file->details->directory);
		g_array_append_val (candidates, candidate);
	}
	g_array_sort (candidates, candidate_compare);

	for (i = 0; i < candidates->len && held_bytes > target; i++) {
		NemoFile *file = g_array_index (candidates, Candidate, i).file;
		gboolean in_store = file->details->thumbnail_from_store;

		nemo_file_forget_held_thumbnail (file);
		file->details->thumbnail_in_store = in_store;
	}

	g_array_free (candidates, TRUE);
}

void
nemo_thumbnail_memory_held (NemoFile *file, GdkPixbuf *pixbuf)
{
	Held *entry;

	watch_budget ();

	if (held == NULL) {
		held = g_hash_table_new_full (NULL, NULL, NULL, g_free);
	}

	entry = g_hash_table_lookup (held, file);
	if (entry == NULL) {
		entry = g_new0 (Held, 1);
		g_hash_table_insert (held, file, entry);
	} else {
		held_bytes -= entry->bytes;
	}

	entry->bytes = gdk_pixbuf_get_byte_length (pixbuf);
	entry->used = g_get_monotonic_time ();
	held_bytes += entry->bytes;

	make_room (file);
	update_full ();
}

void
nemo_thumbnail_memory_released (NemoFile *file)
{
	Held *entry;

	entry = held != NULL ? g_hash_table_lookup (held, file) : NULL;
	if (entry == NULL) {
		return;
	}

	held_bytes -= entry->bytes;
	g_hash_table_remove (held, file);
	update_full ();
}

void
nemo_thumbnail_memory_used (NemoFile *file)
{
	Held *entry;

	entry = held != NULL ? g_hash_table_lookup (held, file) : NULL;
	if (entry != NULL) {
		entry->used = g_get_monotonic_time ();
	}
}

gboolean
nemo_thumbnail_memory_has_room (GdkPixbuf *pixbuf)
{
	gsize bytes = pixbuf != NULL ? gdk_pixbuf_get_byte_length (pixbuf) : 0;

	watch_budget ();

	return held_bytes + bytes <= budget ();
}

gboolean
nemo_thumbnail_memory_full (void)
{
	return g_atomic_int_get (&full);
}

static void
drop_kept (NemoDirectory *directory, gboolean forget)
{
	Kept *folder;
	GList *l;

	folder = kept != NULL ? g_hash_table_lookup (kept, directory) : NULL;
	if (folder == NULL) {
		return;
	}

	/* Shown again meanwhile, so the view has them now. */
	forget = forget && !folder_is_shown (directory);

	g_hash_table_remove (kept, directory);
	if (folder->timeout_id != 0) {
		g_source_remove (folder->timeout_id);
	}

	/* The last unref can take the directory with it, so it is not looked
	   at again after this. */
	for (l = folder->files; l != NULL; l = l->next) {
		if (forget) {
			nemo_file_forget_held_thumbnail (l->data);
		}
		nemo_file_unref (l->data);
	}

	g_list_free (folder->files);
	g_free (folder);
}

static gboolean
kept_timeout (gpointer data)
{
	NemoDirectory *directory = data;
	Kept *folder = g_hash_table_lookup (kept, directory);

	folder->timeout_id = 0;
	drop_kept (directory, TRUE);

	return G_SOURCE_REMOVE;
}

static void
keep_folder (NemoDirectory *directory)
{
	GHashTableIter iter;
	gpointer key;
	GList *files = NULL;
	Kept *folder;

	if (held != NULL) {
		g_hash_table_iter_init (&iter, held);
		while (g_hash_table_iter_next (&iter, &key, NULL)) {
			NemoFile *file = key;

			if (file->details->directory == directory) {
				files = g_list_prepend (files, nemo_file_ref (file));
			}
		}
	}

	/* Left again before the last minute was up. The files are reffed above
	   before the old refs go, so none of them is lost in between. */
	drop_kept (directory, FALSE);

	if (files == NULL) {
		return;
	}

	if (kept == NULL) {
		kept = g_hash_table_new (NULL, NULL);
	}

	folder = g_new0 (Kept, 1);
	folder->files = files;
	folder->timeout_id = g_timeout_add (keep_ms, kept_timeout, directory);
	g_hash_table_insert (kept, directory, folder);
}

void
nemo_thumbnail_memory_folder_shown (NemoDirectory *directory)
{
	guint count;

	if (shown == NULL) {
		shown = g_hash_table_new (NULL, NULL);
	}

	count = GPOINTER_TO_UINT (g_hash_table_lookup (shown, directory));
	g_hash_table_insert (shown, directory, GUINT_TO_POINTER (count + 1));
}

void
nemo_thumbnail_memory_folder_hidden (NemoDirectory *directory)
{
	guint count;

	count = shown != NULL ? GPOINTER_TO_UINT (g_hash_table_lookup (shown, directory)) : 0;
	if (count == 0) {
		return;
	}

	if (count > 1) {
		g_hash_table_insert (shown, directory, GUINT_TO_POINTER (count - 1));
		return;
	}

	g_hash_table_remove (shown, directory);
	keep_folder (directory);
}

void
nemo_thumbnail_memory_flush_hidden (void)
{
	GList *folders, *l;

	if (kept == NULL) {
		return;
	}

	folders = g_hash_table_get_keys (kept);
	for (l = folders; l != NULL; l = l->next) {
		drop_kept (l->data, TRUE);
	}
	g_list_free (folders);
}

gsize
nemo_thumbnail_memory_bytes (void)
{
	return held_bytes;
}

void
nemo_thumbnail_memory_set_times (guint keep, guint recent_ms)
{
	keep_ms = keep;
	recent_us = (gint64) recent_ms * 1000;
}
