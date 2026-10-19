/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */

/* nemo-folder-settings.h - which folder's saved view settings apply.

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

/* View settings are saved per folder as one set. A folder with no set of
 * its own takes the set of the nearest parent that has one, when
 * inheriting is on, and otherwise the defaults. Nothing here is read or
 * written while "Remember per-folder settings" is off.
 *
 * Changing one value in a folder with no set of its own first copies the
 * set it was using, so the other values do not jump back to the defaults.
 */

#ifndef NEMO_FOLDER_SETTINGS_H
#define NEMO_FOLDER_SETTINGS_H

#include <glib.h>
#include "nemo-file.h"

/* NULL-terminated; the metadata keys that make up one folder's set */
extern const char * const nemo_folder_settings_keys[];

/* store uri of the folder whose set applies, or NULL for the defaults */
char     *nemo_folder_settings_source_for_uri (const char *uri);
char     *nemo_folder_settings_source_uri     (NemoFile   *folder);

gboolean  nemo_folder_settings_has_own        (NemoFile   *folder);

char     *nemo_folder_settings_get            (NemoFile   *folder,
					       const char *key,
					       const char *default_value);
GList    *nemo_folder_settings_get_list       (NemoFile   *folder,
					       const char *key);
int       nemo_folder_settings_get_int        (NemoFile   *folder,
					       const char *key,
					       int         default_value);
gboolean  nemo_folder_settings_get_boolean    (NemoFile   *folder,
					       const char *key,
					       gboolean    default_value);

void      nemo_folder_settings_set            (NemoFile   *folder,
					       const char *key,
					       const char *default_value,
					       const char *value);
void      nemo_folder_settings_set_list       (NemoFile   *folder,
					       const char *key,
					       GList      *list);
void      nemo_folder_settings_set_int        (NemoFile   *folder,
					       const char *key,
					       int         default_value,
					       int         value);
void      nemo_folder_settings_set_boolean    (NemoFile   *folder,
					       const char *key,
					       gboolean    default_value,
					       gboolean    value);

/* copy what the folder uses now into a set of its own */
void      nemo_folder_settings_adopt          (NemoFile   *folder);

/* drop the folder's own set */
void      nemo_folder_settings_forget         (NemoFile   *folder);

/* A folder has one sort for every view, kept as a list column and its
 * direction. The icon view sorts by fewer things, under names of its own. An
 * icon view sort saved before this still counts where there is no column. */
const char *nemo_folder_settings_icon_sort_for_column (const char *column);
const char *nemo_folder_settings_column_for_icon_sort (const char *sort_by);

/* the column a folder sorts by with nothing saved, and which way */
const char *nemo_folder_settings_default_sort_column (NemoFile *folder,
						      gboolean *reversed);

/* NULL when the folder has none saved; free with g_free */
char     *nemo_folder_settings_get_sort_column      (NemoFile   *folder);
gboolean  nemo_folder_settings_get_sort_reversed    (NemoFile   *folder,
						     gboolean    default_value);
void      nemo_folder_settings_set_sort_column      (NemoFile   *folder,
						     const char *column);
void      nemo_folder_settings_set_sort_reversed    (NemoFile   *folder,
						     gboolean    reversed);
/* back to the default sort, whatever the views saved it as */
void      nemo_folder_settings_forget_sort          (NemoFile   *folder);

#endif /* NEMO_FOLDER_SETTINGS_H */
