/* While the delete test guard waits on an answer, the job that asked has its
 * progress paused. Otherwise the job's "Preparing" window comes up two seconds
 * in, beside the guard dialog, the way it did before 20260924.
 *
 * Runs a real job through the job queue, finds the guard dialog when it comes
 * up, checks the pause and answers Cancel. Then again with the job already
 * paused, as by the user, which must still be paused after the answer.
 * Needs a display; exits 77 without. */

#include <config.h>

#include <stdlib.h>
#include <string.h>
#include <gtk/gtk.h>

#include <libnemo-private/nemo-delete-testguard.h>
#include <libnemo-private/nemo-global-preferences.h>
#include <libnemo-private/nemo-job-queue.h>
#include <libnemo-private/nemo-progress-info.h>
#include <libnemo-private/nemo-progress-info-manager.h>

#include "test-scratch.h"
#include "test-check.h"

#define GUARD_TITLE "Delete/overwrite test guard"

typedef struct {
	NemoProgressInfo *info;
	GFile *file;
	gboolean user_paused;
	gboolean own_info;
	gboolean go_ahead;
	gboolean paused_after;
	gboolean seen_dialog;
	gboolean paused_while_asking;
	gboolean done;
	GMainLoop *loop;
} Run;

static gboolean
job_done (gpointer user_data)
{
	Run *run = user_data;

	run->done = TRUE;
	nemo_progress_info_finish (run->info);
	g_main_loop_quit (run->loop);

	return G_SOURCE_REMOVE;
}

static gboolean
guard_job (GIOSchedulerJob *io_job,
	   GCancellable    *cancellable,
	   gpointer         user_data)
{
	Run *run = user_data;

	run->own_info = (nemo_job_queue_get_current_info () == run->info);
	nemo_progress_info_start (run->info);
	if (run->user_paused) {
		nemo_progress_info_pause (run->info);
	}

	run->go_ahead = nemo_delete_testguard_ask_one ("Delete", run->file);
	run->paused_after = nemo_progress_info_get_is_paused (run->info);

	if (run->user_paused) {
		nemo_progress_info_resume (run->info);
	}

	g_io_scheduler_job_send_to_mainloop_async (io_job, job_done, run, NULL);

	return FALSE;
}

static GtkWidget *
find_guard_dialog (void)
{
	GList *windows, *l;
	GtkWidget *found = NULL;

	windows = gtk_window_list_toplevels ();
	for (l = windows; l != NULL; l = l->next) {
		const char *title = gtk_window_get_title (l->data);

		if (GTK_IS_DIALOG (l->data) && gtk_widget_get_visible (l->data) &&
		    g_strcmp0 (title, GUARD_TITLE) == 0) {
			found = l->data;
			break;
		}
	}
	g_list_free (windows);

	return found;
}

static gboolean
answer_guard (gpointer user_data)
{
	Run *run = user_data;
	GtkWidget *dialog = find_guard_dialog ();

	if (dialog == NULL) {
		return G_SOURCE_CONTINUE;
	}

	run->seen_dialog = TRUE;
	run->paused_while_asking = nemo_progress_info_get_is_paused (run->info);
	gtk_dialog_response (GTK_DIALOG (dialog), GTK_RESPONSE_CANCEL);

	return G_SOURCE_REMOVE;
}

static gboolean
give_up (gpointer user_data)
{
	Run *run = user_data;

	g_printerr ("the job never finished\n");
	g_main_loop_quit (run->loop);

	return G_SOURCE_REMOVE;
}

static void
run_case (const char *path,
	  gboolean    user_paused)
{
	Run run = { 0 };
	guint poll_id, limit_id;

	check (nemo_job_queue_get_current_info () == NULL);

	run.info = nemo_progress_info_new ();
	run.file = g_file_new_for_path (path);
	run.user_paused = user_paused;
	run.loop = g_main_loop_new (NULL, FALSE);

	poll_id = g_timeout_add (50, answer_guard, &run);
	limit_id = g_timeout_add_seconds (20, give_up, &run);

	nemo_job_queue_add_new_job (nemo_job_queue_get (), guard_job, &run,
				    NULL, run.info, TRUE);
	g_main_loop_run (run.loop);

	if (!run.seen_dialog) {
		g_source_remove (poll_id);
	}
	if (run.done) {
		g_source_remove (limit_id);
	}

	check (run.done);
	check (run.own_info);
	check (run.seen_dialog);
	check (run.paused_while_asking);
	check (!run.go_ahead);
	/* The guard undoes its own pause and only that. */
	check (run.paused_after == user_paused);
	check (g_file_test (path, G_FILE_TEST_EXISTS));

	g_object_unref (run.file);
	g_object_unref (run.info);
	g_main_loop_unref (run.loop);
}

/* Every job question resumes only a pause it made itself, which rests on
   pause saying whether it was the one that paused. */
static void
check_pause_says_who (void)
{
	NemoProgressInfo *info = nemo_progress_info_new ();

	check (nemo_progress_info_pause (info));
	check (!nemo_progress_info_pause (info));
	check (nemo_progress_info_get_is_paused (info));
	nemo_progress_info_resume (info);
	check (!nemo_progress_info_get_is_paused (info));
	check (nemo_progress_info_pause (info));
	nemo_progress_info_resume (info);

	g_object_unref (info);
}

int
main (int argc, char *argv[])
{
	NemoProgressInfoManager *manager;
	char *tmp, *path;

	g_setenv ("NEMO_TESTGUARD_ALL_DELETES", "1", TRUE);

	if (!gtk_init_check (&argc, &argv)) {
		g_print ("SKIP: no display\n");
		return 77;
	}

	tmp = test_scratch_config_home ("nemo-guard-pause-XXXXXX");
	nemo_global_preferences_init ();

	/* Keeps each progress alive until its "finished" idle has run, so the
	   first job is off the queue's books before the second. See
	   test-nemo-link-copy-job.c. */
	manager = nemo_progress_info_manager_new ();

	path = g_build_filename (tmp, "doomed.txt", NULL);
	g_file_set_contents (path, "x", 1, NULL);

	check_pause_says_who ();
	run_case (path, FALSE);
	run_case (path, TRUE);

	g_object_unref (manager);
	g_free (path);
	g_free (tmp);

	if (failures > 0) {
		g_printerr ("%d failure(s)\n", failures);
		return EXIT_FAILURE;
	}

	g_print ("OK\n");
	return EXIT_SUCCESS;
}
