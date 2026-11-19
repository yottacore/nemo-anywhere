/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */

/* nemo-settings-search.h - the search field at the top of Settings.

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

#ifndef NEMO_SETTINGS_SEARCH_H
#define NEMO_SETTINGS_SEARCH_H

#include <gtk/gtk.h>

/* In the order the groups are listed. */
typedef enum {
	NEMO_SETTINGS_MATCH_NONE = 0,
	NEMO_SETTINGS_MATCH_LABEL,
	NEMO_SETTINGS_MATCH_ALIAS,
	NEMO_SETTINGS_MATCH_FUZZY,
} NemoSettingsMatchKind;

typedef struct {
	char      *label;
	char      *path;     /* The levels above the label, joined, or NULL. */
	char     **aliases;  /* NULL-terminated, or NULL. */
	gpointer   target;
} NemoSettingsEntry;

typedef struct {
	guint                  index;  /* Into the entries searched. */
	NemoSettingsMatchKind  kind;
	int                    score;  /* Lower comes first, within a kind. */
	const char            *text;   /* The label, or the alias that matched. */
	GArray                *marks;  /* guint character offsets into text. */
} NemoSettingsHit;

/* Text holding query as typed, case aside. Fills marks, if given, with the
   characters that matched. */
gboolean    nemo_settings_search_literal (const char *text,
					  const char *query,
					  int        *score,
					  GArray     *marks);

/* fzf-like: the query's letters in order, with gaps. Fewer gaps score lower. */
gboolean    nemo_settings_search_fuzzy   (const char *text,
					  const char *query,
					  int        *score,
					  GArray     *marks);

/* Every entry that matches, best first. An entry is listed once, in its best
   group. Returns: (transfer full) an array of NemoSettingsHit. */
GPtrArray  *nemo_settings_search_run     (GPtrArray  *entries,
					  const char *query);

/* text as Pango markup, with the marked characters in mark_color, or the
   text color when it's NULL. Returns: (transfer full) */
char       *nemo_settings_search_markup  (const char *text,
					  GArray     *marks,
					  const char *mark_color);

/* "a > b > c", with the arrow the design names. Returns: (transfer full) */
char       *nemo_settings_search_join    (const char *path,
					  const char *label);

/* Returns: (transfer full) */
NemoSettingsEntry *nemo_settings_entry_new  (const char *label,
					     const char *path,
					     gpointer    target);
void               nemo_settings_entry_free (NemoSettingsEntry *entry);

/* Connects the field and its match list in the Settings window. */
void        nemo_settings_search_setup   (GtkBuilder *builder);

#endif /* NEMO_SETTINGS_SEARCH_H */
