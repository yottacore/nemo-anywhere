/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */

/* test-nemo-prefs-current.c - the Current tab on the Views page.

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

/* With per-folder settings off there is nothing for the Current tab to show,
 * and an insensitive page still switches on a click, so the notebook itself
 * has to refuse it. Turning the setting off while the tab is up goes back to
 * Default. Built from the real dialog resource, with no window to follow. */

#include <config.h>

#include <stdlib.h>
#include <gtk/gtk.h>

#include <libnemo-private/nemo-global-preferences.h>

#include "nemo-prefs-current-folder.h"
#include "nemo-window.h"
#include "nemo-window-slot.h"

#include "test-scratch.h"
#include "test-check.h"

/* Only reached through a window to follow, and the dialog here has none. */
GType
nemo_window_get_type (void)
{
	return GTK_TYPE_APPLICATION_WINDOW;
}

/* Returns: (transfer none) */
NemoWindowSlot *
nemo_window_get_active_slot (G_GNUC_UNUSED NemoWindow *window)
{
	return NULL;
}

/* Returns: (transfer full) */
GFile *
nemo_window_slot_get_location (G_GNUC_UNUSED NemoWindowSlot *slot)
{
	return NULL;
}

/* Returns: (transfer full) */
char *
nemo_window_slot_get_location_uri (G_GNUC_UNUSED NemoWindowSlot *slot)
{
	return NULL;
}

void
nemo_window_slot_force_reload (G_GNUC_UNUSED NemoWindowSlot *slot)
{
}

static void
set_remembering (gboolean on)
{
	nemo_config_set_boolean (nemo_preferences, NEMO_PREFERENCES_REMEMBER_FOLDER_SETTINGS, on);
}

int
main (int argc, char *argv[])
{
	GtkBuilder *builder;
	GtkWidget *dialog, *current_page, *current_tab;
	GtkNotebook *notebook;
	GError *error = NULL;
	int current;
	char *tmp;

	tmp = test_scratch_config_home ("nemo-prefs-current-test-XXXXXX");

	if (!gtk_init_check (&argc, &argv)) {
		g_print ("SKIP: no display\n");
		g_free (tmp);
		return 77;
	}

	nemo_global_preferences_init ();

	builder = gtk_builder_new ();
	if (!gtk_builder_add_from_resource (builder,
					    "/org/nemo/nemo-file-management-properties.glade",
					    &error)) {
		g_printerr ("FAIL cannot load the dialog: %s\n", error->message);
		g_clear_error (&error);
		return EXIT_FAILURE;
	}

	dialog = GTK_WIDGET (gtk_builder_get_object (builder, "file_management_dialog"));
	notebook = GTK_NOTEBOOK (gtk_builder_get_object (builder, "views_notebook"));
	current_page = GTK_WIDGET (gtk_builder_get_object (builder, "views_current_page"));
	current_tab = GTK_WIDGET (gtk_builder_get_object (builder, "views_current_tab"));
	current = gtk_notebook_page_num (notebook, current_page);
	check (current > 0);

	set_remembering (FALSE);
	nemo_prefs_current_folder_setup (builder, dialog, NULL);

	/* Off: the tab is grayed, and asking for it leaves Default up. */
	check (!gtk_widget_get_sensitive (current_tab));
	gtk_notebook_set_current_page (notebook, current);
	check (gtk_notebook_get_current_page (notebook) == 0);

	/* On: the tab opens. */
	set_remembering (TRUE);
	check (gtk_widget_get_sensitive (current_tab));
	gtk_notebook_set_current_page (notebook, current);
	check (gtk_notebook_get_current_page (notebook) == current);

	/* Turned off while it is up, Default comes back. */
	set_remembering (FALSE);
	check (gtk_notebook_get_current_page (notebook) == 0);
	check (!gtk_widget_get_sensitive (current_tab));

	gtk_widget_destroy (dialog);
	g_object_unref (builder);
	g_free (tmp);

	if (failures > 0) {
		g_printerr ("%d check(s) failed\n", failures);
		return EXIT_FAILURE;
	}
	g_print ("PASS\n");
	return EXIT_SUCCESS;
}
