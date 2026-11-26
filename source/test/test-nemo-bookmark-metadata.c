/* A damaged bookmark-metadata file is kept when the bookmarks are saved.
 * "aside" has it moved to .bad. "aside-fails" puts a folder in the way of
 * .bad, so the file has to stay where it is and nothing may be saved over it.
 * Each runs in a process of its own, since the block lasts until restart.
 * The warning each logs is expected. Runs against a throwaway config root. */

#include <config.h>

#include <stdlib.h>
#include <string.h>
#include <gio/gio.h>
#include <glib/gstdio.h>

#include "test-scratch.h"
#include "test-check.h"

/* For the static helpers and the pending op queue. */
#include "nemo-bookmark-list.c"

#define OP_TIMEOUT_SECONDS 30

static const char *damaged = "[Scratch\nIconEmblems=emblem-important;\n";

static void
wait_for_ops (NemoBookmarkList *bookmarks)
{
	gint64 give_up_at = g_get_monotonic_time () + OP_TIMEOUT_SECONDS * G_USEC_PER_SEC;

	while (!g_queue_is_empty (bookmarks->pending_ops)) {
		if (g_get_monotonic_time () > give_up_at) {
			g_printerr ("FAIL: a bookmarks load or save did not finish within %d seconds\n",
				    OP_TIMEOUT_SECONDS);
			failures++;
			return;
		}
		g_main_context_iteration (NULL, TRUE);
	}
}

static char *
read_file (const char *path)
{
	char *text = NULL;

	if (!g_file_get_contents (path, &text, NULL, NULL)) {
		return NULL;
	}
	return text;
}

int
main (int argc, char **argv)
{
	g_autofree char *tmpdir = NULL;
	gboolean aside_fails;
	NemoBookmarkList *bookmarks;
	GFile *file, *parent, *location;
	char *dir, *path, *uri, *line, *metadata, *aside, *blocker, *text;

	if (argc < 2 || (strcmp (argv[1], "aside") != 0 && strcmp (argv[1], "aside-fails") != 0)) {
		g_printerr ("Syntax: %s aside|aside-fails\n", argv[0]);
		return EXIT_FAILURE;
	}
	aside_fails = strcmp (argv[1], "aside-fails") == 0;

	tmpdir = test_scratch_config_home ("nemo-bookmark-metadata-XXXXXX");
	g_assert (tmpdir != NULL);

	/* A bookmark looks up its icon on the screen. */
	if (!gtk_init_check (&argc, &argv)) {
		g_print ("SKIP: no display\n");
		return 77;
	}
	nemo_global_preferences_init ();

	/* The metadata file is read only when there's a bookmarks file. */
	file = nemo_bookmark_list_get_file ();
	parent = g_file_get_parent (file);
	dir = g_file_get_path (parent);
	path = g_file_get_path (file);
	uri = g_filename_to_uri (tmpdir, NULL, NULL);
	line = g_strdup_printf ("%s Scratch\n", uri);
	check (g_mkdir_with_parents (dir, 0700) == 0);
	check (g_file_set_contents (path, line, -1, NULL));

	metadata = bookmark_metadata_path ();
	aside = g_strconcat (metadata, ".bad", NULL);
	blocker = g_build_filename (aside, "in-the-way", NULL);
	g_free (dir);
	dir = g_path_get_dirname (metadata);
	check (g_mkdir_with_parents (dir, 0700) == 0);
	check (g_file_set_contents (metadata, damaged, -1, NULL));
	if (aside_fails) {
		check (g_mkdir_with_parents (aside, 0700) == 0);
		check (g_file_set_contents (blocker, "x", -1, NULL));
	}

	bookmarks = nemo_bookmark_list_get_default ();
	wait_for_ops (bookmarks);
	check (nemo_bookmark_list_length (bookmarks) == 1);

	/* Any change saves the bookmarks, and the metadata file with them. */
	location = g_file_new_for_path (g_get_tmp_dir ());
	nemo_bookmark_list_append (bookmarks, nemo_bookmark_new (location, "Added", NULL, NULL));
	g_object_unref (location);
	wait_for_ops (bookmarks);
	check (nemo_bookmark_list_length (bookmarks) == 2);

	if (aside_fails) {
		text = read_file (metadata);
		check (g_strcmp0 (text, damaged) == 0);
		g_free (text);
		g_remove (blocker);
		g_rmdir (aside);
	} else {
		text = read_file (aside);
		check (g_strcmp0 (text, damaged) == 0);
		g_free (text);
		g_remove (aside);
	}

	/* The bookmarks themselves still save. */
	text = read_file (path);
	check (text != NULL && strstr (text, "Added") != NULL);
	g_free (text);

	g_remove (metadata);
	g_remove (path);
	g_free (blocker);
	g_free (aside);
	g_free (metadata);
	g_free (line);
	g_free (uri);
	g_free (path);
	g_free (dir);
	g_object_unref (parent);
	g_object_unref (file);

	if (failures == 0) {
		g_print ("bookmark-metadata %s: all checks passed\n", argv[1]);
	}
	return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
