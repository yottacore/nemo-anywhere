/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */

/* test-nemo-prefs-dialog.c - the Settings window as it opens.

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

/* Opens the real dialog. No scrollbar in it hides until hovered, so a page cut
 * short by a small screen doesn't look complete. Close is in a bar outside
 * the scrolling and is the default, and Escape closes. The window is never
 * more than 9 tenths of the work area, and its floor grows with the text
 * scale, which is how Windows passes a fractional display scale. No tab, in
 * the list on the left, ever keeps the keyboard, and Ctrl+Page Up and Down,
 * Ctrl+Tab and Ctrl+Shift+Tab step through the list, sub-tabs included.
 * Sub-tabs are indented rows, and the parent and its showing sub-tab are both
 * lit. List columns has a sub-tab for each list. No page needs a scrollbar at
 * the size the window opens. The search field sits over the pages, and goes
 * to the setting picked. The Keyboard tab lists the shortcuts, and changes,
 * turns off, and resets each one. The pages that build their own widgets are
 * left empty here. */

#include <config.h>

#include <stdlib.h>
#include <string.h>
#include <gtk/gtk.h>

#include <libnemo-private/nemo-bulk-rename.h>
#include <libnemo-private/nemo-column-chooser.h>
#include <libnemo-private/nemo-column-utilities.h>
#include <libnemo-private/nemo-desktop-utils.h>
#include <libnemo-private/nemo-global-preferences.h>
#include <libnemo-private/nemo-shortcuts.h>
#include <libnemo-private/nemo-tab-list.h>

#include "nemo-file-management-properties.h"
#include "nemo-settings-aliases.h"
#include "nemo-settings-search.h"
#include "nemo-plugin-manager.h"
#include "nemo-prefs-current-folder.h"
#include "nemo-prefs-file-cache.h"
#include "nemo-prefs-keyboard.h"
#include "nemo-template-config-widget.h"

#include "test-scratch.h"
#include "test-check.h"

/* The same floor the dialog is written with, for a 96dpi screen. */
#define MIN_WIDTH 1000
#define MIN_HEIGHT 700

/* Returns: (transfer floating) */
NemoPluginManager *
nemo_plugin_manager_new (void)
{
	return (NemoPluginManager *) gtk_box_new (GTK_ORIENTATION_VERTICAL, 0);
}

/* Returns: (transfer floating) */
GtkWidget *
nemo_template_config_widget_new (void)
{
	return gtk_box_new (GTK_ORIENTATION_VERTICAL, 0);
}

void
nemo_prefs_file_cache_setup (G_GNUC_UNUSED GtkBuilder *builder)
{
}

void
nemo_prefs_current_folder_setup (G_GNUC_UNUSED GtkBuilder *builder, G_GNUC_UNUSED GtkWidget *dialog, G_GNUC_UNUSED GtkWindow *parent)
{
}

static void
settle (void)
{
	int i;

	for (i = 0; i < 20; i++) {
		while (gtk_events_pending ()) {
			gtk_main_iteration ();
		}
		g_usleep (10000);
	}
}

static GtkWidget *
find_named (GtkWidget *widget, const char *name)
{
	GList *children, *l;
	GtkWidget *found = NULL;

	if (g_strcmp0 (gtk_buildable_get_name (GTK_BUILDABLE (widget)), name) == 0) {
		return widget;
	}
	if (!GTK_IS_CONTAINER (widget)) {
		return NULL;
	}

	children = gtk_container_get_children (GTK_CONTAINER (widget));
	for (l = children; l != NULL && found == NULL; l = l->next) {
		found = find_named (l->data, name);
	}
	g_list_free (children);

	return found;
}

static void
count_overlay (GtkWidget *widget, gpointer data)
{
	int *counts = data;

	if (GTK_IS_SCROLLED_WINDOW (widget)) {
		counts[0]++;
		if (gtk_scrolled_window_get_overlay_scrolling (GTK_SCROLLED_WINDOW (widget))) {
			counts[1]++;
		}
	}
	if (GTK_IS_CONTAINER (widget)) {
		gtk_container_forall (GTK_CONTAINER (widget), count_overlay, data);
	}
}

static GtkWidget *
open_dialog (void)
{
	GList *windows, *l;
	GtkWidget *dialog = NULL;

	nemo_file_management_properties_dialog_show (NULL, NULL);
	settle ();

	windows = gtk_window_list_toplevels ();
	for (l = windows; l != NULL; l = l->next) {
		if (g_strcmp0 (gtk_buildable_get_name (GTK_BUILDABLE (l->data)), "file_management_dialog") == 0) {
			dialog = l->data;
		}
	}
	g_list_free (windows);

	return dialog;
}

static void
test_widgets (GtkWidget *dialog)
{
	GtkWidget *close_button, *close_bar;
	int counts[2] = { 0, 0 };

	count_overlay (dialog, counts);
	g_print ("%d scrolled windows, %d with overlay scrolling\n", counts[0], counts[1]);
	check (counts[0] > 0);
	check (counts[1] == 0);

	close_button = find_named (dialog, "close_button");
	close_bar = find_named (dialog, "close_bar");
	check (close_button != NULL && close_bar != NULL);
	check (close_button != NULL && gtk_widget_has_default (close_button));
	check (close_bar != NULL && gtk_widget_get_ancestor (close_bar, GTK_TYPE_SCROLLED_WINDOW) == NULL);
	check (close_bar != NULL && gtk_widget_is_ancestor (close_button, close_bar));
}

/* With the keyboard in another window, GTK reports focus moves that didn't
 * happen, so the Tab walk below asks for it first and waits. */
static gboolean
take_keyboard (GtkWidget *window)
{
	int i;

	gtk_window_present (GTK_WINDOW (window));
	for (i = 0; i < 500 && !gtk_window_has_toplevel_focus (GTK_WINDOW (window)); i++) {
		if (i % 10 == 0 && gtk_widget_get_mapped (window)) {
			gdk_window_focus (gtk_widget_get_window (window), GDK_CURRENT_TIME);
		}
		g_usleep (10000);
		while (gtk_events_pending ()) {
			gtk_main_iteration ();
		}
	}

	return gtk_window_has_toplevel_focus (GTK_WINDOW (window));
}

static void
collect_rows (GtkWidget *widget, gpointer data)
{
	GPtrArray *rows = data;

	if (GTK_IS_LIST_BOX_ROW (widget) || GTK_IS_LIST_BOX (widget)) {
		g_ptr_array_add (rows, widget);
	}
	if (GTK_IS_CONTAINER (widget)) {
		gtk_container_forall (GTK_CONTAINER (widget), collect_rows, data);
	}
}

static gboolean
is_a_tab (GtkWidget *focus, GtkWidget *sidebar)
{
	return focus != NULL &&
	       (GTK_IS_NOTEBOOK (focus) || focus == sidebar || gtk_widget_is_ancestor (focus, sidebar));
}

/* Returns whether the key has a keycode, which GTK's own key bindings need. */
static gboolean
send_key (GtkWidget *dialog, guint keyval, GdkModifierType state)
{
	GdkEvent *event;
	GdkKeymapKey *keys = NULL;
	gint n_keys = 0;
	gboolean coded = FALSE;

	event = gdk_event_new (GDK_KEY_PRESS);
	event->key.window = g_object_ref (gtk_widget_get_window (dialog));
	event->key.keyval = keyval;
	event->key.state = state;
	/* Key bindings are looked up by keycode, not keyval. */
	if (gdk_keymap_get_entries_for_keyval (gdk_keymap_get_for_display (gdk_display_get_default ()),
					       keyval, &keys, &n_keys) && n_keys > 0) {
		event->key.hardware_keycode = (guint16) keys[0].keycode;
		event->key.group = (guint8) keys[0].group;
		coded = TRUE;
	}
	g_free (keys);
	event->key.time = GDK_CURRENT_TIME;
	gdk_event_set_device (event, gdk_seat_get_keyboard (gdk_display_get_default_seat (gdk_display_get_default ())));
	gtk_main_do_event (event);
	gdk_event_free (event);
	settle ();

	return coded;
}

/* Views draws a line under its 2 per-folder boxes, above the Default and
   Current sub-tabs (2026101816495205). */
static void
test_per_folder_line (GtkWidget *dialog)
{
	GtkWidget *remember = find_named (dialog, "remember_folder_settings_checkbutton");
	GtkWidget *line = find_named (dialog, "per_folder_separator");
	GtkWidget *stack = find_named (dialog, "views_stack");

	check (remember != NULL && line != NULL && stack != NULL);
	if (remember == NULL || line == NULL || stack == NULL) {
		return;
	}
	check (GTK_IS_SEPARATOR (line));
	check (gtk_widget_get_visible (line));
	check (gtk_widget_get_parent (line) == gtk_widget_get_parent (remember));
	check (gtk_widget_get_parent (line) == gtk_widget_get_parent (stack));
	if (GTK_IS_BOX (gtk_widget_get_parent (line))) {
		GtkBox *box = GTK_BOX (gtk_widget_get_parent (line));
		int at_remember, at_line, at_stack;

		gtk_container_child_get (GTK_CONTAINER (box), remember, "position", &at_remember, NULL);
		gtk_container_child_get (GTK_CONTAINER (box), line, "position", &at_line, NULL);
		gtk_container_child_get (GTK_CONTAINER (box), stack, "position", &at_stack, NULL);
		check (at_remember < at_line && at_line < at_stack);
	}
}

static void
test_tabs_take_no_focus (GtkWidget *dialog)
{
	GtkWidget *sidebar = find_named (dialog, "page_sidebar");
	/* Views had Default and Current as tabs on top until 2026101009150769
	   moved them into the list as sub-tabs.
	GtkWidget *notebook = find_named (dialog, "views_notebook"); */
	GtkWidget *remember = find_named (dialog, "remember_folder_settings_checkbutton");
	GtkWidget *stack = find_named (dialog, "page_stack");
	GPtrArray *rows = g_ptr_array_new ();
	GHashTable *seen = g_hash_table_new (NULL, NULL);
	guint i;

	check (sidebar != NULL && remember != NULL && stack != NULL);
	if (sidebar == NULL || remember == NULL || stack == NULL) {
		g_ptr_array_free (rows, TRUE);
		g_hash_table_destroy (seen);
		return;
	}

	collect_rows (sidebar, rows);
	check (rows->len > 1);
	for (i = 0; i < rows->len; i++) {
		check (!gtk_widget_get_can_focus (rows->pdata[i]));
	}

	/* Tab right round the dialog, twice, never stopping on a tab. */
	if (take_keyboard (dialog)) {
		gtk_window_set_focus (GTK_WINDOW (dialog), NULL);
		for (i = 0; i < 400; i++) {
			GtkWidget *focus;

			if (!gtk_widget_child_focus (dialog, GTK_DIR_TAB_FORWARD)) {
				continue;
			}
			focus = gtk_window_get_focus (GTK_WINDOW (dialog));
			if (is_a_tab (focus, sidebar)) {
				g_print ("FAIL: Tab stopped on a tab: %s\n", G_OBJECT_TYPE_NAME (focus));
				failures++;
				break;
			}
			g_hash_table_add (seen, focus);
		}
		g_print ("Tab visited %u widgets\n", g_hash_table_size (seen));
		check (g_hash_table_size (seen) > 3);
	} else {
		g_print ("SKIP: the Tab walk, since the dialog never got the keyboard\n");
	}

	/* Views' tabs on top went with 2026101009150769, and with them this
	   check and the one below that the keys inside them stayed theirs.
	gtk_widget_grab_focus (notebook);
	check (!is_a_tab (gtk_window_get_focus (GTK_WINDOW (dialog)), sidebar)); */

	/* The list steps with Ctrl+Page Up and Down, and wraps round. Views
	   has sub-tabs now, so the first step stays on it. */
	gtk_widget_grab_focus (remember);
	check (g_strcmp0 (gtk_stack_get_visible_child_name (GTK_STACK (stack)), "views") == 0);
	send_key (dialog, GDK_KEY_Page_Down, GDK_CONTROL_MASK);
	/* check (g_strcmp0 (gtk_stack_get_visible_child_name (GTK_STACK (stack)), "behavior") == 0); */
	send_key (dialog, GDK_KEY_Page_Down, GDK_CONTROL_MASK);
	check (g_strcmp0 (gtk_stack_get_visible_child_name (GTK_STACK (stack)), "behavior") == 0);
	send_key (dialog, GDK_KEY_Page_Up, GDK_CONTROL_MASK);
	send_key (dialog, GDK_KEY_Page_Up, GDK_CONTROL_MASK);
	check (g_strcmp0 (gtk_stack_get_visible_child_name (GTK_STACK (stack)), "views") == 0);
	send_key (dialog, GDK_KEY_Page_Up, GDK_CONTROL_MASK);
	check (g_strcmp0 (gtk_stack_get_visible_child_name (GTK_STACK (stack)), "keyboard") == 0);
	send_key (dialog, GDK_KEY_Page_Down, GDK_CONTROL_MASK);
	check (g_strcmp0 (gtk_stack_get_visible_child_name (GTK_STACK (stack)), "views") == 0);

	/* The Views tabs check went here.
	gtk_notebook_set_current_page (GTK_NOTEBOOK (notebook), 0);
	gtk_widget_child_focus (gtk_notebook_get_nth_page (GTK_NOTEBOOK (notebook), 0), GTK_DIR_TAB_FORWARD);
	if (send_key (dialog, GDK_KEY_Page_Down, GDK_CONTROL_MASK)) {
		check (gtk_notebook_get_current_page (GTK_NOTEBOOK (notebook)) == 1);
	} else {
		g_print ("SKIP: Ctrl+Page Down in the Views tabs, since the key has no keycode\n");
	}
	check (g_strcmp0 (gtk_stack_get_visible_child_name (GTK_STACK (stack)), "views") == 0);
	gtk_notebook_set_current_page (GTK_NOTEBOOK (notebook), 0); */

	g_ptr_array_free (rows, TRUE);
	g_hash_table_destroy (seen);
}

/* The page showing, as stack names from the top down, such as
   "behavior/behavior-files". */
static char *
showing (GtkWidget *stack)
{
	GString *path = g_string_new (NULL);

	while (GTK_IS_STACK (stack)) {
		const char *name = gtk_stack_get_visible_child_name (GTK_STACK (stack));
		GtkWidget *page = gtk_stack_get_visible_child (GTK_STACK (stack));

		if (path->len > 0) {
			g_string_append_c (path, '/');
		}
		g_string_append (path, name != NULL ? name : "?");
		/* Views' sub-tabs sit under the switches shared by both. */
		stack = GTK_IS_STACK (page) ? page : find_named (page, "views_stack");
	}

	return g_string_free (path, FALSE);
}

static void
check_showing (GtkWidget *stack, const char *expected)
{
	char *path = showing (stack);

	if (g_strcmp0 (path, expected) != 0) {
		g_print ("FAIL: showing %s, expected %s\n", path, expected);
		failures++;
	}
	g_free (path);
}

/* The rows with the selected look, by their text, such as "Views|Default". */
static char *
lit_rows (GtkWidget *sidebar)
{
	GPtrArray *rows = g_ptr_array_new ();
	GString *lit = g_string_new (NULL);
	guint i;

	collect_rows (sidebar, rows);
	for (i = 0; i < rows->len; i++) {
		GtkWidget *row = rows->pdata[i];

		if (!GTK_IS_LIST_BOX_ROW (row) ||
		    !(gtk_widget_get_state_flags (row) & GTK_STATE_FLAG_SELECTED)) {
			continue;
		}
		if (lit->len > 0) {
			g_string_append_c (lit, '|');
		}
		g_string_append (lit, gtk_label_get_text (GTK_LABEL (gtk_bin_get_child (GTK_BIN (row)))));
	}
	g_ptr_array_free (rows, TRUE);

	return g_string_free (lit, FALSE);
}

static void
check_lit (GtkWidget *sidebar, const char *expected)
{
	char *lit = lit_rows (sidebar);

	if (g_strcmp0 (lit, expected) != 0) {
		g_print ("FAIL: lit rows %s, expected %s\n", lit, expected);
		failures++;
	}
	g_free (lit);
}

static GtkWidget *
row_titled (GtkWidget *sidebar, const char *title)
{
	GPtrArray *rows = g_ptr_array_new ();
	GtkWidget *found = NULL;
	guint i;

	collect_rows (sidebar, rows);
	for (i = 0; i < rows->len && found == NULL; i++) {
		if (GTK_IS_LIST_BOX_ROW (rows->pdata[i]) &&
		    g_strcmp0 (gtk_label_get_text (GTK_LABEL (gtk_bin_get_child (rows->pdata[i]))), title) == 0) {
			found = rows->pdata[i];
		}
	}
	g_ptr_array_free (rows, TRUE);

	return found;
}

static void
click_row (GtkWidget *sidebar, const char *title)
{
	GtkWidget *row = row_titled (sidebar, title);

	check (row != NULL);
	if (row != NULL) {
		g_signal_emit_by_name (gtk_widget_get_parent (row), "row-activated", row);
		settle ();
	}
}

/* 2026101009150767: Behavior is split into sub-tabs, and at the size it
   opens no page needs a scrollbar. 2026101009150769: sub-tabs are rows in
   the list, indented, and a parent and its showing sub-tab are both lit.
   2026101009150770: the keys go through sub-tabs before the next page. */
static void
test_sub_tabs (GtkWidget *dialog)
{
	GtkWidget *sidebar = find_named (dialog, "page_sidebar");
	GtkWidget *stack = find_named (dialog, "page_stack");
	GtkWidget *behavior = find_named (dialog, "behavior_stack");
	GtkWidget *row, *sub, *focus;
	GList *pages;
	guint count;
	int width = 0, height = 0;
	GdkRectangle work;

	check (sidebar != NULL && stack != NULL && behavior != NULL);
	if (sidebar == NULL || stack == NULL || behavior == NULL) {
		return;
	}

	pages = gtk_container_get_children (GTK_CONTAINER (behavior));
	count = g_list_length (pages);
	g_list_free (pages);
	g_print ("Behavior has %u sub-tabs\n", count);
	check (count >= 2 && count <= 4);

	/* Indented under the parent. */
	row = row_titled (sidebar, "Behavior");
	sub = row_titled (sidebar, "General");
	check (row != NULL && sub != NULL);
	if (row != NULL && sub != NULL) {
		check (gtk_widget_get_margin_start (gtk_bin_get_child (GTK_BIN (sub))) >
		       gtk_widget_get_margin_start (gtk_bin_get_child (GTK_BIN (row))));
	}

	check_showing (stack, "views/views-default");
	check_lit (sidebar, "Views|Default");

	click_row (sidebar, "Files and Trash");
	check_showing (stack, "behavior/behavior-files");
	check_lit (sidebar, "Behavior|Files and Trash");

	/* A parent opens on the sub-tab it last showed. */
	click_row (sidebar, "Display");
	check_lit (sidebar, "Display");
	click_row (sidebar, "Behavior");
	check_showing (stack, "behavior/behavior-files");
	check_lit (sidebar, "Behavior|Files and Trash");

	click_row (sidebar, "Current");
	check_showing (stack, "views/views-current");
	check_lit (sidebar, "Views|Current");

	/* Ctrl+Tab steps the tabs rather than the focus. Close sits outside the
	   pages, so a page switch leaves it the focus. */
	click_row (sidebar, "Default");
	focus = find_named (dialog, "close_button");
	gtk_widget_grab_focus (focus);
	check (gtk_window_get_focus (GTK_WINDOW (dialog)) == focus);
	send_key (dialog, GDK_KEY_Tab, GDK_CONTROL_MASK);
	check_showing (stack, "views/views-current");
	check (gtk_window_get_focus (GTK_WINDOW (dialog)) == focus);
	send_key (dialog, GDK_KEY_ISO_Left_Tab, GDK_CONTROL_MASK | GDK_SHIFT_MASK);
	check_showing (stack, "views/views-default");

	/* Past the last sub-tab is the next page, and back from the first
	   sub-tab is the last sub-tab of the page before. */
	click_row (sidebar, "Media and programs");
	send_key (dialog, GDK_KEY_Page_Down, GDK_CONTROL_MASK);
	check_showing (stack, "display");
	send_key (dialog, GDK_KEY_Page_Up, GDK_CONTROL_MASK);
	check_showing (stack, "behavior/behavior-programs");
	click_row (sidebar, "General");
	send_key (dialog, GDK_KEY_Tab, GDK_CONTROL_MASK | GDK_SHIFT_MASK);
	check_showing (stack, "views/views-current");
	check_lit (sidebar, "Views|Current");

	/* A hidden page has no row and the keys pass it by. */
	click_row (sidebar, "Toolbar");
	gtk_widget_hide (find_named (dialog, "scrolledwindow5"));
	settle ();
	check (row_titled (sidebar, "Preview") != NULL &&
	       !gtk_widget_get_visible (row_titled (sidebar, "Preview")));
	send_key (dialog, GDK_KEY_Page_Up, GDK_CONTROL_MASK);
	check_showing (stack, "list-columns/list-columns-search");
	gtk_widget_show (find_named (dialog, "scrolledwindow5"));
	settle ();
	check (gtk_widget_get_visible (row_titled (sidebar, "Preview")));

	/* Every page fits at the opening size, unless the screen kept the
	   window back. */
	gtk_window_get_size (GTK_WINDOW (dialog), &width, &height);
	nemo_desktop_utils_get_monitor_work_rect (nemo_desktop_utils_get_primary_monitor (), &work);
	if (height < work.height * 9 / 10) {
		const char *const leaves[] = {
			"views_default_page", "views_current_page", "scrolledwindow1",
			"behavior_files_scrolledwindow", "behavior_programs_scrolledwindow",
			"scrolledwindow3", "windows_scrolledwindow", "scrolledwindow4", "list_columns_search_scrolledwindow",
			"scrolledwindow5", "scrolledwindow6",
			"scrolledwindow8", "templates_scrolledwindow", "scrolledwindow7", NULL
		};
		int i;

		for (i = 0; leaves[i] != NULL; i++) {
			GtkWidget *page = find_named (dialog, leaves[i]);
			GtkWidget *scrolled;
			GtkAdjustment *adj;

			check (page != NULL);
			/* Windows is a page on Windows only. */
			if (page == NULL || !gtk_widget_get_visible (page)) {
				continue;
			}
			nemo_tab_list_show_page (NEMO_TAB_LIST (sidebar), page);
			settle ();
			scrolled = GTK_IS_SCROLLED_WINDOW (page) ? page : gtk_widget_get_ancestor (page, GTK_TYPE_SCROLLED_WINDOW);
			adj = gtk_scrolled_window_get_vadjustment (GTK_SCROLLED_WINDOW (scrolled));
			if (gtk_adjustment_get_upper (adj) > gtk_adjustment_get_page_size (adj) + 1) {
				g_print ("FAIL: %s needs %.0f of %.0f at %dx%d\n", leaves[i],
					 gtk_adjustment_get_upper (adj), gtk_adjustment_get_page_size (adj),
					 width, height);
				failures++;
			}
		}
	} else {
		g_print ("SKIP: the fit check, since the window is at the screen's cap\n");
	}

	click_row (sidebar, "Default");
}

static GtkWidget *
find_chooser (GtkWidget *widget)
{
	GList *children, *l;
	GtkWidget *found = NULL;

	if (NEMO_IS_COLUMN_CHOOSER (widget)) {
		return widget;
	}
	if (!GTK_IS_CONTAINER (widget)) {
		return NULL;
	}
	children = gtk_container_get_children (GTK_CONTAINER (widget));
	for (l = children; l != NULL && found == NULL; l = l->next) {
		found = find_chooser (l->data);
	}
	g_list_free (children);
	return found;
}

static gboolean
strv_has (char **strv, const char *name)
{
	return strv != NULL && g_strv_contains ((const char * const *) strv, name);
}

/* 2026110520234231: List columns has a sub-tab for each list, each with its
   own columns. Only find results have a Hits column. */
static void
test_list_columns_tabs (GtkWidget *dialog)
{
	const char *const titles[] = { "Regular list view", "Search list view" };
	const char *const paths[] = { "list-columns/list-columns-regular",
				      "list-columns/list-columns-search" };
	GtkWidget *sidebar = find_named (dialog, "page_sidebar");
	GtkWidget *stack = find_named (dialog, "page_stack");
	GtkWidget *sub = find_named (dialog, "list_columns_stack");
	GList *pages;
	int i;

	check (sidebar != NULL && stack != NULL && sub != NULL && GTK_IS_STACK (sub));
	if (sidebar == NULL || stack == NULL || sub == NULL || !GTK_IS_STACK (sub)) {
		return;
	}
	check (gtk_widget_get_parent (sub) == stack);

	pages = gtk_container_get_children (GTK_CONTAINER (sub));
	check (g_list_length (pages) == 2);
	for (i = 0; i < 2 && g_list_nth (pages, i) != NULL; i++) {
		GtkWidget *page = g_list_nth_data (pages, i);
		GtkWidget *chooser = find_chooser (page);
		char *title = NULL;
		char **visible = NULL, **order = NULL;

		gtk_container_child_get (GTK_CONTAINER (sub), page, "title", &title, NULL);
		g_print ("List columns sub-tab %d is \"%s\"\n", i, title);
		check (g_strcmp0 (title, titles[i]) == 0);
		g_free (title);

		check (chooser != NULL);
		if (chooser != NULL) {
			nemo_column_chooser_get_settings (NEMO_COLUMN_CHOOSER (chooser), &visible, &order);
			check (strv_has (order, "name"));
			check (strv_has (order, "search_result_count") == (i == 1));
			if (i == 1) {
				char **want = nemo_search_columns_get_visible ();

				check (g_strv_equal ((const char * const *) visible, (const char * const *) want));
				g_strfreev (want);
			}
			g_strfreev (visible);
			g_strfreev (order);
		}

		check (row_titled (sidebar, titles[i]) != NULL);
		click_row (sidebar, titles[i]);
		check_showing (stack, paths[i]);
		check_lit (sidebar, i == 0 ? "List columns|Regular list view" : "List columns|Search list view");
	}
	g_list_free (pages);

	click_row (sidebar, "Default");
}

/* The field shows the renamer found here, showing it writes nothing, and the
   label and the field both list the known ones. */
static void
test_bulk_rename (GtkWidget *dialog)
{
	GtkWidget *entry = find_named (dialog, "bulk_rename_entry");
	GtkWidget *label = find_named (dialog, "command_label_0");
	char *found = nemo_bulk_rename_find_default ();
	char *known = nemo_bulk_rename_known_commands ();
	char *tip;

	check (entry != NULL && label != NULL);
	if (entry == NULL || label == NULL) {
		goto out;
	}
	check (g_strcmp0 (gtk_entry_get_text (GTK_ENTRY (entry)), found) == 0);
	check (!nemo_config_is_set (nemo_preferences, NEMO_PREFERENCES_BULK_RENAME_TOOL));

	tip = gtk_widget_get_tooltip_text (entry);
	check (tip != NULL && g_str_has_prefix (tip, "Renamer programs need only accept a quoted list of files or folders."));
	check (tip != NULL && g_str_has_suffix (tip, known));
	g_free (tip);
	tip = gtk_widget_get_tooltip_text (label);
	check (tip != NULL && g_str_has_suffix (tip, known));
	g_free (tip);

out:
	g_free (known);
	g_free (found);
}

/* The selection model dropdown is on Advanced and lists only the models that
   are built, Nemo Classic and Nemo Anywhere v1 so far, in the design doc's
   order. */
static void
test_selection_model (GtkWidget *dialog)
{
	GtkWidget *combo = find_named (dialog, "selection_model_combobox");
	GtkWidget *page = find_named (dialog, "advanced_scrolledwindow");
	char *text;

	check (combo != NULL && page != NULL);
	if (combo == NULL || page == NULL) {
		return;
	}
	check (gtk_widget_is_ancestor (combo, page));
	/* The design doc's order: Nemo Anywhere, Nemo Classic, Nemo Anywhere v1, with Nemo Anywhere the default. */
	check (gtk_tree_model_iter_n_children (gtk_combo_box_get_model (GTK_COMBO_BOX (combo)), NULL) == 3);
	check (gtk_combo_box_get_active (GTK_COMBO_BOX (combo)) == 0);
	text = gtk_combo_box_text_get_active_text (GTK_COMBO_BOX_TEXT (combo));
	check (g_strcmp0 (text, "Nemo Anywhere") == 0);
	g_free (text);
	check (!nemo_config_is_set (nemo_preferences, NEMO_PREFERENCES_SELECTION_MODEL));
	check (nemo_global_preferences_get_selection_model () == NEMO_SELECTION_MODEL_ANYWHERE);

	gtk_combo_box_set_active (GTK_COMBO_BOX (combo), 1);
	text = gtk_combo_box_text_get_active_text (GTK_COMBO_BOX_TEXT (combo));
	check (g_strcmp0 (text, "Nemo Classic") == 0);
	g_free (text);
	text = nemo_config_get_string (nemo_preferences, NEMO_PREFERENCES_SELECTION_MODEL);
	check (g_strcmp0 (text, "nemo-classic") == 0);
	g_free (text);
	check (nemo_global_preferences_get_selection_model () == NEMO_SELECTION_MODEL_CLASSIC);

	gtk_combo_box_set_active (GTK_COMBO_BOX (combo), 2);
	text = gtk_combo_box_text_get_active_text (GTK_COMBO_BOX_TEXT (combo));
	check (g_strcmp0 (text, "Nemo Anywhere v1") == 0);
	g_free (text);
	check (nemo_global_preferences_get_selection_model () == NEMO_SELECTION_MODEL_ANYWHERE_V1);

	nemo_config_reset (nemo_preferences, NEMO_PREFERENCES_SELECTION_MODEL);
	check (gtk_combo_box_get_active (GTK_COMBO_BOX (combo)) == 0);
	check (nemo_global_preferences_get_selection_model () == NEMO_SELECTION_MODEL_ANYWHERE);

	/* A settings file that names a model keeps it. */
	nemo_config_set_string (nemo_preferences, NEMO_PREFERENCES_SELECTION_MODEL, "nemo-anywhere-v1");
	check (gtk_combo_box_get_active (GTK_COMBO_BOX (combo)) == 2);
	nemo_config_reset (nemo_preferences, NEMO_PREFERENCES_SELECTION_MODEL);
	check (nemo_global_preferences_get_selection_model () == NEMO_SELECTION_MODEL_ANYWHERE);
}

/* The default view lists the views in the View menu's order, and each row
   writes its own view. */
static void
test_default_view (GtkWidget *dialog)
{
	static const struct {
		const char *label;
		const char *value;
	} rows[] = {
		{ "Detailed view", "list-view" },
		{ "Compact view", "compact-view" },
		{ "Icon view", "icon-view" },
		{ "Image thumbnail view", "image-view" },
	};
	GtkWidget *combo = find_named (dialog, "default_view_combobox");
	GtkTreeModel *model;
	GtkTreeIter iter;
	char *text;
	guint i;

	check (combo != NULL);
	if (combo == NULL) {
		return;
	}
	model = gtk_combo_box_get_model (GTK_COMBO_BOX (combo));
	check (gtk_tree_model_iter_n_children (model, NULL) == (int) G_N_ELEMENTS (rows));
	check (!nemo_config_is_set (nemo_preferences, NEMO_PREFERENCES_DEFAULT_FOLDER_VIEWER));
	check (gtk_combo_box_get_active (GTK_COMBO_BOX (combo)) == 0);

	for (i = 0; i < G_N_ELEMENTS (rows); i++) {
		text = NULL;
		if (gtk_tree_model_iter_nth_child (model, &iter, NULL, (int) i)) {
			gtk_tree_model_get (model, &iter, 0, &text, -1);
		}
		check (g_strcmp0 (text, rows[i].label) == 0);
		g_free (text);

		gtk_combo_box_set_active (GTK_COMBO_BOX (combo), (int) i);
		text = nemo_config_get_string (nemo_preferences, NEMO_PREFERENCES_DEFAULT_FOLDER_VIEWER);
		check (g_strcmp0 (text, rows[i].value) == 0);
		g_free (text);
	}

	nemo_config_reset (nemo_preferences, NEMO_PREFERENCES_DEFAULT_FOLDER_VIEWER);
	check (gtk_combo_box_get_active (GTK_COMBO_BOX (combo)) == 0);
}

/* Where pasted and new items go is on Advanced, under the selection model, with
   its 3 models in the design doc's order and the picked one's tooltip. */
static void
test_paste_target (GtkWidget *dialog)
{
	GtkWidget *combo = find_named (dialog, "paste_target_combobox");
	GtkWidget *above = find_named (dialog, "selection_model_combobox");
	GtkWidget *page = find_named (dialog, "advanced_scrolledwindow");
	GtkAllocation a, b;
	char *text;

	check (combo != NULL && above != NULL && page != NULL);
	if (combo == NULL || above == NULL || page == NULL) {
		return;
	}
	check (gtk_widget_is_ancestor (combo, page));
	check (gtk_widget_get_parent (gtk_widget_get_parent (gtk_widget_get_parent (combo))) ==
	       gtk_widget_get_parent (gtk_widget_get_parent (gtk_widget_get_parent (above))));
	gtk_widget_get_allocation (above, &a);
	gtk_widget_get_allocation (combo, &b);
	check (!gtk_widget_get_mapped (combo) || b.y > a.y);

	check (gtk_tree_model_iter_n_children (gtk_combo_box_get_model (GTK_COMBO_BOX (combo)), NULL) == 3);
	check (gtk_combo_box_get_active (GTK_COMBO_BOX (combo)) == 0);
	text = gtk_combo_box_text_get_active_text (GTK_COMBO_BOX_TEXT (combo));
	check (g_strcmp0 (text, "Nemo Anywhere") == 0);
	g_free (text);
	text = gtk_widget_get_tooltip_text (combo);
	check (text != NULL && g_str_has_prefix (text, "If a selected folder is expanded"));
	g_free (text);
	check (!nemo_config_is_set (nemo_preferences, NEMO_PREFERENCES_PASTE_TARGET));
	check (nemo_global_preferences_get_paste_target () == NEMO_PASTE_TARGET_ANYWHERE);

	gtk_combo_box_set_active (GTK_COMBO_BOX (combo), 1);
	text = gtk_combo_box_text_get_active_text (GTK_COMBO_BOX_TEXT (combo));
	check (g_strcmp0 (text, "Nemo Classic") == 0);
	g_free (text);
	text = nemo_config_get_string (nemo_preferences, NEMO_PREFERENCES_PASTE_TARGET);
	check (g_strcmp0 (text, "nemo-classic") == 0);
	g_free (text);
	text = gtk_widget_get_tooltip_text (combo);
	check (text != NULL && g_str_has_prefix (text, "Like upstream Nemo."));
	g_free (text);
	check (nemo_global_preferences_get_paste_target () == NEMO_PASTE_TARGET_CLASSIC);

	gtk_combo_box_set_active (GTK_COMBO_BOX (combo), 2);
	text = gtk_combo_box_text_get_active_text (GTK_COMBO_BOX_TEXT (combo));
	check (g_strcmp0 (text, "macOS/Dolphin") == 0);
	g_free (text);
	text = gtk_widget_get_tooltip_text (combo);
	check (text != NULL && g_str_has_prefix (text, "New items always go in the viewed folder"));
	g_free (text);
	check (nemo_global_preferences_get_paste_target () == NEMO_PASTE_TARGET_VIEWED_FOLDER);

	nemo_config_reset (nemo_preferences, NEMO_PREFERENCES_PASTE_TARGET);
	check (gtk_combo_box_get_active (GTK_COMBO_BOX (combo)) == 0);
	check (nemo_global_preferences_get_paste_target () == NEMO_PASTE_TARGET_ANYWHERE);
}

/* The whole-name rename box is grayed under Nemo Anywhere, where F2 and
   Shift+F2 are fixed, with a tooltip saying so, and comes back with the model. */
static void
test_rename_selects_grayed (GtkWidget *dialog)
{
	GtkWidget *check_button = find_named (dialog, "rename_selects_whole_name_checkbutton");
	char *tip;

	check (check_button != NULL);
	if (check_button == NULL) {
		return;
	}
	/* Nemo Anywhere is the default. */
	check (!gtk_widget_get_sensitive (check_button));
	tip = gtk_widget_get_tooltip_text (check_button);
	check (tip != NULL && strstr (tip, "Shift+F2") != NULL);
	g_free (tip);

	nemo_config_set_string (nemo_preferences, NEMO_PREFERENCES_SELECTION_MODEL, "nemo-anywhere-v1");
	check (gtk_widget_get_sensitive (check_button));
	tip = gtk_widget_get_tooltip_text (check_button);
	check (tip != NULL && strstr (tip, "Shift+F2") == NULL);
	g_free (tip);

	nemo_config_reset (nemo_preferences, NEMO_PREFERENCES_SELECTION_MODEL);
	check (!gtk_widget_get_sensitive (check_button));
}

/* The rows of the search's match list, as plain text. */
static GPtrArray *
match_rows (GtkWidget *dialog)
{
	GPtrArray *rows = g_ptr_array_new ();
	GPtrArray *texts = g_ptr_array_new_with_free_func (g_free);
	guint i;

	collect_rows (find_named (dialog, "settings_search_results"), rows);
	for (i = 0; i < rows->len; i++) {
		GtkWidget *row = rows->pdata[i];

		if (GTK_IS_LIST_BOX_ROW (row) && gtk_widget_get_visible (row)) {
			g_ptr_array_add (texts, g_strdup (gtk_label_get_text (GTK_LABEL (gtk_bin_get_child (GTK_BIN (row))))));
		}
	}
	g_ptr_array_free (rows, TRUE);

	return texts;
}

/* 2026111912460963: a search field over the pages, outside the scrolling.
   Matches as typed come first, each with its path and the matched letters
   marked. Enter goes to the first, Escape empties the field before it closes
   anything, Ctrl+F goes to the field, and picking a tab leaves the search. */
static void
test_search (GtkWidget *dialog)
{
	GtkWidget *entry = find_named (dialog, "settings_search_entry");
	GtkWidget *results = find_named (dialog, "settings_search_scroll");
	GtkWidget *stack = find_named (dialog, "page_stack");
	GtkWidget *sidebar = find_named (dialog, "page_sidebar");
	GtkWidget *reverse = find_named (dialog, "reverse_sort_checkbox");
	GtkListBoxRow *first;
	GPtrArray *texts;
	GtkAllocation entry_at, stack_at;

	check (entry != NULL && results != NULL && stack != NULL && reverse != NULL);
	if (entry == NULL || results == NULL || stack == NULL || reverse == NULL) {
		return;
	}

	check (gtk_widget_get_ancestor (entry, GTK_TYPE_SCROLLED_WINDOW) == NULL);
	gtk_widget_get_allocation (entry, &entry_at);
	gtk_widget_get_allocation (stack, &stack_at);
	check (entry_at.y + entry_at.height <= stack_at.y);
	check (!gtk_widget_get_visible (results));

	gtk_entry_set_text (GTK_ENTRY (entry), "reverse sort");
	settle ();
	check (gtk_widget_get_visible (results));
	check (!gtk_widget_get_visible (stack));
	texts = match_rows (dialog);
	check (texts->len >= 2);
	if (texts->len >= 2) {
		g_print ("first match: %s\n", (char *) texts->pdata[0]);
		check (strcmp (texts->pdata[0], "Views \xe2\x8f\xb5 Default \xe2\x8f\xb5 View \xe2\x8f\xb5 Reverse sort") == 0);
		check (g_str_has_prefix (texts->pdata[1], "Views \xe2\x8f\xb5 Current"));
	}
	g_ptr_array_free (texts, TRUE);

	first = gtk_list_box_get_row_at_index (GTK_LIST_BOX (find_named (dialog, "settings_search_results")), 0);
	check (first != NULL);
	if (first != NULL) {
		const char *markup = gtk_label_get_label (GTK_LABEL (gtk_bin_get_child (GTK_BIN (first))));

		check (strstr (markup, "weight=\"bold\" size=\"larger\"") != NULL);
		check (strstr (markup, ">Reverse sort</span>") != NULL);
	}

	/* Matched as typed, then loosely. */
	gtk_entry_set_text (GTK_ENTRY (entry), "rvrs srt");
	settle ();
	texts = match_rows (dialog);
	check (texts->len >= 1);
	if (texts->len >= 1) {
		check (g_str_has_suffix (texts->pdata[0], "Reverse sort"));
	}
	g_ptr_array_free (texts, TRUE);

	gtk_entry_set_text (GTK_ENTRY (entry), "zqxjzqxj");
	settle ();
	texts = match_rows (dialog);
	check (texts->len == 0);
	g_ptr_array_free (texts, TRUE);

	/* Enter goes to the first match, and leaves the search. */
	click_row (sidebar, "Advanced");
	gtk_entry_set_text (GTK_ENTRY (entry), "reverse sort");
	settle ();
	g_signal_emit_by_name (entry, "activate");
	settle ();
	check (*gtk_entry_get_text (GTK_ENTRY (entry)) == '\0');
	check (gtk_widget_get_visible (stack));
	check (!gtk_widget_get_visible (results));
	check_showing (stack, "views/views-default");
	check (gtk_window_get_focus (GTK_WINDOW (dialog)) == reverse);
	check (gtk_style_context_has_class (gtk_widget_get_style_context (reverse), "nemo-settings-found"));

	/* A click on a match does the same. */
	gtk_entry_set_text (GTK_ENTRY (entry), "reverse sort");
	settle ();
	{
		GtkWidget *box = find_named (dialog, "settings_search_results");
		GtkListBoxRow *second = gtk_list_box_get_row_at_index (GTK_LIST_BOX (box), 1);

		check (second != NULL);
		if (second != NULL) {
			g_signal_emit_by_name (box, "row-activated", second);
			settle ();
			check (*gtk_entry_get_text (GTK_ENTRY (entry)) == '\0');
			check (!gtk_widget_get_visible (results));
			check_showing (stack, "views/views-current");
		}
	}

	/* Picking a tab leaves the search too. */
	gtk_entry_set_text (GTK_ENTRY (entry), "trash");
	settle ();
	click_row (sidebar, "Behavior");
	check (*gtk_entry_get_text (GTK_ENTRY (entry)) == '\0');
	check (gtk_widget_get_visible (stack));
	check (!gtk_widget_get_visible (results));

	send_key (dialog, GDK_KEY_f, GDK_CONTROL_MASK);
	check (gtk_window_get_focus (GTK_WINDOW (dialog)) == entry);

	/* The first Escape only empties the field. */
	g_object_add_weak_pointer (G_OBJECT (dialog), (gpointer *) &dialog);
	gtk_entry_set_text (GTK_ENTRY (entry), "trash");
	settle ();
	check (send_key (dialog, GDK_KEY_Escape, 0));
	check (dialog != NULL);
	if (dialog != NULL) {
		check (*gtk_entry_get_text (GTK_ENTRY (entry)) == '\0');
		check (gtk_widget_get_visible (stack));
		g_object_remove_weak_pointer (G_OBJECT (dialog), (gpointer *) &dialog);
	}
}

static GtkWidget *
find_bound (GtkWidget *widget, const char *key)
{
	GList *children, *l;
	GtkWidget *found = NULL;

	if (g_strcmp0 (g_object_get_data (G_OBJECT (widget), NEMO_CONFIG_BOUND_KEY), key) == 0) {
		return widget;
	}
	if (!GTK_IS_CONTAINER (widget)) {
		return NULL;
	}
	children = gtk_container_get_children (GTK_CONTAINER (widget));
	for (l = children; l != NULL && found == NULL; l = l->next) {
		found = find_bound (l->data, key);
	}
	g_list_free (children);

	return found;
}

/* On a page this platform shows. */
static gboolean
shown_under (GtkWidget *widget, GtkWidget *top)
{
	for (; widget != NULL && widget != top; widget = gtk_widget_get_parent (widget)) {
		if (!gtk_widget_get_visible (widget)) {
			return FALSE;
		}
	}
	return widget == top;
}

/* 2026111912460983: every setting with aliases in the schema has a control
   the search finds, and gets them once. An alias typed lists its setting with
   the alias after the label. */
static void
test_search_aliases (GtkWidget *dialog)
{
	GtkWidget *entry = find_named (dialog, "settings_search_entry");
	GtkWidget *stack = find_named (dialog, "page_stack");
	GPtrArray *entries, *texts;
	guint i, j;

	check (entry != NULL && stack != NULL);
	if (entry == NULL || stack == NULL) {
		return;
	}

	entries = nemo_settings_search_collect (stack);
	for (i = 0; settings_aliases[i].key != NULL; i++) {
		const char *key = settings_aliases[i].key;
		GtkWidget *control;
		guint given = 0;

		/* Its page is stubbed out above. */
		if (g_str_has_prefix (key, "file-cache.")) {
			continue;
		}
		control = find_bound (dialog, key);
		if (control == NULL) {
			g_printerr ("%s has aliases and no control\n", key);
			check (control != NULL);
			continue;
		}
		if (!shown_under (control, stack)) {
			continue;
		}
		for (j = 0; j < entries->len; j++) {
			NemoSettingsEntry *e = entries->pdata[j];

			if (e->aliases != NULL &&
			    g_strcmp0 (g_object_get_data (G_OBJECT (e->target), NEMO_CONFIG_BOUND_KEY), key) == 0) {
				given++;
				check (g_strv_equal ((const char *const *) e->aliases, settings_aliases[i].aliases));
			}
		}
		if (given != 1) {
			g_printerr ("%s's aliases are on %u entries\n", key, given);
		}
		check (given == 1);
	}
	g_ptr_array_free (entries, TRUE);

	gtk_entry_set_text (GTK_ENTRY (entry), "zebra stripes");
	settle ();
	texts = match_rows (dialog);
	check (texts->len >= 1);
	if (texts->len >= 1) {
		g_print ("alias match: %s\n", (char *) texts->pdata[0]);
		check (g_str_has_suffix (texts->pdata[0], " (zebra stripes)"));
	}
	g_ptr_array_free (texts, TRUE);
	gtk_entry_set_text (GTK_ENTRY (entry), "");
	settle ();
}

static void
noop (void)
{
}

/* Like a window's menus: a key loaded from the file before the actions are
   added, which is the order the app does it in. */
static const GtkActionEntry kb_entries[] = {
	{ "Menu", NULL, "_Menu", NULL, NULL, NULL },
	{ "Cut", NULL, "Cu_t", "<control>x", "Cut the selection", G_CALLBACK (noop) },
	{ "Trash", NULL, "Mo_ve to Trash", "Delete", NULL, G_CALLBACK (noop) },
	{ "Rename", NULL, "_Rename...", "F2", NULL, G_CALLBACK (noop) },
	{ "Bare", NULL, "_Bare", NULL, NULL, G_CALLBACK (noop) },
};

static const GtkToggleActionEntry kb_toggles[] = {
	{ "Hidden", NULL, "Show _Hidden Files", "<control>h", NULL, G_CALLBACK (noop), FALSE },
};

static GtkActionGroup *kb_group;

static void
add_test_shortcuts (void)
{
	gtk_accel_map_change_entry ("<Actions>/TestActions/Rename", GDK_KEY_F6, 0, TRUE);

	kb_group = gtk_action_group_new ("TestActions");
	gtk_action_group_add_actions (kb_group, kb_entries, G_N_ELEMENTS (kb_entries), NULL);
	nemo_shortcuts_note_actions (kb_group, kb_entries, G_N_ELEMENTS (kb_entries));
	gtk_action_group_add_toggle_actions (kb_group, kb_toggles, G_N_ELEMENTS (kb_toggles), NULL);
	nemo_shortcuts_note_toggle_actions (kb_group, kb_toggles, G_N_ELEMENTS (kb_toggles));
}

static GtkWidget *
find_widget_named (GtkWidget *widget, const char *name)
{
	GList *children, *l;
	GtkWidget *found = NULL;

	if (g_strcmp0 (gtk_widget_get_name (widget), name) == 0) {
		return widget;
	}
	if (!GTK_IS_CONTAINER (widget)) {
		return NULL;
	}

	children = gtk_container_get_children (GTK_CONTAINER (widget));
	for (l = children; l != NULL && found == NULL; l = l->next) {
		found = find_widget_named (l->data, name);
	}
	g_list_free (children);

	return found;
}

static char *
kb_row (GtkTreeModel *model, const char *name, guint *key, GdkModifierType *mods, char **reset)
{
	g_autofree char *accel_path = g_strconcat ("<Actions>/TestActions/", name, NULL);
	GtkTreeIter iter;
	gboolean more;

	for (more = gtk_tree_model_get_iter_first (model, &iter); more; more = gtk_tree_model_iter_next (model, &iter)) {
		g_autofree char *row_path = NULL;

		gtk_tree_model_get (model, &iter, NEMO_PREFS_KEYBOARD_COL_PATH, &row_path, -1);
		if (g_strcmp0 (row_path, accel_path) == 0) {
			gtk_tree_model_get (model, &iter,
					    NEMO_PREFS_KEYBOARD_COL_KEY, key,
					    NEMO_PREFS_KEYBOARD_COL_MODS, mods,
					    NEMO_PREFS_KEYBOARD_COL_RESET, reset,
					    -1);
			return gtk_tree_model_get_string_from_iter (model, &iter);
		}
	}

	return NULL;
}

static gboolean
kb_is (GtkTreeModel *model, const char *name, guint want_key, GdkModifierType want_mods, gboolean want_reset)
{
	g_autofree char *accel_path = g_strconcat ("<Actions>/TestActions/", name, NULL);
	g_autofree char *row = NULL;
	g_autofree char *reset = NULL;
	GtkAccelKey current = { 0, 0, 0 };
	guint key = 0;
	GdkModifierType mods = 0;
	gboolean ok;

	row = kb_row (model, name, &key, &mods, &reset);
	gtk_accel_map_lookup_entry (accel_path, &current);
	ok = row != NULL && key == want_key && mods == want_mods && (reset != NULL) == want_reset &&
		current.accel_key == want_key && current.accel_mods == want_mods;
	if (!ok) {
		g_print ("%s: row %u/%x, map %u/%x, reset %s; wanted %u/%x\n", name, key, mods,
			 current.accel_key, current.accel_mods, reset != NULL ? "shown" : "hidden", want_key, want_mods);
	}

	return ok;
}

/* As a person does it: Enter on the row, then the keys. */
static void
kb_edit (GtkWidget *dialog, GtkWidget *tree, GtkTreeModel *model, const char *name, guint key, GdkModifierType mods)
{
	g_autofree char *reset = NULL;
	guint k;
	GdkModifierType m;
	g_autofree char *row = kb_row (model, name, &k, &m, &reset);
	GtkTreePath *path;

	if (row == NULL) {
		return;
	}
	path = gtk_tree_path_new_from_string (row);
	gtk_tree_view_set_cursor (GTK_TREE_VIEW (tree), path, NULL, FALSE);
	gtk_tree_path_free (path);
	send_key (dialog, GDK_KEY_Return, 0);
	send_key (dialog, key, mods);
	settle ();
}

static void
kb_click_reset (GtkWidget *tree, GtkTreeModel *model, const char *name)
{
	g_autofree char *reset = NULL;
	guint k;
	GdkModifierType m;
	g_autofree char *row = kb_row (model, name, &k, &m, &reset);
	GtkTreePath *path;
	GdkRectangle area;
	GdkEvent *event;

	if (row == NULL) {
		return;
	}
	path = gtk_tree_path_new_from_string (row);
	gtk_tree_view_get_cell_area (GTK_TREE_VIEW (tree), path,
				     gtk_tree_view_get_column (GTK_TREE_VIEW (tree), 2), &area);
	gtk_tree_path_free (path);

	event = gdk_event_new (GDK_BUTTON_PRESS);
	event->button.window = g_object_ref (gtk_tree_view_get_bin_window (GTK_TREE_VIEW (tree)));
	event->button.x = area.x + area.width / 2.0;
	event->button.y = area.y + area.height / 2.0;
	event->button.button = GDK_BUTTON_PRIMARY;
	event->button.time = GDK_CURRENT_TIME;
	gdk_event_set_device (event, gdk_seat_get_pointer (gdk_display_get_default_seat (gdk_display_get_default ())));
	gtk_widget_event (tree, event);
	gdk_event_free (event);
}

/* The Keyboard tab lists every action with a name, with the key it has now,
 * and not the submenus. A key from the file shows, with the revert arrow,
 * and the arrow puts back the key the action started with. Enter on a row
 * waits for a key, which takes Ctrl+Page Down and Escape too. A key another
 * action has, or a plain letter, is refused and says why. Backspace turns a
 * key off. Reset all puts every one back. */
static void
test_keyboard (GtkWidget *dialog)
{
	GtkWidget *stack, *tree, *note, *reset_all;
	GtkTreeModel *model;
	GtkTreeIter iter;
	g_autofree char *first = NULL;

	stack = find_named (dialog, "page_stack");
	tree = find_widget_named (dialog, "keyboard_tree");
	note = find_widget_named (dialog, "keyboard_note");
	reset_all = find_widget_named (dialog, "keyboard_reset_all");
	check (stack != NULL && tree != NULL && note != NULL && reset_all != NULL);
	if (stack == NULL || tree == NULL || note == NULL || reset_all == NULL) {
		return;
	}
	gtk_stack_set_visible_child_name (GTK_STACK (stack), "keyboard");
	settle ();

	model = gtk_tree_view_get_model (GTK_TREE_VIEW (tree));
	g_print ("Keyboard tab: %d rows\n", gtk_tree_model_iter_n_children (model, NULL));
	check (gtk_tree_model_iter_n_children (model, NULL) == 5);
	check (gtk_tree_model_get_iter_first (model, &iter));
	gtk_tree_model_get (model, &iter, NEMO_PREFS_KEYBOARD_COL_LABEL, &first, -1);
	check (g_strcmp0 (first, "Bare") == 0);

	check (kb_is (model, "Cut", GDK_KEY_x, GDK_CONTROL_MASK, FALSE));
	check (kb_is (model, "Bare", 0, 0, FALSE));
	check (kb_is (model, "Rename", GDK_KEY_F6, 0, TRUE));

	kb_click_reset (tree, model, "Rename");
	check (kb_is (model, "Rename", GDK_KEY_F2, 0, FALSE));

	gtk_widget_grab_focus (tree);
	settle ();
	if (!gtk_widget_has_focus (tree)) {
		g_print ("SKIP: changing keys, since the dialog never got the keyboard\n");
		gtk_stack_set_visible_child_name (GTK_STACK (stack), "views");
		return;
	}

	kb_edit (dialog, tree, model, "Cut", GDK_KEY_k, GDK_CONTROL_MASK);
	check (kb_is (model, "Cut", GDK_KEY_k, GDK_CONTROL_MASK, TRUE));
	check (*gtk_label_get_text (GTK_LABEL (note)) == '\0');

	/* Taken by the next key, not by the dialog's own. */
	kb_edit (dialog, tree, model, "Bare", GDK_KEY_Page_Down, GDK_CONTROL_MASK);
	check (kb_is (model, "Bare", GDK_KEY_Page_Down, GDK_CONTROL_MASK, TRUE));
	check (g_strcmp0 (gtk_stack_get_visible_child_name (GTK_STACK (stack)), "keyboard") == 0);

	kb_edit (dialog, tree, model, "Cut", GDK_KEY_Escape, 0);
	check (kb_is (model, "Cut", GDK_KEY_k, GDK_CONTROL_MASK, TRUE));
	check (gtk_widget_get_visible (dialog));

	kb_edit (dialog, tree, model, "Cut", GDK_KEY_h, GDK_CONTROL_MASK);
	check (kb_is (model, "Cut", GDK_KEY_k, GDK_CONTROL_MASK, TRUE));
	g_print ("Taken: %s\n", gtk_label_get_text (GTK_LABEL (note)));
	check (strstr (gtk_label_get_text (GTK_LABEL (note)), "Show Hidden Files") != NULL);

	kb_edit (dialog, tree, model, "Cut", GDK_KEY_q, 0);
	check (kb_is (model, "Cut", GDK_KEY_k, GDK_CONTROL_MASK, TRUE));
	check (strstr (gtk_label_get_text (GTK_LABEL (note)), "Ctrl or Alt") != NULL);

	kb_edit (dialog, tree, model, "Cut", GDK_KEY_BackSpace, 0);
	check (kb_is (model, "Cut", 0, 0, TRUE));

	kb_click_reset (tree, model, "Cut");
	check (kb_is (model, "Cut", GDK_KEY_x, GDK_CONTROL_MASK, FALSE));

	/* Trash's Delete goes to Bare, and back by Reset all. */
	kb_edit (dialog, tree, model, "Trash", GDK_KEY_d, GDK_CONTROL_MASK);
	kb_edit (dialog, tree, model, "Bare", GDK_KEY_Delete, 0);
	kb_edit (dialog, tree, model, "Cut", GDK_KEY_F7, 0);
	check (kb_is (model, "Bare", GDK_KEY_Delete, 0, TRUE));
	check (kb_is (model, "Cut", GDK_KEY_F7, 0, TRUE));
	gtk_button_clicked (GTK_BUTTON (reset_all));
	check (kb_is (model, "Trash", GDK_KEY_Delete, 0, FALSE));
	check (kb_is (model, "Bare", 0, 0, FALSE));
	check (kb_is (model, "Cut", GDK_KEY_x, GDK_CONTROL_MASK, FALSE));

	gtk_stack_set_visible_child_name (GTK_STACK (stack), "views");
	settle ();
}

static void
test_escape_closes (GtkWidget *dialog)
{
	GdkEvent *event;

	g_object_add_weak_pointer (G_OBJECT (dialog), (gpointer *) &dialog);

	event = gdk_event_new (GDK_KEY_PRESS);
	event->key.window = g_object_ref (gtk_widget_get_window (dialog));
	event->key.keyval = GDK_KEY_Escape;
	event->key.state = 0;
	event->key.time = GDK_CURRENT_TIME;
	gdk_event_set_device (event, gdk_seat_get_keyboard (gdk_display_get_default_seat (gdk_display_get_default ())));
	gtk_main_do_event (event);
	gdk_event_free (event);
	settle ();

	check (dialog == NULL);
	if (dialog != NULL) {
		g_object_remove_weak_pointer (G_OBJECT (dialog), (gpointer *) &dialog);
		gtk_widget_destroy (dialog);
	}
}

/* xft_dpi is in 1024ths of a dot per inch, as GTK keeps it. */
static void
test_size (int xft_dpi)
{
	GdkRectangle work;
	GtkWidget *dialog;
	double scale = MAX (1.0, xft_dpi / 1024.0 / 96.0);
	int width = 0, height = 0;
	int cap_width, cap_height, floor_width, floor_height;

	g_object_set (gtk_settings_get_default (), "gtk-xft-dpi", xft_dpi, NULL);
	nemo_desktop_utils_get_monitor_work_rect (nemo_desktop_utils_get_primary_monitor (), &work);

	dialog = open_dialog ();
	check (dialog != NULL);
	if (dialog == NULL) {
		return;
	}

	gtk_window_get_default_size (GTK_WINDOW (dialog), &width, &height);
	cap_width = work.width * 9 / 10;
	cap_height = work.height * 9 / 10;
	floor_width = MIN ((int) (MIN_WIDTH * scale), cap_width);
	floor_height = MIN ((int) (MIN_HEIGHT * scale), cap_height);
	g_print ("dpi %d: %dx%d in a %dx%d work area (floor %dx%d)\n",
		 xft_dpi / 1024, width, height, work.width, work.height, floor_width, floor_height);

	check (width <= cap_width);
	check (height <= cap_height);
	check (width >= floor_width);
	check (height >= floor_height);

	gtk_widget_destroy (dialog);
	settle ();
}

int
main (int argc, char *argv[])
{
	GtkWidget *dialog;
	char *tmp;

	tmp = test_scratch_config_home ("nemo-prefs-dialog-test-XXXXXX");

	if (!gtk_init_check (&argc, &argv)) {
		g_print ("SKIP: no display\n");
		g_free (tmp);
		return 77;
	}
	/* A session with no monitor, such as a service session on Windows, has
	   nothing to place a menu or size a dialog against. */
	if (gdk_display_get_n_monitors (gdk_display_get_default ()) == 0) {
		g_print ("SKIP: no monitor\n");
		g_free (tmp);
		return 77;
	}

	nemo_global_preferences_init ();
	add_test_shortcuts ();

	dialog = open_dialog ();
	check (dialog != NULL);
	if (dialog != NULL) {
		test_widgets (dialog);
		test_per_folder_line (dialog);
		test_tabs_take_no_focus (dialog);
		test_sub_tabs (dialog);
		test_list_columns_tabs (dialog);
		test_bulk_rename (dialog);
		test_default_view (dialog);
		test_selection_model (dialog);
		test_paste_target (dialog);
		test_rename_selects_grayed (dialog);
		test_search (dialog);
		test_search_aliases (dialog);
		test_keyboard (dialog);
		test_escape_closes (dialog);
	}

	/* 100%, 115%, where the floor still fits a 1280 wide screen, and 200%,
	   where only the cap keeps it on screen. */
	test_size (96 * 1024);
	test_size (110 * 1024);
	test_size (192 * 1024);

	g_free (tmp);

	if (failures > 0) {
		g_printerr ("%d check(s) failed\n", failures);
		return EXIT_FAILURE;
	}
	g_print ("PASS\n");
	return EXIT_SUCCESS;
}
