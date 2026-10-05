/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */

/* nemo-prefs-current-folder.c - the Default and Current tabs on Views.

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
#include "nemo-prefs-current-folder.h"

#include <string.h>
#include <glib/gi18n.h>

#include <libnemo-private/nemo-file.h>
#include <libnemo-private/nemo-folder-settings.h>
#include <libnemo-private/nemo-global-preferences.h>
#include <libnemo-private/nemo-metadata.h>

#include "nemo-window.h"
#include "nemo-window-slot.h"

enum {
	FIELD_VIEW,
	FIELD_ARRANGE,
	FIELD_REVERSE,
	FIELD_FOLDERS_FIRST,
	FIELD_FAVORITES_FIRST,
	FIELD_ICON_ZOOM,
	FIELD_ICON_IMAGE_ZOOM,
	FIELD_LABELS_BESIDE,
	FIELD_COMPACT_ZOOM,
	FIELD_SAME_WIDTH,
	FIELD_LIST_ZOOM,
	FIELD_EXPANDERS,
	N_FIELDS
};

#define ALL_FIELDS ((1u << N_FIELDS) - 1)

/* Default tab ids; the Current tab's copies carry "_current". The order
 * matches the enum above. */
static const char * const field_widgets[N_FIELDS] = {
	"default_view_combobox",
	"sort_order_combobox",
	"reverse_sort_checkbox",
	"sort_folders_first_checkbutton",
	"sort_favorites_first_checkbutton",
	"icon_view_size_spinbutton",
	"icon_view_image_size_spinbutton",
	"labels_beside_icons_checkbutton",
	"compact_view_size_spinbutton",
	"all_columns_same_width_checkbutton",
	"list_view_size_spinbutton",
	"list_view_show_expanders_checkbutton",
};

/* Same order as the view combo's rows. */
static const char * const view_iids[] = {
	NEMO_ICON_VIEW_IID,
	NEMO_LIST_VIEW_IID,
	NEMO_COMPACT_VIEW_IID,
};

/* One row of "Arrange items" in each of the forms it is stored in. Icon view
 * has no sort by access time. */
static const struct {
	const char *preference;
	const char *icon_view;
	const char *list_view;
} arrange_rows[] = {
	{ "name", "name", "name" },
	{ "size", "size", "size" },
	{ "type", "type", "type" },
	{ "detailed_type", "detailed_type", "detailed_type" },
	{ "mtime", "modification date", "date_modified" },
	{ "atime", NULL, "date_accessed" },
	{ "trash-time", "trashed", "trashed_on" },
};

typedef struct {
	GtkWidget      *dialog;
	GtkWidget      *notebook;
	GtkWidget      *current_page;
	GtkWidget      *current_tab_label;
	GtkWidget      *path_label;
	GtkWidget      *copy_to_current_button;
	GtkWidget      *copy_to_default_button;
	GtkWidget      *forget_button;
	GtkWidget      *defaults[N_FIELDS];
	GtkWidget      *current[N_FIELDS];
	GtkApplication *app;
	gulong          active_window_id;
	NemoWindow     *window;
	gulong          loading_id;
	NemoFile       *folder;
	gboolean        syncing;
} CurrentTab;

static gboolean
remembering (void)
{
	return nemo_global_preferences_get_remember_folder_settings ();
}

static int
view_index (const char *iid, int fallback)
{
	guint i;

	for (i = 0; iid != NULL && i < G_N_ELEMENTS (view_iids); i++) {
		if (strcmp (view_iids[i], iid) == 0) {
			return i;
		}
	}

	return fallback;
}

static int
default_view_index (void)
{
	char *iid;
	int index;

	iid = nemo_global_preferences_get_default_folder_viewer_preference_as_iid ();
	index = view_index (iid, 0);
	g_free (iid);

	return index;
}

static int
default_arrange_index (void)
{
	char *name;
	guint i;
	int index = 0;

	name = nemo_config_get_string (nemo_preferences, NEMO_PREFERENCES_DEFAULT_SORT_ORDER);
	for (i = 0; name != NULL && i < G_N_ELEMENTS (arrange_rows); i++) {
		if (strcmp (arrange_rows[i].preference, name) == 0) {
			index = i;
		}
	}
	g_free (name);

	return index;
}

static const char *
field_key (int field)
{
	switch (field) {
	case FIELD_FOLDERS_FIRST: return NEMO_METADATA_KEY_SORT_DIRECTORIES_FIRST;
	case FIELD_FAVORITES_FIRST: return NEMO_METADATA_KEY_SORT_FAVORITES_FIRST;
	case FIELD_ICON_ZOOM: return NEMO_METADATA_KEY_ICON_VIEW_ZOOM_LEVEL;
	case FIELD_ICON_IMAGE_ZOOM: return NEMO_METADATA_KEY_ICON_VIEW_IMAGE_ZOOM_LEVEL;
	case FIELD_LABELS_BESIDE: return NEMO_METADATA_KEY_ICON_VIEW_LABELS_BESIDE_ICONS;
	case FIELD_COMPACT_ZOOM: return NEMO_METADATA_KEY_COMPACT_VIEW_ZOOM_LEVEL;
	case FIELD_SAME_WIDTH: return NEMO_METADATA_KEY_COMPACT_VIEW_ALL_COLUMNS_SAME_WIDTH;
	case FIELD_LIST_ZOOM: return NEMO_METADATA_KEY_LIST_VIEW_ZOOM_LEVEL;
	case FIELD_EXPANDERS: return NEMO_METADATA_KEY_LIST_VIEW_ENABLE_EXPANSION;
	default: return NULL;
	}
}

/* the preference a field falls back to, as a spin box value, a combo row or a
   check state */
/* A size is stored as pixels and shown as a per cent of the standard size. */
static int
size_percent (gint size)
{
	if (nemo_icon_size_is_legacy_level (size)) {
		size = nemo_icon_size_from_legacy_level (size);	/* saved before sizes were pixels */
	}

	return nemo_icon_size_percent (size);
}

static int
field_default (int field)
{
	switch (field) {
	case FIELD_VIEW: return default_view_index ();
	case FIELD_ARRANGE: return default_arrange_index ();
	case FIELD_REVERSE: return nemo_config_get_boolean (nemo_preferences, NEMO_PREFERENCES_DEFAULT_SORT_IN_REVERSE_ORDER);
	case FIELD_FOLDERS_FIRST: return nemo_config_get_boolean (nemo_preferences, NEMO_PREFERENCES_SORT_DIRECTORIES_FIRST);
	case FIELD_FAVORITES_FIRST: return nemo_config_get_boolean (nemo_preferences, NEMO_PREFERENCES_SORT_FAVORITES_FIRST);
	case FIELD_ICON_ZOOM: return nemo_config_get_int (nemo_icon_view_preferences, NEMO_PREFERENCES_ICON_VIEW_DEFAULT_ICON_SIZE);
	case FIELD_ICON_IMAGE_ZOOM: return nemo_config_get_int (nemo_icon_view_preferences, NEMO_PREFERENCES_ICON_VIEW_DEFAULT_IMAGE_ICON_SIZE);
	case FIELD_LABELS_BESIDE: return nemo_config_get_boolean (nemo_icon_view_preferences, NEMO_PREFERENCES_ICON_VIEW_LABELS_BESIDE_ICONS);
	case FIELD_COMPACT_ZOOM: return nemo_config_get_int (nemo_compact_view_preferences, NEMO_PREFERENCES_COMPACT_VIEW_DEFAULT_ICON_SIZE);
	case FIELD_SAME_WIDTH: return nemo_config_get_boolean (nemo_compact_view_preferences, NEMO_PREFERENCES_COMPACT_VIEW_ALL_COLUMNS_SAME_WIDTH);
	case FIELD_LIST_ZOOM: return nemo_config_get_int (nemo_list_view_preferences, NEMO_PREFERENCES_LIST_VIEW_DEFAULT_ICON_SIZE);
	case FIELD_EXPANDERS: return nemo_config_get_boolean (nemo_list_view_preferences, NEMO_PREFERENCES_LIST_VIEW_ENABLE_EXPANSION);
	default: return 0;
	}
}

static int
widget_value (GtkWidget *widget)
{
	if (GTK_IS_SPIN_BUTTON (widget)) {
		return gtk_spin_button_get_value_as_int (GTK_SPIN_BUTTON (widget));
	}

	if (GTK_IS_COMBO_BOX (widget)) {
		return gtk_combo_box_get_active (GTK_COMBO_BOX (widget));
	}

	return gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (widget));
}

static const char *
change_signal (GtkWidget *widget)
{
	if (GTK_IS_SPIN_BUTTON (widget)) {
		return "value-changed";
	}

	return GTK_IS_COMBO_BOX (widget) ? "changed" : "toggled";
}

static void
set_widget_value (GtkWidget *widget, int value)
{
	if (GTK_IS_SPIN_BUTTON (widget)) {
		gtk_spin_button_set_value (GTK_SPIN_BUTTON (widget), (gdouble) value);
	} else if (GTK_IS_COMBO_BOX (widget)) {
		gtk_combo_box_set_active (GTK_COMBO_BOX (widget), value);
	} else {
		gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (widget), value);
	}
}

/* "Arrange items" and "Reverse sort" are stored once per view type, so show
 * the pair the folder's own view type uses. */
static gboolean
folder_uses_list_view (CurrentTab *tab)
{
	return widget_value (tab->current[FIELD_VIEW]) == view_index (NEMO_LIST_VIEW_IID, -1);
}

static int
folder_value (CurrentTab *tab, int field)
{
	int fallback = field_default (field);
	char *value;
	int result;
	guint i;

	switch (field) {
	case FIELD_VIEW:
		value = nemo_folder_settings_get (tab->folder, NEMO_METADATA_KEY_DEFAULT_VIEW, NULL);
		result = view_index (value, fallback);
		g_free (value);
		return result;

	case FIELD_ARRANGE:
		result = fallback;
		if (folder_uses_list_view (tab)) {
			value = nemo_folder_settings_get (tab->folder, NEMO_METADATA_KEY_LIST_VIEW_SORT_COLUMN, NULL);
			for (i = 0; value != NULL && i < G_N_ELEMENTS (arrange_rows); i++) {
				if (g_strcmp0 (arrange_rows[i].list_view, value) == 0) {
					result = i;
				}
			}
		} else {
			value = nemo_folder_settings_get (tab->folder, NEMO_METADATA_KEY_ICON_VIEW_SORT_BY, NULL);
			for (i = 0; value != NULL && i < G_N_ELEMENTS (arrange_rows); i++) {
				if (g_strcmp0 (arrange_rows[i].icon_view, value) == 0) {
					result = i;
				}
			}
		}
		g_free (value);
		return result;

	case FIELD_REVERSE:
		return nemo_folder_settings_get_boolean (tab->folder,
							 folder_uses_list_view (tab) ? NEMO_METADATA_KEY_LIST_VIEW_SORT_REVERSED
										     : NEMO_METADATA_KEY_ICON_VIEW_SORT_REVERSED,
							 fallback);

	case FIELD_ICON_ZOOM:
	case FIELD_ICON_IMAGE_ZOOM:
	case FIELD_COMPACT_ZOOM:
	case FIELD_LIST_ZOOM:
		return size_percent (nemo_folder_settings_get_int (tab->folder, field_key (field),
								   nemo_icon_size_from_percent (fallback)));

	default:
		return nemo_folder_settings_get_boolean (tab->folder, field_key (field), fallback);
	}
}

static void
update_buttons (CurrentTab *tab)
{
	gboolean usable = remembering () && tab->folder != NULL;

	gtk_widget_set_sensitive (tab->copy_to_current_button, usable);
	gtk_widget_set_sensitive (tab->copy_to_default_button, usable);
	gtk_widget_set_sensitive (tab->forget_button, usable && nemo_folder_settings_has_own (tab->folder));
}

static void
refresh (CurrentTab *tab)
{
	int field;

	if (tab->folder != NULL) {
		GFile *location = nemo_file_get_location (tab->folder);
		char *shown = g_file_get_parse_name (location);

		gtk_label_set_text (GTK_LABEL (tab->path_label), shown);
		g_free (shown);
		g_object_unref (location);
	} else {
		gtk_label_set_text (GTK_LABEL (tab->path_label), "");
	}

	if (!remembering ()) {
		gtk_notebook_set_current_page (GTK_NOTEBOOK (tab->notebook), 0);
	}
	gtk_widget_set_sensitive (tab->current_tab_label, remembering ());
	gtk_widget_set_sensitive (tab->current_page, remembering () && tab->folder != NULL);

	tab->syncing = TRUE;
	/* the view comes first, since the sort fields read it back */
	for (field = 0; field < N_FIELDS; field++) {
		set_widget_value (tab->current[field], folder_value (tab, field));
	}
	tab->syncing = FALSE;

	update_buttons (tab);
}

/* Shows a change right away in the window it was made for. */
static void
apply_to_window (CurrentTab *tab)
{
	NemoWindowSlot *slot;
	GFile *shown, *location;

	if (tab->window == NULL || tab->folder == NULL) {
		return;
	}

	slot = nemo_window_get_active_slot (tab->window);
	if (slot == NULL || (shown = nemo_window_slot_get_location (slot)) == NULL) {
		return;
	}

	location = nemo_file_get_location (tab->folder);
	if (g_file_equal (shown, location)) {
		nemo_window_slot_force_reload (slot);
	}
	g_object_unref (location);
	g_object_unref (shown);
}

static void
save_fields (CurrentTab *tab, guint fields)
{
	int field;

	if (tab->folder == NULL || !remembering ()) {
		return;
	}

	nemo_folder_settings_adopt (tab->folder);

	for (field = 0; field < N_FIELDS; field++) {
		int value, fallback;

		if ((fields & (1u << field)) == 0) {
			continue;
		}

		value = widget_value (tab->current[field]);
		fallback = field_default (field);
		if (value < 0) {
			continue;
		}

		switch (field) {
		case FIELD_VIEW:
			nemo_folder_settings_set (tab->folder, NEMO_METADATA_KEY_DEFAULT_VIEW,
						  view_iids[fallback], view_iids[value]);
			break;

		case FIELD_ARRANGE:
			if (arrange_rows[value].icon_view != NULL) {
				nemo_folder_settings_set (tab->folder, NEMO_METADATA_KEY_ICON_VIEW_SORT_BY,
							  arrange_rows[fallback].icon_view,
							  arrange_rows[value].icon_view);
			}
			nemo_folder_settings_set (tab->folder, NEMO_METADATA_KEY_LIST_VIEW_SORT_COLUMN,
						  arrange_rows[fallback].list_view,
						  arrange_rows[value].list_view);
			break;

		case FIELD_REVERSE:
			nemo_folder_settings_set_boolean (tab->folder, NEMO_METADATA_KEY_ICON_VIEW_SORT_REVERSED,
							  fallback, value);
			nemo_folder_settings_set_boolean (tab->folder, NEMO_METADATA_KEY_LIST_VIEW_SORT_REVERSED,
							  fallback, value);
			break;

		case FIELD_ICON_ZOOM:
		case FIELD_ICON_IMAGE_ZOOM:
		case FIELD_COMPACT_ZOOM:
		case FIELD_LIST_ZOOM:
			nemo_folder_settings_set_int (tab->folder, field_key (field),
						      nemo_icon_size_from_percent (fallback),
						      nemo_icon_size_from_percent (value));
			break;

		default:
			nemo_folder_settings_set_boolean (tab->folder, field_key (field), fallback, value);
			break;
		}
	}

	apply_to_window (tab);
	update_buttons (tab);
}

static void
current_field_changed (GtkWidget *widget, CurrentTab *tab)
{
	int field;

	if (tab->syncing) {
		return;
	}

	for (field = 0; field < N_FIELDS; field++) {
		if (tab->current[field] == widget) {
			save_fields (tab, 1u << field);
		}
	}

	/* the sort pair on show depends on the view type */
	if (widget == tab->current[FIELD_VIEW]) {
		refresh (tab);
	}
}

/* the Default widgets are bound to the preferences, so writing them is enough */
static void
copy_to_default_clicked (G_GNUC_UNUSED GtkButton *button, CurrentTab *tab)
{
	int field;

	for (field = 0; field < N_FIELDS; field++) {
		set_widget_value (tab->defaults[field], widget_value (tab->current[field]));
	}
}

static void
copy_to_current_clicked (G_GNUC_UNUSED GtkButton *button, CurrentTab *tab)
{
	int field;

	tab->syncing = TRUE;
	for (field = 0; field < N_FIELDS; field++) {
		set_widget_value (tab->current[field], widget_value (tab->defaults[field]));
	}
	tab->syncing = FALSE;

	save_fields (tab, ALL_FIELDS);
	refresh (tab);
}

static void
forget_clicked (G_GNUC_UNUSED GtkButton *button, CurrentTab *tab)
{
	nemo_folder_settings_forget (tab->folder);
	apply_to_window (tab);
	refresh (tab);
}

static void
set_folder_uri (CurrentTab *tab, const char *uri)
{
	g_clear_pointer (&tab->folder, nemo_file_unref);
	if (uri != NULL) {
		tab->folder = nemo_file_get_by_uri (uri);
	}
	refresh (tab);
}

static void
loading_uri (G_GNUC_UNUSED NemoWindow *window, const char *uri, CurrentTab *tab)
{
	set_folder_uri (tab, uri);
}

static void
follow_window (CurrentTab *tab, NemoWindow *window)
{
	NemoWindowSlot *slot;
	char *uri = NULL;

	if (window == tab->window) {
		return;
	}

	if (tab->window != NULL) {
		g_signal_handler_disconnect (tab->window, tab->loading_id);
		g_object_remove_weak_pointer (G_OBJECT (tab->window), (gpointer *) &tab->window);
	}

	tab->window = window;
	tab->loading_id = 0;
	if (window == NULL) {
		return;
	}

	g_object_add_weak_pointer (G_OBJECT (window), (gpointer *) &tab->window);
	tab->loading_id = g_signal_connect (window, "loading_uri", G_CALLBACK (loading_uri), tab);

	slot = nemo_window_get_active_slot (window);
	if (slot != NULL) {
		uri = nemo_window_slot_get_location_uri (slot);
	}
	set_folder_uri (tab, uri);
	g_free (uri);
}

static void
active_window_changed (GtkApplication *app, G_GNUC_UNUSED GParamSpec *pspec, CurrentTab *tab)
{
	GtkWindow *window = gtk_application_get_active_window (app);

	if (NEMO_IS_WINDOW (window)) {
		follow_window (tab, NEMO_WINDOW (window));
	}
}

/* An insensitive page still switches on a click, so the tab has to refuse it here. */
static void
page_switching (GtkNotebook *notebook, GtkWidget *page, G_GNUC_UNUSED guint page_num, CurrentTab *tab)
{
	if (page == tab->current_page && !remembering ()) {
		g_signal_stop_emission_by_name (notebook, "switch-page");
	}
}

/* connected after, so the new page is already the current one */
static void
page_switched_after (G_GNUC_UNUSED GtkNotebook *notebook, G_GNUC_UNUSED GtkWidget *page, G_GNUC_UNUSED guint page_num, CurrentTab *tab)
{
	refresh (tab);
}

/* The folder's settings can change behind the dialog, from the view itself. */
static void
dialog_activated (GtkWindow *dialog, G_GNUC_UNUSED GParamSpec *pspec, CurrentTab *tab)
{
	if (gtk_window_is_active (dialog)) {
		refresh (tab);
	}
}

static void
preference_changed (CurrentTab *tab)
{
	refresh (tab);
}

static void
dialog_destroyed (G_GNUC_UNUSED GtkWidget *dialog, CurrentTab *tab)
{
	follow_window (tab, NULL);
	if (tab->app != NULL) {
		g_signal_handler_disconnect (tab->app, tab->active_window_id);
		g_object_unref (tab->app);
	}
	g_signal_handlers_disconnect_by_data (nemo_preferences, tab);
	g_clear_pointer (&tab->folder, nemo_file_unref);
	g_free (tab);
}

void
nemo_prefs_current_folder_setup (GtkBuilder *builder,
				 GtkWidget  *dialog,
				 GtkWindow  *parent)
{
	CurrentTab *tab;
	int field;

	tab = g_new0 (CurrentTab, 1);
	tab->dialog = dialog;
	tab->notebook = GTK_WIDGET (gtk_builder_get_object (builder, "views_notebook"));
	tab->current_page = GTK_WIDGET (gtk_builder_get_object (builder, "views_current_page"));
	tab->current_tab_label = GTK_WIDGET (gtk_builder_get_object (builder, "views_current_tab"));
	tab->path_label = GTK_WIDGET (gtk_builder_get_object (builder, "current_folder_path_label"));
	tab->copy_to_current_button = GTK_WIDGET (gtk_builder_get_object (builder, "copy_to_current_button"));
	tab->copy_to_default_button = GTK_WIDGET (gtk_builder_get_object (builder, "copy_to_default_button"));
	tab->forget_button = GTK_WIDGET (gtk_builder_get_object (builder, "forget_folder_settings_button"));

	for (field = 0; field < N_FIELDS; field++) {
		char *name = g_strconcat (field_widgets[field], "_current", NULL);

		tab->defaults[field] = GTK_WIDGET (gtk_builder_get_object (builder, field_widgets[field]));
		tab->current[field] = GTK_WIDGET (gtk_builder_get_object (builder, name));
		g_free (name);

		g_signal_connect (tab->current[field], change_signal (tab->current[field]),
				  G_CALLBACK (current_field_changed), tab);
	}

	g_object_bind_property (gtk_builder_get_object (builder, "remember_folder_settings_checkbutton"), "active",
				gtk_builder_get_object (builder, "inherit_view_alignment"), "sensitive",
				G_BINDING_SYNC_CREATE);

	g_signal_connect (tab->copy_to_current_button, "clicked", G_CALLBACK (copy_to_current_clicked), tab);
	g_signal_connect (tab->copy_to_default_button, "clicked", G_CALLBACK (copy_to_default_clicked), tab);
	g_signal_connect (tab->forget_button, "clicked", G_CALLBACK (forget_clicked), tab);
	g_signal_connect (tab->notebook, "switch-page", G_CALLBACK (page_switching), tab);
	g_signal_connect_after (tab->notebook, "switch-page", G_CALLBACK (page_switched_after), tab);
	g_signal_connect (dialog, "notify::is-active", G_CALLBACK (dialog_activated), tab);
	g_signal_connect (dialog, "destroy", G_CALLBACK (dialog_destroyed), tab);

	g_signal_connect_swapped (nemo_preferences, "changed::" NEMO_PREFERENCES_REMEMBER_FOLDER_SETTINGS,
				  G_CALLBACK (preference_changed), tab);
	g_signal_connect_swapped (nemo_preferences, "changed::" NEMO_PREFERENCES_INHERIT_VIEW_SETTINGS,
				  G_CALLBACK (preference_changed), tab);

	if (parent != NULL && gtk_window_get_application (parent) != NULL) {
		tab->app = g_object_ref (gtk_window_get_application (parent));
		tab->active_window_id = g_signal_connect (tab->app, "notify::active-window",
							  G_CALLBACK (active_window_changed), tab);
	}

	follow_window (tab, NEMO_IS_WINDOW (parent) ? NEMO_WINDOW (parent) : NULL);
	refresh (tab);
}
