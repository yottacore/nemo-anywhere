#include "test.h"

#include <libnemo-private/nemo-file-operations.h>
#include <libnemo-private/nemo-progress-info.h>
#include <libnemo-private/nemo-progress-info-manager.h>

#include <glib/gstdio.h>
#include <stdlib.h>

#include "test-scratch.h"
#include "test-check.h"

/* Sources and destination are built here rather than taken from the command
 * line: with no arguments this used to print a usage line and fail, and with
 * arguments it asserted nothing - the copy could do anything at all and the
 * run still counted as a pass. */

#define COPY_TIMEOUT_SECONDS 20

static gboolean copy_finished;
static gboolean copy_succeeded;

static void
copy_done (G_GNUC_UNUSED GHashTable *debuting_uris,
           gboolean success,
           G_GNUC_UNUSED gpointer data)
{
	copy_succeeded = success;
	copy_finished = TRUE;
	gtk_main_quit ();
}

static gboolean
give_up (G_GNUC_UNUSED gpointer data)
{
	g_printerr ("FAIL: copy did not finish within %d seconds\n",
		    COPY_TIMEOUT_SECONDS);
	failures++;
	gtk_main_quit ();
	return G_SOURCE_REMOVE;
}

static char *
write_file (const char *dir, const char *name, const char *contents)
{
	char *path = g_build_filename (dir, name, NULL);

	check (g_file_set_contents (path, contents, -1, NULL));
	return path;
}

int
main (int argc, char* argv[])
{
	GtkWidget *window;
	GList *sources = NULL;
	GFile *dest;
	char *tmp, *src_dir, *dst_dir, *src_file, *dest_file, *contents = NULL;
	guint timeout_id;

	test_init (&argc, &argv);

	tmp = test_scratch_dir ("nemo-copy-test-XXXXXX", NULL);
	src_dir = g_build_filename (tmp, "from", NULL);
	dst_dir = g_build_filename (tmp, "to", NULL);
	g_mkdir_with_parents (src_dir, 0700);
	g_mkdir_with_parents (dst_dir, 0700);

	src_file = write_file (src_dir, "copied.txt", "payload");
	sources = g_list_prepend (sources, g_file_new_for_path (src_file));
	dest = g_file_new_for_path (dst_dir);

	window = test_window_new ("copy test", 5);
	gtk_widget_show (window);

	nemo_file_operations_copy (sources,
				       NULL /* GArray *relative_item_points */,
				       dest,
				       GTK_WINDOW (window),
				       copy_done, NULL);

	/* The operation runs on a worker; without a deadline a hang here would
	   sit in the suite until meson's own timeout killed it. */
	timeout_id = g_timeout_add_seconds (COPY_TIMEOUT_SECONDS, give_up, NULL);
	gtk_main ();
	g_source_remove (timeout_id);

	check (copy_finished);
	check (copy_succeeded);

	dest_file = g_build_filename (dst_dir, "copied.txt", NULL);
	check (g_file_test (dest_file, G_FILE_TEST_IS_REGULAR));
	if (g_file_get_contents (dest_file, &contents, NULL, NULL)) {
		check (g_strcmp0 (contents, "payload") == 0);
		g_free (contents);
	} else {
		check (FALSE);
	}

	/* The source is a copy, not a move. */
	check (g_file_test (src_file, G_FILE_TEST_IS_REGULAR));

	g_remove (dest_file);
	g_remove (src_file);
	g_rmdir (dst_dir);
	g_rmdir (src_dir);
	g_rmdir (tmp);

	g_free (dest_file);
	g_free (src_file);
	g_free (src_dir);
	g_free (dst_dir);
	g_free (tmp);
	g_list_free_full (sources, g_object_unref);
	g_object_unref (dest);

	if (failures == 0) {
		g_print ("copy: all checks passed\n");
	}

	return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
