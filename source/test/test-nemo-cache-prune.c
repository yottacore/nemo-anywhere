/* The file cache prune. Each rule is checked for what it takes and for what it
 * has to leave alone: a missing file goes but a file on a missing folder stays,
 * an old thumbnail goes but a recently drawn one stays, an old name with no
 * thumbnail goes but a recent one stays, and the size limit takes the least
 * recently drawn first. The claim is checked from both sides, since
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

/* A name stored with a checksum and no thumbnail, `fill` standing in for the
 * checksum. */
static void
name_with_digest (NemoCacheDb *db, const char *uri, gint64 file_bytes, guint8 fill)
{
	NemoFileId id = { 0 };

	id.bytes = file_bytes;
	id.mtime = MTIME;
	memset (id.digest, fill, sizeof (id.digest));
	id.has_digest = TRUE;

	check (nemo_cache_db_set_digest (db, uri, &id));
}

static gboolean
reads_digest (NemoCacheDb *db, const char *uri, gint64 file_bytes)
{
	guint8 digest[NEMO_CACHE_DIGEST_LEN];

	return nemo_cache_db_lookup_digest (db, uri, file_bytes, MTIME, digest);
}

static void
backdate (const char *uri, gint64 days)
{
	g_autofree char *sql = g_strdup_printf ("UPDATE paths SET seen = %" G_GINT64_FORMAT
						" WHERE uri = '%s'", wall () - days * DAY, uri);

	check (query_int (sql) == 1);
}

static gint64
files_of_size (gint64 file_bytes)
{
	g_autofree char *sql = g_strdup_printf ("SELECT COUNT(*) FROM files WHERE bytes = %"
						G_GINT64_FORMAT, file_bytes);

	return query_int (sql);
}

/* A name whose thumbnail has gone holds only what the file gives again, so it
 * goes once nothing has been stored under it for the age limit, or 180 days
 * with no limit, and its file record with it. A name that still has a
 * thumbnail is left to the thumbnail rules, and a copy's other name keeps the
 * record. Read back only through calls that never put a row back. */
static void
check_bare_names (NemoCacheDb *db)
{
	const char *stale = "file:///bare/stale.jpg";
	const char *fresh = "file:///bare/fresh.jpg";
	const char *drawn = "file:///bare/drawn.jpg";
	const char *copy_old = "file:///bare/copy-old.txt";
	const char *copy_new = "file:///bare/copy-new.txt";
	const char *unlimited = "file:///bare/unlimited.txt";
	const char *younger = "file:///bare/younger.txt";
	NemoCachePruneRules rules = rules_now ();
	g_autofree char *sql = NULL;
	int i;

	store (db, stale, 9001, wall (), 64);
	store (db, fresh, 9002, wall (), 64);
	store (db, drawn, 9003, wall (), 64);
	check (nemo_cache_db_thumbnail_forget (db, stale));
	check (nemo_cache_db_thumbnail_forget (db, fresh));
	name_with_digest (db, stale, 9001, 0x11);
	name_with_digest (db, copy_old, 9004, 0x22);
	name_with_digest (db, copy_new, 9004, 0x22);
	check (files_of_size (9004) == 1);

	backdate (stale, 400);
	backdate (drawn, 400);
	backdate (copy_old, 400);
	check (reads_digest (db, stale, 9001));

	rules.max_age_secs = 180 * DAY;
	check (nemo_cache_db_prune (&rules, NULL, NULL, NULL) == NEMO_CACHE_PRUNE_DONE);

	check (!reads_digest (db, stale, 9001));
	check (paths_named (stale) == 0);
	check (files_of_size (9001) == 0);

	check (paths_named (fresh) == 1);
	check (paths_named (drawn) == 1);
	check (has_thumbnail (db, drawn));

	check (paths_named (copy_old) == 0);
	check (reads_digest (db, copy_new, 9004));
	check (files_of_size (9004) == 1);

	name_with_digest (db, unlimited, 9005, 0x33);
	name_with_digest (db, younger, 9006, 0x44);
	backdate (unlimited, 200);
	backdate (younger, 100);

	/* More than one write's worth. */
	for (i = 0; i < 300; i++) {
		g_autofree char *uri = g_strdup_printf ("file:///bare/many/%03d.txt", i);
		NemoFileId id = { 0 };

		id.bytes = 9100 + i;
		id.mtime = MTIME;
		check (nemo_cache_db_note_file (db, uri, &id));
	}
	sql = g_strdup_printf ("UPDATE paths SET seen = %" G_GINT64_FORMAT
			       " WHERE uri LIKE 'file:///bare/many/%%'", wall () - 200 * DAY);
	check (query_int (sql) == 300);

	rules = rules_now ();
	check (nemo_cache_db_prune (&rules, NULL, NULL, NULL) == NEMO_CACHE_PRUNE_DONE);
	check (!reads_digest (db, unlimited, 9005));
	check (files_of_size (9005) == 0);
	check (reads_digest (db, younger, 9006));
	check (query_int ("SELECT COUNT(*) FROM paths WHERE uri LIKE 'file:///bare/many/%'") == 0);
	check (query_int ("SELECT COUNT(*) FROM files WHERE bytes BETWEEN 9100 AND 9399") == 0);
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

typedef struct {
	sqlite3 *handle;
	gint     stop;
	gint     writes;
	gint     failed;
	gint     most_steps;
	gint64   longest;
} Waiter;

/* Another window writing every 20 ms through a connection of its own, while
 * the prune runs. Only the wait for the lock is timed, since a commit can go
 * on to copy the journal into the file, which is the disk and not the
 * prune. */
static gpointer
keep_writing (gpointer data)
{
	Waiter *waiter = data;

	while (!g_atomic_int_get (&waiter->stop)) {
		gint   steps = nemo_cache_db_prune_steps ();
		gint64 began = g_get_monotonic_time ();
		int    rc = sqlite3_exec (waiter->handle, "BEGIN IMMEDIATE", NULL, NULL, NULL);

		waiter->longest = MAX (waiter->longest, g_get_monotonic_time () - began);
		waiter->most_steps = MAX (waiter->most_steps, nemo_cache_db_prune_steps () - steps);
		waiter->writes++;

		if (rc == SQLITE_OK)
			rc = sqlite3_exec (waiter->handle,
					   "UPDATE prune SET removed = removed WHERE id = 1; COMMIT",
					   NULL, NULL, NULL);
		if (rc != SQLITE_OK) {
			waiter->failed++;
			sqlite3_exec (waiter->handle, "ROLLBACK", NULL, NULL, NULL);
		}

		g_usleep (20 * 1000);
	}

	return NULL;
}

#define WAITING_THUMBNAILS 32
#define WAITING_THUMBNAIL  ((gint64) 512 * 1024)

/* sqlite gives a free lock to whoever asks first, and its own wait sleeps up
 * to 100 ms between tries. A prune that went from one write straight into the
 * next kept a waiting window out until the whole run of writes was done, over
 * a second with a big cache. A waiter may sit out the write it found, and
 * never a run of them. Counted in the prune's writes rather than in time,
 * since a loaded box slows both sides alike.
 *
 * A window reading the cache holds its snapshot through the pass, so the
 * prune's commits have nothing to copy back into the file. Without that the
 * copying leaves gaps of its own, as long as the disk makes them, and the
 * result would turn on whether the waiter happened to wake in one. */
static void
check_waiting_writer (NemoCacheDb *db)
{
	NemoCachePruneRules rules;
	Waiter   waiter = { 0 };
	GThread *thread;
	sqlite3 *reader;
	gint64   removed = 0;
	gint64   used;
	gint     steps;
	int      i;

	for (i = 0; i < WAITING_THUMBNAILS; i++) {
		g_autofree char *uri = g_strdup_printf ("file:///waiting/old-%02d.jpg", i);

		store (db, uri, 8000 + i, wall () - 3 * DAY + i, WAITING_THUMBNAIL);
	}
	nemo_cache_db_flush (db);
	used = (query_int ("PRAGMA page_count") - query_int ("PRAGMA freelist_count"))
		* query_int ("PRAGMA page_size");

	rules = rules_now ();
	rules.max_bytes = used - WAITING_THUMBNAILS * WAITING_THUMBNAIL + WAITING_THUMBNAIL / 2;

	reader = nemo_cache_db_open_another ();
	waiter.handle = nemo_cache_db_open_another ();
	check (reader != NULL && waiter.handle != NULL);
	if (reader == NULL || waiter.handle == NULL) {
		sqlite3_close (reader);
		sqlite3_close (waiter.handle);
		return;
	}
	check (sqlite3_exec (reader, "BEGIN; SELECT COUNT(*) FROM prune", NULL, NULL, NULL) == SQLITE_OK);

	thread = g_thread_new ("waiting-writer", keep_writing, &waiter);
	g_usleep (50 * 1000);

	steps = nemo_cache_db_prune_steps ();
	check (nemo_cache_db_prune (&rules, NULL, &removed, NULL) == NEMO_CACHE_PRUNE_DONE);
	steps = nemo_cache_db_prune_steps () - steps;

	g_atomic_int_set (&waiter.stop, 1);
	g_thread_join (thread);
	sqlite3_close (waiter.handle);
	sqlite3_exec (reader, "COMMIT", NULL, NULL, NULL);
	sqlite3_close (reader);

	g_print ("  waiting writer: %d writes beside %d prune writes, longest wait %.3f s,"
		 " at most %d prune writes in one wait\n", waiter.writes, steps,
		 waiter.longest / 1e6, waiter.most_steps);
	check (removed >= WAITING_THUMBNAILS);
	check (steps >= WAITING_THUMBNAILS / 2);
	check (waiter.failed == 0);
	check (waiter.most_steps <= 2);
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
	check_bare_names (db);
	check_size (db);
	check_pick_cost (db);
	check_batch_bytes (db);
	check_waiting_writer (db);
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
