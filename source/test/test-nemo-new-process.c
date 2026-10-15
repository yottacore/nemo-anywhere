/* The command line one copy hands another to show a location, and the
 * fallback when a selection cannot be said on one. Also the one for a tab
 * moved out to a window of its own. */

#include <config.h>

#include <gio/gio.h>

#include "nemo-new-process.h"
#include <libnemo-private/nemo-file-utilities.h>
#include "test-check.h"

int
main (void)
{
	GFile *folder = g_file_new_for_path ("/some/folder");
	GFile *item = g_file_new_for_path ("/some/folder/item.txt");
	GFile *elsewhere = g_file_new_for_path ("/other/place");
	char *exe = nemo_get_exe_path ();
	char **args;

	/* The first word is this very program, by full path. */
	check (exe != NULL);
	check (exe != NULL && g_path_is_absolute (exe));
	check (exe != NULL && g_file_test (exe, G_FILE_TEST_IS_EXECUTABLE));

	args = nemo_new_process_argv (folder, NULL);
	check (g_strv_length (args) == 2);
	check (g_strcmp0 (args[0], exe) == 0);
	check (g_strcmp0 (args[1], "file:///some/folder") == 0);
	g_strfreev (args);

	/* An item in the folder asked for: the folder is implied by --select. */
	args = nemo_new_process_argv (folder, item);
	check (g_strv_length (args) == 3);
	check (g_strcmp0 (args[1], "--select") == 0);
	check (g_strcmp0 (args[2], "file:///some/folder/item.txt") == 0);
	g_strfreev (args);

	/* A selection with no folder given still works. */
	args = nemo_new_process_argv (NULL, item);
	check (g_strv_length (args) == 3);
	check (g_strcmp0 (args[1], "--select") == 0);
	g_strfreev (args);

	/* An item outside the folder cannot be said: the folder wins. */
	args = nemo_new_process_argv (elsewhere, item);
	check (g_strv_length (args) == 2);
	check (g_strcmp0 (args[1], "file:///other/place") == 0);
	g_strfreev (args);

	/* Nothing at all opens the default place. */
	args = nemo_new_process_argv (NULL, NULL);
	check (g_strv_length (args) == 1);
	g_strfreev (args);

	/* A tab moved out to a window of its own brings its view and every
	   selected item, and the folder still comes last. */
	{
		char *selected[] = { (char *) "file:///some/folder/a", (char *) "file:///some/folder/b", NULL };

		args = nemo_new_process_argv_tab (folder, "OAFIID:Nemo_File_Manager_List_View", selected);
		check (g_strv_length (args) == 8);
		check (g_strcmp0 (args[0], exe) == 0);
		check (g_strcmp0 (args[1], "--tab-view") == 0);
		check (g_strcmp0 (args[2], "OAFIID:Nemo_File_Manager_List_View") == 0);
		check (g_strcmp0 (args[3], "--tab-select") == 0);
		check (g_strcmp0 (args[4], "file:///some/folder/a") == 0);
		check (g_strcmp0 (args[5], "--tab-select") == 0);
		check (g_strcmp0 (args[6], "file:///some/folder/b") == 0);
		check (g_strcmp0 (args[7], "file:///some/folder") == 0);
		g_strfreev (args);

		/* With nothing to carry it is just the folder. */
		selected[0] = NULL;
		args = nemo_new_process_argv_tab (folder, "", selected);
		check (g_strv_length (args) == 2);
		check (g_strcmp0 (args[1], "file:///some/folder") == 0);
		g_strfreev (args);
		args = nemo_new_process_argv_tab (folder, NULL, NULL);
		check (g_strv_length (args) == 2);
		g_strfreev (args);
	}

	g_free (exe);
	g_object_unref (folder);
	g_object_unref (item);
	g_object_unref (elsewhere);

	if (failures == 0) {
		g_print ("OK\n");
	}

	return failures == 0 ? 0 : 1;
}
