/* nemo-shortcut-properties.c - editing a Windows shortcut from Properties
 *
 * Copyright © 2026 t00mietum (CryptogID: ปʬϝღถɔ4რఠΔթะ9ƾǝu)
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published
 * by the Free Software Foundation.
 */

#include <config.h>

#include "nemo-shortcut-properties.h"

#ifdef G_OS_WIN32

#include <string.h>
#include <glib/gi18n.h>
#include <eel/eel-stock-dialogs.h>
#include <libnemo-private/nemo-file.h>
#include <libnemo-private/nemo-shortcut-win32.h>

enum {
	FIELD_TARGET,
	FIELD_ARGUMENTS,
	FIELD_WORKING_DIR,
	FIELD_DESCRIPTION,
	N_FIELDS
};

static const struct {
	const char *label;
	gboolean takes_a_file;
} field_info[N_FIELDS] = {
	{ N_("Target"), TRUE },
	{ N_("Arguments"), FALSE },
	{ N_("Start in"), TRUE },
	{ N_("Comment"), FALSE },
};

typedef struct {
	char *lnk_path;
	NemoShortcutInfo info;
	GtkWidget *entries[N_FIELDS];
} Editor;

enum {
	TARGET_URI_LIST
};

static const GtkTargetEntry target_table[] = {
	{ "text/uri-list", 0, TARGET_URI_LIST }
};

static void
editor_free (Editor *editor)
{
	nemo_shortcut_info_clear (&editor->info);
	g_free (editor->lnk_path);
	g_free (editor);
}

static char **
field_slot (Editor *editor, int field)
{
	switch (field) {
	case FIELD_TARGET:      return &editor->info.target;
	case FIELD_ARGUMENTS:   return &editor->info.arguments;
	case FIELD_WORKING_DIR: return &editor->info.working_dir;
	default:                return &editor->info.description;
	}
}

/* Writes the shortcut when an entry differs from what it was read as. */
static void
save_entry (GtkEntry *entry, Editor *editor)
{
	int field = GPOINTER_TO_INT (g_object_get_data (G_OBJECT (entry), "field"));
	char **slot = field_slot (editor, field);
	const char *value = gtk_entry_get_text (entry);
	char *previous;
	GError *error = NULL;

	if (g_strcmp0 (value, *slot) == 0) {
		return;
	}

	previous = *slot;
	*slot = g_strdup (value);

	if (nemo_shortcut_win32_update (editor->lnk_path, &editor->info, &error)) {
		g_free (previous);
		return;
	}

	/* Put the field back to what the file still says. */
	g_free (*slot);
	*slot = previous;
	gtk_entry_set_text (entry, previous != NULL ? previous : "");

	eel_show_error_dialog (_("The shortcut could not be changed."),
			       error != NULL ? error->message : NULL,
			       GTK_WINDOW (gtk_widget_get_toplevel (GTK_WIDGET (entry))));
	g_clear_error (&error);
}

static void
entry_activate_cb (GtkWidget *entry, Editor *editor)
{
	save_entry (GTK_ENTRY (entry), editor);
}

static gboolean
entry_focus_out_cb (GtkWidget *entry, G_GNUC_UNUSED GdkEventFocus *event, Editor *editor)
{
	save_entry (GTK_ENTRY (entry), editor);
	return FALSE;
}

/* A file dropped on the field becomes its path. */
static void
drag_data_received_cb (GtkWidget        *entry,
		       G_GNUC_UNUSED GdkDragContext   *context,
		       G_GNUC_UNUSED int               x,
		       G_GNUC_UNUSED int               y,
		       GtkSelectionData *selection_data,
		       G_GNUC_UNUSED guint             info,
		       G_GNUC_UNUSED guint             time,
		       G_GNUC_UNUSED gpointer          user_data)
{
	char **uris = g_uri_list_extract_uris ((const char *) gtk_selection_data_get_data (selection_data));
	char *path = uris != NULL && uris[0] != NULL ? g_filename_from_uri (uris[0], NULL, NULL) : NULL;

	if (path != NULL) {
		gtk_entry_set_text (GTK_ENTRY (entry), path);
		gtk_widget_grab_focus (entry);
	}

	g_free (path);
	g_strfreev (uris);
}

gboolean
nemo_shortcut_properties_should_show (GList *files)
{
	NemoFile *file;
	char *name;
	gboolean show;

	if (files == NULL || files->next != NULL) {
		return FALSE;
	}

	file = NEMO_FILE (files->data);
	/* A symlink named .lnk would be saved through, as in Edit link. */
	if (nemo_file_is_directory (file) || !nemo_file_is_local (file) ||
	    nemo_file_is_symbolic_link (file)) {
		return FALSE;
	}

	name = nemo_file_get_name (file);
	show = name != NULL && g_str_has_suffix (name, ".lnk");
	if (!show && name != NULL) {
		char *lower = g_ascii_strdown (name, -1);

		show = g_str_has_suffix (lower, ".lnk");
		g_free (lower);
	}

	g_free (name);
	return show;
}

GtkWidget *
nemo_shortcut_properties_make_box (GtkSizeGroup *label_size_group,
				   GList        *files)
{
	Editor *editor = g_new0 (Editor, 1);
	GtkWidget *box, *grid;
	GFile *location;
	GError *error = NULL;
	int field;

	location = nemo_file_get_location (NEMO_FILE (files->data));
	editor->lnk_path = g_file_get_path (location);
	g_object_unref (location);

	if (!nemo_shortcut_win32_read_info (editor->lnk_path, &editor->info, &error)) {
		g_warning ("%s: %s", editor->lnk_path, error != NULL ? error->message : "could not read the shortcut");
		g_clear_error (&error);
	}

	box = gtk_box_new (GTK_ORIENTATION_VERTICAL, 6);
	grid = gtk_grid_new ();
	gtk_orientable_set_orientation (GTK_ORIENTABLE (grid), GTK_ORIENTATION_VERTICAL);
	gtk_grid_set_row_spacing (GTK_GRID (grid), 6);
	gtk_grid_set_column_spacing (GTK_GRID (grid), 12);

	for (field = 0; field < N_FIELDS; field++) {
		char *text = g_strdup_printf ("%s:", _(field_info[field].label));
		GtkWidget *label = gtk_label_new (text);
		GtkWidget *entry = gtk_entry_new ();
		const char *value = *field_slot (editor, field);

		g_free (text);
		gtk_misc_set_alignment (GTK_MISC (label), 0.0, 0.5);
		gtk_size_group_add_widget (label_size_group, label);

		gtk_widget_set_hexpand (entry, TRUE);
		gtk_entry_set_text (GTK_ENTRY (entry), value != NULL ? value : "");
		g_object_set_data (G_OBJECT (entry), "field", GINT_TO_POINTER (field));

		gtk_container_add (GTK_CONTAINER (grid), label);
		gtk_grid_attach_next_to (GTK_GRID (grid), entry, label, GTK_POS_RIGHT, 1, 1);

		g_signal_connect (entry, "activate", G_CALLBACK (entry_activate_cb), editor);
		g_signal_connect (entry, "focus_out_event", G_CALLBACK (entry_focus_out_cb), editor);

		if (field_info[field].takes_a_file) {
			gtk_drag_dest_set (entry,
					   GTK_DEST_DEFAULT_MOTION | GTK_DEST_DEFAULT_HIGHLIGHT | GTK_DEST_DEFAULT_DROP,
					   target_table, G_N_ELEMENTS (target_table),
					   GDK_ACTION_COPY | GDK_ACTION_MOVE);
			g_signal_connect (entry, "drag_data_received", G_CALLBACK (drag_data_received_cb), NULL);
		}

		editor->entries[field] = entry;
	}

	gtk_box_pack_start (GTK_BOX (box), grid, FALSE, FALSE, 0);
	g_object_set_data_full (G_OBJECT (box), "editor", editor, (GDestroyNotify) editor_free);
	gtk_widget_show_all (box);

	return box;
}

#else

gboolean
nemo_shortcut_properties_should_show (G_GNUC_UNUSED GList *files)
{
	return FALSE;
}

GtkWidget *
nemo_shortcut_properties_make_box (G_GNUC_UNUSED GtkSizeGroup *label_size_group,
				   G_GNUC_UNUSED GList        *files)
{
	return NULL;
}

#endif
