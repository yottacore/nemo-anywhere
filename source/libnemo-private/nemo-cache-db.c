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

/* Draw counts are batched rather than written one at a time - see note_render. */
#define RENDER_FLUSH_SECS 30
#define RENDER_FLUSH_MAX  256

/* A claim on the prune nobody has touched for this long belongs to a process
 * that died part way through. */
#define CLAIM_STALE_SECS (10 * 60)

/* Rows per prune transaction. Small enough that a window waiting to write
 * never waits long. */
#define PRUNE_BATCH 256

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
	"SELECT file_id FROM thumbnails" \
	" WHERE " THUMBNAIL_AGE " < ? LIMIT " G_STRINGIFY (PRUNE_BATCH)

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

	/* uri -> draws not yet written. Guarded by `lock`. */
	GHashTable  *pending;
	guint        flush_id;
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

char *
nemo_cache_db_path (void)
{
	g_autofree char *dir = nemo_get_user_cache_directory ();

	if (dir == NULL)
		return NULL;

	return g_build_filename (dir, "files.db", NULL);
}

static int
read_user_version (sqlite3 *handle)
{
	sqlite3_stmt *stmt = NULL;
	int           version = 0;

	if (sqlite3_prepare_v2 (handle, "PRAGMA user_version", -1, &stmt, NULL) != SQLITE_OK)
		return -1;

	if (sqlite3_step (stmt) == SQLITE_ROW)
		version = sqlite3_column_int (stmt, 0);

	sqlite3_finalize (stmt);

	return version;
}

/* Opens the file and puts the schema in it. Answers NULL and leaves nothing
 * behind if it could not, so the caller can wipe and try once more. */
static sqlite3 *
open_at (const char *path, gboolean *out_rebuild)
{
	sqlite3 *handle = NULL;
	char    *err = NULL;
	int      version;
	int      rc;

	*out_rebuild = FALSE;

	rc = sqlite3_open_v2 (path, &handle,
			      SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_FULLMUTEX,
			      NULL);
	if (rc != SQLITE_OK) {
		*out_rebuild = is_corruption (rc);
		g_warning ("could not open the file cache %s: %s", path, sqlite3_errstr (rc));
		sqlite3_close (handle);
		return NULL;
	}

	sqlite3_busy_timeout (handle, BUSY_TIMEOUT_MS);

	/* Incremental vacuum so the prune can hand space back a few pages at a
	 * time, rather than a full VACUUM holding the write lock for as long as
	 * it takes to copy the whole file. It only takes on a file with no tables
	 * yet, which is every file this version opens, since an older one is
	 * wiped.
	 *
	 * WAL so a reading window is not blocked by a writing one, and NORMAL
	 * because losing the last few rows to a power cut costs a re-read and
	 * nothing else. The size limit trims the journal back after a prune has
	 * grown it. */
	rc = sqlite3_exec (handle,
			   "PRAGMA auto_vacuum = INCREMENTAL;"
			   "PRAGMA journal_mode = WAL;"
			   "PRAGMA journal_size_limit = 67108864;"
			   "PRAGMA synchronous = NORMAL;"
			   "PRAGMA foreign_keys = ON;",
			   NULL, NULL, &err);
	if (rc != SQLITE_OK) {
		*out_rebuild = is_corruption (rc);
		g_warning ("could not set up the file cache: %s", err ? err : sqlite3_errstr (rc));
		sqlite3_free (err);
		sqlite3_close (handle);
		return NULL;
	}

	/* 0 is a file nobody has written tables into yet. Anything else that is
	 * not ours was written by another version, and gets the same treatment as
	 * a damaged file. */
	version = read_user_version (handle);
	if (version != 0 && version != SCHEMA_VERSION) {
		*out_rebuild = TRUE;
		sqlite3_close (handle);
		return NULL;
	}

	rc = sqlite3_exec (handle, SCHEMA, NULL, NULL, &err);
	if (rc != SQLITE_OK) {
		*out_rebuild = is_corruption (rc);
		g_warning ("could not make the file cache tables: %s", err ? err : sqlite3_errstr (rc));
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
	db->pending = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);

	return db;
}

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
	static const char sql[] =
		"DELETE FROM thumbnails WHERE file_id = (SELECT file_id FROM paths WHERE uri = ?)";
	sqlite3_stmt *stmt;
	gboolean      done = FALSE;

	g_return_val_if_fail (uri != NULL, FALSE);

	if (db == NULL || db->broken)
		return FALSE;

	g_mutex_lock (&db->lock);

	stmt = prep (db, sql, "forget thumbnail");
	if (stmt != NULL) {
		sqlite3_bind_text (stmt, 1, uri, -1, SQLITE_STATIC);
		done = db_ok (db, sqlite3_step (stmt), "forget thumbnail");
		sqlite3_finalize (stmt);
	}

	g_mutex_unlock (&db->lock);

	return done;
}

/* Every drawn icon in a folder would otherwise be a write, and scrolling a big
 * folder draws the same file over and over. Counts are held in memory and
 * written in one transaction, which is also all the age rule needs - it works
 * in days. Called with the lock held. */
static void
flush_renders (NemoCacheDb *db)
{
	static const char sql[] =
		"UPDATE thumbnails SET rendered = ?, renders = renders + ?"
		" WHERE file_id = (SELECT file_id FROM paths WHERE uri = ?)";
	sqlite3_stmt  *stmt;
	GHashTableIter iter;
	gpointer       uri, count;
	gint64         now;

	if (db->broken || g_hash_table_size (db->pending) == 0)
		return;

	now = g_get_real_time () / G_USEC_PER_SEC;

	if (!begin (db))
		return;

	stmt = prep (db, sql, "draw count");
	if (stmt == NULL) {
		rollback (db);
		return;
	}

	g_hash_table_iter_init (&iter, db->pending);
	while (g_hash_table_iter_next (&iter, &uri, &count)) {
		sqlite3_reset (stmt);
		sqlite3_bind_int64 (stmt, 1, now);
		sqlite3_bind_int64 (stmt, 2, (gint64) GPOINTER_TO_INT (count));
		sqlite3_bind_text (stmt, 3, (const char *) uri, -1, SQLITE_STATIC);
		db_ok (db, sqlite3_step (stmt), "draw count");
	}

	sqlite3_finalize (stmt);

	commit (db);

	g_hash_table_remove_all (db->pending);
}

static gboolean
flush_renders_timeout (gpointer data)
{
	NemoCacheDb *db = data;

	g_mutex_lock (&db->lock);
	db->flush_id = 0;
	flush_renders (db);
	g_mutex_unlock (&db->lock);

	return G_SOURCE_REMOVE;
}

void
nemo_cache_db_note_render (NemoCacheDb *db, const char *uri)
{
	gpointer old;

	g_return_if_fail (uri != NULL);

	if (db == NULL || db->broken)
		return;

	touch ();

	g_mutex_lock (&db->lock);

	old = g_hash_table_lookup (db->pending, uri);
	if (old != NULL)
		g_hash_table_replace (db->pending, g_strdup (uri),
				      GINT_TO_POINTER (GPOINTER_TO_INT (old) + 1));
	else
		g_hash_table_insert (db->pending, g_strdup (uri), GINT_TO_POINTER (1));

	if (g_hash_table_size (db->pending) >= RENDER_FLUSH_MAX)
		flush_renders (db);
	else if (db->flush_id == 0)
		db->flush_id = g_timeout_add_seconds (RENDER_FLUSH_SECS, flush_renders_timeout, db);

	g_mutex_unlock (&db->lock);
}

void
nemo_cache_db_flush (NemoCacheDb *db)
{
	if (db == NULL || db->broken)
		return;

	g_mutex_lock (&db->lock);

	if (db->flush_id != 0) {
		g_source_remove (db->flush_id);
		db->flush_id = 0;
	}
	flush_renders (db);

	g_mutex_unlock (&db->lock);
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

	g_hash_table_remove_all (db->pending);

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
	if (db->flush_id != 0) {
		g_source_remove (db->flush_id);
		db->flush_id = 0;
	}
	flush_renders (db);

	/* Folds the WAL back in, so the next launch opens one file rather than
	 * replaying a journal that could be most of the cache. */
	sqlite3_exec (db->handle, "PRAGMA wal_checkpoint (TRUNCATE)", NULL, NULL, NULL);
}

void
nemo_cache_db_quit (void)
{
	NemoCacheDb *db;

	/* Not get(), which would make a store for a run that never used one. */
	g_mutex_lock (&the_db_lock);
	db = the_db;
	g_mutex_unlock (&the_db_lock);

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

	g_hash_table_destroy (db->pending);

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

	/* A share can take twenty seconds to say no, once per question. */
	if ((path[0] == '/' && path[1] == '/') || (path[0] == '\\' && path[1] == '\\'))
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
			if (!begin (db))
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
		}

		if (rowids->len < PRUNE_BATCH)
			break;
	}

	return TRUE;
}

/* Runs one batch statement inside its own transaction, with the claim checked
 * first. `sql` may carry one bound value. Answers the rows changed, or -1. */
static gint64
batch_delete (NemoCacheDb *db, const char *sql, gboolean bind, gint64 value, const char *what)
{
	sqlite3_stmt *stmt;
	gint64        changed = -1;

	if (!begin (db))
		return -1;

	if (!still_ours (db)) {
		rollback (db);
		return -1;
	}

	stmt = prep (db, sql, what);
	if (stmt != NULL) {
		if (bind)
			sqlite3_bind_int64 (stmt, 1, value);
		if (db_ok (db, sqlite3_step (stmt), what))
			changed = sqlite3_changes (db->handle);
		sqlite3_finalize (stmt);
	}

	if (changed < 0) {
		rollback (db);
		return -1;
	}

	return commit (db) ? changed : -1;
}

/* A file record nothing points at any more goes, and its thumbnail with it.
 * The thumbnails are deleted by hand rather than left to the cascade, because
 * sqlite does not count what a cascade removes. */
static gboolean
drop_orphans (NemoCacheDb *db, GCancellable *cancellable, gint64 *removed)
{
	static const char pick_sql[] =
		"DELETE FROM doomed;"
		"INSERT INTO doomed SELECT id FROM files f"
		" WHERE NOT EXISTS (SELECT 1 FROM paths p WHERE p.file_id = f.id)"
		" LIMIT " G_STRINGIFY (PRUNE_BATCH) ";";
	gint64   picked;
	gboolean ok;

	if (!db_ok (db, sqlite3_exec (db->handle,
				      "CREATE TEMP TABLE IF NOT EXISTS doomed (id INTEGER PRIMARY KEY)",
				      NULL, NULL, NULL), "orphans"))
		return FALSE;

	do {
		if (g_cancellable_is_cancelled (cancellable) || !begin (db))
			return FALSE;

		ok = still_ours (db)
			&& db_ok (db, sqlite3_exec (db->handle, pick_sql, NULL, NULL, NULL), "orphans");
		picked = ok ? sqlite3_changes (db->handle) : 0;

		if (ok && picked > 0) {
			ok = db_ok (db, sqlite3_exec (db->handle,
						      "DELETE FROM thumbnails WHERE file_id IN (SELECT id FROM doomed)",
						      NULL, NULL, NULL), "orphans");
			if (ok)
				*removed += sqlite3_changes (db->handle);
			ok = ok && db_ok (db, sqlite3_exec (db->handle,
							    "DELETE FROM files WHERE id IN (SELECT id FROM doomed)",
							    NULL, NULL, NULL), "orphans");
		}

		if (ok)
			ok = commit (db);
		else
			rollback (db);

		if (!ok)
			return FALSE;
	} while (picked == PRUNE_BATCH);

	return TRUE;
}

/* A thumbnail's age is the last time it was drawn, or when it was made if it
 * never has been. */
static gboolean
drop_old (NemoCacheDb *db, GCancellable *cancellable, gint64 cutoff, gint64 *removed)
{
	static const char sql[] = "DELETE FROM thumbnails WHERE file_id IN (" OLDER_THAN_SQL ")";
	gint64 changed;

	do {
		if (g_cancellable_is_cancelled (cancellable))
			return FALSE;

		changed = batch_delete (db, sql, TRUE, cutoff, "drop old");
		if (changed < 0)
			return FALSE;

		*removed += changed;
	} while (changed == PRUNE_BATCH);

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
	static const char pick_sql[] = OLDEST_SQL;
	static const char drop_sql[] = "DELETE FROM thumbnails WHERE file_id = ?";
	g_autoptr (GArray) doomed = g_array_new (FALSE, FALSE, sizeof (gint64));
	sqlite3_stmt      *stmt;
	gint64             used, excess, freed;
	gboolean           ok;
	guint              i;

	for (;;) {
		if (g_cancellable_is_cancelled (cancellable))
			return FALSE;

		used = used_bytes (db);
		if (used < 0)
			return FALSE;
		if (used <= max_bytes)
			return TRUE;

		excess = used - max_bytes;
		freed = 0;
		g_array_set_size (doomed, 0);

		if (!begin (db))
			return FALSE;
		ok = still_ours (db);

		stmt = ok ? prep (db, pick_sql, "pick oldest") : NULL;
		if (stmt != NULL) {
			while (freed < excess && sqlite3_step (stmt) == SQLITE_ROW) {
				gint64 fid = sqlite3_column_int64 (stmt, 0);

				g_array_append_val (doomed, fid);
				freed += sqlite3_column_int64 (stmt, 1);
			}
			sqlite3_finalize (stmt);
		} else {
			ok = FALSE;
		}

		stmt = (ok && doomed->len > 0) ? prep (db, drop_sql, "drop oldest") : NULL;
		for (i = 0; stmt != NULL && ok && i < doomed->len; i++) {
			sqlite3_reset (stmt);
			sqlite3_bind_int64 (stmt, 1, g_array_index (doomed, gint64, i));
			ok = db_ok (db, sqlite3_step (stmt), "drop oldest");
		}
		sqlite3_finalize (stmt);

		if (ok)
			ok = commit (db);
		else
			rollback (db);

		if (!ok)
			return FALSE;

		/* Nothing left to take, and what is over is names and records. */
		if (doomed->len == 0)
			return TRUE;

		*removed += doomed->len;
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

		if (!db_ok (db, sqlite3_exec (db->handle,
					      "PRAGMA incremental_vacuum (" G_STRINGIFY (COMPACT_STEP_PAGES) ")",
					      NULL, NULL, NULL), "compact"))
			return FALSE;

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

	g_return_val_if_fail (rules != NULL, NEMO_CACHE_PRUNE_FAILED);

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
