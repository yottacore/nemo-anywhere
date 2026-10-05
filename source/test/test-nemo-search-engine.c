/* Filename search through the front door - nemo_search_engine_new(), whichever
 * backend that picks. Content search is covered separately.
 *
 * On Windows that front door can be the search index rather than a walk, and an
 * index does not see a file created a moment ago, so these checks want another
 * look before the Windows suite is switched on.
 *
 * The engine walks the tree itself where there is no index, so a query with no
 * location starts at the filesystem root and takes as long as the box is big.
 * Every search here is pointed at a throwaway directory.
 */

#include <config.h>

#include <glib/gstdio.h>
#include <gtk/gtk.h>

#include <libnemo-private/nemo-global-preferences.h>
#include <libnemo-private/nemo-search-engine.h>

#include "test-scratch.h"
#include "test-check.h"

typedef struct {
	GList *names;		/* basename of every hit, owned */
	gboolean finished;
	GMainLoop *loop;
} Run;

static void
hits_added (G_GNUC_UNUSED NemoSearchEngine *engine, GList *hits, gpointer data)
{
	Run *run = data;
	GList *l;

	/* The hits belong to whoever was handed them - in the app that is the
	   search directory, and it frees them on reset. */
	for (l = hits; l != NULL; l = l->next) {
		FileSearchResult *hit = l->data;

		run->names = g_list_prepend (run->names, g_path_get_basename (hit->uri));
		file_search_result_free (hit);
	}
}

static void
search_finished (G_GNUC_UNUSED NemoSearchEngine *engine, gpointer data)
{
	Run *run = data;

	run->finished = TRUE;
	g_main_loop_quit (run->loop);
}

static gboolean
give_up (gpointer data)
{
	Run *run = data;

	g_main_loop_quit (run->loop);

	return TRUE;
}

static GList *
search (const char *location, const char *pattern, gboolean recurse)
{
	NemoSearchEngine *engine;
	NemoQuery *query;
	Run run = { NULL, FALSE, NULL };
	guint timeout;

	engine = nemo_search_engine_new ();
	run.loop = g_main_loop_new (NULL, FALSE);

	g_signal_connect (engine, "hits-added", G_CALLBACK (hits_added), &run);
	g_signal_connect (engine, "finished", G_CALLBACK (search_finished), &run);

	query = nemo_query_new ();
	nemo_query_set_location (query, location);
	nemo_query_set_file_pattern (query, pattern);
	nemo_query_set_recurse (query, recurse);
	nemo_search_engine_set_query (engine, query);
	g_object_unref (query);

	nemo_search_engine_start (engine);

	timeout = g_timeout_add_seconds (30, give_up, &run);
	g_main_loop_run (run.loop);
	g_source_remove (timeout);

	if (!run.finished) {
		g_printerr ("FAIL search for '%s' never finished\n", pattern);
		failures++;
		nemo_search_engine_stop (engine);
	}

	/* Belt and braces. The unref below takes the handlers with it, but the
	   walk is on its own thread and the frame these point at is going. */
	g_signal_handlers_disconnect_by_data (engine, &run);

	g_main_loop_unref (run.loop);
	g_object_unref (engine);

	return run.names;
}

static gboolean
found (GList *names, const char *name)
{
	return g_list_find_custom (names, name, (GCompareFunc) g_strcmp0) != NULL;
}

static void
write_file (const char *dir, const char *name)
{
	char *path = g_build_filename (dir, name, NULL);

	if (!g_file_set_contents (path, "nothing to see\n", -1, NULL)) {
		g_printerr ("FAIL could not write %s\n", path);
		failures++;
	}

	g_free (path);
}

static void
remove_file (const char *dir, const char *name)
{
	char *path = g_build_filename (dir, name, NULL);

	g_unlink (path);
	g_free (path);
}

int
main (int argc, char *argv[])
{
	char *scratch, *dir, *sub, *uri;
	GList *names;

	scratch = test_scratch_config_home ("nemo-search-engine-home-XXXXXX");

	gtk_init_check (&argc, &argv);
	nemo_global_preferences_init ();

	dir = test_scratch_dir ("nemo-search-engine-XXXXXX", NULL);
	sub = g_build_filename (dir, "deeper", NULL);
	g_mkdir_with_parents (sub, 0755);

	write_file (dir, "needle-top.txt");
	write_file (sub, "needle-deep.txt");
	write_file (dir, "haystack.txt");

	uri = g_filename_to_uri (dir, NULL, NULL);

	names = search (uri, "needle", TRUE);
	check (found (names, "needle-top.txt"));
	check (found (names, "needle-deep.txt"));
	check (!found (names, "haystack.txt"));
	g_list_free_full (names, g_free);

	/* Without recursion the search has to stop at the top directory. */
	names = search (uri, "needle", FALSE);
	check (found (names, "needle-top.txt"));
	check (!found (names, "needle-deep.txt"));
	g_list_free_full (names, g_free);

	/* A pattern nothing matches still has to come back, or the view that
	   asked for it sits on a spinner forever. */
	names = search (uri, "no-such-file-anywhere", TRUE);
	check (names == NULL);
	g_list_free_full (names, g_free);

	remove_file (dir, "needle-top.txt");
	remove_file (dir, "haystack.txt");
	remove_file (sub, "needle-deep.txt");
	g_rmdir (sub);
	g_rmdir (dir);

	g_free (uri);
	g_free (sub);
	g_free (dir);
	g_free (scratch);

	if (failures == 0) {
		g_print ("nemo-search-engine: all checks passed\n");
	}

	return failures == 0 ? 0 : 1;
}
