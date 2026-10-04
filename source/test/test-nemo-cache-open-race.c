/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */

/* test-nemo-cache-open-race.c - copies opening a new file cache at once.

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

/* Every launch is its own process, so two windows started together can both
 * find no cache file and set one up at the same moment. Turning a new file
 * over to its journal reads the file and then takes the write lock, and sqlite
 * does not wait on that second step. So a copy that got there while another
 * held the lock was told the file was locked, and ran with no cache.
 *
 * First, one connection holds the write lock on a new file for a moment while
 * the store opens it, which always hit that step. Then each round starts a few
 * copies of this program on a cache that is not there yet, all timed to open
 * it in the same instant, and every one of them has to get a store. That one
 * only hit it about one round in a hundred.
 *
 * On Windows the copies also quit without closing the store, as the app does,
 * and one that is still going away can leave the next unable to set up. A view
 * of the -shm file with no lock behind it stands in for that copy. */

#include <config.h>

#include <stdlib.h>
#include <string.h>
#include <glib.h>
#include <glib/gstdio.h>

#include <sqlite3.h>

#ifdef G_OS_WIN32
#include <windows.h>
#endif

#include <libnemo-private/nemo-cache-db.h>

#include "test-scratch.h"
#include "test-check.h"

#define COPIES 4

/* Long enough for every copy to be loaded and waiting before the moment. */
#define START_DELAY_MS 300

/* The store's own wait on a busy file. A copy that gave up only after that
 * did what it should, on a disk too slow to let the others finish. */
#define BUSY_TIMEOUT_MS 3000

/* Well inside that. */
#define HOLD_MS 300

/* How long a reader is in the way of the holder's commit. */
#define READ_MS 50

/* Exit status of a copy that waited out the busy timeout. */
#define WAITED_OUT 2

/* On Windows sqlite logs the system call that failed and the file it was on,
 * which says more than the code the store reports. Busy is the normal wait. */
static void
sqlite_said (void *data, int code, const char *message)
{
	(void) data;

	switch (code & 0xff) {
	case SQLITE_BUSY:
	case SQLITE_NOTICE:
	case SQLITE_WARNING:
		return;
	default:
		g_printerr ("sqlite (%d): %s\n", code, message);
	}
}

static int
run_copy (const char *when)
{
	gint64 start = g_ascii_strtoll (when, NULL, 10);
	gint64 late;

	sqlite3_config (SQLITE_CONFIG_LOG, sqlite_said, NULL);

	/* Spin, since a sleep wakes too late to line the copies up. */
	while (g_get_real_time () < start)
		;

	if (nemo_cache_db_get () != NULL)
		return EXIT_SUCCESS;

	late = (g_get_real_time () - start) / 1000;
	g_printerr ("a copy had no store after %" G_GINT64_FORMAT " ms\n", late);

	return late >= BUSY_TIMEOUT_MS ? WAITED_OUT : EXIT_FAILURE;
}

static void
remove_store (const char *path)
{
	g_autofree char *wal = g_strconcat (path, "-wal", NULL);
	g_autofree char *shm = g_strconcat (path, "-shm", NULL);

	g_unlink (path);
	g_unlink (wal);
	g_unlink (shm);
}

typedef struct {
	const char *path;
	sqlite3    *holder;
	gboolean    reader_in;
	int         commit_rc;
} Hold;

static gpointer
end_read_later (gpointer user_data)
{
	g_usleep (READ_MS * 1000);
	sqlite3_exec (user_data, "COMMIT", NULL, NULL, NULL);

	return NULL;
}

/* While the store waits it keeps trying, and each try takes a read lock for a
 * moment. Under load one of those can be there right as the holder commits,
 * and a commit with no wait of its own is then refused and keeps its lock until
 * the store has given up. That happened once on a slow box. So a read lock is
 * made to be there every time, and the holder has to wait it out like any
 * other copy would. */
static gpointer
release_later (gpointer user_data)
{
	Hold *hold = user_data;
	sqlite3 *reader = NULL;
	GThread *thread;

	g_usleep (HOLD_MS * 1000);

	hold->reader_in = sqlite3_open (hold->path, &reader) == SQLITE_OK
		&& sqlite3_exec (reader, "BEGIN; SELECT COUNT (*) FROM sqlite_master", NULL, NULL, NULL) == SQLITE_OK;
	thread = g_thread_new ("reader", end_read_later, reader);

	hold->commit_rc = sqlite3_exec (hold->holder, "COMMIT", NULL, NULL, NULL);

	g_thread_join (thread);
	sqlite3_close (reader);

	return NULL;
}

/* The state the other copy is in halfway through its own setup: the file has
 * a first page, is not in WAL yet, and the write lock is taken. */
static void
check_waits_for_lock (const char *path)
{
	Hold hold = { path, NULL, FALSE, SQLITE_OK };
	NemoCacheDb *db;
	GThread *thread;
	gint64 started, waited;

	remove_store (path);
	check (sqlite3_open (path, &hold.holder) == SQLITE_OK);
	sqlite3_busy_timeout (hold.holder, BUSY_TIMEOUT_MS);
	check (sqlite3_exec (hold.holder, "PRAGMA user_version = 0; BEGIN IMMEDIATE", NULL, NULL, NULL) == SQLITE_OK);

	thread = g_thread_new ("holder", release_later, &hold);
	started = g_get_monotonic_time ();
	db = nemo_cache_db_get ();
	waited = (g_get_monotonic_time () - started) / 1000;
	g_thread_join (thread);

	check (hold.reader_in);
	if (hold.commit_rc != SQLITE_OK)
		g_printerr ("the holder could not let go of the lock: %s\n", sqlite3_errstr (hold.commit_rc));
	check (hold.commit_rc == SQLITE_OK);

	if (db == NULL)
		g_printerr ("no store while the lock was held, gave up after %" G_GINT64_FORMAT " ms\n", waited);
	check (db != NULL);

	nemo_cache_db_close ();
	sqlite3_close (hold.holder);
	remove_store (path);
}

#ifdef G_OS_WIN32
/* Bigger than the first region sqlite maps, as a real one would be. */
#define OLD_VIEW_BYTES (64 * 1024)

typedef struct {
	HANDLE file;
	HANDLE mapping;
	void  *view;
} OldView;

static gpointer
unmap_later (gpointer user_data)
{
	OldView *old = user_data;

	g_usleep (HOLD_MS * 1000);
	UnmapViewOfFile (old->view);
	CloseHandle (old->mapping);
	CloseHandle (old->file);

	return NULL;
}

/* A copy that quit lets go of its locks before Windows unmaps the -shm file
 * from it. Windows will not empty a file while a view of it is left, so the
 * store has to wait for that view to go, as it would for a lock. */
static void
check_waits_for_old_view (const char *path)
{
	g_autofree char *shm_path = g_strconcat (path, "-shm", NULL);
	g_autofree gunichar2 *wide = NULL;
	OldView old = { INVALID_HANDLE_VALUE, NULL, NULL };
	NemoCacheDb *db;
	GThread *thread;
	gint64 started, waited;

	remove_store (path);
	wide = g_utf8_to_utf16 (shm_path, -1, NULL, NULL, NULL);
	old.file = CreateFileW (wide, GENERIC_READ | GENERIC_WRITE,
				FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
				NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
	check (old.file != INVALID_HANDLE_VALUE);
	if (old.file == INVALID_HANDLE_VALUE)
		return;
	old.mapping = CreateFileMappingW (old.file, NULL, PAGE_READWRITE, 0, OLD_VIEW_BYTES, NULL);
	if (old.mapping != NULL)
		old.view = MapViewOfFile (old.mapping, FILE_MAP_WRITE, 0, 0, OLD_VIEW_BYTES);
	check (old.view != NULL);
	if (old.view == NULL) {
		if (old.mapping != NULL)
			CloseHandle (old.mapping);
		CloseHandle (old.file);
		return;
	}

	thread = g_thread_new ("old view", unmap_later, &old);
	started = g_get_monotonic_time ();
	db = nemo_cache_db_get ();
	waited = (g_get_monotonic_time () - started) / 1000;
	g_thread_join (thread);

	if (db == NULL)
		g_printerr ("no store while an old view was left, gave up after %" G_GINT64_FORMAT " ms\n", waited);
	check (db != NULL);

	nemo_cache_db_close ();
	remove_store (path);
}
#endif

typedef struct {
	int        running;
	int        missed;
	int        waited_out;
	GMainLoop *loop;
} Round;

static void
copy_done (GPid pid, gint status, gpointer user_data)
{
	Round *round = user_data;
	GError *error = NULL;

	if (!g_spawn_check_wait_status (status, &error)) {
		if (g_error_matches (error, G_SPAWN_EXIT_ERROR, WAITED_OUT))
			round->waited_out++;
		else
			round->missed++;
		g_clear_error (&error);
	}
	g_spawn_close_pid (pid);

	if (--round->running == 0)
		g_main_loop_quit (round->loop);
}

/* How many copies came back without a store before their wait was up. */
static int
run_round (const char *self, int *waited_out)
{
	Round round = { 0 };
	char when[32];
	int i;

	round.loop = g_main_loop_new (NULL, FALSE);
	g_snprintf (when, sizeof (when), "%" G_GINT64_FORMAT,
		    g_get_real_time () + START_DELAY_MS * 1000);

	for (i = 0; i < COPIES; i++) {
		char *argv[] = { (char *) self, (char *) "--copy", when, NULL };
		GError *error = NULL;
		GPid pid;

		if (!g_spawn_async (NULL, argv, NULL, G_SPAWN_DO_NOT_REAP_CHILD,
				    NULL, NULL, &pid, &error)) {
			g_printerr ("spawn: %s\n", error->message);
			g_error_free (error);
			round.missed++;
			continue;
		}
		round.running++;
		g_child_watch_add (pid, copy_done, &round);
	}

	if (round.running > 0)
		g_main_loop_run (round.loop);
	g_main_loop_unref (round.loop);

	*waited_out += round.waited_out;

	return round.missed;
}

int
main (int argc, char *argv[])
{
	g_autofree char *root = NULL;
	g_autofree char *path = NULL;
	const char *env_rounds;
	int rounds = 20;
	int round, missed = 0, bad_rounds = 0, waited_out = 0;

	if (argc == 3 && strcmp (argv[1], "--copy") == 0)
		return run_copy (argv[2]);

	env_rounds = g_getenv ("NEMO_TEST_RACE_ROUNDS");
	if (env_rounds != NULL)
		rounds = atoi (env_rounds);

	/* The copies inherit this, so they all find the same file. */
	root = test_scratch_config_home ("nemo-cache-race-XXXXXX");
	path = nemo_cache_db_path ();
	check (path != NULL);
	if (path == NULL)
		return EXIT_FAILURE;

	check_waits_for_lock (path);
#ifdef G_OS_WIN32
	check_waits_for_old_view (path);
#endif

	for (round = 0; round < rounds; round++) {
		int this_round;

		remove_store (path);
		this_round = run_round (argv[0], &waited_out);
		if (this_round > 0) {
			bad_rounds++;
			missed += this_round;
		}
	}

	if (bad_rounds > 0)
		g_printerr ("%d of %d rounds had a copy with no store, %d copies in all\n",
			    bad_rounds, rounds, missed);
	check (bad_rounds == 0);

	if (waited_out > 0)
		g_printerr ("%d copies waited out the busy timeout, not counted\n", waited_out);

	if (failures > 0) {
		g_printerr ("%d failure(s)\n", failures);
		return EXIT_FAILURE;
	}

	g_print ("OK, %d rounds of %d copies\n", rounds, COPIES);
	return EXIT_SUCCESS;
}
