/* GLib's directory walk loses its way past MAX_PATH on Windows: it reports
 * success and then lists the process's working directory instead of the folder
 * asked for. That reads as an empty folder in the window and would send a
 * recursive walk into the wrong tree. These checks build a folder deeper than
 * the limit and hold nemo_enumerate_children to the real contents, sync and
 * async, against the same answers on a short path.
 *
 * The exe links the application manifest, because long-path awareness is a
 * property of the process, not of the call. Without it every check below would
 * fail on the file not existing rather than on the wrong contents. */

#include <config.h>

#include <stdlib.h>
#include <string.h>
#include <gio/gio.h>
#include <glib/gstdio.h>

#include <libnemo-private/nemo-dir-enum.h>

#include "test-scratch.h"
#include "test-check.h"

static gboolean skipped = FALSE;

static const char *entry_names[] = { "alpha.txt", "beta.txt", "gamma.txt", "subdir" };

/* Deepen root by fixed-length segments until it is past MAX_PATH, then fill the
 * bottom folder with the four entries above. NULL if the box cannot make one -
 * long paths are a registry opt-in, so a machine without it is a skip, not a
 * failure. */
static char *
build_deep_folder (const char *root)
{
	char *deep = g_strdup (root);
	const char *segment = "seg-xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx";
	gsize i;

	while (strlen (deep) < 300) {
		char *next = g_build_filename (deep, segment, NULL);

		g_free (deep);
		deep = next;

		if (g_mkdir (deep, 0755) != 0) {
			g_free (deep);
			return NULL;
		}
	}

	for (i = 0; i < G_N_ELEMENTS (entry_names); i++) {
		char *child = g_build_filename (deep, entry_names[i], NULL);
		gboolean ok;

		if (g_str_has_suffix (entry_names[i], ".txt")) {
			ok = g_file_set_contents (child, "hello", -1, NULL);
		} else {
			ok = (g_mkdir (child, 0755) == 0);
		}
		g_free (child);

		if (!ok) {
			g_free (deep);
			return NULL;
		}
	}

	return deep;
}

static int
compare_names (const void *a, const void *b, G_GNUC_UNUSED gpointer user_data)
{
	return g_ascii_strcasecmp (*(const char * const *) a, *(const char * const *) b);
}

/* Sorted names read out of an enumerator, so a comparison does not depend on
 * the order the file system hands them back. */
static char **
drain (GFileEnumerator *enumerator)
{
	GPtrArray *names = g_ptr_array_new ();
	GFileInfo *info;

	while ((info = g_file_enumerator_next_file (enumerator, NULL, NULL)) != NULL) {
		g_ptr_array_add (names, g_strdup (g_file_info_get_name (info)));
		g_object_unref (info);
	}
	g_ptr_array_add (names, NULL);

	{
		char **out = (char **) g_ptr_array_free (names, FALSE);

		g_qsort_with_data (out, g_strv_length (out), sizeof (char *),
				   compare_names, NULL);
		return out;
	}
}

static void
check_contents (char **got, const char *where)
{
	gsize i;

	if (g_strv_length (got) != G_N_ELEMENTS (entry_names)) {
		g_printerr ("FAIL %s: %u entries, wanted %u\n", where,
			    g_strv_length (got), (unsigned) G_N_ELEMENTS (entry_names));
		failures++;
		return;
	}

	for (i = 0; i < G_N_ELEMENTS (entry_names); i++) {
		if (g_strcmp0 (got[i], entry_names[i]) != 0) {
			g_printerr ("FAIL %s: entry %u is \"%s\", wanted \"%s\"\n",
				    where, (unsigned) i, got[i], entry_names[i]);
			failures++;
		}
	}
}

static void
test_long_path_sync (const char *deep)
{
	GFile *dir = g_file_new_for_path (deep);
	GError *error = NULL;
	GFileEnumerator *enumerator;
	char **got;

	check (nemo_dir_enum_path_is_long (dir));

	enumerator = nemo_enumerate_children (dir, "standard::name,standard::type",
					      G_FILE_QUERY_INFO_NOFOLLOW_SYMLINKS,
					      NULL, &error);
	if (enumerator == NULL) {
		g_printerr ("FAIL long path did not open: %s\n", error->message);
		failures++;
		g_clear_error (&error);
		g_object_unref (dir);
		return;
	}

	got = drain (enumerator);
	check_contents (got, "long path, sync");

	g_strfreev (got);
	g_file_enumerator_close (enumerator, NULL, NULL);
	g_object_unref (enumerator);
	g_object_unref (dir);
}

/* The file type has to survive, since it comes from a query on each child
 * rather than from the walk. */
static void
test_long_path_types (const char *deep)
{
	GFile *dir = g_file_new_for_path (deep);
	GFileEnumerator *enumerator;
	GFileInfo *info;
	int dirs = 0, files = 0;

	enumerator = nemo_enumerate_children (dir, "standard::name,standard::type",
					      G_FILE_QUERY_INFO_NOFOLLOW_SYMLINKS,
					      NULL, NULL);
	if (enumerator == NULL) {
		g_object_unref (dir);
		return;
	}

	while ((info = g_file_enumerator_next_file (enumerator, NULL, NULL)) != NULL) {
		if (g_file_info_get_file_type (info) == G_FILE_TYPE_DIRECTORY) {
			dirs++;
		} else {
			files++;
		}
		g_object_unref (info);
	}

	check (dirs == 1);
	check (files == 3);

	g_file_enumerator_close (enumerator, NULL, NULL);
	g_object_unref (enumerator);
	g_object_unref (dir);
}

typedef struct {
	GMainLoop *loop;
	char **names;
} AsyncResult;

static void
async_ready (GObject *source, GAsyncResult *result, gpointer user_data)
{
	AsyncResult *state = user_data;
	GError *error = NULL;
	GFileEnumerator *enumerator;

	enumerator = nemo_enumerate_children_finish (G_FILE (source), result, &error);
	if (enumerator == NULL) {
		g_printerr ("FAIL async open: %s\n", error->message);
		failures++;
		g_clear_error (&error);
	} else {
		state->names = drain (enumerator);
		g_file_enumerator_close (enumerator, NULL, NULL);
		g_object_unref (enumerator);
	}

	g_main_loop_quit (state->loop);
}

/* The window's own listing takes the async route, so it gets its own check. */
static void
test_long_path_async (const char *deep)
{
	GFile *dir = g_file_new_for_path (deep);
	AsyncResult state = { NULL, NULL };

	state.loop = g_main_loop_new (NULL, FALSE);

	nemo_enumerate_children_async (dir, "standard::name,standard::type",
				       G_FILE_QUERY_INFO_NOFOLLOW_SYMLINKS,
				       G_PRIORITY_DEFAULT, NULL, async_ready, &state);
	g_main_loop_run (state.loop);

	if (state.names != NULL) {
		check_contents (state.names, "long path, async");
		g_strfreev (state.names);
	}

	g_main_loop_unref (state.loop);
	g_object_unref (dir);
}

/* An ordinary path still goes to GLib, and has to come back the same either
 * way round. */
static void
test_short_path_matches_glib (const char *root)
{
	GFile *dir = g_file_new_for_path (root);
	GFileEnumerator *ours, *theirs;
	char **from_us, **from_glib;

	check (!nemo_dir_enum_path_is_long (dir));

	ours = nemo_enumerate_children (dir, "standard::name", G_FILE_QUERY_INFO_NONE, NULL, NULL);
	theirs = g_file_enumerate_children (dir, "standard::name", G_FILE_QUERY_INFO_NONE, NULL, NULL);

	if (ours == NULL || theirs == NULL) {
		g_printerr ("FAIL short path did not open\n");
		failures++;
		g_clear_object (&ours);
		g_clear_object (&theirs);
		g_object_unref (dir);
		return;
	}

	from_us = drain (ours);
	from_glib = drain (theirs);

	check (g_strv_length (from_us) == g_strv_length (from_glib));
	check (g_strv_length (from_us) > 0);
	if (g_strv_length (from_us) == g_strv_length (from_glib)) {
		guint i;

		for (i = 0; from_us[i] != NULL; i++) {
			check (g_strcmp0 (from_us[i], from_glib[i]) == 0);
		}
	}

	g_strfreev (from_us);
	g_strfreev (from_glib);
	g_object_unref (ours);
	g_object_unref (theirs);
	g_object_unref (dir);
}

/* A folder that is not there has to report that, not an empty listing. */
static void
test_missing_long_path (const char *deep)
{
	char *missing = g_build_filename (deep, "not-here", "either", NULL);
	GFile *dir = g_file_new_for_path (missing);
	GError *error = NULL;
	GFileEnumerator *enumerator;

	enumerator = nemo_enumerate_children (dir, "standard::name", G_FILE_QUERY_INFO_NONE,
					      NULL, &error);

	check (enumerator == NULL);
	check (error != NULL && error->domain == G_IO_ERROR);

	g_clear_error (&error);
	g_clear_object (&enumerator);
	g_object_unref (dir);
	g_free (missing);
}

int
main (void)
{
	char *root, *deep;

	root = test_scratch_dir ("nemo-dir-enum-test-XXXXXX", NULL);
	if (root == NULL) {
		g_printerr ("could not make a temporary directory\n");
		return EXIT_FAILURE;
	}

	deep = build_deep_folder (root);
	if (deep == NULL) {
		g_print ("SKIP: this machine will not create a path past MAX_PATH "
			 "(long paths are off in the registry)\n");
		skipped = TRUE;
	} else {
		test_long_path_sync (deep);
		test_long_path_types (deep);
		test_long_path_async (deep);
		test_missing_long_path (deep);
	}

	test_short_path_matches_glib (root);

	g_free (deep);
	g_free (root);

	if (failures > 0) {
		g_printerr ("%d failure(s)\n", failures);
		return EXIT_FAILURE;
	}

	/* The long paths are what this test is for, so without them it has not run. */
	if (skipped)
		return 77;

	g_print ("long-path enumeration: all checks passed\n");
	return EXIT_SUCCESS;
}
