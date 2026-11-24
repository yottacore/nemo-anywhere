/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */

/* nemo-shortcuts-window.c - the keys the keyboard shortcuts window shows.

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

#include "nemo-shortcuts-window.h"

#include <string.h>

#include <libnemo-private/nemo-shortcuts.h>

#define SHELL "<Actions>/ShellActions/"
#define DIR_VIEW "<Actions>/DirViewActions/"

typedef struct {
	const char *row;
	const char *paths[3];
	/* The keys shown when Ctrl+I inverts the selection, if they differ. */
	const char *shown_inverts;
} Row;

/* The rows of the keyboard shortcuts window, by id, and the shortcuts each
   stands for.
   A key in a row that none of them starts with, such as Return for Open,
   is a key binding and stays as written. */
static const Row rows[] = {
	{ "shortcut_new_window",       { SHELL "New Window" },                          NULL },
	{ "shortcut_close",            { SHELL "Close" },                               NULL },
	{ "shortcut_close_all",        { SHELL "Close All Windows" },                   NULL },
	{ "shortcut_extra_pane",       { SHELL "Show Hide Extra Pane" },                NULL },
	{ "shortcut_search",           { SHELL "Search" },                              NULL },
	{ "shortcut_add_bookmark",     { SHELL "Add Bookmark" },                        NULL },
	{ "shortcut_edit_bookmarks",   { SHELL "Edit Bookmarks" },                      NULL },
	{ "shortcut_make_link",        { DIR_VIEW "Create Link" },                      NULL },
	{ "shortcut_pin",              { DIR_VIEW "Pin File", DIR_VIEW "Unpin File" },  NULL },
	{ "shortcut_help",             { SHELL "NemoHelp" },                            NULL },
	{ "shortcut_shortcuts",        { SHELL "NemoShortcuts" },                       NULL },
	{ "shortcut_open",             { DIR_VIEW "Open" },                             NULL },
	{ "shortcut_open_in_tab",      { SHELL "NewTabAccel" },                         NULL },
	{ "shortcut_open_alternate",   { DIR_VIEW "OpenAlternate" },                    NULL },
	{ "shortcut_open_location",    { DIR_VIEW "OpenContainingFolder" },             NULL },
	{ "shortcut_new_tab",          { SHELL "New Tab", SHELL "NewTabAccel" },        NULL },
	{ "shortcut_tab_previous",     { SHELL "TabsPrevious" },                        NULL },
	{ "shortcut_tab_next",         { SHELL "TabsNext" },                            NULL },
	{ "shortcut_tab_move_left",    { SHELL "TabsMoveLeft" },                        NULL },
	{ "shortcut_tab_move_right",   { SHELL "TabsMoveRight" },                       NULL },
	{ "shortcut_back",             { SHELL "Back" },                                NULL },
	{ "shortcut_forward",          { SHELL "Forward" },                             NULL },
	{ "shortcut_up",               { SHELL "Up" },                                  NULL },
	{ "shortcut_down",             { DIR_VIEW "OpenAccel" },                        NULL },
	{ "shortcut_home",             { SHELL "Home" },                                NULL },
	{ "shortcut_location",         { SHELL "Edit Location" },                       NULL },
	{ "shortcut_extra_pane_dual",  { SHELL "Show Hide Extra Pane" },                NULL },
	{ "shortcut_same_location",    { SHELL "SplitViewSameLocation" },               NULL },
	{ "shortcut_next_pane",        { SHELL "SplitViewNextPane" },                   NULL },
	{ "shortcut_zoom_in",          { SHELL "Zoom In" },                             NULL },
	{ "shortcut_zoom_out",         { SHELL "Zoom Out" },                            NULL },
	{ "shortcut_zoom_normal",      { SHELL "Zoom Normal" },                         NULL },
	{ "shortcut_reload",           { SHELL "Reload" },                              NULL },
	{ "shortcut_hidden",           { SHELL "Show Hidden Files" },                   NULL },
	{ "shortcut_sidebar",          { SHELL "Show Hide Sidebar" },                   NULL },
	{ "shortcut_list_view",        { SHELL "ListView" },                            NULL },
	{ "shortcut_compact_view",     { SHELL "CompactView" },                         NULL },
	{ "shortcut_icon_view",        { SHELL "IconView" },                            NULL },
	{ "shortcut_image_view",       { SHELL "ImageView" },                           NULL },
	{ "shortcut_new_folder",       { DIR_VIEW "New Folder" },                       NULL },
	{ "shortcut_rename",           { DIR_VIEW "Rename" },                           NULL },
	{ "shortcut_trash",            { DIR_VIEW "Trash" },                            NULL },
	{ "shortcut_delete",           { DIR_VIEW "Delete" },                           NULL },
	{ "shortcut_cut",              { DIR_VIEW "Cut" },                              NULL },
	{ "shortcut_copy",             { DIR_VIEW "Copy" },                             NULL },
	{ "shortcut_paste",            { DIR_VIEW "Paste" },                            NULL },
	{ "shortcut_paste_key",        { DIR_VIEW "PasteIntoViewed" },                  NULL },
	{ "shortcut_select_all",       { DIR_VIEW "Select All" },                       NULL },
	{ "shortcut_invert_selection", { DIR_VIEW "Invert Selection", DIR_VIEW "PropertiesAccel" },
	                               "<Primary>I <shift><Primary>I" },
	{ "shortcut_select_pattern",   { DIR_VIEW "Select Pattern" },                   NULL },
	/* The window's own Undo is for a text change, where it's built. */
	{ "shortcut_undo",             { DIR_VIEW "Undo", SHELL "Undo" },               NULL },
	{ "shortcut_redo",             { DIR_VIEW "Redo" },                             NULL },
	{ "shortcut_properties",       { DIR_VIEW "Properties", DIR_VIEW "PropertiesAccel" },
	                               "<alt>Return" },
};

#define SHOWN "nemo-shortcuts-shown"
#define UNUSED "nemo-shortcuts-unused"
#define INVERTS "nemo-shortcuts-primary-i-inverts"
#define ROWS "nemo-shortcuts-rows"

static void
accel_map_changed (GtkWidget *window)
{
	nemo_shortcuts_window_show_keys (window,
					 GPOINTER_TO_INT (g_object_get_data (G_OBJECT (window), INVERTS)));
}

/* Returns: (transfer none) */
GtkWidget *
nemo_shortcuts_window_take (GtkBuilder *builder)
{
	GtkWidget *window = GTK_WIDGET (gtk_builder_get_object (builder, "keyboard_shortcuts"));
	GPtrArray *found = g_ptr_array_new ();
	guint i;

	for (i = 0; i < G_N_ELEMENTS (rows); i++) {
		GObject *row = gtk_builder_get_object (builder, rows[i].row);
		char *shown = NULL;

		if (row == NULL) {
			g_warning ("The keyboard shortcuts window has no row %s", rows[i].row);
		} else {
			g_object_get (row, "accelerator", &shown, NULL);
			g_object_set_data_full (row, SHOWN, shown, g_free);
		}
		/* The rows go with the window, so they aren't held. */
		g_ptr_array_add (found, row);
	}
	g_object_set_data_full (G_OBJECT (window), ROWS, found, (GDestroyNotify) g_ptr_array_unref);

	g_signal_connect_object (gtk_accel_map_get (), "changed",
				 G_CALLBACK (accel_map_changed), window, G_CONNECT_SWAPPED);

	return window;
}

void
nemo_shortcuts_window_show_keys (GtkWidget *window,
				 gboolean   primary_i_inverts)
{
	GPtrArray *found = g_object_get_data (G_OBJECT (window), ROWS);
	guint i;

	g_return_if_fail (found != NULL && found->len == G_N_ELEMENTS (rows));

	g_object_set_data (G_OBJECT (window), INVERTS, GINT_TO_POINTER (primary_i_inverts));

	for (i = 0; i < G_N_ELEMENTS (rows); i++) {
		GtkWidget *row = g_ptr_array_index (found, i);
		const char *shown;
		g_autofree char *keys = NULL;

		if (row == NULL) {
			continue;
		}
		shown = primary_i_inverts && rows[i].shown_inverts != NULL
			? rows[i].shown_inverts
			: g_object_get_data (G_OBJECT (row), SHOWN);
		keys = nemo_shortcuts_keys_text (shown, rows[i].paths);
		g_object_set (row, "accelerator", keys, NULL);
		gtk_widget_set_visible (row, *keys != '\0' && g_object_get_data (G_OBJECT (row), UNUSED) == NULL);
	}
}

void
nemo_shortcuts_window_set_unused (GtkWidget  *window,
				  const char *row_id,
				  gboolean    unused)
{
	GPtrArray *found = g_object_get_data (G_OBJECT (window), ROWS);
	guint i;

	g_return_if_fail (found != NULL && found->len == G_N_ELEMENTS (rows));

	for (i = 0; i < G_N_ELEMENTS (rows); i++) {
		if (strcmp (rows[i].row, row_id) == 0 && g_ptr_array_index (found, i) != NULL) {
			g_object_set_data (g_ptr_array_index (found, i), UNUSED, GINT_TO_POINTER (unused));
		}
	}
}

/* Returns: (transfer none) */
const char *const *
nemo_shortcuts_window_row_ids (void)
{
	static const char *ids[G_N_ELEMENTS (rows) + 1];
	guint i;

	for (i = 0; i < G_N_ELEMENTS (rows); i++) {
		ids[i] = rows[i].row;
	}
	ids[i] = NULL;

	return ids;
}
