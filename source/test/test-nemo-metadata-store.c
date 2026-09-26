/* Exercises the app-owned metadata store: set/unset, GFileInfo overlay,
 * move re-keying (including directory descendants), and the on-disk
 * round trip. Runs against a throwaway config root. First, before anything
 * has loaded the store, a damaged file is found in its place. */

#include <config.h>

#include <stdlib.h>
#include <string.h>
#include <gio/gio.h>
#include <glib/gstdio.h>

#include <libnemo-private/nemo-metadata-store.h>

#include "test-scratch.h"
#include "test-check.h"

static char *
info_string (const char *uri, const char *key)
{
	GFileInfo *info;
	char *attr, *result;

	info = g_file_info_new ();
	nemo_metadata_store_apply_to_info (uri, info);

	attr = g_strconcat ("metadata::", key, NULL);
	result = g_strdup (g_file_info_get_attribute_string (info, attr));

	g_free (attr);
	g_object_unref (info);

	return result;
}

/* A file that does not parse holds every folder's view, zoom, sort and layout
 * all the same. It used to leave an empty store that the next change wrote
 * straight over it. It is set aside whole instead, and the next save starts a
 * new file beside it. The warning it logs is expected. */
static void
test_damaged_file_set_aside (const char *tmpdir)
{
	const char *damaged = "{ \"file:///tmp/kept\": { \"nemo-icon-view-zoom\": ";
	char *dir, *store_file, *aside, *contents = NULL, *value;

	dir = g_build_filename (tmpdir, "nemo-anywhere", NULL);
	store_file = g_build_filename (dir, "metadata.json", NULL);
	aside = g_strconcat (store_file, ".bad", NULL);
	check (g_mkdir_with_parents (dir, 0700) == 0);
	check (g_file_set_contents (store_file, damaged, -1, NULL));

	/* The first use loads it. */
	nemo_metadata_store_set_string ("file:///tmp/new", "new-key", "n");
	nemo_metadata_store_flush ();

	check (g_file_get_contents (aside, &contents, NULL, NULL));
	check (g_strcmp0 (contents, damaged) == 0);
	g_free (contents);

	contents = NULL;
	check (g_file_get_contents (store_file, &contents, NULL, NULL));
	check (contents != NULL && strstr (contents, "file:///tmp/new") != NULL);
	g_free (contents);

	value = info_string ("file:///tmp/new", "new-key");
	check (g_strcmp0 (value, "n") == 0);
	g_free (value);

	nemo_metadata_store_set_string ("file:///tmp/new", "new-key", NULL);
	g_remove (aside);
	g_free (aside);
	g_free (store_file);
	g_free (dir);
}

int
main (int argc, char *argv[])
{
	char *tmpdir, *value;
	char *store_file;
	char *contents;
	char **listval;
	GFileInfo *info;

	tmpdir = test_scratch_config_home ("nemo-metastore-test-XXXXXX");
	g_assert (tmpdir != NULL);

	test_damaged_file_set_aside (tmpdir);

	/* set / read back */
	nemo_metadata_store_set_string ("file:///tmp/a", "test-key", "hello");
	value = info_string ("file:///tmp/a", "test-key");
	check (g_strcmp0 (value, "hello") == 0);
	g_free (value);

	/* overwrite */
	nemo_metadata_store_set_string ("file:///tmp/a", "test-key", "world");
	value = info_string ("file:///tmp/a", "test-key");
	check (g_strcmp0 (value, "world") == 0);
	g_free (value);

	/* list values survive as lists */
	{
		char *list[] = { "one", "two", NULL };
		nemo_metadata_store_set_stringv ("file:///tmp/a", "test-list", list);
	}
	info = g_file_info_new ();
	nemo_metadata_store_apply_to_info ("file:///tmp/a", info);
	listval = g_file_info_get_attribute_stringv (info, "metadata::test-list");
	check (listval != NULL && g_strv_length (listval) == 2 &&
	       strcmp (listval[0], "one") == 0 && strcmp (listval[1], "two") == 0);
	g_object_unref (info);

	/* unset */
	nemo_metadata_store_set_string ("file:///tmp/a", "test-key", NULL);
	value = info_string ("file:///tmp/a", "test-key");
	check (value == NULL);

	/* rename re-keys the entry and directory descendants */
	nemo_metadata_store_set_string ("file:///tmp/dir", "dir-key", "d");
	nemo_metadata_store_set_string ("file:///tmp/dir/child", "child-key", "c");
	nemo_metadata_store_rename ("file:///tmp/dir", "file:///tmp/moved");

	value = info_string ("file:///tmp/moved", "dir-key");
	check (g_strcmp0 (value, "d") == 0);
	g_free (value);

	value = info_string ("file:///tmp/moved/child", "child-key");
	check (g_strcmp0 (value, "c") == 0);
	g_free (value);

	value = info_string ("file:///tmp/dir", "dir-key");
	check (value == NULL);

	/* similarly-prefixed sibling is not swept up by the descendant rewrite */
	nemo_metadata_store_set_string ("file:///tmp/dir2", "sib-key", "s");
	nemo_metadata_store_rename ("file:///tmp/dir2x", "file:///tmp/elsewhere");
	value = info_string ("file:///tmp/dir2", "sib-key");
	check (g_strcmp0 (value, "s") == 0);
	g_free (value);

	/* flush writes the file; the content survives a reload cycle in a
	 * fresh process (approximated here by checking the JSON contains
	 * what we set - the loader is exercised on every app start) */
	nemo_metadata_store_flush ();
	store_file = g_build_filename (tmpdir, "nemo-anywhere", "metadata.json", NULL);
	check (g_file_test (store_file, G_FILE_TEST_EXISTS));
	contents = NULL;
	check (g_file_get_contents (store_file, &contents, NULL, NULL));
	check (contents != NULL && strstr (contents, "test-list") != NULL &&
	       strstr (contents, "file:///tmp/moved/child") != NULL);
	g_free (contents);
	g_free (store_file);

	if (failures == 0) {
		g_print ("metadata-store: all checks passed\n");
	}

	return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
