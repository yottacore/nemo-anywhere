/* Find results keep their own columns, in the order they were dragged into,
 * whatever remember-folder-settings says, since they are not a folder. With
 * nothing saved they show Name, Ext, Size, Modified and Location, in that
 * order, with Type and the other dates hidden. */

#include <config.h>

#include <stdlib.h>
#include <string.h>
#include <gtk/gtk.h>

#include <libnemo-private/nemo-column-utilities.h>
#include <libnemo-private/nemo-global-preferences.h>

#include "test-scratch.h"
#include "test-check.h"

static gboolean
list_is (char **got, const char *const *want)
{
	gboolean same = g_strv_equal ((const char *const *) got, want);

	if (!same) {
		char *joined = g_strjoinv (", ", got);
		char *wanted = g_strjoinv (", ", (char **) want);

		g_printerr ("  got [%s], wanted [%s]\n", joined, wanted);
		g_free (joined);
		g_free (wanted);
	}
	g_strfreev (got);

	return same;
}

static gboolean
file_mentions (const char *needle)
{
	char *path = nemo_config_get_path ();
	char *text = NULL;
	gboolean found;

	nemo_config_flush ();
	found = g_file_get_contents (path, &text, NULL, NULL) && strstr (text, needle) != NULL;
	g_free (text);
	g_free (path);

	return found;
}

int
main (int argc, char *argv[])
{
	static const char *const shown[] = {
		"name", "extension", "size", "date_modified", "where", NULL
	};
	static const char *const order[] = {
		"name", "extension", "type", "size", "date_modified", "date_created",
		"date_accessed", "where", NULL
	};
	static const char *const picked[] = { "where", "name", "type", NULL };
	char *dir;

	dir = test_scratch_config_home ("nemo-searchcols-XXXXXX");
	g_assert (dir != NULL);

	gtk_init_check (&argc, &argv);
	nemo_global_preferences_init ();

	check (list_is (nemo_search_columns_get_visible (), shown));
	check (list_is (nemo_search_columns_get_order (), order));
	check (list_is (nemo_search_columns_get_default_visible (), shown));
	check (list_is (nemo_search_columns_get_default_order (), order));

	/* Off is the default, and the pick is kept anyway, order and all. */
	check (!nemo_global_preferences_get_remember_folder_settings ());
	nemo_search_columns_save (picked);
	check (list_is (nemo_search_columns_get_visible (), picked));
	check (list_is (nemo_search_columns_get_order (), picked));
	check (file_mentions ("search-visible-columns: where, name, type"));

	/* Turning remembering on or off doesn't touch it either. */
	nemo_config_set_boolean (nemo_preferences, NEMO_PREFERENCES_REMEMBER_FOLDER_SETTINGS, TRUE);
	check (list_is (nemo_search_columns_get_visible (), picked));
	nemo_config_set_boolean (nemo_preferences, NEMO_PREFERENCES_REMEMBER_FOLDER_SETTINGS, FALSE);
	check (list_is (nemo_search_columns_get_order (), picked));

	/* Back to the defaults, and nothing left in the file. */
	nemo_search_columns_save (NULL);
	check (list_is (nemo_search_columns_get_visible (), shown));
	check (list_is (nemo_search_columns_get_order (), order));
	check (!file_mentions ("search-visible-columns: where, name, type"));

	/* An empty pick reads as nothing saved, not as no columns at all. */
	{
		static const char *const none[] = { NULL };

		nemo_search_columns_save (none);
		check (list_is (nemo_search_columns_get_visible (), shown));
	}

	nemo_config_shutdown ();
	g_free (dir);

	if (failures > 0) {
		g_printerr ("%d check(s) failed\n", failures);
		return EXIT_FAILURE;
	}

	g_print ("OK\n");
	return EXIT_SUCCESS;
}
