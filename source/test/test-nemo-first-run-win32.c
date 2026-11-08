/* First-run setup on Windows: the bookmark list is seeded with the platform
 * defaults, anything carried over from a POSIX machine is dropped, and --reset
 * puts it all back. Runs against a throwaway config root. */

#include <config.h>

#include <stdlib.h>
#include <string.h>
#include <gio/gio.h>
#include <glib/gstdio.h>
#include <gtk/gtk.h>

#include <libnemo-private/nemo-config.h>
#include <libnemo-private/nemo-file-utilities.h>
#include <libnemo-private/nemo-global-preferences.h>

#include "nemo-bookmark-list.h"

#include "test-scratch.h"
#include "test-check.h"

static char *
bookmarks_path (void)
{
	char *user_directory = nemo_get_user_directory ();
	char *path = g_build_filename (user_directory, "bookmarks", NULL);

	g_free (user_directory);
	return path;
}

static char *
toolkit_bookmarks_path (void)
{
	return g_build_filename (g_get_user_config_dir (), "gtk-3.0", "bookmarks", NULL);
}

static char *
read_bookmarks (void)
{
	char *path = bookmarks_path ();
	char *text = NULL;

	if (!g_file_get_contents (path, &text, NULL, NULL)) {
		text = NULL;
	}
	g_free (path);

	return text ? text : g_strdup ("");
}

static void
write_bookmarks (const char *text)
{
	char *path = bookmarks_path ();
	char *dir = g_path_get_dirname (path);

	g_mkdir_with_parents (dir, 0700);
	g_assert (g_file_set_contents (path, text, -1, NULL));

	g_free (dir);
	g_free (path);
}

/* The expected entry for a real folder on this machine. Reading it back from
 * the system rather than spelling it out keeps the test off boxes where the
 * drive is not C: or the profile folders are named in another language. */
static char *
uri_for (const char *path)
{
	GFile *file;
	char  *uri;

	if (path == NULL) {
		return NULL;
	}

	file = g_file_new_for_path (path);
	uri = g_file_get_uri (file);
	g_object_unref (file);

	return uri;
}

static void
clear_marker (void)
{
	nemo_config_reset (nemo_config_get_group (NEMO_STATE_GROUP),
	                   NEMO_STATE_FIRST_RUN_DONE);
	nemo_config_flush ();
}

/* A fresh profile gets the drive root and the user's own folders, and nothing
 * that looks like it came from somewhere else. */
static void
test_seeds_defaults (void)
{
	char       *path = bookmarks_path ();
	char       *text;
	char       *root;
	char       *documents;
	const char *drive = g_getenv ("SystemDrive");

	g_remove (path);
	g_free (path);
	clear_marker ();

	nemo_bookmark_list_first_run_setup ();
	text = read_bookmarks ();

	root = g_strdup_printf ("%s\\", drive != NULL ? drive : "C:");
	path = uri_for (root);
	check (path != NULL && strstr (text, path) != NULL);
	g_free (path);
	g_free (root);

	documents = uri_for (g_get_user_special_dir (G_USER_DIRECTORY_DOCUMENTS));
	if (documents != NULL) {
		check (strstr (text, documents) != NULL);
		g_free (documents);
	}

	check (strstr (text, "file:///home/") == NULL);
	check (nemo_config_get_boolean (nemo_config_get_group (NEMO_STATE_GROUP),
	                                NEMO_STATE_FIRST_RUN_DONE));

	g_free (text);
}

/* Once the marker is set nothing is touched again, however the list looks. */
static void
test_second_run_is_a_no_op (void)
{
	char *text;

	write_bookmarks ("file:///C:/Windows\n");
	nemo_bookmark_list_first_run_setup ();

	text = read_bookmarks ();
	check (g_strcmp0 (text, "file:///C:/Windows\n") == 0);
	g_free (text);
}

/* A config carried from a POSIX box: the paths that cannot exist here go, and
 * a set someone actually curated on Windows is left alone rather than replaced
 * with the defaults. */
static void
test_foreign_bookmarks_dropped (void)
{
	char *text;

	clear_marker ();
	write_bookmarks ("file:///home/somebody/src\n"
	                 "file:///C:/Windows\n"
	                 "file:///usr/share\n");

	nemo_bookmark_list_first_run_setup ();

	text = read_bookmarks ();
	check (strstr (text, "file:///home/somebody/src") == NULL);
	check (strstr (text, "file:///usr/share") == NULL);
	check (strstr (text, "file:///C:/Windows") != NULL);
	/* The kept entry is enough - the defaults must not have been added on top. */
	check (g_strrstr (text, "\n") == text + strlen (text) - 1);
	g_free (text);
}

/* Nothing but foreign entries means there is nothing worth keeping, so the
 * defaults go in after all. */
static void
test_all_foreign_falls_back_to_defaults (void)
{
	char *text;
	char *documents;

	clear_marker ();
	write_bookmarks ("file:///home/somebody/src\n");

	nemo_bookmark_list_first_run_setup ();

	text = read_bookmarks ();
	check (strstr (text, "file:///home/somebody/src") == NULL);

	documents = uri_for (g_get_user_special_dir (G_USER_DIRECTORY_DOCUMENTS));
	if (documents != NULL) {
		check (strstr (text, documents) != NULL);
		g_free (documents);
	}

	g_free (text);
}

/* A list an older build kept in the toolkit's own file, in the local profile,
 * is carried across to the roaming one the first time ours is missing, and a
 * reset takes both away so it cannot come back. */
static void
test_toolkit_list_copied_across (void)
{
	char *ours = bookmarks_path ();
	char *toolkit = toolkit_bookmarks_path ();
	char *dir = g_path_get_dirname (toolkit);
	char *text;

	g_remove (ours);
	g_mkdir_with_parents (dir, 0700);
	g_assert (g_file_set_contents (toolkit, "file:///C:/Kept\n", -1, NULL));
	clear_marker ();

	nemo_bookmark_list_first_run_setup ();

	text = read_bookmarks ();
	check (strstr (text, "file:///C:/Kept") != NULL);
	check (g_file_test (toolkit, G_FILE_TEST_IS_REGULAR));
	g_free (text);

	nemo_bookmark_list_reset_files ();
	check (!g_file_test (ours, G_FILE_TEST_EXISTS));
	check (!g_file_test (toolkit, G_FILE_TEST_EXISTS));

	g_free (dir);
	g_free (toolkit);
	g_free (ours);
}

static void
test_foreign_settings_dropped (void)
{
	NemoConfigGroup  *terminal = nemo_config_get_group ("terminal");
	NemoConfigGroup  *search   = nemo_config_get_group ("search");
	const char *const skip[]   = { "/dev", "/home/somebody/big", ".git", NULL };
	char             *exec;
	char            **kept;

	nemo_config_set_string (terminal, "exec", "/usr/bin/gnome-terminal");
	nemo_config_set_strv (search, "search-skip-folders", skip);

	nemo_config_drop_foreign_paths ();

	exec = nemo_config_get_string (terminal, "exec");
	check (g_strcmp0 (exec, "") == 0);
	g_free (exec);

	kept = nemo_config_get_strv (search, "search-skip-folders");
	check (g_strv_length (kept) == 1);
	check (g_strcmp0 (kept[0], ".git") == 0);
	g_strfreev (kept);

	nemo_config_reset (search, "search-skip-folders");
}

/* A Windows value that happens to start with a letter drive is not foreign, and
 * neither is a UNC path. */
static void
test_windows_settings_kept (void)
{
	NemoConfigGroup *terminal = nemo_config_get_group ("terminal");
	char            *exec;

	nemo_config_set_string (terminal, "exec", "C:\\Windows\\System32\\cmd.exe");
	nemo_config_drop_foreign_paths ();

	exec = nemo_config_get_string (terminal, "exec");
	check (g_strcmp0 (exec, "C:\\Windows\\System32\\cmd.exe") == 0);
	g_free (exec);

	nemo_config_reset (terminal, "exec");
}

static void
test_reset_files (void)
{
	char *path;

	write_bookmarks ("file:///C:/Windows\n");
	nemo_bookmark_list_reset_files ();

	path = bookmarks_path ();
	check (!g_file_test (path, G_FILE_TEST_EXISTS));
	g_free (path);
}

/* Every stored key goes, so the next start reads the shipped defaults and the
 * first-run marker is gone with them. */
static void
test_reset_all (void)
{
	NemoConfigGroup *prefs = nemo_config_get_group ("preferences");

	nemo_config_set_boolean (prefs, "show-hidden-files", TRUE);
	nemo_config_set_boolean (nemo_config_get_group (NEMO_STATE_GROUP),
	                         NEMO_STATE_FIRST_RUN_DONE, TRUE);

	nemo_config_reset_all ();
	nemo_config_flush ();

	check (nemo_config_get_boolean (prefs, "show-hidden-files") == FALSE);
	check (nemo_config_get_boolean (nemo_config_get_group (NEMO_STATE_GROUP),
	                                NEMO_STATE_FIRST_RUN_DONE) == FALSE);
}

int
main (int argc, char *argv[])
{
	char *tmp;

	tmp = test_scratch_config_home ("nemo-first-run-test-XXXXXX");
	g_setenv ("LOCALAPPDATA", tmp, TRUE);

	gtk_init (&argc, &argv);
	g_log_set_always_fatal (G_LOG_LEVEL_CRITICAL);

	nemo_config_init ();

	test_seeds_defaults ();
	test_second_run_is_a_no_op ();
	test_foreign_bookmarks_dropped ();
	test_all_foreign_falls_back_to_defaults ();
	test_toolkit_list_copied_across ();
	test_foreign_settings_dropped ();
	test_windows_settings_kept ();
	test_reset_files ();
	test_reset_all ();

	nemo_config_shutdown ();
	g_free (tmp);

	if (failures == 0)
		g_print ("nemo-first-run: all checks passed\n");

	return failures == 0 ? 0 : 1;
}
