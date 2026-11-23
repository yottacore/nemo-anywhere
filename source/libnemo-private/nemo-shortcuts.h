/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */

/* nemo-shortcuts.h - the menu shortcuts, with the key each one starts with.

   Copyright © 2026 t00mietum (CryptogID: ปʬϝღถɔ4რఠΔթะ9ƾǝu).

   This program is free software; you can redistribute it and/or
   modify it under the terms of the GNU General Public License as
   published by the Free Software Foundation; version 2 of the
   License.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, see <http://www.gnu.org/licenses/>.
*/

#ifndef NEMO_SHORTCUTS_H
#define NEMO_SHORTCUTS_H

#include <gtk/gtk.h>

typedef struct {
	char            *path;
	char            *label;
	char            *tooltip;
	guint            key;
	GdkModifierType  mods;
} NemoShortcut;

typedef enum {
	NEMO_SHORTCUT_SET,
	NEMO_SHORTCUT_TAKEN,
	NEMO_SHORTCUT_NEEDS_MODIFIER,
	NEMO_SHORTCUT_REFUSED,
} NemoShortcutResult;

/* Called right after the same entries go into the group. GTK keeps an
   action's first key to itself, so this is the only place it's known. */
void                nemo_shortcuts_note_actions        (GtkActionGroup             *group,
							const GtkActionEntry       *entries,
							guint                       n_entries);
void                nemo_shortcuts_note_toggle_actions (GtkActionGroup             *group,
							const GtkToggleActionEntry *entries,
							guint                       n_entries);
void                nemo_shortcuts_note_radio_actions  (GtkActionGroup             *group,
							const GtkRadioActionEntry  *entries,
							guint                       n_entries);

/* Sorted by label. Free the list only. */
GList              *nemo_shortcuts_list                (void);
const NemoShortcut *nemo_shortcuts_lookup              (const char                 *path);
void                nemo_shortcuts_get_current         (const NemoShortcut         *shortcut,
							guint                      *key,
							GdkModifierType            *mods);
gboolean            nemo_shortcuts_is_default          (const NemoShortcut         *shortcut);

/* A key another listed shortcut has is refused, and *taken_by says which. */
NemoShortcutResult  nemo_shortcuts_set                 (const NemoShortcut         *shortcut,
							guint                       key,
							GdkModifierType             mods,
							const NemoShortcut        **taken_by);
NemoShortcutResult  nemo_shortcuts_reset               (const NemoShortcut         *shortcut,
							const NemoShortcut        **taken_by);
void                nemo_shortcuts_reset_all           (void);

#endif /* NEMO_SHORTCUTS_H */
