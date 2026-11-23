/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */

/* nemo-prefs-keyboard.c - the Keyboard tab in Settings.

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

#include "nemo-prefs-keyboard.h"

#include <glib/gi18n.h>

#include <libnemo-private/nemo-shortcuts.h>

#include "nemo-settings-search.h"

#define RESET_ICON "edit-undo-symbolic"

static const char *const keyboard_aliases[] = { "keyboard shortcuts", "hotkeys", "key bindings", "keybindings", "accelerators", NULL };

typedef struct {
	GtkTreeView       *tree;
	GtkListStore      *store;
	GtkTreeViewColumn *key_column;
	GtkTreeViewColumn *reset_column;
	GtkLabel          *note;
	/* The row waiting for a key, if any. */
	GtkTreeRowReference *waiting;
} Page;

static void
fill_row (Page *page, GtkTreeIter *iter, const NemoShortcut *shortcut)
{
	guint key;
	GdkModifierType mods;

	nemo_shortcuts_get_current (shortcut, &key, &mods);
	gtk_list_store_set (page->store, iter,
			    NEMO_PREFS_KEYBOARD_COL_KEY, key,
			    NEMO_PREFS_KEYBOARD_COL_MODS, mods,
			    NEMO_PREFS_KEYBOARD_COL_RESET, nemo_shortcuts_is_default (shortcut) ? NULL : RESET_ICON,
			    -1);
}

static const NemoShortcut *
row_shortcut (Page *page, GtkTreePath *path)
{
	GtkTreeIter iter;
	g_autofree char *accel_path = NULL;

	if (path == NULL || !gtk_tree_model_get_iter (GTK_TREE_MODEL (page->store), &iter, path)) {
		return NULL;
	}
	gtk_tree_model_get (GTK_TREE_MODEL (page->store), &iter,
			    NEMO_PREFS_KEYBOARD_COL_PATH, &accel_path, -1);

	return nemo_shortcuts_lookup (accel_path);
}

static gboolean
is_waiting (Page *page, GtkTreePath *path)
{
	GtkTreePath *waiting;
	gboolean same;

	if (page->waiting == NULL || !gtk_tree_row_reference_valid (page->waiting)) {
		return FALSE;
	}
	waiting = gtk_tree_row_reference_get_path (page->waiting);
	same = gtk_tree_path_compare (waiting, path) == 0;
	gtk_tree_path_free (waiting);

	return same;
}

static void
stop_waiting (Page *page)
{
	g_clear_pointer (&page->waiting, gtk_tree_row_reference_free);
	gtk_widget_queue_draw (GTK_WIDGET (page->tree));
}

static void
start_waiting (Page *page, GtkTreePath *path)
{
	stop_waiting (page);
	page->waiting = gtk_tree_row_reference_new (GTK_TREE_MODEL (page->store), path);
	gtk_label_set_text (page->note, "");
	gtk_widget_queue_draw (GTK_WIDGET (page->tree));
}

static void
key_cell_data (G_GNUC_UNUSED GtkTreeViewColumn *column,
	       GtkCellRenderer *cell,
	       GtkTreeModel    *model,
	       GtkTreeIter     *iter,
	       gpointer         data)
{
	Page *page = data;
	GtkTreePath *path = gtk_tree_model_get_path (model, iter);
	g_autofree char *text = NULL;
	guint key;
	GdkModifierType mods;

	if (is_waiting (page, path)) {
		g_object_set (cell, "text", _("New shortcut..."), "style", PANGO_STYLE_ITALIC, NULL);
		gtk_tree_path_free (path);
		return;
	}
	gtk_tree_path_free (path);

	gtk_tree_model_get (model, iter,
			    NEMO_PREFS_KEYBOARD_COL_KEY, &key,
			    NEMO_PREFS_KEYBOARD_COL_MODS, &mods, -1);
	text = key != 0 ? gtk_accelerator_get_label (key, mods) : g_strdup (_("Off"));
	g_object_set (cell, "text", text, "style", PANGO_STYLE_NORMAL, NULL);
}

static void
say (Page *page, NemoShortcutResult result, guint key, GdkModifierType mods, const NemoShortcut *taken_by)
{
	g_autofree char *keys = gtk_accelerator_get_label (key, mods);
	g_autofree char *text = NULL;

	switch (result) {
	case NEMO_SHORTCUT_TAKEN:
		text = g_strdup_printf (_("%s is already the shortcut for %s."), keys, taken_by->label);
		break;
	case NEMO_SHORTCUT_NEEDS_MODIFIER:
		text = g_strdup_printf (_("%s types a character, so it needs Ctrl or Alt held too."), keys);
		break;
	case NEMO_SHORTCUT_REFUSED:
		text = g_strdup_printf (_("%s couldn't be set."), keys);
		break;
	case NEMO_SHORTCUT_SET:
	default:
		break;
	}

	gtk_label_set_text (page->note, text != NULL ? text : "");
}

static void
reset_row (Page *page, GtkTreePath *path)
{
	const NemoShortcut *shortcut = row_shortcut (page, path);
	const NemoShortcut *taken_by = NULL;
	NemoShortcutResult result;

	if (shortcut == NULL || nemo_shortcuts_is_default (shortcut)) {
		return;
	}

	result = nemo_shortcuts_reset (shortcut, &taken_by);
	say (page, result, shortcut->key, shortcut->mods, taken_by);
}

static void
reset_all_clicked (G_GNUC_UNUSED GtkButton *button, Page *page)
{
	stop_waiting (page);
	nemo_shortcuts_reset_all ();
	gtk_label_set_text (page->note, "");
}

/* The key as GTK's own shortcut editor reads it: Shift only where it changed
   the key, and the lower case. */
static void
read_chord (GdkEventKey *event, guint *key, GdkModifierType *mods)
{
	GdkModifierType consumed = 0;

	gdk_keymap_translate_keyboard_state (gdk_keymap_get_for_display (gdk_window_get_display (event->window)),
					     event->hardware_keycode, event->state, event->group,
					     NULL, NULL, NULL, &consumed);

	*key = gdk_keyval_to_lower (event->keyval);
	if (*key == GDK_KEY_ISO_Left_Tab) {
		*key = GDK_KEY_Tab;
	}
	*mods = event->state & gtk_accelerator_get_default_mod_mask () & ~consumed;
	if (*key != event->keyval) {
		*mods |= GDK_SHIFT_MASK;
	}
}

/* On the dialog, and connected ahead of its own keys, so while a row waits
   every key goes to it: Escape and Ctrl+Tab included. */
static gboolean
dialog_key_press (G_GNUC_UNUSED GtkWidget *dialog, GdkEventKey *event, GtkWidget *tree)
{
	Page *page = g_object_get_data (G_OBJECT (tree), "keyboard-page");
	GtkTreePath *path;
	const NemoShortcut *shortcut;
	const NemoShortcut *taken_by = NULL;
	NemoShortcutResult result;
	guint key;
	GdkModifierType mods;

	if (page == NULL || page->waiting == NULL) {
		return GDK_EVENT_PROPAGATE;
	}
	if (event->is_modifier) {
		return GDK_EVENT_STOP;
	}

	path = gtk_tree_row_reference_get_path (page->waiting);
	shortcut = row_shortcut (page, path);
	gtk_tree_path_free (path);
	if (shortcut == NULL) {
		stop_waiting (page);
		return GDK_EVENT_PROPAGATE;
	}

	read_chord (event, &key, &mods);
	if (mods == 0 && key == GDK_KEY_Escape) {
		stop_waiting (page);
		return GDK_EVENT_STOP;
	}
	if (mods == 0 && key == GDK_KEY_BackSpace) {
		key = 0;
	} else if (!gtk_accelerator_valid (key, mods)) {
		gtk_widget_error_bell (GTK_WIDGET (page->tree));
		return GDK_EVENT_STOP;
	}

	stop_waiting (page);
	result = nemo_shortcuts_set (shortcut, key, mods, &taken_by);
	say (page, result, key, mods, taken_by);

	return GDK_EVENT_STOP;
}

static gboolean
tree_focus_out (G_GNUC_UNUSED GtkWidget *tree, G_GNUC_UNUSED GdkEventFocus *event, Page *page)
{
	if (page->waiting != NULL) {
		stop_waiting (page);
	}

	return GDK_EVENT_PROPAGATE;
}

/* The revert arrow is a click target only, as in the rest of the dialog. */
static gboolean
tree_button_press (GtkWidget *widget, GdkEventButton *event, Page *page)
{
	GtkTreePath *path = NULL;
	GtkTreeViewColumn *column = NULL;

	if (event->type != GDK_BUTTON_PRESS || event->button != GDK_BUTTON_PRIMARY ||
	    event->window != gtk_tree_view_get_bin_window (GTK_TREE_VIEW (widget))) {
		return GDK_EVENT_PROPAGATE;
	}
	if (!gtk_tree_view_get_path_at_pos (GTK_TREE_VIEW (widget), (gint) event->x, (gint) event->y,
					    &path, &column, NULL, NULL)) {
		return GDK_EVENT_PROPAGATE;
	}

	if (column == page->reset_column) {
		stop_waiting (page);
		reset_row (page, path);
		gtk_tree_path_free (path);
		return GDK_EVENT_STOP;
	}

	/* One click on the key waits for a new one, rather than needing the row
	   picked first. */
	if (column == page->key_column) {
		gtk_widget_grab_focus (widget);
		gtk_tree_view_set_cursor (GTK_TREE_VIEW (widget), path, NULL, FALSE);
		start_waiting (page, path);
		gtk_tree_path_free (path);
		return GDK_EVENT_STOP;
	}

	stop_waiting (page);
	gtk_tree_path_free (path);
	return GDK_EVENT_PROPAGATE;
}

static gboolean
tree_key_press (GtkWidget *widget, GdkEventKey *event, Page *page)
{
	GtkTreePath *path = NULL;

	if ((event->state & gtk_accelerator_get_default_mod_mask ()) != 0) {
		return GDK_EVENT_PROPAGATE;
	}
	switch (event->keyval) {
	case GDK_KEY_space:
	case GDK_KEY_KP_Space:
	case GDK_KEY_Return:
	case GDK_KEY_KP_Enter:
	case GDK_KEY_ISO_Enter:
		break;
	default:
		return GDK_EVENT_PROPAGATE;
	}

	gtk_tree_view_get_cursor (GTK_TREE_VIEW (widget), &path, NULL);
	if (path == NULL) {
		return GDK_EVENT_PROPAGATE;
	}
	start_waiting (page, path);
	gtk_tree_path_free (path);

	return GDK_EVENT_STOP;
}

/* Swapped, so it goes with the list. */
static void
accel_map_changed (GtkWidget  *tree,
		   const char *accel_path,
		   G_GNUC_UNUSED guint key,
		   G_GNUC_UNUSED GdkModifierType mods,
		   G_GNUC_UNUSED GtkAccelMap *map)
{
	Page *page = g_object_get_data (G_OBJECT (tree), "keyboard-page");
	const NemoShortcut *shortcut = nemo_shortcuts_lookup (accel_path);
	GtkTreeModel *model;
	GtkTreeIter iter;
	gboolean more;

	if (page == NULL || shortcut == NULL) {
		return;
	}

	model = GTK_TREE_MODEL (page->store);
	for (more = gtk_tree_model_get_iter_first (model, &iter); more; more = gtk_tree_model_iter_next (model, &iter)) {
		g_autofree char *row_path = NULL;

		gtk_tree_model_get (model, &iter, NEMO_PREFS_KEYBOARD_COL_PATH, &row_path, -1);
		if (g_strcmp0 (row_path, accel_path) == 0) {
			fill_row (page, &iter, shortcut);
			break;
		}
	}
}

static void
free_page (Page *page)
{
	g_clear_pointer (&page->waiting, gtk_tree_row_reference_free);
	g_free (page);
}

void
nemo_prefs_keyboard_setup (GtkBuilder *builder)
{
	GtkWidget *box, *tab, *dialog, *top, *reset_all, *scrolled, *tree, *help, *note;
	GtkCellRenderer *renderer;
	GtkTreeViewColumn *column;
	GList *list, *l;
	Page *page;

	box = GTK_WIDGET (gtk_builder_get_object (builder, "keyboard_page_box"));
	tab = GTK_WIDGET (gtk_builder_get_object (builder, "keyboard_scrolledwindow"));
	dialog = GTK_WIDGET (gtk_builder_get_object (builder, "file_management_dialog"));
	if (box == NULL || tab == NULL || dialog == NULL) {
		return;
	}
	/* The tab has no settings key to hang aliases on. Search takes a tab's
	   aliases from the stack's own child. */
	g_object_set_data (G_OBJECT (tab), NEMO_SETTINGS_SEARCH_ALIASES, (gpointer) keyboard_aliases);

	page = g_new0 (Page, 1);
	page->store = gtk_list_store_new (NEMO_PREFS_KEYBOARD_N_COLS,
					  G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING,
					  G_TYPE_UINT, GDK_TYPE_MODIFIER_TYPE, G_TYPE_STRING);

	list = nemo_shortcuts_list ();
	for (l = list; l != NULL; l = l->next) {
		const NemoShortcut *shortcut = l->data;
		g_autofree char *tip = shortcut->tooltip != NULL ? g_markup_escape_text (shortcut->tooltip, -1) : NULL;
		GtkTreeIter iter;

		gtk_list_store_append (page->store, &iter);
		gtk_list_store_set (page->store, &iter,
				    NEMO_PREFS_KEYBOARD_COL_PATH, shortcut->path,
				    NEMO_PREFS_KEYBOARD_COL_LABEL, shortcut->label,
				    NEMO_PREFS_KEYBOARD_COL_TOOLTIP, tip,
				    -1);
		fill_row (page, &iter, shortcut);
	}
	g_list_free (list);

	/* A button goes at the left, on a line of its own. */
	top = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 6);
	reset_all = gtk_button_new_with_mnemonic (_("_Reset all"));
	gtk_widget_set_name (reset_all, "keyboard_reset_all");
	gtk_widget_set_tooltip_text (reset_all, _("Put every shortcut back to the key it starts with."));
	gtk_box_pack_start (GTK_BOX (top), reset_all, FALSE, FALSE, 0);
	gtk_box_pack_start (GTK_BOX (box), top, FALSE, FALSE, 0);

	tree = gtk_tree_view_new_with_model (GTK_TREE_MODEL (page->store));
	g_object_unref (page->store);
	gtk_widget_set_name (tree, "keyboard_tree");
	gtk_tree_view_set_tooltip_column (GTK_TREE_VIEW (tree), NEMO_PREFS_KEYBOARD_COL_TOOLTIP);
	gtk_tree_view_set_search_column (GTK_TREE_VIEW (tree), NEMO_PREFS_KEYBOARD_COL_LABEL);
	page->tree = GTK_TREE_VIEW (tree);
	g_object_set_data_full (G_OBJECT (tree), "keyboard-page", page, (GDestroyNotify) free_page);

	renderer = gtk_cell_renderer_text_new ();
	g_object_set (renderer, "ellipsize", PANGO_ELLIPSIZE_END, NULL);
	column = gtk_tree_view_column_new_with_attributes (_("Action"), renderer,
							   "text", NEMO_PREFS_KEYBOARD_COL_LABEL, NULL);
	gtk_tree_view_column_set_expand (column, TRUE);
	gtk_tree_view_append_column (GTK_TREE_VIEW (tree), column);

	/* GTK's own shortcut cell takes a grab before it's on screen, which
	   upstream GTK gets wrong (GNOME/gtk#180), so the key is read here. */
	renderer = gtk_cell_renderer_text_new ();
	page->key_column = gtk_tree_view_column_new ();
	gtk_tree_view_column_set_title (page->key_column, _("Shortcut"));
	gtk_tree_view_column_pack_start (page->key_column, renderer, TRUE);
	gtk_tree_view_column_set_cell_data_func (page->key_column, renderer, key_cell_data, page, NULL);
	gtk_tree_view_append_column (GTK_TREE_VIEW (tree), page->key_column);

	renderer = gtk_cell_renderer_pixbuf_new ();
	page->reset_column = gtk_tree_view_column_new_with_attributes ("", renderer,
								       "icon-name", NEMO_PREFS_KEYBOARD_COL_RESET,
								       NULL);
	gtk_tree_view_append_column (GTK_TREE_VIEW (tree), page->reset_column);

	g_signal_connect (tree, "button-press-event", G_CALLBACK (tree_button_press), page);
	g_signal_connect (tree, "key-press-event", G_CALLBACK (tree_key_press), page);
	g_signal_connect (tree, "focus-out-event", G_CALLBACK (tree_focus_out), page);
	g_signal_connect_object (dialog, "key-press-event", G_CALLBACK (dialog_key_press), tree, 0);

	/* The list scrolls on its own, so the page asks only for this much and
	   the window is sized by the other pages. */
	scrolled = gtk_scrolled_window_new (NULL, NULL);
	gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW (scrolled), GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
	gtk_scrolled_window_set_shadow_type (GTK_SCROLLED_WINDOW (scrolled), GTK_SHADOW_IN);
	gtk_scrolled_window_set_overlay_scrolling (GTK_SCROLLED_WINDOW (scrolled), FALSE);
	gtk_scrolled_window_set_min_content_height (GTK_SCROLLED_WINDOW (scrolled), 200);
	gtk_widget_set_vexpand (scrolled, TRUE);
	gtk_container_add (GTK_CONTAINER (scrolled), tree);
	gtk_box_pack_start (GTK_BOX (box), scrolled, TRUE, TRUE, 0);

	help = gtk_label_new (_("Click a shortcut, or press Enter or Space on it, then press the new keys. Escape leaves it as it was, and Backspace turns it off."));
	gtk_label_set_line_wrap (GTK_LABEL (help), TRUE);
	gtk_label_set_xalign (GTK_LABEL (help), 0);
	gtk_style_context_add_class (gtk_widget_get_style_context (help), GTK_STYLE_CLASS_DIM_LABEL);
	gtk_box_pack_start (GTK_BOX (box), help, FALSE, FALSE, 0);

	note = gtk_label_new (NULL);
	gtk_widget_set_name (note, "keyboard_note");
	gtk_label_set_line_wrap (GTK_LABEL (note), TRUE);
	gtk_label_set_xalign (GTK_LABEL (note), 0);
	gtk_box_pack_start (GTK_BOX (box), note, FALSE, FALSE, 0);
	page->note = GTK_LABEL (note);

	g_signal_connect (reset_all, "clicked", G_CALLBACK (reset_all_clicked), page);
	g_signal_connect_object (gtk_accel_map_get (), "changed", G_CALLBACK (accel_map_changed), tree, G_CONNECT_SWAPPED);

	gtk_widget_show_all (box);
}
