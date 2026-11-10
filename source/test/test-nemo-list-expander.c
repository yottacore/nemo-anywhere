/* The expander on a folder row. It has to go when the folder turns out to be
 * empty, and come back when something is put in it. The second half is the one
 * that was missing: the model only ever took the expander away. */

#include <config.h>

#include <gtk/gtk.h>
#include <glib/gstdio.h>

#include <libnemo-private/nemo-config.h>
#include <libnemo-private/nemo-file-private.h>
#include <libnemo-private/nemo-global-preferences.h>

#include "nemo-list-model.h"

#include "test-scratch.h"
#include "test-check.h"

static NemoFile *
file_at (const char *root, const char *relative)
{
	char *path, *uri;
	NemoFile *file;

	path = g_build_filename (root, relative, NULL);
	uri = g_filename_to_uri (path, NULL, NULL);
	file = nemo_file_get_by_uri (uri);

	g_free (uri);
	g_free (path);

	return file;
}

/* The count is read in the background, so spin until it says what the folder
 * on disk says. Returns FALSE on giving up, which reads as a failure rather
 * than a hang. */
static gboolean
wait_for_count (NemoFile *file, guint want)
{
	guint count;
	gboolean unreadable;
	int spins;

	for (spins = 0; spins < 3000; spins++) {
		if (nemo_file_get_directory_item_count (file, &count, &unreadable)
		    && count == want) {
			return TRUE;
		}
		g_main_context_iteration (NULL, FALSE);
		g_usleep (2000);
	}

	return FALSE;
}

/* The model is fed from a loaded directory, and a NemoFile doesn't know anything
 * about itself, even that it's a folder, until that load has gotten to it. */
static gboolean
wait_for_directory (NemoDirectory *directory)
{
	int spins;

	for (spins = 0; spins < 3000; spins++) {
		if (nemo_directory_are_all_files_seen (directory)) {
			return TRUE;
		}
		g_main_context_iteration (NULL, FALSE);
		g_usleep (2000);
	}

	return FALSE;
}

static gboolean
row_has_expander (NemoListModel *model, NemoFile *file, NemoDirectory *directory)
{
	GtkTreeIter iter;

	if (!nemo_list_model_get_tree_iter_from_file (model, file, directory, &iter)) {
		return FALSE;
	}

	return gtk_tree_model_iter_has_child (GTK_TREE_MODEL (model), &iter);
}

int
main (int argc, char *argv[])
{
	NemoListModel *model;
	NemoColumn *column;
	NemoDirectory *directory;
	NemoFile *empty, *full;
	char *tmp, *uri, *child;
	int client;

	tmp = test_scratch_config_home ("nemo-list-expander-test-XXXXXX");

	gtk_init (&argc, &argv);
	nemo_global_preferences_init ();

	{
		char *a = g_build_filename (tmp, "empty", NULL);
		char *b = g_build_filename (tmp, "full", "inner", NULL);

		g_mkdir_with_parents (a, 0755);
		g_mkdir_with_parents (b, 0755);

		g_free (a);
		g_free (b);
	}

	uri = g_filename_to_uri (tmp, NULL, NULL);
	directory = nemo_directory_get_by_uri (uri);
	g_free (uri);

	nemo_directory_file_monitor_add (directory, &client, TRUE,
					 NEMO_FILE_ATTRIBUTE_INFO
					 | NEMO_FILE_ATTRIBUTE_DIRECTORY_ITEM_COUNT,
					 NULL, NULL);
	check (wait_for_directory (directory));

	model = g_object_new (NEMO_TYPE_LIST_MODEL, NULL);
	column = nemo_column_new ("name", "name", "Name", "");
	nemo_list_model_add_column (model, column);

	empty = file_at (tmp, "empty");
	full = file_at (tmp, "full");

	check (nemo_file_is_directory (empty));
	check (nemo_file_is_directory (full));

	check (nemo_list_model_add_file (model, empty, directory));
	check (nemo_list_model_add_file (model, full, directory));

	/* Once the counts are in, the empty folder has no expander and the other
	   one does. */
	check (wait_for_count (empty, 0));
	check (wait_for_count (full, 1));
	nemo_list_model_file_changed (model, empty, directory);
	nemo_list_model_file_changed (model, full, directory);
	check (!row_has_expander (model, empty, directory));
	check (row_has_expander (model, full, directory));

	/* Something is put in it. A move or a copy the app made invalidates the
	   count, which is the only word the model gets. */
	child = g_build_filename (tmp, "empty", "arrived", NULL);
	g_mkdir (child, 0755);
	g_free (child);

	nemo_file_invalidate_count_and_mime_list (empty);
	check (wait_for_count (empty, 1));
	nemo_list_model_file_changed (model, empty, directory);
	check (row_has_expander (model, empty, directory));

	/* And away again when it is emptied, so the 2 halves stay symmetric. */
	child = g_build_filename (tmp, "empty", "arrived", NULL);
	g_rmdir (child);
	g_free (child);

	nemo_file_invalidate_count_and_mime_list (empty);
	check (wait_for_count (empty, 0));
	nemo_list_model_file_changed (model, empty, directory);
	check (!row_has_expander (model, empty, directory));

	nemo_directory_file_monitor_remove (directory, &client);
	nemo_list_model_clear (model);

	nemo_file_unref (empty);
	nemo_file_unref (full);
	nemo_directory_unref (directory);
	g_object_unref (column);
	g_object_unref (model);

	nemo_config_shutdown ();
	g_free (tmp);

	if (failures == 0)
		g_print ("nemo-list-expander: all checks passed\n");

	return failures == 0 ? 0 : 1;
}
