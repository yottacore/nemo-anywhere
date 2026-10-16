/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */

/* test-nemo-tooltip-focus-probe.c - hovers each kind of tooltip with the
   window focused and then with another program focused, from inside the
   program.

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

/* Preloaded into the real program by test-nemo-tooltip-focus. Once the
 * window shows $NEMO_TIP_FOLDER it takes the focus and moves the pointer over
 * a toolbar button, a place, an icon and, in the list view, a row. Each has to
 * show its tooltip. Then it starts $NEMO_TIP_THIEF, another program that
 * takes the focus, and the tooltip up has to go, and none of the 4 may show.
 * One "ok" or "FAIL" line per check goes to $NEMO_TIP_OUT, and "done" at the
 * end.
 *
 * No symbol of the program is used. */

#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <gtk/gtk.h>
#include <eel/eel-canvas.h>

/* Long enough for the first tooltip's delay, with room to spare. */
#define HOVER_TICKS 15
/* A tip that should show gets this long on a loaded box, hovered again each HOVER_TICKS. */
#define SHOW_TICKS  (HOVER_TICKS * 6)

typedef enum {
	TIP_BUTTON,
	TIP_PLACE,
	TIP_ICON,
	TIP_ROW,
} Tip;

static const char *tip_names[] = { "a toolbar button", "a place", "an icon", "a list row" };

static FILE       *out;
static char       *folder_name;
static char       *thief;
static GPid        thief_pid;
static GtkWidget  *window;
static int         waited;
static int         step;
static int         step_at;
static int         asks;

static void
report (gboolean passed, const char *what)
{
	fprintf (out, "%s %s\n", passed ? "ok" : "FAIL", what);
	fflush (out);
}

static gboolean
is_a (GtkWidget *widget, const char *type_name)
{
	GType type = g_type_from_name (type_name);

	return type != 0 && widget != NULL && g_type_is_a (G_OBJECT_TYPE (widget), type);
}

typedef struct {
	const char *type_name;
	gboolean    with_tooltip;
	GtkWidget  *found;
} Find;

static void
find_cb (GtkWidget *widget, gpointer data)
{
	Find *find = data;
	char *text;

	if (find->found != NULL) {
		return;
	}
	if (is_a (widget, find->type_name) && gtk_widget_get_mapped (widget)) {
		text = find->with_tooltip ? gtk_widget_get_tooltip_text (widget) : NULL;
		if (!find->with_tooltip || (text != NULL && *text != '\0')) {
			find->found = widget;
		}
		g_free (text);
		if (find->found != NULL) {
			return;
		}
	}
	if (GTK_IS_CONTAINER (widget)) {
		gtk_container_forall (GTK_CONTAINER (widget), find_cb, data);
	}
}

static GtkWidget *
find_in (GtkWidget *top, const char *type_name, gboolean with_tooltip)
{
	Find find = { type_name, with_tooltip, NULL };

	if (top != NULL) {
		find_cb (top, &find);
	}
	return find.found;
}

static GtkWidget *
view_child (void)
{
	GtkWidget *view = find_in (window, "NemoView", FALSE);

	return view != NULL ? gtk_bin_get_child (GTK_BIN (view)) : NULL;
}

static gboolean
tooltip_showing (void)
{
	GList *toplevels, *l;
	gboolean showing = FALSE;

	toplevels = gtk_window_list_toplevels ();
	for (l = toplevels; l != NULL; l = l->next) {
		if (gtk_window_get_type_hint (l->data) == GDK_WINDOW_TYPE_HINT_TOOLTIP &&
		    gtk_widget_get_mapped (l->data)) {
			showing = TRUE;
		}
	}
	g_list_free (toplevels);
	return showing;
}

static void
warp_to (GtkWidget *widget, int x, int y)
{
	GdkDisplay *display = gtk_widget_get_display (widget);
	GdkDevice *pointer = gdk_seat_get_pointer (gdk_display_get_default_seat (display));
	GtkWidget *toplevel = gtk_widget_get_toplevel (widget);
	int top_x, top_y, wx = 0, wy = 0;

	gdk_window_get_origin (gtk_widget_get_window (toplevel), &top_x, &top_y);
	gtk_widget_translate_coordinates (widget, toplevel, x, y, &wx, &wy);
	gdk_device_warp (pointer, gtk_widget_get_screen (widget), top_x + wx, top_y + wy);
}

static gboolean
tree_row_point (GtkTreeView *tree, const char *path_text, int *x, int *y)
{
	GtkTreePath *path = gtk_tree_path_new_from_string (path_text);
	GdkRectangle area;

	gtk_tree_view_get_background_area (tree, path, NULL, &area);
	gtk_tree_path_free (path);
	if (area.height <= 0) {
		return FALSE;
	}
	gtk_tree_view_convert_bin_window_to_widget_coords (tree, area.x + 40, area.y + area.height / 2, x, y);
	return TRUE;
}

/* Puts the pointer over the thing, or says why not. */
static gboolean
hover (Tip tip)
{
	GtkWidget *widget = NULL;
	int x = 0, y = 0;
	char *line;

	switch (tip) {
	case TIP_BUTTON:
		widget = find_in (find_in (window, "GtkToolbar", FALSE), "GtkButton", TRUE);
		if (widget != NULL) {
			x = gtk_widget_get_allocated_width (widget) / 2;
			y = gtk_widget_get_allocated_height (widget) / 2;
		}
		break;
	case TIP_PLACE:
		widget = find_in (find_in (window, "NemoPlacesSidebar", FALSE), "GtkTreeView", FALSE);
		/* The first place under the first heading. */
		if (widget != NULL && !tree_row_point (GTK_TREE_VIEW (widget), "0:0", &x, &y)) {
			widget = NULL;
		}
		break;
	case TIP_ICON: {
		GtkWidget *pane = view_child ();
		AtkObject *accessible, *child;
		GObject *object;

		if (pane == NULL || !is_a (pane, "NemoIconContainer")) {
			break;
		}
		accessible = gtk_widget_get_accessible (pane);
		if (atk_object_get_n_accessible_children (accessible) < 1) {
			break;
		}
		child = atk_object_ref_accessible_child (accessible, 0);
		object = ATK_IS_GOBJECT_ACCESSIBLE (child) ?
			atk_gobject_accessible_get_object (ATK_GOBJECT_ACCESSIBLE (child)) : NULL;
		if (object != NULL && g_type_is_a (G_OBJECT_TYPE (object), g_type_from_name ("EelCanvasItem"))) {
			EelCanvasItem *item = (EelCanvasItem *) object;
			int sx = (int) gtk_adjustment_get_value (gtk_scrollable_get_hadjustment (GTK_SCROLLABLE (pane)));
			int sy = (int) gtk_adjustment_get_value (gtk_scrollable_get_vadjustment (GTK_SCROLLABLE (pane)));

			widget = pane;
			x = (int) ((item->x1 + item->x2) / 2) - sx;
			y = (int) (item->y1 + (item->y2 - item->y1) / 3) - sy;
		}
		g_object_unref (child);
		break;
	}
	case TIP_ROW:
		widget = find_in (view_child (), "GtkTreeView", FALSE);
		if (widget == NULL && is_a (view_child (), "GtkTreeView")) {
			widget = view_child ();
		}
		if (widget != NULL && !tree_row_point (GTK_TREE_VIEW (widget), "0", &x, &y)) {
			widget = NULL;
		}
		break;
	}

	if (widget == NULL) {
		line = g_strdup_printf ("found %s to point at", tip_names[tip]);
		report (FALSE, line);
		g_free (line);
		return FALSE;
	}

	/* From one side then the other, so it is a move GTK sees. */
	warp_to (widget, x - 2, y);
	warp_to (widget, x, y);
	return TRUE;
}

static void
check_tip (Tip tip, gboolean focused)
{
	char *line;

	if (focused) {
		line = g_strdup_printf ("%s shows its tooltip with the window focused", tip_names[tip]);
		report (tooltip_showing (), line);
	} else {
		line = g_strdup_printf ("%s shows no tooltip with another program focused", tip_names[tip]);
		report (!tooltip_showing (), line);
	}
	g_free (line);
}

/* Showing, or out of time. Hovers again now and then in case a re-layout moved the thing. */
static gboolean
shown_yet (Tip tip, int since)
{
	if (since < HOVER_TICKS) {
		return FALSE;
	}
	if (tooltip_showing () || since >= SHOW_TICKS) {
		return TRUE;
	}
	if (since % HOVER_TICKS == 0) {
		hover (tip);
	}
	return FALSE;
}

static gboolean
list_has_rows (void)
{
	GtkWidget *tree = find_in (view_child (), "GtkTreeView", FALSE);
	int x, y;

	if (tree == NULL && is_a (view_child (), "GtkTreeView")) {
		tree = view_child ();
	}
	return tree != NULL && tree_row_point (GTK_TREE_VIEW (tree), "0", &x, &y);
}

static void
stop_thief (void)
{
	if (thief_pid > 0) {
		kill (thief_pid, SIGTERM);
		g_spawn_close_pid (thief_pid);
		thief_pid = 0;
	}
}

static void
finish (void)
{
	stop_thief ();
	fprintf (out, "done\n");
	fflush (out);
}

static gboolean
start_thief (void)
{
	char *argv[] = { thief, (char *) "--thief", NULL };
	GError *error = NULL;

	if (!g_spawn_async (NULL, argv, NULL, G_SPAWN_DEFAULT, NULL, NULL, &thief_pid, &error)) {
		report (FALSE, error->message);
		g_clear_error (&error);
		return FALSE;
	}
	return TRUE;
}

/* The steps, in order. Each waits for the one before to settle. */
enum {
	WAIT_WINDOW,
	TAKE_FOCUS,
	FOCUSED_BUTTON,
	FOCUSED_PLACE,
	FOCUSED_ICON,
	TO_LIST,
	FOCUSED_ROW,
	STEAL,
	STOLEN,
	UNFOCUSED_ROW,
	UNFOCUSED_BUTTON,
	UNFOCUSED_PLACE,
	TO_ICONS,
	UNFOCUSED_ICON,
	ALL_DONE,
};

static void
next_step (void)
{
	step++;
	step_at = waited;
}

/* Up to 80 seconds in all. */
static gboolean
tick (G_GNUC_UNUSED gpointer data)
{
	GList *toplevels, *l;
	const char *title;
	int since = waited - step_at;
	GtkWidget *pane;

	if (++waited > 800) {
		report (FALSE, "the steps finished in time");
		finish ();
		return G_SOURCE_REMOVE;
	}

	switch (step) {
	case WAIT_WINDOW:
		toplevels = gtk_window_list_toplevels ();
		for (l = toplevels; l != NULL; l = l->next) {
			if (is_a (l->data, "NemoWindow") && gtk_widget_get_mapped (l->data)) {
				window = l->data;
			}
		}
		g_list_free (toplevels);
		title = window != NULL ? gtk_window_get_title (GTK_WINDOW (window)) : NULL;
		pane = view_child ();
		if (title == NULL || !g_str_has_prefix (title, folder_name) || pane == NULL ||
		    !is_a (pane, "NemoIconContainer") ||
		    atk_object_get_n_accessible_children (gtk_widget_get_accessible (pane)) < 3) {
			return G_SOURCE_CONTINUE;
		}
		next_step ();
		break;
	case TAKE_FOCUS:
		/* With no window manager one ask can be lost, so ask until it takes. */
		if (!gtk_window_is_active (GTK_WINDOW (window))) {
			if (++asks > 50) {
				report (FALSE, "the window took the focus");
				finish ();
				return G_SOURCE_REMOVE;
			}
			gdk_window_focus (gtk_widget_get_window (window), GDK_CURRENT_TIME);
			return G_SOURCE_CONTINUE;
		}
		if (since < 10) {
			return G_SOURCE_CONTINUE;
		}
		report (TRUE, "the window took the focus");
		hover (TIP_BUTTON);
		next_step ();
		break;
	case FOCUSED_BUTTON:
	case FOCUSED_PLACE:
	case FOCUSED_ICON:
		if (!shown_yet (TIP_BUTTON + (step - FOCUSED_BUTTON), since)) {
			return G_SOURCE_CONTINUE;
		}
		check_tip (TIP_BUTTON + (step - FOCUSED_BUTTON), TRUE);
		if (step != FOCUSED_ICON) {
			hover (TIP_BUTTON + (step - FOCUSED_BUTTON) + 1);
		}
		next_step ();
		break;
	case TO_LIST:
		if (since == 1) {
			gtk_accel_groups_activate (G_OBJECT (window), GDK_KEY_2, GDK_CONTROL_MASK);
		}
		/* The list can take a while to fill when the box is busy. */
		if (since < 20 || (!list_has_rows () && since < 150)) {
			return G_SOURCE_CONTINUE;
		}
		hover (TIP_ROW);
		next_step ();
		break;
	case FOCUSED_ROW:
		if (!shown_yet (TIP_ROW, since)) {
			return G_SOURCE_CONTINUE;
		}
		check_tip (TIP_ROW, TRUE);
		if (!start_thief ()) {
			finish ();
			return G_SOURCE_REMOVE;
		}
		next_step ();
		break;
	case STEAL:
		if (gtk_window_is_active (GTK_WINDOW (window))) {
			if (since > 100) {
				report (FALSE, "another program took the focus");
				finish ();
				return G_SOURCE_REMOVE;
			}
			return G_SOURCE_CONTINUE;
		}
		report (TRUE, "another program took the focus");
		next_step ();
		break;
	case STOLEN:
		if (since < 5) {
			return G_SOURCE_CONTINUE;
		}
		report (!tooltip_showing (), "the tooltip up went when the focus left");
		hover (TIP_ROW);
		next_step ();
		break;
	case UNFOCUSED_ROW:
	case UNFOCUSED_BUTTON:
		if (since < HOVER_TICKS) {
			return G_SOURCE_CONTINUE;
		}
		check_tip (step == UNFOCUSED_ROW ? TIP_ROW : TIP_BUTTON, FALSE);
		hover (step == UNFOCUSED_ROW ? TIP_BUTTON : TIP_PLACE);
		next_step ();
		break;
	case UNFOCUSED_PLACE:
		if (since < HOVER_TICKS) {
			return G_SOURCE_CONTINUE;
		}
		check_tip (TIP_PLACE, FALSE);
		next_step ();
		break;
	case TO_ICONS:
		if (since == 1) {
			gtk_accel_groups_activate (G_OBJECT (window), GDK_KEY_1, GDK_CONTROL_MASK);
		}
		if (since < 20) {
			return G_SOURCE_CONTINUE;
		}
		hover (TIP_ICON);
		next_step ();
		break;
	case UNFOCUSED_ICON:
		if (since < HOVER_TICKS) {
			return G_SOURCE_CONTINUE;
		}
		check_tip (TIP_ICON, FALSE);
		report (!gtk_window_is_active (GTK_WINDOW (window)), "the other program kept the focus throughout");
		next_step ();
		break;
	default:
		finish ();
		return G_SOURCE_REMOVE;
	}

	return G_SOURCE_CONTINUE;
}

__attribute__((constructor)) static void
probe_start (void)
{
	const char *path = g_getenv ("NEMO_TIP_OUT");

	folder_name = g_strdup (g_getenv ("NEMO_TIP_FOLDER"));
	thief = g_strdup (g_getenv ("NEMO_TIP_THIEF"));
	if (path == NULL || folder_name == NULL || thief == NULL) {
		return;
	}
	out = fopen (path, "w");
	/* Anything the program starts loads this too. */
	g_unsetenv ("NEMO_TIP_OUT");
	g_unsetenv ("NEMO_TIP_FOLDER");
	g_unsetenv ("LD_PRELOAD");
	if (out == NULL) {
		return;
	}
	g_timeout_add (100, tick, NULL);
}
