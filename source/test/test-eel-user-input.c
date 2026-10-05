/* Exercises typed-location parsing: backslash-separated input and the home and
 * variable shorthands resolve by fallback, while literal names - including real
 * backslash-named files on POSIX - always win. */

#include <config.h>

#include <stdlib.h>
#include <string.h>
#include <gio/gio.h>
#include <glib/gstdio.h>

#include <eel/eel-vfs-extensions.h>

#include "test-scratch.h"
#include "test-check.h"

/* One expansion, checked as text. NULL expected means "nothing to expand". */
static void
check_expansion (const char *typed, const char *expected)
{
	char *got = eel_expand_user_input (typed);

	check (g_strcmp0 (got, expected) == 0);
	g_free (got);
}

static void
test_expansion (const char *tmpdir)
{
	char *expected;

	g_setenv ("NEMO_TEST_DIR", tmpdir, TRUE);
	g_unsetenv ("NEMO_TEST_UNSET");

	/* Both spellings work everywhere, so a path can be carried between boxes. */
	expected = g_strconcat (tmpdir, "/sub", NULL);
	check_expansion ("%NEMO_TEST_DIR%/sub", expected);
	check_expansion ("$NEMO_TEST_DIR/sub", expected);
	check_expansion ("${NEMO_TEST_DIR}/sub", expected);
	g_free (expected);

	/* A name nobody set is left exactly as typed, which is what keeps a folder
	   with a % or a $ in its name reachable. */
	check_expansion ("%NEMO_TEST_UNSET%/sub", NULL);
	check_expansion ("$NEMO_TEST_UNSET/sub", NULL);
	check_expansion ("50% off", NULL);
	check_expansion ("plain/path", NULL);

	/* An admin share ends in a bare $, and there is no name after it. */
	check_expansion ("\\\\host\\c$\\temp", NULL);

#ifdef G_OS_WIN32
	check_expansion ("~", g_get_home_dir ());

	/* Only at the start, and only as a whole path element. */
	check_expansion ("a~b", NULL);
	check_expansion ("~user", NULL);
#endif
}

int
main (void)
{
	char *tmpdir, *real_file, *typed, *resolved;
	GFile *location;

	tmpdir = test_scratch_dir ("eel-input-test-XXXXXX", NULL);
	g_assert (tmpdir != NULL);

	test_expansion (tmpdir);

	real_file = g_build_filename (tmpdir, "plain.txt", NULL);
	g_assert (g_file_set_contents (real_file, "x", -1, NULL));

	/* backslash-separated form of an existing path resolves to it */
	typed = g_strdelimit (g_strdup (real_file), "/", '\\');
	location = eel_g_file_new_for_user_input (typed);
	resolved = g_file_get_path (location);
	check (resolved != NULL && g_file_test (resolved, G_FILE_TEST_EXISTS));
	g_free (resolved);
	g_object_unref (location);
	g_free (typed);

	/* a variable standing in for the folder reaches the same file */
	typed = g_strdup ("%NEMO_TEST_DIR%/plain.txt");
	location = eel_g_file_new_for_user_input (typed);
	resolved = g_file_get_path (location);
	check (resolved != NULL && g_file_test (resolved, G_FILE_TEST_EXISTS));
	g_free (resolved);
	g_object_unref (location);
	g_free (typed);

	/* nonexistent input stays literal - no surprise rewriting */
	typed = g_build_filename (tmpdir, "no-such\\thing", NULL);
	location = eel_g_file_new_for_user_input (typed);
	resolved = g_file_get_path (location);
	check (resolved != NULL && strstr (resolved, "no-such") != NULL);
	g_free (resolved);
	g_object_unref (location);
	g_free (typed);

#ifndef G_OS_WIN32
	/* a file literally named with a backslash keeps working on POSIX */
	{
		char *bs_file = g_build_filename (tmpdir, "a\\b.txt", NULL);
		g_assert (g_file_set_contents (bs_file, "y", -1, NULL));

		location = eel_g_file_new_for_user_input (bs_file);
		resolved = g_file_get_path (location);
		check (resolved != NULL && strcmp (resolved, bs_file) == 0);
		g_free (resolved);
		g_object_unref (location);
		g_unlink (bs_file);
		g_free (bs_file);
	}
#endif

	g_unlink (real_file);
	g_free (real_file);
	g_rmdir (tmpdir);
	g_free (tmpdir);

	if (failures == 0) {
		g_print ("eel-user-input: all checks passed\n");
	}

	return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
