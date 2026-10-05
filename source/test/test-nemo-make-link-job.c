/* Make link, through the real job, with what the dialog would have answered
 * handed in up front.
 *
 * Argument: "relative" (default), "absolute", "hardlink", "junction",
 * "shortcut", "shortcut-portable" (the same, under home, where a variable
 * covers the files), "names" for what links made beside their originals
 * are called, "every" for every answer the dialog can give, made both beside
 * the originals and in another folder, or "redo" for links made again after
 * an undo.
 */

#include "test.h"

#include <libnemo-private/nemo-global-preferences.h>
#include <libnemo-private/nemo-file-operations.h>
#include <libnemo-private/nemo-file-undo-manager.h>
#include <libnemo-private/nemo-link-copy.h>
#ifdef G_OS_WIN32
#include <libnemo-private/nemo-shortcut-win32.h>
#endif
#include <libnemo-private/nemo-lnk.h>

#include <glib/gstdio.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "test-scratch.h"
#include "test-check.h"

#define JOB_TIMEOUT_SECONDS 20

static gboolean job_finished;
static gboolean job_succeeded;

static void
job_done (GHashTable *debuting_uris,
          gboolean success,
          gpointer data)
{
	job_succeeded = success;
	job_finished = TRUE;
	gtk_main_quit ();
}

static gboolean
give_up (gpointer data)
{
	g_printerr ("FAIL: link job did not finish within %d seconds\n", JOB_TIMEOUT_SECONDS);
	failures++;
	gtk_main_quit ();
	return G_SOURCE_REMOVE;
}

static NemoLinkKind
kind_of (const char *path)
{
	GFile *file = g_file_new_for_path (path);
	NemoLinkKind kind = nemo_link_kind (file, NULL);

	g_object_unref (file);
	return kind;
}

static char *
target_of (const char *path)
{
	GFile *file = g_file_new_for_path (path);
	char *target = NULL;

	nemo_link_read_target (file, &target, NULL);
	g_object_unref (file);
	return target;
}

static char *
uri_of (const char *path)
{
	return g_filename_to_uri (path, NULL, NULL);
}

/* The flag word in the header says which parts went in. */
#define HAS_LINK_INFO 0x002
#define HAS_RELATIVE  0x008
#define HAS_ENV       0x200

static guint32
lnk_flags (const char *lnk_path)
{
	char *bytes = NULL;
	gsize length = 0;
	guint32 flags = 0;

	if (g_file_get_contents (lnk_path, &bytes, &length, NULL) && length >= 0x4c) {
		flags = ((guint8) bytes[20]) | ((guint8) bytes[21] << 8);
	}
	g_free (bytes);

	return flags;
}

/* The shortcut at lnk_path leads to want, read the way this platform reads
   one, with the absolute and relative paths in it. With check_env, a
   variable covers want, so the portable path must be there too. */
static void
check_shortcut (const char *lnk_path, const char *want, gboolean check_env)
{
	guint32 flags = lnk_flags (lnk_path);

#ifdef G_OS_WIN32
	char *target = NULL;

	check (nemo_shortcut_win32_read (lnk_path, &target, NULL));
	check (target != NULL && g_ascii_strcasecmp (target, want) == 0);
	g_free (target);
#else
	NemoLnk lnk;
	char *uri, *want_uri = uri_of (want);

	check (nemo_lnk_read (lnk_path, &lnk));
	uri = nemo_lnk_resolve (lnk_path, &lnk);
	check (g_strcmp0 (uri, want_uri) == 0);
	check (nemo_lnk_is_dir (&lnk) == g_file_test (want, G_FILE_TEST_IS_DIR));
	nemo_lnk_clear (&lnk);
	g_free (uri);
	g_free (want_uri);
#endif
	check ((flags & HAS_LINK_INFO) != 0);
	check ((flags & HAS_RELATIVE) != 0);
	/* Only where a variable covers the target, which it does here for the
	   portable run alone, so only that one is checked. */
	if (check_env) {
		check ((flags & HAS_ENV) != 0);
	}
}

static gboolean
run_link_job (GList *uris, const char *dir, NemoLinkOptions *options, GtkWidget *window)
{
	char *dir_uri = uri_of (dir);
	guint timeout_id;

	job_finished = FALSE;
	job_succeeded = FALSE;
	nemo_file_operations_symlink (uris, NULL, dir_uri, options, window, job_done, NULL);

	timeout_id = g_timeout_add_seconds (JOB_TIMEOUT_SECONDS, give_up, NULL);
	gtk_main ();
	g_source_remove (timeout_id);
	g_free (dir_uri);

	return job_finished && job_succeeded;
}

static void
check_named (const char *dir, const char *name)
{
	char *path = g_build_filename (dir, name, NULL);

	if (!g_file_test (path, G_FILE_TEST_EXISTS | G_FILE_TEST_IS_SYMLINK)) {
		g_printerr ("FAIL: no \"%s\"\n", name);
		failures++;
	}
	g_free (path);
}

/* Beside its original a link says what kind it is, and the extension stays
   last except on a shortcut. Elsewhere the name is kept, which the other
   runs check. */
static void
check_names (const char *src_dir, const char *payload, const char *folder,
	     guint supported, GtkWidget *window)
{
	NemoLinkOptions options = { NEMO_MAKE_SYMLINK, NEMO_MAKE_SYMLINK, TRUE };
	GList *both = NULL, *file_only = NULL;

	both = g_list_append (both, uri_of (payload));
	both = g_list_append (both, uri_of (folder));
	file_only = g_list_append (file_only, uri_of (payload));

	if (supported & NEMO_LINK_FILE_SYMLINK) {
		check (run_link_job (both, src_dir, &options, window));
		check_named (src_dir, "payload - symlink.txt");
		check_named (src_dir, "folder - symlink");

		check (run_link_job (both, src_dir, &options, window));
		check_named (src_dir, "payload - symlink 2.txt");
		check_named (src_dir, "folder - symlink 2");
	} else {
		g_printerr ("note: no symlinks here, only hardlink and shortcut names checked\n");
	}

	options.file_kind = NEMO_MAKE_HARDLINK;
	check (run_link_job (file_only, src_dir, &options, window));
	check_named (src_dir, "payload - hardlink.txt");

	options.file_kind = NEMO_MAKE_SHORTCUT;
	options.folder_kind = NEMO_MAKE_SHORTCUT;
	check (run_link_job (both, src_dir, &options, window));
	check_named (src_dir, "payload.txt - shortcut.lnk");
	check_named (src_dir, "folder - shortcut.lnk");

	check (run_link_job (file_only, src_dir, &options, window));
	check_named (src_dir, "payload.txt - shortcut 2.lnk");

	g_list_free_full (both, g_free);
	g_list_free_full (file_only, g_free);
}

static const char *
make_word (NemoMakeLink kind)
{
	switch (kind) {
	case NEMO_MAKE_JUNCTION:
		return "junction";
	case NEMO_MAKE_HARDLINK:
		return "hardlink";
	case NEMO_MAKE_SHORTCUT:
		return "shortcut";
	default:
		return "symlink";
	}
}

/* What a link made from name comes out called: beside its original it says
   what it is, before the extension except on a shortcut. */
static char *
made_name (const char *name, NemoMakeLink kind, gboolean beside)
{
	const char *dot = strrchr (name, '.');
	char *stem;
	char *made;

	if (kind == NEMO_MAKE_SHORTCUT) {
		return beside ? g_strdup_printf ("%s - shortcut.lnk", name)
			      : g_strdup_printf ("%s.lnk", name);
	}
	if (!beside) {
		return g_strdup (name);
	}

	stem = dot != NULL ? g_strndup (name, dot - name) : g_strdup (name);
	made = g_strdup_printf ("%s - %s%s", stem, make_word (kind), dot != NULL ? dot : "");
	g_free (stem);
	return made;
}

/* One link, whatever kind was asked for, checked against what it was made
   from. */
static void
check_made (const char *dir, const char *made, const char *original,
	    NemoMakeLink kind, gboolean is_dir, const NemoLinkOptions *options)
{
	char *path = g_build_filename (dir, made, NULL);
	char *target, *contents = NULL;
	FILE *fp;

	if (!g_file_test (path, G_FILE_TEST_EXISTS | G_FILE_TEST_IS_SYMLINK)) {
		g_printerr ("FAIL: no \"%s\"\n", made);
		failures++;
		g_free (path);
		return;
	}

	switch (kind) {
	case NEMO_MAKE_SYMLINK:
		check (kind_of (path) == (is_dir ? NEMO_LINK_DIR_SYMLINK : NEMO_LINK_FILE_SYMLINK));
		target = target_of (path);
		check (target != NULL && g_path_is_absolute (target) == !options->relative);
		g_free (target);
		if (is_dir) {
			check (g_file_test (path, G_FILE_TEST_IS_DIR));
		} else {
			check (g_file_get_contents (path, &contents, NULL, NULL) &&
			       g_strcmp0 (contents, "payload") == 0);
		}
		break;
	case NEMO_MAKE_JUNCTION:
		check (kind_of (path) == NEMO_LINK_JUNCTION);
		check (g_file_test (path, G_FILE_TEST_IS_DIR));
		break;
	case NEMO_MAKE_HARDLINK:
		/* A second name: written through it, seen through the original. */
		check (kind_of (path) == NEMO_LINK_NONE);
		fp = g_fopen (path, "ab");
		check (fp != NULL);
		if (fp != NULL) {
			fputs (" more", fp);
			fclose (fp);
		}
		check (g_file_get_contents (original, &contents, NULL, NULL) &&
		       g_strcmp0 (contents, "payload more") == 0);
		break;
	default:
		check_shortcut (path, original, FALSE);
		break;
	}

	g_free (contents);
	g_free (path);
}

/* Every answer that changes what comes out. The path choice only matters
   while a symlink is made, so it is only varied then. Each one in its own
   folder, since a hardlink check writes to the original. */
static void
check_every (const char *tmp, guint supported, GtkWidget *window)
{
	static const NemoMakeLink folder_kinds[] = {
		NEMO_MAKE_SYMLINK, NEMO_MAKE_JUNCTION, NEMO_MAKE_SHORTCUT
	};
	static const NemoMakeLink file_kinds[] = {
		NEMO_MAKE_SYMLINK, NEMO_MAKE_HARDLINK, NEMO_MAKE_SHORTCUT
	};
	int number = 0;
	int pick, fo, fi, rel, beside;

	/* 1 is the file alone, 2 the folder alone, 3 both. */
	for (pick = 1; pick <= 3; pick++)
	for (fo = 0; fo < (int) G_N_ELEMENTS (folder_kinds); fo++)
	for (fi = 0; fi < (int) G_N_ELEMENTS (file_kinds); fi++)
	for (rel = 0; rel <= 1; rel++)
	for (beside = 0; beside <= 1; beside++) {
		gboolean with_file = (pick & 1) != 0;
		gboolean with_folder = (pick & 2) != 0;
		NemoLinkOptions options = { folder_kinds[fo], file_kinds[fi], rel };
		gboolean symlink = (with_folder && options.folder_kind == NEMO_MAKE_SYMLINK) ||
				   (with_file && options.file_kind == NEMO_MAKE_SYMLINK);
		char *name, *root, *from, *to, *payload, *folder, *made;
		GList *uris = NULL;
		int before = failures;

		/* Nothing new from a kind that is not in the selection, or from a
		   choice that changes nothing for what is made. */
		if ((!with_folder && fo > 0) || (!with_file && fi > 0) ||
		    (!symlink && rel > 0)) {
			continue;
		}
		if (symlink && !(supported & NEMO_LINK_FILE_SYMLINK)) {
			continue;
		}
		if (with_folder && options.folder_kind == NEMO_MAKE_JUNCTION &&
		    !(supported & NEMO_LINK_JUNCTION)) {
			continue;
		}

		name = g_strdup_printf ("case-%03d", number++);
		root = g_build_filename (tmp, name, NULL);
		from = g_build_filename (root, "from", NULL);
		to = beside ? g_strdup (from) : g_build_filename (root, "to", NULL);
		payload = g_build_filename (from, "payload.txt", NULL);
		folder = g_build_filename (from, "folder", NULL);
		g_mkdir_with_parents (folder, 0700);
		g_mkdir_with_parents (to, 0700);
		check (g_file_set_contents (payload, "payload", -1, NULL));

		if (with_file) {
			uris = g_list_append (uris, uri_of (payload));
		}
		if (with_folder) {
			uris = g_list_append (uris, uri_of (folder));
		}

		check (run_link_job (uris, to, &options, window));

		if (with_file) {
			made = made_name ("payload.txt", options.file_kind, beside);
			check_made (to, made, payload, options.file_kind, FALSE, &options);
			g_free (made);
		}
		if (with_folder) {
			made = made_name ("folder", options.folder_kind, beside);
			check_made (to, made, folder, options.folder_kind, TRUE, &options);
			g_free (made);
		}

		/* The originals are still what they were. */
		check (kind_of (payload) == NEMO_LINK_NONE);
		check (kind_of (folder) == NEMO_LINK_NONE && g_file_test (folder, G_FILE_TEST_IS_DIR));

		if (failures > before) {
			g_printerr ("  in %s: %s%s%s, %s, %s\n", name,
				    with_file ? make_word (options.file_kind) : "",
				    with_file && with_folder ? " and " : "",
				    with_folder ? make_word (options.folder_kind) : "",
				    rel ? "relative" : "absolute",
				    beside ? "beside" : "elsewhere");
		}

		g_list_free_full (uris, g_free);
		g_free (folder);
		g_free (payload);
		g_free (to);
		g_free (from);
		g_free (root);
		g_free (name);
	}

	if (failures == 0) {
		g_print ("make link job (every): %d combinations, all checks passed\n", number);
	}
}

static void
redo_done (GObject *source, GAsyncResult *res, gpointer data)
{
	gboolean user_cancel = FALSE;

	job_succeeded = nemo_file_undo_info_apply_finish (NEMO_FILE_UNDO_INFO (source), res,
							  &user_cancel, NULL);
	job_finished = TRUE;
	gtk_main_quit ();
}

/* Takes away a link Make link left in dir, the way an undo would. */
static void
remove_made (const char *dir, const char *name, NemoMakeLink kind)
{
	char *made = made_name (name, kind, FALSE);
	char *path = g_build_filename (dir, made, NULL);

	if (kind_of (path) == NEMO_LINK_JUNCTION) {
		check (g_rmdir (path) == 0);
	} else {
		check (g_remove (path) == 0);
	}
	g_free (path);
	g_free (made);
}

/* A redo makes the same kind of link the dialog asked for the first time. */
static void
check_redo (const char *tmp, guint supported, GtkWidget *window)
{
	NemoLinkOptions sets[] = {
		{ NEMO_MAKE_SHORTCUT, NEMO_MAKE_HARDLINK, FALSE },
		{ NEMO_MAKE_SYMLINK, NEMO_MAKE_SYMLINK, TRUE },
		{ NEMO_MAKE_JUNCTION, NEMO_MAKE_SHORTCUT, FALSE },
	};
	guint i;

	for (i = 0; i < G_N_ELEMENTS (sets); i++) {
		const NemoLinkOptions *options = &sets[i];
		char *name, *root, *from, *to, *payload, *folder, *made;
		NemoFileUndoInfo *info;
		GList *uris = NULL;
		int round;

		if ((options->file_kind == NEMO_MAKE_SYMLINK && !(supported & NEMO_LINK_FILE_SYMLINK)) ||
		    (options->folder_kind == NEMO_MAKE_JUNCTION && !(supported & NEMO_LINK_JUNCTION))) {
			continue;
		}

		name = g_strdup_printf ("redo-%u", i);
		root = g_build_filename (tmp, name, NULL);
		from = g_build_filename (root, "from", NULL);
		to = g_build_filename (root, "to", NULL);
		payload = g_build_filename (from, "payload.txt", NULL);
		folder = g_build_filename (from, "folder", NULL);
		g_mkdir_with_parents (folder, 0700);
		g_mkdir_with_parents (to, 0700);
		check (g_file_set_contents (payload, "payload", -1, NULL));
		uris = g_list_append (uris, uri_of (payload));
		uris = g_list_append (uris, uri_of (folder));

		check (run_link_job (uris, to, sets + i, window));
		info = nemo_file_undo_manager_get_action ();
		check (info != NULL);

		/* The first run, then the redo, each checked the same way. */
		for (round = 0; round < 2 && info != NULL; round++) {
			int before = failures;

			if (round == 1) {
				guint timeout_id;

				g_object_ref (info);
				remove_made (to, "payload.txt", options->file_kind);
				remove_made (to, "folder", options->folder_kind);
				check (g_file_set_contents (payload, "payload", -1, NULL));
				job_finished = FALSE;
				job_succeeded = FALSE;
				nemo_file_undo_manager_push_flag ();
				nemo_file_undo_info_apply_async (info, FALSE, GTK_WINDOW (window), redo_done, NULL);
				timeout_id = g_timeout_add_seconds (JOB_TIMEOUT_SECONDS, give_up, NULL);
				gtk_main ();
				g_source_remove (timeout_id);
				check (job_finished && job_succeeded);
				g_object_unref (info);
			}

			made = made_name ("payload.txt", options->file_kind, FALSE);
			check_made (to, made, payload, options->file_kind, FALSE, options);
			g_free (made);
			made = made_name ("folder", options->folder_kind, FALSE);
			check_made (to, made, folder, options->folder_kind, TRUE, options);
			g_free (made);

			if (failures > before) {
				g_printerr ("  in %s, %s: file %s, folder %s, %s\n", name,
					    round == 0 ? "first run" : "redo",
					    make_word (options->file_kind), make_word (options->folder_kind),
					    options->relative ? "relative" : "absolute");
			}
		}

		g_list_free_full (uris, g_free);
		g_free (folder);
		g_free (payload);
		g_free (to);
		g_free (from);
		g_free (root);
		g_free (name);
	}
}

int
main (int argc, char *argv[])
{
	NemoLinkOptions options = { NEMO_MAKE_SYMLINK, NEMO_MAKE_SYMLINK, TRUE };
	GtkWidget *window;
	GList *uris = NULL;
	const char *how;
	char *home, *dst_uri;
	/* Freed on every return, the early ones too. */
	g_autofree char *tmp = NULL, *src_dir = NULL, *dst_dir = NULL;
	g_autofree char *payload = NULL, *folder = NULL, *made_file = NULL, *made_folder = NULL;
	char *text, *want, *contents = NULL;
	guint supported, timeout_id;
	gboolean with_file, with_folder, portable_run;
	FILE *fp;

	home = test_scratch_config_home ("nemo-make-link-home-XXXXXX");
	nemo_global_preferences_init ();
	test_init (&argc, &argv);
	how = (argc > 1) ? argv[1] : "relative";
	portable_run = g_strcmp0 (how, "shortcut-portable") == 0;

	/* Portable needs the files where a variable covers them, and home is
	   the one there is off Windows. */
	if (portable_run || g_strcmp0 (how, "every") == 0) {
		tmp = test_scratch_dir_in (g_get_home_dir (), "nemo-make-link-XXXXXX", NULL);
	} else {
		tmp = test_scratch_dir ("nemo-make-link-XXXXXX", NULL);
	}
	g_free (home);
	src_dir = g_build_filename (tmp, "from", NULL);
	dst_dir = g_build_filename (tmp, "to", NULL);
	g_mkdir_with_parents (src_dir, 0700);
	g_mkdir_with_parents (dst_dir, 0700);

	payload = g_build_filename (src_dir, "payload.txt", NULL);
	folder = g_build_filename (src_dir, "folder", NULL);
	made_file = g_build_filename (dst_dir, "payload.txt", NULL);
	made_folder = g_build_filename (dst_dir, "folder", NULL);
	check (g_file_set_contents (payload, "payload", -1, NULL));
	g_mkdir_with_parents (folder, 0700);

	supported = nemo_link_kinds_supported (dst_dir);

	if (g_strcmp0 (how, "every") == 0) {
		window = test_window_new ("make link test", 5);
		gtk_widget_show (window);
		check_every (tmp, supported, window);
		return failures > 0 ? EXIT_FAILURE : EXIT_SUCCESS;
	}
	if (g_strcmp0 (how, "redo") == 0) {
		window = test_window_new ("make link test", 5);
		gtk_widget_show (window);
		check_redo (tmp, supported, window);
		if (failures == 0) {
			g_print ("make link job (redo): all checks passed\n");
		}
		return failures > 0 ? EXIT_FAILURE : EXIT_SUCCESS;
	}
	if (g_strcmp0 (how, "names") == 0) {
		window = test_window_new ("make link test", 5);
		gtk_widget_show (window);
		check_names (src_dir, payload, folder, supported, window);
		return failures > 0 ? EXIT_FAILURE : EXIT_SUCCESS;
	}
	with_file = g_strcmp0 (how, "junction") != 0;
	with_folder = g_strcmp0 (how, "hardlink") != 0;

	if (g_strcmp0 (how, "absolute") == 0) {
		options.relative = FALSE;
	} else if (g_strcmp0 (how, "hardlink") == 0) {
		options.file_kind = NEMO_MAKE_HARDLINK;
	} else if (g_strcmp0 (how, "junction") == 0) {
		options.folder_kind = NEMO_MAKE_JUNCTION;
	} else if (g_str_has_prefix (how, "shortcut")) {
		options.folder_kind = NEMO_MAKE_SHORTCUT;
		options.file_kind = NEMO_MAKE_SHORTCUT;
	}

	if (g_strcmp0 (how, "junction") == 0
	    ? !(supported & NEMO_LINK_JUNCTION)
	    : (g_strcmp0 (how, "hardlink") != 0 && !g_str_has_prefix (how, "shortcut") &&
	       !(supported & NEMO_LINK_FILE_SYMLINK))) {
		g_printerr ("note: that kind of link cannot be made here, nothing to check\n");
		return 77;
	}

	if (with_file) {
		uris = g_list_append (uris, uri_of (payload));
	}
	if (with_folder) {
		uris = g_list_append (uris, uri_of (folder));
	}
	dst_uri = uri_of (dst_dir);

	window = test_window_new ("make link test", 5);
	gtk_widget_show (window);

	nemo_file_operations_symlink (uris, NULL, dst_uri, &options, window, job_done, NULL);

	timeout_id = g_timeout_add_seconds (JOB_TIMEOUT_SECONDS, give_up, NULL);
	gtk_main ();
	g_source_remove (timeout_id);

	check (job_finished);
	check (job_succeeded);

	if (g_strcmp0 (how, "relative") == 0) {
		want = g_build_filename ("..", "from", "payload.txt", NULL);
		text = target_of (made_file);
		check (g_strcmp0 (text, want) == 0);
		g_free (text);
		g_free (want);

		want = g_build_filename ("..", "from", "folder", NULL);
		text = target_of (made_folder);
		check (g_strcmp0 (text, want) == 0);
		check (kind_of (made_folder) == NEMO_LINK_DIR_SYMLINK);
		g_free (text);
		g_free (want);

		/* And it leads where it should. */
		check (g_file_get_contents (made_file, &contents, NULL, NULL) &&
		       g_strcmp0 (contents, "payload") == 0);
		g_clear_pointer (&contents, g_free);
	} else if (g_strcmp0 (how, "absolute") == 0) {
		text = target_of (made_file);
		check (text != NULL && g_path_is_absolute (text));
		check (g_strcmp0 (text, payload) == 0);
		g_free (text);
		check (kind_of (made_folder) == NEMO_LINK_DIR_SYMLINK);
	} else if (g_strcmp0 (how, "hardlink") == 0) {
		/* Not a link at all to look at: a second name for the same file. */
		check (kind_of (made_file) == NEMO_LINK_NONE);
		fp = g_fopen (made_file, "ab");
		check (fp != NULL);
		if (fp != NULL) {
			fputs (" more", fp);
			fclose (fp);
		}
		check (g_file_get_contents (payload, &contents, NULL, NULL) &&
		       g_strcmp0 (contents, "payload more") == 0);
		g_clear_pointer (&contents, g_free);
	} else if (g_str_has_prefix (how, "shortcut")) {
		char *lnk_file = g_strconcat (made_file, ".lnk", NULL);
		char *lnk_folder = g_strconcat (made_folder, ".lnk", NULL);

		/* Only the .lnk files, under their own names. */
		check (!g_file_test (made_file, G_FILE_TEST_EXISTS));
		check (!g_file_test (made_folder, G_FILE_TEST_EXISTS));
		check_shortcut (lnk_file, payload, portable_run);
		check_shortcut (lnk_folder, folder, portable_run);

		/* The pair moved together still finds its way on the relative path.
		   Windows needs its own resolve for that, so only here. */
#ifndef G_OS_WIN32
		{
			char *moved = g_build_filename (tmp, "moved", NULL);
			char *moved_from = g_build_filename (moved, "from", NULL);
			char *moved_to = g_build_filename (moved, "to", NULL);
			char *moved_lnk = g_build_filename (moved_to, "payload.txt.lnk", NULL);
			char *moved_payload = g_build_filename (moved_from, "payload.txt", NULL);

			g_mkdir_with_parents (moved, 0700);
			check (g_rename (src_dir, moved_from) == 0);
			check (g_rename (dst_dir, moved_to) == 0);
			check_shortcut (moved_lnk, moved_payload, portable_run);
			check (g_rename (moved_from, src_dir) == 0);
			check (g_rename (moved_to, dst_dir) == 0);
			g_free (moved_payload);
			g_free (moved_lnk);
			g_free (moved_to);
			g_free (moved_from);
			g_free (moved);
		}
#endif
		g_free (lnk_folder);
		g_free (lnk_file);
	} else {
		check (kind_of (made_folder) == NEMO_LINK_JUNCTION);
	}

	/* The originals are where they were. */
	check (kind_of (payload) == NEMO_LINK_NONE);
	check (g_file_test (folder, G_FILE_TEST_IS_DIR));

	g_list_free_full (uris, g_free);
	g_free (dst_uri);

	if (failures > 0) {
		return EXIT_FAILURE;
	}

	g_print ("make link job (%s): all checks passed\n", how);
	return EXIT_SUCCESS;
}
