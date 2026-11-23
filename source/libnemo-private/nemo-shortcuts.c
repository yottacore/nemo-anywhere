/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */

/* nemo-shortcuts.c - the menu shortcuts, with the key each one starts with.

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

#include <config.h>

#include "nemo-shortcuts.h"

#include <string.h>

/* path -> NemoShortcut, for the life of the process. */
static GHashTable *shortcuts = NULL;

static char *
strip_mnemonic (const char *label)
{
	GString *out = g_string_new (NULL);
	const char *p;

	for (p = label; *p != '\0'; p++) {
		if (*p == '_') {
			if (p[1] == '_') {
				g_string_append_c (out, '_');
				p++;
			}
			continue;
		}
		g_string_append_c (out, *p);
	}

	return g_string_free (out, FALSE);
}

/* Works the key out the way gtk_action_group_add_action_with_accel does. */
static void
note_one (GtkActionGroup *group,
	  const char     *name,
	  const char     *stock_id,
	  const char     *accelerator,
	  gboolean        is_menu)
{
	NemoShortcut *shortcut;
	GtkAction *action;
	const char *label;
	char *path;
	guint key = 0;
	GdkModifierType mods = 0;

	action = gtk_action_group_get_action (group, name);
	if (action == NULL) {
		return;
	}

	label = gtk_action_get_label (action);
	/* A submenu, or a key with nothing to call it in a list. */
	if (is_menu || label == NULL || *label == '\0') {
		return;
	}

	if (shortcuts == NULL) {
		shortcuts = g_hash_table_new (g_str_hash, g_str_equal);
	}

	path = g_strconcat ("<Actions>/", gtk_action_group_get_name (group), "/", name, NULL);
	if (g_hash_table_contains (shortcuts, path)) {
		g_free (path);
		return;
	}

	if (accelerator != NULL) {
		if (*accelerator != '\0') {
			gtk_accelerator_parse (accelerator, &key, &mods);
		}
	} else if (stock_id != NULL) {
		GtkStockItem item;

		if (gtk_stock_lookup (stock_id, &item)) {
			key = item.keyval;
			mods = item.modifier;
		}
	}

	shortcut = g_new0 (NemoShortcut, 1);
	shortcut->path = path;
	shortcut->label = strip_mnemonic (label);
	shortcut->tooltip = g_strdup (gtk_action_get_tooltip (action));
	shortcut->key = key;
	shortcut->mods = mods;
	g_hash_table_insert (shortcuts, path, shortcut);
}

void
nemo_shortcuts_note_actions (GtkActionGroup       *group,
			     const GtkActionEntry *entries,
			     guint                 n_entries)
{
	guint i;

	for (i = 0; i < n_entries; i++) {
		note_one (group, entries[i].name, entries[i].stock_id, entries[i].accelerator,
			  entries[i].callback == NULL);
	}
}

void
nemo_shortcuts_note_toggle_actions (GtkActionGroup             *group,
				    const GtkToggleActionEntry *entries,
				    guint                       n_entries)
{
	guint i;

	for (i = 0; i < n_entries; i++) {
		note_one (group, entries[i].name, entries[i].stock_id, entries[i].accelerator, FALSE);
	}
}

void
nemo_shortcuts_note_radio_actions (GtkActionGroup            *group,
				   const GtkRadioActionEntry *entries,
				   guint                      n_entries)
{
	guint i;

	for (i = 0; i < n_entries; i++) {
		note_one (group, entries[i].name, entries[i].stock_id, entries[i].accelerator, FALSE);
	}
}

static gint
by_label (gconstpointer a, gconstpointer b)
{
	const NemoShortcut *sa = a, *sb = b;
	gint order = g_utf8_collate (sa->label, sb->label);

	return order != 0 ? order : strcmp (sa->path, sb->path);
}

/* Returns: (transfer container): sorted by label */
GList *
nemo_shortcuts_list (void)
{
	if (shortcuts == NULL) {
		return NULL;
	}

	return g_list_sort (g_hash_table_get_values (shortcuts), by_label);
}

/* Returns: (transfer none) (nullable): kept for the life of the process */
const NemoShortcut *
nemo_shortcuts_lookup (const char *path)
{
	return shortcuts != NULL ? g_hash_table_lookup (shortcuts, path) : NULL;
}

void
nemo_shortcuts_get_current (const NemoShortcut *shortcut,
			    guint              *key,
			    GdkModifierType    *mods)
{
	GtkAccelKey current;

	if (gtk_accel_map_lookup_entry (shortcut->path, &current)) {
		*key = current.accel_key;
		*mods = current.accel_mods & gtk_accelerator_get_default_mod_mask ();
	} else {
		*key = shortcut->key;
		*mods = shortcut->mods;
	}
}

static gboolean
same_key (guint key_a, GdkModifierType mods_a, guint key_b, GdkModifierType mods_b)
{
	GdkModifierType mask = gtk_accelerator_get_default_mod_mask ();

	return gdk_keyval_to_lower (key_a) == gdk_keyval_to_lower (key_b) &&
		(mods_a & mask) == (mods_b & mask);
}

gboolean
nemo_shortcuts_is_default (const NemoShortcut *shortcut)
{
	guint key;
	GdkModifierType mods;

	nemo_shortcuts_get_current (shortcut, &key, &mods);
	if (key == 0 && shortcut->key == 0) {
		return TRUE;
	}

	return same_key (key, mods, shortcut->key, shortcut->mods);
}

static const NemoShortcut *
find_holder (const NemoShortcut *shortcut, guint key, GdkModifierType mods)
{
	GHashTableIter iter;
	gpointer value;

	g_hash_table_iter_init (&iter, shortcuts);
	while (g_hash_table_iter_next (&iter, NULL, &value)) {
		const NemoShortcut *other = value;
		guint other_key;
		GdkModifierType other_mods;

		if (other == shortcut) {
			continue;
		}
		nemo_shortcuts_get_current (other, &other_key, &other_mods);
		if (other_key != 0 && same_key (key, mods, other_key, other_mods)) {
			return other;
		}
	}

	return NULL;
}

NemoShortcutResult
nemo_shortcuts_set (const NemoShortcut  *shortcut,
		    guint                key,
		    GdkModifierType      mods,
		    const NemoShortcut **taken_by)
{
	const NemoShortcut *holder;

	if (taken_by != NULL) {
		*taken_by = NULL;
	}
	mods &= gtk_accelerator_get_default_mod_mask ();

	/* Without Ctrl or Alt, a key that types something would stop typing in
	   the location bar and in a rename. */
	if (key != 0 && g_unichar_isprint (gdk_keyval_to_unicode (key)) &&
	    (mods & (GDK_CONTROL_MASK | GDK_MOD1_MASK | GDK_SUPER_MASK | GDK_META_MASK)) == 0) {
		return NEMO_SHORTCUT_NEEDS_MODIFIER;
	}

	if (key != 0) {
		holder = find_holder (shortcut, key, mods);
		if (holder != NULL) {
			if (taken_by != NULL) {
				*taken_by = holder;
			}
			return NEMO_SHORTCUT_TAKEN;
		}
	}

	if (!gtk_accel_map_change_entry (shortcut->path, key, mods, FALSE)) {
		return NEMO_SHORTCUT_REFUSED;
	}

	return NEMO_SHORTCUT_SET;
}

NemoShortcutResult
nemo_shortcuts_reset (const NemoShortcut  *shortcut,
		      const NemoShortcut **taken_by)
{
	const NemoShortcut *holder;

	if (taken_by != NULL) {
		*taken_by = NULL;
	}

	/* A default can be a plain key, such as Delete or F2, so it skips the
	   modifier rule. */
	if (shortcut->key != 0) {
		holder = find_holder (shortcut, shortcut->key, shortcut->mods);
		if (holder != NULL) {
			if (taken_by != NULL) {
				*taken_by = holder;
			}
			return NEMO_SHORTCUT_TAKEN;
		}
	}

	if (!gtk_accel_map_change_entry (shortcut->path, shortcut->key, shortcut->mods, FALSE)) {
		return NEMO_SHORTCUT_REFUSED;
	}

	return NEMO_SHORTCUT_SET;
}

void
nemo_shortcuts_reset_all (void)
{
	GHashTableIter iter;
	gpointer value;

	if (shortcuts == NULL) {
		return;
	}

	/* Off first, so a key moved from one to another isn't in the way. */
	g_hash_table_iter_init (&iter, shortcuts);
	while (g_hash_table_iter_next (&iter, NULL, &value)) {
		if (!nemo_shortcuts_is_default (value)) {
			gtk_accel_map_change_entry (((NemoShortcut *) value)->path, 0, 0, TRUE);
		}
	}

	g_hash_table_iter_init (&iter, shortcuts);
	while (g_hash_table_iter_next (&iter, NULL, &value)) {
		const NemoShortcut *shortcut = value;

		if (!nemo_shortcuts_is_default (shortcut)) {
			gtk_accel_map_change_entry (shortcut->path, shortcut->key, shortcut->mods, TRUE);
		}
	}
}
