/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */

/* nemo-cache-db-prune.c - when the file cache is cleaned up.

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

#include "nemo-cache-db-prune.h"

#include "nemo-cache-db.h"
#include "nemo-global-preferences.h"

/* How often the timer looks. Being due is measured in hours, so a minute late
 * is nothing. */
#define CHECK_SECS 60

/* A pass another copy is running is looked at again after this long. */
#define BUSY_RETRY_SECS (15 * 60)

/* How long quitting waits for a pass to let go of its claim. A claim left
 * behind is only held until it goes stale, so this is a courtesy. */
#define STOP_WAIT_USECS (2 * G_USEC_PER_SEC)

typedef struct {
	NemoCachePruneRules  rules;
	NemoCachePruneResult result;
	gint64               removed;
	gint64               due;
	NemoCachePruneDone   done;
	gpointer             done_data;
} PrunePass;

static guint         check_id = 0;
static GCancellable *running = NULL;
static gint          in_thread = 0;
static gint64        known_due = 0;	/* epoch seconds, 0 until a pass has said */
static gint64        started = 0;	/* monotonic seconds */

static void
pass_in_thread (GTask        *task,
		gpointer      source,
		gpointer      task_data,
		GCancellable *cancellable)
{
	PrunePass *pass = task_data;

	(void) source;

	/* A pass reads the draw times, and the last few are still in memory.
	 * Here rather than on the window's thread, since it waits on the file. */
	nemo_cache_db_flush (nemo_cache_db_get ());

	pass->result = nemo_cache_db_prune (&pass->rules, cancellable, &pass->removed, &pass->due);

	g_atomic_int_set (&in_thread, 0);
	g_task_return_boolean (task, TRUE);
}

static void
pass_done (GObject *source, GAsyncResult *res, gpointer user_data)
{
	PrunePass *pass = g_task_get_task_data (G_TASK (res));
	gint64     now = g_get_real_time () / G_USEC_PER_SEC;

	(void) source;
	(void) user_data;

	switch (pass->result) {
	case NEMO_CACHE_PRUNE_DONE:
		g_debug ("file cache cleaned up, %" G_GINT64_FORMAT " thumbnails dropped",
			 pass->removed);
		known_due = pass->due;
		break;
	case NEMO_CACHE_PRUNE_NOT_DUE:
	case NEMO_CACHE_PRUNE_FAILED:
		known_due = pass->due;
		break;
	case NEMO_CACHE_PRUNE_BUSY:
		known_due = now + BUSY_RETRY_SECS;
		break;
	case NEMO_CACHE_PRUNE_DAMAGED:
		/* Nothing more to do with it until the next launch starts it over. */
		known_due = G_MAXINT64;
		break;
	case NEMO_CACHE_PRUNE_CANCELLED:
	default:
		break;
	}

	g_clear_object (&running);

	if (pass->done != NULL)
		pass->done (pass->result, pass->removed, pass->done_data);
}

static NemoCachePruneRules
read_rules (void)
{
	NemoConfigGroup    *group = nemo_config_get_group (NEMO_FILE_CACHE_GROUP);
	NemoCachePruneRules rules = { 0 };
	gdouble             gib = nemo_config_get_double (group, NEMO_FILE_CACHE_MAX_SIZE_GIB);
	gint                days = nemo_config_get_int (group, NEMO_FILE_CACHE_MAX_AGE_DAYS);
	gint                low = nemo_config_get_int (group, NEMO_FILE_CACHE_PRUNE_MIN_HOURS);
	gint                high = nemo_config_get_int (group, NEMO_FILE_CACHE_PRUNE_MAX_HOURS);

	/* Anything under a megabyte is a typo, not a limit. */
	rules.max_bytes = gib > 0 ? MAX ((gint64) (gib * 1024 * 1024 * 1024), 1024 * 1024) : 0;
	rules.max_age_secs = days > 0 ? (gint64) days * 24 * 60 * 60 : 0;
	rules.drop_missing = nemo_config_get_boolean (group, NEMO_FILE_CACHE_DROP_MISSING);

	/* An hour at the least, so a zero cannot make every check a pass. */
	low = MAX (low, 1);
	high = MAX (high, low);
	rules.gap_min_secs = (gint64) low * 60 * 60;
	rules.gap_max_secs = (gint64) high * 60 * 60;

	rules.now = g_get_real_time () / G_USEC_PER_SEC;

	return rules;
}

static gboolean
is_idle (void)
{
	gint   minutes = nemo_config_get_int (nemo_config_get_group (NEMO_FILE_CACHE_GROUP),
					      NEMO_FILE_CACHE_PRUNE_IDLE_MINUTES);
	gint64 quiet_since = MAX (nemo_cache_db_last_used (), started);
	gint64 now = g_get_monotonic_time () / G_USEC_PER_SEC;

	return minutes <= 0 || now - quiet_since >= (gint64) minutes * 60;
}

static void
start_pass (gboolean force, NemoCachePruneDone done, gpointer done_data)
{
	PrunePass *pass;
	GTask     *task;

	pass = g_new0 (PrunePass, 1);
	pass->rules = read_rules ();
	pass->rules.force = force;
	pass->done = done;
	pass->done_data = done_data;

	running = g_cancellable_new ();
	g_atomic_int_set (&in_thread, 1);

	task = g_task_new (NULL, running, pass_done, NULL);
	g_task_set_task_data (task, pass, g_free);
	g_task_set_priority (task, force ? G_PRIORITY_DEFAULT : G_PRIORITY_LOW);
	g_task_run_in_thread (task, pass_in_thread);
	g_object_unref (task);
}

static gboolean
check_due (gpointer user_data)
{
	(void) user_data;

	if (running != NULL || !is_idle ())
		return G_SOURCE_CONTINUE;

	if (known_due != 0 && g_get_real_time () / G_USEC_PER_SEC < known_due)
		return G_SOURCE_CONTINUE;

	start_pass (FALSE, NULL, NULL);

	return G_SOURCE_CONTINUE;
}

gboolean
nemo_cache_db_prune_now (NemoCachePruneDone done, gpointer user_data)
{
	if (running != NULL)
		return FALSE;

	start_pass (TRUE, done, user_data);

	return TRUE;
}

gboolean
nemo_cache_db_prune_running (void)
{
	return running != NULL;
}

void
nemo_cache_db_prune_schedule (void)
{
	if (check_id != 0)
		return;

	started = g_get_monotonic_time () / G_USEC_PER_SEC;
	check_id = g_timeout_add_seconds (CHECK_SECS, check_due, NULL);
}

void
nemo_cache_db_prune_stop (void)
{
	gint64 deadline;

	if (check_id != 0) {
		g_source_remove (check_id);
		check_id = 0;
	}

	if (running == NULL)
		return;

	g_cancellable_cancel (running);

	deadline = g_get_monotonic_time () + STOP_WAIT_USECS;
	while (g_atomic_int_get (&in_thread) && g_get_monotonic_time () < deadline)
		g_usleep (10 * 1000);
}
