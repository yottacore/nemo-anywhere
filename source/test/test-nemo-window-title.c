/* The window title names the folder and then the program, so a taskbar button
 * says which program it belongs to. The folder part is whatever the tabs show,
 * which is a name or a full path depending on the preference. Most of this
 * checks the wrapping around it; the last part checks the folder part itself
 * follows the preference, the home folder included. */

#include <config.h>

#include <stdlib.h>
#include <string.h>
#include <glib.h>
#include <glib/gstdio.h>
#include <gio/gio.h>
#include <gtk/gtk.h>

#include <libnemo-private/nemo-config.h>
#include <libnemo-private/nemo-file-utilities.h>
#include <libnemo-private/nemo-global-preferences.h>

#include "test-scratch.h"

static int failures = 0;

static void
check_is (const char *location_title, const char *expected)
{
	char *got = nemo_compute_window_title (location_title);

	if (g_strcmp0 (got, expected) != 0) {
		g_printerr ("FAIL: %s -> %s (wanted %s)\n",
			    location_title != NULL ? location_title : "(null)",
			    got != NULL ? got : "(null)",
			    expected != NULL ? expected : "(null)");
		failures++;
	}

	g_free (got);
}

/* Long enough to be cut, and made of one character so the expected string can
   be built the same way rather than pasted in. */
static void
check_long_one_is_truncated (void)
{
	char *location_title;
	char *left;
	char *right;
	char *expected;

	location_title = g_strnfill (240, 'a');
	left = g_strnfill (88, 'a');
	right = g_strnfill (89, 'a');
	expected = g_strdup_printf ("%s...%s - Nemo Anywhere", left, right);

	check_is (location_title, expected);

	g_free (location_title);
	g_free (left);
	g_free (right);
	g_free (expected);
}

static void
check_location_title (GFile *location, const char *expected)
{
	char *got = nemo_compute_title_for_location (location);
	char *where = g_file_get_uri (location);

	if (g_strcmp0 (got, expected) != 0) {
		g_printerr ("FAIL: title for %s -> %s (wanted %s)\n",
			    where, got != NULL ? got : "(null)", expected);
		failures++;
	}

	g_free (where);
	g_free (got);
}

/* The path the title should carry for a folder, spelled the way the display
   separator says. */
static char *
display_path (GFile *location)
{
	char *path = g_file_get_path (location);

	nemo_path_apply_display_separator (path);
	return path;
}

/* The home folder answered "Home" before the preference was read, so turning
   it on there did nothing. The sub folder sits in the scratch dir, which is
   also home wherever GLib honors HOME. */
static void
check_full_path_preference (const char *scratch)
{
	GFile *home_dir = g_file_new_for_path (g_get_home_dir ());
	GFile *scratch_dir = g_file_new_for_path (scratch);
	GFile *sub = g_file_get_child (scratch_dir, "Projects");
	char *home_path, *sub_path;

	if (!g_file_make_directory (sub, NULL, NULL)) {
		g_printerr ("FAIL: could not make the sub folder\n");
		failures++;
	}
	home_path = display_path (home_dir);
	sub_path = display_path (sub);

	nemo_config_set_boolean (nemo_preferences,
				 NEMO_PREFERENCES_SHOW_FULL_PATH_TITLES, FALSE);
	check_location_title (home_dir, "Home");
	check_location_title (sub, "Projects");

	nemo_config_set_boolean (nemo_preferences,
				 NEMO_PREFERENCES_SHOW_FULL_PATH_TITLES, TRUE);
	check_location_title (home_dir, home_path);
	check_location_title (sub, sub_path);

	nemo_config_reset (nemo_preferences, NEMO_PREFERENCES_SHOW_FULL_PATH_TITLES);

	g_free (home_path);
	g_free (sub_path);
	g_object_unref (sub);
	g_object_unref (scratch_dir);
	g_object_unref (home_dir);
}

int
main (int argc, char *argv[])
{
	char *scratch;

	/* A folder name, which is what the title holds with the full-path
	   preference off. */
	check_is ("Documents", "Documents - Nemo Anywhere");
	check_is ("Home", "Home - Nemo Anywhere");

	/* A path, which is what it holds with the preference on. Both separators,
	   since the display separator follows the platform. */
	check_is ("/home/somebody/Documents",
		  "/home/somebody/Documents - Nemo Anywhere");
	check_is ("C:\\Users\\somebody\\Documents",
		  "C:\\Users\\somebody\\Documents - Nemo Anywhere");

	/* A space anywhere in it, folder or parent, puts the whole thing in
	   quotes, so the " - " before the program name cannot be mistaken for
	   part of it. */
	check_is ("My Documents", "\"My Documents\" - Nemo Anywhere");
	check_is ("/home/some body/Documents",
		  "\"/home/some body/Documents\" - Nemo Anywhere");
	check_is ("a\tb", "\"a\tb\" - Nemo Anywhere");

	/* A drive root keeps the name the sidebar gives it, not a bare separator.
	   That was a bug once; see the closed item in the backlog. */
	check_is ("Windows (C:)", "\"Windows (C:)\" - Nemo Anywhere");

	/* Nothing to name yet - a window that has not loaded a location. */
	check_is (NULL, "Nemo Anywhere");
	check_is ("", "Nemo Anywhere");

	/* A quote in a folder name is left alone, so the title can be ambiguous.
	   Escaping it would read worse than the odd name does. */
	check_is ("it's mine", "\"it's mine\" - Nemo Anywhere");
	check_is ("say \"hi\"", "\"say \"hi\"\" - Nemo Anywhere");

	check_long_one_is_truncated ();

	scratch = test_scratch_config_home ("nemo-window-title-XXXXXX");
	/* A NemoFile reaches for the icon theme as it is made. */
	gtk_init_check (&argc, &argv);
	nemo_global_preferences_init ();
	check_full_path_preference (scratch);
	g_free (scratch);

	if (failures > 0) {
		g_printerr ("%d failure(s)\n", failures);
		return EXIT_FAILURE;
	}

	g_print ("window-title: all checks passed\n");
	return EXIT_SUCCESS;
}
