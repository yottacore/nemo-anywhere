/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */

/* test-nemo-archive-stop-time.c - how long a stopped compress takes to end.

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

/* A tar entry's header gives its size, so on the way out libarchive fills what
 * is left of an open entry with zeros, through the compressor. Once a stop
 * let those zeros go all the way, a stop near the start of a big file took
 * as long as compressing the rest of it would have. A stop has to end in about
 * the same time whatever was left to write.
 *
 * So in each compressed tar format and the 7z, a 4 GiB file and a 16 GiB
 * one, sparse so they cost no disk, are each stopped at their first progress
 * report. The time from the stop to the job's end for the big one has to be
 * under twice that for the small one, plus some slack for a busy box. With
 * the zeros going all the way, the tar.gz took 15 s and 56 s, and the tar.xz
 * 5 s and 20 s. Without, the tar.gz takes about 0.1 s either way, and the
 * tar.xz under 3 s, where xz has to fill one output buffer with zeros before
 * a write fails.
 *
 * The built-in 7z writer pads the same way, but into a temporary file of its
 * own, so how the job treats the output does not reach it. A 2 GB file took
 * 24 s. Link storing is off here, since with it on a 7z goes to the 7-Zip
 * program where it is installed. */

#include <config.h>

#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <glib/gstdio.h>
#include <gtk/gtk.h>

#include <libnemo-private/nemo-archive.h>
#include <libnemo-private/nemo-global-preferences.h>
#include <libnemo-private/nemo-progress-info.h>
#include <libnemo-private/nemo-progress-info-manager.h>

#include "test-scratch.h"
#include "test-check.h"

#define GIB ((gint64) 1024 * 1024 * 1024)
#define SLACK_SECONDS 3.0
#define JOB_TIMEOUT_SECONDS 100

/* Both times how slow the box is. An emulated arm64 box takes about a minute
   for the tar.xz stop, against under 3 s on a normal one. */
static double slack;
static guint job_timeout;

typedef struct {
	NemoProgressInfo *info;
	gint64 stopped_at;
	gint64 done_at;
	gboolean succeeded;
	gboolean done;
} StopCase;

static void
stop_once_writing (NemoProgressInfo *info, gpointer data)
{
	StopCase *stop = data;

	if (info == stop->info && stop->stopped_at == 0 &&
	    nemo_progress_info_get_progress (info) > 0) {
		stop->stopped_at = g_get_monotonic_time ();
		nemo_progress_info_cancel (info);
	}
}

static void
watch_new_progress (NemoProgressInfoManager *manager,
		    NemoProgressInfo        *info,
		    gpointer                 data)
{
	StopCase *stop = data;

	(void) manager;
	if (stop->info == NULL) {
		stop->info = g_object_ref (info);
		g_signal_connect (info, "progress-changed", G_CALLBACK (stop_once_writing), stop);
	}
}

static void
compress_done (GFile *result, gboolean success, gpointer data)
{
	StopCase *stop = data;

	(void) result;
	stop->done_at = g_get_monotonic_time ();
	stop->succeeded = success;
	stop->done = TRUE;
	gtk_main_quit ();
}

static gboolean
give_up (gpointer data)
{
	(void) data;
	g_printerr ("FAIL: the job did not end within %u seconds\n", job_timeout);
	failures++;
	gtk_main_quit ();
	return G_SOURCE_REMOVE;
}

/* Seconds from the stop to the end, or -1 where the job did not stop. */
static double
time_stop (NemoProgressInfoManager *manager,
	   GtkWidget               *window,
	   GFile                   *source,
	   const char              *tmp,
	   NemoArchiveFormat        format,
	   const char              *name)
{
	char *path = g_strdup_printf ("%s/stopped.%s", tmp, name);
	GFile *destination = g_file_new_for_path (path);
	GList *sources = g_list_append (NULL, source);
	NemoArchiveOptions options;
	StopCase stop = { 0 };
	gulong watch_id;
	guint timeout_id;
	double seconds = -1;

	nemo_archive_options_init (&options);
	options.format = format;
	options.store_links = FALSE;
	check (nemo_archive_pick_backend (format, &options) == NEMO_ARCHIVE_BACKEND_LIBARCHIVE);

	watch_id = g_signal_connect (manager, "new-progress-info",
				     G_CALLBACK (watch_new_progress), &stop);
	timeout_id = g_timeout_add_seconds (job_timeout, give_up, NULL);
	nemo_archive_create (sources, destination, &options, GTK_WINDOW (window),
			     compress_done, &stop);
	gtk_main ();
	if (stop.done) {
		g_source_remove (timeout_id);
	}
	g_signal_handler_disconnect (manager, watch_id);

	/* A job that ends quickly can still have a progress report queued, and
	   the next case's StopCase may sit at this one's address. */
	if (stop.info != NULL) {
		g_signal_handlers_disconnect_by_data (stop.info, &stop);
		g_object_unref (stop.info);
	}

	check (stop.done && stop.stopped_at != 0 && !stop.succeeded);
	check (!g_file_test (path, G_FILE_TEST_EXISTS));

	if (stop.done && stop.stopped_at != 0) {
		seconds = (double) (stop.done_at - stop.stopped_at) / G_USEC_PER_SEC;
	}

	nemo_archive_options_clear (&options);
	g_list_free (sources);
	g_object_unref (destination);
	g_free (path);

	return seconds;
}

static GFile *
make_sparse_file (const char *tmp, const char *name, gint64 size)
{
	char *path = g_build_filename (tmp, name, NULL);
	GFile *file = g_file_new_for_path (path);
	int fd;

	fd = g_open (path, O_WRONLY | O_CREAT | O_TRUNC, 0600);
	check (fd >= 0);
	if (fd >= 0) {
		check (ftruncate (fd, size) == 0);
		close (fd);
	}
	g_free (path);

	return file;
}

static void
check_format (NemoProgressInfoManager *manager,
	      GtkWidget               *window,
	      GFile                   *small,
	      GFile                   *big,
	      const char              *tmp,
	      NemoArchiveFormat        format,
	      const char              *name)
{
	double small_seconds = time_stop (manager, window, small, tmp, format, name);
	double big_seconds = time_stop (manager, window, big, tmp, format, name);

	g_print ("%s: %.2f s from the stop to the end with 4 GiB, %.2f s with 16 GiB\n",
		 name, small_seconds, big_seconds);
	if (small_seconds >= 0 && big_seconds >= 0) {
		check (big_seconds < 2 * small_seconds + slack);
	}
}

int
main (int argc, char *argv[])
{
	NemoProgressInfoManager *manager;
	GtkWidget *window;
	GFile *small;
	GFile *big;
	char *tmp;

	tmp = test_scratch_config_home ("nemo-stop-time-XXXXXX");
	slack = SLACK_SECONDS * test_slowness ();
	job_timeout = (guint) (JOB_TIMEOUT_SECONDS * test_slowness ());

	if (!gtk_init_check (&argc, &argv)) {
		g_print ("SKIP: no display\n");
		g_free (tmp);
		return 77;
	}
	nemo_global_preferences_init ();
	manager = nemo_progress_info_manager_new ();
	window = gtk_window_new (GTK_WINDOW_TOPLEVEL);

	small = make_sparse_file (tmp, "small.bin", 4 * GIB);
	big = make_sparse_file (tmp, "big.bin", 16 * GIB);

	check_format (manager, window, small, big, tmp, NEMO_ARCHIVE_FORMAT_TAR_GZ, "tar.gz");
	check_format (manager, window, small, big, tmp, NEMO_ARCHIVE_FORMAT_TAR_XZ, "tar.xz");
	check_format (manager, window, small, big, tmp, NEMO_ARCHIVE_FORMAT_7Z, "7z");

	g_object_unref (small);
	g_object_unref (big);
	gtk_widget_destroy (window);
	g_object_unref (manager);
	g_free (tmp);

	if (failures > 0) {
		g_printerr ("%d failure(s)\n", failures);
		return EXIT_FAILURE;
	}

	g_print ("OK\n");
	return EXIT_SUCCESS;
}
