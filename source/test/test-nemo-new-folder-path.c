/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */

/* test-nemo-new-folder-path.c - a new folder named with a path.

   Copyright © 2026 t00mietum (CryptogID: ปʬϝღถɔ4რఠΔթะ9ƾǝu).

   This program is free software; you can redistribute it and/or
   modify it under the terms of the GNU General Public License as
   published by the Free Software Foundation; version 2 of the
   License.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   General Public License for more details.

   You should have received a copy of the GNU General Public
   License along with this program; if not, write to the
   Free Software Foundation, Inc., 51 Franklin Street, Suite 500,
   Boston, MA 02110-1335, USA.
*/

/* How a typed path splits, per platform, and the real job behind it: the
 * folder New folder made turns into the first missing folder of the path,
 * folders already there are gone through, and undo and redo take away and
 * put back what was made. Runs under wine too, for the backslash and the
 * setting that turns "/" off in typed locations. */

#include "test.h"

#include <libnemo-private/nemo-file.h>
#include <libnemo-private/nemo-file-operations.h>
#include <libnemo-private/nemo-file-undo-manager.h>
#include <libnemo-private/nemo-file-undo-operations.h>
#include <libnemo-private/nemo-global-preferences.h>
#include <libnemo-private/nemo-progress-info-manager.h>

#include <glib/gstdio.h>
#include <stdlib.h>
#include <string.h>

#include "test-scratch.h"
#include "test-check.h"

#define JOB_TIMEOUT_SECONDS 20

static gboolean job_finished;
static GFile   *job_top;
static GError  *job_error;
static char    *root;

static gboolean
give_up (G_GNUC_UNUSED gpointer data)
{
	g_printerr ("FAIL: job did not finish within %d seconds\n", JOB_TIMEOUT_SECONDS);
	failures++;
	gtk_main_quit ();
	return G_SOURCE_REMOVE;
}

static void
wait_for_job (void)
{
	guint timeout_id = g_timeout_add_seconds (JOB_TIMEOUT_SECONDS, give_up, NULL);

	gtk_main ();
	g_source_remove (timeout_id);
}

static void
nest_done (GFile *top, GError *error, G_GNUC_UNUSED gpointer data)
{
	g_clear_object (&job_top);
	g_clear_error (&job_error);
	job_top = top != NULL ? g_object_ref (top) : NULL;
	job_error = error != NULL ? g_error_copy (error) : NULL;
	job_finished = TRUE;
	gtk_main_quit ();
}

static void
apply_done (GObject *source, GAsyncResult *res, gpointer data)
{
	gboolean user_cancel = FALSE;

	*(gboolean *) data = nemo_file_undo_info_apply_finish (NEMO_FILE_UNDO_INFO (source), res,
								&user_cancel, NULL);
	job_finished = TRUE;
	gtk_main_quit ();
}

static gboolean
apply_undo (gboolean undo)
{
	NemoFileUndoInfo *info = nemo_file_undo_manager_get_action ();
	gboolean ok = FALSE;

	if (info == NULL) {
		return FALSE;
	}
	g_object_ref (info);
	job_finished = FALSE;
	nemo_file_undo_manager_push_flag ();
	nemo_file_undo_info_apply_async (info, undo, NULL, apply_done, &ok);
	wait_for_job ();
	g_object_unref (info);
	return job_finished && ok;
}

static char *
in_root (const char *relative)
{
	/* The relative paths here are written with "/". */
	char *native = g_strdelimit (g_strdup (relative), "/", G_DIR_SEPARATOR);
	char *path = g_build_filename (root, native, NULL);

	g_free (native);
	return path;
}

static gboolean
is_dir (const char *relative)
{
	char *path = in_root (relative);
	gboolean yes = g_file_test (path, G_FILE_TEST_IS_DIR);

	g_free (path);
	return yes;
}

static gboolean
exists (const char *relative)
{
	char *path = in_root (relative);
	gboolean yes = g_file_test (path, G_FILE_TEST_EXISTS);

	g_free (path);
	return yes;
}

static void
make_dir (const char *relative)
{
	char *path = in_root (relative);

	check (g_mkdir_with_parents (path, 0700) == 0);
	g_free (path);
}

static gboolean
top_is (const char *relative)
{
	char *path = in_root (relative);
	GFile *want = g_file_new_for_path (path);
	gboolean yes = job_top != NULL && g_file_equal (job_top, want);

	g_object_unref (want);
	g_free (path);
	return yes;
}

/* Runs the job the way the rename does: fresh is the folder New folder made,
   typed is what went in the box. */
static void
nest (const char *fresh, const char *typed)
{
	char *path = in_root (fresh);
	GFile *location = g_file_new_for_path (path);
	GError *error = NULL;
	char **parts = nemo_new_folder_path_split (typed, &error);

	check (parts != NULL);
	if (parts == NULL) {
		g_printerr ("  split refused \"%s\": %s\n", typed, error->message);
		g_clear_error (&error);
	} else {
		job_finished = FALSE;
		nemo_file_operations_nest_new_folder (NULL, location, (const char * const *) parts,
						      nest_done, NULL);
		wait_for_job ();
		check (job_finished);
	}
	g_strfreev (parts);
	g_object_unref (location);
	g_free (path);
}

#ifdef G_OS_WIN32
static void
renamed (G_GNUC_UNUSED NemoFile *file, G_GNUC_UNUSED GFile *result, GError *error,
	 G_GNUC_UNUSED gpointer data)
{
	g_clear_error (&job_error);
	job_error = error != NULL ? g_error_copy (error) : NULL;
	job_finished = TRUE;
	/* A refusal answers before the rename starts. */
	if (gtk_main_level () > 0) {
		gtk_main_quit ();
	}
}

/* The rename a name goes to when it doesn't nest. */
static void
plain_rename (const char *relative, const char *typed)
{
	char *path = in_root (relative);
	GFile *location = g_file_new_for_path (path);
	NemoFile *file = nemo_file_get (location);

	job_finished = FALSE;
	nemo_file_rename (file, typed, renamed, NULL);
	if (!job_finished) {
		wait_for_job ();
	}
	check (job_finished);
	nemo_file_unref (file);
	g_object_unref (location);
	g_free (path);
}
#endif

/* want is the parts joined with "|", or NULL for a refusal. */
static void
check_split (const char *typed, const char *want)
{
	GError *error = NULL;
	char **parts = nemo_new_folder_path_split (typed, &error);
	char *got = parts != NULL ? g_strjoinv ("|", parts) : NULL;

	if (g_strcmp0 (got, want) != 0) {
		g_printerr ("FAIL split \"%s\": got %s, want %s\n", typed,
			    got != NULL ? got : "(refused)", want != NULL ? want : "(refused)");
		failures++;
	}
	check ((parts == NULL) == (error != NULL));
	if (error != NULL) {
		check (error->message != NULL && error->message[0] != '\0');
	}
	g_clear_error (&error);
	g_free (got);
	g_strfreev (parts);
}

static void
check_splits (void)
{
	check (!nemo_new_folder_name_has_separator ("plain"));
	check (nemo_new_folder_name_has_separator ("a/b"));

	check_split ("plain", "plain");
	check_split ("a/b/c", "a|b|c");
	check_split ("a/b/", "a|b");
	check_split ("a/b//", "a|b");
	check_split ("solo/", "solo");
	check_split ("with space/x", "with space|x");
	check_split ("/a", NULL);
	check_split ("/", NULL);
	check_split ("a//b", NULL);
	check_split ("./a", NULL);
	check_split ("a/./b", NULL);
	check_split ("../a", NULL);
	check_split ("a/../b", NULL);
	check_split ("a/..", NULL);
	check_split ("", NULL);
	check_split ("a.b/..c", "a.b|..c");

#ifdef G_OS_WIN32
	check (nemo_new_folder_name_has_separator ("a\\b"));
	check_split ("a\\b", "a|b");
	check_split ("a\\b/c\\", "a|b|c");
	check_split ("\\a", NULL);
	check_split ("a\\/b", NULL);
	check_split ("a\\..\\b", NULL);

	/* With typed locations not taking "/", a "/" is no separator either, and
	   a name with one is left to the plain rename. */
	nemo_config_set_boolean (nemo_windows_preferences, NEMO_PREFERENCES_ALLOW_SLASH_INPUT, FALSE);
	check (!nemo_new_folder_name_has_separator ("a/b"));
	check (!nemo_new_folder_name_has_separator ("a\\b/c"));
	check (nemo_new_folder_name_has_separator ("a\\b"));
	check_split ("a\\b", "a|b");
	check_split ("a\\b\\", "a|b");
	check_split ("a/b", NULL);
	check_split ("a\\b/c", NULL);
	check_split ("a\\b/", NULL);
	check_split ("/a", NULL);
	nemo_config_set_boolean (nemo_windows_preferences, NEMO_PREFERENCES_ALLOW_SLASH_INPUT, TRUE);
	check (nemo_new_folder_name_has_separator ("a/b"));
	check_split ("a/b", "a|b");
#else
	/* Only a Windows path takes the backslash as a separator. */
	check (!nemo_new_folder_name_has_separator ("a\\b"));
	check_split ("a\\b", "a\\b");
	check_split ("a\\b/c", "a\\b|c");
#endif
}

static void
check_jobs (void)
{
	char *path;

	/* All new: the fresh folder becomes the top one. */
	make_dir ("Untitled Folder");
	nest ("Untitled Folder", "alpha/beta/gamma");
	check (job_error == NULL);
	check (is_dir ("alpha/beta/gamma"));
	check (!exists ("Untitled Folder"));
	check (top_is ("alpha"));

	/* Undo takes all 3, redo puts them back. */
	check (apply_undo (TRUE));
	check (!exists ("alpha"));
	check (apply_undo (FALSE));
	check (is_dir ("alpha/beta/gamma"));

	/* Something put inside since keeps that folder and the ones above it. */
	path = in_root ("alpha/beta/note.txt");
	check (g_file_set_contents (path, "keep", -1, NULL));
	g_free (path);
	check (apply_undo (TRUE));
	check (!exists ("alpha/beta/gamma"));
	check (exists ("alpha/beta/note.txt"));
	check (is_dir ("alpha"));

	/* Folders already there are gone through and left alone by undo. */
	make_dir ("there");
	make_dir ("Untitled Folder");
	nest ("Untitled Folder", "there/new/deeper");
	check (job_error == NULL);
	check (is_dir ("there/new/deeper"));
	check (!exists ("Untitled Folder"));
	check (top_is ("there"));
	check (apply_undo (TRUE));
	check (!exists ("there/new"));
	check (is_dir ("there"));

	/* The last one already there is the usual error, and nothing moves. */
	make_dir ("there/new/deeper");
	make_dir ("Untitled Folder");
	nest ("Untitled Folder", "there/new/deeper");
	check (g_error_matches (job_error, G_IO_ERROR, G_IO_ERROR_EXISTS));
	check (is_dir ("Untitled Folder"));
	check (is_dir ("there/new/deeper"));

	/* A file on the way. */
	path = in_root ("a file");
	check (g_file_set_contents (path, "x", -1, NULL));
	g_free (path);
	nest ("Untitled Folder", "a file/inside");
	check (g_error_matches (job_error, G_IO_ERROR, G_IO_ERROR_NOT_DIRECTORY));
	check (is_dir ("Untitled Folder"));
	check (!exists ("a file/inside"));

	/* The fresh folder's own name as the first part keeps it there. */
	nest ("Untitled Folder", "Untitled Folder/kept");
	check (job_error == NULL);
	check (is_dir ("Untitled Folder/kept"));
	check (top_is ("Untitled Folder"));
	check (apply_undo (TRUE));
	check (!exists ("Untitled Folder"));

	/* Only a trailing separator: a plain rename. */
	make_dir ("Untitled Folder");
	nest ("Untitled Folder", "solo/");
	check (job_error == NULL);
	check (is_dir ("solo"));
	check (!exists ("Untitled Folder"));

	/* The backslash: a separator on Windows, part of the name elsewhere. */
	make_dir ("Untitled Folder");
	nest ("Untitled Folder", "back\\slash");
	check (job_error == NULL);
#ifdef G_OS_WIN32
	check (is_dir ("back/slash"));
	check (top_is ("back"));
#else
	check (is_dir ("back\\slash"));
	check (top_is ("back\\slash"));
#endif
	check (!exists ("Untitled Folder"));
	check (apply_undo (TRUE));
	check (!exists ("back\\slash"));
	check (!exists ("back"));

#ifdef G_OS_WIN32
	/* With "/" off the backslash still nests, and a "/" is refused with
	   nothing made, like any character a name can't have. */
	nemo_config_set_boolean (nemo_windows_preferences, NEMO_PREFERENCES_ALLOW_SLASH_INPUT, FALSE);
	make_dir ("Untitled Folder");
	nest ("Untitled Folder", "off\\still");
	check (job_error == NULL);
	check (is_dir ("off/still"));
	check (!exists ("Untitled Folder"));
	make_dir ("Untitled Folder");
	check (!nemo_new_folder_name_has_separator ("slash/off"));
	plain_rename ("Untitled Folder", "slash/off");
	check (job_error != NULL);
	check (is_dir ("Untitled Folder"));
	check (!exists ("slash"));
	check (!nemo_new_folder_name_has_separator ("mixed\\slash/off"));
	plain_rename ("Untitled Folder", "mixed\\slash/off");
	check (job_error != NULL);
	check (is_dir ("Untitled Folder"));
	check (!exists ("mixed"));
	nemo_config_set_boolean (nemo_windows_preferences, NEMO_PREFERENCES_ALLOW_SLASH_INPUT, TRUE);
#endif
}

int
main (int argc, char *argv[])
{
	NemoProgressInfoManager *manager;
	char *home;

	home = test_scratch_config_home ("nemo-new-folder-path-home-XXXXXX");
	g_setenv ("NEMO_TESTGUARD_ALL_DELETES", "0", TRUE);
	nemo_global_preferences_init ();
	test_init (&argc, &argv);

	/* More than one job in the run, so the queue needs this held. */
	manager = nemo_progress_info_manager_new ();

	root = test_scratch_dir ("nemo-new-folder-path-XXXXXX", NULL);

	check_splits ();
	check_jobs ();

	g_clear_object (&job_top);
	g_clear_error (&job_error);
	g_object_unref (manager);
	g_free (root);
	g_free (home);

	if (failures > 0) {
		g_printerr ("%d failure(s)\n", failures);
		return EXIT_FAILURE;
	}
	g_print ("OK\n");
	return EXIT_SUCCESS;
}
