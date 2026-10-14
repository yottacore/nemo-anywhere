/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */

/* test-eel-tab-keys.c - tabs of each kind take no keyboard focus.

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

/* eel_gtk_tabs_keep_focus_off on a stack switcher and on a notebook: no tab
 * takes the focus by a grab or by Tab, a tab for a page added later included,
 * and Ctrl+Page Up and Down, and Ctrl+Tab, step the pages from a page, from a
 * scrolled window in one and from outside them, wrapping round past a hidden
 * page. A notebook inside a page keeps the keys while the focus is in it. The
 * _within form reaches the switchers GTK's About and shortcuts windows build
 * inside.
 *
 * Needs a display. The Tab walks need the keyboard too, and are skipped
 * without it. Runs under wine as well. */

#include <config.h>

#include <gtk/gtk.h>
#include <eel/eel-gtk-extensions.h>

#include "test-check.h"

static void
pump (void)
{
	while (gtk_events_pending ()) {
		gtk_main_iteration ();
	}
}

static gboolean
take_keyboard (GtkWidget *window)
{
	int i;

	for (i = 0; i < 500 && !gtk_window_has_toplevel_focus (GTK_WINDOW (window)); i++) {
		if (i % 10 == 0 && gtk_widget_get_mapped (window)) {
			gtk_window_present (GTK_WINDOW (window));
			gdk_window_focus (gtk_widget_get_window (window), GDK_CURRENT_TIME);
		}
		g_usleep (10000);
		pump ();
	}
	return gtk_window_has_toplevel_focus (GTK_WINDOW (window));
}

/* FALSE when the key has no keycode, which a binding needs. */
static gboolean
send_key_with (GtkWidget *window, guint keyval, GdkModifierType state)
{
	GdkDisplay *display = gtk_widget_get_display (window);
	GdkKeymapKey *keys = NULL;
	gint n_keys = 0;
	gboolean coded = FALSE;
	GdkEvent *event;

	event = gdk_event_new (GDK_KEY_PRESS);
	event->key.window = g_object_ref (gtk_widget_get_window (window));
	event->key.time = GDK_CURRENT_TIME;
	event->key.keyval = keyval;
	event->key.state = state;
	if (gdk_keymap_get_entries_for_keyval (gdk_keymap_get_for_display (display), keyval,
					       &keys, &n_keys) && n_keys > 0) {
		event->key.hardware_keycode = (guint16) keys[0].keycode;
		event->key.group = (guint8) keys[0].group;
		coded = TRUE;
	}
	g_free (keys);
	gdk_event_set_device (event, gdk_seat_get_keyboard (gdk_display_get_default_seat (display)));
	gtk_main_do_event (event);
	gdk_event_free (event);
	pump ();
	return coded;
}

static gboolean
send_key (GtkWidget *window, guint keyval)
{
	return send_key_with (window, keyval, GDK_CONTROL_MASK);
}

static gboolean
focus_within (GtkWidget *window, GtkWidget *container)
{
	GtkWidget *focus = gtk_window_get_focus (GTK_WINDOW (window));

	return focus != NULL && (focus == container || gtk_widget_is_ancestor (focus, container));
}

/* Tab twice round the window. FALSE if it ever stopped inside tabs. */
static gboolean
tab_walk_skips (GtkWidget *window, GtkWidget *tabs, guint *visited)
{
	GHashTable *seen = g_hash_table_new (NULL, NULL);
	gboolean skipped = TRUE;
	int i;

	gtk_window_set_focus (GTK_WINDOW (window), NULL);
	for (i = 0; i < 40 && skipped; i++) {
		if (!gtk_widget_child_focus (window, GTK_DIR_TAB_FORWARD)) {
			continue;
		}
		skipped = !focus_within (window, tabs);
		g_hash_table_add (seen, gtk_window_get_focus (GTK_WINDOW (window)));
	}
	*visited = g_hash_table_size (seen);
	g_hash_table_destroy (seen);
	return skipped;
}

static GtkWidget *
scrolled_button (const char *label, GtkWidget **button)
{
	GtkWidget *scrolled = gtk_scrolled_window_new (NULL, NULL);

	*button = gtk_button_new_with_label (label);
	gtk_container_add (GTK_CONTAINER (scrolled), *button);
	return scrolled;
}

static void
check_switcher (void)
{
	GtkWidget *window, *box, *entry, *switcher, *stack, *close_button;
	GtkWidget *a_button, *inner, *inner_button, *hidden, *focus;
	GList *buttons, *l;
	gboolean focusable = FALSE;
	guint visited;

	stack = gtk_stack_new ();
	gtk_stack_add_titled (GTK_STACK (stack), scrolled_button ("a", &a_button), "a", "A");
	inner = gtk_notebook_new ();
	inner_button = gtk_button_new_with_label ("inner one");
	gtk_notebook_append_page (GTK_NOTEBOOK (inner), inner_button, gtk_label_new ("one"));
	gtk_notebook_append_page (GTK_NOTEBOOK (inner), gtk_button_new_with_label ("inner two"),
				  gtk_label_new ("two"));
	gtk_stack_add_titled (GTK_STACK (stack), inner, "b", "B");
	gtk_stack_add_titled (GTK_STACK (stack), gtk_button_new_with_label ("c"), "c", "C");

	/* Set up before it is in a window, the way a dialog builds it. */
	switcher = gtk_stack_switcher_new ();
	gtk_stack_switcher_set_stack (GTK_STACK_SWITCHER (switcher), GTK_STACK (stack));
	eel_gtk_tabs_keep_focus_off (switcher);

	/* A page added after, the way an extension's page is. */
	hidden = gtk_button_new_with_label ("d");
	gtk_stack_add_titled (GTK_STACK (stack), hidden, "d", "D");

	entry = gtk_entry_new ();
	close_button = gtk_button_new_with_label ("Close");
	box = gtk_box_new (GTK_ORIENTATION_VERTICAL, 0);
	gtk_box_pack_start (GTK_BOX (box), entry, FALSE, FALSE, 0);
	gtk_box_pack_start (GTK_BOX (box), switcher, FALSE, FALSE, 0);
	gtk_box_pack_start (GTK_BOX (box), stack, TRUE, TRUE, 0);
	gtk_box_pack_start (GTK_BOX (box), close_button, FALSE, FALSE, 0);

	window = gtk_window_new (GTK_WINDOW_TOPLEVEL);
	gtk_window_set_default_size (GTK_WINDOW (window), 400, 300);
	gtk_container_add (GTK_CONTAINER (window), box);
	gtk_widget_show_all (window);
	gtk_widget_hide (hidden);
	pump ();

	buttons = gtk_container_get_children (GTK_CONTAINER (switcher));
	check (g_list_length (buttons) == 4);
	for (l = buttons; l != NULL; l = l->next) {
		focusable |= gtk_widget_get_can_focus (l->data);
	}
	check (!focusable);

	/* What a click on a tab does first. */
	gtk_widget_grab_focus (a_button);
	gtk_widget_grab_focus (buttons->data);
	check (gtk_window_get_focus (GTK_WINDOW (window)) == a_button);
	g_list_free (buttons);

	if (take_keyboard (window)) {
		check (tab_walk_skips (window, switcher, &visited));
		check (visited >= 3);
	} else {
		g_print ("SKIP: the stack switcher's Tab walk, since the window never got the keyboard\n");
	}

	/* From a scrolled window in a page, which scrolls sideways on these keys. */
	gtk_stack_set_visible_child_name (GTK_STACK (stack), "a");
	gtk_widget_grab_focus (a_button);
	send_key (window, GDK_KEY_Page_Down);
	check (g_strcmp0 (gtk_stack_get_visible_child_name (GTK_STACK (stack)), "b") == 0);

	/* A notebook in the page has them while the focus is in it. */
	gtk_widget_grab_focus (inner_button);
	if (send_key (window, GDK_KEY_Page_Down)) {
		check (gtk_notebook_get_current_page (GTK_NOTEBOOK (inner)) == 1);
	} else {
		g_print ("SKIP: Ctrl+Page Down in the inner notebook, since the key has no keycode\n");
	}
	check (g_strcmp0 (gtk_stack_get_visible_child_name (GTK_STACK (stack)), "b") == 0);

	/* From outside the pages, wrapping both ways past the hidden one. */
	gtk_widget_grab_focus (close_button);
	send_key (window, GDK_KEY_Page_Down);
	check (g_strcmp0 (gtk_stack_get_visible_child_name (GTK_STACK (stack)), "c") == 0);
	send_key (window, GDK_KEY_Page_Down);
	check (g_strcmp0 (gtk_stack_get_visible_child_name (GTK_STACK (stack)), "a") == 0);
	send_key (window, GDK_KEY_Page_Up);
	check (g_strcmp0 (gtk_stack_get_visible_child_name (GTK_STACK (stack)), "c") == 0);
	send_key (window, GDK_KEY_KP_Page_Up);
	check (g_strcmp0 (gtk_stack_get_visible_child_name (GTK_STACK (stack)), "b") == 0);

	/* Ctrl+Tab and Ctrl+Shift+Tab, as in Settings. */
	send_key_with (window, GDK_KEY_Tab, GDK_CONTROL_MASK);
	check (g_strcmp0 (gtk_stack_get_visible_child_name (GTK_STACK (stack)), "c") == 0);
	send_key_with (window, GDK_KEY_ISO_Left_Tab, GDK_CONTROL_MASK | GDK_SHIFT_MASK);
	check (g_strcmp0 (gtk_stack_get_visible_child_name (GTK_STACK (stack)), "b") == 0);
	send_key_with (window, GDK_KEY_Page_Down, 0);
	check (g_strcmp0 (gtk_stack_get_visible_child_name (GTK_STACK (stack)), "b") == 0);

	focus = gtk_window_get_focus (GTK_WINDOW (window));
	check (focus == NULL || !gtk_widget_is_ancestor (focus, switcher));

	gtk_widget_destroy (window);
	pump ();
}

static void
check_notebook (void)
{
	GtkWidget *window, *box, *entry, *notebook, *close_button, *first, *scrolled;

	notebook = gtk_notebook_new ();
	scrolled = scrolled_button ("first", &first);
	gtk_notebook_append_page (GTK_NOTEBOOK (notebook), scrolled, gtk_label_new ("one"));
	gtk_notebook_append_page (GTK_NOTEBOOK (notebook), gtk_button_new_with_label ("second"),
				  gtk_label_new ("two"));
	gtk_notebook_append_page (GTK_NOTEBOOK (notebook), gtk_button_new_with_label ("third"),
				  gtk_label_new ("three"));
	eel_gtk_tabs_keep_focus_off (notebook);

	entry = gtk_entry_new ();
	close_button = gtk_button_new_with_label ("Close");
	box = gtk_box_new (GTK_ORIENTATION_VERTICAL, 0);
	gtk_box_pack_start (GTK_BOX (box), entry, FALSE, FALSE, 0);
	gtk_box_pack_start (GTK_BOX (box), notebook, TRUE, TRUE, 0);
	gtk_box_pack_start (GTK_BOX (box), close_button, FALSE, FALSE, 0);

	window = gtk_window_new (GTK_WINDOW_TOPLEVEL);
	gtk_window_set_default_size (GTK_WINDOW (window), 400, 300);
	gtk_container_add (GTK_CONTAINER (window), box);
	gtk_widget_show_all (window);
	pump ();

	/* What a click on a tab does. */
	gtk_widget_grab_focus (entry);
	gtk_widget_grab_focus (notebook);
	check (focus_within (window, scrolled));

	if (take_keyboard (window)) {
		/* Every stop is either the entry, the Close button or in the page. */
		GHashTable *seen = g_hash_table_new (NULL, NULL);
		GtkWidget *focus;
		gboolean on_tabs = FALSE;
		int i;

		gtk_window_set_focus (GTK_WINDOW (window), NULL);
		for (i = 0; i < 20; i++) {
			if (gtk_widget_child_focus (window, GTK_DIR_TAB_FORWARD)) {
				focus = gtk_window_get_focus (GTK_WINDOW (window));
				on_tabs |= focus == notebook;
				g_hash_table_add (seen, focus);
			}
		}
		check (!on_tabs);
		check (g_hash_table_size (seen) >= 3);
		g_hash_table_destroy (seen);
	} else {
		g_print ("SKIP: the notebook's Tab walk, since the window never got the keyboard\n");
	}

	gtk_widget_grab_focus (first);
	send_key (window, GDK_KEY_Page_Down);
	check (gtk_notebook_get_current_page (GTK_NOTEBOOK (notebook)) == 1);
	gtk_widget_grab_focus (close_button);
	send_key (window, GDK_KEY_Page_Down);
	check (gtk_notebook_get_current_page (GTK_NOTEBOOK (notebook)) == 2);
	send_key (window, GDK_KEY_Page_Down);
	check (gtk_notebook_get_current_page (GTK_NOTEBOOK (notebook)) == 0);
	send_key (window, GDK_KEY_Page_Up);
	check (gtk_notebook_get_current_page (GTK_NOTEBOOK (notebook)) == 2);
	check (gtk_window_get_focus (GTK_WINDOW (window)) != notebook);

	gtk_widget_destroy (window);
	pump ();
}

static void
find_switcher_cb (GtkWidget *widget, gpointer data)
{
	GtkWidget **found = data;

	if (*found == NULL && GTK_IS_STACK_SWITCHER (widget)) {
		*found = widget;
	} else if (*found == NULL && GTK_IS_CONTAINER (widget)) {
		gtk_container_forall (GTK_CONTAINER (widget), find_switcher_cb, data);
	}
}

/* GTK's About window builds its switcher inside. It has one only with a
   header bar; without, Credits and License are plain buttons by Close. */
static void
check_about (void)
{
	GtkWidget *about, *switcher = NULL;
	GtkStack *stack;
	GList *buttons, *l;
	gboolean focusable = FALSE;
	const char *first;

	about = g_object_new (GTK_TYPE_ABOUT_DIALOG, "program-name", "Tabs", "license", "Some license text.",
			      "wrap-license", TRUE, "use-header-bar", TRUE, NULL);
	eel_gtk_tabs_keep_focus_off_within (about);
	eel_gtk_tabs_keep_focus_off_within (about);
	gtk_widget_show (about);
	pump ();

	gtk_container_forall (GTK_CONTAINER (about), find_switcher_cb, &switcher);
	check (switcher != NULL);
	if (switcher == NULL) {
		gtk_widget_destroy (about);
		return;
	}

	buttons = gtk_container_get_children (GTK_CONTAINER (switcher));
	check (g_list_length (buttons) >= 2);
	for (l = buttons; l != NULL; l = l->next) {
		focusable |= gtk_widget_get_can_focus (l->data);
	}
	check (!focusable);
	g_list_free (buttons);

	stack = gtk_stack_switcher_get_stack (GTK_STACK_SWITCHER (switcher));
	first = gtk_stack_get_visible_child_name (stack);
	send_key (about, GDK_KEY_Page_Down);
	check (g_strcmp0 (gtk_stack_get_visible_child_name (stack), first) != 0);
	send_key (about, GDK_KEY_Page_Up);
	check (g_strcmp0 (gtk_stack_get_visible_child_name (stack), first) == 0);

	gtk_widget_destroy (about);
	pump ();
}

/* The shortcuts window splits a long section into pages, with a button for
   each, the way the app's own shortcuts window is. */
static void
check_shortcuts (void)
{
	GString *ui = g_string_new ("<interface><object class=\"GtkShortcutsWindow\" id=\"w\">"
				    "<child><object class=\"GtkShortcutsSection\">"
				    "<property name=\"visible\">1</property>"
				    "<property name=\"section-name\">s</property>"
				    "<property name=\"max-height\">8</property>");
	GtkBuilder *builder;
	GtkWidget *shortcuts, *switcher = NULL;
	GtkStack *stack;
	GList *buttons, *l;
	gboolean focusable = FALSE;
	const char *first;
	int i, j;

	/* Hidden groups take no room, so these have to say they show. */
	for (i = 0; i < 6; i++) {
		g_string_append_printf (ui, "<child><object class=\"GtkShortcutsGroup\">"
					"<property name=\"visible\">1</property>"
					"<property name=\"title\">Group %d</property>", i);
		for (j = 0; j < 4; j++) {
			g_string_append_printf (ui, "<child><object class=\"GtkShortcutsShortcut\">"
						"<property name=\"visible\">1</property>"
						"<property name=\"title\">Item %d</property>"
						"<property name=\"accelerator\">&lt;ctrl&gt;F%d</property>"
						"</object></child>", j, j + 1);
		}
		g_string_append (ui, "</object></child>");
	}
	g_string_append (ui, "</object></child></object></interface>");

	builder = gtk_builder_new_from_string (ui->str, -1);
	g_string_free (ui, TRUE);
	shortcuts = GTK_WIDGET (gtk_builder_get_object (builder, "w"));
	eel_gtk_tabs_keep_focus_off_within (shortcuts);
	gtk_widget_show_all (shortcuts);

	gtk_container_forall (GTK_CONTAINER (shortcuts), find_switcher_cb, &switcher);
	check (switcher != NULL);
	if (switcher == NULL) {
		gtk_widget_destroy (shortcuts);
		g_object_unref (builder);
		return;
	}

	/* The pages are made once the window is up. */
	buttons = NULL;
	for (i = 0; i < 200 && g_list_length (buttons) < 2; i++) {
		g_list_free (buttons);
		g_usleep (10000);
		pump ();
		buttons = gtk_container_get_children (GTK_CONTAINER (switcher));
	}
	check (g_list_length (buttons) >= 2);
	for (l = buttons; l != NULL; l = l->next) {
		focusable |= gtk_widget_get_can_focus (l->data);
	}
	check (!focusable);
	g_list_free (buttons);

	stack = gtk_stack_switcher_get_stack (GTK_STACK_SWITCHER (switcher));
	first = gtk_stack_get_visible_child_name (stack);
	send_key (shortcuts, GDK_KEY_Page_Down);
	check (g_strcmp0 (gtk_stack_get_visible_child_name (stack), first) != 0);
	send_key (shortcuts, GDK_KEY_Page_Up);
	check (g_strcmp0 (gtk_stack_get_visible_child_name (stack), first) == 0);

	gtk_widget_destroy (shortcuts);
	g_object_unref (builder);
	pump ();
}

int
main (int argc, char *argv[])
{
	if (!gtk_init_check (&argc, &argv)) {
		g_print ("SKIP: no display\n");
		return 77;
	}

	check_switcher ();
	check_notebook ();
	check_about ();
	check_shortcuts ();

	if (failures > 0) {
		g_printerr ("%d failure(s)\n", failures);
		return 1;
	}
	g_print ("OK\n");
	return 0;
}
