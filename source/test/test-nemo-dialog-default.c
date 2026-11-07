/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */

/* test-nemo-dialog-default.c - Enter takes the button that goes ahead.

   Copyright © 2026 t00mietum (CryptogID: ปʬϝღถɔ4რఠΔթะ9ƾǝu).

   This program is free software; you can redistribute it and/or
   modify it under the terms of the GNU General Public License as
   published by the Free Software Foundation; version 2 of the
   License.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   General Public License for more details.

   You should have received a copy of the GNU General Public
   License along with this program; if not, write to the
   Free Software Foundation, Inc., 51 Franklin Street, Suite 500,
   Boston, MA 02110-1335, USA.
*/

/* Every dialog opens with the button that goes ahead as the default, so Enter
 * takes it, and with the focus on that button and the focus ring showing, so
 * it can be seen before a key is pressed. Where an entry has to take the
 * typing it keeps the focus, and Enter there still takes the default. The
 * test guard is the one dialog that starts on Cancel.
 *
 * These are the dialogs that can be made without the app. The rest are held
 * to a default by cicd/utility/test-dialog-defaults.bash. Needs a display and
 * skips without one. */

#include <config.h>

#include <stdlib.h>
#include <string.h>
#include <glib/gstdio.h>
#include <gtk/gtk.h>

#include <eel/eel-stock-dialogs.h>
#include <libnemo-private/nemo-delete-testguard.h>
#include <libnemo-private/nemo-extract-conflict-dialog.h>
#include <libnemo-private/nemo-file-conflict-dialog.h>
#include <libnemo-private/nemo-global-preferences.h>
#include <libnemo-private/nemo-link-copy.h>
#include <libnemo-private/nemo-link-edit.h>

#include "test-scratch.h"
#include "test-check.h"

static const char *
widget_name (GtkWidget *widget)
{
	if (widget == NULL) {
		return "nothing";
	}
	if (GTK_IS_BUTTON (widget) && gtk_button_get_label (GTK_BUTTON (widget)) != NULL) {
		return gtk_button_get_label (GTK_BUTTON (widget));
	}
	return G_OBJECT_TYPE_NAME (widget);
}

static void
check_starts_on (GtkWidget *dialog, int response, const char *what)
{
	GtkWidget *want, *focus, *start;

	if (!gtk_widget_get_mapped (dialog)) {
		gtk_widget_show (dialog);
	}

	want = gtk_dialog_get_widget_for_response (GTK_DIALOG (dialog), response);
	start = gtk_window_get_default_widget (GTK_WINDOW (dialog));
	focus = gtk_window_get_focus (GTK_WINDOW (dialog));

	if (want == NULL || start != want) {
		g_printerr ("FAIL %s: Enter takes %s, not %s\n", what,
			    widget_name (start), widget_name (want));
		failures++;
		return;
	}

	if (GTK_IS_ENTRY (focus)) {
		if (!gtk_entry_get_activates_default (GTK_ENTRY (focus))) {
			g_printerr ("FAIL %s: Enter in the focused entry doesn't take %s\n",
				    what, widget_name (want));
			failures++;
		}
		return;
	}

	if (focus != want) {
		g_printerr ("FAIL %s: the focus starts on %s, not %s\n", what,
			    widget_name (focus), widget_name (want));
		failures++;
	} else if (!gtk_window_get_focus_visible (GTK_WINDOW (dialog))) {
		g_printerr ("FAIL %s: %s has the focus with no focus ring\n", what,
			    widget_name (want));
		failures++;
	}
}

/* For a dialog that runs inside the call that makes it: checked once it is up,
   then answered Cancel. */
typedef struct {
	const char *what;
	const char *title;  /* NULL for any dialog */
	int         response;
	int         seen;
} Probe;

static gboolean
probe_tick (gpointer data)
{
	Probe *probe = data;
	GList *windows, *l;

	windows = gtk_window_list_toplevels ();
	for (l = windows; l != NULL; l = l->next) {
		if (!GTK_IS_DIALOG (l->data) || !gtk_widget_get_mapped (l->data)) {
			continue;
		}
		if (probe->title != NULL &&
		    g_strcmp0 (gtk_window_get_title (l->data), probe->title) != 0) {
			continue;
		}
		check_starts_on (l->data, probe->response, probe->what);
		probe->seen++;
		gtk_dialog_response (l->data, GTK_RESPONSE_CANCEL);
		break;
	}
	g_list_free (windows);

	return G_SOURCE_CONTINUE;
}

static guint
probe_start (Probe *probe)
{
	return g_timeout_add (50, probe_tick, probe);
}

static void
probe_end (Probe *probe, guint id)
{
	g_source_remove (id);
	if (probe->seen == 0) {
		g_printerr ("FAIL %s: never came up\n", probe->what);
		failures++;
	}
}

static void
check_eel (void)
{
	GtkDialog *dialog;
	Probe probe = { "simple dialog", NULL, 1, 0 };
	guint id;

	dialog = eel_show_error_dialog ("Could not copy", "The disk is full.", NULL);
	check_starts_on (GTK_WIDGET (dialog), GTK_RESPONSE_OK, "error");
	gtk_widget_destroy (GTK_WIDGET (dialog));

	dialog = eel_show_error_dialog_with_details ("Could not copy", "The disk is full.",
						     "No space left on device", NULL);
	check_starts_on (GTK_WIDGET (dialog), GTK_RESPONSE_OK, "error with details");
	gtk_widget_destroy (GTK_WIDGET (dialog));

	dialog = eel_show_info_dialog_with_details ("Copied", "All of it.", "12 files", NULL);
	check_starts_on (GTK_WIDGET (dialog), GTK_RESPONSE_OK, "info with details");
	gtk_widget_destroy (GTK_WIDGET (dialog));

	dialog = eel_show_warning_dialog ("Drag and drop isn't supported.", "Not here.", NULL);
	check_starts_on (GTK_WIDGET (dialog), GTK_RESPONSE_OK, "warning");
	gtk_widget_destroy (GTK_WIDGET (dialog));

	dialog = eel_create_info_dialog ("Done", "All of it.", NULL);
	check_starts_on (GTK_WIDGET (dialog), GTK_RESPONSE_OK, "info");
	gtk_widget_destroy (GTK_WIDGET (dialog));

	dialog = eel_show_yes_no_dialog ("Move it to the trash?", "It has no target.",
					 "Mo_ve to Trash", "_Cancel", NULL);
	check_starts_on (GTK_WIDGET (dialog), GTK_RESPONSE_YES, "yes or no");
	gtk_widget_destroy (GTK_WIDGET (dialog));

	dialog = eel_create_question_dialog ("Make a hardlink anyway?", "It isn't a copy.",
					     "_Cancel", GTK_RESPONSE_CANCEL,
					     "Make _hardlink", GTK_RESPONSE_OK, NULL);
	check_starts_on (GTK_WIDGET (dialog), GTK_RESPONSE_OK, "question");
	gtk_widget_destroy (GTK_WIDGET (dialog));

	id = probe_start (&probe);
	eel_run_simple_dialog (NULL, FALSE, GTK_MESSAGE_QUESTION, "Replace it?",
			       "Replacing it overwrites its contents.",
			       "_Cancel", "_Replace", NULL);
	probe_end (&probe, id);
}

/* The labels are filled in once the files are ready; the entry is last. */
static void
wait_for_conflict (GtkWidget *dialog)
{
	int spins;

	for (spins = 0; spins < 5000; spins++) {
		char *name = nemo_file_conflict_dialog_get_new_name (NEMO_FILE_CONFLICT_DIALOG (dialog));
		gboolean ready = name != NULL && *name != '\0';

		g_free (name);
		if (ready) {
			return;
		}
		g_main_context_iteration (NULL, FALSE);
		g_usleep (2000);
	}
}

static void
check_conflicts (const char *root)
{
	g_autofree char *from = g_build_filename (root, "from", NULL);
	g_autofree char *to = g_build_filename (root, "to", NULL);
	g_autofree char *src_path = g_build_filename (from, "a.txt", NULL);
	g_autofree char *dest_path = g_build_filename (to, "a.txt", NULL);
	g_autoptr (GFile) src = g_file_new_for_path (src_path);
	g_autoptr (GFile) dest = g_file_new_for_path (dest_path);
	g_autoptr (GFile) dest_dir = g_file_new_for_path (to);
	GtkWidget *dialog;

	check (g_mkdir_with_parents (from, 0700) == 0);
	check (g_mkdir_with_parents (to, 0700) == 0);
	check (g_file_set_contents (src_path, "new", -1, NULL));
	check (g_file_set_contents (dest_path, "old one", -1, NULL));

	dialog = nemo_file_conflict_dialog_new (NULL, src, dest, dest_dir);
	wait_for_conflict (dialog);
	check_starts_on (dialog, CONFLICT_RESPONSE_REPLACE, "file conflict");
	gtk_widget_destroy (dialog);

	dialog = nemo_extract_conflict_dialog_new (NULL, "box.zip", "a.txt", FALSE, 3, 0,
						   dest, dest_dir);
	check_starts_on (dialog, CONFLICT_RESPONSE_REPLACE, "extract conflict");
	gtk_widget_destroy (dialog);
}

static void
check_links (const char *root)
{
	g_autofree char *target = g_build_filename (root, "target.txt", NULL);
	g_autofree char *link_path = g_build_filename (root, "link.txt", NULL);
	g_autoptr (GFile) folder = g_file_new_for_path (root);
	NemoLinkOptions options;
	NemoLinkCounts counts = { 1, 0, 0 };
	NemoLinkChoice choice;
	Probe make = { "make link", NULL, GTK_RESPONSE_OK, 0 };
	Probe links = { "copy links", NULL, GTK_RESPONSE_OK, 0 };
	guint id;

	check (g_file_set_contents (target, "here", -1, NULL));

	id = probe_start (&make);
	nemo_link_options_ask (NULL, folder, 0, 1, FALSE, &options);
	probe_end (&make, id);

	id = probe_start (&links);
	nemo_link_choice_ask (NULL, folder, &counts, nemo_link_kinds_supported (root), FALSE, &choice);
	probe_end (&links, id);

	/* An entry takes the typing here. */
	if (nemo_link_create (target, link_path, NULL, NEMO_LINK_FILE_SYMLINK, NULL)) {
		g_autoptr (GFile) edited = g_file_new_for_path (link_path);
		Probe edit = { "edit link", NULL, GTK_RESPONSE_OK, 0 };

		id = probe_start (&edit);
		nemo_link_edit_ask (NULL, edited);
		probe_end (&edit, id);
	} else {
		g_print ("no symlink here, edit link not checked\n");
	}
}

/* The paranoid mode dialog: a stray Enter there means Cancel. */
static void
check_test_guard (const char *root)
{
	g_autofree char *path = g_build_filename (root, "precious.txt", NULL);
	g_autoptr (GFile) file = g_file_new_for_path (path);
	Probe guard = { "test guard", "Delete/overwrite test guard", GTK_RESPONSE_CANCEL, 0 };
	guint id;

	check (g_file_set_contents (path, "keep me", -1, NULL));
	if (!nemo_delete_testguard_armed ()) {
		g_printerr ("FAIL the test guard isn't armed\n");
		failures++;
		return;
	}

	id = probe_start (&guard);
	check (!nemo_delete_testguard_ask_one ("Delete", file));
	probe_end (&guard, id);
}

int
main (int argc, char *argv[])
{
	char *config, *root;

	/* Read once, so before anything asks. */
	g_setenv (NEMO_TESTGUARD_ENV_VAR, "1", TRUE);

	if (!gtk_init_check (&argc, &argv)) {
		g_print ("SKIP: no display\n");
		return 77;
	}

	config = test_scratch_config_home ("nemo-dialog-default-home-XXXXXX");
	root = test_scratch_dir ("nemo-dialog-default-XXXXXX", NULL);
	if (config == NULL || root == NULL) {
		g_printerr ("FAIL could not make a temp dir\n");
		return EXIT_FAILURE;
	}
	nemo_global_preferences_init ();

	check_eel ();
	check_conflicts (root);
	check_links (root);
	check_test_guard (root);

	g_free (root);
	g_free (config);

	if (failures == 0) {
		g_print ("dialog defaults: all checks passed\n");
	}
	return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
