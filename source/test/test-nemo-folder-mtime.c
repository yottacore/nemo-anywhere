/* A folder's row has to show the new modified time once something is added
 * to it, taken out or moved in. Nothing watching the folder's parent reports
 * that, so it rides on the same notices the file jobs send.
 */

#include <config.h>

#include <stdlib.h>
#include <glib/gstdio.h>
#include <gtk/gtk.h>

#include <libnemo-private/nemo-directory.h>
#include <libnemo-private/nemo-directory-notify.h>
#include <libnemo-private/nemo-file.h>

#include "test-scratch.h"
#include "test-check.h"

/* Each round sets its own old time and waits to see exactly that, so it knows
   the parent's monitor has caught up before the change it is testing. */
#define OLD_TIME ((time_t) 1000000000)

static int loads = 0;

static void
done_loading (G_GNUC_UNUSED NemoDirectory *directory, G_GNUC_UNUSED gpointer data)
{
	loads++;
}

static gboolean
wait_for_load (void)
{
	gint64 deadline = g_get_monotonic_time () + 20 * G_USEC_PER_SEC;

	while (loads < 1 && g_get_monotonic_time () < deadline) {
		g_main_context_iteration (NULL, FALSE);
		g_usleep (1000);
	}

	return loads >= 1;
}

/* Waits for the folder's cached time to be old (want_old) or anything else.
   A short wait is enough to call a miss; the info fetch for one local folder
   takes a few milliseconds. */
static gboolean
wait_for_mtime (NemoFile *folder, time_t old, gboolean want_old)
{
	gint64 deadline = g_get_monotonic_time () + 5 * G_USEC_PER_SEC;

	while (g_get_monotonic_time () < deadline) {
		if ((nemo_file_get_mtime (folder) == old) == want_old) {
			return TRUE;
		}
		g_main_context_iteration (NULL, FALSE);
		g_usleep (1000);
	}

	return FALSE;
}

static void
write_file (const char *path)
{
	check (g_file_set_contents (path, "x\n", -1, NULL));
}

static void
notify_one (void (*notify) (GList *), const char *path)
{
	GList *list = g_list_prepend (NULL, g_file_new_for_path (path));

	notify (list);
	g_list_free_full (list, g_object_unref);
}

/* Through GIO, since g_utime on Windows goes to msvcrt, which cannot open a
   folder to set its time. Elsewhere the monitor on the parent reports the new
   time. Windows does not, so there the change is announced the way a job would.
   Only there: on Linux the monitor's own refresh can then come after the change
   under test and pass it for the wrong reason. */
static void
make_old (const char *path, time_t old)
{
	GFile *file = g_file_new_for_path (path);

	check (g_file_set_attribute_uint64 (file, G_FILE_ATTRIBUTE_TIME_MODIFIED, (guint64) old,
					    G_FILE_QUERY_INFO_NONE, NULL, NULL));
	g_object_unref (file);
#ifdef G_OS_WIN32
	notify_one (nemo_directory_notify_files_changed, path);
#endif
}

int
main (int argc, char **argv)
{
	static int client;
	NemoDirectory *directory;
	NemoFile *box;
	GList *pairs;
	GFilePair pair;
	GFile *box_file;
	char *scratch, *tmp, *uri, *box_path, *other_path;
	char *added, *moved_from, *moved_to;

	scratch = test_scratch_config_home ("nemo-foldermtime-home-XXXXXX");

	gtk_init_check (&argc, &argv);

	tmp = test_scratch_dir ("nemo-foldermtime-XXXXXX", NULL);
	if (tmp == NULL) {
		g_printerr ("FAIL could not make a temp dir\n");
		g_free (scratch);
		return EXIT_FAILURE;
	}

	box_path = g_build_filename (tmp, "box", NULL);
	other_path = g_build_filename (tmp, "other", NULL);
	added = g_build_filename (box_path, "added.txt", NULL);
	moved_from = g_build_filename (other_path, "moved.txt", NULL);
	moved_to = g_build_filename (box_path, "moved.txt", NULL);

	check (g_mkdir (box_path, 0755) == 0);
	check (g_mkdir (other_path, 0755) == 0);
	write_file (moved_from);
	make_old (box_path, OLD_TIME);

	/* The parent is what is open, as when the folder is a row in the list. */
	uri = g_filename_to_uri (tmp, NULL, NULL);
	directory = nemo_directory_get_by_uri (uri);
	g_signal_connect (directory, "done-loading", G_CALLBACK (done_loading), NULL);
	nemo_directory_file_monitor_add (directory, &client, TRUE,
					 NEMO_FILE_ATTRIBUTE_INFO, NULL, NULL);
	check (wait_for_load ());

	box_file = g_file_new_for_path (box_path);
	box = nemo_file_get_existing (box_file);
	g_object_unref (box_file);
	check (box != NULL);
	if (box == NULL) {
		goto out;
	}
	check (wait_for_mtime (box, OLD_TIME, TRUE));

	write_file (added);
	notify_one (nemo_directory_notify_files_added, added);
	if (!wait_for_mtime (box, OLD_TIME, FALSE)) {
		g_printerr ("FAIL folder time did not change after an add\n");
		failures++;
	}

	make_old (box_path, OLD_TIME + 100);
	check (wait_for_mtime (box, OLD_TIME + 100, TRUE));
	g_unlink (added);
	notify_one (nemo_directory_notify_files_removed, added);
	if (!wait_for_mtime (box, OLD_TIME + 100, FALSE)) {
		g_printerr ("FAIL folder time did not change after a removal\n");
		failures++;
	}

	make_old (box_path, OLD_TIME + 200);
	check (wait_for_mtime (box, OLD_TIME + 200, TRUE));
	check (g_rename (moved_from, moved_to) == 0);
	pair.from = g_file_new_for_path (moved_from);
	pair.to = g_file_new_for_path (moved_to);
	pairs = g_list_prepend (NULL, &pair);
	nemo_directory_notify_files_moved (pairs);
	g_list_free (pairs);
	g_object_unref (pair.from);
	g_object_unref (pair.to);
	if (!wait_for_mtime (box, OLD_TIME + 200, FALSE)) {
		g_printerr ("FAIL folder time did not change after a move in\n");
		failures++;
	}

	nemo_file_unref (box);
out:
	nemo_directory_file_monitor_remove (directory, &client);
	nemo_directory_unref (directory);

	g_unlink (moved_to);
	g_unlink (moved_from);
	g_unlink (added);
	g_rmdir (box_path);
	g_rmdir (other_path);
	g_rmdir (tmp);

	g_free (added);
	g_free (moved_from);
	g_free (moved_to);
	g_free (box_path);
	g_free (other_path);
	g_free (uri);
	g_free (tmp);
	g_free (scratch);

	if (failures == 0) {
		g_print ("nemo-folder-mtime: all checks passed\n");
	}

	return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
