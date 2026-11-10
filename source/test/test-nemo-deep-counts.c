/* The folder properties dialog shows folders and files on rows of their own,
 * each with its own hidden count, so the walk has to keep hidden folders and
 * hidden files apart. It used to lump them into one number. */

#include <config.h>

#include <stdlib.h>
#include <glib/gstdio.h>
#include <gtk/gtk.h>

#include <libnemo-private/nemo-file.h>
#include <libnemo-private/nemo-file-attributes.h>
#include <libnemo-private/nemo-global-preferences.h>

#include "test-scratch.h"
#include "test-check.h"

static void
put (const char *base, const char *rel, const char *contents)
{
	g_autofree char *path = g_build_filename (base, rel, NULL);

	if (contents == NULL) {
		check (g_mkdir (path, 0755) == 0);
	} else {
		check (g_file_set_contents (path, contents, -1, NULL));
	}
}

int
main (int argc, char **argv)
{
	g_autofree char *tree = NULL;
	g_autofree char *uri = NULL;
	char *tmp;
	NemoFile *file;
	NemoRequestStatus status = NEMO_REQUEST_NOT_STARTED;
	guint dirs, files, unreadable, hidden_dirs, hidden_files;
	goffset size;
	int spins;

	gtk_init_check (&argc, &argv);

	tmp = test_scratch_config_home ("nemo-deep-counts-XXXXXX");
	if (tmp == NULL) {
		g_printerr ("FAIL could not make a temp dir\n");
		return EXIT_FAILURE;
	}

	nemo_global_preferences_init ();

	tree = g_build_filename (tmp, "tree", NULL);
	check (g_mkdir (tree, 0755) == 0);
	put (tree, "sub", NULL);
	put (tree, "sub/a.txt", "abc");
	put (tree, "sub/.quiet.txt", "q");
	put (tree, "sub/deeper", NULL);
	put (tree, ".stash", NULL);
	put (tree, ".stash/.inside", NULL);
	put (tree, ".stash/x.txt", "xx");
	put (tree, "b.txt", "hello");

	uri = g_filename_to_uri (tree, NULL, NULL);
	file = nemo_file_get_by_uri (uri);
	nemo_file_monitor_add (file, &status, NEMO_FILE_ATTRIBUTE_INFO | NEMO_FILE_ATTRIBUTE_DEEP_COUNTS);

	/* Until its info is in, the file isn't known to be a folder and reports
	   zero counts as done. */
	for (spins = 0; spins < 5000 && !nemo_file_is_directory (file); spins++) {
		g_main_context_iteration (NULL, FALSE);
		g_usleep (2000);
	}
	check (nemo_file_is_directory (file));

	for (spins = 0; spins < 5000; spins++) {
		status = nemo_file_get_deep_counts (file, &dirs, &files, &unreadable,
						    &hidden_dirs, &hidden_files, &size, TRUE);
		if (status == NEMO_REQUEST_DONE) {
			break;
		}
		if (status == NEMO_REQUEST_NOT_STARTED) {
			nemo_file_recompute_deep_counts (file);
		}
		g_main_context_iteration (NULL, FALSE);
		g_usleep (2000);
	}

	check (status == NEMO_REQUEST_DONE);
	/* The walk goes into a hidden folder too, and what is in there only
	   counts as hidden if it is hidden itself. */
	check (dirs == 2);          /* sub, sub/deeper */
	check (files == 3);         /* sub/a.txt, b.txt, .stash/x.txt */
	check (hidden_dirs == 2);   /* .stash, .stash/.inside */
	check (hidden_files == 1);  /* sub/.quiet.txt */
	check (unreadable == 0);
	check (size >= 3 + 1 + 2 + 5);

	nemo_file_monitor_remove (file, &status);
	nemo_file_unref (file);
	g_free (tmp);

	if (failures == 0)
		g_print ("nemo-deep-counts: all checks passed\n");

	return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
