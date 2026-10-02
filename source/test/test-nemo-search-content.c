/* Content search over a temp dir. On win32 GIO calls the extension the content
 * type (".txt"), so the old is_a("text/plain") test answered no for every file
 * and "Containing:" never found anything at all. Text with an extension GIO does
 * not know, and text with no extension, have to be found by their bytes.
 *
 * Off Windows, two binary types also get a pair of search helpers each. The
 * helper with the higher Priority runs first, and the next is only tried when
 * that one cannot run at all: a helper that runs and finds nothing is the
 * answer. */

#include <config.h>

#include <string.h>
#include <gtk/gtk.h>
#include <libnemo-private/nemo-global-preferences.h>
#include <libnemo-private/nemo-search-engine-advanced.h>
#include <libnemo-private/nemo-query.h>

#include "test-scratch.h"

static gboolean done;
static GList *found;

static void
hits_added_cb (NemoSearchEngine *engine, GList *hits, gpointer data)
{
	for (GList *l = hits; l != NULL; l = l->next) {
		FileSearchResult *result = l->data;
		found = g_list_prepend (found, g_path_get_basename (result->uri));
		file_search_result_free (result);
	}
}

static void
finished_cb (NemoSearchEngine *engine, gpointer data)
{
	done = TRUE;
}

static void
write_file (const char *dir, const char *name, const char *contents, gssize len)
{
	char *path = g_build_filename (dir, name, NULL);

	if (!g_file_set_contents (path, contents, len, NULL)) {
		g_error ("could not write %s", path);
	}

	g_free (path);
}

/* Binary literals carry NULs, so the length comes from the literal itself. */
#define WRITE_LITERAL(dir, name, lit) write_file ((dir), (name), (lit), sizeof (lit) - 1)

#ifndef G_OS_WIN32
static void
write_helper (const char *dir, const char *name, const char *mime,
	      int priority, const char *exec)
{
	char *text = g_strdup_printf ("[Nemo Search Helper]\n"
				      "TryExec=sh;\n"
				      "Exec=%s\n"
				      "MimeType=%s;\n"
				      "Priority=%d\n", exec, mime, priority);

	write_file (dir, name, text, -1);
	g_free (text);
}

/* The GIF's helper that ranks first names a program that is not there; the
   BMP's runs, and finds nothing. */
static void
write_helpers (const char *data_home)
{
	char *dir = g_build_filename (data_home, NEMO_APP_SLUG, "search-helpers", NULL);

	g_mkdir_with_parents (dir, 0700);
	write_helper (dir, "gif-first.nemo_search_helper", "image/gif", 200,
		      "nemo-test-no-such-helper %s");
	write_helper (dir, "gif-second.nemo_search_helper", "image/gif", 100,
		      "sh -c 'echo the needle' %s");
	write_helper (dir, "bmp-first.nemo_search_helper", "image/bmp", 200,
		      "sh -c 'echo nothing here' %s");
	write_helper (dir, "bmp-second.nemo_search_helper", "image/bmp", 100,
		      "sh -c 'echo the needle' %s");
	g_free (dir);
}
#endif

static gboolean
was_found (const char *name)
{
	for (GList *l = found; l != NULL; l = l->next) {
		if (g_strcmp0 (l->data, name) == 0) {
			return TRUE;
		}
	}

	return FALSE;
}

int
main (int argc, char *argv[])
{
	NemoSearchEngine *engine;
	NemoQuery        *query;
	char             *dir, *uri;
	int               spins = 0;
	int               failures = 0;

	char *scratch = test_scratch_config_home ("nemo-search-content-home-XXXXXX");
#ifndef G_OS_WIN32
	char *data_home = g_build_filename (scratch, "data", NULL);

	/* Read before anything asks GLib for it, which caches the answer. */
	g_setenv ("XDG_DATA_HOME", data_home, TRUE);
	write_helpers (data_home);
	g_free (data_home);
#endif

	gtk_init_check (&argc, &argv);
	nemo_global_preferences_init ();

	dir = test_scratch_dir ("nemo-search-content-XXXXXX", NULL);

	write_file (dir, "plain.txt", "the needle is here\n", -1);
	write_file (dir, "notes.md", "# heading\n\nthe needle is here too\n", -1);
	write_file (dir, "readme", "no extension, needle all the same\n", -1);
	write_file (dir, "other.txt", "nothing of interest\n", -1);
	/* A PNG header, then the word - binary, so it must not be read as text. */
	WRITE_LITERAL (dir, "image.png", "\x89PNG\r\n\x1a\n\x00\x00\x00\x0dneedle");
#ifndef G_OS_WIN32
	/* Neither holds the word; only a helper's output can. */
	WRITE_LITERAL (dir, "picture.gif", "GIF89a\x01\x00\x01\x00\x00\x00\x00;");
	WRITE_LITERAL (dir, "picture.bmp", "BM\x3a\x00\x00\x00\x00\x00\x00\x00\x36\x00\x00\x00");
#endif

	engine = nemo_search_engine_advanced_new ();
	g_signal_connect (engine, "hits-added", G_CALLBACK (hits_added_cb), NULL);
	g_signal_connect (engine, "finished", G_CALLBACK (finished_cb), NULL);

	query = nemo_query_new ();
	uri = g_filename_to_uri (dir, NULL, NULL);
	nemo_query_set_location (query, uri);
	g_free (uri);
	nemo_query_set_content_pattern (query, "needle");
	nemo_search_engine_set_query (engine, query);
	g_object_unref (query);

	nemo_search_engine_start (engine);

	while (!done && spins++ < 500) {
		g_main_context_iteration (NULL, FALSE);
		g_usleep (10000);
	}

	if (!done) {
		g_printerr ("FAIL: search never finished\n");
		return 1;
	}

	const char *expected[] = { "plain.txt", "notes.md", "readme" };

	for (gsize i = 0; i < G_N_ELEMENTS (expected); i++) {
		if (!was_found (expected[i])) {
			g_printerr ("FAIL: '%s' contains the pattern and was not found\n", expected[i]);
			failures++;
		}
	}

	if (was_found ("other.txt")) {
		g_printerr ("FAIL: 'other.txt' does not contain the pattern and was found\n");
		failures++;
	}

	if (was_found ("image.png")) {
		g_printerr ("FAIL: 'image.png' is binary and was searched anyway\n");
		failures++;
	}

#ifndef G_OS_WIN32
	if (!was_found ("picture.gif")) {
		g_printerr ("FAIL: 'picture.gif' was not found through its second helper\n");
		failures++;
	}

	if (was_found ("picture.bmp")) {
		g_printerr ("FAIL: 'picture.bmp' was found by a helper ranked below one that ran\n");
		failures++;
	}
#endif

	g_object_unref (engine);
	g_list_free_full (found, g_free);
	g_free (dir);
	g_free (scratch);

	if (failures > 0) {
		return 1;
	}

	g_print ("nemo-search-content: all checks passed\n");
	return 0;
}
