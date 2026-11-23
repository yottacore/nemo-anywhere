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

#include <glib/gstdio.h>

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

/* The shortcut file as this process last read or wrote it, path -> accel name.
   Every window saves its whole map, so only what the file changed since then
   is taken from it, or a change not saved yet would be lost. */
static char         *file_name = NULL;
static GHashTable   *file_known = NULL;
static GFileMonitor *file_monitor = NULL;
static guint         file_reread_id = 0;
static gboolean      file_taking = FALSE;

/* One quoted string of GTK's format, escapes and all. */
static const char *
read_quoted (const char *p, char **out)
{
	const char *start;
	char *raw;

	while (*p == ' ' || *p == '\t') {
		p++;
	}
	if (*p != '"') {
		return NULL;
	}
	start = ++p;
	while (*p != '\0' && *p != '"') {
		if (*p == '\\' && p[1] != '\0') {
			p++;
		}
		p++;
	}
	if (*p != '"') {
		return NULL;
	}
	raw = g_strndup (start, p - start);
	*out = g_strcompress (raw);
	g_free (raw);

	return p + 1;
}

/* Only lines GTK wrote as changed. A commented one is still at its default. */
static GHashTable *
read_file (const char *filename)
{
	GHashTable *found = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, g_free);
	char *contents = NULL;
	char **lines, **line;

	if (!g_file_get_contents (filename, &contents, NULL, NULL)) {
		return found;
	}

	lines = g_strsplit (contents, "\n", -1);
	for (line = lines; *line != NULL; line++) {
		const char *p = *line;
		char *path = NULL, *accel = NULL;

		while (*p == ' ' || *p == '\t') {
			p++;
		}
		if (!g_str_has_prefix (p, "(gtk_accel_path")) {
			continue;
		}
		p += strlen ("(gtk_accel_path");
		p = read_quoted (p, &path);
		if (p != NULL) {
			p = read_quoted (p, &accel);
		}
		while (p != NULL && (*p == ' ' || *p == '\t')) {
			p++;
		}
		if (p != NULL && *p == ')') {
			g_hash_table_replace (found, path, accel);
		} else {
			g_free (path);
			g_free (accel);
		}
	}
	g_strfreev (lines);
	g_free (contents);

	return found;
}

/* Merged rather than replaced, so a file caught half written takes nothing
   back. */
static void
know (GHashTable *entries)
{
	GHashTableIter iter;
	gpointer path, accel;

	g_hash_table_iter_init (&iter, entries);
	while (g_hash_table_iter_next (&iter, &path, &accel)) {
		g_hash_table_replace (file_known, g_strdup (path), g_strdup (accel));
	}
}

void
nemo_shortcuts_file_reread (void)
{
	GHashTable *now;
	GPtrArray *changed;
	GHashTableIter iter;
	gpointer path, accel;
	guint i;

	if (file_name == NULL) {
		return;
	}

	now = read_file (file_name);
	changed = g_ptr_array_new ();
	g_hash_table_iter_init (&iter, now);
	while (g_hash_table_iter_next (&iter, &path, &accel)) {
		if (g_strcmp0 (g_hash_table_lookup (file_known, path), accel) != 0) {
			g_ptr_array_add (changed, path);
		}
	}

	/* What the file changed beats a change here that isn't saved yet, since
	   it's the newer one. Off first, so a key that moved isn't in the way. */
	file_taking = TRUE;
	for (i = 0; i < changed->len; i++) {
		gtk_accel_map_change_entry (changed->pdata[i], 0, 0, TRUE);
	}
	for (i = 0; i < changed->len; i++) {
		guint key = 0;
		GdkModifierType mods = 0;

		gtk_accelerator_parse (g_hash_table_lookup (now, changed->pdata[i]), &key, &mods);
		gtk_accel_map_change_entry (changed->pdata[i], key, mods, TRUE);
	}
	file_taking = FALSE;

	know (now);
	g_ptr_array_free (changed, TRUE);
	g_hash_table_destroy (now);
}

static gboolean
reread_later (G_GNUC_UNUSED gpointer data)
{
	file_reread_id = 0;
	nemo_shortcuts_file_reread ();

	return G_SOURCE_REMOVE;
}

/* Another window's save is a rename over the file, which comes as several
   events. */
static void
file_changed (G_GNUC_UNUSED GFileMonitor     *monitor,
	      G_GNUC_UNUSED GFile            *file,
	      G_GNUC_UNUSED GFile            *other,
	      GFileMonitorEvent               event,
	      G_GNUC_UNUSED gpointer          data)
{
	if (event == G_FILE_MONITOR_EVENT_DELETED || event == G_FILE_MONITOR_EVENT_ATTRIBUTE_CHANGED ||
	    event == G_FILE_MONITOR_EVENT_PRE_UNMOUNT || event == G_FILE_MONITOR_EVENT_UNMOUNTED) {
		return;
	}
	if (file_reread_id == 0) {
		file_reread_id = g_timeout_add (100, reread_later, NULL);
	}
}

/* After the file is loaded into the map. */
void
nemo_shortcuts_file_watch (const char *filename)
{
	GHashTable *now;
	GFile *file;

	g_free (file_name);
	file_name = g_strdup (filename);
	if (file_known == NULL) {
		file_known = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, g_free);
	}
	now = read_file (file_name);
	know (now);
	g_hash_table_destroy (now);

	g_clear_object (&file_monitor);
	file = g_file_new_for_path (file_name);
	file_monitor = g_file_monitor_file (file, G_FILE_MONITOR_NONE, NULL, NULL);
	g_object_unref (file);
	if (file_monitor != NULL) {
		g_signal_connect (file_monitor, "changed", G_CALLBACK (file_changed), NULL);
	}
}

/* Written whole and renamed into place, so another window never reads half
   of it. */
void
nemo_shortcuts_file_save (void)
{
	GHashTable *written;
	char *tmp;
	int fd;

	if (file_name == NULL) {
		return;
	}

	nemo_shortcuts_file_reread ();

	tmp = g_strconcat (file_name, ".XXXXXX", NULL);
	fd = g_mkstemp (tmp);
	if (fd < 0) {
		g_free (tmp);
		gtk_accel_map_save (file_name);
		written = read_file (file_name);
		know (written);
		g_hash_table_destroy (written);
		return;
	}
	gtk_accel_map_save_fd (fd);
	g_close (fd, NULL);
	written = read_file (tmp);

	if (g_rename (tmp, file_name) != 0) {
		/* Windows can refuse it while another window reads the file. */
		g_unlink (tmp);
		gtk_accel_map_save (file_name);
	}
	know (written);
	g_hash_table_destroy (written);
	g_free (tmp);
}

gboolean
nemo_shortcuts_file_taking (void)
{
	return file_taking;
}
