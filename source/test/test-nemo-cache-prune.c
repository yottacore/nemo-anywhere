/* The file cache prune. Each rule is checked for what it takes and for what it
 * has to leave alone: a missing file goes but a file on a missing folder stays,
 * an old thumbnail goes but a recently drawn one stays, and the size limit takes
 * the least recently drawn first. The claim is checked from both sides, since
 * two processes pruning at once is what it exists to stop. */

#include <config.h>

#include <stdio.h>
#include <string.h>
#include <glib.h>
#include <glib/gstdio.h>
#include <sqlite3.h>

#ifdef G_OS_WIN32
#include <process.h>
#else
#include <unistd.h>
#endif

#include <libnemo-private/nemo-cache-db.h>
#include <libnemo-private/nemo-cache-db-prune.h>
#include <libnemo-private/nemo-global-preferences.h>

#include "test-scratch.h"
#include "test-check.h"

#define DAY    ((gint64) 24 * 60 * 60)
#define HOUR   ((gint64) 60 * 60)
#define MTIME  ((gint64) 1700000000 * G_USEC_PER_SEC)

static char *scratch = NULL;

static gint64
wall (void)
{
	return g_get_real_time () / G_USEC_PER_SEC;
}

static NemoCachePruneRules
rules_now (void)
{
	NemoCachePruneRules rules = { 0 };

	rules.gap_min_secs = 4 * HOUR;
	rules.gap_max_secs = 24 * HOUR;
	rules.force = TRUE;
	rules.now = wall ();

	return rules;
}

/* Reads one number straight out of the file, the way another process would. */
static gint64
query_int (const char *sql)
{
	g_autofree char *path = nemo_cache_db_path ();
	sqlite3         *handle = NULL;
	sqlite3_stmt    *stmt = NULL;
	gint64           value = -1;

	if (sqlite3_open_v2 (path, &handle, SQLITE_OPEN_READWRITE, NULL) != SQLITE_OK) {
		sqlite3_close (handle);
		return -1;
	}

	sqlite3_busy_timeout (handle, 3000);

	if (sqlite3_prepare_v2 (handle, sql, -1, &stmt, NULL) == SQLITE_OK) {
		int rc = sqlite3_step (stmt);

		if (rc == SQLITE_ROW)
			value = sqlite3_column_int64 (stmt, 0);
		else if (rc == SQLITE_DONE)
			value = sqlite3_changes (handle);
	}

	sqlite3_finalize (stmt);
	sqlite3_close (handle);

	return value;
}

static gint64
paths_named (const char *uri)
{
	g_autofree char *sql = g_strdup_printf ("SELECT COUNT(*) FROM paths WHERE uri = '%s'", uri);

	return query_int (sql);
}

static gboolean
has_thumbnail (NemoCacheDb *db, const char *uri)
{
	return nemo_cache_db_thumbnail_stats (db, uri, NULL, NULL);
}

/* A thumbnail of `bytes` bytes for a file of its own, stored at `stored`. */
static void
store (NemoCacheDb *db, const char *uri, gint64 file_bytes, gint64 stored, gsize image_len)
{
	NemoFileId          id = { 0 };
	NemoThumbnailRecord rec = { 0 };
	char               *buf = g_malloc (image_len);
	g_autoptr (GBytes)  image = NULL;

	memset (buf, 'x', image_len);
	image = g_bytes_new_take (buf, image_len);

	id.bytes = file_bytes;
	id.mtime = MTIME;

	rec.size = rec.width = rec.height = 128;
	rec.format = NEMO_THUMBNAIL_FORMAT_JPEG;
	rec.stored = stored;

	check (nemo_cache_db_thumbnail_store (db, uri, &id, &rec, image));
}

/* A pass that finishes says when the next is due, inside the gap, and the next
 * one is not due until then unless forced. */
static void
check_due_and_gap (void)
{
	NemoCachePruneRules rules = rules_now ();
	gint64 removed = -1, due = 0, again = 0;

	check (nemo_cache_db_prune (&rules, NULL, &removed, &due) == NEMO_CACHE_PRUNE_DONE);
	check (due >= rules.now + rules.gap_min_secs);
	check (due <= rules.now + rules.gap_max_secs);
	check (query_int ("SELECT due FROM prune") == due);
	check (query_int ("SELECT owner FROM prune") == 0);
	check (query_int ("SELECT completed_by FROM prune") == (gint64) getpid ());
	check (query_int ("SELECT completed FROM prune") > 0);

	rules.force = FALSE;
	check (nemo_cache_db_prune (&rules, NULL, NULL, &again) == NEMO_CACHE_PRUNE_NOT_DUE);
	check (again == due);

	rules.now = due;
	check (nemo_cache_db_prune (&rules, NULL, NULL, NULL) == NEMO_CACHE_PRUNE_DONE);
}

/* A live claim from another process keeps this one out; one nobody has touched
 * for an hour is a process that died, and is taken over. */
static void
check_claim (void)
{
	NemoCachePruneRules rules = rules_now ();
	g_autofree char *live = g_strdup_printf ("UPDATE prune SET owner = %d, heartbeat = %" G_GINT64_FORMAT,
						 getpid () + 1, wall ());
	g_autofree char *dead = g_strdup_printf ("UPDATE prune SET owner = %d, heartbeat = %" G_GINT64_FORMAT,
						 getpid () + 1, wall () - HOUR);

	check (query_int (live) == 1);
	check (nemo_cache_db_prune (&rules, NULL, NULL, NULL) == NEMO_CACHE_PRUNE_BUSY);
	check (query_int ("SELECT owner FROM prune") == getpid () + 1);

	check (query_int (dead) == 1);
	check (nemo_cache_db_prune (&rules, NULL, NULL, NULL) == NEMO_CACHE_PRUNE_DONE);
	check (query_int ("SELECT owner FROM prune") == 0);
}

/* A stopped pass lets go of the claim and leaves the due time as it was. */
static void
check_cancel (void)
{
	NemoCachePruneRules rules = rules_now ();
	g_autoptr (GCancellable) cancellable = g_cancellable_new ();
	gint64 due_before = query_int ("SELECT due FROM prune");

	g_cancellable_cancel (cancellable);
	check (nemo_cache_db_prune (&rules, cancellable, NULL, NULL) == NEMO_CACHE_PRUNE_CANCELLED);
	check (query_int ("SELECT owner FROM prune") == 0);
	check (query_int ("SELECT due FROM prune") == due_before);
}

typedef struct {
	gboolean             called;
	NemoCachePruneResult result;
} NowState;

static void
now_done (NemoCachePruneResult result, gint64 removed, gpointer user_data)
{
	NowState *state = user_data;

	(void) removed;

	state->called = TRUE;
	state->result = result;
}

/* The button's pass runs whether one is due or not, and one at a time. */
static void
check_prune_now (void)
{
	NowState state = { 0 };
	g_autofree char *later = g_strdup_printf ("UPDATE prune SET due = %" G_GINT64_FORMAT ", completed = 0",
						  wall () + DAY);
	gint64 deadline;

	check (query_int (later) == 1);

	check (nemo_cache_db_prune_now (now_done, &state));
	check (nemo_cache_db_prune_running ());
	check (!nemo_cache_db_prune_now (now_done, &state));

	deadline = g_get_monotonic_time () + 30 * G_USEC_PER_SEC;
	while (!state.called && g_get_monotonic_time () < deadline)
		g_main_context_iteration (NULL, TRUE);

	check (state.called);
	check (state.result == NEMO_CACHE_PRUNE_DONE);
	check (!nemo_cache_db_prune_running ());
	check (query_int ("SELECT completed FROM prune") > 0);
	check (query_int ("SELECT due FROM prune") != wall () + DAY);
}

static void
check_missing (NemoCacheDb *db)
{
	g_autofree char *dir = g_build_filename (scratch, "pictures", NULL);
	g_autofree char *gone_path = g_build_filename (dir, "gone.jpg", NULL);
	g_autofree char *kept_path = g_build_filename (dir, "kept.jpg", NULL);
	g_autofree char *away_path = g_build_filename (scratch, "unplugged", "away.jpg", NULL);
	g_autofree char *gone = NULL, *kept = NULL, *away = NULL;
	const char *remote = "sftp://example.invalid/far.jpg";
	NemoCachePruneRules rules = rules_now ();
	gint64 removed = 0;

	check (g_mkdir_with_parents (dir, 0700) == 0);
	check (g_file_set_contents (gone_path, "g", 1, NULL));
	check (g_file_set_contents (kept_path, "k", 1, NULL));

	gone = g_filename_to_uri (gone_path, NULL, NULL);
	kept = g_filename_to_uri (kept_path, NULL, NULL);
	away = g_filename_to_uri (away_path, NULL, NULL);

	store (db, gone, 1001, wall (), 64);
	store (db, kept, 1002, wall (), 64);
	store (db, away, 1003, wall (), 64);
	store (db, remote, 1004, wall (), 64);

	check (g_unlink (gone_path) == 0);

	/* Switched off, nothing is looked at. */
	rules.drop_missing = FALSE;
	check (nemo_cache_db_prune (&rules, NULL, &removed, NULL) == NEMO_CACHE_PRUNE_DONE);
	check (paths_named (gone) == 1);
	check (has_thumbnail (db, gone));

	rules.drop_missing = TRUE;
	check (nemo_cache_db_prune (&rules, NULL, &removed, NULL) == NEMO_CACHE_PRUNE_DONE);
	check (removed == 1);
	check (paths_named (gone) == 0);
	check (!has_thumbnail (db, gone));
	check (query_int ("SELECT COUNT(*) FROM files WHERE bytes = 1001") == 0);

	check (has_thumbnail (db, kept));
	check (has_thumbnail (db, away));
	check (has_thumbnail (db, remote));
}

/* Age counts from the last draw, or from when it was made if never drawn. */
static void
check_age (NemoCacheDb *db)
{
	NemoCachePruneRules rules = rules_now ();
	gint64 removed = 0;

	store (db, "file:///age/old.jpg", 2001, wall () - 400 * DAY, 64);
	store (db, "file:///age/new.jpg", 2002, wall () - 10 * DAY, 64);
	store (db, "file:///age/old-but-drawn.jpg", 2003, wall () - 400 * DAY, 64);

	nemo_cache_db_note_render (db, "file:///age/old-but-drawn.jpg");
	nemo_cache_db_flush (db);

	rules.max_age_secs = 180 * DAY;
	check (nemo_cache_db_prune (&rules, NULL, &removed, NULL) == NEMO_CACHE_PRUNE_DONE);
	check (removed == 1);
	check (!has_thumbnail (db, "file:///age/old.jpg"));
	check (has_thumbnail (db, "file:///age/new.jpg"));
	check (has_thumbnail (db, "file:///age/old-but-drawn.jpg"));

	/* The record of the file stays; only the picture was old. */
	check (paths_named ("file:///age/old.jpg") == 1);
}

/* Over the limit, the least recently drawn go until it fits, and the freed
 * space is handed back rather than left inside the file. */
static void
check_size (NemoCacheDb *db)
{
	NemoCachePruneRules rules = rules_now ();
	gint64 limit = 1024 * 1024;
	gint64 removed = 0;
	gint64 used;
	int i;

	for (i = 0; i < 40; i++) {
		g_autofree char *uri = g_strdup_printf ("file:///size/%02d.jpg", i);

		store (db, uri, 3000 + i, wall () - DAY + i, 64 * 1024);
	}

	check (query_int ("PRAGMA page_count") * query_int ("PRAGMA page_size") > limit);

	rules.max_bytes = limit;
	check (nemo_cache_db_prune (&rules, NULL, &removed, NULL) == NEMO_CACHE_PRUNE_DONE);

	used = (query_int ("PRAGMA page_count") - query_int ("PRAGMA freelist_count"))
		* query_int ("PRAGMA page_size");
	check (used > 0 && used <= limit);
	check (removed > 0 && removed < 40);

	/* Taken from the old end only. */
	check (!has_thumbnail (db, "file:///size/00.jpg"));
	check (has_thumbnail (db, "file:///size/39.jpg"));

	/* Compacted: nothing left on the free list. */
	check (query_int ("PRAGMA freelist_count") == 0);
}

/* The prune picks each batch holding the write lock, and every other window's
 * store waits behind it. Picking by age used to sort the whole thumbnail
 * table, about a second a batch at 40 thousand, against a 3 second busy
 * timeout. What sqlite reports for the real queries has to stay within one
 * batch however many rows there are. */
static void
check_pick_cost (NemoCacheDb *db)
{
	gint64 rows_walked = -1, sorts = -1;
	gint64 held = 0;
	int i;

	for (i = 0; i < 1500; i++) {
		g_autofree char *uri = g_strdup_printf ("file:///many/%04d.jpg", i);

		store (db, uri, 5000 + i, wall () - (i * 7919) % 1500, 16);
	}
	nemo_cache_db_usage (db, &held, NULL);
	check (held >= 1500);

	check (nemo_cache_db_prune_pick_cost (db, &rows_walked, &sorts));
	g_print ("  prune picks over %" G_GINT64_FORMAT " thumbnails: %" G_GINT64_FORMAT
		 " rows walked, %" G_GINT64_FORMAT " sorts\n", held, rows_walked, sorts);
	check (sorts == 0);
	check (rows_walked >= 0 && rows_walked <= 256);
}

#define BIG_THUMBNAIL ((gint64) 256 * 1024)
#define BATCH_BYTES   ((gint64) 1024 * 1024)

static void
check_one_batch (const char *rule, gint64 removed, gint64 expected)
{
	gint64 largest = nemo_cache_db_prune_largest_batch ();

	g_print ("  %s: %" G_GINT64_FORMAT " removed, at most %" G_GINT64_FORMAT
		 " bytes in one write\n", rule, removed, largest);
	check (removed == expected);
	check (largest > 0 && largest < BATCH_BYTES + BIG_THUMBNAIL);
}

/* Each batch holds the write lock while it deletes, and a big thumbnail takes
 * longer to delete than a small one. So a batch ends after about a megabyte
 * as well as after so many rows, and every rule that deletes thumbnails has to
 * keep to that. */
static void
check_batch_bytes (NemoCacheDb *db)
{
	g_autofree char *dir = g_build_filename (scratch, "big", NULL);
	NemoCachePruneRules rules;
	gint64 removed = 0;
	gint64 used;
	int i;

	check (g_mkdir_with_parents (dir, 0700) == 0);

	/* Gone from a folder that is still there, so what they held is left with
	 * no name and goes as an orphan. */
	for (i = 0; i < 24; i++) {
		g_autofree char *name = g_strdup_printf ("gone-%02d.jpg", i);
		g_autofree char *path = g_build_filename (dir, name, NULL);
		g_autofree char *uri = g_filename_to_uri (path, NULL, NULL);

		store (db, uri, 7000 + i, wall (), BIG_THUMBNAIL);
	}
	rules = rules_now ();
	rules.drop_missing = TRUE;
	check (nemo_cache_db_prune (&rules, NULL, &removed, NULL) == NEMO_CACHE_PRUNE_DONE);
	check_one_batch ("orphans", removed, 24);

	for (i = 0; i < 24; i++) {
		g_autofree char *uri = g_strdup_printf ("file:///big/old-%02d.jpg", i);

		store (db, uri, 7100 + i, wall () - 400 * DAY, BIG_THUMBNAIL);
	}
	rules = rules_now ();
	rules.max_age_secs = 180 * DAY;
	check (nemo_cache_db_prune (&rules, NULL, &removed, NULL) == NEMO_CACHE_PRUNE_DONE);
	check_one_batch ("age", removed, 24);

	/* The oldest in the file, so the size rule takes these first. */
	for (i = 0; i < 24; i++) {
		g_autofree char *uri = g_strdup_printf ("file:///big/over-%02d.jpg", i);

		store (db, uri, 7200 + i, wall () - 2 * DAY + i, BIG_THUMBNAIL);
	}
	nemo_cache_db_flush (db);
	used = (query_int ("PRAGMA page_count") - query_int ("PRAGMA freelist_count"))
		* query_int ("PRAGMA page_size");
	rules = rules_now ();
	rules.max_bytes = used - 16 * BIG_THUMBNAIL;
	check (nemo_cache_db_prune (&rules, NULL, &removed, NULL) == NEMO_CACHE_PRUNE_DONE);
	check_one_batch ("size", removed, 16);
	check (!has_thumbnail (db, "file:///big/over-15.jpg"));
	check (has_thumbnail (db, "file:///big/over-16.jpg"));
}

/* A damaged page deep in the file does not stop it opening, so the pass is what
 * finds it. It leaves a marker, and the next open starts over. */
static void
check_damage (void)
{
	g_autofree char *path = nemo_cache_db_path ();
	g_autofree char *marker = g_strconcat (path, ".damaged", NULL);
	NemoCachePruneRules rules = rules_now ();
	NemoCacheDb *db;
	GStatBuf info;
	FILE *fp;
	char junk[4096];
	long at;

	db = nemo_cache_db_get ();
	for (int i = 0; i < 20; i++) {
		g_autofree char *uri = g_strdup_printf ("file:///damage/%02d.jpg", i);

		store (db, uri, 4000 + i, wall (), 32 * 1024);
	}
	nemo_cache_db_close ();

	/* The back half is thumbnails, well away from the pages the tables start
	 * on, which is what lets it open at all. */
	check (g_stat (path, &info) == 0);
	memset (junk, 0x5a, sizeof (junk));
	fp = g_fopen (path, "r+b");
	check (fp != NULL);
	if (fp == NULL)
		return;
	for (at = (long) info.st_size / 2; at + (long) sizeof (junk) < (long) info.st_size; at += 3 * (long) sizeof (junk)) {
		check (fseek (fp, at, SEEK_SET) == 0);
		check (fwrite (junk, 1, sizeof (junk), fp) == sizeof (junk));
	}
	fclose (fp);

	check (nemo_cache_db_prune (&rules, NULL, NULL, NULL) == NEMO_CACHE_PRUNE_DAMAGED);
	check (g_file_test (marker, G_FILE_TEST_EXISTS));

	db = nemo_cache_db_get ();
	check (db != NULL);
	check (!g_file_test (marker, G_FILE_TEST_EXISTS));
	check (!has_thumbnail (db, "file:///damage/19.jpg"));
	check (nemo_cache_db_prune (&rules, NULL, NULL, NULL) == NEMO_CACHE_PRUNE_DONE);
}

int
main (int argc, char *argv[])
{
	NemoCacheDb *db;

	(void) argc;
	(void) argv;

	scratch = test_scratch_config_home ("nemo-cacheprune-XXXXXX");
	if (scratch == NULL) {
		g_printerr ("could not make a scratch dir\n");
		return 77;
	}

	/* The scheduler reads its rules from the settings. */
	nemo_config_init ();

	db = nemo_cache_db_get ();
	if (db == NULL) {
		g_printerr ("could not open a file cache in %s\n", scratch);
		return 77;
	}

	check_due_and_gap ();
	check_claim ();
	check_cancel ();
	check_prune_now ();
	check_missing (db);
	check_age (db);
	check_size (db);
	check_pick_cost (db);
	check_batch_bytes (db);
	check_damage ();

	nemo_cache_db_close ();
	g_free (scratch);

	if (failures > 0) {
		g_printerr ("%d check(s) failed\n", failures);
		return 1;
	}

	g_print ("OK\n");
	return 0;
}
