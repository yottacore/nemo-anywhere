/* Builds the conflict dialog for each kind of conflict it words differently:
 * file over file, folder over folder, and file over folder. The labels are
 * filled in once the three files are ready, and the names they use are freed
 * right after. */

#include <config.h>

#include <stdlib.h>
#include <glib/gstdio.h>
#include <gtk/gtk.h>

#include <libnemo-private/nemo-file-conflict-dialog.h>
#include <libnemo-private/nemo-global-preferences.h>

#include "test-scratch.h"
#include "test-check.h"

/* The entry gets the name last, so a filled entry means the labels are done. */
static char *
wait_for_name (NemoFileConflictDialog *dialog)
{
	int spins;

	for (spins = 0; spins < 5000; spins++) {
		char *name = nemo_file_conflict_dialog_get_new_name (dialog);

		if (name != NULL && *name != '\0') {
			return name;
		}
		g_free (name);
		g_main_context_iteration (NULL, FALSE);
		g_usleep (2000);
	}

	return NULL;
}

static void
check_dialog (const char *src_path, const char *dest_path, const char *dest_dir_path)
{
	g_autoptr (GFile) src = g_file_new_for_path (src_path);
	g_autoptr (GFile) dest = g_file_new_for_path (dest_path);
	g_autoptr (GFile) dest_dir = g_file_new_for_path (dest_dir_path);
	g_autofree char *want = g_path_get_basename (dest_path);
	GtkWidget *dialog = nemo_file_conflict_dialog_new (NULL, src, dest, dest_dir);
	g_autofree char *name = wait_for_name (NEMO_FILE_CONFLICT_DIALOG (dialog));

	check (g_strcmp0 (name, want) == 0);
	gtk_widget_destroy (dialog);
}

int
main (int argc, char **argv)
{
	char *tmp, *root;

	if (!gtk_init_check (&argc, &argv)) {
		g_print ("SKIP: no display\n");
		return 77;
	}

	tmp = test_scratch_config_home ("nemo-conflict-dialog-XXXXXX");
	root = test_scratch_dir ("nemo-conflict-files-XXXXXX", NULL);
	if (tmp == NULL || root == NULL) {
		g_printerr ("FAIL could not make a temp dir\n");
		return EXIT_FAILURE;
	}

	nemo_global_preferences_init ();

	g_autofree char *from = g_build_filename (root, "from", NULL);
	g_autofree char *to = g_build_filename (root, "to", NULL);
	g_autofree char *src_file = g_build_filename (from, "a.txt", NULL);
	g_autofree char *dest_file = g_build_filename (to, "a.txt", NULL);
	g_autofree char *src_dir = g_build_filename (from, "sub", NULL);
	g_autofree char *dest_dir = g_build_filename (to, "sub", NULL);
	g_autofree char *src_over_dir = g_build_filename (from, "b", NULL);
	g_autofree char *dest_dir_b = g_build_filename (to, "b", NULL);

	check (g_mkdir_with_parents (src_dir, 0700) == 0);
	check (g_mkdir_with_parents (dest_dir, 0700) == 0);
	check (g_mkdir_with_parents (dest_dir_b, 0700) == 0);
	check (g_file_set_contents (src_file, "new", -1, NULL));
	check (g_file_set_contents (dest_file, "old one", -1, NULL));
	check (g_file_set_contents (src_over_dir, "file", -1, NULL));

	check_dialog (src_file, dest_file, to);
	check_dialog (src_dir, dest_dir, to);
	check_dialog (src_over_dir, dest_dir_b, to);

	g_free (root);
	g_free (tmp);
	return failures == 0 ? 0 : 1;
}
