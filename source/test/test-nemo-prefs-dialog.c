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
 * short by a small screen does not look complete. Close sits in a bar outside
 * the scrolling and is the default, and Escape closes. The window is never
 * more than nine tenths of the work area, and its floor grows with the text
 * scale, which is how Windows passes a fractional display scale. No tab, in
 * the list on the left, ever holds the keyboard, and Ctrl+Page Up and Down,
 * Ctrl+Tab and Ctrl+Shift+Tab step through the list, sub-tabs included.
 * Sub-tabs are indented rows, and the parent and its showing sub-tab are both
 * lit. No page needs a scrollbar at the size the window opens. The pages that
 * build their own widgets are left empty here. */

#include <config.h>

#include <stdlib.h>
#include <gtk/gtk.h>

#include <libnemo-private/nemo-bulk-rename.h>
#include <libnemo-private/nemo-desktop-utils.h>
#include <libnemo-private/nemo-global-preferences.h>
#include <libnemo-private/nemo-tab-list.h>

#include "nemo-file-management-properties.h"
#include "nemo-plugin-manager.h"
#include "nemo-prefs-current-folder.h"
#include "nemo-prefs-file-cache.h"
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

/* With another window holding the keyboard, GTK reports focus moves that did
 * not happen, so the Tab walk below asks for it first and waits. */
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
	check (g_strcmp0 (gtk_stack_get_visible_child_name (GTK_STACK (stack)), "plugins") == 0);
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
	check_showing (stack, "list-columns");
	gtk_widget_show (find_named (dialog, "scrolledwindow5"));
	settle ();
	check (gtk_widget_get_visible (row_titled (sidebar, "Preview")));

	/* Every page fits at the opening size, unless the screen held the
	   window back. */
	gtk_window_get_size (GTK_WINDOW (dialog), &width, &height);
	nemo_desktop_utils_get_monitor_work_rect (nemo_desktop_utils_get_primary_monitor (), &work);
	if (height < work.height * 9 / 10) {
		const char *const leaves[] = {
			"views_default_page", "views_current_page", "scrolledwindow1",
			"behavior_files_scrolledwindow", "behavior_programs_scrolledwindow",
			"scrolledwindow3", "windows_scrolledwindow", "scrolledwindow4", "scrolledwindow5", "scrolledwindow6",
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

	dialog = open_dialog ();
	check (dialog != NULL);
	if (dialog != NULL) {
		test_widgets (dialog);
		test_tabs_take_no_focus (dialog);
		test_sub_tabs (dialog);
		test_bulk_rename (dialog);
		test_escape_closes (dialog);
	}

	/* 100%, 115%, where the floor still fits a 1280 wide screen, and 200%,
	   where only the cap holds it. */
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
