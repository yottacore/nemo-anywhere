/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */

/* test-nemo-job-queue.c - the file job queue and its overall progress.

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

/* The queue refuses the same job while it still waits to run: the check used to
 * compare the job's data against its function, so it never matched and a job
 * queued twice ran twice. And the progress shown for several operations at once
 * is their plain mean; with three or more, the running form it replaced
 * weighted the earlier ones down to almost nothing. */

#include <config.h>

#include <stdlib.h>
#include <gtk/gtk.h>

#include <libnemo-private/nemo-global-preferences.h>
#include <libnemo-private/nemo-job-queue.h>
#include <libnemo-private/nemo-progress-info.h>
#include <libnemo-private/nemo-progress-info-manager.h>

#include "test-scratch.h"
#include "test-check.h"

typedef struct {
	NemoProgressInfo *info;
	GMutex lock;
	GCond cond;
	gboolean hold;
	int runs;
} Job;

static gboolean
job_finished (gpointer data)
{
	Job *job = data;

	nemo_progress_info_finish (job->info);
	return G_SOURCE_REMOVE;
}

/* Waits while told to hold, so a second job has something to queue behind. */
static gboolean
job_func (GIOSchedulerJob *io_job,
	  G_GNUC_UNUSED GCancellable    *cancellable,
	  gpointer         data)
{
	Job *job = data;

	g_mutex_lock (&job->lock);
	job->runs++;
	while (job->hold) {
		g_cond_wait (&job->cond, &job->lock);
	}
	g_mutex_unlock (&job->lock);

	g_io_scheduler_job_send_to_mainloop_async (io_job, job_finished, job, NULL);
	return FALSE;
}

static int
runs_of (Job *job)
{
	int runs;

	g_mutex_lock (&job->lock);
	runs = job->runs;
	g_mutex_unlock (&job->lock);

	return runs;
}

static void
release (Job *job)
{
	g_mutex_lock (&job->lock);
	job->hold = FALSE;
	g_cond_signal (&job->cond);
	g_mutex_unlock (&job->lock);
}

/* Turns the main loop until the condition holds, or for the time given. */
static gboolean
spin_until (Job *job, int runs, gboolean finished, int ms)
{
	gint64 end = g_get_monotonic_time () + ms * 1000;

	while (g_get_monotonic_time () < end) {
		if (runs_of (job) == runs &&
		    (!finished || nemo_progress_info_get_is_finished (job->info))) {
			return TRUE;
		}
		g_main_context_iteration (NULL, FALSE);
		g_usleep (1000);
	}

	return FALSE;
}

static void
check_duplicate_job (void)
{
	NemoJobQueue *queue = nemo_job_queue_get ();
	NemoProgressInfo *again;
	Job first = { 0 }, second = { 0 };

	g_mutex_init (&first.lock);
	g_cond_init (&first.cond);
	g_mutex_init (&second.lock);
	g_cond_init (&second.cond);
	first.hold = TRUE;
	first.info = nemo_progress_info_new ();
	second.info = nemo_progress_info_new ();

	nemo_job_queue_add_new_job (queue, job_func, &first, NULL, first.info, TRUE);
	check (spin_until (&first, 1, FALSE, 5000));

	/* The second waits behind the first, and asking for it again while it
	   waits is refused. */
	nemo_job_queue_add_new_job (queue, job_func, &second, NULL, second.info, FALSE);
	check (g_list_length (nemo_job_queue_get_all_jobs (queue)) == 1);

	again = nemo_progress_info_new ();
	nemo_job_queue_add_new_job (queue, job_func, &second, NULL, again, FALSE);
	check (g_list_length (nemo_job_queue_get_all_jobs (queue)) == 1);

	release (&first);
	check (spin_until (&first, 1, TRUE, 5000));
	check (spin_until (&second, 1, TRUE, 5000));

	/* Long enough for a second copy to have started behind it. */
	spin_until (&second, 2, FALSE, 500);
	check (runs_of (&second) == 1);
	check (nemo_job_queue_get_all_jobs (queue) == NULL);

	g_object_unref (again);
	g_object_unref (second.info);
	g_object_unref (first.info);
	g_cond_clear (&first.cond);
	g_mutex_clear (&first.lock);
	g_cond_clear (&second.cond);
	g_mutex_clear (&second.lock);
	g_object_unref (queue);
}

static NemoProgressInfo *
info_at (double fraction)
{
	NemoProgressInfo *info = nemo_progress_info_new ();

	nemo_progress_info_set_progress (info, fraction * 100, 100);
	return info;
}

static void
check_mean (void)
{
	GList *infos = NULL;
	NemoProgressInfo *done;

	check (nemo_progress_info_mean_progress (NULL) == 0.0);

	infos = g_list_append (infos, info_at (0.9));
	check (G_APPROX_VALUE (nemo_progress_info_mean_progress (infos), 0.9, 1e-9));

	infos = g_list_append (infos, info_at (0.6));
	infos = g_list_append (infos, info_at (0.3));
	check (G_APPROX_VALUE (nemo_progress_info_mean_progress (infos), 0.6, 1e-9));

	/* A finished one no longer counts, whatever it stopped at. */
	done = info_at (0.1);
	nemo_progress_info_finish (done);
	infos = g_list_append (infos, done);
	check (G_APPROX_VALUE (nemo_progress_info_mean_progress (infos), 0.6, 1e-9));

	infos = g_list_append (infos, info_at (0.2));
	check (G_APPROX_VALUE (nemo_progress_info_mean_progress (infos), 0.5, 1e-9));

	g_list_free_full (infos, g_object_unref);
}

int
main (int argc, char *argv[])
{
	NemoProgressInfoManager *manager;
	char *tmp;

	tmp = test_scratch_config_home ("nemo-job-queue-XXXXXX");

	if (!gtk_init_check (&argc, &argv)) {
		g_print ("SKIP: no display\n");
		return 77;
	}
	nemo_global_preferences_init ();

	/* See test-nemo-link-copy-job.c: without a manager the queue never
	   starts the second job. */
	manager = nemo_progress_info_manager_new ();

	check_duplicate_job ();
	check_mean ();

	g_object_unref (manager);
	g_free (tmp);

	if (failures > 0) {
		g_printerr ("%d failure(s)\n", failures);
		return EXIT_FAILURE;
	}

	g_print ("OK\n");
	return EXIT_SUCCESS;
}
