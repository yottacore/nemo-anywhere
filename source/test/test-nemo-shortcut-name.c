/* How a shortcut is named on screen, and what a rename does with the extension
 * that is not on screen. The second half is the one that matters: a rename can
 * arrive without the extension, and without putting it back the shortcut turns
 * into an ordinary file and the target is gone.
 *
 * .desktop launchers run everywhere; the .lnk half is Windows-only. */

#include <config.h>

#include <stdlib.h>
#include <string.h>
#include <glib/gstdio.h>
#include <gtk/gtk.h>

#include <libnemo-private/nemo-file.h>
#include <libnemo-private/nemo-global-preferences.h>

#include "test-scratch.h"
#include "test-check.h"

typedef struct {
	GMainLoop *loop;
	GError    *error;
} RenameWait;

static void
rename_done (G_GNUC_UNUSED NemoFile *file, G_GNUC_UNUSED GFile *result, GError *error, gpointer data)
{
	RenameWait *wait = data;

	if (error != NULL) {
		wait->error = g_error_copy (error);
	}
	g_main_loop_quit (wait->loop);
}

/* Rename @file to @new_name and wait for the operation to finish. */
static gboolean
rename_and_wait (NemoFile *file, const char *new_name)
{
	RenameWait wait = { g_main_loop_new (NULL, FALSE), NULL };
	gboolean ok;

	nemo_file_rename (file, new_name, rename_done, &wait);
	g_main_loop_run (wait.loop);

	ok = wait.error == NULL;
	if (!ok) {
		g_printerr ("  rename to %s failed: %s\n", new_name, wait.error->message);
		g_error_free (wait.error);
	}
	g_main_loop_unref (wait.loop);

	return ok;
}

static NemoFile *
file_at (const char *path)
{
	GFile *location = g_file_new_for_path (path);
	NemoFile *file = nemo_file_get (location);

	g_object_unref (location);
	return file;
}

/* The preference is cached behind a changed:: handler, so let it be delivered. */
static void
set_show_extension (gboolean show)
{
	nemo_config_set_boolean (nemo_preferences,
				 NEMO_PREFERENCES_SHOW_SHORTCUT_EXTENSION, show);

	while (g_main_context_iteration (NULL, FALSE)) {
		/* drain */
	}
}

/* The name on screen, with the extension hidden and then shown. */
static void
check_shown_name (const char *path, const char *hidden, const char *shown)
{
	NemoFile *file = file_at (path);
	char *name;

	g_assert (file != NULL);

	set_show_extension (FALSE);
	name = nemo_file_get_display_name (file);
	check (g_strcmp0 (name, hidden) == 0);
	g_free (name);

	set_show_extension (TRUE);
	name = nemo_file_get_display_name (file);
	check (g_strcmp0 (name, shown) == 0);
	g_free (name);

	nemo_file_unref (file);
}

/* A rename typed as the name on screen keeps the extension. */
static void
check_rename_keeps_extension (const char *dir, const char *path, const char *expected)
{
	NemoFile *file = file_at (path);
	char *renamed;

	g_assert (file != NULL);

	set_show_extension (FALSE);

	if (!rename_and_wait (file, "renamed")) {
		failures++;
		nemo_file_unref (file);
		return;
	}

	renamed = g_build_filename (dir, expected, NULL);
	check (g_file_test (renamed, G_FILE_TEST_EXISTS));
	g_free (renamed);

	renamed = g_build_filename (dir, "renamed", NULL);
	check (!g_file_test (renamed, G_FILE_TEST_EXISTS));
	g_free (renamed);

	nemo_file_unref (file);
}

int
main (int argc, char *argv[])
{
	char *dir;
	char *scratch_config;
	char *path;

	dir = test_scratch_dir ("nemo-linkname-XXXXXX", NULL);
	g_assert (dir != NULL);

	/* A test that reads the real config fails on somebody else's machine. */
	scratch_config = g_build_filename (dir, "config", NULL);
	g_mkdir_with_parents (scratch_config, 0700);
	test_scratch_point_config_at (scratch_config);

	/* Only so the icon theme nemo-file hooks on startup has a screen to hang
	   off; nothing here needs a window. */
	gtk_init_check (&argc, &argv);
	nemo_global_preferences_init ();

	path = g_build_filename (dir, "sample.desktop", NULL);
	if (!g_file_set_contents (path,
				  "[Desktop Entry]\nType=Application\nName=Sample\nExec=true\n",
				  -1, NULL)) {
		g_printerr ("FAIL cannot write %s\n", path);
		failures++;
	}

	check_shown_name (path, "sample", "sample.desktop");
	check_rename_keeps_extension (dir, path, "renamed.desktop");
	g_free (path);

#ifdef G_OS_WIN32
	path = g_build_filename (dir, "target.lnk", NULL);
	if (!g_file_set_contents (path, "not a real shortcut, only a name", -1, NULL)) {
		g_printerr ("FAIL cannot write %s\n", path);
		failures++;
	}

	check_shown_name (path, "target", "target.lnk");

	/* The rename box starts from the whole name, extension included, so the
	   only thing missing from it is the part being changed. */
	{
		NemoFile *file = file_at (path);
		char *name;

		set_show_extension (FALSE);
		name = nemo_file_get_rename_name (file);
		check (g_strcmp0 (name, "target.lnk") == 0);
		g_free (name);
		nemo_file_unref (file);
	}

	check_rename_keeps_extension (dir, path, "renamed.lnk");
	g_free (path);
#endif

	g_free (scratch_config);
	g_free (dir);

	if (failures > 0) {
		g_printerr ("%d failure(s)\n", failures);
		return EXIT_FAILURE;
	}

	g_print ("shortcut-name: all checks passed\n");
	return EXIT_SUCCESS;
}
