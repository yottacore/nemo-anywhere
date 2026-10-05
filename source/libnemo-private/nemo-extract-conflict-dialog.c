/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */

/* nemo-extract-conflict-dialog.c - what to do about something an archive would
   overwrite.

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

#include <config.h>
#include "nemo-extract-conflict-dialog.h"

#include <string.h>
#include <glib/gi18n.h>
#include <pango/pango.h>

#include <eel/eel-vfs-extensions.h>

/* The widgets a caller reads back afterwards. Kept on the dialog rather than in
   a subclass: this is one dialog with two accessors, and a GObject type for it
   would be all boilerplate. */
#define KEY_ENTRY	"nemo-extract-conflict-entry"
#define KEY_CHECKBOX	"nemo-extract-conflict-checkbox"
#define KEY_NAME	"nemo-extract-conflict-name"
#define KEY_RENAME	"nemo-extract-conflict-rename-button"
#define KEY_REPLACE	"nemo-extract-conflict-replace-button"

static const char *
conflict_name (GtkWidget *dialog)
{
	return g_object_get_data (G_OBJECT (dialog), KEY_NAME);
}

static void
entry_text_changed_cb (GtkEditable *editable,
		       gpointer     user_data)
{
	GtkWidget *dialog = user_data;
	GtkWidget *rename_button = g_object_get_data (G_OBJECT (dialog), KEY_RENAME);
	GtkWidget *replace_button = g_object_get_data (G_OBJECT (dialog), KEY_REPLACE);
	GtkWidget *checkbox = g_object_get_data (G_OBJECT (dialog), KEY_CHECKBOX);
	const char *text = gtk_entry_get_text (GTK_ENTRY (editable));
	gboolean renaming;

	/* A name of its own is a rename; the original name back is a replace.
	   One of those can be applied to every conflict, the other cannot. */
	renaming = text[0] != '\0' && g_strcmp0 (text, conflict_name (dialog)) != 0;

	gtk_widget_set_visible (rename_button, renaming);
	gtk_widget_set_visible (replace_button, !renaming);
	gtk_widget_set_sensitive (checkbox, !renaming);

	gtk_dialog_set_default_response (GTK_DIALOG (dialog),
					 renaming ? CONFLICT_RESPONSE_RENAME
						  : CONFLICT_RESPONSE_REPLACE);
}

static void
select_name_region (GtkWidget *dialog)
{
	GtkWidget *entry = g_object_get_data (G_OBJECT (dialog), KEY_ENTRY);
	int start_pos, end_pos;

	gtk_widget_grab_focus (entry);
	eel_filename_get_rename_region (conflict_name (dialog), &start_pos, &end_pos);
	gtk_editable_select_region (GTK_EDITABLE (entry), start_pos, end_pos);
}

static void
expander_activated_cb (GtkExpander *expander,
		       gpointer     user_data)
{
	GtkWidget *dialog = user_data;
	GtkWidget *entry = g_object_get_data (G_OBJECT (dialog), KEY_ENTRY);

	/* "activate" runs before the state flips, so this is the opening one. */
	if (!gtk_expander_get_expanded (expander) &&
	    g_strcmp0 (gtk_entry_get_text (GTK_ENTRY (entry)), conflict_name (dialog)) == 0) {
		select_name_region (dialog);
	}
}

static void
reset_button_clicked_cb (G_GNUC_UNUSED GtkButton *button,
			 gpointer   user_data)
{
	GtkWidget *dialog = user_data;
	GtkWidget *entry = g_object_get_data (G_OBJECT (dialog), KEY_ENTRY);

	gtk_entry_set_text (GTK_ENTRY (entry), conflict_name (dialog));
	select_name_region (dialog);
}

static GtkWidget *
icon_image (GIcon *icon)
{
	GtkWidget *image;
	GIcon *owned;

	owned = icon != NULL ? g_object_ref (icon) : g_themed_icon_new ("text-x-generic");
	image = gtk_image_new_from_gicon (owned, GTK_ICON_SIZE_DIALOG);
	g_object_unref (owned);

	return image;
}

/* An archive records seconds, and often none at all - which reads better as a
   dash than as 1970. */
static char *
format_time (gint64 seconds)
{
	GDateTime *when;
	char *text;

	if (seconds <= 0) {
		return g_strdup ("-");
	}

	when = g_date_time_new_from_unix_local (seconds);
	if (when == NULL) {
		return g_strdup ("-");
	}

	text = g_date_time_format (when, "%c");
	g_date_time_unref (when);

	if (text == NULL) {
		text = g_strdup ("-");
	}

	return text;
}

static void
add_side (GtkWidget  *box,
	  GIcon      *icon,
	  const char *heading,
	  const char *size_text,
	  const char *date_text,
	  const char *extra_label,
	  const char *extra_text)
{
	GtkWidget *image;
	GtkWidget *label;
	GString *str;
	char *markup;

	image = icon_image (icon);
	gtk_box_pack_start (GTK_BOX (box), image, FALSE, FALSE, 0);

	str = g_string_new (NULL);
	g_string_append_printf (str, "<b>%s</b>\n", heading);
	g_string_append_printf (str, "<i>%s</i> %s\n", _("Size:"), size_text);

	if (extra_text != NULL) {
		char *escaped = g_markup_escape_text (extra_text, -1);

		g_string_append_printf (str, "<i>%s</i> %s\n", extra_label, escaped);
		g_free (escaped);
	}

	g_string_append_printf (str, "<i>%s</i> %s", _("Last modified:"), date_text);

	markup = g_string_free (str, FALSE);

	label = gtk_label_new (NULL);
	gtk_label_set_markup (GTK_LABEL (label), markup);
	gtk_label_set_xalign (GTK_LABEL (label), 0.0);
	gtk_box_pack_start (GTK_BOX (box), label, FALSE, FALSE, 0);

	g_free (markup);
}

/* Returns: (transfer none): a toplevel; destroy with gtk_widget_destroy */
GtkWidget *
nemo_extract_conflict_dialog_new (GtkWindow  *parent,
				  const char *archive_name,
				  const char *entry_name,
				  gboolean    entry_is_dir,
				  guint64     entry_size,
				  gint64      entry_mtime,
				  GFile      *destination,
				  GFile      *destination_dir)
{
	GtkWidget *dialog;
	GtkWidget *content;
	GtkWidget *hbox, *vbox, *vbox2, *box;
	GtkWidget *widget, *reset_button;
	GtkWidget *entry, *checkbox, *expander;
	GtkWidget *rename_button, *replace_button;
	GtkWidget *label;
	GFileInfo *dest_info;
	PangoAttrList *attr_list;
	gboolean dest_is_dir;
	gint64 dest_mtime;
	char *dest_name, *dest_dir_name;
	char *primary_text, *secondary_text, *message;
	const char *message_extra;
	char *size_text, *date_text;
	GIcon *entry_icon;

	g_return_val_if_fail (entry_name != NULL, NULL);
	g_return_val_if_fail (G_IS_FILE (destination), NULL);

	dest_info = g_file_query_info (destination,
				       G_FILE_ATTRIBUTE_STANDARD_TYPE ","
				       G_FILE_ATTRIBUTE_STANDARD_SIZE ","
				       G_FILE_ATTRIBUTE_STANDARD_ICON ","
				       G_FILE_ATTRIBUTE_STANDARD_DISPLAY_NAME ","
				       G_FILE_ATTRIBUTE_TIME_MODIFIED,
				       G_FILE_QUERY_INFO_NOFOLLOW_SYMLINKS, NULL, NULL);

	dest_is_dir = dest_info != NULL &&
		g_file_info_get_file_type (dest_info) == G_FILE_TYPE_DIRECTORY;
	dest_mtime = dest_info != NULL ?
		(gint64) g_file_info_get_attribute_uint64 (dest_info, G_FILE_ATTRIBUTE_TIME_MODIFIED) : 0;

	dest_name = dest_info != NULL && g_file_info_get_display_name (dest_info) != NULL ?
		g_strdup (g_file_info_get_display_name (dest_info)) : g_file_get_basename (destination);
	dest_dir_name = destination_dir != NULL ? g_file_get_basename (destination_dir) : NULL;
	if (dest_dir_name == NULL) {
		dest_dir_name = g_strdup ("");
	}

	dialog = gtk_dialog_new ();
	gtk_window_set_title (GTK_WINDOW (dialog), _("File conflict"));
	gtk_window_set_transient_for (GTK_WINDOW (dialog), parent);
	gtk_window_set_modal (GTK_WINDOW (dialog), TRUE);

	content = gtk_dialog_get_content_area (GTK_DIALOG (dialog));

	hbox = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 12);
	gtk_container_set_border_width (GTK_CONTAINER (hbox), 6);
	gtk_box_pack_start (GTK_BOX (content), hbox, FALSE, FALSE, 0);

	widget = gtk_image_new_from_icon_name ("dialog-warning-symbolic", GTK_ICON_SIZE_DIALOG);
	gtk_widget_set_valign (widget, GTK_ALIGN_START);
	gtk_box_pack_start (GTK_BOX (hbox), widget, FALSE, FALSE, 0);

	vbox = gtk_box_new (GTK_ORIENTATION_VERTICAL, 12);
	gtk_box_pack_start (GTK_BOX (hbox), vbox, FALSE, FALSE, 0);

	if (dest_is_dir) {
		primary_text = g_strdup_printf (_("Replace folder \"%s\"?"), dest_name);
		message_extra = _("Replacing it will remove all files in the folder.");
		message = g_strdup_printf (_("A folder with the same name already exists in \"%s\"."),
					   dest_dir_name);
	} else {
		primary_text = g_strdup_printf (_("Replace file \"%s\"?"), dest_name);
		message_extra = _("Replacing it will overwrite its content.");

		if (entry_mtime > 0 && dest_mtime > 0 && entry_mtime > dest_mtime) {
			message = g_strdup_printf (_("An older file with the same name already exists in \"%s\"."),
						   dest_dir_name);
		} else if (entry_mtime > 0 && dest_mtime > 0 && entry_mtime < dest_mtime) {
			message = g_strdup_printf (_("A newer file with the same name already exists in \"%s\"."),
						   dest_dir_name);
		} else {
			message = g_strdup_printf (_("Another file with the same name already exists in \"%s\"."),
						   dest_dir_name);
		}
	}

	secondary_text = g_strdup_printf ("%s\n%s", message, message_extra);
	g_free (message);

	label = gtk_label_new (primary_text);
	gtk_label_set_line_wrap (GTK_LABEL (label), TRUE);
	gtk_label_set_line_wrap_mode (GTK_LABEL (label), PANGO_WRAP_WORD_CHAR);
	gtk_label_set_xalign (GTK_LABEL (label), 0.0);
	gtk_box_pack_start (GTK_BOX (vbox), label, FALSE, FALSE, 0);

	attr_list = pango_attr_list_new ();
	pango_attr_list_insert (attr_list, pango_attr_weight_new (PANGO_WEIGHT_BOLD));
	pango_attr_list_insert (attr_list, pango_attr_scale_new (PANGO_SCALE_LARGE));
	g_object_set (label, "attributes", attr_list, NULL);
	pango_attr_list_unref (attr_list);

	label = gtk_label_new (secondary_text);
	gtk_label_set_line_wrap (GTK_LABEL (label), TRUE);
	gtk_label_set_xalign (GTK_LABEL (label), 0.0);
	gtk_box_pack_start (GTK_BOX (vbox), label, FALSE, FALSE, 0);

	g_free (primary_text);
	g_free (secondary_text);

	vbox2 = gtk_box_new (GTK_ORIENTATION_VERTICAL, 12);
	gtk_widget_set_margin_start (vbox2, 12);
	gtk_box_pack_start (GTK_BOX (vbox), vbox2, FALSE, FALSE, 0);

	/* What is there now. */
	box = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 12);
	gtk_box_pack_start (GTK_BOX (vbox2), box, FALSE, FALSE, 0);

	size_text = dest_info != NULL && !dest_is_dir ?
		g_format_size ((guint64) g_file_info_get_size (dest_info)) : g_strdup ("-");
	date_text = format_time (dest_mtime);
	add_side (box, dest_info != NULL ? g_file_info_get_icon (dest_info) : NULL,
		  _("Original file"), size_text, date_text, NULL, NULL);
	g_free (size_text);
	g_free (date_text);

	/* What the archive holds. Which archive it came from matters here in a
	   way it does not when copying, since several can be unpacked at once. */
	box = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 12);
	gtk_box_pack_start (GTK_BOX (vbox2), box, FALSE, FALSE, 0);

	if (entry_is_dir) {
		entry_icon = g_themed_icon_new ("folder");
		size_text = g_strdup ("-");
	} else {
		char *content_type = g_content_type_guess (entry_name, NULL, 0, NULL);

		entry_icon = content_type != NULL ? g_content_type_get_icon (content_type) : NULL;
		g_free (content_type);
		size_text = g_format_size (entry_size);
	}

	date_text = format_time (entry_mtime);
	add_side (box, entry_icon, _("Replace with"), size_text, date_text,
		  _("From:"), archive_name);
	g_clear_object (&entry_icon);
	g_free (size_text);
	g_free (date_text);

	expander = gtk_expander_new_with_mnemonic (_("_Select a new name for the destination"));
	gtk_box_pack_start (GTK_BOX (vbox2), expander, FALSE, FALSE, 0);

	box = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 6);
	gtk_container_add (GTK_CONTAINER (expander), box);

	entry = gtk_entry_new ();
	gtk_box_pack_start (GTK_BOX (box), entry, TRUE, TRUE, 6);

	reset_button = gtk_button_new_with_label (_("Reset"));
	gtk_button_set_image (GTK_BUTTON (reset_button),
			      gtk_image_new_from_icon_name ("edit-undo-symbolic", GTK_ICON_SIZE_MENU));
	gtk_box_pack_start (GTK_BOX (box), reset_button, FALSE, FALSE, 6);

	checkbox = gtk_check_button_new_with_mnemonic (_("Apply this action to all files"));
	gtk_box_pack_start (GTK_BOX (vbox), checkbox, FALSE, FALSE, 0);

	gtk_dialog_add_button (GTK_DIALOG (dialog), _("_Cancel"), GTK_RESPONSE_CANCEL);
	gtk_dialog_add_button (GTK_DIALOG (dialog), _("_Skip"), CONFLICT_RESPONSE_SKIP);
	gtk_dialog_add_button (GTK_DIALOG (dialog), _("D_uplicate"), CONFLICT_RESPONSE_AUTO_RENAME);
	rename_button = gtk_dialog_add_button (GTK_DIALOG (dialog), _("Re_name"), CONFLICT_RESPONSE_RENAME);
	replace_button = gtk_dialog_add_button (GTK_DIALOG (dialog), _("Replace"), CONFLICT_RESPONSE_REPLACE);

	g_object_set_data (G_OBJECT (dialog), KEY_ENTRY, entry);
	g_object_set_data (G_OBJECT (dialog), KEY_CHECKBOX, checkbox);
	g_object_set_data (G_OBJECT (dialog), KEY_RENAME, rename_button);
	g_object_set_data (G_OBJECT (dialog), KEY_REPLACE, replace_button);
	g_object_set_data_full (G_OBJECT (dialog), KEY_NAME, g_strdup (dest_name), g_free);

	g_signal_connect (expander, "activate", G_CALLBACK (expander_activated_cb), dialog);
	g_signal_connect (reset_button, "clicked", G_CALLBACK (reset_button_clicked_cb), dialog);
	g_signal_connect (entry, "changed", G_CALLBACK (entry_text_changed_cb), dialog);

	gtk_entry_set_text (GTK_ENTRY (entry), dest_name);
	gtk_entry_set_activates_default (GTK_ENTRY (entry), TRUE);

	gtk_container_set_border_width (GTK_CONTAINER (dialog), 5);
	gtk_box_set_spacing (GTK_BOX (content), 14);
	gtk_window_set_resizable (GTK_WINDOW (dialog), FALSE);

	gtk_widget_show_all (content);

	/* Rename and Replace share a slot; the entry still holds the original
	   name, so the question being asked is Replace. */
	gtk_widget_set_visible (rename_button, FALSE);
	gtk_widget_grab_focus (replace_button);
	gtk_dialog_set_default_response (GTK_DIALOG (dialog), CONFLICT_RESPONSE_REPLACE);

	g_clear_object (&dest_info);
	g_free (dest_name);
	g_free (dest_dir_name);

	return dialog;
}

/* Returns: (transfer full): free with g_free */
char *
nemo_extract_conflict_dialog_get_new_name (GtkWidget *dialog)
{
	GtkWidget *entry = g_object_get_data (G_OBJECT (dialog), KEY_ENTRY);

	return g_strdup (gtk_entry_get_text (GTK_ENTRY (entry)));
}

gboolean
nemo_extract_conflict_dialog_get_apply_to_all (GtkWidget *dialog)
{
	GtkWidget *checkbox = g_object_get_data (G_OBJECT (dialog), KEY_CHECKBOX);

	return gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (checkbox));
}
