/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */

/* nemo-config.c - app-owned settings store.

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

#include "nemo-config.h"

#include <gio/gio.h>
#include <glib/gstdio.h>
#include <errno.h>
#include <string.h>

/* SHCL's file tier is in, for its writer: a temp file beside the target, synced
 * before it is published, and on Windows a replace that carries the old file's
 * permissions, attributes and alternate streams onto the new one - none of
 * which g_file_set_contents does.
 *
 * Only the writer. Reading stays on GLib, the same read the own-write check in
 * config_file_changed makes, so the two compare like with like. */

/* A parse that runs out of memory returns NULL and is handled below. Anywhere
 * else SHCL would print and exit 70; fail the way g_malloc does instead. */
#define SHCL_OOM() g_error ("nemo-config: out of memory in the settings store")
#define SHCL_IMPLEMENTATION
#include "shcl.h"

#include "nemo-config-keys.h"
#include "nemo-file-utilities.h"

#define CONFIG_FILE_NAME      "settings.shcl"
#define SAVE_DEBOUNCE_SECONDS 2
/* A failing write retries, but not every couple of seconds forever. */
#define SAVE_RETRY_SECONDS    30
/* Settings hold ~168 keys - a few KB. Cap the read well above that: the parsed
 * document costs many times the file size, and an oversized or corrupt file
 * has no business taking that much of the process. */
#define CONFIG_MAX_BYTES      (8 * 1024 * 1024)

struct _NemoConfigGroup {
	GObject  parent_instance;
	char    *name;
};

G_DEFINE_TYPE (NemoConfigGroup, nemo_config_group, G_TYPE_OBJECT)

enum {
	CHANGED,
	LAST_SIGNAL
};

static guint signals[LAST_SIGNAL];

/* One document for the whole app. The lock is not decoration: file
 * operations write favorites and view state from worker threads. */
static GMutex      config_lock;
static shcl_doc   *config_doc;
static char       *config_path;
static guint       save_timeout_id;
static GFileMonitor *config_monitor;
static char       *last_written;       /* what we last put on disk, to ignore our own event */
static gsize       last_written_len;   /* byte length - the file may legally hold a NUL */
static GHashTable *config_groups;      /* name -> NemoConfigGroup (owned) */
static gboolean    config_ready;
static GHashTable *pending_keys;       /* set of const NemoConfigKey*, changed but not yet on disk */
static gboolean    save_failing;
static GThread    *config_thread;      /* whoever called init - the UI thread */
static gboolean    hands_off;          /* the file is read but never saved over */
static gboolean    rewrite_due;        /* converted from an older format, not saved yet */

static void schedule_save (const NemoConfigKey *k);
static void emit_changed  (const char *group, const char *key);

/* Key table */

/* "group.key", or bare "key" for the file root. Caller frees. */
static char *
key_path (const NemoConfigKey *k)
{
	if (k->group == NULL || *k->group == '\0')
		return g_strdup (k->key);
	return g_strdup_printf ("%s.%s", k->group, k->key);
}

/* The same path, built once per key and measured once, in key-table order.
 * Reads happen per icon hover, so the accessors take this instead. */
typedef struct {
	const char *s;
	gsize       len;
} KeyPath;

static KeyPath *key_paths;

/* Every accessor comes through here, so a linear walk of 168 keys with two
 * string compares each was on the way into every read. */
static const NemoConfigKey *
find_key (const char *group, const char *key)
{
	static GHashTable *index;
	static gsize       index_once;
	char               buf[128];
	char              *path = buf;
	gint               n;
	const NemoConfigKey *found;

	if (g_once_init_enter (&index_once)) {
		const NemoConfigKey *k;
		GHashTable *t = g_hash_table_new_full (g_str_hash, g_str_equal,
		                                       g_free, NULL);
		gsize count = 0, i = 0;

		for (k = nemo_config_keys; k->key != NULL; k++)
			count++;
		key_paths = g_new0 (KeyPath, count);

		/* The table owns the string; key_paths borrows it. Neither is ever
		 * freed, since the key table cannot change at runtime. */
		for (k = nemo_config_keys; k->key != NULL; k++, i++) {
			char *p = key_path (k);

			key_paths[i].s   = p;
			key_paths[i].len = strlen (p);
			g_hash_table_insert (t, p, (gpointer) k);
		}

		index = t;
		g_once_init_leave (&index_once, 1);
	}

	n = (group == NULL || *group == '\0')
		? g_snprintf (buf, sizeof buf, "%s", key)
		: g_snprintf (buf, sizeof buf, "%s.%s", group, key);

	/* No declared key comes close to the buffer, but a caller could hand in
	 * anything, and a truncated path would look up the wrong entry. */
	if (n >= (gint) sizeof buf)
		path = (group == NULL || *group == '\0')
			? g_strdup (key)
			: g_strdup_printf ("%s.%s", group, key);

	found = g_hash_table_lookup (index, path);
	if (path != buf)
		g_free (path);
	return found;
}

/* Only valid once find_key() has built the table, which every accessor does
 * before it has a key to ask about. */
static const KeyPath *
key_path_of (const NemoConfigKey *k)
{
	return &key_paths[k - nemo_config_keys];
}

static const NemoConfigKey *
require_key (NemoConfigGroup *group, const char *key, NemoConfigType type)
{
	const NemoConfigKey *k;

	/* Almost always means nemo_global_preferences_init() has not run, so
	 * say that rather than blaming the key. */
	if (group == NULL) {
		g_critical ("nemo-config: '%s' read before the config store was opened", key);
		return NULL;
	}

	k = find_key (group->name, key);
	if (k == NULL) {
		g_critical ("nemo-config: no such key '%s' in group '%s'",
		            key, group->name);
		return NULL;
	}
	/* An enum is stored as its nick, so reading one as a string is fine. */
	if (k->type != type && !(k->type == NEMO_CONFIG_ENUM && type == NEMO_CONFIG_STRING)) {
		g_critical ("nemo-config: key '%s.%s' read as the wrong type",
		            k->group, k->key);
		return NULL;
	}
	return k;
}

/* Load / save */

static char *
build_config_path (void)
{
	char *dir  = nemo_get_user_directory ();
	char *path = g_build_filename (dir, CONFIG_FILE_NAME, NULL);

	g_free (dir);
	return path;
}

static shcl_doc *
new_empty_doc (void)
{
	shcl_doc *d = shcl_new ();

	if (d == NULL)
		SHCL_OOM ();
	return d;
}

/* The file as it was, beside it, before a format change rewrites it. Same
 * bytes, so putting it back is a rename. Returns the name, or NULL when it
 * could not be written, and then the old file must stay as it is. */
static char *
backup_old_format (const char *text, gsize len, gint64 format)
{
	GDateTime *now   = g_date_time_new_now_local ();
	char      *stamp = g_date_time_format (now, "%Y%m%d-%H%M%S");
	char      *dir   = g_path_get_dirname (config_path);
	char      *stem  = g_path_get_basename (config_path);
	char      *dot   = strrchr (stem, '.');
	char      *path  = NULL;
	int        tries;

	g_date_time_unref (now);
	if (dot != NULL)
		*dot = '\0';

	/* Two copies starting in the same second can both get here. */
	for (tries = 1; tries < 10; tries++) {
		char  *name, *had = NULL;
		gsize  had_len = 0;

		name = tries == 1
			? g_strdup_printf ("%s_backup_%s_format-v%" G_GINT64_FORMAT ".shcl",
			                   stem, stamp, format)
			: g_strdup_printf ("%s_backup_%s-%d_format-v%" G_GINT64_FORMAT ".shcl",
			                   stem, stamp, tries, format);
		path = g_build_filename (dir, name, NULL);
		g_free (name);

		if (!g_file_get_contents (path, &had, &had_len, NULL)) {
			if (!g_file_set_contents (path, text, (gssize) len, NULL))
				g_clear_pointer (&path, g_free);
			break;
		}
		if (had_len == len && memcmp (had, text, len) == 0) {
			g_free (had);
			break;
		}
		g_free (had);
		g_clear_pointer (&path, g_free);
	}

	g_free (stamp);
	g_free (dir);
	g_free (stem);
	return path;
}

/* A converted file is written fresh, the way this release would have written
 * it: only the keys it knows, each with its comment. Anything else stays in
 * the backup. */
static shcl_doc *
known_keys_only (shcl_doc *from)
{
	shcl_doc            *to = new_empty_doc ();
	const NemoConfigKey *k;

	for (k = nemo_config_keys; k->key != NULL; k++) {
		char     *path = key_path (k);
		gsize     plen = strlen (path);
		gboolean  set  = FALSE;

		if (k->type == NEMO_CONFIG_STRING_LIST) {
			shcl_read_str_arr r = shcl_read_string_array (from, path, plen);

			if (r.status == SHCL_GOOD || r.status == SHCL_EMPTY) {
				const char **values = g_new0 (const char *, r.n + 1);
				size_t      *lens   = g_new0 (size_t, r.n + 1);
				size_t       i;

				for (i = 0; i < r.n; i++) {
					values[i] = r.values[i].p;
					lens[i]   = r.values[i].n;
				}
				set = shcl_set_string_array (to, path, plen, values, lens, r.n);
				g_free (values);
				g_free (lens);
			}
		} else {
			shcl_read_str r = shcl_read_string (from, path, plen);

			if (r.status == SHCL_GOOD || r.status == SHCL_EMPTY)
				set = shcl_set_string (to, path, plen, r.value.p, r.value.n);
		}
		if (set && k->summary != NULL)
			shcl_set_comment (to, path, plen, k->summary, strlen (k->summary));

		shcl_reads_release (from);
		g_free (path);
	}
	return to;
}

/* @at_start: a file with no format line is converted only at startup. One
 * that turns up while running is a hand edit in progress, by today's rules. */
static void
load_locked (gboolean at_start)
{
	char     *text = NULL;
	gsize     len  = 0;
	GError   *error = NULL;
	shcl_doc *fresh;

	/* Refuse an implausibly large file. Keep the in-memory doc so a pending
	 * save cannot then overwrite the real file with defaults. */
	{
		GStatBuf st;
		if (g_stat (config_path, &st) == 0 && st.st_size > CONFIG_MAX_BYTES) {
			g_warning ("nemo-config: %s is %" G_GOFFSET_FORMAT
			           " bytes (cap %d); refusing to load",
			           config_path, (goffset) st.st_size, CONFIG_MAX_BYTES);
			if (config_doc == NULL)
				config_doc = new_empty_doc ();
			return;
		}
	}

	if (g_file_get_contents (config_path, &text, &len, &error)) {
		size_t          i, n;
		gint64          format = shcl_format_version (text, len);
		shcl_migration  conv = { NULL, 0, 0, 0, 0 };
		gboolean        was_hands_off = hands_off;

		hands_off = FALSE;

		/* An older release would write over a newer format and lose what it
		 * can't read, and the newer one would then convert it back with a
		 * fresh backup every time. Read it, never save over it. */
		if (format > SHCL_FORMAT_MAJOR) {
			hands_off = TRUE;
			if (!was_hands_off)
				g_warning ("nemo-config: %s is SHCL format %" G_GINT64_FORMAT
				           ", newer than this release reads (%d); "
				           "settings changed here will not be saved",
				           config_path, format, SHCL_FORMAT_MAJOR);
		} else if (format >= 0 ? format < SHCL_FORMAT_MAJOR : at_start) {
			/* No format line means a 2.x release wrote it, or a hand edit
			 * dropped the line. Which one is not known, so only the spellings
			 * both read the same way are converted. If nothing needed it, the
			 * next save adds the line and that is all. */
			conv = shcl_migrate_unstamped (text, len, FALSE);
			if (format < 0 && conv.lost == 0 && conv.len == len &&
			    memcmp (conv.text, text, len) == 0)
				g_clear_pointer (&conv.text, free);
		}

		if (conv.text != NULL) {
			gint64  was    = format >= 0 ? format : 2;
			char   *backup = backup_old_format (text, len, was);

			if (backup == NULL) {
				g_warning ("nemo-config: %s is in an older SHCL format and no "
				           "backup could be written beside it; it will not be "
				           "saved over", config_path);
				hands_off = TRUE;
			} else {
				g_message ("nemo-config: %s was SHCL format %" G_GINT64_FORMAT
				           " and is rewritten for format %d; the old file is %s",
				           config_path, was, SHCL_FORMAT_MAJOR, backup);
				if (conv.ambiguous > 0 || conv.lost > 0)
					g_warning ("nemo-config: %zu value(s) could be read two "
					           "ways and %zu line(s) could not be converted; "
					           "check them against %s",
					           conv.ambiguous, conv.lost, backup);
				rewrite_due = TRUE;
			}
			g_free (backup);
		}

		/* Out of memory, and the one case SHCL hands back. Same answer as an
		 * unreadable file: keep what we have. */
		fresh = conv.text != NULL ? shcl_parse (conv.text, conv.len)
		                          : shcl_parse (text, len);
		free (conv.text);
		if (fresh == NULL) {
			g_warning ("nemo-config: out of memory reading %s", config_path);
			g_free (text);
			rewrite_due = FALSE;
			if (config_doc == NULL)
				config_doc = new_empty_doc ();
			return;
		}
		shcl_free (config_doc);
		config_doc = fresh;

		/* A broken line is skipped, not fatal - say which, once, so a
		 * hand-edit that lost a value is not a silent mystery. */
		n = shcl_diag_count (config_doc);
		for (i = 0; i < n && i < 10; i++) {
			shcl_str m = shcl_diag_message (config_doc, i);
			if (shcl_diag_severity (config_doc, i) != SHCL_SEV_ERROR)
				continue;
			g_warning ("nemo-config: %s line %zu [%s] %.*s",
			           config_path, shcl_diag_line (config_doc, i),
			           shcl_diag_code (config_doc, i), (int) m.n, m.p);
		}

		/* Anything the parse could not keep is gone from the next save, so say
		 * so rather than let a hand-edited line disappear without a word. */
		if (shcl_lost_count (config_doc) > 0) {
			g_warning ("nemo-config: %s has %zu line(s) that could not be kept; "
			           "the next save will not carry them",
			           config_path, shcl_lost_count (config_doc));
		}

		if (rewrite_due) {
			shcl_doc *known = known_keys_only (config_doc);

			shcl_free (config_doc);
			config_doc = known;
		}

		g_free (last_written);
		last_written = g_memdup2 (text, len);
		last_written_len = len;
		g_free (text);
	} else {
		gboolean gone = g_error_matches (error, G_FILE_ERROR, G_FILE_ERROR_NOENT);

		if (!gone)
			g_warning ("nemo-config: cannot read %s: %s",
			           config_path, error->message);
		g_clear_error (&error);

		/* A transient failure (AV/sync/editor lock, the delete half of a
		 * non-atomic external save) must not swap defaults into memory: a
		 * queued save would then write that near-empty doc over the real
		 * file. Keep what we have; only a genuinely absent file resets. */
		if (!gone && config_doc != NULL)
			return;

		if (gone)
			hands_off = FALSE;
		shcl_free (config_doc);
		config_doc = new_empty_doc ();
	}
}

/* Called with the lock held, once a read's result has been copied out. String
 * and list reads, and shcl_to_canonical, are handed out of a per-document arena
 * that only this or shcl_free gives back. Reads happen per icon hover, so left
 * alone it grows for the life of the process. */
static void
release_reads_locked (void)
{
	shcl_reads_release (config_doc);
}

/* Catalog of defaults */

/* Only changed values are stored, so nothing in the file says what else there
 * is. Everything not set is listed at the end, commented out, with the value
 * used instead. Uncommenting a line is the same as changing it in the dialog.
 * Keys the app writes back itself are left out - a size, a position, the last
 * state of a toggle. */

static const char *const catalog_header[] = {
	"# --------------------------------------------------------------------------",
	"# Anything not set above, with the value used instead.",
	"# Uncomment a line to change one.",
	"# --------------------------------------------------------------------------",
	NULL
};

typedef struct {
	const NemoConfigKey *key;
	char                *path;
	char                *desc;   /* "# <summary>", or NULL */
	char                *line;   /* "#<path>: <default>" */
} CatalogEntry;

static gboolean
in_catalog (const NemoConfigKey *k)
{
	return (k->flags & NEMO_CONFIG_KEY_STATE) == 0;
}

/* SHCL's own writer decides how a value is spelled - the quoting, the list
 * separator, the empty list - so ask it rather than keep a second opinion
 * here. One throwaway document holds every default at a numbered top-level
 * name, and its canonical form is one line per key in table order. */
static char **
default_texts (guint *n_out)
{
	shcl_doc            *scratch = new_empty_doc ();
	const NemoConfigKey *k;
	GPtrArray           *out = g_ptr_array_new ();
	shcl_str             canon;
	char               **lines;
	guint                n = 0, i;

	for (k = nemo_config_keys; k->key != NULL; k++) {
		char  path[24];
		gsize plen;

		if (!in_catalog (k))
			continue;
		plen = g_snprintf (path, sizeof path, "k%u", n++);

		switch (k->type) {
		case NEMO_CONFIG_BOOL:
			shcl_set_bool (scratch, path, plen, g_strcmp0 (k->def, "true") == 0);
			break;
		case NEMO_CONFIG_INT:
			shcl_set_int (scratch, path, plen, g_ascii_strtoll (k->def, NULL, 10));
			break;
		case NEMO_CONFIG_FLOAT:
			shcl_set_float (scratch, path, plen, g_ascii_strtod (k->def, NULL));
			break;
		case NEMO_CONFIG_STRING_LIST: {
			gsize  count = 0, j;
			gsize *lens;

			while (k->def_list != NULL && k->def_list[count] != NULL)
				count++;
			lens = g_new0 (gsize, count + 1);
			for (j = 0; j < count; j++)
				lens[j] = strlen (k->def_list[j]);
			shcl_set_string_array (scratch, path, plen,
			                       (const char *const *) k->def_list, lens, count);
			g_free (lens);
			break;
		}
		default:
			/* A string, and an enum, which is stored as its nick. */
			shcl_set_string (scratch, path, plen, k->def, strlen (k->def));
			break;
		}
	}

	canon = shcl_to_canonical (scratch);
	{
		char *copy = g_strndup (canon.p, canon.n);

		lines = g_strsplit (copy, "\n", -1);
		g_free (copy);
	}

	for (i = 0; lines[i] != NULL; i++) {
		const char *colon = strchr (lines[i], ':');

		if (lines[i][0] != 'k' || colon == NULL)
			continue;
		colon++;
		if (*colon == ' ')
			colon++;
		g_ptr_array_add (out, g_strdup (colon));
	}
	g_strfreev (lines);
	shcl_free (scratch);

	*n_out = out->len;
	if (out->len != n)
		g_warning ("nemo-config: catalog wrote %u keys but read back %u",
		           n, out->len);

	g_ptr_array_add (out, NULL);
	return (char **) g_ptr_array_free (out, FALSE);
}

/* Built once - the key table and the defaults never move at runtime. */
static const GPtrArray *
catalog_entries (void)
{
	static GPtrArray *entries;
	static gsize      once;

	if (g_once_init_enter (&once)) {
		GPtrArray           *list = g_ptr_array_new ();
		guint                n_values = 0;
		char               **values = default_texts (&n_values);
		const NemoConfigKey *k;
		guint                n = 0;

		for (k = nemo_config_keys; k->key != NULL && n < n_values; k++) {
			CatalogEntry *e;

			if (!in_catalog (k))
				continue;
			e = g_new0 (CatalogEntry, 1);
			e->key  = k;
			e->path = key_path (k);
			e->desc = k->summary != NULL ? g_strdup_printf ("# %s", k->summary) : NULL;
			e->line = *values[n] == '\0'
				? g_strdup_printf ("#%s:", e->path)
				: g_strdup_printf ("#%s: %s", e->path, values[n]);
			g_ptr_array_add (list, e);
			n++;
		}

		g_strfreev (values);
		entries = list;
		g_once_init_leave (&once, 1);
	}

	return entries;
}

/* The lines the catalog can produce, for recognizing a copy already in the
 * file. Built from every key, not from the block about to be written: a key
 * set since the last save drops out of the block and its old line still has to
 * come off. Kept in two sets because a description alone is not evidence - the
 * same text sits above the key once it is live. */
static void
catalog_line_sets (GHashTable **keys, GHashTable **descs)
{
	static GHashTable *key_set;
	static GHashTable *desc_set;
	static gsize       once;

	if (g_once_init_enter (&once)) {
		GHashTable      *k = g_hash_table_new (g_str_hash, g_str_equal);
		GHashTable      *d = g_hash_table_new (g_str_hash, g_str_equal);
		const GPtrArray *entries = catalog_entries ();
		guint            i;

		for (i = 0; catalog_header[i] != NULL; i++)
			g_hash_table_add (k, (gpointer) catalog_header[i]);
		for (i = 0; i < entries->len; i++) {
			CatalogEntry *e = g_ptr_array_index (entries, i);

			g_hash_table_add (k, e->line);
			if (e->desc != NULL)
				g_hash_table_add (d, e->desc);
		}
		key_set  = k;
		desc_set = d;
		g_once_init_leave (&once, 1);
	}

	*keys  = key_set;
	*descs = desc_set;
}

/* The block to write: the header, then every key @doc does not already carry.
 * Listing one that is set would invite uncommenting it, and two bindings of
 * one key read as ambiguous and fall back to the default. */
static char *
catalog_body (shcl_doc *doc)
{
	GString         *text = g_string_new (NULL);
	const GPtrArray *entries = catalog_entries ();
	const char      *group = NULL;
	guint            i;

	for (i = 0; catalog_header[i] != NULL; i++)
		g_string_append_printf (text, "%s\n", catalog_header[i]);

	for (i = 0; i < entries->len; i++) {
		CatalogEntry *e = g_ptr_array_index (entries, i);

		if (shcl_exists (doc, e->path, strlen (e->path)))
			continue;
		if (group == NULL || strcmp (group, e->key->group) != 0) {
			g_string_append_c (text, '\n');
			group = e->key->group;
		}
		if (e->desc != NULL)
			g_string_append_printf (text, "%s\n", e->desc);
		g_string_append_printf (text, "%s\n", e->line);
	}

	return g_string_free (text, FALSE);
}

/* SHCL's info block goes last, so a later release can tell which format a file
 * was written in before it converts one (shcl_format_version). It comes back
 * through the parser as ordinary comments, like the catalog, and comes off the
 * same way. Lines are matched as written, plus any "##    Field" line, so a
 * newer SHCL that changes a URL or the legal line still replaces the old
 * block instead of stacking a second one. */
static gboolean
is_banner_line (const char *bare)
{
	static char **lines;
	static gsize  once;
	guint         i;

	if (g_once_init_enter (&once)) {
		lines = g_strsplit (SHCL_GEN_BANNER, "\n", -1);
		g_once_init_leave (&once, 1);
	}

	if (g_str_has_prefix (bare, "##    "))
		return TRUE;
	/* A bare "##" is only ours next to the rest of the block. */
	for (i = 0; lines[i] != NULL; i++) {
		if (strlen (lines[i]) > 2 && strcmp (lines[i], bare) == 0)
			return TRUE;
	}
	return FALSE;
}

/* Take any catalog already in the text out and put a fresh one on the end. An
 * external edit puts the old copy back into the document as ordinary comments,
 * which SHCL then writes out again - indented under whatever group it decided
 * they belong to, if a line was uncommented in the middle of them, so the
 * match ignores leading whitespace. Byte length throughout: the file may
 * legally hold a NUL. */
static char *
apply_catalog (shcl_doc *doc, const char *text, gsize len, gsize *out_len)
{
	GHashTable  *key_lines, *desc_lines;
	GByteArray  *body = g_byte_array_new ();
	GPtrArray   *lines = g_ptr_array_new_with_free_func (g_free);
	GArray      *keeps = g_array_new (FALSE, FALSE, sizeof (gsize));
	const char  *p = text;
	const char  *end = text + len;
	char        *catalog;
	gboolean     dropped_last = FALSE;
	guint        i;

	catalog_line_sets (&key_lines, &desc_lines);

	/* Split first: whether a description goes depends on the line after it. */
	while (p < end) {
		const char *nl = memchr (p, '\n', (gsize) (end - p));
		gsize       n  = nl != NULL ? (gsize) (nl - p) : (gsize) (end - p);
		gsize       whole = nl != NULL ? n + 1 : n;

		g_ptr_array_add (lines, g_strndup (p, n));
		g_array_append_val (keeps, whole);
		p = nl != NULL ? nl + 1 : end;
	}

	p = text;
	for (i = 0; i < lines->len; i++) {
		const char *line = g_ptr_array_index (lines, i);
		const char *bare = line + strspn (line, " \t");
		gsize       whole = g_array_index (keeps, gsize, i);
		gboolean    drop  = g_hash_table_contains (key_lines, bare) || is_banner_line (bare);

		if (!drop && g_hash_table_contains (desc_lines, bare) && i + 1 < lines->len) {
			const char *next = g_ptr_array_index (lines, i + 1);

			drop = g_hash_table_contains (key_lines, next + strspn (next, " \t"));
		}
		if (!drop && strcmp (bare, "##") == 0) {
			const char *prev = i > 0 ? g_ptr_array_index (lines, i - 1) : "";
			const char *next = i + 1 < lines->len ? g_ptr_array_index (lines, i + 1) : "";

			drop = is_banner_line (prev + strspn (prev, " \t")) ||
			       is_banner_line (next + strspn (next, " \t"));
		}
		/* A blank that only sat between two stripped lines goes with them. */
		if (!drop && *bare == '\0' && dropped_last)
			drop = TRUE;
		if (!drop)
			g_byte_array_append (body, (const guint8 *) p, whole);
		dropped_last = drop;
		p += whole;
	}
	g_ptr_array_free (lines, TRUE);
	g_array_free (keeps, TRUE);

	/* Whatever blank lines taking those out left behind. */
	while (body->len > 0 && (body->data[body->len - 1] == '\n' ||
	                         body->data[body->len - 1] == '\r'))
		g_byte_array_set_size (body, body->len - 1);
	if (body->len > 0)
		g_byte_array_append (body, (const guint8 *) "\n\n", 2);

	catalog = catalog_body (doc);
	g_byte_array_append (body, (const guint8 *) catalog, strlen (catalog));
	g_free (catalog);
	g_byte_array_append (body, (const guint8 *) "\n" SHCL_GEN_BANNER,
	                     strlen ("\n" SHCL_GEN_BANNER));

	*out_len = body->len;
	g_byte_array_append (body, (const guint8 *) "", 1);   /* NUL, for anything that prints it */
	return (char *) g_byte_array_free (body, FALSE);
}

static gboolean
save_now (gpointer data)
{
	char     *text = NULL;
	char     *dir;
	gsize     text_len;
	GError   *error = NULL;
	shcl_str  canon;
	gboolean  ok;

	g_mutex_lock (&config_lock);
	save_timeout_id = 0;

	/* Load already said why. Changes stay in memory for this run. */
	if (hands_off) {
		g_mutex_unlock (&config_lock);
		return G_SOURCE_REMOVE;
	}

	canon = shcl_to_canonical (config_doc);
	/* By length throughout: SHCL is NUL-transparent, so a NUL that came in
	 * from the file must not truncate the write or the own-write check. */
	text = apply_catalog (config_doc, canon.p, canon.n, &text_len);

	/* Every value a write replaced is still in the document's arena, and the
	 * canonical text is in the read one. Both go here, once the text above
	 * has been copied out. */
	shcl_compact (config_doc);
	g_mutex_unlock (&config_lock);

	dir = g_path_get_dirname (config_path);
	g_mkdir_with_parents (dir, 0700);
	g_free (dir);

	ok = shcl_write_file_atomic (config_path, text != NULL ? text : "", text_len) != 0;
	if (!ok) {
		g_set_error (&error, G_FILE_ERROR, g_file_error_from_errno (errno),
		             "%s", g_strerror (errno));
	}

	g_mutex_lock (&config_lock);
	if (ok) {
		/* Only now: last_written is the "is this event our own write?" test,
		 * so claiming a write that failed would make the untouched file on
		 * disk look foreign and reload it over the live settings. */
		g_free (last_written);
		last_written = g_memdup2 (text, text_len);
		last_written_len = text_len;
		g_hash_table_remove_all (pending_keys);
		save_failing = FALSE;
		rewrite_due = FALSE;
	} else {
		if (!save_failing) {
			g_warning ("nemo-config: cannot write %s: %s",
			           config_path, error->message);
			save_failing = TRUE;
		}
		if (save_timeout_id == 0)
			save_timeout_id = g_timeout_add_seconds (SAVE_RETRY_SECONDS,
			                                         save_now, NULL);
	}
	g_mutex_unlock (&config_lock);

	g_clear_error (&error);
	g_free (text);
	return G_SOURCE_REMOVE;
}

/* Called with the lock held. @k is the key just changed, remembered so an
 * external edit arriving inside the debounce window doesn't discard it. */
static void
schedule_save (const NemoConfigKey *k)
{
	if (k != NULL)
		g_hash_table_add (pending_keys, (gpointer) k);

	if (save_timeout_id != 0)
		return;
	save_timeout_id = g_timeout_add_seconds (SAVE_DEBOUNCE_SECONDS, save_now, NULL);
}

/* Reload on external edit */

/* Snapshot every declared key as text, so an external edit can be turned
 * into the same per-key change signals a set() would have produced. */
static GHashTable *
snapshot_locked (void)
{
	GHashTable          *out = g_hash_table_new_full (g_str_hash, g_str_equal,
	                                                  g_free, g_free);
	const NemoConfigKey *k;

	for (k = nemo_config_keys; k->key != NULL; k++) {
		char          *path = key_path (k);
		/* read_string, not read_raw: raw only answers for fenced blocks and
		 * reports every ordinary value as empty, which made the whole diff
		 * blind to a changed value and only able to see a key appear or go. */
		shcl_read_str  r    = shcl_read_string (config_doc, path, strlen (path));

		if (r.status == SHCL_NOT_FOUND) {
			g_free (path);
			continue;
		}
		g_hash_table_insert (out, path, g_strndup (r.value.p, r.value.n));
		release_reads_locked ();
	}
	return out;
}

/* One not-yet-saved key, held across a reload. Everything in the file is text,
 * so a list of strings covers scalars and lists alike; absent means the key was
 * reset or dropped for matching its default. */
typedef struct {
	const NemoConfigKey *key;
	gboolean             present;
	char               **values;
} PendingValue;

static void
pending_value_free (gpointer data)
{
	PendingValue *p = data;

	g_strfreev (p->values);
	g_free (p);
}

/* Both of these are called with the lock held. */
static GList *
capture_pending_locked (void)
{
	GHashTableIter  iter;
	gpointer        k;
	GList          *out = NULL;

	g_hash_table_iter_init (&iter, pending_keys);
	while (g_hash_table_iter_next (&iter, &k, NULL)) {
		PendingValue      *p    = g_new0 (PendingValue, 1);
		char              *path = key_path (k);
		shcl_read_str_arr  r;

		p->key = k;
		p->present = shcl_exists (config_doc, path, strlen (path)) != 0;

		if (p->present) {
			size_t i;

			r = shcl_read_string_array (config_doc, path, strlen (path));
			p->values = g_new0 (char *, r.n + 1);
			for (i = 0; i < r.n; i++)
				p->values[i] = g_strndup (r.values[i].p, r.values[i].n);
			release_reads_locked ();
		}

		g_free (path);
		out = g_list_prepend (out, p);
	}
	return out;
}

static void
restore_pending_locked (GList *pending)
{
	GList *l;

	for (l = pending; l != NULL; l = l->next) {
		PendingValue *p    = l->data;
		char         *path = key_path (p->key);
		gsize         n    = 0;

		if (!p->present) {
			shcl_remove (config_doc, path, strlen (path));
			g_free (path);
			continue;
		}

		while (p->values[n] != NULL)
			n++;

		if (p->key->type == NEMO_CONFIG_STRING_LIST) {
			size_t *lens = g_new0 (size_t, n + 1);
			gsize   i;

			for (i = 0; i < n; i++)
				lens[i] = strlen (p->values[i]);
			shcl_set_string_array (config_doc, path, strlen (path),
			                       (const char *const *) p->values, lens, n);
			g_free (lens);
		} else {
			/* Every scalar is stored as its text form and SHCL writes strings
			 * unquoted, so this reproduces the bool/int/enum spelling exactly. */
			const char *v = n > 0 ? p->values[0] : "";

			shcl_set_string (config_doc, path, strlen (path), v, strlen (v));
		}

		g_free (path);
	}
}

static void
config_file_changed (GFileMonitor      *monitor,
                     GFile             *file,
                     GFile             *other,
                     GFileMonitorEvent  event,
                     gpointer           data)
{
	GHashTable          *before, *after;
	const NemoConfigKey *k;
	GList               *pending;
	char                *text = NULL;
	gsize                len = 0;
	gboolean             rewrite;

	if (event != G_FILE_MONITOR_EVENT_CHANGES_DONE_HINT &&
	    event != G_FILE_MONITOR_EVENT_CREATED &&
	    event != G_FILE_MONITOR_EVENT_DELETED)
		return;

	/* Our own write comes back as an event; ignore it. */
	if (g_file_get_contents (config_path, &text, &len, NULL)) {
		gboolean ours;
		g_mutex_lock (&config_lock);
		ours = (last_written != NULL && last_written_len == len &&
		        memcmp (last_written, text, len) == 0);
		g_mutex_unlock (&config_lock);
		g_free (text);
		if (ours)
			return;
	}

	g_mutex_lock (&config_lock);
	before = snapshot_locked ();
	pending = capture_pending_locked ();
	load_locked (FALSE);
	/* An in-app change made inside the debounce window is not on disk yet, so
	 * the file we just read doesn't have it. Put those keys back, or the
	 * queued save would write the reloaded document and lose them. */
	restore_pending_locked (pending);
	/* A save skipped while the file was hands off left nothing queued. */
	if (pending != NULL && !hands_off && save_timeout_id == 0)
		schedule_save (NULL);
	after = snapshot_locked ();
	rewrite = rewrite_due;
	g_mutex_unlock (&config_lock);

	g_list_free_full (pending, pending_value_free);
	if (rewrite)
		save_now (NULL);

	for (k = nemo_config_keys; k->key != NULL; k++) {
		char       *path = key_path (k);
		const char *a = g_hash_table_lookup (before, path);
		const char *b = g_hash_table_lookup (after, path);

		if (g_strcmp0 (a, b) != 0)
			emit_changed (k->group, k->key);
		g_free (path);
	}

	g_hash_table_destroy (before);
	g_hash_table_destroy (after);
}

/* Lifecycle */

void
nemo_config_init (void)
{
	GFile    *file;
	gboolean  rewrite;

	if (config_ready)
		return;

	g_mutex_init (&config_lock);
	config_thread = g_thread_self ();
	config_path   = build_config_path ();
	config_groups = g_hash_table_new_full (g_str_hash, g_str_equal,
	                                       g_free, g_object_unref);
	/* Keys live in a static table, so the set borrows the pointers. */
	pending_keys  = g_hash_table_new (g_direct_hash, g_direct_equal);

	g_mutex_lock (&config_lock);
	load_locked (TRUE);
	rewrite = rewrite_due;
	g_mutex_unlock (&config_lock);

	if (rewrite)
		save_now (NULL);

	/* Hand-editing the file is the point, so pick edits up while we run. */
	file = g_file_new_for_path (config_path);
	config_monitor = g_file_monitor_file (file, G_FILE_MONITOR_NONE, NULL, NULL);
	if (config_monitor != NULL)
		g_signal_connect (config_monitor, "changed",
		                  G_CALLBACK (config_file_changed), NULL);
	g_object_unref (file);

	config_ready = TRUE;
}

void
nemo_config_flush (void)
{
	guint id;

	if (!config_ready)
		return;

	/* Under the lock: a worker thread can arm this timer at any moment. */
	g_mutex_lock (&config_lock);
	id = save_timeout_id;
	save_timeout_id = 0;
	g_mutex_unlock (&config_lock);

	if (id == 0)
		return;

	g_source_remove (id);
	save_now (NULL);
}

void
nemo_config_shutdown (void)
{
	if (!config_ready)
		return;

	nemo_config_flush ();
	g_clear_object (&config_monitor);
}

gboolean
nemo_config_is_ready (void)
{
	return config_ready;
}

gboolean
nemo_config_get_default_boolean (const char *group, const char *key)
{
	const NemoConfigKey *k = find_key (group, key);

	g_return_val_if_fail (k != NULL && k->type == NEMO_CONFIG_BOOL, FALSE);

	return g_strcmp0 (k->def, "true") == 0;
}

char *
nemo_config_get_path (void)
{
	return g_strdup (config_path);
}

NemoConfigGroup *
nemo_config_get_group (const char *group)
{
	NemoConfigGroup *g;

	if (group == NULL)
		group = "";

	g_return_val_if_fail (config_ready, NULL);

	/* config_lock guards the table itself: a lazy insert here can race a
	 * worker-thread emit_changed lookup. Groups are never removed, so the
	 * returned pointer stays valid once unlocked. */
	g_mutex_lock (&config_lock);
	g = g_hash_table_lookup (config_groups, group);
	if (g == NULL) {
		g = g_object_new (NEMO_TYPE_CONFIG_GROUP, NULL);
		g->name = g_strdup (group);
		g_hash_table_insert (config_groups, g_strdup (group), g);
	}
	g_mutex_unlock (&config_lock);
	return g;
}

typedef struct {
	char *group;
	char *key;
} ChangedIdle;

static void
emit_changed_now (const char *group, const char *key)
{
	NemoConfigGroup *g;

	/* Look the group up under the lock (get_group may be inserting), but emit
	 * outside it: handlers run synchronously and can re-enter config. */
	g_mutex_lock (&config_lock);
	g = g_hash_table_lookup (config_groups, group);
	g_mutex_unlock (&config_lock);
	if (g != NULL)
		g_signal_emit (g, signals[CHANGED], g_quark_from_string (key), key);
}

static gboolean
emit_changed_idle (gpointer data)
{
	ChangedIdle *c = data;

	emit_changed_now (c->group, c->key);
	return G_SOURCE_REMOVE;
}

static void
emit_changed_idle_free (gpointer data)
{
	ChangedIdle *c = data;

	g_free (c->group);
	g_free (c->key);
	g_free (c);
}

static void
emit_changed (const char *group, const char *key)
{
	if (group == NULL)
		group = "";

	/* GSettings delivered "changed" on the main context and all 84 ported
	 * handlers assume it - they touch widgets. File operations reach a set()
	 * from worker threads (delete -> favorites -> set_strv), so hop when we
	 * are not already there. Always queued: g_main_context_invoke would run
	 * the handler on the worker itself whenever the main thread is outside
	 * the loop at that moment. */
	if (config_thread == NULL || g_thread_self () == config_thread) {
		emit_changed_now (group, key);
		return;
	}

	{
		ChangedIdle *c = g_new0 (ChangedIdle, 1);

		c->group = g_strdup (group);
		c->key   = g_strdup (key);
		g_idle_add_full (G_PRIORITY_DEFAULT, emit_changed_idle, c,
		                 emit_changed_idle_free);
	}
}

static void
nemo_config_group_finalize (GObject *object)
{
	NemoConfigGroup *self = NEMO_CONFIG_GROUP (object);

	g_free (self->name);
	G_OBJECT_CLASS (nemo_config_group_parent_class)->finalize (object);
}

static void
nemo_config_group_class_init (NemoConfigGroupClass *klass)
{
	G_OBJECT_CLASS (klass)->finalize = nemo_config_group_finalize;

	/* Detailed, so "changed::some-key" works exactly as it used to. */
	signals[CHANGED] =
		g_signal_new ("changed", NEMO_TYPE_CONFIG_GROUP,
		              G_SIGNAL_RUN_LAST | G_SIGNAL_DETAILED,
		              0, NULL, NULL, NULL,
		              G_TYPE_NONE, 1, G_TYPE_STRING);
}

static void
nemo_config_group_init (NemoConfigGroup *self)
{
}

/* Reads */

/* A missing key falls back to the declared default. An empty one does not:
 * "set to nothing" is a real value, and conflating the two would make an
 * emptied list spring back to its default. */
gboolean
nemo_config_get_boolean (NemoConfigGroup *group, const char *key)
{
	const NemoConfigKey *k = require_key (group, key, NEMO_CONFIG_BOOL);
	const KeyPath       *p;
	gboolean             fallback, out;

	if (k == NULL)
		return FALSE;

	fallback = (g_strcmp0 (k->def, "true") == 0);
	p = key_path_of (k);

	g_mutex_lock (&config_lock);
	out = shcl_get_bool (config_doc, p->s, p->len, fallback) ? TRUE : FALSE;
	g_mutex_unlock (&config_lock);

	return out;
}

gint64
nemo_config_get_int64 (NemoConfigGroup *group, const char *key)
{
	const NemoConfigKey *k = require_key (group, key, NEMO_CONFIG_INT);
	const KeyPath       *p;
	gint64               out;

	if (k == NULL)
		return 0;

	p = key_path_of (k);
	g_mutex_lock (&config_lock);
	out = shcl_get_int (config_doc, p->s, p->len,
	                    k->def ? g_ascii_strtoll (k->def, NULL, 10) : 0);
	g_mutex_unlock (&config_lock);

	return out;
}

gint
nemo_config_get_int (NemoConfigGroup *group, const char *key)
{
	return (gint) nemo_config_get_int64 (group, key);
}

gdouble
nemo_config_get_double (NemoConfigGroup *group, const char *key)
{
	const NemoConfigKey *k = require_key (group, key, NEMO_CONFIG_FLOAT);
	const KeyPath       *p;
	gdouble              out;

	if (k == NULL)
		return 0.0;

	p = key_path_of (k);
	g_mutex_lock (&config_lock);
	out = shcl_get_float (config_doc, p->s, p->len,
	                      k->def ? g_ascii_strtod (k->def, NULL) : 0.0);
	g_mutex_unlock (&config_lock);

	return out;
}

char *
nemo_config_get_string (NemoConfigGroup *group, const char *key)
{
	const NemoConfigKey *k = require_key (group, key, NEMO_CONFIG_STRING);
	const KeyPath       *p;
	char                *out;
	shcl_read_str        r;

	if (k == NULL)
		return g_strdup ("");

	p = key_path_of (k);
	g_mutex_lock (&config_lock);
	r = shcl_read_string (config_doc, p->s, p->len);
	if (r.status == SHCL_GOOD)
		out = g_strndup (r.value.p, r.value.n);
	else if (r.status == SHCL_EMPTY)
		out = g_strdup ("");
	else
		out = g_strdup (k->def ? k->def : "");
	release_reads_locked ();
	g_mutex_unlock (&config_lock);

	return out;
}

char **
nemo_config_get_strv (NemoConfigGroup *group, const char *key)
{
	const NemoConfigKey *k = require_key (group, key, NEMO_CONFIG_STRING_LIST);
	const KeyPath       *p;
	char               **out;
	shcl_read_str_arr    r;

	if (k == NULL)
		return g_new0 (char *, 1);

	p = key_path_of (k);
	g_mutex_lock (&config_lock);
	r = shcl_read_string_array (config_doc, p->s, p->len);

	/* SHCL_EMPTY is a real value ("set to nothing"), so only a missing or
	 * unusable key falls back. MULTIPLE means the key is written twice - easy
	 * to do by hand, and it used to open the list view with no columns at all. */
	if (r.status == SHCL_MULTIPLE || r.status == SHCL_BAD_TYPE)
		g_warning ("nemo-config: '%s' is %s in %s; using the default",
		           p->s,
		           r.status == SHCL_MULTIPLE ? "listed more than once" : "not a list",
		           config_path);

	if (r.status == SHCL_NOT_FOUND || r.status == SHCL_MULTIPLE ||
	    r.status == SHCL_BAD_TYPE) {
		out = k->def_list ? g_strdupv ((char **) k->def_list)
		                  : g_new0 (char *, 1);
	} else {
		size_t i;

		out = g_new0 (char *, r.n + 1);
		for (i = 0; i < r.n; i++)
			out[i] = g_strndup (r.values[i].p, r.values[i].n);
	}
	release_reads_locked ();
	g_mutex_unlock (&config_lock);

	return out;
}

gint
nemo_config_get_enum (NemoConfigGroup *group, const char *key)
{
	const NemoConfigKey       *k = require_key (group, key, NEMO_CONFIG_ENUM);
	const NemoConfigEnumValue *v;
	const KeyPath             *p;
	char                      *nick = NULL;
	shcl_read_str              r;
	gint                       out = 0;

	if (k == NULL)
		return 0;

	p = key_path_of (k);
	g_mutex_lock (&config_lock);
	r = shcl_read_string (config_doc, p->s, p->len);
	if (r.status == SHCL_GOOD)
		nick = g_strndup (r.value.p, r.value.n);
	release_reads_locked ();
	g_mutex_unlock (&config_lock);

	if (nick == NULL)
		nick = g_strdup (k->def);

	for (v = k->enum_values; v != NULL && v->nick != NULL; v++) {
		if (g_strcmp0 (v->nick, nick) == 0) {
			g_free (nick);
			return v->value;
		}
	}

	/* An unrecognized nick is a hand-edit typo - fall back to the default
	 * rather than to whatever 0 happens to mean. */
	g_warning ("nemo-config: '%s' is not a valid value for '%s.%s'",
	           nick, k->group, k->key);
	g_free (nick);
	for (v = k->enum_values; v != NULL && v->nick != NULL; v++) {
		if (g_strcmp0 (v->nick, k->def) == 0)
			out = v->value;
	}
	return out;
}

/* Writes */

/* Storing a value that equals the default would pin the key: a later change
 * to that default could no longer reach the user. Drop it instead, which
 * also keeps the file down to what was actually chosen. */
static gboolean
drop_if_default (const NemoConfigKey *k, const char *as_text, char *path)
{
	if (k->def == NULL || g_strcmp0 (k->def, as_text) != 0)
		return FALSE;

	shcl_remove (config_doc, path, strlen (path));
	return TRUE;
}

/* Only for a key that wasn't in the file yet: set_comment pushes another
 * leading line each time it's called, so re-commenting on every write would
 * grow the same line forever. An existing key already carries its comment. */
static void
apply_comment_if_new (const NemoConfigKey *k, const char *path, gboolean existed)
{
	if (k->summary == NULL || existed)
		return;
	shcl_set_comment (config_doc, path, strlen (path),
	                  k->summary, strlen (k->summary));
}

void
nemo_config_set_boolean (NemoConfigGroup *group, const char *key, gboolean value)
{
	const NemoConfigKey *k = require_key (group, key, NEMO_CONFIG_BOOL);
	char                *path;
	gboolean             existed;

	if (k == NULL)
		return;

	path = key_path (k);
	g_mutex_lock (&config_lock);
	existed = shcl_exists (config_doc, path, strlen (path)) != 0;
	if (!drop_if_default (k, value ? "true" : "false", path)) {
		shcl_set_bool (config_doc, path, strlen (path), value ? 1 : 0);
		apply_comment_if_new (k, path, existed);
	}
	schedule_save (k);
	g_mutex_unlock (&config_lock);
	g_free (path);

	emit_changed (k->group, k->key);
}

void
nemo_config_set_int (NemoConfigGroup *group, const char *key, gint value)
{
	nemo_config_set_int64 (group, key, value);
}

void
nemo_config_set_int64 (NemoConfigGroup *group, const char *key, gint64 value)
{
	const NemoConfigKey *k = require_key (group, key, NEMO_CONFIG_INT);
	char                *path, *text;
	gboolean             existed;

	if (k == NULL)
		return;

	path = key_path (k);
	text = g_strdup_printf ("%" G_GINT64_FORMAT, value);
	g_mutex_lock (&config_lock);
	existed = shcl_exists (config_doc, path, strlen (path)) != 0;
	if (!drop_if_default (k, text, path)) {
		shcl_set_int (config_doc, path, strlen (path), value);
		apply_comment_if_new (k, path, existed);
	}
	schedule_save (k);
	g_mutex_unlock (&config_lock);
	g_free (text);
	g_free (path);

	emit_changed (k->group, k->key);
}

void
nemo_config_set_double (NemoConfigGroup *group, const char *key, gdouble value)
{
	const NemoConfigKey *k = require_key (group, key, NEMO_CONFIG_FLOAT);
	char                *path;
	gboolean             existed;

	if (k == NULL)
		return;

	path = key_path (k);
	g_mutex_lock (&config_lock);
	existed = shcl_exists (config_doc, path, strlen (path)) != 0;
	shcl_set_float (config_doc, path, strlen (path), value);
	apply_comment_if_new (k, path, existed);
	schedule_save (k);
	g_mutex_unlock (&config_lock);
	g_free (path);

	emit_changed (k->group, k->key);
}

void
nemo_config_set_string (NemoConfigGroup *group, const char *key, const char *value)
{
	const NemoConfigKey *k = require_key (group, key, NEMO_CONFIG_STRING);
	char                *path;
	gboolean             existed;

	if (k == NULL)
		return;
	if (value == NULL)
		value = "";

	path = key_path (k);
	g_mutex_lock (&config_lock);
	existed = shcl_exists (config_doc, path, strlen (path)) != 0;
	if (!drop_if_default (k, value, path)) {
		shcl_set_string (config_doc, path, strlen (path), value, strlen (value));
		apply_comment_if_new (k, path, existed);
	}
	schedule_save (k);
	g_mutex_unlock (&config_lock);
	g_free (path);

	emit_changed (k->group, k->key);
}

void
nemo_config_set_strv (NemoConfigGroup *group, const char *key, const char *const *value)
{
	const NemoConfigKey *k = require_key (group, key, NEMO_CONFIG_STRING_LIST);
	char                *path;
	gsize                n = 0, i;
	size_t              *lens;
	gboolean             existed;

	if (k == NULL)
		return;

	while (value != NULL && value[n] != NULL)
		n++;

	/* Same as the scalars: a list matching the default is not stored. */
	if (k->def_list != NULL) {
		gsize dn = 0;
		while (k->def_list[dn] != NULL)
			dn++;
		if (dn == n) {
			gboolean same = TRUE;
			for (i = 0; i < n; i++) {
				if (g_strcmp0 (k->def_list[i], value[i]) != 0) {
					same = FALSE;
					break;
				}
			}
			if (same) {
				path = key_path (k);
				g_mutex_lock (&config_lock);
				shcl_remove (config_doc, path, strlen (path));
				schedule_save (k);
				g_mutex_unlock (&config_lock);
				g_free (path);
				emit_changed (k->group, k->key);
				return;
			}
		}
	}

	lens = g_new0 (size_t, n + 1);
	for (i = 0; i < n; i++)
		lens[i] = strlen (value[i]);

	path = key_path (k);
	g_mutex_lock (&config_lock);
	existed = shcl_exists (config_doc, path, strlen (path)) != 0;
	shcl_set_string_array (config_doc, path, strlen (path),
	                       (const char *const *) value, lens, n);
	apply_comment_if_new (k, path, existed);
	schedule_save (k);
	g_mutex_unlock (&config_lock);
	g_free (lens);
	g_free (path);

	emit_changed (k->group, k->key);
}

/* The file stores the nick, so both entry points below end up here. */
static void
store_enum_nick (const NemoConfigKey *k, const char *nick)
{
	char     *path = key_path (k);
	gboolean  existed;

	g_mutex_lock (&config_lock);
	existed = shcl_exists (config_doc, path, strlen (path)) != 0;
	if (!drop_if_default (k, nick, path)) {
		shcl_set_string (config_doc, path, strlen (path), nick, strlen (nick));
		apply_comment_if_new (k, path, existed);
	}
	schedule_save (k);
	g_mutex_unlock (&config_lock);
	g_free (path);

	emit_changed (k->group, k->key);
}

/* Set by nick rather than by number, for callers that already have the nick
 * (the bind set-mappings all do). Unknown nicks are refused, not stored. */
static void
set_enum_by_nick (NemoConfigGroup *group, const char *key, const char *nick)
{
	const NemoConfigKey       *k = require_key (group, key, NEMO_CONFIG_ENUM);
	const NemoConfigEnumValue *v;

	if (k == NULL)
		return;

	for (v = k->enum_values; v != NULL && v->nick != NULL; v++) {
		if (g_strcmp0 (v->nick, nick) == 0) {
			store_enum_nick (k, v->nick);
			return;
		}
	}

	g_critical ("nemo-config: '%s' is not a valid value for '%s.%s'",
	            nick != NULL ? nick : "(null)", k->group, k->key);
}

void
nemo_config_set_enum (NemoConfigGroup *group, const char *key, gint value)
{
	const NemoConfigKey       *k = require_key (group, key, NEMO_CONFIG_ENUM);
	const NemoConfigEnumValue *v;

	if (k == NULL)
		return;

	for (v = k->enum_values; v != NULL && v->nick != NULL; v++) {
		if (v->value == value) {
			store_enum_nick (k, v->nick);
			return;
		}
	}

	g_critical ("nemo-config: %d is not a valid value for '%s.%s'",
	            value, k->group, k->key);
}

void
nemo_config_reset (NemoConfigGroup *group, const char *key)
{
	const NemoConfigKey *k;
	char                *path;

	if (group == NULL) {
		g_critical ("nemo-config: '%s' reset before the config store was opened", key);
		return;
	}

	k = find_key (group->name, key);
	if (k == NULL) {
		g_critical ("nemo-config: no such key '%s' in group '%s'",
		            key, group->name);
		return;
	}

	path = key_path (k);
	g_mutex_lock (&config_lock);
	shcl_remove (config_doc, path, strlen (path));
	schedule_save (k);
	g_mutex_unlock (&config_lock);
	g_free (path);

	emit_changed (k->group, k->key);
}

/* How the key stands in the file. Only stored keys are worth touching - a
 * default was never written, so resetting one would emit a change nobody made. */
static int
key_status (const NemoConfigKey *k)
{
	char          *path = key_path (k);
	shcl_read_str  r;

	g_mutex_lock (&config_lock);
	r = shcl_read_string (config_doc, path, strlen (path));
	release_reads_locked ();
	g_mutex_unlock (&config_lock);
	g_free (path);

	return r.status;
}

static gboolean
key_is_stored (const NemoConfigKey *k)
{
	return key_status (k) != SHCL_NOT_FOUND;
}

void
nemo_config_reset_all (void)
{
	const NemoConfigKey *k;

	g_return_if_fail (config_ready);

	for (k = nemo_config_keys; k->key != NULL; k++) {
		if (key_is_stored (k))
			nemo_config_reset (nemo_config_get_group (k->group), k->key);
	}
}

#ifdef G_OS_WIN32
/* A leading slash that is not a UNC share. Nothing a Windows install writes
 * starts that way, so it can only be a path from a POSIX machine. */
static gboolean
is_posix_absolute (const char *value)
{
	return value != NULL && value[0] == '/' && value[1] != '/';
}
#endif

void
nemo_config_drop_foreign_paths (void)
{
#ifdef G_OS_WIN32
	const NemoConfigKey *k;

	g_return_if_fail (config_ready);

	for (k = nemo_config_keys; k->key != NULL; k++) {
		NemoConfigGroup *group;
		int              status = key_status (k);

		if (k->type != NEMO_CONFIG_STRING && k->type != NEMO_CONFIG_STRING_LIST)
			continue;
		/* A key the reader cannot make sense of - written twice, say - falls
		 * back to the default on a read, and writing that back would replace a
		 * line the user can still fix by hand with one they never typed. */
		if (status != SHCL_GOOD && status != SHCL_EMPTY)
			continue;

		group = nemo_config_get_group (k->group);

		if (k->type == NEMO_CONFIG_STRING) {
			char *value = nemo_config_get_string (group, k->key);

			if (is_posix_absolute (value))
				nemo_config_reset (group, k->key);
			g_free (value);
		} else {
			char **values = nemo_config_get_strv (group, k->key);
			GPtrArray *kept = g_ptr_array_new ();
			guint i;

			for (i = 0; values[i] != NULL; i++) {
				if (!is_posix_absolute (values[i]))
					g_ptr_array_add (kept, values[i]);
			}

			if (kept->len != g_strv_length (values)) {
				if (kept->len == 0) {
					nemo_config_reset (group, k->key);
				} else {
					g_ptr_array_add (kept, NULL);
					nemo_config_set_strv (group, k->key,
					                      (const char *const *) kept->pdata);
				}
			}

			g_ptr_array_free (kept, TRUE);
			g_strfreev (values);
		}
	}
#endif
}

char **
nemo_config_list_keys (NemoConfigGroup *group)
{
	const NemoConfigKey *k;
	GPtrArray           *out = g_ptr_array_new ();
	const char          *name = group ? group->name : "";

	for (k = nemo_config_keys; k->key != NULL; k++) {
		if (g_strcmp0 (k->group, name) == 0)
			g_ptr_array_add (out, g_strdup (k->key));
	}
	g_ptr_array_add (out, NULL);
	return (char **) g_ptr_array_free (out, FALSE);
}

/* Property binding */

typedef struct {
	NemoConfigGroup      *group;
	char                 *key;
	GObject              *object;
	char                 *property;
	NemoConfigBindFlags   flags;
	NemoConfigGetMapping  get_mapping;
	NemoConfigSetMapping  set_mapping;
	gpointer              user_data;
	GDestroyNotify        destroy;
	gulong                config_handler;
	gulong                notify_handler;
	gboolean              syncing;
} ConfigBinding;

static void
config_value_clear (NemoConfigValue *cv)
{
	g_clear_pointer (&cv->s, g_free);
	g_clear_pointer (&cv->sv, g_strfreev);
}

static void
read_config_value (ConfigBinding *b, NemoConfigValue *cv)
{
	const NemoConfigKey *k = find_key (b->group->name, b->key);

	memset (cv, 0, sizeof (*cv));
	if (k == NULL)
		return;

	cv->type = k->type;
	switch (k->type) {
	case NEMO_CONFIG_BOOL:
		cv->b = nemo_config_get_boolean (b->group, b->key);
		break;
	case NEMO_CONFIG_INT:
		cv->i = nemo_config_get_int64 (b->group, b->key);
		break;
	case NEMO_CONFIG_FLOAT:
		cv->d = nemo_config_get_double (b->group, b->key);
		break;
	case NEMO_CONFIG_STRING:
		cv->s = nemo_config_get_string (b->group, b->key);
		break;
	case NEMO_CONFIG_STRING_LIST:
		cv->sv = nemo_config_get_strv (b->group, b->key);
		break;
	case NEMO_CONFIG_ENUM:
		cv->i = nemo_config_get_enum (b->group, b->key);
		cv->s = nemo_config_get_string (b->group, b->key);
		break;
	}
}

static void
write_config_value (ConfigBinding *b, const NemoConfigValue *cv)
{
	switch (cv->type) {
	case NEMO_CONFIG_BOOL:
		nemo_config_set_boolean (b->group, b->key, cv->b);
		break;
	case NEMO_CONFIG_INT:
		nemo_config_set_int64 (b->group, b->key, cv->i);
		break;
	case NEMO_CONFIG_FLOAT:
		nemo_config_set_double (b->group, b->key, cv->d);
		break;
	case NEMO_CONFIG_STRING:
		nemo_config_set_string (b->group, b->key, cv->s);
		break;
	case NEMO_CONFIG_STRING_LIST:
		nemo_config_set_strv (b->group, b->key, (const char *const *) cv->sv);
		break;
	case NEMO_CONFIG_ENUM:
		/* A set-mapping hands us the nick and leaves the number at 0, so
		 * taking the number here wrote the zero-valued nick whatever the
		 * user picked. Only the unmapped path fills the number. */
		if (cv->s != NULL)
			set_enum_by_nick (b->group, b->key, cv->s);
		else
			nemo_config_set_enum (b->group, b->key, (gint) cv->i);
		break;
	}
}

/* config -> property */
static void
binding_sync_to_object (ConfigBinding *b)
{
	NemoConfigValue cv;
	GParamSpec     *pspec;
	GValue          value = G_VALUE_INIT;

	if (b->syncing || b->object == NULL)
		return;

	pspec = g_object_class_find_property (G_OBJECT_GET_CLASS (b->object),
	                                      b->property);
	if (pspec == NULL)
		return;

	read_config_value (b, &cv);
	g_value_init (&value, pspec->value_type);

	if (b->get_mapping != NULL) {
		if (!b->get_mapping (&value, &cv, b->user_data)) {
			g_value_unset (&value);
			config_value_clear (&cv);
			return;
		}
	} else {
		switch (cv.type) {
		case NEMO_CONFIG_BOOL: {
			gboolean v = cv.b;
			if (b->flags & NEMO_CONFIG_BIND_INVERT_BOOLEAN)
				v = !v;
			g_value_set_boolean (&value, v);
			break;
		}
		case NEMO_CONFIG_INT:
		case NEMO_CONFIG_ENUM:
			if (pspec->value_type == G_TYPE_UINT)
				g_value_set_uint (&value, (guint) cv.i);
			else
				g_value_set_int (&value, (gint) cv.i);
			break;
		case NEMO_CONFIG_FLOAT:
			g_value_set_double (&value, cv.d);
			break;
		case NEMO_CONFIG_STRING:
			g_value_set_string (&value, cv.s);
			break;
		case NEMO_CONFIG_STRING_LIST:
			g_value_unset (&value);
			config_value_clear (&cv);
			return;
		}
	}

	b->syncing = TRUE;
	g_object_set_property (b->object, b->property, &value);
	b->syncing = FALSE;

	g_value_unset (&value);
	config_value_clear (&cv);
}

/* property -> config */
static void
binding_sync_to_config (ConfigBinding *b)
{
	const NemoConfigKey *k;
	NemoConfigValue      cv;
	GParamSpec          *pspec;
	GValue               value = G_VALUE_INIT;

	if (b->syncing || b->object == NULL)
		return;

	k = find_key (b->group->name, b->key);
	if (k == NULL)
		return;

	pspec = g_object_class_find_property (G_OBJECT_GET_CLASS (b->object),
	                                      b->property);
	if (pspec == NULL)
		return;

	g_value_init (&value, pspec->value_type);
	g_object_get_property (b->object, b->property, &value);

	memset (&cv, 0, sizeof (cv));
	cv.type = k->type;

	if (b->set_mapping != NULL) {
		if (!b->set_mapping (&value, &cv, b->user_data)) {
			g_value_unset (&value);
			config_value_clear (&cv);
			return;
		}
	} else {
		switch (k->type) {
		case NEMO_CONFIG_BOOL: {
			gboolean v = g_value_get_boolean (&value);
			if (b->flags & NEMO_CONFIG_BIND_INVERT_BOOLEAN)
				v = !v;
			cv.b = v;
			break;
		}
		case NEMO_CONFIG_INT:
		case NEMO_CONFIG_ENUM:
			cv.i = (pspec->value_type == G_TYPE_UINT)
			     ? (gint64) g_value_get_uint (&value)
			     : (gint64) g_value_get_int (&value);
			break;
		case NEMO_CONFIG_FLOAT:
			cv.d = g_value_get_double (&value);
			break;
		case NEMO_CONFIG_STRING:
			cv.s = g_value_dup_string (&value);
			break;
		case NEMO_CONFIG_STRING_LIST:
			g_value_unset (&value);
			return;
		}
	}

	b->syncing = TRUE;
	write_config_value (b, &cv);
	b->syncing = FALSE;

	g_value_unset (&value);
	config_value_clear (&cv);
}

static void
on_config_changed (NemoConfigGroup *group, const char *key, gpointer data)
{
	binding_sync_to_object ((ConfigBinding *) data);
}

static void
on_property_notify (GObject *object, GParamSpec *pspec, gpointer data)
{
	binding_sync_to_config ((ConfigBinding *) data);
}

static void
binding_free (ConfigBinding *b)
{
	if (b->config_handler != 0)
		g_signal_handler_disconnect (b->group, b->config_handler);
	if (b->destroy != NULL)
		b->destroy (b->user_data);
	g_free (b->key);
	g_free (b->property);
	g_free (b);
}

static void
binding_object_gone (gpointer data, GObject *where_the_object_was)
{
	ConfigBinding *b = data;

	b->object = NULL;
	binding_free (b);
}

void
nemo_config_bind_with_mapping (NemoConfigGroup      *group,
                               const char           *key,
                               gpointer              object,
                               const char           *property,
                               NemoConfigBindFlags   flags,
                               NemoConfigGetMapping  get_mapping,
                               NemoConfigSetMapping  set_mapping,
                               gpointer              user_data,
                               GDestroyNotify        destroy)
{
	ConfigBinding *b;
	gboolean       do_get, do_set;

	g_return_if_fail (NEMO_IS_CONFIG_GROUP (group));
	g_return_if_fail (G_IS_OBJECT (object));

	/* DEFAULT means both directions, as it did before. */
	do_get = (flags & NEMO_CONFIG_BIND_GET) || !(flags & (NEMO_CONFIG_BIND_GET | NEMO_CONFIG_BIND_SET));
	do_set = (flags & NEMO_CONFIG_BIND_SET) || !(flags & (NEMO_CONFIG_BIND_GET | NEMO_CONFIG_BIND_SET));

	b = g_new0 (ConfigBinding, 1);
	b->group       = group;
	b->key         = g_strdup (key);
	b->object      = object;
	b->property    = g_strdup (property);
	b->flags       = flags;
	b->get_mapping = get_mapping;
	b->set_mapping = set_mapping;
	b->user_data   = user_data;
	b->destroy     = destroy;

	if (do_get) {
		char *detailed = g_strdup_printf ("changed::%s", key);
		b->config_handler = g_signal_connect (group, detailed,
		                                      G_CALLBACK (on_config_changed), b);
		g_free (detailed);
	}
	if (do_set) {
		char *detailed = g_strdup_printf ("notify::%s", property);
		b->notify_handler = g_signal_connect (object, detailed,
		                                      G_CALLBACK (on_property_notify), b);
		g_free (detailed);
	}

	g_object_weak_ref (object, binding_object_gone, b);

	if (do_get)
		binding_sync_to_object (b);
	else if (do_set)
		binding_sync_to_config (b);
}

void
nemo_config_bind (NemoConfigGroup     *group,
                  const char          *key,
                  gpointer             object,
                  const char          *property,
                  NemoConfigBindFlags  flags)
{
	nemo_config_bind_with_mapping (group, key, object, property, flags,
	                               NULL, NULL, NULL, NULL);
}
