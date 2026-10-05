/* A search run through a search folder, the way a window runs one: the folder
 * is handed a query, a monitor starts it, and the hits come back as the folder's
 * own files. The engine underneath has a test of its own. */

#include <config.h>

#include <stdlib.h>
#include <gtk/gtk.h>

#include <libnemo-private/nemo-directory.h>
#include <libnemo-private/nemo-file.h>
#include <libnemo-private/nemo-global-preferences.h>
#include <libnemo-private/nemo-search-directory.h>

#include "test-scratch.h"
#include "test-check.h"

static int loads = 0;

static void
done_loading (G_GNUC_UNUSED NemoDirectory *directory, G_GNUC_UNUSED gpointer data)
{
	loads++;
}

static gboolean
wait_for_load (int want)
{
	gint64 deadline = g_get_monotonic_time () + 30 * G_USEC_PER_SEC;

	while (loads < want && g_get_monotonic_time () < deadline) {
		g_main_context_iteration (NULL, FALSE);
		g_usleep (1000);
	}

	return loads >= want;
}

static gboolean
listed (NemoDirectory *directory, const char *name)
{
	GList *files, *l;
	gboolean hit = FALSE;

	files = nemo_directory_get_file_list (directory);
	for (l = files; l != NULL && !hit; l = l->next) {
		char *file_name = nemo_file_get_name (l->data);

		hit = g_strcmp0 (file_name, name) == 0;
		check (nemo_directory_contains_file (directory, l->data));
		g_free (file_name);
	}
	nemo_file_list_free (files);

	return hit;
}

static guint
count (NemoDirectory *directory)
{
	GList *files = nemo_directory_get_file_list (directory);
	guint n = g_list_length (files);

	nemo_file_list_free (files);
	return n;
}

static void
write_file (const char *dir, const char *name)
{
	char *path = g_build_filename (dir, name, NULL);

	if (!g_file_set_contents (path, "nothing to see\n", -1, NULL)) {
		g_printerr ("FAIL could not write %s\n", path);
		failures++;
	}

	g_free (path);
}

/* A forced reload on its own does not start the search again. The window drops
   its monitor and adds it back straight after, and that is what does. */
static void
reload (NemoDirectory *directory, gpointer client)
{
	nemo_directory_force_reload (directory);
	nemo_directory_file_monitor_remove (directory, client);
	nemo_directory_file_monitor_add (directory, client, TRUE,
					 NEMO_FILE_ATTRIBUTE_INFO, NULL, NULL);
}

int
main (int argc, char *argv[])
{
	static int client;
	NemoDirectory *directory;
	NemoQuery *query;
	char *scratch, *dir, *sub, *uri, *search_uri;

	scratch = test_scratch_config_home ("nemo-search-folder-home-XXXXXX");

	gtk_init_check (&argc, &argv);
	nemo_global_preferences_init ();

	dir = test_scratch_dir ("nemo-search-folder-XXXXXX", NULL);
	sub = g_build_filename (dir, "deeper", NULL);
	g_mkdir_with_parents (sub, 0755);

	write_file (dir, "needle-top.txt");
	write_file (sub, "needle-deep.txt");
	write_file (dir, "haystack.txt");
	write_file (dir, ".needle-hidden.txt");

	uri = g_filename_to_uri (dir, NULL, NULL);
	query = nemo_query_new ();
	nemo_query_set_location (query, uri);
	nemo_query_set_file_pattern (query, "needle");
	nemo_query_set_recurse (query, TRUE);

	search_uri = nemo_search_directory_generate_new_uri ();
	directory = nemo_directory_get_by_uri (search_uri);
	check (NEMO_IS_SEARCH_DIRECTORY (directory));
	if (!NEMO_IS_SEARCH_DIRECTORY (directory)) {
		return EXIT_FAILURE;
	}

	nemo_search_directory_set_query (NEMO_SEARCH_DIRECTORY (directory), query);
	g_object_unref (query);

	g_signal_connect (directory, "done-loading", G_CALLBACK (done_loading), NULL);
	nemo_directory_file_monitor_add (directory, &client, TRUE,
					 NEMO_FILE_ATTRIBUTE_INFO, NULL, NULL);

	check (wait_for_load (1));
	check (listed (directory, "needle-top.txt"));
	check (listed (directory, "needle-deep.txt"));
	check (!listed (directory, "haystack.txt"));
	check (!listed (directory, ".needle-hidden.txt"));
	check (count (directory) == 2);

	/* A reload starts the list over rather than adding the same hits again. */
	reload (directory, &client);
	check (wait_for_load (2));
	check (count (directory) == 2);

	/* The folder reads the hidden-files preference each time a search starts. */
	nemo_config_set_boolean (nemo_preferences, NEMO_PREFERENCES_SHOW_HIDDEN_FILES, TRUE);
#ifdef G_OS_WIN32
	nemo_config_set_boolean (nemo_windows_preferences, NEMO_PREFERENCES_SHOW_DOT_FILES, TRUE);
#endif
	reload (directory, &client);
	check (wait_for_load (3));
	check (listed (directory, ".needle-hidden.txt"));
	check (count (directory) == 3);

	nemo_directory_file_monitor_remove (directory, &client);
	nemo_directory_unref (directory);

	g_free (search_uri);
	g_free (uri);
	g_free (sub);
	g_free (dir);
	g_free (scratch);

	if (failures == 0) {
		g_print ("nemo-search-folder: all checks passed\n");
	}

	return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
