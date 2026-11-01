/* The scratch cleanup removes a directory the test still holds: one that is
 * the current directory, and one with the file cache open in it. Windows
 * removes neither while it is held, and 7 tests left their scratch behind
 * there. A copy handed its parent's slowness makes no scratch of its own,
 * since one that is killed never removes it.
 *
 * On Windows also one with a file someone else holds for a moment, the way a
 * virus scanner holds a file just written. dcx3q left its scratch behind on
 * b29w that way, about one run in five. */

#include <config.h>

#include <stdlib.h>
#include <glib.h>
#include <glib/gstdio.h>

#include <libnemo-private/nemo-cache-db.h>

#include "test-scratch.h"
#include "test-check.h"

#ifdef G_OS_WIN32
#include <windows.h>

static gpointer
let_go_later (gpointer handle)
{
	g_usleep (500000);
	CloseHandle (handle);
	return NULL;
}

/* Open with no delete sharing, so a delete is refused until it is let go. */
static GThread *
hold_for_a_moment (const char *path)
{
	g_autofree gunichar2 *wide = g_utf8_to_utf16 (path, -1, NULL, NULL, NULL);
	HANDLE handle;

	handle = CreateFileW (wide, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
	check (handle != INVALID_HANDLE_VALUE);
	if (handle == INVALID_HANDLE_VALUE)
		return NULL;

	return g_thread_new ("let-go", let_go_later, handle);
}
#endif

int
main (void)
{
	g_autofree char *was = g_get_current_dir ();
	g_autofree char *config = NULL, *held = NULL, *inner = NULL, *db_path = NULL;
	g_autofree char *busy_dir = NULL, *busy = NULL;
	GThread *holder = NULL;

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

	busy_dir = test_scratch_dir ("nemo-scratch-busy-XXXXXX", NULL);
	check (busy_dir != NULL);
	if (busy_dir == NULL) {
		return EXIT_FAILURE;
	}
	busy = g_build_filename (busy_dir, "busy.txt", NULL);
	check (g_file_set_contents (busy, "x", -1, NULL));
#ifdef G_OS_WIN32
	holder = hold_for_a_moment (busy);
#endif

	test_scratch_cleanup ();

	check (!g_file_test (config, G_FILE_TEST_EXISTS));
	check (!g_file_test (held, G_FILE_TEST_EXISTS));
	if (g_file_test (config, G_FILE_TEST_EXISTS)) {
		g_printerr ("left the folder with the file cache in it: %s\n", config);
	}
	if (g_file_test (held, G_FILE_TEST_EXISTS)) {
		g_printerr ("left the current folder: %s\n", held);
	}
	check (!g_file_test (busy_dir, G_FILE_TEST_EXISTS));
	if (g_file_test (busy_dir, G_FILE_TEST_EXISTS)) {
		g_printerr ("left the folder with a held file in it: %s\n", busy_dir);
	}
	if (holder != NULL) {
		g_thread_join (holder);
	}

	g_chdir (was);

	if (failures == 0) {
		g_print ("nemo-scratch-held: all checks passed\n");
	}

	return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
