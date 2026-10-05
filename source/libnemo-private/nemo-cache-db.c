/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */

/* nemo-cache-db.c - what is known about files on disk, kept between runs.

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

#include "nemo-cache-db.h"

#include "nemo-file-utilities.h"
#include "nemo-share.h"

#include <glib/gstdio.h>
#include <sqlite3.h>
#include <string.h>

#ifdef G_OS_WIN32
#include <process.h>
#else
#include <unistd.h>
#endif

/* Bumped when the tables change. The store is a cache, so a file written by
 * another version is thrown away rather than migrated. */
#define SCHEMA_VERSION 4

/* How long a statement waits for another process to finish writing. Long enough
 * that a busy window wins rather than dropping its work, short enough that a
 * stuck one does not hold up a draw. */
#define BUSY_TIMEOUT_MS 3000

/* How often a connection waiting on a busy file tries again. sqlite's own wait
 * backs off to 100 ms between tries, longer than the gap the prune leaves
 * between its writes (see make_way). */
#define BUSY_RETRY_MS 5

/* Draw counts are batched rather than written one at a time - see note_render.
 * A write that failed is not tried again for the same wait. */
#define RENDER_FLUSH_SECS 30
#define RENDER_FLUSH_MAX  256

#define FORGET_SQL \
	"DELETE FROM thumbnails WHERE file_id = (SELECT file_id FROM paths WHERE uri = ?)"

/* A claim on the prune nobody has touched for this long belongs to a process
 * that died part way through. */
#define CLAIM_STALE_SECS (10 * 60)

/* Rows per prune transaction. Small enough that a window waiting to write
 * never waits long. */
#define PRUNE_BATCH 256

/* A batch also ends after about this many bytes of thumbnails, since deleting
 * one takes longer the bigger it is. Without it a batch of big thumbnails
 * would hold the write lock toward the busy timeout of every other window. */
#define PRUNE_BATCH_BYTES (1024 * 1024)

/* The least the prune rests after each write, so a waiter trying every
 * BUSY_RETRY_MS gets a try in the gap. On Windows a short sleep runs on to the
 * next clock tick, about 16 ms, so this allows for two of those. */
#define PRUNE_REST_MIN_MS 40

/* A thumbnail's age, for both prune rules. Indexed, so picking a batch walks
 * the index rather than sorting the whole table under the write lock. The
 * queries have to spell it the same way for sqlite to use the index. */
#define THUMBNAIL_AGE "MAX (rendered, stored)"

/* The two ways the prune picks thumbnails, shared with the test hook so it
 * asks sqlite about the very queries the prune runs. */
#define OLDEST_SQL \
	"SELECT file_id, LENGTH (image) FROM thumbnails" \
	" ORDER BY " THUMBNAIL_AGE ", file_id LIMIT " G_STRINGIFY (PRUNE_BATCH)
#define OLDER_THAN_SQL \
	"SELECT file_id, LENGTH (image) FROM thumbnails" \
	" WHERE " THUMBNAIL_AGE " < ? LIMIT " G_STRINGIFY (PRUNE_BATCH)

/* How long a name with no thumbnail is kept when thumbnails have no age limit,
 * the same as that setting's default. Without it a cache held down by its size
 * limit alone would fill up with them. */
#define BARE_PATH_AGE_SECS ((gint64) 180 * 24 * 60 * 60)

/* Pages handed back to the disk per step while compacting. */
#define COMPACT_STEP_PAGES 256

/* A folder that takes this long to answer is probably on the network, and is
 * not asked about again in the same pass. */
#define SLOW_FOLDER_USECS G_USEC_PER_SEC

struct _NemoCacheDb {
	sqlite3     *handle;
	GMutex       lock;

	/* Set when sqlite reports the file is damaged. Everything after that is a
	 * no-op, and a marker beside the file tells the next launch to start
	 * over, which is safer than deleting a file other processes still have
	 * open. A damaged page deep in the file does not stop it opening, so
	 * without the marker nothing would ever notice. */
	gboolean     broken;

	/* Prune connection only. */
	gint64       largest_batch;
	gint64       held_since;	/* monotonic, when the current write began */
};

/* A static GMutex needs no setup, and guards the one-time open. Not
 * g_once_init, which latches for good - closing the store has to leave it
 * able to open again. */
static GMutex      the_db_lock;
static NemoCacheDb *the_db = NULL;
static gboolean     the_db_tried = FALSE;

/* Monotonic seconds of the last lookup, store or draw, for the prune's idle
 * test. 0 until something uses the store. */
static gint last_used_secs = 0;

/* The most thumbnail bytes one prune transaction took out, in the last pass
 * this process ran. Read only by the tests, after the pass. */
static gint64 last_largest_batch = 0;

/* Writes made by prune passes in this process. Read only by the tests. */
static gint prune_steps = 0;

/* Writes the window's thread hands off: draw counts and refreshes. They have a
 * lock of their own, never held across a call into sqlite, so the window never
 * waits on another copy's hold on the file. The writer thread writes them out.
 * A refresh still queued is also done by whatever takes the store's lock next
 * to read or write a thumbnail, so none is read back after it was asked for.
 * Lock order is the store's lock, then this one. */
static GMutex      queue_lock;
static GCond       queue_cond;
static GHashTable *queued_draws = NULL;		/* uri -> draws not yet written */
static GHashTable *queued_forgets = NULL;	/* uris whose thumbnail goes */
static gint64      draws_due = 0;		/* monotonic; 0 with none queued */
static gint64      retry_after = 0;		/* monotonic, after a failed write */
static gint64      draw_wait = (gint64) RENDER_FLUSH_SECS * G_USEC_PER_SEC;
static GThread    *writer = NULL;
static gboolean    writer_stop = FALSE;
static gboolean    writer_done = FALSE;		/* the process is quitting */

/* When this thread's current wait on a busy file began. A thread runs one
 * statement at a time, so it waits on one file at a time. */
static GPrivate busy_since = G_PRIVATE_INIT (g_free);

static const char SCHEMA[] =
	"CREATE TABLE IF NOT EXISTS files ("
	"  id     INTEGER PRIMARY KEY,"
	"  bytes  INTEGER NOT NULL,"
	"  digest BLOB);"
	"CREATE UNIQUE INDEX IF NOT EXISTS files_digest ON files (digest)"
	" WHERE digest IS NOT NULL;"
	"CREATE TABLE IF NOT EXISTS paths ("
	"  uri     TEXT PRIMARY KEY,"
	"  file_id INTEGER NOT NULL REFERENCES files (id) ON DELETE CASCADE,"
	"  mtime   INTEGER NOT NULL,"
	"  seen    INTEGER NOT NULL);"
	"CREATE INDEX IF NOT EXISTS paths_file ON paths (file_id);"
	"CREATE INDEX IF NOT EXISTS paths_mtime ON paths (mtime);"
	"CREATE TABLE IF NOT EXISTS thumbnails ("
	"  file_id   INTEGER PRIMARY KEY REFERENCES files (id) ON DELETE CASCADE,"
	"  size      INTEGER NOT NULL,"
	"  width     INTEGER NOT NULL,"
	"  height    INTEGER NOT NULL,"
	"  format    INTEGER NOT NULL,"
	"  stored    INTEGER NOT NULL,"
	"  rendered  INTEGER NOT NULL DEFAULT 0,"
	"  renders   INTEGER NOT NULL DEFAULT 0,"
	"  image     BLOB NOT NULL);"
	"CREATE INDEX IF NOT EXISTS thumbnails_age ON thumbnails (" THUMBNAIL_AGE ");"
	/* The prune's own bookkeeping. Claiming a pass is a write transaction,
	 * and sqlite already makes those take turns across processes, so two
	 * copies cannot both think they won. */
	"CREATE TABLE IF NOT EXISTS prune ("
	"  id           INTEGER PRIMARY KEY CHECK (id = 1),"
	"  started      INTEGER NOT NULL DEFAULT 0,"
	"  completed    INTEGER NOT NULL DEFAULT 0,"
	"  completed_by INTEGER NOT NULL DEFAULT 0,"
	"  removed      INTEGER NOT NULL DEFAULT 0,"
	"  owner        INTEGER NOT NULL DEFAULT 0,"
	"  heartbeat    INTEGER NOT NULL DEFAULT 0,"
	"  due          INTEGER NOT NULL DEFAULT 0);"
	"INSERT OR IGNORE INTO prune (id) VALUES (1);";

static char *
damaged_marker_path (const char *db_path)
{
	return g_strconcat (db_path, ".damaged", NULL);
}

static void
mark_damaged (void)
{
	g_autofree char *path = nemo_cache_db_path ();
	g_autofree char *marker = NULL;

	if (path == NULL)
		return;

	marker = damaged_marker_path (path);
	g_file_set_contents (marker, "", 0, NULL);
}

static void
touch (void)
{
	g_atomic_int_set (&last_used_secs, (gint) (g_get_monotonic_time () / G_USEC_PER_SEC));
}

gint64
nemo_cache_db_last_used (void)
{
	return g_atomic_int_get (&last_used_secs);
}

/* sqlite reports a damaged file several ways depending on where it noticed. */
static gboolean
is_corruption (int rc)
{
	switch (rc & 0xff) {
	case SQLITE_CORRUPT:
	case SQLITE_NOTADB:
	case SQLITE_FORMAT:
		return TRUE;
	default:
		return FALSE;
	}
}

/* Answers whether the call went well, and remembers a damaged file. */
static gboolean
db_ok (NemoCacheDb *db, int rc, const char *what)
{
	if (rc == SQLITE_OK || rc == SQLITE_DONE || rc == SQLITE_ROW)
		return TRUE;

	if (is_corruption (rc)) {
		if (!db->broken) {
			g_warning ("the file cache is damaged and will be rebuilt next run (%s: %s)",
				   what, sqlite3_errstr (rc));
			mark_damaged ();
		}
		db->broken = TRUE;
	} else {
		g_debug ("file cache %s: %s", what, sqlite3_errstr (rc));
	}

	return FALSE;
}

/* Prepares a statement, or answers NULL having noted why. */
static sqlite3_stmt *
prep (NemoCacheDb *db, const char *sql, const char *what)
{
	sqlite3_stmt *stmt = NULL;

	if (!db_ok (db, sqlite3_prepare_v2 (db->handle, sql, -1, &stmt, NULL), what)) {
		sqlite3_finalize (stmt);
		return NULL;
	}

	return stmt;
}

/* Runs a statement that returns one id, and finalizes it either way. 0 for no
 * row. */
static gint64
step_id (sqlite3_stmt *stmt)
{
	gint64 id = 0;

	if (sqlite3_step (stmt) == SQLITE_ROW)
		id = sqlite3_column_int64 (stmt, 0);

	sqlite3_finalize (stmt);

	return id;
}

static void
bind_digest (sqlite3_stmt *stmt, int pos, const NemoFileId *id)
{
	if (id->has_digest)
		sqlite3_bind_blob (stmt, pos, id->digest, NEMO_CACHE_DIGEST_LEN, SQLITE_STATIC);
	else
		sqlite3_bind_null (stmt, pos);
}

static gboolean
begin (NemoCacheDb *db)
{
	return db_ok (db, sqlite3_exec (db->handle, "BEGIN IMMEDIATE", NULL, NULL, NULL), "begin");
}

static void
rollback (NemoCacheDb *db)
{
	sqlite3_exec (db->handle, "ROLLBACK", NULL, NULL, NULL);
}

static gboolean
commit (NemoCacheDb *db)
{
	if (db_ok (db, sqlite3_exec (db->handle, "COMMIT", NULL, NULL, NULL), "commit"))
		return TRUE;

	rollback (db);
	return FALSE;
}

/* Returns: (transfer full): free with g_free */
char *
nemo_cache_db_path (void)
{
	g_autofree char *dir = nemo_get_user_cache_directory ();

	if (dir == NULL)
		return NULL;

	return g_build_filename (dir, "files.db", NULL);
}

/* Answers sqlite's code. A read that failed says nothing about the version,
 * so it must not be taken for one. */
static int
read_user_version (sqlite3 *handle, int *version)
{
	sqlite3_stmt *stmt = NULL;
	int           rc;

	rc = sqlite3_prepare_v2 (handle, "PRAGMA user_version", -1, &stmt, NULL);
	if (rc == SQLITE_OK) {
		rc = sqlite3_step (stmt);
		if (rc == SQLITE_ROW) {
			*version = sqlite3_column_int (stmt, 0);
			rc = SQLITE_OK;
		} else if (rc == SQLITE_DONE) {
			rc = SQLITE_ERROR;
		}
	}

	sqlite3_finalize (stmt);

	return rc;
}

/* sqlite's message names only the kind of failure. The extended code says which
 * step failed, such as a delete or a lock, and the system error says why. */
static void
warn_setup (sqlite3 *handle, const char *what, int rc, const char *err)
{
	g_warning ("%s: %s (sqlite code %d, system error %d)", what,
		   err != NULL ? err : sqlite3_errstr (rc),
		   handle != NULL ? sqlite3_extended_errcode (handle) : rc,
		   handle != NULL ? sqlite3_system_errno (handle) : 0);
}

/* sqlite's busy handler, in place of its own timed one. The wait is timed on
 * the clock rather than by adding up the sleeps, since a sleep of a few
 * milliseconds runs long on Windows and on a loaded box. */
static int
busy_wait (void *data, int tries)
{
	gint64 *since = g_private_get (&busy_since);
	gint64  now = g_get_monotonic_time ();

	(void) data;

	if (since == NULL) {
		since = g_new (gint64, 1);
		g_private_set (&busy_since, since);
	}

	if (tries == 0)
		*since = now;
	else if (now - *since >= (gint64) BUSY_TIMEOUT_MS * 1000)
		return 0;

	g_usleep (BUSY_RETRY_MS * 1000);

	return 1;
}

/* On Windows a copy that quits without closing the store lets go of its locks
 * on the -shm file before Windows unmaps the file from it. The next copy to
 * open then finds no lock, takes itself for the first, and empties the file,
 * which Windows refuses while a view of it is left (ERROR_USER_MAPPED_FILE).
 * sqlite let that pass before 3.50 and now answers SQLITE_IOERR_TRUNCATE. The
 * view goes once the old copy is fully gone, so it is a wait like a busy one. */
static gboolean
setup_worth_retry (sqlite3 *handle, int rc)
{
	if ((rc & 0xff) == SQLITE_BUSY)
		return TRUE;

#ifdef G_OS_WIN32
	return rc != SQLITE_OK && sqlite3_extended_errcode (handle) == SQLITE_IOERR_TRUNCATE;
#else
	(void) handle;
	return FALSE;
#endif
}

/* The switch to WAL reads the file and only then takes the write lock, and
 * sqlite will not wait on that second step, since two readers each waiting on
 * the other would wait for good. So a copy that meets another one setting up
 * the same new file is told it is locked at once, and tries again here for as
 * long as it would wait on any other busy file. */
static int
exec_setup (sqlite3 *handle, const char *sql, char **err)
{
	gint64 give_up = g_get_monotonic_time () + BUSY_TIMEOUT_MS * 1000;
	int    rc;

	for (;;) {
		rc = sqlite3_exec (handle, sql, NULL, NULL, err);
		if (!setup_worth_retry (handle, rc) || g_get_monotonic_time () >= give_up)
			return rc;

		sqlite3_free (*err);
		*err = NULL;
		g_usleep (10 * 1000);
	}
}

/* Opens the file and puts the schema in it. Answers NULL and leaves nothing
 * behind if it could not, so the caller can wipe and try once more. */
static sqlite3 *
open_at (const char *path, gboolean *out_rebuild)
{
	sqlite3 *handle = NULL;
	char    *err = NULL;
	int      version = 0;
	int      rc;

	*out_rebuild = FALSE;

	rc = sqlite3_open_v2 (path, &handle,
			      SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_FULLMUTEX,
			      NULL);
	if (rc != SQLITE_OK) {
		*out_rebuild = is_corruption (rc);
		g_autofree char *what = g_strdup_printf ("could not open the file cache %s", path);

		warn_setup (handle, what, rc, NULL);
		sqlite3_close (handle);
		return NULL;
	}

	sqlite3_busy_handler (handle, busy_wait, NULL);

	/* Incremental vacuum so the prune can hand space back a few pages at a
	 * time, rather than a full VACUUM holding the write lock for as long as
	 * it takes to copy the whole file. It only takes on a file with no tables
	 * yet, which is every file this version opens, since an older one is
	 * wiped.
	 *
	 * WAL so a reading window is not blocked by a writing one, and NORMAL
	 * because losing the last few rows to a power cut costs a re-read and
	 * nothing else. The size limit trims the journal back after a prune has
	 * grown it.
	 *
	 * The read at the end is the first one through the WAL on a new file,
	 * which opens its -shm file, so it gets the same wait (setup_worth_retry). */
	rc = exec_setup (handle,
			 "PRAGMA auto_vacuum = INCREMENTAL;"
			 "PRAGMA journal_mode = WAL;"
			 "PRAGMA journal_size_limit = 67108864;"
			 "PRAGMA synchronous = NORMAL;"
			 "PRAGMA foreign_keys = ON;"
			 "SELECT COUNT (*) FROM sqlite_master;",
			 &err);
	if (rc != SQLITE_OK) {
		*out_rebuild = is_corruption (rc);
		warn_setup (handle, "could not set up the file cache", rc, err);
		sqlite3_free (err);
		sqlite3_close (handle);
		return NULL;
	}

	/* Other copies may have the file open, so one that only could not be
	 * read is left alone, and this copy runs without it. */
	rc = read_user_version (handle, &version);
	if (rc != SQLITE_OK) {
		*out_rebuild = is_corruption (rc);
		warn_setup (handle, "could not read the file cache version", rc, NULL);
		sqlite3_close (handle);
		return NULL;
	}

	/* 0 is a file nobody has written tables into yet. Anything else that is
	 * not ours was written by another version, and gets the same treatment as
	 * a damaged file. */
	if (version != 0 && version != SCHEMA_VERSION) {
		*out_rebuild = TRUE;
		sqlite3_close (handle);
		return NULL;
	}

	rc = sqlite3_exec (handle, SCHEMA, NULL, NULL, &err);
	if (rc != SQLITE_OK) {
		*out_rebuild = is_corruption (rc);
		warn_setup (handle, "could not make the file cache tables", rc, err);
		sqlite3_free (err);
		sqlite3_close (handle);
		return NULL;
	}

	sqlite3_exec (handle, "PRAGMA user_version = " G_STRINGIFY (SCHEMA_VERSION) ";",
		      NULL, NULL, NULL);

	return handle;
}

/* WAL keeps two files beside the database, and a wipe that leaves them behind
 * hands the new file someone else's journal. */
static void
remove_db_files (const char *path)
{
	g_autofree char *wal = g_strconcat (path, "-wal", NULL);
	g_autofree char *shm = g_strconcat (path, "-shm", NULL);

	g_unlink (path);
	g_unlink (wal);
	g_unlink (shm);
}

static NemoCacheDb *
db_open (void)
{
	NemoCacheDb     *db;
	sqlite3         *handle;
	gboolean         rebuild = FALSE;
	g_autofree char *path = NULL;
	g_autofree char *marker = NULL;

	/* A build of sqlite with threading compiled out would corrupt the file the
	 * first time a worker wrote while the main loop read. Nothing we ship is
	 * built that way, so say so rather than guard every call. */
	if (sqlite3_threadsafe () == 0) {
		g_warning ("sqlite was built without thread support, so the file cache is off");
		return NULL;
	}

	path = nemo_cache_db_path ();
	if (path == NULL)
		return NULL;

	marker = damaged_marker_path (path);
	if (g_file_test (marker, G_FILE_TEST_EXISTS)) {
		g_message ("starting the damaged file cache at %s over", path);
		remove_db_files (path);

		/* On Windows a file another copy still has open cannot be
		 * removed, so the marker stays for the next launch to try. */
		if (!g_file_test (path, G_FILE_TEST_EXISTS))
			g_unlink (marker);
	}

	handle = open_at (path, &rebuild);

	if (handle == NULL && rebuild) {
		g_message ("starting the file cache at %s over", path);
		remove_db_files (path);
		handle = open_at (path, &rebuild);
	}

	if (handle == NULL)
		return NULL;

	db = g_new0 (NemoCacheDb, 1);
	db->handle = handle;
	g_mutex_init (&db->lock);

	return db;
}

/* Returns: (transfer none): the one store for the process, or NULL */
NemoCacheDb *
nemo_cache_db_get (void)
{
	NemoCacheDb *db;

	g_mutex_lock (&the_db_lock);

	/* One attempt per open store. A database that could not be opened stays
	 * shut rather than being retried on every draw. */
	if (!the_db_tried) {
		the_db = db_open ();
		the_db_tried = TRUE;
	}

	db = the_db;

	g_mutex_unlock (&the_db_lock);

	return db;
}

/* The only one of the three that is really about the contents. The checksum
 * alone, the same as the unique index: a record that paired it with another
 * size would otherwise never be found, and nothing could be added beside it. */
static gint64
file_by_digest (NemoCacheDb *db, const NemoFileId *id)
{
	static const char sql[] = "SELECT id FROM files WHERE digest = ?";
	sqlite3_stmt *stmt;

	if (!id->has_digest)
		return 0;

	stmt = prep (db, sql, "find by checksum");
	if (stmt == NULL)
		return 0;

	sqlite3_bind_blob (stmt, 1, id->digest, NEMO_CACHE_DIGEST_LEN, SQLITE_STATIC);

	return step_id (stmt);
}

/* What this uri held last time, as long as the file under it has not changed.
 * A row carrying a checksum that contradicts the one in hand is not it either,
 * whatever the size and time say. */
static gint64
file_by_uri (NemoCacheDb *db, const char *uri, const NemoFileId *id)
{
	static const char sql[] =
		"SELECT c.id FROM paths p JOIN files c ON c.id = p.file_id"
		" WHERE p.uri = ? AND p.mtime = ? AND c.bytes = ?"
		" AND (? IS NULL OR c.digest IS NULL OR c.digest = ?)";
	sqlite3_stmt *stmt = prep (db, sql, "find by path");

	if (stmt == NULL)
		return 0;

	sqlite3_bind_text (stmt, 1, uri, -1, SQLITE_STATIC);
	sqlite3_bind_int64 (stmt, 2, id->mtime);
	sqlite3_bind_int64 (stmt, 3, id->bytes);
	bind_digest (stmt, 4, id);
	bind_digest (stmt, 5, id);

	return step_id (stmt);
}

/* Some other name with the same size and time. It is a guess, and it is the
 * only one available when nothing has read the file. With a checksum in hand,
 * only a record that has none may be adopted this way - one that has a checksum
 * and matched would already have been found. */
static gint64
file_by_stat (NemoCacheDb *db, const NemoFileId *id)
{
	static const char any[] =
		"SELECT p.file_id FROM paths p JOIN files c ON c.id = p.file_id"
		" WHERE p.mtime = ? AND c.bytes = ? LIMIT 1";
	static const char undigested[] =
		"SELECT p.file_id FROM paths p JOIN files c ON c.id = p.file_id"
		" WHERE p.mtime = ? AND c.bytes = ? AND c.digest IS NULL LIMIT 1";
	sqlite3_stmt *stmt = prep (db, id->has_digest ? undigested : any, "find by size and time");

	if (stmt == NULL)
		return 0;

	sqlite3_bind_int64 (stmt, 1, id->mtime);
	sqlite3_bind_int64 (stmt, 2, id->bytes);

	return step_id (stmt);
}

static gint64
file_add (NemoCacheDb *db, const NemoFileId *id)
{
	static const char sql[] = "INSERT INTO files (bytes, digest) VALUES (?, ?)";
	sqlite3_stmt *stmt = prep (db, sql, "add file");
	gboolean      ok;

	if (stmt == NULL)
		return 0;

	sqlite3_bind_int64 (stmt, 1, id->bytes);
	bind_digest (stmt, 2, id);

	ok = db_ok (db, sqlite3_step (stmt), "add file");
	sqlite3_finalize (stmt);

	return ok ? sqlite3_last_insert_rowid (db->handle) : 0;
}

/* Fills in a checksum nobody had computed yet. Never replaces one, since two
 * different checksums mean two different files. */
static gboolean
file_learn_digest (NemoCacheDb *db, gint64 file_id, const NemoFileId *id)
{
	static const char sql[] = "UPDATE files SET digest = ? WHERE id = ? AND digest IS NULL";
	sqlite3_stmt *stmt;
	gboolean      ok;

	if (!id->has_digest)
		return TRUE;

	stmt = prep (db, sql, "set checksum");
	if (stmt == NULL)
		return FALSE;

	sqlite3_bind_blob (stmt, 1, id->digest, NEMO_CACHE_DIGEST_LEN, SQLITE_STATIC);
	sqlite3_bind_int64 (stmt, 2, file_id);

	ok = db_ok (db, sqlite3_step (stmt), "set checksum");
	sqlite3_finalize (stmt);

	return ok;
}

/* One checksum is one set of contents, so one size. A record found by its
 * checksum at another size took the size from before an edit, and the size in
 * hand is the newer. Left alone, lookups that go by size would keep missing. */
static gboolean
file_set_bytes (NemoCacheDb *db, gint64 file_id, const NemoFileId *id)
{
	static const char sql[] = "UPDATE files SET bytes = ? WHERE id = ? AND bytes <> ?";
	sqlite3_stmt *stmt = prep (db, sql, "set size");
	gboolean      ok;

	if (stmt == NULL)
		return FALSE;

	sqlite3_bind_int64 (stmt, 1, id->bytes);
	sqlite3_bind_int64 (stmt, 2, file_id);
	sqlite3_bind_int64 (stmt, 3, id->bytes);

	ok = db_ok (db, sqlite3_step (stmt), "set size");
	sqlite3_finalize (stmt);

	return ok;
}

/* The record for `id`, made if there is none. */
static gint64
file_for (NemoCacheDb *db, const char *uri, const NemoFileId *id)
{
	gint64 fid = file_by_digest (db, id);

	if (fid != 0)
		return file_set_bytes (db, fid, id) ? fid : 0;

	fid = file_by_uri (db, uri, id);
	if (fid == 0)
		fid = file_by_stat (db, id);

	if (fid == 0)
		return file_add (db, id);

	file_learn_digest (db, fid, id);

	return fid;
}

/* Points a uri at a record, so the next lookup goes straight there. */
static gboolean
link_path (NemoCacheDb *db, const char *uri, gint64 file_id, gint64 mtime)
{
	static const char sql[] =
		"INSERT INTO paths (uri, file_id, mtime, seen) VALUES (?, ?, ?, ?)"
		" ON CONFLICT (uri) DO UPDATE SET file_id = excluded.file_id,"
		" mtime = excluded.mtime, seen = excluded.seen";
	sqlite3_stmt *stmt = prep (db, sql, "link");
	gboolean      ok;

	if (stmt == NULL)
		return FALSE;

	sqlite3_bind_text (stmt, 1, uri, -1, SQLITE_STATIC);
	sqlite3_bind_int64 (stmt, 2, file_id);
	sqlite3_bind_int64 (stmt, 3, mtime);
	sqlite3_bind_int64 (stmt, 4, g_get_real_time () / G_USEC_PER_SEC);

	ok = db_ok (db, sqlite3_step (stmt), "link");
	sqlite3_finalize (stmt);

	return ok;
}

/* Two records turned out to be one file. Everything hanging off `from` moves to
 * `to`, and `from` goes. The survivor keeps its own thumbnail if it has one. */
static gboolean
fold_file (NemoCacheDb *db, gint64 from, gint64 to)
{
	static const char take_thumb[] =
		"INSERT OR IGNORE INTO thumbnails"
		" (file_id, size, width, height, format, stored, rendered, renders, image)"
		" SELECT ?, size, width, height, format, stored, rendered, renders, image"
		" FROM thumbnails WHERE file_id = ?";
	static const char move_paths[] = "UPDATE paths SET file_id = ? WHERE file_id = ?";
	static const char drop_old[] = "DELETE FROM files WHERE id = ?";
	sqlite3_stmt *stmt;
	gboolean      ok = TRUE;

	stmt = prep (db, take_thumb, "fold");
	if (stmt == NULL)
		return FALSE;
	sqlite3_bind_int64 (stmt, 1, to);
	sqlite3_bind_int64 (stmt, 2, from);
	ok = db_ok (db, sqlite3_step (stmt), "fold");
	sqlite3_finalize (stmt);
	if (!ok)
		return FALSE;

	stmt = prep (db, move_paths, "fold");
	if (stmt == NULL)
		return FALSE;
	sqlite3_bind_int64 (stmt, 1, to);
	sqlite3_bind_int64 (stmt, 2, from);
	ok = db_ok (db, sqlite3_step (stmt), "fold");
	sqlite3_finalize (stmt);
	if (!ok)
		return FALSE;

	stmt = prep (db, drop_old, "fold");
	if (stmt == NULL)
		return FALSE;
	sqlite3_bind_int64 (stmt, 1, from);
	ok = db_ok (db, sqlite3_step (stmt), "fold");
	sqlite3_finalize (stmt);

	return ok;
}

gboolean
nemo_cache_db_note_file (NemoCacheDb *db, const char *uri, const NemoFileId *id)
{
	gint64   fid;
	gboolean done = FALSE;

	g_return_val_if_fail (uri != NULL, FALSE);
	g_return_val_if_fail (id != NULL, FALSE);

	if (db == NULL || db->broken)
		return FALSE;

	g_mutex_lock (&db->lock);

	if (begin (db)) {
		fid = file_for (db, uri, id);
		if (fid != 0 && link_path (db, uri, fid, id->mtime))
			done = commit (db);
		else
			rollback (db);
	}

	g_mutex_unlock (&db->lock);

	return done;
}

gboolean
nemo_cache_db_lookup_digest (NemoCacheDb *db,
			     const char  *uri,
			     gint64       bytes,
			     gint64       mtime,
			     guint8      *digest)
{
	static const char sql[] =
		"SELECT c.digest FROM paths p JOIN files c ON c.id = p.file_id"
		" WHERE p.uri = ? AND p.mtime = ? AND c.bytes = ? AND c.digest IS NOT NULL";
	sqlite3_stmt *stmt;
	gboolean      found = FALSE;

	g_return_val_if_fail (uri != NULL, FALSE);
	g_return_val_if_fail (digest != NULL, FALSE);

	if (db == NULL || db->broken)
		return FALSE;

	g_mutex_lock (&db->lock);

	stmt = prep (db, sql, "read checksum");
	if (stmt != NULL) {
		sqlite3_bind_text (stmt, 1, uri, -1, SQLITE_STATIC);
		sqlite3_bind_int64 (stmt, 2, mtime);
		sqlite3_bind_int64 (stmt, 3, bytes);

		if (sqlite3_step (stmt) == SQLITE_ROW) {
			const void *blob = sqlite3_column_blob (stmt, 0);
			int         len = sqlite3_column_bytes (stmt, 0);

			if (blob != NULL && len == NEMO_CACHE_DIGEST_LEN) {
				memcpy (digest, blob, NEMO_CACHE_DIGEST_LEN);
				found = TRUE;
			}
		}

		sqlite3_finalize (stmt);
	}

	g_mutex_unlock (&db->lock);

	return found;
}

gboolean
nemo_cache_db_set_digest (NemoCacheDb *db, const char *uri, const NemoFileId *id)
{
	gint64   target, mine;
	gboolean done = FALSE;

	g_return_val_if_fail (uri != NULL, FALSE);
	g_return_val_if_fail (id != NULL, FALSE);
	g_return_val_if_fail (id->has_digest, FALSE);

	if (db == NULL || db->broken)
		return FALSE;

	g_mutex_lock (&db->lock);

	if (!begin (db))
		goto out;

	target = file_by_digest (db, id);
	mine   = file_by_uri (db, uri, id);

	if (target != 0 && !file_set_bytes (db, target, id))
		goto fail;

	if (mine == 0) {
		/* Nothing known about this path yet, or what was known is stale. */
		if (target == 0)
			target = file_add (db, id);
		done = (target != 0) && link_path (db, uri, target, id->mtime);
	} else if (target == 0 || target == mine) {
		done = file_learn_digest (db, mine, id);
	} else {
		done = fold_file (db, mine, target)
			&& link_path (db, uri, target, id->mtime);
	}

	if (done) {
		done = commit (db);
		goto out;
	}

fail:
	rollback (db);

out:
	g_mutex_unlock (&db->lock);

	return done;
}

/* Fills `record` and, when asked, the image bytes. Called with the lock held. */
static gboolean
read_thumbnail (NemoCacheDb         *db,
		gint64               file_id,
		NemoThumbnailRecord *record,
		GBytes             **image)
{
	static const char sql[] =
		"SELECT size, width, height, format, stored, image FROM thumbnails"
		" WHERE file_id = ?";
	sqlite3_stmt *stmt = prep (db, sql, "read thumbnail");
	gboolean      found = FALSE;

	if (stmt == NULL)
		return FALSE;

	sqlite3_bind_int64 (stmt, 1, file_id);

	if (sqlite3_step (stmt) == SQLITE_ROW) {
		record->size   = sqlite3_column_int (stmt, 0);
		record->width  = sqlite3_column_int (stmt, 1);
		record->height = sqlite3_column_int (stmt, 2);
		record->format = (NemoThumbnailFormat) sqlite3_column_int (stmt, 3);
		record->stored = sqlite3_column_int64 (stmt, 4);

		if (image != NULL) {
			const void *blob = sqlite3_column_blob (stmt, 5);
			int         len = sqlite3_column_bytes (stmt, 5);

			*image = (blob != NULL && len > 0)
				? g_bytes_new (blob, (gsize) len)
				: NULL;
		}

		found = TRUE;
	}

	sqlite3_finalize (stmt);

	return found;
}

/* Every drawn icon in a folder would otherwise be a write, and scrolling a big
 * folder draws the same file over and over. Counts are held in memory and
 * written in one transaction, which is also all the age rule needs - it works
 * in days. Called with the store's lock held. */
static gboolean
write_draws (NemoCacheDb *db, GHashTable *draws)
{
	static const char sql[] =
		"UPDATE thumbnails SET rendered = ?, renders = renders + ?"
		" WHERE file_id = (SELECT file_id FROM paths WHERE uri = ?)";
	sqlite3_stmt  *stmt;
	GHashTableIter iter;
	gpointer       uri, count;
	gint64         now = g_get_real_time () / G_USEC_PER_SEC;

	if (!begin (db))
		return FALSE;

	stmt = prep (db, sql, "draw count");
	if (stmt == NULL) {
		rollback (db);
		return FALSE;
	}

	g_hash_table_iter_init (&iter, draws);
	while (g_hash_table_iter_next (&iter, &uri, &count)) {
		sqlite3_reset (stmt);
		sqlite3_bind_int64 (stmt, 1, now);
		sqlite3_bind_int64 (stmt, 2, (gint64) GPOINTER_TO_INT (count));
		sqlite3_bind_text (stmt, 3, (const char *) uri, -1, SQLITE_STATIC);
		db_ok (db, sqlite3_step (stmt), "draw count");
	}

	sqlite3_finalize (stmt);

	return commit (db);
}

/* Called with the store's lock held. */
static gboolean
write_forgets (NemoCacheDb *db, GHashTable *uris)
{
	sqlite3_stmt  *stmt;
	GHashTableIter iter;
	gpointer       uri;
	gboolean       ok = TRUE;

	if (!begin (db))
		return FALSE;

	stmt = prep (db, FORGET_SQL, "forget thumbnail");
	if (stmt == NULL) {
		rollback (db);
		return FALSE;
	}

	g_hash_table_iter_init (&iter, uris);
	while (ok && g_hash_table_iter_next (&iter, &uri, NULL)) {
		sqlite3_reset (stmt);
		sqlite3_bind_text (stmt, 1, (const char *) uri, -1, SQLITE_STATIC);
		ok = db_ok (db, sqlite3_step (stmt), "forget thumbnail");
	}

	sqlite3_finalize (stmt);

	if (!ok) {
		rollback (db);
		return FALSE;
	}

	return commit (db);
}

/* With queue_lock held. */
static void
queue_init_locked (void)
{
	if (queued_draws != NULL)
		return;

	queued_draws = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);
	queued_forgets = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);
}

/* With queue_lock held. Hands back the table and leaves an empty one. */
static GHashTable *
steal_locked (GHashTable **table)
{
	GHashTable *taken = *table;

	*table = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);

	return taken;
}

/* Writes what the window has queued: refreshes, and draw counts too when
 * `with_draws`. Called with the store's lock held, which is what keeps a
 * refresh in order with the reads: whoever holds it either finds the refresh
 * still queued or already done. What could not be written goes back in the
 * queue for later, unless the file is damaged. */
static void
write_queued (NemoCacheDb *db, gboolean with_draws)
{
	g_autoptr (GHashTable) forgets = NULL;
	g_autoptr (GHashTable) draws = NULL;
	gboolean forgot, drew;
	gint64   now;

	g_mutex_lock (&queue_lock);
	queue_init_locked ();
	if (g_hash_table_size (queued_forgets) > 0)
		forgets = steal_locked (&queued_forgets);
	if (with_draws && g_hash_table_size (queued_draws) > 0) {
		draws = steal_locked (&queued_draws);
		draws_due = 0;
	}
	g_mutex_unlock (&queue_lock);

	if (forgets == NULL && draws == NULL)
		return;

	forgot = forgets == NULL || (!db->broken && write_forgets (db, forgets));
	drew = draws == NULL || (!db->broken && write_draws (db, draws));

	if ((forgot && drew) || db->broken)
		return;

	now = g_get_monotonic_time ();

	g_mutex_lock (&queue_lock);

	if (!forgot) {
		GHashTableIter iter;
		gpointer       uri;

		g_hash_table_iter_init (&iter, forgets);
		while (g_hash_table_iter_next (&iter, &uri, NULL)) {
			g_hash_table_iter_steal (&iter);
			g_hash_table_add (queued_forgets, uri);
		}
	}

	if (!drew) {
		GHashTableIter iter;
		gpointer       uri, count;

		g_hash_table_iter_init (&iter, draws);
		while (g_hash_table_iter_next (&iter, &uri, &count)) {
			gpointer more = g_hash_table_lookup (queued_draws, uri);

			g_hash_table_iter_steal (&iter);
			g_hash_table_replace (queued_draws, uri,
					      GINT_TO_POINTER (GPOINTER_TO_INT (count) + GPOINTER_TO_INT (more)));
		}

		if (draws_due == 0)
			draws_due = now + draw_wait;
	}

	/* Not on every draw: a file held by another copy would be asked again
	 * and again. */
	retry_after = now + draw_wait;

	g_mutex_unlock (&queue_lock);
}

static gboolean
forget_queued (const char *uri)
{
	gboolean queued;

	g_mutex_lock (&queue_lock);
	queued = queued_forgets != NULL && g_hash_table_contains (queued_forgets, uri);
	g_mutex_unlock (&queue_lock);

	return queued;
}

static void
drop_queued (void)
{
	g_mutex_lock (&queue_lock);
	if (queued_draws != NULL) {
		g_hash_table_remove_all (queued_draws);
		g_hash_table_remove_all (queued_forgets);
	}
	draws_due = 0;
	g_mutex_unlock (&queue_lock);
}

/* With queue_lock held. When the writer next has something to do, monotonic,
 * or G_MAXINT64 for nothing queued. */
static gint64
next_write_locked (void)
{
	gint64 at;

	if (g_hash_table_size (queued_forgets) > 0 ||
	    g_hash_table_size (queued_draws) >= RENDER_FLUSH_MAX)
		at = 0;
	else if (draws_due != 0)
		at = draws_due;
	else
		return G_MAXINT64;

	return MAX (at, retry_after);
}

static gpointer
writer_run (gpointer data)
{
	(void) data;

	g_mutex_lock (&queue_lock);

	while (!writer_stop) {
		gint64       at = next_write_locked ();
		NemoCacheDb *db;

		if (at == G_MAXINT64) {
			g_cond_wait (&queue_cond, &queue_lock);
			continue;
		}

		if (g_get_monotonic_time () < at) {
			g_cond_wait_until (&queue_cond, &queue_lock, at);
			continue;
		}

		g_mutex_unlock (&queue_lock);

		/* Opens the store if a refresh came before anything else used it. */
		db = nemo_cache_db_get ();
		if (db != NULL && !db->broken) {
			g_mutex_lock (&db->lock);
			write_queued (db, TRUE);
			g_mutex_unlock (&db->lock);
		} else {
			drop_queued ();
		}

		g_mutex_lock (&queue_lock);
	}

	g_mutex_unlock (&queue_lock);

	return NULL;
}

/* With queue_lock held. The writer starts on the first thing queued. */
static void
wake_writer_locked (void)
{
	if (writer == NULL && !writer_stop && !writer_done)
		writer = g_thread_new ("cache-writer", writer_run, NULL);

	g_cond_signal (&queue_cond);
}

/* Lets the writer finish what it is writing and waits for it. What is still
 * queued stays queued. After `for_good` no writer starts again, since the
 * process is on its way out. */
static void
stop_writer (gboolean for_good)
{
	GThread *thread;

	g_mutex_lock (&queue_lock);
	thread = writer;
	writer = NULL;
	writer_stop = TRUE;
	writer_done = writer_done || for_good;
	g_cond_broadcast (&queue_cond);
	g_mutex_unlock (&queue_lock);

	if (thread != NULL)
		g_thread_join (thread);

	g_mutex_lock (&queue_lock);
	writer_stop = FALSE;
	g_mutex_unlock (&queue_lock);
}

void
nemo_cache_db_note_render (NemoCacheDb *db, const char *uri)
{
	gpointer old;
	guint    queued;

	g_return_if_fail (uri != NULL);

	if (db == NULL || db->broken)
		return;

	touch ();

	g_mutex_lock (&queue_lock);

	queue_init_locked ();

	old = g_hash_table_lookup (queued_draws, uri);
	g_hash_table_replace (queued_draws, g_strdup (uri),
			      GINT_TO_POINTER (GPOINTER_TO_INT (old) + 1));
	queued = g_hash_table_size (queued_draws);

	if (draws_due == 0) {
		draws_due = g_get_monotonic_time () + draw_wait;
		wake_writer_locked ();
	} else if (queued == RENDER_FLUSH_MAX) {
		wake_writer_locked ();
	}

	g_mutex_unlock (&queue_lock);
}

void
nemo_cache_db_thumbnail_forget_later (const char *uri)
{
	g_return_if_fail (uri != NULL);

	g_mutex_lock (&queue_lock);
	queue_init_locked ();
	/* cppcheck-suppress leakNoVarFunctionCall ; the queue owns its keys and frees them */
	g_hash_table_add (queued_forgets, g_strdup (uri));
	wake_writer_locked ();
	g_mutex_unlock (&queue_lock);
}

void
nemo_cache_db_flush (NemoCacheDb *db)
{
	if (db == NULL || db->broken)
		return;

	g_mutex_lock (&db->lock);
	write_queued (db, TRUE);
	g_mutex_unlock (&db->lock);
}

void
nemo_cache_db_set_draw_wait (gint ms)
{
	g_mutex_lock (&queue_lock);
	draw_wait = (gint64) ms * 1000;
	g_mutex_unlock (&queue_lock);
}

gboolean
nemo_cache_db_thumbnail_lookup (NemoCacheDb         *db,
				const char          *uri,
				const NemoFileId    *id,
				NemoThumbnailRecord *record,
				GBytes             **image)
{
	gint64   fid;
	gboolean known_path;
	gboolean found = FALSE;

	g_return_val_if_fail (uri != NULL, FALSE);
	g_return_val_if_fail (id != NULL, FALSE);
	g_return_val_if_fail (record != NULL, FALSE);

	if (image != NULL)
		*image = NULL;

	if (db == NULL || db->broken)
		return FALSE;

	touch ();

	g_mutex_lock (&db->lock);

	/* A refresh asked for and not written yet still counts, even if it
	 * could not be written now. */
	write_queued (db, FALSE);
	if (forget_queued (uri))
		goto out;

	fid = file_by_digest (db, id);
	known_path = FALSE;
	if (fid == 0) {
		fid = file_by_uri (db, uri, id);
		known_path = (fid != 0);
	}
	if (fid == 0)
		fid = file_by_stat (db, id);

	if (fid != 0) {
		found = read_thumbnail (db, fid, record, image);

		/* A hit under another name, so point this one at it too and the
		 * next lookup is direct. */
		if (found && !known_path)
			link_path (db, uri, fid, id->mtime);
	}

out:
	g_mutex_unlock (&db->lock);

	return found;
}

gboolean
nemo_cache_db_thumbnail_store (NemoCacheDb               *db,
			       const char                *uri,
			       const NemoFileId          *id,
			       const NemoThumbnailRecord *record,
			       GBytes                    *image)
{
	/* An earlier image for the same contents is overwritten in place rather
	 * than replaced, so its draw history - which is all the pruning rules
	 * have to go on - survives a re-render at a bigger size. */
	static const char sql[] =
		"INSERT INTO thumbnails"
		" (file_id, size, width, height, format, stored, rendered, renders, image)"
		" VALUES (?, ?, ?, ?, ?, ?, 0, 0, ?)"
		" ON CONFLICT (file_id) DO UPDATE SET size = excluded.size,"
		" width = excluded.width, height = excluded.height, format = excluded.format,"
		" stored = excluded.stored, image = excluded.image";
	sqlite3_stmt *stmt;
	gconstpointer data;
	gsize         len = 0;
	gint64        fid;
	gint64        now;
	gboolean      done = FALSE;

	g_return_val_if_fail (uri != NULL, FALSE);
	g_return_val_if_fail (id != NULL, FALSE);
	g_return_val_if_fail (record != NULL, FALSE);

	if (db == NULL || db->broken)
		return FALSE;

	/* No image is a render that failed, kept so the next run does not try
	 * again. An empty blob rather than NULL, since the column is NOT NULL. */
	if (image != NULL) {
		data = g_bytes_get_data (image, &len);
		if (data == NULL || len == 0)
			return FALSE;
	} else {
		data = "";
		len = 0;
	}

	now = g_get_real_time () / G_USEC_PER_SEC;

	touch ();

	g_mutex_lock (&db->lock);

	/* A queued refresh done after this write would throw it away. */
	write_queued (db, FALSE);

	if (!begin (db))
		goto out;

	fid = file_for (db, uri, id);
	if (fid == 0)
		goto fail;

	stmt = prep (db, sql, "store thumbnail");
	if (stmt == NULL)
		goto fail;

	sqlite3_bind_int64 (stmt, 1, fid);
	sqlite3_bind_int (stmt, 2, record->size);
	sqlite3_bind_int (stmt, 3, image != NULL ? record->width : 0);
	sqlite3_bind_int (stmt, 4, image != NULL ? record->height : 0);
	sqlite3_bind_int (stmt, 5, (int) record->format);
	sqlite3_bind_int64 (stmt, 6, record->stored > 0 ? record->stored : now);
	sqlite3_bind_blob64 (stmt, 7, data, len, SQLITE_STATIC);

	done = db_ok (db, sqlite3_step (stmt), "store thumbnail");
	sqlite3_finalize (stmt);

	if (done)
		done = link_path (db, uri, fid, id->mtime);

	if (done)
		done = commit (db);
	else
		rollback (db);

	/* Written over a refresh that is still queued, so that one is done. */
	if (done) {
		g_mutex_lock (&queue_lock);
		if (queued_forgets != NULL)
			g_hash_table_remove (queued_forgets, uri);
		g_mutex_unlock (&queue_lock);
	}

	goto out;

fail:
	rollback (db);

out:
	g_mutex_unlock (&db->lock);

	return done;
}

gboolean
nemo_cache_db_thumbnail_forget (NemoCacheDb *db, const char *uri)
{
	sqlite3_stmt *stmt;
	gboolean      done = FALSE;

	g_return_val_if_fail (uri != NULL, FALSE);

	if (db == NULL || db->broken)
		return FALSE;

	g_mutex_lock (&db->lock);

	stmt = prep (db, FORGET_SQL, "forget thumbnail");
	if (stmt != NULL) {
		sqlite3_bind_text (stmt, 1, uri, -1, SQLITE_STATIC);
		done = db_ok (db, sqlite3_step (stmt), "forget thumbnail");
		sqlite3_finalize (stmt);
	}

	g_mutex_unlock (&db->lock);

	return done;
}

gboolean
nemo_cache_db_thumbnail_stats (NemoCacheDb *db,
			       const char  *uri,
			       gint64      *renders,
			       gint64      *rendered)
{
	static const char sql[] =
		"SELECT t.renders, t.rendered FROM paths p"
		" JOIN thumbnails t ON t.file_id = p.file_id WHERE p.uri = ?";
	sqlite3_stmt *stmt;
	gboolean      found = FALSE;

	g_return_val_if_fail (uri != NULL, FALSE);

	if (renders != NULL)
		*renders = 0;
	if (rendered != NULL)
		*rendered = 0;

	if (db == NULL || db->broken)
		return FALSE;

	g_mutex_lock (&db->lock);

	stmt = prep (db, sql, "draw stats");
	if (stmt != NULL) {
		sqlite3_bind_text (stmt, 1, uri, -1, SQLITE_STATIC);

		if (sqlite3_step (stmt) == SQLITE_ROW) {
			if (renders != NULL)
				*renders = sqlite3_column_int64 (stmt, 0);
			if (rendered != NULL)
				*rendered = sqlite3_column_int64 (stmt, 1);
			found = TRUE;
		}

		sqlite3_finalize (stmt);
	}

	g_mutex_unlock (&db->lock);

	return found;
}

gboolean
nemo_cache_db_empty (NemoCacheDb *db)
{
	char    *err = NULL;
	gboolean done;

	if (db == NULL || db->broken)
		return FALSE;

	g_mutex_lock (&db->lock);

	drop_queued ();

	done = db_ok (db, sqlite3_exec (db->handle,
					"DELETE FROM thumbnails;"
					"DELETE FROM paths;"
					"DELETE FROM files;",
					NULL, NULL, &err), "empty");
	sqlite3_free (err);

	/* Dropping the rows leaves the file its old size, and somebody who just
	 * asked for the cache to be emptied means the disk space too. In WAL mode
	 * the VACUUM writes the new file into the journal, which then holds more
	 * than the old file did, so it is folded back in and cut short. Another
	 * copy reading at the time can stop the cut, and that is left alone. */
	if (done) {
		sqlite3_exec (db->handle, "VACUUM", NULL, NULL, NULL);
		sqlite3_exec (db->handle, "PRAGMA wal_checkpoint (TRUNCATE)", NULL, NULL, NULL);
	}

	g_mutex_unlock (&db->lock);

	return done;
}

void
nemo_cache_db_usage (NemoCacheDb *db, gint64 *n_thumbnails, gint64 *bytes)
{
	static const char sql[] =
		"SELECT COUNT(*), COALESCE (SUM (LENGTH (image)), 0) FROM thumbnails";
	sqlite3_stmt *stmt;

	if (n_thumbnails != NULL)
		*n_thumbnails = 0;
	if (bytes != NULL)
		*bytes = 0;

	if (db == NULL || db->broken)
		return;

	g_mutex_lock (&db->lock);

	stmt = prep (db, sql, "usage");
	if (stmt != NULL) {
		if (sqlite3_step (stmt) == SQLITE_ROW) {
			if (n_thumbnails != NULL)
				*n_thumbnails = sqlite3_column_int64 (stmt, 0);
			if (bytes != NULL)
				*bytes = sqlite3_column_int64 (stmt, 1);
		}
		sqlite3_finalize (stmt);
	}

	g_mutex_unlock (&db->lock);
}

/* Called with the lock held. */
static void
write_out (NemoCacheDb *db)
{
	if (!db->broken)
		write_queued (db, TRUE);

	/* Folds the WAL back in, so the next launch opens one file rather than
	 * replaying a journal that could be most of the cache. */
	sqlite3_exec (db->handle, "PRAGMA wal_checkpoint (TRUNCATE)", NULL, NULL, NULL);
}

void
nemo_cache_db_quit (void)
{
	NemoCacheDb *db;
	gboolean     forgets;

	stop_writer (TRUE);

	g_mutex_lock (&queue_lock);
	forgets = queued_forgets != NULL && g_hash_table_size (queued_forgets) > 0;
	g_mutex_unlock (&queue_lock);

	/* Not get() unless a refresh is waiting, which would make a store for a
	 * run that never used one. */
	if (forgets) {
		db = nemo_cache_db_get ();
	} else {
		g_mutex_lock (&the_db_lock);
		db = the_db;
		g_mutex_unlock (&the_db_lock);
	}

	if (db == NULL)
		return;

	g_mutex_lock (&db->lock);
	write_out (db);
	g_mutex_unlock (&db->lock);
}

void
nemo_cache_db_close (void)
{
	NemoCacheDb *db;

	/* It writes through the store this frees. */
	stop_writer (FALSE);

	g_mutex_lock (&the_db_lock);
	db = the_db;
	the_db = NULL;
	the_db_tried = FALSE;
	g_mutex_unlock (&the_db_lock);

	if (db == NULL)
		return;

	g_mutex_lock (&db->lock);

	write_out (db);
	sqlite3_close (db->handle);
	db->handle = NULL;

	g_mutex_unlock (&db->lock);
	g_mutex_clear (&db->lock);

	g_free (db);
}

static gint64
wall_secs (void)
{
	return g_get_real_time () / G_USEC_PER_SEC;
}

/* sqlite calls this every so often during a long statement. Non-zero stops it
 * with SQLITE_INTERRUPT, which is how a quit reaches a pass that is part way
 * through checking a big file. */
static int
prune_interrupted (void *data)
{
	return g_cancellable_is_cancelled (G_CANCELLABLE (data));
}

typedef enum {
	CLAIM_WON,
	CLAIM_NOT_DUE,
	CLAIM_BUSY,
	CLAIM_ERROR
} ClaimResult;

static ClaimResult
claim_pass (NemoCacheDb *db, const NemoCachePruneRules *rules, gint64 *due)
{
	static const char read_sql[] = "SELECT owner, heartbeat, due FROM prune WHERE id = 1";
	static const char take_sql[] =
		"UPDATE prune SET owner = ?, heartbeat = ?, started = ?, completed = 0 WHERE id = 1";
	sqlite3_stmt *stmt;
	gint64        owner = 0, heartbeat = 0;
	gint64        me = (gint64) getpid ();
	gboolean      found = FALSE;
	gboolean      ok;

	/* Another copy writing for longer than the busy timeout is another copy
	 * busy, not an error. */
	if (!begin (db))
		return db->broken ? CLAIM_ERROR : CLAIM_BUSY;

	stmt = prep (db, read_sql, "read prune state");
	if (stmt != NULL) {
		if (sqlite3_step (stmt) == SQLITE_ROW) {
			owner     = sqlite3_column_int64 (stmt, 0);
			heartbeat = sqlite3_column_int64 (stmt, 1);
			*due      = sqlite3_column_int64 (stmt, 2);
			found = TRUE;
		}
		sqlite3_finalize (stmt);
	}

	if (!found) {
		rollback (db);
		return CLAIM_ERROR;
	}

	if (owner != 0 && owner != me && wall_secs () - heartbeat < CLAIM_STALE_SECS) {
		rollback (db);
		return CLAIM_BUSY;
	}

	if (!rules->force && rules->now < *due) {
		rollback (db);
		return CLAIM_NOT_DUE;
	}

	stmt = prep (db, take_sql, "claim prune");
	if (stmt == NULL) {
		rollback (db);
		return CLAIM_ERROR;
	}

	sqlite3_bind_int64 (stmt, 1, me);
	sqlite3_bind_int64 (stmt, 2, wall_secs ());
	sqlite3_bind_int64 (stmt, 3, wall_secs ());
	ok = db_ok (db, sqlite3_step (stmt), "claim prune");
	sqlite3_finalize (stmt);

	if (!ok) {
		rollback (db);
		return CLAIM_ERROR;
	}

	return commit (db) ? CLAIM_WON : CLAIM_ERROR;
}

static gboolean
begin_step (NemoCacheDb *db)
{
	if (!begin (db))
		return FALSE;

	db->held_since = g_get_monotonic_time ();

	return TRUE;
}

/* Called after each of the prune's writes. Once the lock is free sqlite gives
 * it to whoever asks first, not to whoever has waited longest, so a pass that
 * went straight on to its next write would keep a waiting store out until the
 * pass was done. Resting as long as the write took lets a waiter in within
 * about one write, and a write slowed by a busy box rests longer to match. A
 * pass takes at least twice as long for it, which is fine for a cleanup in the
 * background. */
static void
make_way (NemoCacheDb *db, GCancellable *cancellable)
{
	gint64 now = g_get_monotonic_time ();
	gint64 until = now + MAX (now - db->held_since, (gint64) PRUNE_REST_MIN_MS * 1000);

	g_atomic_int_inc (&prune_steps);

	while (now < until && !g_cancellable_is_cancelled (cancellable)) {
		g_usleep (MIN (until - now, 10 * 1000));
		now = g_get_monotonic_time ();
	}
}

/* Called inside each batch's transaction. Stamps the claim so nobody takes it
 * for a dead one, and answers false if somebody already has - a pass that
 * stalled past the stale limit must not carry on beside the one that took
 * over. */
static gboolean
still_ours (NemoCacheDb *db)
{
	static const char sql[] = "UPDATE prune SET heartbeat = ? WHERE id = 1 AND owner = ?";
	sqlite3_stmt *stmt = prep (db, sql, "prune heartbeat");
	gboolean      ok;

	if (stmt == NULL)
		return FALSE;

	sqlite3_bind_int64 (stmt, 1, wall_secs ());
	sqlite3_bind_int64 (stmt, 2, (gint64) getpid ());
	ok = db_ok (db, sqlite3_step (stmt), "prune heartbeat")
		&& sqlite3_changes (db->handle) == 1;
	sqlite3_finalize (stmt);

	return ok;
}

/* Lets go of the claim. A finished pass records what it did and when the next
 * one is due; a `due` below zero leaves the old time alone. */
static void
release_pass (NemoCacheDb *db, gboolean finished, gint64 removed, gint64 due)
{
	static const char done_sql[] =
		"UPDATE prune SET owner = 0, completed = ?, completed_by = ?, removed = ?, due = ?"
		" WHERE id = 1 AND owner = ?";
	static const char stop_sql[] =
		"UPDATE prune SET owner = 0, due = MAX (due, ?) WHERE id = 1 AND owner = ?";
	sqlite3_stmt *stmt;
	gint64        me = (gint64) getpid ();

	stmt = prep (db, finished ? done_sql : stop_sql, "release prune");
	if (stmt == NULL)
		return;

	if (finished) {
		sqlite3_bind_int64 (stmt, 1, wall_secs ());
		sqlite3_bind_int64 (stmt, 2, me);
		sqlite3_bind_int64 (stmt, 3, removed);
		sqlite3_bind_int64 (stmt, 4, due);
		sqlite3_bind_int64 (stmt, 5, me);
	} else {
		sqlite3_bind_int64 (stmt, 1, MAX (due, 0));
		sqlite3_bind_int64 (stmt, 2, me);
	}

	db_ok (db, sqlite3_step (stmt), "release prune");
	sqlite3_finalize (stmt);
}

/* quick_check rather than integrity_check: it skips matching every index
 * against its table, which is most of the cost, and still reads every page. */
static gboolean
integrity_ok (NemoCacheDb *db)
{
	sqlite3_stmt *stmt = prep (db, "PRAGMA quick_check (1)", "check");
	const char   *answer;
	gboolean      ok = FALSE;
	int           rc;

	if (stmt == NULL)
		return FALSE;

	rc = sqlite3_step (stmt);
	if (rc == SQLITE_ROW) {
		answer = (const char *) sqlite3_column_text (stmt, 0);
		ok = g_strcmp0 (answer, "ok") == 0;

		if (!ok && !db->broken) {
			g_warning ("the file cache is damaged and will be rebuilt next run (%s)",
				   answer != NULL ? answer : "no detail");
			mark_damaged ();
			db->broken = TRUE;
		}
	} else {
		db_ok (db, rc, "check");
	}

	sqlite3_finalize (stmt);

	return ok;
}

typedef enum {
	FOLDER_THERE = 1,
	FOLDER_GONE,
	FOLDER_SLOW
} FolderState;

/* True for a local file that is gone from a folder that is still there. A file
 * whose whole folder is missing is left alone, since that is more often a drive
 * that is not plugged in than a folder that was deleted. `folders` remembers
 * each folder's answer for the rest of the pass. */
static gboolean
local_file_is_gone (const char *uri, GHashTable *folders)
{
	g_autofree char *path = g_filename_from_uri (uri, NULL, NULL);
	g_autofree char *folder = NULL;
	FolderState      state;
	gint64           asked;

	if (path == NULL)
		return FALSE;

	/* A share can take twenty seconds to say no, once per question, and a
	   hard network mount can hold the pass for good. */
	if (nemo_path_is_on_a_share (path))
		return FALSE;

	folder = g_path_get_dirname (path);
	state = GPOINTER_TO_INT (g_hash_table_lookup (folders, folder));

	if (state == 0) {
		asked = g_get_monotonic_time ();
		state = g_file_test (folder, G_FILE_TEST_IS_DIR) ? FOLDER_THERE : FOLDER_GONE;
		if (g_get_monotonic_time () - asked > SLOW_FOLDER_USECS)
			state = FOLDER_SLOW;
		g_hash_table_insert (folders, g_steal_pointer (&folder), GINT_TO_POINTER (state));
	}

	return state == FOLDER_THERE && !g_file_test (path, G_FILE_TEST_EXISTS);
}

/* Forgets the names of local files that are gone. What they pointed at goes in
 * the next step, once nothing points at it. The files are asked about with no
 * transaction open, since that part can be slow. */
static gboolean
drop_missing_paths (NemoCacheDb *db, GCancellable *cancellable)
{
	static const char list_sql[] =
		"SELECT rowid, uri FROM paths WHERE rowid > ? ORDER BY rowid LIMIT "
		G_STRINGIFY (PRUNE_BATCH);
	static const char drop_sql[] = "DELETE FROM paths WHERE rowid = ?";
	g_autoptr (GHashTable) folders = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);
	g_autoptr (GArray)     gone = g_array_new (FALSE, FALSE, sizeof (gint64));
	g_autoptr (GPtrArray)  uris = g_ptr_array_new_with_free_func (g_free);
	g_autoptr (GArray)     rowids = g_array_new (FALSE, FALSE, sizeof (gint64));
	sqlite3_stmt          *stmt;
	gint64                 after = 0;
	gboolean               ok = TRUE;
	guint                  i;

	for (;;) {
		g_ptr_array_set_size (uris, 0);
		g_array_set_size (rowids, 0);
		g_array_set_size (gone, 0);

		stmt = prep (db, list_sql, "list paths");
		if (stmt == NULL)
			return FALSE;

		sqlite3_bind_int64 (stmt, 1, after);
		while (sqlite3_step (stmt) == SQLITE_ROW) {
			gint64 rowid = sqlite3_column_int64 (stmt, 0);

			g_array_append_val (rowids, rowid);
			g_ptr_array_add (uris, g_strdup ((const char *) sqlite3_column_text (stmt, 1)));
		}
		sqlite3_finalize (stmt);

		if (rowids->len == 0)
			break;

		after = g_array_index (rowids, gint64, rowids->len - 1);

		for (i = 0; i < uris->len; i++) {
			if (g_cancellable_is_cancelled (cancellable))
				return FALSE;
			if (g_ptr_array_index (uris, i) != NULL
			    && local_file_is_gone (g_ptr_array_index (uris, i), folders))
				g_array_append_val (gone, g_array_index (rowids, gint64, i));
		}

		if (gone->len > 0) {
			if (!begin_step (db))
				return FALSE;
			ok = still_ours (db);

			stmt = ok ? prep (db, drop_sql, "drop path") : NULL;
			ok = ok && stmt != NULL;
			for (i = 0; ok && i < gone->len; i++) {
				sqlite3_reset (stmt);
				sqlite3_bind_int64 (stmt, 1, g_array_index (gone, gint64, i));
				ok = db_ok (db, sqlite3_step (stmt), "drop path");
			}
			sqlite3_finalize (stmt);

			if (ok)
				ok = commit (db);
			else
				rollback (db);

			if (!ok)
				return FALSE;

			make_way (db, cancellable);
		}

		if (rowids->len < PRUNE_BATCH)
			break;
	}

	return TRUE;
}

/* Forgets names whose file has no thumbnail left, once nothing has stored one
 * under them since `cutoff`. All such a row keeps is a size, a time and maybe a
 * checksum, and the file gives those again when it is next thumbnailed. What
 * they pointed at goes with the orphans next.
 *
 * Nothing indexes `seen`, so the rows are picked with no transaction open, as
 * in drop_missing_paths, and the delete asks again in case a window stored a
 * thumbnail since. */
static gboolean
drop_bare_paths (NemoCacheDb *db, GCancellable *cancellable, gint64 cutoff)
{
	static const char list_sql[] =
		"SELECT rowid FROM paths p WHERE rowid > ? AND seen < ?"
		" AND NOT EXISTS (SELECT 1 FROM thumbnails t WHERE t.file_id = p.file_id)"
		" ORDER BY rowid LIMIT " G_STRINGIFY (PRUNE_BATCH);
	static const char drop_sql[] =
		"DELETE FROM paths WHERE rowid = ? AND seen < ?"
		" AND NOT EXISTS (SELECT 1 FROM thumbnails t WHERE t.file_id = paths.file_id)";
	g_autoptr (GArray) rowids = g_array_new (FALSE, FALSE, sizeof (gint64));
	sqlite3_stmt      *stmt;
	gint64             after = 0;
	gboolean           ok;
	guint              i;
	int                rc;

	for (;;) {
		if (g_cancellable_is_cancelled (cancellable))
			return FALSE;

		g_array_set_size (rowids, 0);

		stmt = prep (db, list_sql, "list bare paths");
		if (stmt == NULL)
			return FALSE;

		sqlite3_bind_int64 (stmt, 1, after);
		sqlite3_bind_int64 (stmt, 2, cutoff);
		while ((rc = sqlite3_step (stmt)) == SQLITE_ROW) {
			gint64 rowid = sqlite3_column_int64 (stmt, 0);

			g_array_append_val (rowids, rowid);
		}
		sqlite3_finalize (stmt);

		if (!db_ok (db, rc, "list bare paths"))
			return FALSE;
		if (rowids->len == 0)
			return TRUE;

		after = g_array_index (rowids, gint64, rowids->len - 1);

		if (!begin_step (db))
			return FALSE;
		ok = still_ours (db);

		stmt = ok ? prep (db, drop_sql, "drop bare path") : NULL;
		ok = ok && stmt != NULL;
		for (i = 0; ok && i < rowids->len; i++) {
			sqlite3_reset (stmt);
			sqlite3_bind_int64 (stmt, 1, g_array_index (rowids, gint64, i));
			sqlite3_bind_int64 (stmt, 2, cutoff);
			ok = db_ok (db, sqlite3_step (stmt), "drop bare path");
		}
		sqlite3_finalize (stmt);

		if (ok)
			ok = commit (db);
		else
			rollback (db);

		if (!ok)
			return FALSE;

		make_way (db, cancellable);

		if (rowids->len < PRUNE_BATCH)
			return TRUE;
	}
}

/* One prune transaction's worth of thumbnails. */
typedef struct {
	GArray  *ids;
	gint64   bytes;
	gboolean full;
} Batch;

/* Steps a pick whose rows are a file id and the size of its thumbnail, until
 * the batch is full: PRUNE_BATCH rows or `max_bytes` of thumbnails. A full
 * batch means there may be more. Finalizes the statement either way. */
static gboolean
pick_batch (NemoCacheDb *db, sqlite3_stmt *stmt, gint64 max_bytes, Batch *batch, const char *what)
{
	int rc;

	g_array_set_size (batch->ids, 0);
	batch->bytes = 0;
	batch->full = FALSE;

	while ((rc = sqlite3_step (stmt)) == SQLITE_ROW) {
		gint64 fid = sqlite3_column_int64 (stmt, 0);

		g_array_append_val (batch->ids, fid);
		batch->bytes += sqlite3_column_int64 (stmt, 1);

		if (batch->ids->len >= PRUNE_BATCH || batch->bytes >= max_bytes) {
			batch->full = TRUE;
			rc = SQLITE_DONE;
			break;
		}
	}

	sqlite3_finalize (stmt);

	return db_ok (db, rc, what);
}

/* Runs `sql`, which takes one id, for each id in the batch, and adds up the
 * rows it changed. */
static gboolean
delete_each (NemoCacheDb *db, const char *sql, const Batch *batch, gint64 *changed, const char *what)
{
	sqlite3_stmt *stmt;
	gboolean      ok = TRUE;
	guint         i;

	if (batch->ids->len == 0)
		return TRUE;

	stmt = prep (db, sql, what);
	if (stmt == NULL)
		return FALSE;

	for (i = 0; ok && i < batch->ids->len; i++) {
		sqlite3_reset (stmt);
		sqlite3_bind_int64 (stmt, 1, g_array_index (batch->ids, gint64, i));
		ok = db_ok (db, sqlite3_step (stmt), what);
		if (ok && changed != NULL)
			*changed += sqlite3_changes (db->handle);
	}

	sqlite3_finalize (stmt);

	return ok;
}

/* Commits a batch that went well and makes way for anyone waiting, or rolls
 * it back. */
static gboolean
end_batch (NemoCacheDb *db, gboolean ok, const Batch *batch, GCancellable *cancellable)
{
	if (!ok) {
		rollback (db);
		return FALSE;
	}

	if (!commit (db))
		return FALSE;

	db->largest_batch = MAX (db->largest_batch, batch->bytes);
	make_way (db, cancellable);

	return TRUE;
}

/* A file record nothing points at any more goes, and its thumbnail with it.
 * The thumbnails are deleted by hand rather than left to the cascade, because
 * sqlite does not count what a cascade removes. */
static gboolean
drop_orphans (NemoCacheDb *db, GCancellable *cancellable, gint64 *removed)
{
	static const char pick_sql[] =
		"SELECT f.id, IFNULL ((SELECT LENGTH (image) FROM thumbnails t WHERE t.file_id = f.id), 0)"
		" FROM files f WHERE NOT EXISTS (SELECT 1 FROM paths p WHERE p.file_id = f.id)"
		" LIMIT " G_STRINGIFY (PRUNE_BATCH);
	g_autoptr (GArray) ids = g_array_new (FALSE, FALSE, sizeof (gint64));
	Batch              batch = { ids, 0, FALSE };
	sqlite3_stmt      *stmt;
	gboolean           ok;

	do {
		if (g_cancellable_is_cancelled (cancellable) || !begin_step (db))
			return FALSE;

		ok = still_ours (db);
		stmt = ok ? prep (db, pick_sql, "orphans") : NULL;
		ok = stmt != NULL && pick_batch (db, stmt, PRUNE_BATCH_BYTES, &batch, "orphans");
		ok = ok && delete_each (db, "DELETE FROM thumbnails WHERE file_id = ?", &batch, removed, "orphans");
		ok = ok && delete_each (db, "DELETE FROM files WHERE id = ?", &batch, NULL, "orphans");

		if (!end_batch (db, ok, &batch, cancellable))
			return FALSE;
	} while (batch.full);

	return TRUE;
}

/* A thumbnail's age is the last time it was drawn, or when it was made if it
 * never has been. */
static gboolean
drop_old (NemoCacheDb *db, GCancellable *cancellable, gint64 cutoff, gint64 *removed)
{
	g_autoptr (GArray) ids = g_array_new (FALSE, FALSE, sizeof (gint64));
	Batch              batch = { ids, 0, FALSE };
	sqlite3_stmt      *stmt;
	gboolean           ok;

	do {
		if (g_cancellable_is_cancelled (cancellable) || !begin_step (db))
			return FALSE;

		ok = still_ours (db);
		stmt = ok ? prep (db, OLDER_THAN_SQL, "drop old") : NULL;
		if (stmt != NULL)
			sqlite3_bind_int64 (stmt, 1, cutoff);
		ok = stmt != NULL && pick_batch (db, stmt, PRUNE_BATCH_BYTES, &batch, "drop old");
		ok = ok && delete_each (db, "DELETE FROM thumbnails WHERE file_id = ?", &batch, removed, "drop old");

		if (!end_batch (db, ok, &batch, cancellable))
			return FALSE;
	} while (batch.full);

	return TRUE;
}

static gint64
pragma_int (NemoCacheDb *db, const char *sql)
{
	sqlite3_stmt *stmt = prep (db, sql, "size");

	return stmt != NULL ? step_id (stmt) : -1;
}

/* What the file really takes, not counting pages it has already freed. */
static gint64
used_bytes (NemoCacheDb *db)
{
	gint64 pages = pragma_int (db, "PRAGMA page_count");
	gint64 unused = pragma_int (db, "PRAGMA freelist_count");
	gint64 page_size = pragma_int (db, "PRAGMA page_size");

	if (pages < 0 || unused < 0 || page_size <= 0)
		return -1;

	return (pages - unused) * page_size;
}

/* Least recently drawn first, taking only as many as it takes to get under. */
static gboolean
drop_to_size (NemoCacheDb *db, GCancellable *cancellable, gint64 max_bytes, gint64 *removed)
{
	g_autoptr (GArray) ids = g_array_new (FALSE, FALSE, sizeof (gint64));
	Batch              batch = { ids, 0, FALSE };
	sqlite3_stmt      *stmt;
	gint64             used;
	gboolean           ok;

	for (;;) {
		if (g_cancellable_is_cancelled (cancellable))
			return FALSE;

		used = used_bytes (db);
		if (used < 0)
			return FALSE;
		if (used <= max_bytes)
			return TRUE;

		if (!begin_step (db))
			return FALSE;

		ok = still_ours (db);
		stmt = ok ? prep (db, OLDEST_SQL, "pick oldest") : NULL;
		ok = stmt != NULL
			&& pick_batch (db, stmt, MIN (used - max_bytes, PRUNE_BATCH_BYTES), &batch, "pick oldest");
		ok = ok && delete_each (db, "DELETE FROM thumbnails WHERE file_id = ?", &batch, NULL, "drop oldest");

		if (!end_batch (db, ok, &batch, cancellable))
			return FALSE;

		/* Nothing left to take, and what is over is names and records. */
		if (batch.ids->len == 0)
			return TRUE;

		*removed += batch.ids->len;
	}
}

/* Hands freed pages back to the disk a step at a time, each its own short
 * write, so nobody else waits on it for long. */
static gboolean
compact (NemoCacheDb *db, GCancellable *cancellable)
{
	gint64 unused, before;

	unused = pragma_int (db, "PRAGMA freelist_count");

	while (unused > 0) {
		if (g_cancellable_is_cancelled (cancellable))
			return FALSE;

		db->held_since = g_get_monotonic_time ();
		if (!db_ok (db, sqlite3_exec (db->handle,
					      "PRAGMA incremental_vacuum (" G_STRINGIFY (COMPACT_STEP_PAGES) ")",
					      NULL, NULL, NULL), "compact"))
			return FALSE;
		make_way (db, cancellable);

		before = unused;
		unused = pragma_int (db, "PRAGMA freelist_count");

		/* A file made before incremental vacuum was turned on never
		 * shrinks this way. Not worth looping over. */
		if (unused >= before)
			break;
	}

	sqlite3_exec (db->handle, "PRAGMA wal_checkpoint (PASSIVE)", NULL, NULL, NULL);

	return unused >= 0;
}

static gint64
next_gap (const NemoCachePruneRules *rules)
{
	gint64 low = MAX (rules->gap_min_secs, 0);
	gint64 span = MAX (rules->gap_max_secs - low, 0);

	span = MIN (span, (gint64) G_MAXINT32 - 1);

	return low + (span > 0 ? g_random_int_range (0, (gint32) span + 1) : 0);
}

NemoCachePruneResult
nemo_cache_db_prune (const NemoCachePruneRules *rules,
		     GCancellable              *cancellable,
		     gint64                    *removed_out,
		     gint64                    *due_out)
{
	g_autofree char     *path = nemo_cache_db_path ();
	g_autoptr (GCancellable) own_cancellable = NULL;
	NemoCacheDb          pruner = { 0 };
	NemoCachePruneResult result;
	gboolean             rebuild = FALSE;
	gboolean             ok;
	gint64               removed = 0;
	gint64               due = 0;
	gint64               bare_age;

	g_return_val_if_fail (rules != NULL, NEMO_CACHE_PRUNE_FAILED);

	bare_age = rules->max_age_secs > 0 ? rules->max_age_secs : BARE_PATH_AGE_SECS;

	if (removed_out != NULL)
		*removed_out = 0;
	if (due_out != NULL)
		*due_out = 0;

	if (path == NULL)
		return NEMO_CACHE_PRUNE_FAILED;

	if (cancellable == NULL)
		cancellable = own_cancellable = g_cancellable_new ();

	/* A connection of its own, so a long pass never holds the lock the draw
	 * path waits on. The two take turns at the file through sqlite's own
	 * locking, the same as two processes do. */
	pruner.handle = open_at (path, &rebuild);
	if (pruner.handle == NULL) {
		if (rebuild)
			mark_damaged ();
		return rebuild ? NEMO_CACHE_PRUNE_DAMAGED : NEMO_CACHE_PRUNE_FAILED;
	}

	switch (claim_pass (&pruner, rules, &due)) {
	case CLAIM_WON:
		/* Only now, so a quit cannot leave the claim half taken. */
		sqlite3_progress_handler (pruner.handle, 1000, prune_interrupted, cancellable);
		break;
	case CLAIM_NOT_DUE:
		result = NEMO_CACHE_PRUNE_NOT_DUE;
		goto out;
	case CLAIM_BUSY:
		due = 0;
		result = NEMO_CACHE_PRUNE_BUSY;
		goto out;
	case CLAIM_ERROR:
	default:
		due = 0;
		result = pruner.broken ? NEMO_CACHE_PRUNE_DAMAGED : NEMO_CACHE_PRUNE_FAILED;
		goto out;
	}

	ok = integrity_ok (&pruner)
		&& (!rules->drop_missing || drop_missing_paths (&pruner, cancellable))
		&& drop_bare_paths (&pruner, cancellable, rules->now - bare_age)
		&& drop_orphans (&pruner, cancellable, &removed)
		&& (rules->max_age_secs <= 0
		    || drop_old (&pruner, cancellable, rules->now - rules->max_age_secs, &removed))
		&& (rules->max_bytes <= 0
		    || drop_to_size (&pruner, cancellable, rules->max_bytes, &removed))
		&& compact (&pruner, cancellable);

	/* Or the release below is interrupted too. */
	sqlite3_progress_handler (pruner.handle, 0, NULL, NULL);

	if (ok) {
		due = rules->now + next_gap (rules);
		release_pass (&pruner, TRUE, removed, due);
		result = NEMO_CACHE_PRUNE_DONE;
	} else if (pruner.broken) {
		/* The file is going at the next launch, claim and all. */
		due = 0;
		result = NEMO_CACHE_PRUNE_DAMAGED;
	} else if (g_cancellable_is_cancelled (cancellable)) {
		release_pass (&pruner, FALSE, 0, -1);
		due = 0;
		result = NEMO_CACHE_PRUNE_CANCELLED;
	} else {
		/* Not straight back into whatever went wrong. */
		due = rules->now + MAX (rules->gap_min_secs, 0);
		release_pass (&pruner, FALSE, 0, due);
		result = NEMO_CACHE_PRUNE_FAILED;
	}

out:
	sqlite3_close (pruner.handle);
	last_largest_batch = pruner.largest_batch;

	if (removed_out != NULL)
		*removed_out = removed;
	if (due_out != NULL)
		*due_out = due;

	return result;
}

/* Runs one pick query to the end and adds up what sqlite says it cost. */
static gboolean
add_pick_cost (NemoCacheDb *db, const char *sql, gint64 *rows_walked, gint64 *sorts)
{
	sqlite3_stmt *stmt = prep (db, sql, "pick cost");
	int           rc;

	if (stmt == NULL)
		return FALSE;

	/* Older than the epoch: nothing matches, which is the case that used to
	 * walk the whole table. The other query takes no value. */
	if (sqlite3_bind_parameter_count (stmt) > 0)
		sqlite3_bind_int64 (stmt, 1, 0);

	while ((rc = sqlite3_step (stmt)) == SQLITE_ROW)
		;

	*rows_walked += sqlite3_stmt_status (stmt, SQLITE_STMTSTATUS_FULLSCAN_STEP, FALSE);
	*sorts += sqlite3_stmt_status (stmt, SQLITE_STMTSTATUS_SORT, FALSE);

	sqlite3_finalize (stmt);

	return db_ok (db, rc, "pick cost");
}

gboolean
nemo_cache_db_prune_pick_cost (NemoCacheDb *db, gint64 *rows_walked, gint64 *sorts)
{
	gboolean ok;

	g_return_val_if_fail (rows_walked != NULL && sorts != NULL, FALSE);

	*rows_walked = 0;
	*sorts = 0;

	if (db == NULL || db->broken)
		return FALSE;

	g_mutex_lock (&db->lock);

	ok = add_pick_cost (db, OLDEST_SQL, rows_walked, sorts)
		&& add_pick_cost (db, OLDER_THAN_SQL, rows_walked, sorts);

	g_mutex_unlock (&db->lock);

	return ok;
}

gint64
nemo_cache_db_prune_largest_batch (void)
{
	return last_largest_batch;
}

gint
nemo_cache_db_prune_steps (void)
{
	return g_atomic_int_get (&prune_steps);
}

/* Returns: (transfer full): close with sqlite3_close */
struct sqlite3 *
nemo_cache_db_open_another (void)
{
	g_autofree char *path = nemo_cache_db_path ();
	gboolean         rebuild;

	return path != NULL ? open_at (path, &rebuild) : NULL;
}
