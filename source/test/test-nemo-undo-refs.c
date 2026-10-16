/* Each change made through NemoFile leaves an undo record, and once it's
 * handed over the undo manager has the only ref. Rename, permissions, owner
 * and group each kept a second ref of their own, so every one leaked a record.
 * Owner runs only as root, and group only for a user in a second group. */

#include <config.h>

#include <stdlib.h>
#include <unistd.h>
#include <glib/gstdio.h>
#include <gtk/gtk.h>

#include <libnemo-private/nemo-file.h>
#include <libnemo-private/nemo-file-undo-manager.h>
#include <libnemo-private/nemo-global-preferences.h>

#include "test-scratch.h"
#include "test-check.h"

static gboolean finished;

static void
op_done (G_GNUC_UNUSED NemoFile *file, G_GNUC_UNUSED GFile *result, GError *error,
	 G_GNUC_UNUSED gpointer data)
{
	if (error != NULL) {
		g_printerr ("  op failed: %s\n", error->message);
	}
	finished = TRUE;
}

static void
info_ready (G_GNUC_UNUSED NemoFile *file, G_GNUC_UNUSED gpointer data)
{
	finished = TRUE;
}

static void
spin (void)
{
	for (int spins = 0; !finished && spins < 5000; spins++) {
		g_main_context_iteration (NULL, FALSE);
		g_usleep (2000);
	}
	check (finished);
	/* Let anything queued behind the callback run too. */
	while (g_main_context_iteration (NULL, FALSE)) {
	}
}

static NemoFile *
loaded_file (const char *path)
{
	g_autoptr (GFile) location = g_file_new_for_path (path);
	NemoFile *file = nemo_file_get (location);

	finished = FALSE;
	nemo_file_call_when_ready (file, NEMO_FILE_ATTRIBUTE_INFO, info_ready, NULL);
	spin ();
	return file;
}

/* The manager is emptied before each op, so a record here is the op's own. */
static void
check_record (const char *what)
{
	NemoFileUndoInfo *info = nemo_file_undo_manager_get_action ();

	if (info == NULL) {
		g_printerr ("FAIL %s left no undo record\n", what);
		failures++;
		return;
	}
	if (G_OBJECT (info)->ref_count != 1) {
		g_printerr ("FAIL %s: undo record has %u refs, expected 1\n", what,
			    G_OBJECT (info)->ref_count);
		failures++;
		return;
	}
	g_print ("ok %s\n", what);
}

static gid_t
other_group (const char *path)
{
	gid_t groups[256];
	GStatBuf st;
	gid_t current;
	int n;

	if (g_stat (path, &st) != 0) {
		return (gid_t) -1;
	}
	current = st.st_gid;
	if (geteuid () == 0) {
		return current == 1 ? 2 : 1;
	}
	n = getgroups (G_N_ELEMENTS (groups), groups);

	for (int i = 0; i < n; i++) {
		if (groups[i] != current) {
			return groups[i];
		}
	}
	return (gid_t) -1;
}

int
main (int argc, char **argv)
{
	g_autofree char *home = test_scratch_config_home ("nemo-undo-refs-home-XXXXXX");
	g_autofree char *root = test_scratch_dir ("nemo-undo-refs-XXXXXX", NULL);
	g_autofree char *path = NULL;
	g_autofree char *renamed = NULL;
	char *id;
	NemoFile *file;
	gid_t gid;

	if (home == NULL || root == NULL) {
		g_printerr ("FAIL: could not make a temp dir\n");
		return EXIT_FAILURE;
	}
	gtk_init_check (&argc, &argv);
	nemo_global_preferences_init ();

	path = g_build_filename (root, "before.txt", NULL);
	renamed = g_build_filename (root, "after.txt", NULL);
	check (g_file_set_contents (path, "x", -1, NULL));

	file = loaded_file (path);
	nemo_file_undo_manager_set_action (NULL);
	finished = FALSE;
	nemo_file_rename (file, "after.txt", op_done, NULL);
	spin ();
	check_record ("rename");
	nemo_file_unref (file);

	file = loaded_file (renamed);
	nemo_file_undo_manager_set_action (NULL);
	finished = FALSE;
	nemo_file_set_permissions (file, 0600, op_done, NULL);
	spin ();
	check_record ("permissions");

	if (geteuid () == 0) {
		nemo_file_undo_manager_set_action (NULL);
		finished = FALSE;
		nemo_file_set_owner (file, "1", op_done, NULL);
		spin ();
		check_record ("owner");
	} else {
		g_print ("not root, owner not checked\n");
	}

	gid = other_group (renamed);
	if (gid != (gid_t) -1) {
		nemo_file_unref (file);
		file = loaded_file (renamed);
		id = g_strdup_printf ("%u", (unsigned) gid);
		nemo_file_undo_manager_set_action (NULL);
		finished = FALSE;
		nemo_file_set_group (file, id, op_done, NULL);
		spin ();
		check_record ("group");
		g_free (id);
	} else {
		g_print ("in one group only, group not checked\n");
	}
	nemo_file_unref (file);

	if (failures > 0) {
		g_printerr ("%d failure(s)\n", failures);
		return EXIT_FAILURE;
	}
	g_print ("OK\n");
	return EXIT_SUCCESS;
}
