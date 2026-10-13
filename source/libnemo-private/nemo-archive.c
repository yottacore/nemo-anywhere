/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */

/* nemo-archive.c - create an archive from a selection.

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
#include "nemo-archive.h"

#include <glib/gi18n.h>

#include "arc-compress.h"
#include "nemo-archive-host.h"
#include "nemo-file-changes-queue.h"
#include "nemo-job-queue.h"
#include "nemo-progress-info.h"

/* The archive core writes it, on the job's thread. What stays here is the
   queueing, the progress window and the callback. */
typedef struct {
	NemoArchiveHostJob  on;		/* what the host's calls reach */
	ArcCompress        *work;
	GCancellable       *cancellable;
	NemoArchiveCallback done_callback;
	gpointer            done_callback_data;
} ArchiveJob;

/* Returns: (transfer full): free with g_strfreev */
char **
nemo_archive_build_command (NemoArchiveBackend        backend,
			    NemoArchiveFormat         format,
			    const NemoArchiveOptions *options,
			    const char               *program,
			    const char               *archive_path,
			    GList                    *names,
			    GList                    *leave_out)
{
	ArcSettings settings = { { NULL }, 0 };
	ArcHost host;
	char **argv;

	nemo_archive_settings_load (&settings);
	nemo_archive_host_init (&host);
	argv = arc_build_command (&settings, &host, backend, format, options, program, archive_path,
				  names, leave_out);
	arc_settings_clear (&settings);
	return argv;
}

/* Returns: (transfer full): free with g_strfreev */
char **
nemo_archive_build_links_command (NemoArchiveBackend        backend,
				  NemoArchiveFormat         format,
				  const NemoArchiveOptions *options,
				  const char               *program,
				  const char               *archive_path,
				  GList                    *names)
{
	ArcSettings settings = { { NULL }, 0 };
	ArcHost host;
	char **argv;

	nemo_archive_settings_load (&settings);
	nemo_archive_host_init (&host);
	argv = arc_build_links_command (&settings, &host, backend, format, options, program,
					archive_path, names);
	arc_settings_clear (&settings);
	return argv;
}

gboolean
nemo_archive_verify (GFile                    *archive_file,
		     GList                    *sources,
		     const NemoArchiveOptions *options,
		     NemoArchiveBackend        backend,
		     GCancellable             *cancellable,
		     char                    **reason)
{
	ArcHost host;

	nemo_archive_host_init (&host);
	return arc_verify (&host, archive_file, sources, options, backend, cancellable, reason);
}

/* Returns: (transfer full): hand it to nemo_archive_tool_stderr_finish, which joins it */
GThread *
nemo_archive_tool_stderr_start (NemoToolRun *process)
{
	ArcHost host;

	nemo_archive_host_init (&host);
	return arc_tool_stderr_start (&host, (ArcRun *) process);
}

static gboolean
archive_job_done (gpointer user_data)
{
	ArchiveJob *job = user_data;
	gboolean success = arc_compress_succeeded (job->work);

	nemo_file_changes_consume_changes (TRUE);

	arc_compress_finish (job->work);

	if (job->done_callback != NULL) {
		job->done_callback (success ? arc_compress_result (job->work) : NULL,
				    success, job->done_callback_data);
	}

	nemo_progress_info_finish (job->on.progress);

	if (job->on.parent_window != NULL) {
		g_object_remove_weak_pointer (G_OBJECT (job->on.parent_window),
					      (gpointer *) &job->on.parent_window);
	}

	arc_compress_free (job->work);
	g_clear_object (&job->on.progress);
	g_clear_object (&job->cancellable);
	g_free (job);

	return FALSE;
}

static gboolean
archive_job (GIOSchedulerJob *io_job,
	     GCancellable    *cancellable,
	     gpointer         user_data)
{
	ArchiveJob *job = user_data;

	(void) cancellable;

	nemo_progress_info_start (job->on.progress);
	arc_compress_run (job->work);

	g_io_scheduler_job_send_to_mainloop_async (io_job, archive_job_done, job, NULL);

	return FALSE;
}

/* The progress window, and the core's job with the settings as they are
   now. The archives go in with arc_compress_add. */
static ArchiveJob *
archive_job_new (GFile                    *result_file,
		 const NemoArchiveOptions *options,
		 GtkWindow                *parent_window,
		 NemoArchiveCallback       done_callback,
		 gpointer                  done_callback_data)
{
	ArchiveJob *job = g_new0 (ArchiveJob, 1);
	ArcSettings settings = { { NULL }, 0 };
	ArcHost host;

	job->done_callback = done_callback;
	job->done_callback_data = done_callback_data;

	job->on.parent_window = parent_window;
	if (parent_window != NULL) {
		g_object_add_weak_pointer (G_OBJECT (parent_window), (gpointer *) &job->on.parent_window);
	}

	job->on.progress = nemo_progress_info_new ();
	/* Comes with a ref, which archive_job_done drops. */
	job->cancellable = nemo_progress_info_get_cancellable (job->on.progress);

	nemo_archive_settings_load (&settings);
	nemo_archive_host_init_job (&host, &job->on);
	job->work = arc_compress_new (&host, &settings, options, job->cancellable, result_file);
	arc_settings_clear (&settings);

	return job;
}

/* Everything the two entry points share once the archives are in: the
   progress window's first words, and the queueing. */
static void
start_job (ArchiveJob *job)
{
	guint count = arc_compress_count (job->work);
	char *initial_details;

	nemo_progress_info_set_status (job->on.progress, _("Preparing to compress"));

	if (count > 1) {
		initial_details = g_strdup_printf (ngettext ("%d archive", "%d archives",
							     count),
						   count);
	} else {
		initial_details = g_file_get_basename (arc_compress_destination (job->work, 0));
	}
	nemo_progress_info_take_initial_details (job->on.progress, initial_details);

	if (!arc_compress_can_start (job->work)) {
		archive_job_done (job);
		return;
	}

	nemo_job_queue_add_new_job (nemo_job_queue_get (), archive_job, job,
				    job->cancellable, job->on.progress, TRUE);
}

void
nemo_archive_create (GList                    *sources,
		     GFile                    *destination,
		     const NemoArchiveOptions *options,
		     GtkWindow                *parent_window,
		     NemoArchiveCallback       done_callback,
		     gpointer                  done_callback_data)
{
	ArchiveJob *job;

	g_return_if_fail (sources != NULL);
	g_return_if_fail (G_IS_FILE (destination));
	g_return_if_fail (options != NULL);

	job = archive_job_new (destination, options, parent_window, done_callback, done_callback_data);
	arc_compress_add (job->work, sources, destination);

	start_job (job);
}

void
nemo_archive_create_each (GList                    *sources,
			  GFile                    *destination_dir,
			  const NemoArchiveOptions *options,
			  GtkWindow                *parent_window,
			  NemoArchiveCallback       done_callback,
			  gpointer                  done_callback_data)
{
	GList *chosen = NULL;		/* GFile *, borrowed */
	GList *destinations = NULL;	/* GFile *, owned */
	ArchiveJob *job;
	GList *l, *d;

	g_return_if_fail (sources != NULL);
	g_return_if_fail (G_IS_FILE (destination_dir));
	g_return_if_fail (options != NULL);

	for (l = sources; l != NULL; l = l->next) {
		GFile *source = G_FILE (l->data);
		GFile *destination;
		char *basename;
		char *name;

		basename = g_file_get_basename (source);
		name = nemo_archive_each_name (basename, options->format);
		g_free (basename);

		/* Nothing worth naming an archive after - a root, say. */
		if (name == NULL) {
			continue;
		}

		destination = g_file_get_child_for_display_name (destination_dir, name, NULL);
		if (destination == NULL) {
			destination = g_file_get_child (destination_dir, name);
		}
		g_free (name);

		chosen = g_list_append (chosen, source);
		destinations = g_list_append (destinations, destination);
	}

	if (chosen == NULL) {
		return;
	}

	job = archive_job_new (destination_dir, options, parent_window, done_callback, done_callback_data);
	for (l = chosen, d = destinations; l != NULL; l = l->next, d = d->next) {
		GList *one = g_list_append (NULL, l->data);

		arc_compress_add (job->work, one, G_FILE (d->data));
		g_list_free (one);
	}
	g_list_free (chosen);
	g_list_free_full (destinations, g_object_unref);

	start_job (job);
}
