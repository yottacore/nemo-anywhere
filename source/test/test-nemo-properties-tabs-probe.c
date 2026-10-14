/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */

/* test-nemo-properties-tabs-probe.c - the Properties window's tabs and the
   keyboard, from inside the program.

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

/* Preloaded into the real program by test-nemo-properties-tabs. Once the
 * window shows $NEMO_PROPTABS_FOLDER it opens Properties on that folder with
 * Ctrl+I. The tabs along the top must never take the focus: not by Tab, and
 * not by a click, which still switches the page. Ctrl+Page Up and Down, and
 * Ctrl+Tab and Ctrl+Shift+Tab, step the pages and wrap round, from a page and
 * from the Close button. One "ok"
 * or "FAIL" line per check goes to $NEMO_PROPTABS_OUT, and "done" at the end.
 *
 * No symbol of the program is used. */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <gtk/gtk.h>

static FILE       *out;
static const char *folder_path;
static GtkWidget  *window;
static GtkWidget  *props;
static GtkWidget  *tab_area;
static GtkWidget  *tab_strip;
static int         waited;
static int         step;
static int         step_at;

static void
report (gboolean passed, const char *what)
{
	fprintf (out, "%s %s\n", passed ? "ok" : "FAIL", what);
	fflush (out);
}

static void
finish (void)
{
	fprintf (out, "done\n");
	fflush (out);
}

static gboolean
is_a (gpointer object, const char *type_name)
{
	GType type = g_type_from_name (type_name);

	return type != 0 && object != NULL && g_type_is_a (G_OBJECT_TYPE (object), type);
}

static void
pump (void)
{
	while (gtk_events_pending ()) {
		gtk_main_iteration ();
	}
}

static void
send_key (GtkWidget *toplevel, guint keyval, GdkModifierType state)
{
	GdkDisplay *display = gtk_widget_get_display (toplevel);
	GdkKeymapKey *keys = NULL;
	gint n_keys = 0;
	GdkEvent *event;

	event = gdk_event_new (GDK_KEY_PRESS);
	event->key.window = g_object_ref (gtk_widget_get_window (toplevel));
	event->key.send_event = TRUE;
	event->key.time = GDK_CURRENT_TIME;
	event->key.keyval = keyval;
	event->key.state = state;
	if (gdk_keymap_get_entries_for_keyval (gdk_keymap_get_for_display (display), keyval,
					       &keys, &n_keys) && n_keys > 0) {
		event->key.hardware_keycode = (guint16) keys[0].keycode;
		event->key.group = (guint8) keys[0].group;
	}
	g_free (keys);
	gdk_event_set_device (event, gdk_seat_get_keyboard (gdk_display_get_default_seat (display)));
	gtk_main_do_event (event);
	gdk_event_free (event);
	pump ();
}

static void
send_button (GtkWidget *button, GdkEventType type)
{
	GdkWindow *target = gtk_button_get_event_window (GTK_BUTTON (button));
	GdkSeat *seat = gdk_display_get_default_seat (gtk_widget_get_display (button));
	GdkEvent *event = gdk_event_new (type);
	int origin_x, origin_y, x, y;

	x = gtk_widget_get_allocated_width (button) / 2;
	y = gtk_widget_get_allocated_height (button) / 2;
	gdk_window_get_origin (target, &origin_x, &origin_y);
	event->button.window = g_object_ref (target);
	event->button.time = (guint32) (g_get_monotonic_time () / 1000);
	event->button.x = x;
	event->button.y = y;
	event->button.x_root = origin_x + x;
	event->button.y_root = origin_y + y;
	event->button.button = 1;
	if (type == GDK_BUTTON_RELEASE) {
		event->button.state = GDK_BUTTON1_MASK;
	}
	gdk_event_set_device (event, gdk_seat_get_pointer (seat));
	gtk_main_do_event (event);
	gdk_event_free (event);
	pump ();
}

static void
find_switcher_cb (GtkWidget *widget, gpointer data)
{
	GtkWidget **found = data;

	if (*found != NULL) {
		return;
	}
	if (GTK_IS_STACK_SWITCHER (widget)) {
		*found = widget;
	} else if (GTK_IS_CONTAINER (widget)) {
		gtk_container_forall (GTK_CONTAINER (widget), find_switcher_cb, data);
	}
}

static void
find_button_cb (GtkWidget *widget, gpointer data)
{
	GtkWidget **found = data;

	if (*found == NULL && GTK_IS_BUTTON (widget) && gtk_widget_get_visible (widget) &&
	    gtk_widget_get_can_focus (widget) && !gtk_widget_is_ancestor (widget, tab_area) &&
	    !gtk_widget_is_ancestor (widget, tab_strip)) {
		*found = widget;
	} else if (*found == NULL && GTK_IS_CONTAINER (widget)) {
		gtk_container_forall (GTK_CONTAINER (widget), find_button_cb, data);
	}
}

static gboolean
on_a_tab (GtkWidget *switcher)
{
	GtkWidget *focus = gtk_window_get_focus (GTK_WINDOW (props));

	return focus != NULL && gtk_widget_is_ancestor (focus, switcher);
}

static int
page_index (GtkStack *stack)
{
	GList *children = gtk_container_get_children (GTK_CONTAINER (stack));
	int index = g_list_index (children, gtk_stack_get_visible_child (stack));

	g_list_free (children);
	return index;
}

static gboolean
take_keyboard (void)
{
	int i;

	for (i = 0; i < 300 && !gtk_window_has_toplevel_focus (GTK_WINDOW (props)); i++) {
		if (i % 10 == 0) {
			gdk_window_focus (gtk_widget_get_window (props), GDK_CURRENT_TIME);
		}
		g_usleep (10000);
		pump ();
	}
	return gtk_window_has_toplevel_focus (GTK_WINDOW (props));
}

static void
check_tabs (void)
{
	GtkWidget *switcher = NULL, *close_button = NULL;
	GList *buttons, *children, *l;
	GHashTable *seen;
	GtkStack *stack;
	gboolean focusable = FALSE, stopped = FALSE;
	int pages, i;
	char *what;

	find_switcher_cb (props, &switcher);
	report (switcher != NULL, "the window has tabs");
	if (switcher == NULL) {
		return;
	}
	stack = gtk_stack_switcher_get_stack (GTK_STACK_SWITCHER (switcher));
	buttons = gtk_container_get_children (GTK_CONTAINER (switcher));
	pages = (int) g_list_length (buttons);
	what = g_strdup_printf ("at least 2 tabs (%d)", pages);
	report (pages >= 2, what);
	g_free (what);
	if (pages < 2) {
		g_list_free (buttons);
		return;
	}

	for (l = buttons; l != NULL; l = l->next) {
		focusable |= gtk_widget_get_can_focus (l->data);
	}
	report (!focusable, "no tab can take the focus");

	/* Tab right round the window, twice over. */
	if (!take_keyboard ()) {
		report (FALSE, "the window took the keyboard, for the Tab walk");
	} else {
		seen = g_hash_table_new (NULL, NULL);
		gtk_window_set_focus (GTK_WINDOW (props), NULL);
		for (i = 0; i < 100 && !stopped; i++) {
			if (!gtk_widget_child_focus (props, GTK_DIR_TAB_FORWARD)) {
				continue;
			}
			stopped = on_a_tab (switcher);
			g_hash_table_add (seen, gtk_window_get_focus (GTK_WINDOW (props)));
		}
		report (!stopped, "Tab never stops on a tab");
		report (g_hash_table_size (seen) >= 2, "Tab moves round the window");
		g_hash_table_destroy (seen);
	}

	/* A click picks the page and leaves the focus off the tab. */
	children = gtk_container_get_children (GTK_CONTAINER (stack));
	gtk_stack_set_visible_child (stack, children->data);
	g_list_free (children);
	gtk_widget_child_focus (gtk_stack_get_visible_child (stack), GTK_DIR_TAB_FORWARD);
	pump ();
	send_button (g_list_nth_data (buttons, 1), GDK_BUTTON_PRESS);
	send_button (g_list_nth_data (buttons, 1), GDK_BUTTON_RELEASE);
	report (page_index (stack) == 1, "a click on the second tab shows its page");
	report (!on_a_tab (switcher), "the click leaves the focus off the tab");

	/* The keys from inside a page, both ways round the end. */
	gtk_widget_child_focus (gtk_stack_get_visible_child (stack), GTK_DIR_TAB_FORWARD);
	for (i = page_index (stack); i < pages - 1; i++) {
		send_key (props, GDK_KEY_Page_Down, GDK_CONTROL_MASK);
	}
	report (page_index (stack) == pages - 1, "Ctrl+Page Down steps to the last page");
	send_key (props, GDK_KEY_Page_Down, GDK_CONTROL_MASK);
	report (page_index (stack) == 0, "Ctrl+Page Down wraps round to the first page");
	send_key (props, GDK_KEY_Page_Up, GDK_CONTROL_MASK);
	report (page_index (stack) == pages - 1, "Ctrl+Page Up wraps round to the last page");

	/* Ctrl+Tab and Ctrl+Shift+Tab too, as in Settings. */
	send_key (props, GDK_KEY_Tab, GDK_CONTROL_MASK);
	report (page_index (stack) == 0, "Ctrl+Tab wraps round to the first page");
	send_key (props, GDK_KEY_ISO_Left_Tab, GDK_CONTROL_MASK | GDK_SHIFT_MASK);
	report (page_index (stack) == pages - 1, "Ctrl+Shift+Tab wraps round to the last page");

	/* And from outside the pages. */
	tab_area = gtk_widget_get_parent (GTK_WIDGET (stack));
	tab_strip = switcher;
	find_button_cb (props, &close_button);
	report (close_button != NULL, "the window has a button below the pages");
	if (close_button != NULL) {
		gtk_widget_grab_focus (close_button);
		send_key (props, GDK_KEY_Page_Up, GDK_CONTROL_MASK);
		report (page_index (stack) == pages - 2, "Ctrl+Page Up steps back from the Close button");
	}
	report (!on_a_tab (switcher), "the keys leave the focus off the tabs");

	g_list_free (buttons);
}

/* Up to sixty seconds in all. */
static gboolean
tick (G_GNUC_UNUSED gpointer data)
{
	GList *toplevels, *l;
	const char *title;
	char *base;

	if (++waited > 600) {
		report (FALSE, "the steps finished in time");
		finish ();
		return G_SOURCE_REMOVE;
	}

	switch (step) {
	case 0:
		toplevels = gtk_window_list_toplevels ();
		for (l = toplevels; l != NULL; l = l->next) {
			if (is_a (l->data, "NemoWindow") && gtk_widget_get_mapped (l->data)) {
				window = l->data;
			}
		}
		g_list_free (toplevels);
		title = window != NULL ? gtk_window_get_title (GTK_WINDOW (window)) : NULL;
		base = g_path_get_basename (folder_path);
		if (title == NULL || !g_str_has_prefix (title, base)) {
			g_free (base);
			return G_SOURCE_CONTINUE;
		}
		g_free (base);
		break;
	case 1:
		if (waited - step_at < 10) {
			return G_SOURCE_CONTINUE;
		}
		report (gtk_accel_groups_activate (G_OBJECT (window), GDK_KEY_i, GDK_CONTROL_MASK),
			"Ctrl+I is a shortcut in the window");
		break;
	case 2:
		toplevels = gtk_window_list_toplevels ();
		for (l = toplevels; l != NULL; l = l->next) {
			if (is_a (l->data, "NemoPropertiesWindow") && gtk_widget_get_mapped (l->data)) {
				props = l->data;
			}
		}
		g_list_free (toplevels);
		if (props == NULL) {
			if (waited - step_at > 100) {
				report (FALSE, "the Properties window opened");
				finish ();
				return G_SOURCE_REMOVE;
			}
			return G_SOURCE_CONTINUE;
		}
		break;
	case 3:
		/* A moment for the pages to fill in. */
		if (waited - step_at < 10) {
			return G_SOURCE_CONTINUE;
		}
		report (TRUE, "the Properties window opened");
		check_tabs ();
		finish ();
		return G_SOURCE_REMOVE;
	}

	step++;
	step_at = waited;
	return G_SOURCE_CONTINUE;
}

__attribute__((constructor)) static void
probe_start (void)
{
	const char *path = g_getenv ("NEMO_PROPTABS_OUT");

	folder_path = g_strdup (g_getenv ("NEMO_PROPTABS_FOLDER"));
	if (path == NULL || folder_path == NULL) {
		return;
	}
	out = fopen (path, "w");
	/* Anything the program starts loads this too, and would empty the file
	   again. */
	g_unsetenv ("NEMO_PROPTABS_OUT");
	g_unsetenv ("NEMO_PROPTABS_FOLDER");
	if (out == NULL) {
		return;
	}
	g_timeout_add (100, tick, NULL);
}
