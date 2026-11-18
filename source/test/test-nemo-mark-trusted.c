/* Marking a launcher trusted puts the xdg-open line on top of one that has
 * no shebang. An empty one used to get a byte written past the end of the
 * new buffer, which only the sanitizer build sees.
 */

#include "test.h"

#include <libnemo-private/nemo-global-preferences.h>
#include <libnemo-private/nemo-file-operations.h>

#include <stdlib.h>
#include <string.h>

#include "test-scratch.h"
#include "test-check.h"

#define SHEBANG "#!/usr/bin/env xdg-open\n"

static gboolean timed_out;

static void
marked (G_GNUC_UNUSED gboolean success, G_GNUC_UNUSED gpointer data)
{
	gtk_main_quit ();
}

static gboolean
give_up (G_GNUC_UNUSED gpointer data)
{
	timed_out = TRUE;
	gtk_main_quit ();
	return G_SOURCE_REMOVE;
}

static void
mark_one (const char *dir, const char *name, const char *before, const char *after)
{
	char *path = g_build_filename (dir, name, NULL);
	char *got = NULL;
	GFile *file = g_file_new_for_path (path);
	guint timeout_id;

	check (g_file_set_contents (path, before, -1, NULL));
	timed_out = FALSE;
	timeout_id = g_timeout_add_seconds (20, give_up, NULL);
	nemo_file_mark_desktop_file_trusted (file, NULL, FALSE, marked, NULL);
	gtk_main ();
	if (!timed_out) {
		g_source_remove (timeout_id);
	}
	check (!timed_out);
	check (g_file_get_contents (path, &got, NULL, NULL));
	if (g_strcmp0 (got, after) != 0) {
		g_printerr ("FAIL: %s holds \"%s\"\n", name, got ? got : "(null)");
		failures++;
	}
#ifndef G_OS_WIN32
	check (g_file_test (path, G_FILE_TEST_IS_EXECUTABLE));
#endif
	g_free (got);
	g_object_unref (file);
	g_free (path);
}

int
main (int argc, char *argv[])
{
	char *home = test_scratch_config_home ("nemo-mark-trusted-home-XXXXXX");
	char *dir;

	nemo_global_preferences_init ();
	test_init (&argc, &argv);
	dir = test_scratch_dir ("nemo-mark-trusted-XXXXXX", NULL);

	mark_one (dir, "empty.desktop", "", SHEBANG);
	mark_one (dir, "plain.desktop", "[Desktop Entry]\n", SHEBANG "[Desktop Entry]\n");
	mark_one (dir, "has-one.desktop", "#!/bin/sh\n", "#!/bin/sh\n");

	g_free (dir);
	g_free (home);
	if (failures > 0) {
		return EXIT_FAILURE;
	}
	g_print ("mark trusted: all checks passed\n");
	return EXIT_SUCCESS;
}
