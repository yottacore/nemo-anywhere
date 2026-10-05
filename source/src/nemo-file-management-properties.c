/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */

/* nemo-file-management-properties.c - Functions to create and show the nemo preference dialog.

   Copyright (C) 2002 Jan Arne Petersen

   The Gnome Library is free software; you can redistribute it and/or
   modify it under the terms of the GNU Library General Public License as
   published by the Free Software Foundation; either version 2 of the
   License, or (at your option) any later version.

   The Gnome Library is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   Library General Public License for more details.

   You should have received a copy of the GNU Library General Public
   License along with the Gnome Library; see the file COPYING.LIB.  If not,
   write to the Free Software Foundation, Inc., 51 Franklin Street - Suite 500,
   Boston, MA 02110-1335, USA.

   Authors: Jan Arne Petersen <jpetersen@uni-bonn.de>
*/

#include <config.h>

#include "nemo-file-management-properties.h"

#include <string.h>
#include <time.h>
#include <gtk/gtk.h>
#include <gio/gio.h>

#include <glib/gi18n.h>

#include <eel/eel-glib-extensions.h>

#include <libnemo-private/nemo-appearance.h>
#include <libnemo-private/nemo-column-chooser.h>
#include <libnemo-private/nemo-column-utilities.h>
#include <libnemo-private/nemo-delete-testguard.h>
#include <libnemo-private/nemo-desktop-utils.h>
#include <libnemo-private/nemo-global-preferences.h>
#include <libnemo-private/nemo-module.h>

#include "nemo-plugin-manager.h"
#include "nemo-prefs-current-folder.h"
#include "nemo-prefs-file-cache.h"
#include "nemo-template-config-widget.h"
#include "nemo-actions.h"

/* string enum preferences */
#define NEMO_FILE_MANAGEMENT_PROPERTIES_DEFAULT_VIEW_WIDGET "default_view_combobox"
#define NEMO_FILE_MANAGEMENT_PROPERTIES_ICON_VIEW_SIZE_WIDGET "icon_view_size_spinbutton"
#define NEMO_FILE_MANAGEMENT_PROPERTIES_ICON_VIEW_IMAGE_SIZE_WIDGET "icon_view_image_size_spinbutton"
#define NEMO_FILE_MANAGEMENT_PROPERTIES_IMAGE_FOLDER_SWITCH_WIDGET "image_folder_switch_checkbutton"
#define NEMO_FILE_MANAGEMENT_PROPERTIES_COMPACT_VIEW_SIZE_WIDGET "compact_view_size_spinbutton"
#define NEMO_FILE_MANAGEMENT_PROPERTIES_LIST_VIEW_SIZE_WIDGET "list_view_size_spinbutton"
#define NEMO_FILE_MANAGEMENT_PROPERTIES_SORT_ORDER_WIDGET "sort_order_combobox"
#define NEMO_FILE_MANAGEMENT_PROPERTIES_DATE_FORMAT_WIDGET "date_format_combobox"
#define NEMO_FILE_MANAGEMENT_PROPERTIES_PATH_SEPARATOR_WIDGET "path_separator_combobox"
#define NEMO_FILE_MANAGEMENT_PROPERTIES_ALLOW_SLASH_INPUT_WIDGET "allow_slash_input_checkbutton"
#define NEMO_FILE_MANAGEMENT_PROPERTIES_USE_WINDOWS_SEARCH_WIDGET "use_windows_search_checkbutton"
#define NEMO_FILE_MANAGEMENT_PROPERTIES_SHOW_SHORTCUT_EXTENSION_WIDGET "show_shortcut_extension_checkbutton"
#define NEMO_FILE_MANAGEMENT_PROPERTIES_SHOW_HIDDEN_FILES_WIDGET "show_hidden_files_checkbutton"
#define NEMO_FILE_MANAGEMENT_PROPERTIES_SHOW_DOT_FILES_WIDGET "show_dot_files_checkbutton"

#define NEMO_FILE_MANAGEMENT_PROPERTIES_PREVIEW_IMAGE_WIDGET "preview_image_combobox"
#define NEMO_FILE_MANAGEMENT_PROPERTIES_PREVIEW_FOLDER_WIDGET "preview_folder_combobox"
#define NEMO_FILE_MANAGEMENT_PROPERTIES_SIZE_PREFIXES_WIDGET "size_prefixes_combobox"

/* bool preferences */
#define NEMO_FILE_MANAGEMENT_PROPERTIES_INHERIT_VIEW_WIDGET "inherit_view_checkbox"
#define NEMO_FILE_MANAGEMENT_PROPERTIES_REVERSE_SORT_WIDGET "reverse_sort_checkbox"
#define NEMO_FILE_MANAGEMENT_QUICK_RENAMES_WITH_PAUSE_IN_BETWEEN "quick_renames_with_pause_in_between"
#define NEMO_FILE_MANAGEMENT_PROPERTIES_RENAME_SELECTS_WHOLE_NAME_WIDGET "rename_selects_whole_name_checkbutton"
#define NEMO_FILE_MANAGEMENT_PROPERTIES_FAVORITES_FIRST_WIDGET "sort_favorites_first_checkbutton"
#define NEMO_FILE_MANAGEMENT_PROPERTIES_FOLDERS_FIRST_WIDGET "sort_folders_first_checkbutton"
#define NEMO_FILE_MANAGEMENT_PROPERTIES_COMPACT_LAYOUT_WIDGET "compact_layout_checkbutton"
#define NEMO_FILE_MANAGEMENT_PROPERTIES_LABELS_BESIDE_ICONS_WIDGET "labels_beside_icons_checkbutton"
#define NEMO_FILE_MANAGEMENT_PROPERTIES_ALL_COLUMNS_SAME_WIDTH "all_columns_same_width_checkbutton"
#define NEMO_FILE_MANAGEMENT_PROPERTIES_ALWAYS_USE_BROWSER_WIDGET "always_use_browser_checkbutton"
#define NEMO_FILE_MANAGEMENT_PROPERTIES_DRAG_CONFIRM_MOVE_WIDGET "drag_confirm_move_checkbutton"
#define NEMO_FILE_MANAGEMENT_PROPERTIES_WINDOW_PER_PROCESS_WIDGET "window_per_process_checkbutton"
#define NEMO_FILE_MANAGEMENT_PROPERTIES_DRAG_CONFIRM_COPY_WIDGET "drag_confirm_copy_checkbutton"
#define NEMO_FILE_MANAGEMENT_PROPERTIES_TRASH_CONFIRM_MOVE_WIDGET "trash_confirm_move_checkbutton"
#define NEMO_FILE_MANAGEMENT_PROPERTIES_TRASH_CONFIRM_WIDGET "trash_confirm_checkbutton"
#define NEMO_FILE_MANAGEMENT_PROPERTIES_TRASH_DELETE_WIDGET "trash_delete_checkbutton"
#define NEMO_FILE_MANAGEMENT_PROPERTIES_TESTGUARD_WIDGET "testguard_all_deletes_checkbutton"
#define NEMO_FILE_MANAGEMENT_PROPERTIES_SWAP_TRASH_DELETE "swap_trash_binding_checkbutton"
#define NEMO_FILE_MANAGEMENT_PROPERTIES_OPEN_NEW_WINDOW_WIDGET "new_window_checkbutton"
#define NEMO_FILE_MANAGEMENT_PROPERTIES_SHOW_LIST_VIEW_EXPANDERS_WIDGET "list_view_show_expanders_checkbutton"
#define NEMO_FILE_MANAGEMENT_PROPERTIES_LIST_VIEW_ROW_SHADING_WIDGET "list_view_row_shading_checkbutton"

#define NEMO_FILE_MANAGEMENT_PROPERTIES_SHOW_PREVIOUS_ICON_TOOLBAR_WIDGET "show_previous_icon_toolbar_togglebutton"
#define NEMO_FILE_MANAGEMENT_PROPERTIES_SHOW_NEXT_ICON_TOOLBAR_WIDGET "show_next_icon_toolbar_togglebutton"
#define NEMO_FILE_MANAGEMENT_PROPERTIES_SHOW_UP_ICON_TOOLBAR_WIDGET "show_up_icon_toolbar_togglebutton"
#define NEMO_FILE_MANAGEMENT_PROPERTIES_SHOW_RELOAD_ICON_TOOLBAR_WIDGET "show_reload_icon_toolbar_togglebutton"
#define NEMO_FILE_MANAGEMENT_PROPERTIES_SHOW_EDIT_ICON_TOOLBAR_WIDGET "show_edit_icon_toolbar_togglebutton"
#define NEMO_FILE_MANAGEMENT_PROPERTIES_SHOW_HOME_ICON_TOOLBAR_WIDGET "show_home_icon_toolbar_togglebutton"
#define NEMO_FILE_MANAGEMENT_PROPERTIES_SHOW_COMPUTER_ICON_TOOLBAR_WIDGET "show_computer_icon_toolbar_togglebutton"
#define NEMO_FILE_MANAGEMENT_PROPERTIES_SHOW_SEARCH_ICON_TOOLBAR_WIDGET "show_search_icon_toolbar_togglebutton"
#define NEMO_FILE_MANAGEMENT_PROPERTIES_SHOW_NEW_FOLDER_ICON_TOOLBAR_WIDGET "show_new_folder_icon_toolbar_togglebutton"
#define NEMO_FILE_MANAGEMENT_PROPERTIES_SHOW_OPEN_IN_TERMINAL_ICON_TOOLBAR_WIDGET "show_open_in_terminal_icon_toolbar_togglebutton"
#define NEMO_FILE_MANAGEMENT_PROPERTIES_SHOW_ICON_VIEW_ICON_TOOLBAR_WIDGET "show_icon_view_icon_toolbar_togglebutton"
#define NEMO_FILE_MANAGEMENT_PROPERTIES_SHOW_LIST_VIEW_ICON_TOOLBAR_WIDGET "show_list_view_icon_toolbar_togglebutton"
#define NEMO_FILE_MANAGEMENT_PROPERTIES_SHOW_COMPACT_VIEW_ICON_TOOLBAR_WIDGET "show_compact_view_icon_toolbar_togglebutton"
#define NEMO_FILE_MANAGEMENT_PROPERTIES_SHOW_SHOW_THUMBNAILS_ICON_TOOLBAR_WIDGET "show_show_thumbnails_icon_toolbar_togglebutton"
#define NEMO_FILE_MANAGEMENT_PROPERTIES_SHOW_TOGGLE_EXTRA_PANE_ICON_TOOLBAR_WIDGET "show_toggle_extra_pane_icon_toolbar_togglebutton"

#define NEMO_FILE_MANAGEMENT_PROPERTIES_SHOW_FULL_PATH_IN_TITLE_BARS_WIDGET "show_full_path_in_title_bars_checkbutton"
#define NEMO_FILE_MANAGEMENT_PROPERTIES_CLOSE_DEVICE_VIEW_ON_EJECT_WIDGET "close_device_view_on_eject_checkbutton"
#define NEMO_FILE_MANAGEMENT_PROPERTIES_AUTOMOUNT_MEDIA_WIDGET "media_automount_checkbutton"
#define NEMO_FILE_MANAGEMENT_PROPERTIES_AUTOOPEN_MEDIA_WIDGET "media_autoopen_checkbutton"
#define NEMO_FILE_MANAGEMENT_PROPERTIES_DETECT_CONTENT_MEDIA_WIDGET "media_detect_content_checkbutton"
#define NEMO_FILE_MANAGEMENT_PROPERTIES_SHOW_ADVANCED_PERMISSIONS_WIDGET "show_advanced_permissions_checkbutton"
#define NEMO_FILE_MANAGEMENT_PROPERTIES_START_WITH_DUAL_PANE_WIDGET "start_with_dual_pane_checkbutton"
#define NEMO_FILE_MANAGEMENT_PROPERTIES_ALWAYS_SHOW_TABS_WIDGET "always_show_tabs_checkbutton"
#define NEMO_FILE_MANAGEMENT_PROPERTIES_REMEMBER_FOLDER_SETTINGS_WIDGET "remember_folder_settings_checkbutton"
#define NEMO_FILE_MANAGEMENT_PROPERTIES_BOOKMARKS_IN_TO_MENUS_WIDGET "bookmarks_in_to_checkbutton"
#define NEMO_FILE_MANAGEMENT_PROPERTIES_PLACES_IN_TO_MENUS_WIDGET "places_in_to_checkbutton"
#define NEMO_FILE_MANAGEMENT_PROPERTIES_INHERIT_SHOW_THUMBNAILS_WIDGET "inherit_show_thumbnails_checkbutton"

#define NEMO_FILE_MANAGEMENT_PROPERTIES_BULK_RENAME_WIDGET "bulk_rename_entry"
#define NEMO_FILE_MANAGEMENT_PROPERTIES_TERMINAL_EXEC_WIDGET "terminal_exec_entry"

#define NEMO_FILE_MANAGEMENT_PROPERTIES_TOOLTIPS_ON_ICON_VIEW_WIDGET "tooltips_on_icon_view_checkbutton"
#define NEMO_FILE_MANAGEMENT_PROPERTIES_TOOLTIPS_ON_LIST_VIEW_WIDGET "tooltips_on_list_view_checkbutton"
#define NEMO_FILE_MANAGEMENT_PROPERTIES_TOOLTIP_FILE_TYPE_WIDGET "tt_show_file_type_checkbutton"
#define NEMO_FILE_MANAGEMENT_PROPERTIES_TOOLTIP_MOD_DATE_WIDGET "tt_show_modified_date_checkbutton"
#define NEMO_FILE_MANAGEMENT_PROPERTIES_TOOLTIP_ACCESS_DATE_WIDGET "tt_show_accessed_date_checkbutton"
#define NEMO_FILE_MANAGEMENT_PROPERTIES_TOOLTIP_CREATED_DATE_WIDGET "tt_show_created_date_checkbutton"
#define NEMO_FILE_MANAGEMENT_PROPERTIES_TOOLTIP_FULL_PATH_WIDGET "tt_show_full_path_checkbutton"

#define NEMO_FILE_MANAGEMENT_PROPERTIES_NEMO_PREFERENCES_SKIP_FILE_OP_QUEUE_WIDGET "skip_file_op_queue_checkbutton"
#define NEMO_FILE_MANAGEMENT_PROPERTIES_NEMO_PREFERENCES_CLICK_DBL_PARENT_FOLDER_WIDGET "click_double_parent_folder_checkbutton"
#define NEMO_FILE_MANAGEMENT_PROPERTIES_NEMO_PREFERENCES_EXPAND_ROW_ON_DND_DWELL_WIDGET "expand_row_on_dnd_dwell_checkbutton"

/* int enums */
#define NEMO_FILE_MANAGEMENT_PROPERTIES_THUMBNAIL_LIMIT_WIDGET "preview_image_size_combobox"

#define W(s) (gtk_builder_get_object (builder, s))

#define TOOLBAR_PADDING 20

static const char * const default_view_values[] = {
	"icon-view",
	"list-view",
	"compact-view",
	NULL
};

static const char * const sort_order_values[] = {
	"name",
	"size",
	"type",
    "detailed_type",
	"mtime",
	"atime",
	"trash-time",
	NULL
};

static const char * const date_format_values[] = {
	"locale",
	"iso",
	"informal",
	NULL
};

/* Order matches the rows appended to path_separator_combobox. */
static const char * const path_separator_values[] = {
	"backslash",
	"slash",
	NULL
};

/* Order matches the rows appended to appearance_mode_combobox. */
static const char * const appearance_mode_values[] = {
	"system",
	"light",
	"dark",
	NULL
};

static const char * const preview_image_values[] = {
    "always",
    "local-only",
    "never",
    NULL
};

static const char * const preview_folder_values[] = {
	"always",
	"local-only",
	"never",
	NULL
};

static const char * const click_behavior_components[] = {
	"single_click_radiobutton",
	"double_click_radiobutton",
	NULL
};

static const char * const click_behavior_values[] = {
	"single",
	"double",
	NULL
};

static const char * const executable_text_components[] = {
	"scripts_execute_radiobutton",
	"scripts_view_radiobutton",
	"scripts_confirm_radiobutton",
	NULL
};

static const char * const executable_text_values[] = {
	"launch",
	"display",
	"ask",
	NULL
};

static const char * const size_prefixes_values[] = {
	"base-10",
	"base-10-full",
	"base-2",
	"base-2-full",
	NULL
};

static const guint64 thumbnail_limit_values[] = {
	102400,
	512000,
	1048576,
	3145728,
	5242880,
	10485760,
	104857600,
	1073741824,
	2147483648U,
	4294967295U,
	8589934592U,
	17179869184U,
	34359738368U,
	68719476736U
};

static const char * const icon_captions_components[] = {
	"captions_0_combobox",
	"captions_1_combobox",
	"captions_2_combobox",
	NULL
};

static GtkWidget *preferences_dialog = NULL;

static void
nemo_file_management_properties_size_group_create (GtkBuilder *builder,
						       char *prefix,
						       int items)
{
	GtkSizeGroup *size_group;
	int i;
	char *item_name;
	GtkWidget *widget;

	size_group = gtk_size_group_new (GTK_SIZE_GROUP_HORIZONTAL);

	for (i = 0; i < items; i++) {	
		item_name = g_strdup_printf ("%s_%d", prefix, i);
		widget = GTK_WIDGET (gtk_builder_get_object (builder, item_name));
		gtk_size_group_add_widget (size_group, widget);
		g_free (item_name);
	}
	g_object_unref (G_OBJECT (size_group));
}

static void
columns_changed_callback (NemoColumnChooser *chooser,
			  G_GNUC_UNUSED gpointer callback_data)
{
	char **visible_columns;
	char **column_order;

	nemo_column_chooser_get_settings (NEMO_COLUMN_CHOOSER (chooser),
					      &visible_columns,
					      &column_order);

	nemo_config_set_strv (nemo_list_view_preferences,
			     NEMO_PREFERENCES_LIST_VIEW_DEFAULT_VISIBLE_COLUMNS,
			     (const char * const *)visible_columns);
	nemo_config_set_strv (nemo_list_view_preferences,
			     NEMO_PREFERENCES_LIST_VIEW_DEFAULT_COLUMN_ORDER,
			     (const char * const *)column_order);

	g_strfreev (visible_columns);
	g_strfreev (column_order);
}

static void
free_column_names_array (GPtrArray *column_names)
{
	g_ptr_array_set_free_func (column_names, g_free);
	g_ptr_array_free (column_names, TRUE);
}

static void
create_icon_caption_combo_box_items (GtkComboBoxText *combo_box,
			             GList *columns)
{
	GList *l;
	GPtrArray *column_names;

	column_names = g_ptr_array_new ();

	/* Translators: this is referred to captions under icons. */
	gtk_combo_box_text_append_text (combo_box, _("None"));
	g_ptr_array_add (column_names, g_strdup ("none"));

	for (l = columns; l != NULL; l = l->next) {
		NemoColumn *column;
		char *name;
		char *label;

		column = NEMO_COLUMN (l->data);

		g_object_get (G_OBJECT (column), 
			      "name", &name, "label", &label, 
			      NULL);

		/* Don't show name here, it doesn't make sense */
		if (!strcmp (name, "name")) {
			g_free (name);
			g_free (label);
			continue;
		}

		gtk_combo_box_text_append_text (combo_box, label);
		g_ptr_array_add (column_names, name);

		g_free (label);
	}
	g_object_set_data_full (G_OBJECT (combo_box), "column_names",
			        column_names,
			        (GDestroyNotify) free_column_names_array);
}

static void
icon_captions_changed_callback (G_GNUC_UNUSED GtkComboBox *combo_box,
				gpointer user_data)
{
	GPtrArray *captions;
	GtkBuilder *builder;
	guint i;

	builder = GTK_BUILDER (user_data);

	captions = g_ptr_array_new ();

	for (i = 0; icon_captions_components[i] != NULL; i++) {
		int active;
		GPtrArray *column_names;
		char *name;
		GtkWidget *c_box;

		c_box = GTK_WIDGET (gtk_builder_get_object
					(builder, icon_captions_components[i]));
		active = gtk_combo_box_get_active (GTK_COMBO_BOX (c_box));

		column_names = g_object_get_data (G_OBJECT (c_box),
						  "column_names");

		name = g_ptr_array_index (column_names, active);
		g_ptr_array_add (captions, name);
	}
	g_ptr_array_add (captions, NULL);

	nemo_config_set_strv (nemo_icon_view_preferences,
			     NEMO_PREFERENCES_ICON_VIEW_CAPTIONS,
			     (const char **)captions->pdata);
	g_ptr_array_free (captions, TRUE);
}

static void
update_caption_combo_box (GtkBuilder *builder,
			  const char *combo_box_name,
			  const char *name)
{
	GtkWidget *combo_box;
	guint i;
	GPtrArray *column_names;

	combo_box = GTK_WIDGET (gtk_builder_get_object (builder, combo_box_name));

	g_signal_handlers_block_by_func
		(combo_box,
		 G_CALLBACK (icon_captions_changed_callback),
		 builder);

	column_names = g_object_get_data (G_OBJECT (combo_box), 
					  "column_names");

	for (i = 0; i < column_names->len; ++i) {
		if (!strcmp (name, g_ptr_array_index (column_names, i))) {
			gtk_combo_box_set_active (GTK_COMBO_BOX (combo_box), i);
			break;
		}
	}

	g_signal_handlers_unblock_by_func
		(combo_box,
		 G_CALLBACK (icon_captions_changed_callback),
		 builder);
}

static void
update_icon_captions_from_settings (GtkBuilder *builder)
{
	char **captions;
	int i, j;

	captions = nemo_config_get_strv (nemo_icon_view_preferences, NEMO_PREFERENCES_ICON_VIEW_CAPTIONS);
	if (captions == NULL)
		return;

	for (i = 0, j = 0; 
	     icon_captions_components[i] != NULL;
	     i++) {
		char *data;

		if (captions[j]) {
			data = captions[j];
			++j;
		} else {
			data = (char *)"none";
		}

		update_caption_combo_box (builder, 
					  icon_captions_components[i],
					  data);
	}

	g_strfreev (captions);
}

static void
nemo_file_management_properties_dialog_setup_icon_caption_page (GtkBuilder *builder)
{
	GList *columns;
	int i;
	gboolean writable = TRUE;   /* our config is a plain file we own */

	columns = nemo_get_common_columns ();

	for (i = 0; icon_captions_components[i] != NULL; i++) {
		GtkWidget *combo_box;

		combo_box = GTK_WIDGET (gtk_builder_get_object (builder,
								icon_captions_components[i]));

		create_icon_caption_combo_box_items (GTK_COMBO_BOX_TEXT (combo_box), columns);
		gtk_widget_set_sensitive (combo_box, writable);

		g_signal_connect (combo_box, "changed",
				  G_CALLBACK (icon_captions_changed_callback),
				  builder);
	}

	nemo_column_list_free (columns);

	update_icon_captions_from_settings (builder);
}

static void
nemo_file_management_properties_dialog_setup_plugin_page (GtkBuilder *builder)
{
    GtkWidget *box;

    box = GTK_WIDGET (gtk_builder_get_object (builder, "plugin_box"));

    gtk_box_pack_start (GTK_BOX (box),
                        GTK_WIDGET (nemo_plugin_manager_new ()),
                        TRUE, TRUE, 0);
}

static void
nemo_file_management_properties_dialog_setup_templates_page (GtkBuilder *builder)
{
    GtkWidget *box;

    box = GTK_WIDGET (gtk_builder_get_object (builder, "templates_box"));

    gtk_box_pack_start (GTK_BOX (box),
                        GTK_WIDGET (nemo_template_config_widget_new ()),
                        TRUE, TRUE, 0);
}

/* Appearance page.
 *
 * Only the themes drawn for the mode in force are offered, so the two lists
 * change as the mode does. Each row remembers the theme's directory name in a
 * parallel array; row 0 is the app's own default - whatever the platform picked,
 * which on the bundled targets is the theme the app ships with - and is stored
 * as an empty string.
 */

#define APPEARANCE_ROW_NAMES	"nemo-appearance-row-names"
#define APPEARANCE_KEY		"nemo-appearance-key"

static void fill_appearance_combo (GtkComboBoxText *combo,
				   NemoThemeKind    kind,
				   const char      *key);

/* Picking a look should bring its icons with it - a Windows 11 window frame
 * full of macOS icons is nobody's intent. Only when the style has an icon set
 * of its own; where it has none (Windows 10 today) the icons stay put rather
 * than jumping to something unrelated. */
static void
match_icons_to_style (GtkBuilder *builder, const char *widget_name)
{
	char *icons;

	if (widget_name != NULL && widget_name[0] == '\0') {
		/* Back to the platform's own answer for both halves. */
		icons = g_strdup ("");
	} else {
		icons = nemo_appearance_icons_for_widget_theme (widget_name);
		if (icons == NULL) {
			return;
		}
	}

	nemo_config_set_string (nemo_appearance_preferences,
				NEMO_PREFERENCES_APPEARANCE_ICON_THEME, icons);
	fill_appearance_combo (GTK_COMBO_BOX_TEXT (gtk_builder_get_object (builder, "appearance_icon_combobox")),
			       NEMO_THEME_KIND_ICON, NEMO_PREFERENCES_APPEARANCE_ICON_THEME);
	g_free (icons);
}

static void
appearance_combo_changed (GtkComboBox *combo, gpointer user_data)
{
	const char *key = g_object_get_data (G_OBJECT (combo), APPEARANCE_KEY);
	GPtrArray  *names;
	const char *chosen;
	int         active;

	if (g_object_get_data (G_OBJECT (combo), "nemo-appearance-filling") != NULL) {
		return;
	}

	names = g_object_get_data (G_OBJECT (combo), APPEARANCE_ROW_NAMES);
	active = gtk_combo_box_get_active (combo);

	if (names == NULL || active < 0 || active >= (int) names->len) {
		return;
	}

	chosen = g_ptr_array_index (names, active);
	nemo_config_set_string (nemo_appearance_preferences, key, chosen);

	if (g_strcmp0 (key, NEMO_PREFERENCES_APPEARANCE_GTK_THEME) == 0) {
		match_icons_to_style (GTK_BUILDER (user_data), chosen);
	}
}

static void
fill_appearance_combo (GtkComboBoxText *combo,
		       NemoThemeKind    kind,
		       const char      *key)
{
	GList     *themes;
	GList     *node;
	GPtrArray *names;
	char      *stored;
	char      *resolved = NULL;
	guint      fits;
	int        row = 0;
	int        active = 0;

	fits = nemo_appearance_is_dark () ? NEMO_THEME_FITS_DARK : NEMO_THEME_FITS_LIGHT;
	themes = nemo_appearance_list_themes (kind, fits);

	stored = nemo_config_get_string (nemo_appearance_preferences, key);
	if (stored != NULL && stored[0] != '\0') {
		resolved = nemo_appearance_theme_for_mode (kind, stored);
	}

	names = g_ptr_array_new_with_free_func (g_free);

	g_object_set_data (G_OBJECT (combo), "nemo-appearance-filling", GINT_TO_POINTER (1));

	gtk_combo_box_text_remove_all (combo);
	gtk_combo_box_text_append_text (combo, _("Nemo Anywhere"));
	g_ptr_array_add (names, g_strdup (""));

	for (node = themes; node != NULL; node = node->next) {
		NemoThemeInfo *info = node->data;

		row++;
		gtk_combo_box_text_append_text (combo,
						info->style != NULL ? info->style : info->display);
		g_ptr_array_add (names, g_strdup (info->name));

		if (resolved != NULL && strcmp (resolved, info->name) == 0) {
			active = row;
		}
	}

	g_object_set_data_full (G_OBJECT (combo), APPEARANCE_ROW_NAMES,
				names, (GDestroyNotify) g_ptr_array_unref);
	gtk_combo_box_set_active (GTK_COMBO_BOX (combo), active);

	g_object_set_data (G_OBJECT (combo), "nemo-appearance-filling", NULL);

	g_free (stored);
	g_free (resolved);
	g_list_free_full (themes, (GDestroyNotify) nemo_theme_info_free);
}

static void
appearance_mode_changed (G_GNUC_UNUSED NemoConfigGroup *group,
			 G_GNUC_UNUSED const char      *key,
			 gpointer         user_data)
{
	GtkBuilder *builder = user_data;

	fill_appearance_combo (GTK_COMBO_BOX_TEXT (gtk_builder_get_object (builder, "appearance_style_combobox")),
			       NEMO_THEME_KIND_WIDGET, NEMO_PREFERENCES_APPEARANCE_GTK_THEME);
	fill_appearance_combo (GTK_COMBO_BOX_TEXT (gtk_builder_get_object (builder, "appearance_icon_combobox")),
			       NEMO_THEME_KIND_ICON, NEMO_PREFERENCES_APPEARANCE_ICON_THEME);
}

static void
create_date_format_menu (GtkBuilder *builder)
{
	GtkComboBoxText *combo_box;
	gchar *date_string;
	GDateTime *now;

	combo_box = GTK_COMBO_BOX_TEXT
		(gtk_builder_get_object (builder,
					 NEMO_FILE_MANAGEMENT_PROPERTIES_DATE_FORMAT_WIDGET));

	now = g_date_time_new_now_local ();

	date_string = g_date_time_format (now, "%c");
	gtk_combo_box_text_append_text (combo_box, date_string);
	g_free (date_string);

	date_string = g_date_time_format (now, "%Y-%m-%d %H:%M:%S");
	gtk_combo_box_text_append_text (combo_box, date_string);
	g_free (date_string);

	gtk_combo_box_text_append_text (combo_box, _("Yesterday"));

	g_date_time_unref (now);
}

#ifdef G_OS_WIN32
/* A forward slash cannot be refused while it is the separator on screen, so the
   switch is pinned on and grayed out for as long as it is. */
static void
path_separator_changed (GtkComboBox *combo_box,
			GtkWidget   *check)
{
	gboolean showing_slash = gtk_combo_box_get_active (combo_box) == 1;

	if (showing_slash) {
		gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (check), TRUE);
	}

	gtk_widget_set_sensitive (check, !showing_slash);
}
#endif

#ifndef G_OS_WIN32
static void
hide_group (GtkBuilder *builder, const char *id)
{
	GtkWidget *group = GTK_WIDGET (gtk_builder_get_object (builder, id));

	gtk_widget_hide (group);
	gtk_widget_set_no_show_all (group, TRUE);
}
#endif

/* Everything on the Windows page is meaningless elsewhere, so the page goes.
   The shortcut group is not on it: .desktop launchers hide their extension too. */
static void
set_up_windows_page (GtkBuilder *builder)
{
#ifdef G_OS_WIN32
	GtkComboBoxText *combo_box;
	GtkWidget *check;

	combo_box = GTK_COMBO_BOX_TEXT
		(gtk_builder_get_object (builder,
					 NEMO_FILE_MANAGEMENT_PROPERTIES_PATH_SEPARATOR_WIDGET));
	check = GTK_WIDGET (gtk_builder_get_object (builder,
						    NEMO_FILE_MANAGEMENT_PROPERTIES_ALLOW_SLASH_INPUT_WIDGET));

	gtk_combo_box_text_append_text (combo_box, "\\");
	gtk_combo_box_text_append_text (combo_box, "/");

	g_signal_connect_object (combo_box, "changed",
				 G_CALLBACK (path_separator_changed), check, 0);
#else
	GtkButton *check;

	check = GTK_BUTTON (gtk_builder_get_object (builder,
						    NEMO_FILE_MANAGEMENT_PROPERTIES_SHOW_SHORTCUT_EXTENSION_WIDGET));
	gtk_button_set_label (check, _("Show the _.desktop extension in a shortcut's name"));

	hide_group (builder, "windows_scrolledwindow");
#endif
}

static void
set_columns_from_settings (NemoColumnChooser *chooser)
{
	char **visible_columns;
	char **column_order;

	visible_columns = nemo_config_get_strv (nemo_list_view_preferences,
					       NEMO_PREFERENCES_LIST_VIEW_DEFAULT_VISIBLE_COLUMNS);
	column_order = nemo_config_get_strv (nemo_list_view_preferences,
					    NEMO_PREFERENCES_LIST_VIEW_DEFAULT_COLUMN_ORDER);

	nemo_column_chooser_set_settings (NEMO_COLUMN_CHOOSER (chooser),
					      visible_columns,
					      column_order);

	g_strfreev (visible_columns);
	g_strfreev (column_order);
}

static void
use_default_callback (NemoColumnChooser *chooser,
		      G_GNUC_UNUSED gpointer user_data)
{
	nemo_config_reset (nemo_list_view_preferences,
			  NEMO_PREFERENCES_LIST_VIEW_DEFAULT_VISIBLE_COLUMNS);
	nemo_config_reset (nemo_list_view_preferences,
			  NEMO_PREFERENCES_LIST_VIEW_DEFAULT_COLUMN_ORDER);
	set_columns_from_settings (chooser);
}

static void
nemo_file_management_properties_dialog_setup_list_column_page (GtkBuilder *builder)
{
	GtkWidget *chooser;
	GtkWidget *box;

	chooser = nemo_column_chooser_new (NULL);

	set_columns_from_settings (NEMO_COLUMN_CHOOSER (chooser));

	g_signal_connect (chooser, "changed",
			  G_CALLBACK (columns_changed_callback), chooser);
	g_signal_connect (chooser, "use_default",
			  G_CALLBACK (use_default_callback), chooser);

	gtk_widget_show (chooser);
	box = GTK_WIDGET (gtk_builder_get_object (builder, "list_columns_vbox"));

	gtk_box_pack_start (GTK_BOX (box), chooser, TRUE, TRUE, 0);
}

static void
bind_builder_bool (GtkBuilder *builder,
		   NemoConfigGroup *settings,
		   const char *widget_name,
		   const char *prefs)
{
	nemo_config_bind (settings, prefs,
			 gtk_builder_get_object (builder, widget_name),
			 "active", NEMO_CONFIG_BIND_DEFAULT);
}

static void
bind_builder_bool_inverted (GtkBuilder *builder,
			    NemoConfigGroup *settings,
			    const char *widget_name,
			    const char *prefs)
{
	nemo_config_bind (settings, prefs,
			 gtk_builder_get_object (builder, widget_name),
			 "active", NEMO_CONFIG_BIND_INVERT_BOOLEAN);
}

static void
bind_builder_string_entry (GtkBuilder *builder,
                            NemoConfigGroup *settings,
                           const char *widget_name,
                           const char *prefs)
{
    nemo_config_bind (settings, prefs,
                     gtk_builder_get_object (builder, widget_name),
                     "text", NEMO_CONFIG_BIND_DEFAULT);
}

static gboolean
enum_get_mapping (GValue                *value,
		  const NemoConfigValue *config_value,
		  gpointer               user_data)
{
	const char **enum_values = user_data;
	const char *str;
	int i;

	str = config_value->s;
	for (i = 0; enum_values[i] != NULL; i++) {
		if (strcmp (enum_values[i], str) == 0) {
			g_value_set_int (value, i);
			return TRUE;
		}
	}

	return FALSE;
}

static gboolean
enum_set_mapping (const GValue    *value,
		  NemoConfigValue *config_value,
		  gpointer         user_data)
{
	const char **enum_values = user_data;

	config_value->s = g_strdup (enum_values[g_value_get_int (value)]);
	return TRUE;
}

static void
bind_builder_enum (GtkBuilder *builder,
		   NemoConfigGroup *settings,
		   const char *widget_name,
		   const char *prefs,
		   const char **enum_values)
{
	nemo_config_bind_with_mapping (settings, prefs,
				      gtk_builder_get_object (builder, widget_name),
				      "active", NEMO_CONFIG_BIND_DEFAULT,
				      enum_get_mapping,
				      enum_set_mapping,
				      enum_values, NULL);
}


typedef struct {
	const guint64 *values;
	int n_values;
} UIntEnumBinding;

static gboolean
uint_enum_get_mapping (GValue                *value,
		       const NemoConfigValue *config_value,
		       gpointer               user_data)
{
	UIntEnumBinding *binding = user_data;
	guint64 v;
	int i;

	v = (guint64) config_value->i;

	/* Exact first, so a list carrying a zero for "no limit" finds it wherever
	   it sits rather than settling on the smallest real entry. */
	for (i = 0; i < binding->n_values; i++) {
		if (binding->values[i] == v) {
			g_value_set_int (value, i);
			return TRUE;
		}
	}

	for (i = 0; i < binding->n_values; i++) {
		if (binding->values[i] >= v) {
			g_value_set_int (value, i);
			return TRUE;
		}
	}

	return FALSE;
}

static gboolean
uint_enum_set_mapping (const GValue    *value,
		       NemoConfigValue *config_value,
		       gpointer         user_data)
{
	UIntEnumBinding *binding = user_data;

	config_value->i = (gint64) binding->values[g_value_get_int (value)];
	return TRUE;
}

static void
bind_builder_uint_enum (GtkBuilder *builder,
			NemoConfigGroup *settings,
			const char *widget_name,
			const char *prefs,
			const guint64 *values,
			int n_values)
{
	UIntEnumBinding *binding;

	binding = g_new (UIntEnumBinding, 1);
	binding->values = values;
	binding->n_values = n_values;

	nemo_config_bind_with_mapping (settings, prefs,
				      gtk_builder_get_object (builder, widget_name),
				      "active", NEMO_CONFIG_BIND_DEFAULT,
				      uint_enum_get_mapping,
				      uint_enum_set_mapping,
				      binding, g_free);
}

/* A size spin box holds a per cent of the standard 64 pixels. The widget's own
   value is a double and the setting is a whole number. */
static gboolean
percent_get_mapping (GValue                *value,
		     const NemoConfigValue *config_value,
		     G_GNUC_UNUSED gpointer               user_data)
{
	g_value_set_double (value, (gdouble) config_value->i);
	return TRUE;
}

static gboolean
percent_set_mapping (const GValue    *value,
		     NemoConfigValue *config_value,
		     G_GNUC_UNUSED gpointer         user_data)
{
	config_value->i = (gint64) (g_value_get_double (value) + 0.5);
	return TRUE;
}

static void
bind_builder_percent (GtkBuilder *builder,
		      NemoConfigGroup *settings,
		      const char *widget_name,
		      const char *prefs)
{
	nemo_config_bind_with_mapping (settings, prefs,
				      gtk_builder_get_object (builder, widget_name),
				      "value", NEMO_CONFIG_BIND_DEFAULT,
				      percent_get_mapping,
				      percent_set_mapping,
				      NULL, NULL);
}

/* One radio button per value: only the button being switched ON writes. */
static gboolean
radio_mapping_set (const GValue    *gvalue,
		   NemoConfigValue *config_value,
		   gpointer         user_data)
{
	const gchar *widget_value = user_data;

	if (!g_value_get_boolean (gvalue))
		return FALSE;

	config_value->s = g_strdup (widget_value);
	return TRUE;
}

static gboolean
radio_mapping_get (GValue                *gvalue,
		   const NemoConfigValue *config_value,
		   gpointer               user_data)
{
	const gchar *widget_value = user_data;
	const gchar *value;

	value = config_value->s;

	if (g_strcmp0 (value, widget_value) == 0) {
		g_value_set_boolean (gvalue, TRUE);
	} else {
		g_value_set_boolean (gvalue, FALSE);
	}

	return TRUE;
}

static void
bind_builder_radio (GtkBuilder *builder,
		    NemoConfigGroup *settings,
		    const char **widget_names,
		    const char *prefs,
		    const char **values)
{
	GtkWidget *button;
	int i;

	for (i = 0; widget_names[i] != NULL; i++) {
		button = GTK_WIDGET (gtk_builder_get_object (builder, widget_names[i]));

		nemo_config_bind_with_mapping (settings, prefs,
					      button, "active",
					      NEMO_CONFIG_BIND_DEFAULT,
					      radio_mapping_get, radio_mapping_set,
					      (gpointer) values[i], NULL);
	}
}

static void
setup_configurable_menu_items (GtkBuilder *builder)
{
    guint i;

    for (i = 0; i < CONFIGURABLE_MENU_ITEM_COUNT; i++) {
        if (CONFIGURABLE_MENU_ITEM_INFO[i].config_widget_name == NULL) {
            continue;
        }

        bind_builder_bool (builder,
                           nemo_menu_config_preferences,
                           CONFIGURABLE_MENU_ITEM_INFO[i].config_widget_name,
                           CONFIGURABLE_MENU_ITEM_INFO[i].settings_key);
    }
}

static void
setup_tooltip_items (GtkBuilder *builder)
{
    gboolean enabled = FALSE;

    enabled = gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (W (NEMO_FILE_MANAGEMENT_PROPERTIES_TOOLTIPS_ON_ICON_VIEW_WIDGET))) ||
              gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (W (NEMO_FILE_MANAGEMENT_PROPERTIES_TOOLTIPS_ON_LIST_VIEW_WIDGET)));

    gtk_widget_set_sensitive (GTK_WIDGET (W (NEMO_FILE_MANAGEMENT_PROPERTIES_TOOLTIP_FILE_TYPE_WIDGET)), enabled);
    gtk_widget_set_sensitive (GTK_WIDGET (W (NEMO_FILE_MANAGEMENT_PROPERTIES_TOOLTIP_MOD_DATE_WIDGET)), enabled);
    gtk_widget_set_sensitive (GTK_WIDGET (W (NEMO_FILE_MANAGEMENT_PROPERTIES_TOOLTIP_CREATED_DATE_WIDGET)), enabled);
    gtk_widget_set_sensitive (GTK_WIDGET (W (NEMO_FILE_MANAGEMENT_PROPERTIES_TOOLTIP_ACCESS_DATE_WIDGET)), enabled);
    gtk_widget_set_sensitive (GTK_WIDGET (W (NEMO_FILE_MANAGEMENT_PROPERTIES_TOOLTIP_FULL_PATH_WIDGET)), enabled);
}

static void
connect_tooltip_items (GtkBuilder *builder)
{
    GtkToggleButton *w;

    w = GTK_TOGGLE_BUTTON (W (NEMO_FILE_MANAGEMENT_PROPERTIES_TOOLTIPS_ON_ICON_VIEW_WIDGET));
    g_signal_connect_swapped (w, "toggled", G_CALLBACK (setup_tooltip_items), builder);

    w = GTK_TOGGLE_BUTTON (W (NEMO_FILE_MANAGEMENT_PROPERTIES_TOOLTIPS_ON_LIST_VIEW_WIDGET));
    g_signal_connect_swapped (w, "toggled", G_CALLBACK (setup_tooltip_items), builder);

}

/* When single click radio button is selected, checkbox for quick renames should get unselected and disable to avoid annoying features */
static void
setup_quick_renames (GtkBuilder *builder)
{
	gboolean enabled = FALSE;
	enabled = gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (W (click_behavior_components[1])));
	if(enabled==FALSE){
		gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(W (NEMO_FILE_MANAGEMENT_QUICK_RENAMES_WITH_PAUSE_IN_BETWEEN)), FALSE);
	}
	gtk_widget_set_sensitive (GTK_WIDGET (W (NEMO_FILE_MANAGEMENT_QUICK_RENAMES_WITH_PAUSE_IN_BETWEEN)), enabled);
}

static void
connect_quick_renames (GtkBuilder *builder)
{
	GtkRadioButton *w;
	w=GTK_RADIO_BUTTON(W(click_behavior_components[0]));
 		g_signal_connect_swapped (w, "toggled", G_CALLBACK (setup_quick_renames), builder);

	w=GTK_RADIO_BUTTON(W(click_behavior_components[1]));
		g_signal_connect_swapped (w, "toggled", G_CALLBACK (setup_quick_renames), builder);
}

static void
on_dialog_destroy (G_GNUC_UNUSED GtkWidget *widget,
                   gpointer   user_data)
{
    GtkBuilder *builder = GTK_BUILDER (user_data);

    g_object_unref (builder);
}

static void
set_gtk_filechooser_sort_first (GObject *object,
				G_GNUC_UNUSED GParamSpec *pspec)
{
	nemo_desktop_settings_set_filechooser_bool (NEMO_PREFERENCES_SORT_DIRECTORIES_FIRST,
						   gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (object)));
}

static void
nemo_file_management_properties_dialog_setup_appearance_page (GtkBuilder *builder)
{
	GtkComboBoxText     *mode;
	GtkWidget           *style_combo;
	GtkWidget           *icon_combo;
	GtkWidget           *note;
	const char * const  *roots;
	char                *dir;
	char                *text;

	mode = GTK_COMBO_BOX_TEXT (gtk_builder_get_object (builder, "appearance_mode_combobox"));
	gtk_combo_box_text_append_text (mode, _("Follow the system"));
	gtk_combo_box_text_append_text (mode, _("Light"));
	gtk_combo_box_text_append_text (mode, _("Dark"));

	bind_builder_enum (builder, nemo_appearance_preferences,
			   "appearance_mode_combobox",
			   NEMO_PREFERENCES_APPEARANCE_MODE,
			   (const char **) appearance_mode_values);

	style_combo = GTK_WIDGET (gtk_builder_get_object (builder, "appearance_style_combobox"));
	icon_combo = GTK_WIDGET (gtk_builder_get_object (builder, "appearance_icon_combobox"));

	fill_appearance_combo (GTK_COMBO_BOX_TEXT (style_combo),
			       NEMO_THEME_KIND_WIDGET, NEMO_PREFERENCES_APPEARANCE_GTK_THEME);
	fill_appearance_combo (GTK_COMBO_BOX_TEXT (icon_combo),
			       NEMO_THEME_KIND_ICON, NEMO_PREFERENCES_APPEARANCE_ICON_THEME);

	g_object_set_data (G_OBJECT (style_combo), APPEARANCE_KEY,
			   (gpointer) NEMO_PREFERENCES_APPEARANCE_GTK_THEME);
	g_object_set_data (G_OBJECT (icon_combo), APPEARANCE_KEY,
			   (gpointer) NEMO_PREFERENCES_APPEARANCE_ICON_THEME);
	g_signal_connect (style_combo, "changed", G_CALLBACK (appearance_combo_changed), builder);
	g_signal_connect (icon_combo, "changed", G_CALLBACK (appearance_combo_changed), builder);

	/* Refilter as the mode changes - including a change made outside the
	 * dialog. Tied to the builder so it goes when the dialog does. */
	g_signal_connect_object (nemo_appearance_preferences,
				 "changed::" NEMO_PREFERENCES_APPEARANCE_MODE,
				 G_CALLBACK (appearance_mode_changed), builder, 0);

	note = GTK_WIDGET (gtk_builder_get_object (builder, "appearance_dropin_label"));
	roots = nemo_appearance_get_theme_roots ();
	if (roots != NULL && roots[0] != NULL) {
		dir = g_build_filename (roots[0], "themes", NULL);
		text = g_strdup_printf (_("Themes placed in %s and the icons folder beside it "
					  "are offered here too."), dir);
		gtk_label_set_text (GTK_LABEL (note), text);
		g_free (text);
		g_free (dir);
	}
}

/* The dialog opens at least this big whatever it holds, so it does not come up
   as a cramped little box. Written for a 96dpi screen - see text_scale. */
#define PREFERENCES_MIN_WIDTH 1000
#define PREFERENCES_MIN_HEIGHT 700

/* Every page in the stack, in the order the sidebar lists them. */
static const char *const preferences_pages[] = {
	"scrolledwindow2",		/* Views */
	"scrolledwindow1",		/* Behavior */
	"scrolledwindow3",		/* Display */
	"windows_scrolledwindow",	/* Windows */
	"scrolledwindow4",		/* List columns */
	"scrolledwindow5",		/* Preview */
	"scrolledwindow6",		/* Toolbar */
	"scrolledwindow8",		/* Context menus */
	"templates_scrolledwindow",	/* Document templates */
	"scrolledwindow7",		/* Plugins */
	NULL
};

/* Windows hands a fractional display scale to GTK as a font size and nothing
   else, so at 150% the text grows and a floor written in raw pixels quietly
   means two thirds of what it says. Everything measured scales itself; only the
   two constants above need this. */
static double
text_scale (void)
{
	gint xft_dpi = -1;

	g_object_get (gtk_settings_get_default (), "gtk-xft-dpi", &xft_dpi, NULL);

	if (xft_dpi <= 0) {
		return 1.0;
	}

	return MAX (1.0, xft_dpi / 1024.0 / 96.0);
}

/* A scrolled window asks for almost nothing itself - that is the whole point of
   it - so what is inside it is what gets measured, plus the page's own border. */
static GtkWidget *
page_content (GtkBuilder *builder, const char *id, gint *border)
{
	GtkWidget *page = GTK_WIDGET (gtk_builder_get_object (builder, id));

	if (page == NULL) {
		return NULL;
	}

	*border = 2 * gtk_container_get_border_width (GTK_CONTAINER (page));

	return gtk_bin_get_child (GTK_BIN (page));
}

/* Give the window enough for the longest page, not just the one it opens on:
   sizing to Views alone left Display and Behavior scrolling from the start.
   Measured rather than fixed, because the same page is a different height under
   a different theme, font size or translation. */
/* Escape closes, as it does any dialog. Connected after, so a widget that
   uses the key itself, such as a cell being edited, gets it first. */
static gboolean
dialog_key_press (GtkWidget   *dialog,
		  GdkEventKey *event,
		  G_GNUC_UNUSED gpointer     user_data)
{
	if (event->keyval == GDK_KEY_Escape &&
	    (event->state & gtk_accelerator_get_default_mod_mask ()) == 0) {
		gtk_widget_destroy (dialog);
		return GDK_EVENT_STOP;
	}

	return GDK_EVENT_PROPAGATE;
}

static void
size_dialog_to_longest_page (GtkBuilder *builder,
			     GtkWidget  *dialog,
			     GtkWindow  *parent)
{
	GtkWidget *sidebar, *bar;
	GdkRectangle work;
	GtkRequisition wanted;
	double scale = text_scale ();
	gint content_width = 0;
	gint content_height = 0;
	gint sidebar_width = 0;
	gint width, height;
	gint monitor;
	int i;

	for (i = 0; preferences_pages[i] != NULL; i++) {
		gint border = 0;
		GtkWidget *content = page_content (builder, preferences_pages[i], &border);

		if (content == NULL) {
			continue;
		}

		gtk_widget_get_preferred_size (content, &wanted, NULL);
		content_width = MAX (content_width, wanted.width + border);
	}

	sidebar = GTK_WIDGET (gtk_builder_get_object (builder, "page_sidebar"));
	if (sidebar != NULL) {
		gtk_widget_get_preferred_size (sidebar, &wanted, NULL);
		sidebar_width = wanted.width;
	}

	width = MAX (content_width + sidebar_width, (gint) (PREFERENCES_MIN_WIDTH * scale));

	monitor = parent != NULL
		? nemo_desktop_utils_get_monitor_for_widget (GTK_WIDGET (parent))
		: nemo_desktop_utils_get_primary_monitor ();
	nemo_desktop_utils_get_monitor_work_rect (monitor, &work);

	width = MIN (width, work.width * 9 / 10);

	/* Height depends on width - a label that fits on one line here takes two in
	   a narrower window - so ask only once the width is settled. */
	for (i = 0; preferences_pages[i] != NULL; i++) {
		gint border = 0;
		GtkWidget *content = page_content (builder, preferences_pages[i], &border);
		gint page_height = 0;

		if (content == NULL) {
			continue;
		}

		gtk_widget_get_preferred_height_for_width (content,
							   MAX (1, width - sidebar_width - border),
							   &page_height, NULL);
		content_height = MAX (content_height, page_height + border);
	}

	/* The close bar sits under every page, outside the scrolling. */
	bar = GTK_WIDGET (gtk_builder_get_object (builder, "close_bar"));
	if (bar != NULL) {
		gtk_widget_get_preferred_size (bar, &wanted, NULL);
		content_height += wanted.height + 1;
	}

	height = MAX (content_height, (gint) (PREFERENCES_MIN_HEIGHT * scale));

	if (height > work.height * 9 / 10) {
		height = work.height * 9 / 10;
		/* It will scroll after all, and the bar has to come from somewhere
		   other than the text. */
		width = MIN (width + 20, work.width * 9 / 10);
	}

	gtk_window_set_default_size (GTK_WINDOW (dialog), width, height);
}

/* An overlay scrollbar stays hidden until the pointer is over it, so a page or
   page list cut off by a small screen looks complete. Classic bars still only
   show when something does not fit. The sidebar's own scrolled window is an
   internal child, hence forall. */
static void
show_scrollbars_when_needed (GtkWidget *widget,
			     G_GNUC_UNUSED gpointer   unused)
{
	if (GTK_IS_SCROLLED_WINDOW (widget)) {
		gtk_scrolled_window_set_overlay_scrolling (GTK_SCROLLED_WINDOW (widget), FALSE);
	}

	if (GTK_IS_CONTAINER (widget)) {
		gtk_container_forall (GTK_CONTAINER (widget), show_scrollbars_when_needed, NULL);
	}
}

static  void
nemo_file_management_properties_dialog_setup (GtkBuilder  *builder,
                                              GtkWindow   *window,
                                              const gchar *initial_page)
{
	GtkWidget *dialog, *close_button;

	/* setup UI */
	nemo_file_management_properties_size_group_create (builder,
							       (char *)"views_label",
							       5);
	nemo_file_management_properties_size_group_create (builder,
							       (char *)"views_current_label",
							       5);
	nemo_file_management_properties_size_group_create (builder,
							       (char *)"captions_label",
							       3);
	/* The two command entries on Behavior start at the same x, so the pair
	   reads as one block rather than two rows that happen to be near. */
	nemo_file_management_properties_size_group_create (builder,
							       (char *)"command_label",
							       2);
	nemo_file_management_properties_size_group_create (builder,
							       (char *)"preview_label",
							       3);
	create_date_format_menu (builder);
	set_up_windows_page (builder);


	/* nemo patch */
	bind_builder_bool (builder, nemo_preferences,
			   NEMO_FILE_MANAGEMENT_PROPERTIES_SHOW_PREVIOUS_ICON_TOOLBAR_WIDGET,
			   NEMO_PREFERENCES_SHOW_PREVIOUS_ICON_TOOLBAR);
	bind_builder_bool (builder, nemo_preferences,
			   NEMO_FILE_MANAGEMENT_PROPERTIES_SHOW_NEXT_ICON_TOOLBAR_WIDGET,
			   NEMO_PREFERENCES_SHOW_NEXT_ICON_TOOLBAR);
	bind_builder_bool (builder, nemo_preferences,
			   NEMO_FILE_MANAGEMENT_PROPERTIES_SHOW_UP_ICON_TOOLBAR_WIDGET,
			   NEMO_PREFERENCES_SHOW_UP_ICON_TOOLBAR);
	bind_builder_bool (builder, nemo_preferences,
			   NEMO_FILE_MANAGEMENT_PROPERTIES_SHOW_RELOAD_ICON_TOOLBAR_WIDGET,
			   NEMO_PREFERENCES_SHOW_RELOAD_ICON_TOOLBAR);
	bind_builder_bool (builder, nemo_preferences,
			   NEMO_FILE_MANAGEMENT_PROPERTIES_SHOW_EDIT_ICON_TOOLBAR_WIDGET,
			   NEMO_PREFERENCES_SHOW_EDIT_ICON_TOOLBAR);
	bind_builder_bool (builder, nemo_preferences,
			   NEMO_FILE_MANAGEMENT_PROPERTIES_SHOW_HOME_ICON_TOOLBAR_WIDGET,
			   NEMO_PREFERENCES_SHOW_HOME_ICON_TOOLBAR);
	bind_builder_bool (builder, nemo_preferences,
			   NEMO_FILE_MANAGEMENT_PROPERTIES_SHOW_COMPUTER_ICON_TOOLBAR_WIDGET,
			   NEMO_PREFERENCES_SHOW_COMPUTER_ICON_TOOLBAR);
	bind_builder_bool (builder, nemo_preferences,
			   NEMO_FILE_MANAGEMENT_PROPERTIES_SHOW_SEARCH_ICON_TOOLBAR_WIDGET,
			   NEMO_PREFERENCES_SHOW_SEARCH_ICON_TOOLBAR);
    bind_builder_bool (builder, nemo_preferences,
        NEMO_FILE_MANAGEMENT_PROPERTIES_SHOW_NEW_FOLDER_ICON_TOOLBAR_WIDGET,
        NEMO_PREFERENCES_SHOW_NEW_FOLDER_ICON_TOOLBAR);
    bind_builder_bool (builder, nemo_preferences,
        NEMO_FILE_MANAGEMENT_PROPERTIES_SHOW_OPEN_IN_TERMINAL_ICON_TOOLBAR_WIDGET,
        NEMO_PREFERENCES_SHOW_OPEN_IN_TERMINAL_TOOLBAR);
    bind_builder_bool (builder, nemo_preferences,
        NEMO_FILE_MANAGEMENT_PROPERTIES_SHOW_TOGGLE_EXTRA_PANE_ICON_TOOLBAR_WIDGET,
        NEMO_PREFERENCES_SHOW_TOGGLE_EXTRA_PANE_TOOLBAR);
    bind_builder_bool (builder, nemo_preferences,
			   NEMO_FILE_MANAGEMENT_PROPERTIES_SHOW_ICON_VIEW_ICON_TOOLBAR_WIDGET,
			   NEMO_PREFERENCES_SHOW_ICON_VIEW_ICON_TOOLBAR);
    bind_builder_bool (builder, nemo_preferences,
			   NEMO_FILE_MANAGEMENT_PROPERTIES_SHOW_LIST_VIEW_ICON_TOOLBAR_WIDGET,
			   NEMO_PREFERENCES_SHOW_LIST_VIEW_ICON_TOOLBAR);
    bind_builder_bool (builder, nemo_preferences,
			   NEMO_FILE_MANAGEMENT_PROPERTIES_SHOW_COMPACT_VIEW_ICON_TOOLBAR_WIDGET,
			   NEMO_PREFERENCES_SHOW_COMPACT_VIEW_ICON_TOOLBAR);
    bind_builder_bool (builder, nemo_preferences,
			   NEMO_FILE_MANAGEMENT_PROPERTIES_SHOW_SHOW_THUMBNAILS_ICON_TOOLBAR_WIDGET,
			   NEMO_PREFERENCES_SHOW_SHOW_THUMBNAILS_TOOLBAR);

	/* setup preferences */
	bind_builder_bool (builder, nemo_icon_view_preferences,
			   NEMO_FILE_MANAGEMENT_PROPERTIES_LABELS_BESIDE_ICONS_WIDGET,
			   NEMO_PREFERENCES_ICON_VIEW_LABELS_BESIDE_ICONS);
	bind_builder_bool (builder, nemo_compact_view_preferences,
			   NEMO_FILE_MANAGEMENT_PROPERTIES_ALL_COLUMNS_SAME_WIDTH,
			   NEMO_PREFERENCES_COMPACT_VIEW_ALL_COLUMNS_SAME_WIDTH);
	bind_builder_bool (builder, nemo_preferences,
			   NEMO_FILE_MANAGEMENT_PROPERTIES_FOLDERS_FIRST_WIDGET,
			   NEMO_PREFERENCES_SORT_DIRECTORIES_FIRST);
	g_signal_connect (gtk_builder_get_object (builder, NEMO_FILE_MANAGEMENT_PROPERTIES_FOLDERS_FIRST_WIDGET),
                          "notify::active",
                          G_CALLBACK (set_gtk_filechooser_sort_first), NULL);
	bind_builder_bool(builder, nemo_preferences,
			    NEMO_FILE_MANAGEMENT_QUICK_RENAMES_WITH_PAUSE_IN_BETWEEN,
			    NEMO_PREFERENCES_CLICK_TO_RENAME);
	bind_builder_bool (builder, nemo_preferences,
			   NEMO_FILE_MANAGEMENT_PROPERTIES_RENAME_SELECTS_WHOLE_NAME_WIDGET,
			   NEMO_PREFERENCES_RENAME_SELECTS_WHOLE_NAME);
	bind_builder_bool_inverted (builder, nemo_preferences,
				    NEMO_FILE_MANAGEMENT_PROPERTIES_ALWAYS_USE_BROWSER_WIDGET,
				    NEMO_PREFERENCES_ALWAYS_USE_BROWSER);
	bind_builder_bool (builder, nemo_preferences,
			   NEMO_FILE_MANAGEMENT_PROPERTIES_DRAG_CONFIRM_MOVE_WIDGET,
			   NEMO_PREFERENCES_CONFIRM_DRAG_MOVE);
	bind_builder_bool (builder, nemo_preferences,
			   NEMO_FILE_MANAGEMENT_PROPERTIES_DRAG_CONFIRM_COPY_WIDGET,
			   NEMO_PREFERENCES_CONFIRM_DRAG_COPY);
	bind_builder_bool (builder, nemo_preferences,
			   NEMO_FILE_MANAGEMENT_PROPERTIES_WINDOW_PER_PROCESS_WIDGET,
			   NEMO_PREFERENCES_WINDOW_PER_PROCESS);
	bind_builder_bool (builder, nemo_preferences,
			   NEMO_FILE_MANAGEMENT_PROPERTIES_TRASH_CONFIRM_MOVE_WIDGET,
			   NEMO_PREFERENCES_CONFIRM_MOVE_TO_TRASH);
	bind_builder_bool (builder, nemo_preferences,
			   NEMO_FILE_MANAGEMENT_PROPERTIES_TRASH_CONFIRM_WIDGET,
			   NEMO_PREFERENCES_CONFIRM_TRASH);
	bind_builder_bool (builder, nemo_preferences,
			   NEMO_FILE_MANAGEMENT_PROPERTIES_TRASH_DELETE_WIDGET,
			   NEMO_PREFERENCES_ENABLE_DELETE);
	bind_builder_bool (builder, nemo_config_get_group (NEMO_DEBUG_GROUP),
			   NEMO_FILE_MANAGEMENT_PROPERTIES_TESTGUARD_WIDGET,
			   NEMO_PREFERENCES_TESTGUARD_ALL_DELETES);
	/* Built with the guard forced on, the box could say off and still ask. */
	gtk_widget_set_sensitive (GTK_WIDGET (W (NEMO_FILE_MANAGEMENT_PROPERTIES_TESTGUARD_WIDGET)),
				  !NEMO_TESTGUARD_ALL_DELETES);
    bind_builder_bool (builder, nemo_preferences,
               NEMO_FILE_MANAGEMENT_PROPERTIES_SWAP_TRASH_DELETE,
               NEMO_PREFERENCES_SWAP_TRASH_DELETE);
	bind_builder_bool (builder, nemo_preferences,
			   NEMO_FILE_MANAGEMENT_PROPERTIES_SHOW_FULL_PATH_IN_TITLE_BARS_WIDGET,
			   NEMO_PREFERENCES_SHOW_FULL_PATH_TITLES);
  bind_builder_bool (builder, nemo_preferences,
			   NEMO_FILE_MANAGEMENT_PROPERTIES_INHERIT_VIEW_WIDGET,
			   NEMO_PREFERENCES_INHERIT_VIEW_SETTINGS);
  bind_builder_bool (builder, nemo_preferences,
			   NEMO_FILE_MANAGEMENT_PROPERTIES_REMEMBER_FOLDER_SETTINGS_WIDGET,
			   NEMO_PREFERENCES_REMEMBER_FOLDER_SETTINGS);
  bind_builder_bool (builder, nemo_preferences,
               NEMO_FILE_MANAGEMENT_PROPERTIES_REVERSE_SORT_WIDGET,
               NEMO_PREFERENCES_DEFAULT_SORT_IN_REVERSE_ORDER);
  bind_builder_bool (builder, nemo_preferences,
               NEMO_FILE_MANAGEMENT_PROPERTIES_FAVORITES_FIRST_WIDGET,
               NEMO_PREFERENCES_SORT_FAVORITES_FIRST);
	bind_builder_enum (builder, nemo_preferences,
			   NEMO_FILE_MANAGEMENT_PROPERTIES_DEFAULT_VIEW_WIDGET,
			   NEMO_PREFERENCES_DEFAULT_FOLDER_VIEWER,
			   (const char **) default_view_values);
	bind_builder_percent (builder, nemo_icon_view_preferences,
			      NEMO_FILE_MANAGEMENT_PROPERTIES_ICON_VIEW_SIZE_WIDGET,
			      NEMO_PREFERENCES_ICON_VIEW_DEFAULT_ICON_SIZE);
	bind_builder_percent (builder, nemo_icon_view_preferences,
			      NEMO_FILE_MANAGEMENT_PROPERTIES_ICON_VIEW_IMAGE_SIZE_WIDGET,
			      NEMO_PREFERENCES_ICON_VIEW_DEFAULT_IMAGE_ICON_SIZE);
	bind_builder_bool (builder, nemo_icon_view_preferences,
			   NEMO_FILE_MANAGEMENT_PROPERTIES_IMAGE_FOLDER_SWITCH_WIDGET,
			   NEMO_PREFERENCES_ICON_VIEW_IMAGE_FOLDER_SWITCH);
	bind_builder_percent (builder, nemo_compact_view_preferences,
			      NEMO_FILE_MANAGEMENT_PROPERTIES_COMPACT_VIEW_SIZE_WIDGET,
			      NEMO_PREFERENCES_COMPACT_VIEW_DEFAULT_ICON_SIZE);
	bind_builder_percent (builder, nemo_list_view_preferences,
			      NEMO_FILE_MANAGEMENT_PROPERTIES_LIST_VIEW_SIZE_WIDGET,
			      NEMO_PREFERENCES_LIST_VIEW_DEFAULT_ICON_SIZE);
	bind_builder_enum (builder, nemo_preferences,
			   NEMO_FILE_MANAGEMENT_PROPERTIES_SORT_ORDER_WIDGET,
			   NEMO_PREFERENCES_DEFAULT_SORT_ORDER,
			   (const char **) sort_order_values);
	bind_builder_enum (builder, nemo_preferences,
			   NEMO_FILE_MANAGEMENT_PROPERTIES_PREVIEW_IMAGE_WIDGET,
			   NEMO_PREFERENCES_SHOW_IMAGE_FILE_THUMBNAILS,
			   (const char **) preview_image_values);
    bind_builder_bool (builder, nemo_preferences,
               NEMO_FILE_MANAGEMENT_PROPERTIES_INHERIT_SHOW_THUMBNAILS_WIDGET,
               NEMO_PREFERENCES_INHERIT_SHOW_THUMBNAILS);
	bind_builder_enum (builder, nemo_preferences,
			   NEMO_FILE_MANAGEMENT_PROPERTIES_PREVIEW_FOLDER_WIDGET,
			   NEMO_PREFERENCES_SHOW_DIRECTORY_ITEM_COUNTS,
			   (const char **) preview_folder_values);
	bind_builder_enum (builder, nemo_preferences,
			   NEMO_FILE_MANAGEMENT_PROPERTIES_SIZE_PREFIXES_WIDGET,
			   NEMO_PREFERENCES_SIZE_PREFIXES,
			   (const char **) size_prefixes_values);
	bind_builder_enum (builder, nemo_preferences,
			   NEMO_FILE_MANAGEMENT_PROPERTIES_DATE_FORMAT_WIDGET,
			   NEMO_PREFERENCES_DATE_FORMAT,
			   (const char **) date_format_values);
	bind_builder_enum (builder, nemo_windows_preferences,
			   NEMO_FILE_MANAGEMENT_PROPERTIES_PATH_SEPARATOR_WIDGET,
			   NEMO_PREFERENCES_PATH_SEPARATOR,
			   (const char **) path_separator_values);
	bind_builder_bool (builder, nemo_windows_preferences,
			   NEMO_FILE_MANAGEMENT_PROPERTIES_ALLOW_SLASH_INPUT_WIDGET,
			   NEMO_PREFERENCES_ALLOW_SLASH_INPUT);
	bind_builder_bool (builder, nemo_preferences,
			   NEMO_FILE_MANAGEMENT_PROPERTIES_SHOW_SHORTCUT_EXTENSION_WIDGET,
			   NEMO_PREFERENCES_SHOW_SHORTCUT_EXTENSION);
	bind_builder_bool (builder, nemo_windows_preferences,
			   NEMO_FILE_MANAGEMENT_PROPERTIES_USE_WINDOWS_SEARCH_WIDGET,
			   NEMO_PREFERENCES_USE_SEARCH_INDEX);
	bind_builder_bool (builder, nemo_preferences,
			   NEMO_FILE_MANAGEMENT_PROPERTIES_SHOW_HIDDEN_FILES_WIDGET,
			   NEMO_PREFERENCES_SHOW_HIDDEN_FILES);
	bind_builder_bool (builder, nemo_windows_preferences,
			   NEMO_FILE_MANAGEMENT_PROPERTIES_SHOW_DOT_FILES_WIDGET,
			   NEMO_PREFERENCES_SHOW_DOT_FILES);
	bind_builder_radio (builder, nemo_preferences,
			    (const char **) click_behavior_components,
			    NEMO_PREFERENCES_CLICK_POLICY,
			    (const char **) click_behavior_values);
	bind_builder_radio (builder, nemo_preferences,
			    (const char **) executable_text_components,
			    NEMO_PREFERENCES_EXECUTABLE_TEXT_ACTIVATION,
			    (const char **) executable_text_values);

	bind_builder_uint_enum (builder, nemo_preferences,
				NEMO_FILE_MANAGEMENT_PROPERTIES_THUMBNAIL_LIMIT_WIDGET,
				NEMO_PREFERENCES_IMAGE_FILE_THUMBNAIL_LIMIT,
				thumbnail_limit_values,
				G_N_ELEMENTS (thumbnail_limit_values));

	nemo_prefs_file_cache_setup (builder);

    bind_builder_bool (builder, nemo_media_handling_preferences,
               NEMO_FILE_MANAGEMENT_PROPERTIES_AUTOMOUNT_MEDIA_WIDGET,
               GNOME_DESKTOP_MEDIA_HANDLING_AUTOMOUNT);

    bind_builder_bool (builder, nemo_media_handling_preferences,
               NEMO_FILE_MANAGEMENT_PROPERTIES_AUTOOPEN_MEDIA_WIDGET,
               GNOME_DESKTOP_MEDIA_HANDLING_AUTOMOUNT_OPEN);

    bind_builder_bool (builder, nemo_preferences,
               NEMO_FILE_MANAGEMENT_PROPERTIES_DETECT_CONTENT_MEDIA_WIDGET,
               NEMO_PREFERENCES_MEDIA_HANDLING_DETECT_CONTENT);

    bind_builder_bool (builder, nemo_preferences,
                       NEMO_FILE_MANAGEMENT_PROPERTIES_CLOSE_DEVICE_VIEW_ON_EJECT_WIDGET,
                       NEMO_PREFERENCES_CLOSE_DEVICE_VIEW_ON_EJECT);

    bind_builder_bool (builder, nemo_preferences,
                       NEMO_FILE_MANAGEMENT_PROPERTIES_SHOW_ADVANCED_PERMISSIONS_WIDGET,
                       NEMO_PREFERENCES_SHOW_ADVANCED_PERMISSIONS);

    bind_builder_string_entry (builder, nemo_preferences,
                         NEMO_FILE_MANAGEMENT_PROPERTIES_BULK_RENAME_WIDGET,
                         NEMO_PREFERENCES_BULK_RENAME_TOOL);

    bind_builder_string_entry (builder, nemo_config_get_group ("terminal"),
                         NEMO_FILE_MANAGEMENT_PROPERTIES_TERMINAL_EXEC_WIDGET,
                         "exec");

    bind_builder_bool (builder, nemo_preferences,
                       NEMO_FILE_MANAGEMENT_PROPERTIES_START_WITH_DUAL_PANE_WIDGET,
                       NEMO_PREFERENCES_START_WITH_DUAL_PANE);

    bind_builder_bool (builder, nemo_preferences,
                       NEMO_FILE_MANAGEMENT_PROPERTIES_ALWAYS_SHOW_TABS_WIDGET,
                       NEMO_PREFERENCES_ALWAYS_SHOW_TABS);

    bind_builder_bool (builder, nemo_preferences,
                       NEMO_FILE_MANAGEMENT_PROPERTIES_BOOKMARKS_IN_TO_MENUS_WIDGET,
                       NEMO_PREFERENCES_SHOW_BOOKMARKS_IN_TO_MENUS);

    bind_builder_bool (builder, nemo_preferences,
                       NEMO_FILE_MANAGEMENT_PROPERTIES_PLACES_IN_TO_MENUS_WIDGET,
                       NEMO_PREFERENCES_SHOW_PLACES_IN_TO_MENUS);

    bind_builder_bool (builder, nemo_preferences,
                       NEMO_FILE_MANAGEMENT_PROPERTIES_TOOLTIPS_ON_ICON_VIEW_WIDGET,
                       NEMO_PREFERENCES_TOOLTIPS_ICON_VIEW);

    bind_builder_bool (builder, nemo_preferences,
                       NEMO_FILE_MANAGEMENT_PROPERTIES_TOOLTIPS_ON_LIST_VIEW_WIDGET,
                       NEMO_PREFERENCES_TOOLTIPS_LIST_VIEW);

    bind_builder_bool (builder, nemo_preferences,
                       NEMO_FILE_MANAGEMENT_PROPERTIES_TOOLTIP_FILE_TYPE_WIDGET,
                       NEMO_PREFERENCES_TOOLTIP_FILE_TYPE);

    bind_builder_bool (builder, nemo_preferences,
                       NEMO_FILE_MANAGEMENT_PROPERTIES_TOOLTIP_MOD_DATE_WIDGET,
                       NEMO_PREFERENCES_TOOLTIP_MOD_DATE);
    
    bind_builder_bool (builder, nemo_preferences,
                       NEMO_FILE_MANAGEMENT_PROPERTIES_TOOLTIP_ACCESS_DATE_WIDGET,
                       NEMO_PREFERENCES_TOOLTIP_ACCESS_DATE);

    bind_builder_bool (builder, nemo_preferences,
                       NEMO_FILE_MANAGEMENT_PROPERTIES_TOOLTIP_CREATED_DATE_WIDGET,
                       NEMO_PREFERENCES_TOOLTIP_CREATED_DATE);

    bind_builder_bool (builder, nemo_preferences,
                       NEMO_FILE_MANAGEMENT_PROPERTIES_TOOLTIP_FULL_PATH_WIDGET,
                       NEMO_PREFERENCES_TOOLTIP_FULL_PATH);

    bind_builder_bool (builder, nemo_preferences,
                       NEMO_FILE_MANAGEMENT_PROPERTIES_NEMO_PREFERENCES_SKIP_FILE_OP_QUEUE_WIDGET,
                       NEMO_PREFERENCES_NEVER_QUEUE_FILE_OPS);

    bind_builder_bool (builder, nemo_preferences,
                       NEMO_FILE_MANAGEMENT_PROPERTIES_NEMO_PREFERENCES_CLICK_DBL_PARENT_FOLDER_WIDGET,
                       NEMO_PREFERENCES_CLICK_DOUBLE_PARENT_FOLDER);

    bind_builder_bool (builder, nemo_preferences,
                       NEMO_FILE_MANAGEMENT_PROPERTIES_NEMO_PREFERENCES_EXPAND_ROW_ON_DND_DWELL_WIDGET,
                       NEMO_PREFERENCES_EXPAND_ROW_ON_DND_DWELL);

    bind_builder_bool (builder, nemo_list_view_preferences,
                       NEMO_FILE_MANAGEMENT_PROPERTIES_SHOW_LIST_VIEW_EXPANDERS_WIDGET,
                       NEMO_PREFERENCES_LIST_VIEW_ENABLE_EXPANSION);

    bind_builder_bool (builder, nemo_list_view_preferences,
                       NEMO_FILE_MANAGEMENT_PROPERTIES_LIST_VIEW_ROW_SHADING_WIDGET,
                       NEMO_PREFERENCES_LIST_VIEW_ROW_SHADING);

    setup_tooltip_items (builder);
    connect_tooltip_items (builder);

    /* to make checkbox for quickrenames get disabled when single click is selected */ 
    setup_quick_renames(builder);
    connect_quick_renames(builder);

	nemo_file_management_properties_dialog_setup_appearance_page (builder);
	nemo_file_management_properties_dialog_setup_icon_caption_page (builder);
	nemo_file_management_properties_dialog_setup_list_column_page (builder);
    nemo_file_management_properties_dialog_setup_plugin_page (builder);
    nemo_file_management_properties_dialog_setup_templates_page (builder);


    setup_configurable_menu_items (builder);

    dialog = GTK_WIDGET (gtk_builder_get_object (builder, "file_management_dialog"));

	g_signal_connect (dialog, "delete-event",
			  G_CALLBACK (gtk_widget_destroy), NULL);
	g_signal_connect_after (dialog, "key-press-event",
				G_CALLBACK (dialog_key_press), NULL);

	close_button = GTK_WIDGET (gtk_builder_get_object (builder, "close_button"));
	g_signal_connect_swapped (close_button, "clicked",
				  G_CALLBACK (gtk_widget_destroy), dialog);
	gtk_widget_grab_default (close_button);

    g_signal_connect (dialog, "destroy",
                      G_CALLBACK (on_dialog_destroy), builder);

	if (window) {
		gtk_window_set_transient_for (GTK_WINDOW (dialog), window);
	}

	nemo_prefs_current_folder_setup (builder, dialog, window);

	show_scrollbars_when_needed (dialog, NULL);
	size_dialog_to_longest_page (builder, dialog, window);

	preferences_dialog = dialog;
	g_object_add_weak_pointer (G_OBJECT (dialog), (gpointer *) &preferences_dialog);

    if (initial_page != NULL) {
        GtkStack *stack;

        stack = GTK_STACK (gtk_builder_get_object (builder, "page_stack"));

        gtk_stack_set_visible_child_name (stack, initial_page);
    }

	gtk_widget_show (dialog);
}

void
nemo_file_management_properties_dialog_show (GtkWindow   *window,
                                             const gchar *initial_page)
{
	GtkBuilder *builder;

	if (preferences_dialog != NULL) {
		gtk_window_present (GTK_WINDOW (preferences_dialog));
		return;
	}

	builder = gtk_builder_new ();
    gtk_builder_set_translation_domain (builder, GETTEXT_PACKAGE);
	gtk_builder_add_from_resource (builder,
				       "/org/nemo/nemo-file-management-properties.glade",
				       NULL);

	nemo_file_management_properties_dialog_setup (builder, window, initial_page);
}
