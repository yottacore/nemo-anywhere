/* The live half of the directory engine: a monitor is handed the folder's
 * contents and then keeps up with what changes on disk underneath it.
 * call_when_ready, the one-shot half, is covered by the load-clean test.
 */

#include <config.h>

#include <stdlib.h>
#include <glib/gstdio.h>
#include <gtk/gtk.h>

#include <libnemo-private/nemo-directory.h>
#include <libnemo-private/nemo-file.h>

#include "test-scratch.h"
#include "test-check.h"

static int loads = 0;
static GList *added = NULL;	/* names, owned */

static void
files_added (G_GNUC_UNUSED NemoDirectory *directory, GList *files, G_GNUC_UNUSED gpointer data)
{
	GList *l;

	for (l = files; l != NULL; l = l->next) {
		char *name = nemo_file_get_name (l->data);

		check (name != NULL);
		if (name != NULL) {
			added = g_list_prepend (added, name);
		}
	}
}

static void
done_loading (G_GNUC_UNUSED NemoDirectory *directory, G_GNUC_UNUSED gpointer data)
{
	loads++;
}

static gboolean
seen (const char *name)
{
	return g_list_find_custom (added, name, (GCompareFunc) g_strcmp0) != NULL;
}

static gboolean
wait_for_load (int want)
{
	gint64 deadline = g_get_monotonic_time () + 20 * G_USEC_PER_SEC;

	while (loads < want && g_get_monotonic_time () < deadline) {
		g_main_context_iteration (NULL, FALSE);
		g_usleep (1000);
	}

	return loads >= want;
}

static gboolean
wait_for_name (const char *name)
{
	gint64 deadline = g_get_monotonic_time () + 20 * G_USEC_PER_SEC;

	while (!seen (name) && g_get_monotonic_time () < deadline) {
		g_main_context_iteration (NULL, FALSE);
		g_usleep (1000);
	}

	return seen (name);
}

static void
write_file (const char *dir, const char *name)
{
	char *path = g_build_filename (dir, name, NULL);

	if (!g_file_set_contents (path, "x\n", -1, NULL)) {
		g_printerr ("FAIL could not write %s\n", path);
		failures++;
	}

	g_free (path);
}

static void
remove_file (const char *dir, const char *name)
{
	char *path = g_build_filename (dir, name, NULL);

	g_unlink (path);
	g_free (path);
}

int
main (int argc, char **argv)
{
	static int client;
	NemoDirectory *directory;
	char *scratch, *tmp, *uri;
	GList *files;

	scratch = test_scratch_config_home ("nemo-dirmonitor-home-XXXXXX");

	gtk_init_check (&argc, &argv);

	tmp = test_scratch_dir ("nemo-dirmonitor-XXXXXX", NULL);
	if (tmp == NULL) {
		g_printerr ("FAIL could not make a temp dir\n");
		g_free (scratch);
		return EXIT_FAILURE;
	}

	write_file (tmp, "one.txt");
	write_file (tmp, "two.txt");
	write_file (tmp, "three.txt");

	uri = g_filename_to_uri (tmp, NULL, NULL);
	directory = nemo_directory_get_by_uri (uri);

	g_signal_connect (directory, "files-added", G_CALLBACK (files_added), NULL);
	g_signal_connect (directory, "done-loading", G_CALLBACK (done_loading), NULL);

	nemo_directory_file_monitor_add (directory, &client, TRUE,
					 NEMO_FILE_ATTRIBUTE_INFO, NULL, NULL);

	check (wait_for_load (1));
	check (seen ("one.txt"));
	check (seen ("two.txt"));
	check (seen ("three.txt"));

	/* A file that appears after the listing has to arrive on its own. */
	write_file (tmp, "late.txt");
	check (wait_for_name ("late.txt"));

	/* A forced reload has to finish, not just start. */
	nemo_directory_force_reload (directory);
	check (wait_for_load (2));

	files = nemo_directory_get_file_list (directory);
	check (g_list_length (files) == 4);
	nemo_file_list_free (files);

	nemo_directory_file_monitor_remove (directory, &client);
	nemo_directory_unref (directory);

	g_list_free_full (added, g_free);
	g_free (uri);

	remove_file (tmp, "one.txt");
	remove_file (tmp, "two.txt");
	remove_file (tmp, "three.txt");
	remove_file (tmp, "late.txt");
	g_rmdir (tmp);
	g_free (tmp);
	g_free (scratch);

	if (failures == 0) {
		g_print ("nemo-directory-monitor: all checks passed\n");
	}

	return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
