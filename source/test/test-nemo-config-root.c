/* Where our config directory lives. GLib answers XDG on every platform, which
 * is right on Linux and BSD but puts Windows settings in the local, machine-
 * bound AppData and macOS settings in a hidden dotfile dir. This covers the
 * platform answer and the one-time move of a directory an older build left
 * behind. Env is set before the first call because both GLib and we cache. */

#include <config.h>

#include <stdlib.h>
#include <string.h>
#include <gio/gio.h>
#include <glib/gstdio.h>

#include <libnemo-private/nemo-file-utilities.h>

#include "test-scratch.h"
#include "test-check.h"

static void
write_marker (const char *dir, const char *name)
{
	char *path = g_build_filename (dir, name, NULL);

	g_mkdir_with_parents (dir, 0755);
	if (!g_file_set_contents (path, "marker", -1, NULL)) {
		g_printerr ("could not write %s - the test cannot mean anything\n", path);
		exit (EXIT_FAILURE);
	}
	g_free (path);
}

static gboolean
has_marker (const char *dir, const char *name)
{
	char *path = g_build_filename (dir, name, NULL);
	gboolean found = g_file_test (path, G_FILE_TEST_IS_REGULAR);

	g_free (path);
	return found;
}

int
main (void)
{
	char *sandbox;
	char *xdg;
	char *roaming;
	char *legacy;
	char *expected;
	char *user_dir;
	const char *root;

	sandbox = test_scratch_dir ("nemo-config-root-XXXXXX", NULL);
	g_assert (sandbox != NULL);

	xdg     = g_build_filename (sandbox, "xdg", NULL);
	roaming = g_build_filename (sandbox, "roaming", NULL);

	/* Before the first call: GLib caches its XDG answer, and so do we. */
	g_setenv ("XDG_CONFIG_HOME", xdg, TRUE);
	g_setenv ("APPDATA", roaming, TRUE);

	root = nemo_get_user_config_root ();
	check (root != NULL && *root != '\0');

#if defined(G_OS_WIN32)
	/* Roaming AppData, not the local one GLib hands out - settings are small
	 * and worth following a user between machines. */
	check (g_strcmp0 (root, roaming) == 0);
	check (g_strcmp0 (root, g_get_user_config_dir ()) != 0);
#elif defined(__APPLE__)
	{
		char *support = g_build_filename (g_get_home_dir (), "Library", "Application Support", NULL);

		check (g_strcmp0 (root, support) == 0);
		g_free (support);
	}
#else
	/* Linux and BSD: XDG is the native answer, so nothing changes there. */
	check (g_strcmp0 (root, g_get_user_config_dir ()) == 0);
	check (g_strcmp0 (root, xdg) == 0);
#endif

	legacy = g_build_filename (g_get_user_config_dir (), NEMO_APP_SLUG, NULL);
	expected = g_build_filename (root, NEMO_APP_SLUG, NULL);

	/* The XDG location is also the user data dir on Windows, where actions and
	 * scripts live. A directory there with no settings file is not an old
	 * config and must stay where it is. */
	if (g_strcmp0 (root, g_get_user_config_dir ()) != 0) {
		char *actions = g_build_filename (legacy, "actions", NULL);
		char *moved;

		g_mkdir_with_parents (actions, 0700);
		user_dir = nemo_get_user_directory ();
		moved = g_build_filename (user_dir, "actions", NULL);

		check (g_strcmp0 (user_dir, expected) == 0);
		check (g_file_test (actions, G_FILE_TEST_IS_DIR));
		check (!g_file_test (moved, G_FILE_TEST_EXISTS));

		check (test_scratch_remove_tree (user_dir));
		g_free (user_dir);
		g_free (moved);
		g_free (actions);
	}

	/* A directory an older build left in the XDG location moves across
	 * rather than being ignored, which would read as settings lost. */
	write_marker (legacy, "settings.shcl");

	user_dir = nemo_get_user_directory ();

	check (g_strcmp0 (user_dir, expected) == 0);
	check (g_file_test (user_dir, G_FILE_TEST_IS_DIR));
	check (has_marker (user_dir, "settings.shcl"));

	/* On a platform where the two are the same place, "moving" it must be a
	 * no-op and not lose the directory it was handed. */
	if (g_strcmp0 (root, g_get_user_config_dir ()) == 0) {
		check (g_strcmp0 (user_dir, legacy) == 0);
	} else {
		check (!g_file_test (legacy, G_FILE_TEST_EXISTS));
	}

	g_free (user_dir);
	g_free (expected);
	g_free (legacy);
	g_free (roaming);
	g_free (xdg);
	g_free (sandbox);

	if (failures == 0) {
		g_print ("config-root: all checks passed\n");
	}
	return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
