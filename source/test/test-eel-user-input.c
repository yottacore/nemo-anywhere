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

/* A common name from either platform still expands where nothing set it. */
static void
test_stand_ins (const char *tmpdir)
{
	/* glib caches these on first use, so they keep their values once the
	   variables they read are gone. */
	const char *home = g_get_home_dir ();
	const char *tmp = g_get_tmp_dir ();
	const char *user = g_get_user_name ();
	const char *names[] = {
		"HOME", "USERPROFILE", "TMP", "TEMP", "TMPDIR", "USER", "USERNAME",
		"XDG_CONFIG_HOME", "APPDATA", "XDG_DATA_HOME", "LOCALAPPDATA",
		"XDG_CACHE_HOME", "DEVELOPER_DIR",
	};
	char *expected;
	guint i;

	for (i = 0; i < G_N_ELEMENTS (names); i++) {
		g_unsetenv (names[i]);
	}

	expected = g_strconcat (home, "\\Pictures", NULL);
	check_expansion ("%USERPROFILE%\\Pictures", expected);
	check_expansion ("%UserProfile%\\Pictures", expected);
	check_expansion ("$USERPROFILE\\Pictures", expected);
	g_free (expected);

	expected = g_strconcat (home, "/.config", NULL);
	check_expansion ("${HOME}/.config", expected);
	check_expansion ("%HOME%/.config", expected);
	g_free (expected);

	expected = g_strconcat (tmp, "/misc", NULL);
	check_expansion ("$TMP/misc", expected);
	check_expansion ("%TEMP%/misc", expected);
	check_expansion ("$TMPDIR/misc", expected);
	g_free (expected);

	/* No single match on the other platform, so these stay as typed. */
	check_expansion ("%APPDATA%/x", NULL);
	check_expansion ("$XDG_CONFIG_HOME/x", NULL);
	check_expansion ("%LOCALAPPDATA%/x", NULL);
	check_expansion ("$XDG_DATA_HOME/x", NULL);
	check_expansion ("$XDG_CACHE_HOME/x", NULL);

	expected = g_strconcat ("/home/", user, NULL);
	check_expansion ("/home/$USER", expected);
	check_expansion ("/home/%USERNAME%", expected);
	g_free (expected);

	/* Unix names keep their case. */
	check_expansion ("$home/x", NULL);
	check_expansion ("$UserProfile/x", NULL);

	/* No stand-in off a Mac, but it works anywhere it's set. */
	check_expansion ("$DEVELOPER_DIR/x", NULL);

	/* What's set always beats a stand-in. */
	g_setenv ("TMP", tmpdir, TRUE);
	g_setenv ("DEVELOPER_DIR", tmpdir, TRUE);
	expected = g_strconcat (tmpdir, "/misc", NULL);
	check_expansion ("$TMP/misc", expected);
	check_expansion ("$DEVELOPER_DIR/misc", expected);
	g_free (expected);
	g_unsetenv ("TMP");
	g_unsetenv ("DEVELOPER_DIR");
}

static void
test_expansion (const char *tmpdir)
{
	char *expected;

	g_setenv ("NEMO_TEST_DIR", tmpdir, TRUE);
	g_unsetenv ("NEMO_TEST_UNSET");

	/* Both spellings work everywhere, so a path can be copied between boxes. */
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
	expected = g_strconcat (g_get_home_dir (), "/.cache", NULL);
	check_expansion ("~/.cache", expected);
	g_free (expected);

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
	test_stand_ins (tmpdir);

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

	/* a variable standing in for the folder gives the same file */
	typed = g_strdup ("%NEMO_TEST_DIR%/plain.txt");
	location = eel_g_file_new_for_user_input (typed);
	resolved = g_file_get_path (location);
	check (resolved != NULL && g_file_test (resolved, G_FILE_TEST_EXISTS));
	g_free (resolved);
	g_object_unref (location);
	g_free (typed);

	/* a Windows path behind a variable resolves too */
	typed = g_strdup ("%NEMO_TEST_DIR%\\plain.txt");
	location = eel_g_file_new_for_user_input (typed);
	{
		GFile *want = g_file_new_for_path (real_file);

		check (g_file_equal (location, want));
		g_object_unref (want);
	}
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
