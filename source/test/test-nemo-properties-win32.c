/* Which properties window a selection gets on Windows. The shell's own sheet
 * only knows real paths that share one folder, so everything else has to fall
 * back to ours - and falling back silently is the failure that matters, because
 * it reads as the Properties item doing nothing at all. Windows-only. */

#include <config.h>

#include <stdlib.h>
#include <string.h>
#include <gio/gio.h>
#include <glib/gstdio.h>

#include <libnemo-private/nemo-file.h>

#include "nemo-properties-win32.h"

#include "test-scratch.h"
#include "test-check.h"

static GList *
file_list_for_paths (const char * const *paths,
		     guint               count)
{
	GList *files = NULL;
	guint i;

	for (i = 0; i < count; i++) {
		GFile *location = g_file_new_for_path (paths[i]);

		files = g_list_append (files, nemo_file_get (location));
		g_object_unref (location);
	}

	return files;
}

static GList *
file_list_for_uri (const char *uri)
{
	return g_list_append (NULL, nemo_file_get_by_uri (uri));
}

static char *
write_file (const char *dir,
	    const char *name)
{
	char *path = g_build_filename (dir, name, NULL);
	gboolean written = g_file_set_contents (path, "x", 1, NULL);

	check (written);

	return path;
}

int
main (void)
{
	char *root, *nest;
	char *alpha, *beta, *deep, *missing;
	const char *pair[2];
	GList *files;

	root = test_scratch_dir ("nemo-props-XXXXXX", NULL);
	g_assert (root != NULL);

	nest = g_build_filename (root, "nest", NULL);
	g_mkdir_with_parents (nest, 0755);

	alpha = write_file (root, "alpha.txt");
	beta = write_file (root, "beta.txt");
	deep = write_file (nest, "gamma.txt");
	missing = g_build_filename (root, "never-written.txt", NULL);

	/* One real file is the ordinary case, and the whole point of the item. */
	pair[0] = alpha;
	files = file_list_for_paths (pair, 1);
	check (nemo_properties_win32_can_show (files));
	nemo_file_list_free (files);

	/* Several, as long as one folder holds them all - that is the only shape
	 * the shell's multi-item sheet has. */
	pair[0] = alpha;
	pair[1] = beta;
	files = file_list_for_paths (pair, 2);
	check (nemo_properties_win32_can_show (files));
	nemo_file_list_free (files);

	/* A folder is an object like any other. */
	pair[0] = nest;
	files = file_list_for_paths (pair, 1);
	check (nemo_properties_win32_can_show (files));
	nemo_file_list_free (files);

	/* Two folders deep is where a search result set sits, and the sheet
	 * cannot show it - ours has to. */
	pair[0] = alpha;
	pair[1] = deep;
	files = file_list_for_paths (pair, 2);
	check (!nemo_properties_win32_can_show (files));
	nemo_file_list_free (files);

	/* Gone between the click and the sheet: ours reports it, the shell would
	 * refuse from a worker thread with nowhere to say so. */
	pair[0] = missing;
	files = file_list_for_paths (pair, 1);
	check (!nemo_properties_win32_can_show (files));
	nemo_file_list_free (files);

	/* Virtual locations have no path at all. */
	files = file_list_for_uri ("trash:///");
	check (!nemo_properties_win32_can_show (files));
	nemo_file_list_free (files);

	files = file_list_for_uri ("network:///");
	check (!nemo_properties_win32_can_show (files));
	nemo_file_list_free (files);

	/* Nothing selected is not something to hand the shell either. */
	check (!nemo_properties_win32_can_show (NULL));

	g_unlink (alpha);
	g_unlink (beta);
	g_unlink (deep);
	g_rmdir (nest);
	g_rmdir (root);

	g_free (missing);
	g_free (deep);
	g_free (beta);
	g_free (alpha);
	g_free (nest);
	g_free (root);

	if (failures > 0) {
		g_printerr ("%d check(s) failed\n", failures);
		return EXIT_FAILURE;
	}

	g_print ("OK\n");

	return EXIT_SUCCESS;
}
