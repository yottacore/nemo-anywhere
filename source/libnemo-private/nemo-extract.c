/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */

/* nemo-extract.c - unpack an archive.

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
#include "nemo-extract.h"

#include <glib/gi18n.h>

#include "arc-command.h"
#include "nemo-archive-host.h"
#include "nemo-file-changes-queue.h"
#include "nemo-job-queue.h"
#include "nemo-progress-info.h"

/* The archive core unpacks it, on the job's thread. What stays here is the
   queueing, the progress window and the callback. */
typedef struct {
	NemoArchiveHostJob  on;		/* what the host's calls reach */
	ArcExtract         *work;
	GCancellable       *cancellable;
	NemoExtractCallback done_callback;
	gpointer            done_callback_data;
} ExtractJob;

/* Returns: (transfer full): free with g_strfreev */
char **
nemo_extract_build_command (NemoExtractBackend  backend,
			    const char         *program,
			    const char         *archive_path,
			    const char         *destination_path,
			    const char         *password)
{
	ArcSettings settings = { { NULL }, 0 };
	ArcHost host;
	char **argv;

	nemo_archive_settings_load (&settings);
	nemo_archive_host_init (&host);
	argv = arc_build_extract_command (&settings, &host, backend, program, archive_path,
					  destination_path, password);
	arc_settings_clear (&settings);
	return argv;
}

static gboolean
extract_job_done (gpointer user_data)
{
	ExtractJob *job = user_data;

	nemo_file_changes_consume_changes (TRUE);

	arc_extract_finish (job->work);

	if (job->done_callback != NULL) {
		job->done_callback (arc_extract_destination (job->work),
				    arc_extract_succeeded (job->work), job->done_callback_data);
	}

	nemo_progress_info_finish (job->on.progress);

	if (job->on.parent_window != NULL) {
		g_object_remove_weak_pointer (G_OBJECT (job->on.parent_window),
					      (gpointer *) &job->on.parent_window);
	}

	arc_extract_free (job->work);
	g_clear_object (&job->on.progress);
	g_clear_object (&job->cancellable);
	g_free (job);

	return FALSE;
}

static gboolean
extract_job (GIOSchedulerJob *io_job,
	     GCancellable    *cancellable,
	     gpointer         user_data)
{
	ExtractJob *job = user_data;

	(void) cancellable;

	/* The core's questions wait on the main loop through it. */
	job->on.io_job = io_job;
	nemo_progress_info_start (job->on.progress);
	arc_extract_run (job->work);

	g_io_scheduler_job_send_to_mainloop_async (io_job, extract_job_done, job, NULL);

	return FALSE;
}

void
nemo_extract_files (GList               *archives,
		    GFile               *destination_dir,
		    NemoExtractLayout    layout,
		    GtkWindow           *parent_window,
		    NemoExtractCallback  done_callback,
		    gpointer             done_callback_data)
{
	ExtractJob *job;
	ArcSettings settings = { { NULL }, 0 };
	ArcHost host;

	g_return_if_fail (archives != NULL);
	g_return_if_fail (G_IS_FILE (destination_dir));

	job = g_new0 (ExtractJob, 1);
	job->done_callback = done_callback;
	job->done_callback_data = done_callback_data;

	job->on.parent_window = parent_window;
	if (parent_window != NULL) {
		g_object_add_weak_pointer (G_OBJECT (parent_window), (gpointer *) &job->on.parent_window);
	}

	job->on.progress = nemo_progress_info_new ();
	/* Comes with a ref, which extract_job_done drops. */
	job->cancellable = nemo_progress_info_get_cancellable (job->on.progress);

	nemo_archive_settings_load (&settings);
	nemo_archive_host_init_job (&host, &job->on);
	job->work = arc_extract_new (&host, &settings, archives, destination_dir, layout,
				     job->cancellable);
	arc_settings_clear (&settings);

	nemo_progress_info_set_status (job->on.progress, _("Preparing to unpack"));
	nemo_progress_info_take_initial_details (job->on.progress,
						 g_file_get_basename (G_FILE (archives->data)));

	nemo_job_queue_add_new_job (nemo_job_queue_get (), extract_job, job,
				    job->cancellable, job->on.progress, TRUE);
}
