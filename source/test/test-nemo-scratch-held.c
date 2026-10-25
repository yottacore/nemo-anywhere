/* The scratch cleanup removes a directory the test still holds: one that is
 * the current directory, and one with the file cache open in it. Windows
 * removes neither while it is held, and 7 tests left their scratch behind
 * there. A copy handed its parent's slowness makes no scratch of its own,
 * since one that is killed never removes it. */

#include <config.h>

#include <stdlib.h>
#include <glib.h>
#include <glib/gstdio.h>

#include <libnemo-private/nemo-cache-db.h>

#include "test-scratch.h"
#include "test-check.h"

int
main (void)
{
	g_autofree char *was = g_get_current_dir ();
	g_autofree char *config = NULL, *held = NULL, *inner = NULL, *db_path = NULL;

	config = test_scratch_config_home ("nemo-scratch-cache-XXXXXX");
	check (config != NULL);
	if (config == NULL) {
		return EXIT_FAILURE;
	}
	check (nemo_cache_db_get () != NULL);
	db_path = nemo_cache_db_path ();
	check (db_path != NULL && g_str_has_prefix (db_path, config));
	check (db_path != NULL && g_file_test (db_path, G_FILE_TEST_IS_REGULAR));

	held = test_scratch_dir ("nemo-scratch-cwd-XXXXXX", NULL);
	check (held != NULL);
	if (held == NULL) {
		return EXIT_FAILURE;
	}
	inner = g_build_filename (held, "inner", NULL);
	check (g_mkdir (inner, 0700) == 0);
	check (g_chdir (inner) == 0);

	g_setenv ("NEMO_TEST_SLOWNESS", "3.5", TRUE);
	check (test_slowness () > 3.4 && test_slowness () < 3.6);

	test_scratch_cleanup ();

	check (!g_file_test (config, G_FILE_TEST_EXISTS));
	check (!g_file_test (held, G_FILE_TEST_EXISTS));
	if (g_file_test (config, G_FILE_TEST_EXISTS)) {
		g_printerr ("left the folder with the file cache in it: %s\n", config);
	}
	if (g_file_test (held, G_FILE_TEST_EXISTS)) {
		g_printerr ("left the current folder: %s\n", held);
	}

	g_chdir (was);

	if (failures == 0) {
		g_print ("nemo-scratch-held: all checks passed\n");
	}

	return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
