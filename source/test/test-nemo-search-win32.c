/* The Windows Search engine. The statement it builds is checked word for word;
 * the index itself is then asked whether it holds the home folder, and a search
 * is run through the engine end to end - through the index where the folder is
 * covered, through the walk where it is not - and has to finish either way. */

#include <config.h>

#include <string.h>
#include <gtk/gtk.h>
#include <libnemo-private/nemo-global-preferences.h>
#include <libnemo-private/nemo-search-engine-win32.h>
#include <libnemo-private/nemo-query.h>

#include "test-scratch.h"

static int failures;
static gboolean done;
static int n_hits;

static void
check (gboolean ok, const char *what)
{
	g_print ("%s: %s\n", ok ? "PASS" : "FAIL", what);
	if (!ok) {
		failures++;
	}
}

static void
check_sql (const char *what, const char *got, const char *expected)
{
	check (g_strcmp0 (got, expected) == 0, what);
	if (g_strcmp0 (got, expected) != 0) {
		g_print ("     got: %s\n  wanted: %s\n", got, expected);
	}
}

static void
hits_added_cb (NemoSearchEngine *engine, GList *hits, gpointer data)
{
	for (GList *l = hits; l != NULL; l = l->next) {
		file_search_result_free (l->data);
		n_hits++;
	}
}

static void
finished_cb (NemoSearchEngine *engine, gpointer data)
{
	done = TRUE;
}

static void
test_sql (void)
{
	char *sql;

	sql = nemo_search_win32_build_sql ("C:\\Users\\me", TRUE, "report *.txt", FALSE, NULL);
	check_sql ("scope, two name words", sql,
		   "SELECT System.ItemPathDisplay, System.FileAttributes FROM SystemIndex WHERE "
		   "SCOPE='file:C:/Users/me' AND System.FileName LIKE '%report%' AND System.FileName LIKE '%.txt'");
	g_free (sql);

	sql = nemo_search_win32_build_sql ("C:\\", FALSE, NULL, FALSE, "hello 'world'");
	check_sql ("one folder deep, content phrase", sql,
		   "SELECT System.ItemPathDisplay, System.FileAttributes FROM SystemIndex WHERE "
		   "DIRECTORY='file:C:/' AND CONTAINS(System.Search.Contents, '\"hello ''world''*\"')");
	g_free (sql);

	sql = nemo_search_win32_build_sql ("D:\\it's", TRUE, "^rep.*t$", TRUE, "\"quoted\"");
	check_sql ("regex name is left to the matcher, quotes dropped from the phrase", sql,
		   "SELECT System.ItemPathDisplay, System.FileAttributes FROM SystemIndex WHERE "
		   "SCOPE='file:D:/it''s' AND CONTAINS(System.Search.Contents, '\"quoted*\"')");
	g_free (sql);

	sql = nemo_search_win32_build_sql ("C:\\x", TRUE, "a_b 100%", FALSE, "");
	check_sql ("words LIKE cannot spell are left out, empty content adds nothing", sql,
		   "SELECT System.ItemPathDisplay, System.FileAttributes FROM SystemIndex WHERE SCOPE='file:C:/x'");
	g_free (sql);
}

static void
test_live (void)
{
	NemoSearchEngine *engine;
	NemoQuery *query;
	/* Documents is read off the profile, not the environment, so pointing HOME
	 * at the scratch dir above does not move it. Indexed on a stock Windows. */
	const char *home = g_get_user_special_dir (G_USER_DIRECTORY_DOCUMENTS);
	char *uri;
	gboolean indexed;
	int spins = 0;

	if (home == NULL) {
		home = g_get_home_dir ();
	}

	check (!nemo_search_win32_folder_is_indexed ("Q:\\no\\such\\folder\\anywhere"),
	       "a folder that does not exist is not indexed");

	indexed = nemo_search_win32_folder_is_indexed (home);
	g_print ("      %s is %s\n", home, indexed ? "indexed" : "not indexed (the walk answers instead)");

	nemo_config_set_boolean (nemo_windows_preferences, NEMO_PREFERENCES_USE_SEARCH_INDEX, TRUE);

	engine = nemo_search_engine_win32_new ();
	g_signal_connect (engine, "hits-added", G_CALLBACK (hits_added_cb), NULL);
	g_signal_connect (engine, "finished", G_CALLBACK (finished_cb), NULL);

	query = nemo_query_new ();
	uri = g_filename_to_uri (home, NULL, NULL);
	nemo_query_set_location (query, uri);
	g_free (uri);
	nemo_query_set_file_pattern (query, "*");
	nemo_query_set_recurse (query, FALSE);
	nemo_search_engine_set_query (engine, query);
	g_object_unref (query);

	nemo_search_engine_start (engine);

	while (!done && spins++ < 3000) {
		g_main_context_iteration (NULL, FALSE);
		g_usleep (10000);
	}

	check (done, "a search of the home folder finishes");
	g_print ("      %d item(s) found\n", n_hits);

	if (indexed) {
		check (n_hits > 0, "the index answers with the home folder's items");
	}

	/* A content search goes through the index's own phrase syntax. What it
	 * finds depends on the machine; that it comes back at all is the check. */
	query = nemo_query_new ();
	uri = g_filename_to_uri (home, NULL, NULL);
	nemo_query_set_location (query, uri);
	g_free (uri);
	nemo_query_set_content_pattern (query, "the");
	nemo_search_engine_set_query (engine, query);
	g_object_unref (query);

	done = FALSE;
	n_hits = 0;
	spins = 0;
	nemo_search_engine_start (engine);

	while (!done && spins++ < 6000) {
		g_main_context_iteration (NULL, FALSE);
		g_usleep (10000);
	}

	check (done, "a content search of the home folder finishes");
	g_print ("      %d item(s) contain the word\n", n_hits);

	g_object_unref (engine);
}

int
main (int argc, char *argv[])
{
	char *scratch = test_scratch_config_home ("nemo-search-win32-home-XXXXXX");

	gtk_init_check (&argc, &argv);
	nemo_global_preferences_init ();

	test_sql ();
	test_live ();

	g_free (scratch);

	return failures == 0 ? 0 : 1;
}
