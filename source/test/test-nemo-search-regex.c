/* An invalid filename regex must not crash the search. The advanced engine
 * left the compiled pattern NULL but kept regex mode on, then per file passed
 * that NULL to g_regex_match and unref'd an uninitialized match. Here we run
 * a real search over a temp dir with a deliberately broken pattern and just
 * require the engine to finish. Without the fix the worker thread faults. */

#include <config.h>

#include <string.h>
#include <gtk/gtk.h>
#include <libnemo-private/nemo-search-engine-advanced.h>
#include <libnemo-private/nemo-query.h>

#include "test-scratch.h"

static gboolean done;

static gboolean
put (const char *dir, const char *name, const char *text)
{
	g_autofree char *path = g_build_filename (dir, name, NULL);

	if (!g_file_set_contents (path, text, -1, NULL)) {
		g_printerr ("FAIL: could not write %s\n", path);
		return FALSE;
	}
	return TRUE;
}

static void
finished_cb (G_GNUC_UNUSED NemoSearchEngine *engine, G_GNUC_UNUSED gpointer data)
{
	done = TRUE;
}

int
main (int argc, char *argv[])
{
	NemoSearchEngine *engine;
	NemoQuery        *query;
	char             *dir, *uri;
	int               spins = 0;

	gtk_init_check (&argc, &argv);

	/* A directory with a file or two to iterate over. */
	dir = test_scratch_dir ("nemo-search-regex-XXXXXX", NULL);
	if (!put (dir, "somefile.txt", "x") || !put (dir, "another.txt", "y")) {
		return 1;
	}

	engine = nemo_search_engine_advanced_new ();
	g_signal_connect (engine, "finished", G_CALLBACK (finished_cb), NULL);

	query = nemo_query_new ();
	uri = g_strdup_printf ("file://%s", dir);
	nemo_query_set_location (query, uri);
	g_free (uri);
	nemo_query_set_use_file_regex (query, TRUE);
	/* Unbalanced bracket - g_regex_new fails to compile this. */
	nemo_query_set_file_pattern (query, "[");
	nemo_search_engine_set_query (engine, query);
	g_object_unref (query);

	nemo_search_engine_start (engine);

	/* pump the loop until the worker thread reports done (bounded) */
	while (!done && spins++ < 500) {
		g_main_context_iteration (NULL, FALSE);
		g_usleep (10000);
	}

	g_object_unref (engine);
	g_free (dir);

	if (!done) {
		g_printerr ("FAIL: search never finished\n");
		return 1;
	}
	g_print ("nemo-search-regex: all checks passed\n");
	return 0;
}
