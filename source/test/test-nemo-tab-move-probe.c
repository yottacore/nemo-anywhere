/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */

/* test-nemo-tab-move-probe.c - what a window shows, read from inside the
   program, and a tab dragged off to empty screen.

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

/* Preloaded into every copy test-nemo-tab-move starts, and into any copy those
 * start in turn. Each one keeps $NEMO_TABMOVE_OUT/<pid> up to date with its
 * window's title, the current tab's view and the names selected in it, and how
 * many windows the process has. On SIGUSR1 it drags the current tab off to an
 * empty part of the screen: the pointer goes to the screen's far corner and
 * the tab bar is asked for a window to drop the tab in, which is the question
 * GTK asks when a tab is dropped outside every tab bar. SIGUSR2 runs the
 * command in $NEMO_TABMOVE_OUT/<pid>.do: "drop X Y" drops the tab the same way
 * at that point, over another copy's window, and "close" closes it from the
 * tab menu. No symbol of the program is used. */

#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include <glib-unix.h>
#include <gtk/gtk.h>

static char    *out_path;
static char    *last;
static gboolean tore;

static gboolean
is_a (gpointer object, const char *type_name)
{
	GType type = g_type_from_name (type_name);

	return type != 0 && object != NULL && g_type_is_a (G_OBJECT_TYPE (object), type);
}

typedef struct {
	const char *type_name;
	GtkWidget  *found;
} Find;

static void
find_cb (GtkWidget *widget, gpointer data)
{
	Find *find = data;

	if (find->found != NULL) {
		return;
	}
	if (is_a (widget, find->type_name) && gtk_widget_get_mapped (widget)) {
		find->found = widget;
	} else if (GTK_IS_CONTAINER (widget)) {
		gtk_container_forall (GTK_CONTAINER (widget), find_cb, data);
	}
}

static GtkWidget *
find_in (GtkWidget *top, const char *type_name)
{
	Find find = { type_name, NULL };

	find_cb (top, &find);
	return find.found;
}

/* The shown window, and how many the process has, shown or not. */
static GtkWidget *
shown_window (int *count)
{
	GList *toplevels = gtk_window_list_toplevels ();
	GList *l;
	GtkWidget *shown = NULL;

	*count = 0;
	for (l = toplevels; l != NULL; l = l->next) {
		if (!is_a (l->data, "NemoWindow")) {
			continue;
		}
		(*count)++;
		if (shown == NULL && gtk_widget_get_mapped (l->data)) {
			shown = l->data;
		}
	}
	g_list_free (toplevels);

	return shown;
}

static const char *
view_name (GtkWidget *view)
{
	gboolean compact = FALSE;

	if (is_a (view, "NemoListView")) {
		return "list";
	}
	if (is_a (view, "NemoIconView")) {
		g_object_get (view, "compact", &compact, NULL);
		return compact ? "compact" : "icon";
	}
	return view != NULL ? G_OBJECT_TYPE_NAME (view) : "none";
}

/* Names in the order the view keeps them. Only the icon and compact views,
   whose accessible object answers for the selection. */
static void
add_selected (GString *state, GtkWidget *view)
{
	GtkWidget *child = view != NULL ? gtk_bin_get_child (GTK_BIN (view)) : NULL;
	AtkObject *accessible;
	int i, n;

	if (child == NULL || !is_a (child, "NemoIconContainer")) {
		return;
	}
	accessible = gtk_widget_get_accessible (child);
	if (!ATK_IS_SELECTION (accessible)) {
		return;
	}
	n = atk_selection_get_selection_count (ATK_SELECTION (accessible));
	for (i = 0; i < n; i++) {
		AtkObject *item = atk_selection_ref_selection (ATK_SELECTION (accessible), i);

		if (item != NULL) {
			g_string_append_printf (state, "%s%s", i > 0 ? "," : "",
						atk_object_get_name (item) != NULL ?
						atk_object_get_name (item) : "");
			g_object_unref (item);
		}
	}
}

static gboolean
tick (gpointer data)
{
	GString *state = g_string_new (NULL);
	GtkWidget *window, *view = NULL;
	const char *title = NULL;
	int windows;

	window = shown_window (&windows);
	if (window != NULL) {
		title = gtk_window_get_title (GTK_WINDOW (window));
		view = find_in (window, "NemoView");
	}
	g_string_append_printf (state, "title=%s\n", title != NULL ? title : "");
	g_string_append_printf (state, "view=%s\n", view_name (view));
	g_string_append (state, "selected=");
	add_selected (state, view);
	g_string_append_printf (state, "\nwindows=%d\ntore=%d\n", windows, tore ? 1 : 0);

	if (g_strcmp0 (state->str, last) != 0) {
		g_file_set_contents (out_path, state->str, -1, NULL);
		g_free (last);
		last = g_strdup (state->str);
	}
	g_string_free (state, TRUE);

	return G_SOURCE_CONTINUE;
}

static void
drop_at (int x, int y)
{
	GtkWidget *window, *notebook, *page;
	GdkDisplay *display = gdk_display_get_default ();
	GtkNotebook *dropped_in = NULL;
	int windows;

	window = shown_window (&windows);
	notebook = window != NULL ? find_in (window, "NemoNotebook") : NULL;
	if (notebook == NULL) {
		return;
	}
	page = gtk_notebook_get_nth_page (GTK_NOTEBOOK (notebook),
					  gtk_notebook_get_current_page (GTK_NOTEBOOK (notebook)));

	gdk_device_warp (gdk_seat_get_pointer (gdk_display_get_default_seat (display)),
			 gdk_display_get_default_screen (display), x, y);
	gdk_display_sync (display);

	g_signal_emit_by_name (notebook, "create-window", page, x, y, &dropped_in);
	tore = TRUE;
}

static gboolean
tear_off (gpointer data)
{
	GdkWindow *root = gdk_get_default_root_window ();

	/* Over nothing at all, no window of any copy. */
	drop_at (gdk_window_get_width (root) - 2, gdk_window_get_height (root) - 2);

	return G_SOURCE_CONTINUE;
}

/* The menu the Menu key opens on the tab bar, then its last item, Close tab. */
static void
close_from_menu (void)
{
	GtkWidget *window, *notebook, *item;
	GList *menus, *items;
	gboolean handled = FALSE;
	int windows;

	window = shown_window (&windows);
	notebook = window != NULL ? find_in (window, "NemoNotebook") : NULL;
	if (notebook == NULL) {
		return;
	}
	g_signal_emit_by_name (notebook, "popup-menu", &handled);
	menus = gtk_menu_get_for_attach_widget (notebook);
	if (!handled || menus == NULL) {
		return;
	}
	items = gtk_container_get_children (GTK_CONTAINER (g_list_last (menus)->data));
	item = items != NULL ? g_object_ref (g_list_last (items)->data) : NULL;
	g_list_free (items);
	if (item == NULL) {
		return;
	}
	gtk_menu_shell_deactivate (GTK_MENU_SHELL (g_list_last (menus)->data));
	gtk_menu_item_activate (GTK_MENU_ITEM (item));
	g_object_unref (item);
}

static gboolean
run_command (gpointer data)
{
	char *path = g_strconcat (out_path, ".do", NULL);
	char *text = NULL;
	int x, y;

	if (g_file_get_contents (path, &text, NULL, NULL)) {
		if (sscanf (text, "drop %d %d", &x, &y) == 2) {
			drop_at (x, y);
		} else if (g_str_has_prefix (text, "close")) {
			close_from_menu ();
		}
	}
	g_free (text);
	g_free (path);

	return G_SOURCE_CONTINUE;
}

__attribute__((constructor)) static void
probe_start (void)
{
	const char *dir = g_getenv ("NEMO_TABMOVE_OUT");

	if (dir == NULL) {
		return;
	}
	out_path = g_strdup_printf ("%s/%d", dir, (int) getpid ());
	g_timeout_add (100, tick, NULL);
	g_unix_signal_add (SIGUSR1, tear_off, NULL);
	g_unix_signal_add (SIGUSR2, run_command, NULL);
}
