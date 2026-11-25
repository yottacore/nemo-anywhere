/* Text built around a value that can be NULL. An undo record for a copy whose
 * one file was skipped has no target name, a remote folder has no path, and a
 * file with no extension and no known type has no extension to name, and a file
 * not read yet has no dates. Each went to a "%s", which glibc prints as "(null)"
 * and other C libraries may crash on.
 * Test ID: dfsfq */

#include <config.h>

#include <string.h>
#include <gtk/gtk.h>

#include <libnemo-private/nemo-file.h>
#include <libnemo-private/nemo-file-undo-operations.h>
#include <libnemo-private/nemo-global-preferences.h>
#include <libnemo-private/nemo-mime-application-chooser.h>

#include "test-scratch.h"
#include "test-check.h"

static void
check_text (const char *what, const char *text)
{
	if (text == NULL || strstr (text, "(null)") != NULL) {
		g_printerr ("FAIL %s: \"%s\"\n", what, text != NULL ? text : "NULL");
		failures++;
	}
}

static void
check_strings (const char *what, NemoFileUndoInfo *info, const char *want)
{
	g_autofree char *undo_label = NULL, *undo_desc = NULL;
	g_autofree char *redo_label = NULL, *redo_desc = NULL;

	nemo_file_undo_info_get_strings (info, &undo_label, &undo_desc, &redo_label, &redo_desc);
	check_text (what, undo_label);
	check_text (what, undo_desc);
	check_text (what, redo_label);
	check_text (what, redo_desc);
	if (want != NULL && (undo_desc == NULL || strstr (undo_desc, want) == NULL)) {
		g_printerr ("FAIL %s: \"%s\" has no %s\n", what, undo_desc, want);
		failures++;
	}
	g_object_unref (info);
}

static void
walk_labels (GtkWidget *widget, gpointer what)
{
	if (GTK_IS_LABEL (widget)) {
		check_text (what, gtk_label_get_text (GTK_LABEL (widget)));
	}
	if (GTK_IS_CONTAINER (widget)) {
		gtk_container_forall (GTK_CONTAINER (widget), walk_labels, what);
	}
}

int
main (int argc, char **argv)
{
	static const NemoFileUndoOp ops[] = {
		NEMO_FILE_UNDO_OP_MOVE,
		NEMO_FILE_UNDO_OP_RESTORE_FROM_TRASH,
		NEMO_FILE_UNDO_OP_COPY,
		NEMO_FILE_UNDO_OP_DUPLICATE,
		NEMO_FILE_UNDO_OP_CREATE_LINK,
	};
	g_autoptr (GFile) here = g_file_new_for_path (g_get_tmp_dir ());
	g_autoptr (GFile) remote = g_file_new_for_uri ("nemo-nowhere:///far/away");
	g_autoptr (GFile) origin = g_file_get_child (here, "a.txt");
	g_autoptr (GFile) target = g_file_get_child (here, "b.txt");
	g_autofree char *home = test_scratch_config_home ("nemo-null-labels-home-XXXXXX");
	g_autofree char *tip = NULL;
	gboolean shown = gtk_init_check (&argc, &argv);
	NemoFileUndoInfo *info;
	NemoFile *unread;

	if (home == NULL) {
		g_printerr ("FAIL: could not make a temp dir\n");
		return 1;
	}
	nemo_global_preferences_init ();

	for (guint i = 0; i < G_N_ELEMENTS (ops); i++) {
		g_autofree char *what = g_strdup_printf ("op %d with its one item skipped", ops[i]);
		g_autofree char *far = g_strdup_printf ("op %d between remote folders", ops[i]);

		check_strings (what, nemo_file_undo_info_ext_new (ops[i], 1, here, here), NULL);
		check_strings (far, nemo_file_undo_info_ext_new (ops[i], 2, remote, remote), NULL);
	}

	info = nemo_file_undo_info_ext_new (NEMO_FILE_UNDO_OP_COPY, 1, here, here);
	nemo_file_undo_info_ext_add_origin_target_pair (NEMO_FILE_UNDO_INFO_EXT (info), origin, target);
	check_strings ("copy of one item", info, "'b.txt'");

	info = nemo_file_undo_info_trash_new (1);
	nemo_file_undo_info_trash_add_file (NEMO_FILE_UNDO_INFO_TRASH (info), remote);
	check_strings ("trash of a remote item", info, NULL);

	check_strings ("permissions of a remote folder",
		       nemo_file_undo_info_rec_permissions_new (remote, 0644, 0777, 0755, 0777), NULL);

	unread = nemo_file_get_by_uri ("file:///nowhere/never-read.txt");
	tip = nemo_file_construct_tooltip (unread, NEMO_FILE_TOOLTIP_FLAGS_ACCESS_DATE | NEMO_FILE_TOOLTIP_FLAGS_MOD_DATE | NEMO_FILE_TOOLTIP_FLAGS_CREATED_DATE, NULL);
	check_text ("tooltip of a file not read yet", tip);
	nemo_file_unref (unread);

	if (shown) {
		const char *unknown = "application/octet-stream";

		if (g_content_type_is_unknown (unknown)) {
			GtkWidget *ok = g_object_ref_sink (gtk_button_new ());
			GtkWidget *chooser = g_object_ref_sink (nemo_mime_application_chooser_new ("file:///nowhere/README", NULL, unknown, ok));

			walk_labels (chooser, (gpointer) "open with, a file with no extension");
			gtk_widget_destroy (chooser);
			g_object_unref (chooser);
			gtk_widget_destroy (ok);
			g_object_unref (ok);
		} else {
			g_print ("%s isn't the unknown type here, open with not checked\n", unknown);
		}
	} else {
		g_print ("no display, open with not checked\n");
	}

	if (failures > 0) {
		g_printerr ("%d failure(s)\n", failures);
		return 1;
	}
	g_print ("OK\n");
	return 0;
}
