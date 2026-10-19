/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */

/* nemo-folder-settings.c - which folder's saved view settings apply.

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
#include "nemo-folder-settings.h"

#include <stdio.h>
#include <string.h>
#include <gio/gio.h>

#include "nemo-global-preferences.h"
#include "nemo-metadata.h"
#include "nemo-file-private.h"
#include "nemo-metadata-store.h"

const char * const nemo_folder_settings_keys[] = {
	NEMO_METADATA_KEY_FOLDER_SETTINGS_SAVED,
	NEMO_METADATA_KEY_DEFAULT_VIEW,
	NEMO_METADATA_KEY_ICON_VIEW_ZOOM_LEVEL,
	NEMO_METADATA_KEY_ICON_VIEW_IMAGE_ZOOM_LEVEL,
	NEMO_METADATA_KEY_ICON_VIEW_SORT_BY,
	NEMO_METADATA_KEY_ICON_VIEW_SORT_REVERSED,
	NEMO_METADATA_KEY_COMPACT_VIEW_ZOOM_LEVEL,
	NEMO_METADATA_KEY_LIST_VIEW_ZOOM_LEVEL,
	NEMO_METADATA_KEY_LIST_VIEW_SORT_COLUMN,
	NEMO_METADATA_KEY_LIST_VIEW_SORT_REVERSED,
	NEMO_METADATA_KEY_LIST_VIEW_VISIBLE_COLUMNS,
	NEMO_METADATA_KEY_LIST_VIEW_COLUMN_ORDER,
	NEMO_METADATA_KEY_LIST_VIEW_COLUMN_WIDTHS,
	NEMO_METADATA_KEY_LIST_VIEW_ENABLE_EXPANSION,
	NEMO_METADATA_KEY_ICON_VIEW_LABELS_BESIDE_ICONS,
	NEMO_METADATA_KEY_COMPACT_VIEW_ALL_COLUMNS_SAME_WIDTH,
	NEMO_METADATA_KEY_SORT_DIRECTORIES_FIRST,
	NEMO_METADATA_KEY_SORT_FAVORITES_FIRST,
	NULL
};

/* Returns: (transfer full): free with g_free */
char *
nemo_folder_settings_source_for_uri (const char *uri)
{
	GFile *location, *parent;
	char *source = NULL;

	g_return_val_if_fail (uri != NULL, NULL);

	if (!nemo_global_preferences_get_remember_folder_settings ()) {
		return NULL;
	}

	if (nemo_metadata_store_has_any (uri, nemo_folder_settings_keys)) {
		return g_strdup (uri);
	}

	if (!nemo_global_preferences_get_inherit_view_settings ()) {
		return NULL;
	}

	location = g_file_new_for_uri (uri);
	while (source == NULL && (parent = g_file_get_parent (location)) != NULL) {
		char *parent_uri;

		g_object_unref (location);
		location = parent;

		parent_uri = g_file_get_uri (location);
		if (nemo_metadata_store_has_any (parent_uri, nemo_folder_settings_keys)) {
			source = parent_uri;
		} else {
			g_free (parent_uri);
		}
	}
	g_object_unref (location);

	return source;
}

/* Returns: (transfer full): free with g_free */
char *
nemo_folder_settings_source_uri (NemoFile *folder)
{
	char *uri, *source;

	if (folder == NULL) {
		return NULL;
	}

	uri = nemo_file_get_metadata_store_uri (folder);
	source = nemo_folder_settings_source_for_uri (uri);
	g_free (uri);

	return source;
}

gboolean
nemo_folder_settings_has_own (NemoFile *folder)
{
	char *uri;
	gboolean own;

	if (folder == NULL || !nemo_global_preferences_get_remember_folder_settings ()) {
		return FALSE;
	}

	uri = nemo_file_get_metadata_store_uri (folder);
	own = nemo_metadata_store_has_any (uri, nemo_folder_settings_keys);
	g_free (uri);

	return own;
}

/* Returns: (transfer full): free with g_free */
char *
nemo_folder_settings_get (NemoFile   *folder,
			  const char *key,
			  const char *default_value)
{
	char *source, *value = NULL;

	source = nemo_folder_settings_source_uri (folder);
	if (source != NULL) {
		value = nemo_metadata_store_get_string (source, key);
		g_free (source);
	}

	return value != NULL ? value : g_strdup (default_value);
}

/* Returns: (transfer full): free with g_list_free_full (list, g_free) */
GList *
nemo_folder_settings_get_list (NemoFile   *folder,
			       const char *key)
{
	char *source;
	char **values = NULL;
	GList *list = NULL;
	int i;

	source = nemo_folder_settings_source_uri (folder);
	if (source != NULL) {
		values = nemo_metadata_store_get_stringv (source, key);
		g_free (source);
	}

	for (i = 0; values != NULL && values[i] != NULL; i++) {
		list = g_list_prepend (list, g_strdup (values[i]));
	}
	g_strfreev (values);

	return g_list_reverse (list);
}

int
nemo_folder_settings_get_int (NemoFile   *folder,
			      const char *key,
			      int         default_value)
{
	char *value;
	int result;
	char c;

	value = nemo_folder_settings_get (folder, key, NULL);
	if (value == NULL || sscanf (value, " %d %c", &result, &c) != 1) {
		result = default_value;
	}
	g_free (value);

	return result;
}

gboolean
nemo_folder_settings_get_boolean (NemoFile   *folder,
				  const char *key,
				  gboolean    default_value)
{
	char *value;
	gboolean result = default_value;

	value = nemo_folder_settings_get (folder, key, NULL);
	if (g_strcmp0 (value, "true") == 0) {
		result = TRUE;
	} else if (g_strcmp0 (value, "false") == 0) {
		result = FALSE;
	}
	g_free (value);

	return result;
}

static void
copy_set (NemoFile *folder, const char *from_uri)
{
	int i;

	for (i = 1; nemo_folder_settings_keys[i] != NULL; i++) {
		const char *key = nemo_folder_settings_keys[i];
		char **values;
		char *value;

		values = nemo_metadata_store_get_stringv (from_uri, key);
		if (values != NULL) {
			GList *list = NULL;
			int j;

			for (j = 0; values[j] != NULL; j++) {
				list = g_list_prepend (list, values[j]);
			}
			list = g_list_reverse (list);
			nemo_file_set_metadata_list (folder, key, list);
			g_list_free (list);
			g_strfreev (values);
			continue;
		}

		value = nemo_metadata_store_get_string (from_uri, key);
		if (value != NULL) {
			nemo_file_set_metadata (folder, key, NULL, value);
			g_free (value);
		}
	}
}

void
nemo_folder_settings_adopt (NemoFile *folder)
{
	char *source;

	if (folder == NULL ||
	    !nemo_global_preferences_get_remember_folder_settings () ||
	    nemo_folder_settings_has_own (folder)) {
		return;
	}

	source = nemo_folder_settings_source_uri (folder);
	if (source != NULL) {
		copy_set (folder, source);
		g_free (source);
	}

	nemo_file_set_metadata (folder, NEMO_METADATA_KEY_FOLDER_SETTINGS_SAVED, NULL, "true");
}

/* Views write their settings back while a folder loads. That is not a change,
 * and must not give a folder that has nothing saved a set of its own. */
static gboolean
same_as_used (NemoFile *folder, const char *key, const char *default_value, const char *value)
{
	char *used;
	gboolean same;

	if (nemo_folder_settings_has_own (folder)) {
		return FALSE;
	}

	used = nemo_folder_settings_get (folder, key, default_value);
	same = g_strcmp0 (used, value != NULL ? value : default_value) == 0;
	g_free (used);

	return same;
}

static gboolean
same_list_as_used (NemoFile *folder, const char *key, GList *list)
{
	GList *used, *a, *b;
	gboolean same;

	if (nemo_folder_settings_has_own (folder)) {
		return FALSE;
	}

	used = nemo_folder_settings_get_list (folder, key);
	for (a = used, b = list; a != NULL && b != NULL; a = a->next, b = b->next) {
		if (g_strcmp0 (a->data, b->data) != 0) {
			break;
		}
	}
	same = a == NULL && b == NULL;
	g_list_free_full (used, g_free);

	return same;
}

void
nemo_folder_settings_set (NemoFile   *folder,
			  const char *key,
			  const char *default_value,
			  const char *value)
{
	if (folder == NULL || !nemo_global_preferences_get_remember_folder_settings () ||
	    same_as_used (folder, key, default_value, value)) {
		return;
	}

	nemo_folder_settings_adopt (folder);
	nemo_file_set_metadata (folder, key, default_value, value);
}

void
nemo_folder_settings_set_list (NemoFile   *folder,
			       const char *key,
			       GList      *list)
{
	if (folder == NULL || !nemo_global_preferences_get_remember_folder_settings () ||
	    same_list_as_used (folder, key, list)) {
		return;
	}

	nemo_folder_settings_adopt (folder);
	nemo_file_set_metadata_list (folder, key, list);
}

void
nemo_folder_settings_set_int (NemoFile   *folder,
			      const char *key,
			      int         default_value,
			      int         value)
{
	char default_str[32], value_str[32];

	g_snprintf (default_str, sizeof (default_str), "%d", default_value);
	g_snprintf (value_str, sizeof (value_str), "%d", value);
	nemo_folder_settings_set (folder, key, default_str, value_str);
}

void
nemo_folder_settings_set_boolean (NemoFile   *folder,
				  const char *key,
				  gboolean    default_value,
				  gboolean    value)
{
	nemo_folder_settings_set (folder, key,
				  default_value ? "true" : "false",
				  value ? "true" : "false");
}

void
nemo_folder_settings_forget (NemoFile *folder)
{
	int i;

	if (folder == NULL) {
		return;
	}

	for (i = 0; nemo_folder_settings_keys[i] != NULL; i++) {
		nemo_file_set_metadata (folder, nemo_folder_settings_keys[i], NULL, NULL);
	}
}

static const struct {
	const char *column;
	const char *icon_sort;
} sort_names[] = {
	{ "name", "name" },
	{ "size", "size" },
	{ "type", "type" },
	{ "detailed_type", "detailed_type" },
	{ "date_modified", "modification date" },
	{ "trashed_on", "trashed" },
};

/* Returns: (transfer none) (nullable): NULL where the icon view has no such sort */
const char *
nemo_folder_settings_icon_sort_for_column (const char *column)
{
	guint i;

	for (i = 0; column != NULL && i < G_N_ELEMENTS (sort_names); i++) {
		if (strcmp (sort_names[i].column, column) == 0) {
			return sort_names[i].icon_sort;
		}
	}

	return NULL;
}

/* Returns: (transfer none) (nullable) */
const char *
nemo_folder_settings_column_for_icon_sort (const char *sort_by)
{
	guint i;

	for (i = 0; sort_by != NULL && i < G_N_ELEMENTS (sort_names); i++) {
		if (strcmp (sort_names[i].icon_sort, sort_by) == 0) {
			return sort_names[i].column;
		}
	}

	return NULL;
}

/* Returns: (transfer none) */
const char *
nemo_folder_settings_default_sort_column (NemoFile *folder, gboolean *reversed)
{
	/* In the order of default-sort-order's values. A list has no manual
	   order, so that one is by name. */
	static const char * const by_setting[] = {
		"name", "name", "size", "type", "detailed_type",
		"date_modified", "date_accessed", "trashed_on",
	};
	const char *column = NULL;
	gboolean ignored;
	int setting;

	if (reversed == NULL) {
		reversed = &ignored;
	}
	if (folder != NULL) {
		column = nemo_file_get_default_sort_attribute (folder, reversed);
	}
	if (column != NULL) {
		return column;
	}

	setting = nemo_config_get_enum (nemo_preferences, NEMO_PREFERENCES_DEFAULT_SORT_ORDER);
	*reversed = nemo_config_get_boolean (nemo_preferences, NEMO_PREFERENCES_DEFAULT_SORT_IN_REVERSE_ORDER);

	return setting >= 0 && (guint) setting < G_N_ELEMENTS (by_setting) ? by_setting[setting] : "name";
}

/* Whether the sort in use is the shared one, rather than an icon view sort
   saved before the views shared one. */
static gboolean
sort_saved_as_column (NemoFile *folder)
{
	char *column, *reversed;
	gboolean saved;

	column = nemo_folder_settings_get (folder, NEMO_METADATA_KEY_LIST_VIEW_SORT_COLUMN, NULL);
	reversed = nemo_folder_settings_get (folder, NEMO_METADATA_KEY_LIST_VIEW_SORT_REVERSED, NULL);
	saved = column != NULL || reversed != NULL;
	g_free (column);
	g_free (reversed);

	return saved;
}

/* Returns: (transfer full) (nullable): NULL for the default, else free with g_free */
char *
nemo_folder_settings_get_sort_column (NemoFile *folder)
{
	char *sort_by;
	const char *column;

	if (sort_saved_as_column (folder)) {
		return nemo_folder_settings_get (folder, NEMO_METADATA_KEY_LIST_VIEW_SORT_COLUMN, NULL);
	}

	sort_by = nemo_folder_settings_get (folder, NEMO_METADATA_KEY_ICON_VIEW_SORT_BY, NULL);
	column = nemo_folder_settings_column_for_icon_sort (sort_by);
	g_free (sort_by);

	return g_strdup (column);
}

gboolean
nemo_folder_settings_get_sort_reversed (NemoFile *folder, gboolean default_value)
{
	return nemo_folder_settings_get_boolean (folder,
						 sort_saved_as_column (folder) ?
						 NEMO_METADATA_KEY_LIST_VIEW_SORT_REVERSED :
						 NEMO_METADATA_KEY_ICON_VIEW_SORT_REVERSED,
						 default_value);
}

/* The first change moves an old icon view sort to the shared keys, so it
   can't come back from under a new one that matches the defaults. */
static void
move_icon_sort (NemoFile *folder)
{
	char *sort_by, *reversed;
	const char *default_column;
	gboolean default_reversed;

	if (sort_saved_as_column (folder)) {
		return;
	}
	sort_by = nemo_folder_settings_get (folder, NEMO_METADATA_KEY_ICON_VIEW_SORT_BY, NULL);
	reversed = nemo_folder_settings_get (folder, NEMO_METADATA_KEY_ICON_VIEW_SORT_REVERSED, NULL);
	if (sort_by != NULL || reversed != NULL) {
		nemo_folder_settings_adopt (folder);
		default_column = nemo_folder_settings_default_sort_column (folder, &default_reversed);
		nemo_file_set_metadata (folder, NEMO_METADATA_KEY_LIST_VIEW_SORT_COLUMN, default_column,
					nemo_folder_settings_column_for_icon_sort (sort_by));
		nemo_file_set_metadata (folder, NEMO_METADATA_KEY_LIST_VIEW_SORT_REVERSED,
					default_reversed ? "true" : "false", reversed);
		nemo_file_set_metadata (folder, NEMO_METADATA_KEY_ICON_VIEW_SORT_BY, NULL, NULL);
		nemo_file_set_metadata (folder, NEMO_METADATA_KEY_ICON_VIEW_SORT_REVERSED, NULL, NULL);
	}
	g_free (sort_by);
	g_free (reversed);
}

void
nemo_folder_settings_set_sort_column (NemoFile *folder, const char *column)
{
	const char *default_column;
	char *used;
	gboolean same;

	if (folder == NULL || !nemo_global_preferences_get_remember_folder_settings ()) {
		return;
	}

	default_column = nemo_folder_settings_default_sort_column (folder, NULL);
	used = nemo_folder_settings_get_sort_column (folder);
	same = g_strcmp0 (used != NULL ? used : default_column,
			  column != NULL ? column : default_column) == 0;
	g_free (used);
	if (same) {
		return;
	}

	move_icon_sort (folder);
	nemo_folder_settings_set (folder, NEMO_METADATA_KEY_LIST_VIEW_SORT_COLUMN, default_column, column);
}

void
nemo_folder_settings_set_sort_reversed (NemoFile *folder, gboolean reversed)
{
	gboolean default_reversed;

	if (folder == NULL || !nemo_global_preferences_get_remember_folder_settings ()) {
		return;
	}

	nemo_folder_settings_default_sort_column (folder, &default_reversed);
	if (nemo_folder_settings_get_sort_reversed (folder, default_reversed) == reversed) {
		return;
	}

	move_icon_sort (folder);
	nemo_folder_settings_set_boolean (folder, NEMO_METADATA_KEY_LIST_VIEW_SORT_REVERSED,
					  default_reversed, reversed);
}

void
nemo_folder_settings_forget_sort (NemoFile *folder)
{
	if (folder == NULL || !nemo_folder_settings_has_own (folder)) {
		return;
	}

	nemo_file_set_metadata (folder, NEMO_METADATA_KEY_LIST_VIEW_SORT_COLUMN, NULL, NULL);
	nemo_file_set_metadata (folder, NEMO_METADATA_KEY_LIST_VIEW_SORT_REVERSED, NULL, NULL);
	nemo_file_set_metadata (folder, NEMO_METADATA_KEY_ICON_VIEW_SORT_BY, NULL, NULL);
	nemo_file_set_metadata (folder, NEMO_METADATA_KEY_ICON_VIEW_SORT_REVERSED, NULL, NULL);
}
