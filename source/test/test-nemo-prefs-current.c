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

/* With per-folder settings off the Current tab still opens, and shows its
 * controls grayed out, so it is plain what is turned off. Its label is grayed
 * too. Turning the setting off while the tab is up leaves it up, grayed.
 * Built from the real dialog resource, with no window to follow. */

#include <config.h>

#include <stdlib.h>
#include <gtk/gtk.h>

#include <libnemo-private/nemo-global-preferences.h>
#include <libnemo-private/nemo-tab-list.h>

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
	GtkStack *views;
	GtkWidget *default_page;
	GError *error = NULL;
	char *tmp;

	tmp = test_scratch_config_home ("nemo-prefs-current-test-XXXXXX");

	if (!gtk_init_check (&argc, &argv)) {
		g_print ("SKIP: no display\n");
		g_free (tmp);
		return 77;
	}

	nemo_global_preferences_init ();

	g_type_ensure (NEMO_TYPE_TAB_LIST);
	builder = gtk_builder_new ();
	if (!gtk_builder_add_from_resource (builder,
					    "/org/nemo/nemo-file-management-properties.glade",
					    &error)) {
		g_printerr ("FAIL cannot load the dialog: %s\n", error->message);
		g_clear_error (&error);
		return EXIT_FAILURE;
	}

	dialog = GTK_WIDGET (gtk_builder_get_object (builder, "file_management_dialog"));
	/* Default and Current were tabs on top until 2026101009150769 made
	   them sub-tabs in the list, so the switches below go through the
	   stack and the tab is the row's label. */
	views = GTK_STACK (gtk_builder_get_object (builder, "views_stack"));
	current_page = GTK_WIDGET (gtk_builder_get_object (builder, "views_current_page"));
	default_page = GTK_WIDGET (gtk_builder_get_object (builder, "views_default_page"));
	current_tab = nemo_tab_list_get_label (NEMO_TAB_LIST (gtk_builder_get_object (builder, "page_sidebar")),
					       current_page);
	check (views != NULL && default_page != NULL && current_page != NULL && current_tab != NULL);
	check (current_tab != NULL && g_strcmp0 (gtk_label_get_text (GTK_LABEL (current_tab)), "Current") == 0);

	set_remembering (FALSE);
	nemo_prefs_current_folder_setup (builder, dialog, NULL);

	/* Off: the tab is grayed, and it opens on grayed controls. It used to
	   refuse the switch and leave Default up, back when it opened on a blank
	   pane; 2026101009150768 went back to opening it.
	check (gtk_notebook_get_current_page (notebook) == 0); */
	check (!gtk_widget_get_sensitive (current_tab));
	gtk_stack_set_visible_child (views, current_page);
	check (gtk_stack_get_visible_child (views) == current_page);
	check (gtk_widget_get_visible (current_page));
	check (!gtk_widget_is_sensitive (current_page));
	/* The switch that turns it back on stays live above it. */
	check (gtk_widget_is_sensitive (GTK_WIDGET (gtk_builder_get_object (builder, "remember_folder_settings_checkbutton"))));
	gtk_stack_set_visible_child (views, default_page);

	/* On: the tab opens. */
	set_remembering (TRUE);
	check (gtk_widget_get_sensitive (current_tab));
	gtk_stack_set_visible_child (views, current_page);
	check (gtk_stack_get_visible_child (views) == current_page);

	/* Turned off while it is up, it stays up and grays out. Default used to
	   come back here, for the same reason as above.
	check (gtk_notebook_get_current_page (notebook) == 0); */
	set_remembering (FALSE);
	check (gtk_stack_get_visible_child (views) == current_page);
	check (!gtk_widget_is_sensitive (current_page));
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
