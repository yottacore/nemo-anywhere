/* The scratch helper's early tree removal. Tests plant links in their fixtures,
 * so the walk has to take a link as a link and never as a way in. Eight tests
 * used to roll their own version of this walk and one of them planted a link in
 * the tree it removed. */

#include <config.h>

#include <stdlib.h>
#include <unistd.h>
#include <glib.h>
#include <glib/gstdio.h>

#include "test-scratch.h"
#include "test-check.h"

static char *
make_file (const char *dir, const char *name)
{
	char *path = g_build_filename (dir, name, NULL);

	if (!g_file_set_contents (path, "x", -1, NULL)) {
		g_printerr ("could not write %s - the test cannot mean anything\n", path);
		exit (EXIT_FAILURE);
	}
	return path;
}

int
main (void)
{
	g_autofree char *root = NULL, *keep_dir = NULL, *precious = NULL;
	g_autofree char *doomed = NULL, *inner = NULL, *inner_file = NULL, *link = NULL;
	g_autofree char *stranger = NULL, *stranger_file = NULL;

	root = test_scratch_dir ("nemo-scratch-guard-XXXXXX", NULL);
	g_assert (root != NULL);

	/* What must survive, beside the tree rather than inside it. */
	keep_dir = g_build_filename (root, "keep", NULL);
	g_mkdir_with_parents (keep_dir, 0700);
	precious = make_file (keep_dir, "precious.txt");

	doomed = g_build_filename (root, "doomed", NULL);
	inner = g_build_filename (doomed, "inner", NULL);
	g_mkdir_with_parents (inner, 0700);
	inner_file = make_file (inner, "file.txt");

	link = g_build_filename (inner, "way-out", NULL);
	if (symlink (keep_dir, link) != 0) {
		g_printerr ("SKIP: symlink() unavailable\n");
		return 77;
	}

	check (test_scratch_remove_tree (doomed));
	check (!g_file_test (doomed, G_FILE_TEST_EXISTS));
	check (!g_file_test (inner_file, G_FILE_TEST_EXISTS));

	/* The link was removed as itself, and what it pointed at wasn't touched. */
	check (g_file_test (keep_dir, G_FILE_TEST_IS_DIR));
	check (g_file_test (precious, G_FILE_TEST_IS_REGULAR));

	/* Anything outside a directory this process made is refused outright. The
	 * one that exists is made by hand and thrown away by name below: if this
	 * guard ever breaks, the test shouldn't be the thing that takes out
	 * whatever it was pointed at. */
	stranger = g_build_filename (g_get_tmp_dir (), "nemo-scratch-guard-not-ours", NULL);
	g_mkdir_with_parents (stranger, 0700);
	stranger_file = make_file (stranger, "still-here.txt");

	check (!test_scratch_remove_tree (stranger));
	check (g_file_test (stranger_file, G_FILE_TEST_IS_REGULAR));

	g_remove (stranger_file);
	g_rmdir (stranger);

	if (failures == 0) {
		g_print ("scratch guard: all checks passed\n");
	}
	g_printerr ("%d failure(s)\n", failures);
	return failures == 0 ? 0 : 1;
}
