/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */

/* eel-gtk-extensions.c - implementation of new functions that operate on
  			  gtk classes. Perhaps some of these should be
  			  rolled into gtk someday.

   Copyright (C) 1999, 2000, 2001 Eazel, Inc.

   The Gnome Library is free software; you can redistribute it and/or
   modify it under the terms of the GNU Library General Public License as
   published by the Free Software Foundation; either version 2 of the
   License, or (at your option) any later version.

   The Gnome Library is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   Library General Public License for more details.

   You should have received a copy of the GNU Library General Public
   License along with the Gnome Library; see the file COPYING.LIB.  If not,
   write to the Free Software Foundation, Inc., 51 Franklin Street - Suite 500,
   Boston, MA 02110-1335, USA.

   Authors: John Sullivan <sullivan@eazel.com>
            Ramiro Estrugo <ramiro@eazel.com>
	    Darin Adler <darin@eazel.com>
*/

#include <config.h>
#include "eel-gtk-extensions.h"

#include "eel-glib-extensions.h"
#include "eel-gnome-extensions.h"
#include "eel-gdk-extensions.h"
#include "eel-string.h"

#ifdef GDK_WINDOWING_X11
#include <X11/Xlib.h>
#include <X11/Xatom.h>
#include <gdk/gdkx.h>
#endif
#include <gdk/gdk.h>
#include <gdk/gdkprivate.h>
#ifdef GDK_WINDOWING_WAYLAND
#include <gdk/gdkwayland.h>
#endif
#include <gtk/gtk.h>
#include <glib/gi18n-lib.h>
#include <math.h>

/* This number is fairly arbitrary. Long enough to show a pretty long
 * menu title, but not so long to make a menu grotesquely wide.
 */
#define MAXIMUM_MENU_TITLE_LENGTH	48

/* Used for window position & size sanity-checking. The sizes are big enough to prevent
 * at least normal-sized gnome panels from obscuring the window at the screen edges.
 */
#define MINIMUM_ON_SCREEN_WIDTH		100
#define MINIMUM_ON_SCREEN_HEIGHT	100


/**
 * eel_gtk_window_get_geometry_string:
 * @window: a #GtkWindow
 *
 * Obtains the geometry string for this window, suitable for
 * set_geometry_string(); assumes the window has NorthWest gravity
 *
 * Return value: geometry string, must be freed
 **/
char*
eel_gtk_window_get_geometry_string (GtkWindow *window)
{
	char *str;
	int w, h, x, y;

	g_return_val_if_fail (GTK_IS_WINDOW (window), NULL);
	g_return_val_if_fail (gtk_window_get_gravity (window) ==
			      GDK_GRAVITY_NORTH_WEST, NULL);

	gtk_window_get_position (window, &x, &y);
	gtk_window_get_size (window, &w, &h);

	str = g_strdup_printf ("%dx%d+%d+%d", w, h, x, y);

	return str;
}

static void
sanity_check_window_position (int *left, int *top)
{
	g_assert (left != NULL);
	g_assert (top != NULL);

	/* Make sure the top of the window is on screen, for
	 * draggability (might not be necessary with all window managers,
	 * but seems reasonable anyway). Make sure the top of the window
	 * isn't off the bottom of the screen, or so close to the bottom
	 * that it might be obscured by the panel.
	 */
	*top = CLAMP (*top, 0, gdk_screen_height() - MINIMUM_ON_SCREEN_HEIGHT);

	/* FIXME bugzilla.eazel.com 669:
	 * If window has negative left coordinate, set_uposition sends it
	 * somewhere else entirely. Not sure what level contains this bug (XWindows?).
	 * Hacked around by pinning the left edge to zero, which just means you
	 * can't set a window to be partly off the left of the screen using
	 * this routine.
	 */
	/* Make sure the left edge of the window isn't off the right edge of
	 * the screen, or so close to the right edge that it might be
	 * obscured by the panel.
	 */
	*left = CLAMP (*left, 0, gdk_screen_width() - MINIMUM_ON_SCREEN_WIDTH);
}

static void
sanity_check_window_dimensions (guint *width, guint *height)
{
	g_assert (width != NULL);
	g_assert (height != NULL);

	/* Pin the size of the window to the screen, so we don't end up in
	 * a state where the window is so big essential parts of it can't
	 * be reached (might not be necessary with all window managers,
	 * but seems reasonable anyway).
	 */
	*width = MIN ((int)*width, gdk_screen_width());
	*height = MIN ((int)*height, gdk_screen_height());
}

/**
 * eel_gtk_window_set_initial_geometry:
 *
 * Sets the position and size of a GtkWindow before the
 * GtkWindow is shown. It is an error to call this on a window that
 * is already on-screen. Takes into account screen size, and does
 * some sanity-checking on the passed-in values.
 *
 * @window: A non-visible GtkWindow
 * @geometry_flags: A EelGdkGeometryFlags value defining which of
 * the following parameters have defined values
 * @left: pixel coordinate for left of window
 * @top: pixel coordinate for top of window
 * @width: width of window in pixels
 * @height: height of window in pixels
 */
static void
eel_gtk_window_set_initial_geometry (GtkWindow *window,
					  EelGdkGeometryFlags geometry_flags,
					  int left,
					  int top,
					  guint width,
					  guint height)
{
	GdkScreen *screen;
	int real_left, real_top;
	int screen_width, screen_height;

	g_return_if_fail (GTK_IS_WINDOW (window));

	/* Setting the default size doesn't work when the window is already showing.
	 * Someday we could make this move an already-showing window, but we don't
	 * need that functionality yet.
	 */
	g_return_if_fail (!gtk_widget_get_visible (GTK_WIDGET (window)));

	if ((geometry_flags & EEL_GDK_X_VALUE) && (geometry_flags & EEL_GDK_Y_VALUE)) {
		real_left = left;
		real_top = top;

		screen = gtk_window_get_screen (window);
		screen_width  = gdk_screen_get_width  (screen);
		screen_height = gdk_screen_get_height (screen);

		/* GDK doesn't allow win_gravity South/East, so place by hand: "-N"
		 * means the window's right (or bottom) edge sits N pixels in from
		 * that screen edge. The parser has already made real_left negative,
		 * so the sign is applied once here, and the window's own size is
		 * subtracted - without either, -20-20 ended up off-screen and got
		 * clamped back to the primary monitor's origin.
		 */
		if (geometry_flags & (EEL_GDK_X_NEGATIVE | EEL_GDK_Y_NEGATIVE)) {
			int win_width = (int) width;
			int win_height = (int) height;

			if (!(geometry_flags & EEL_GDK_WIDTH_VALUE) ||
			    !(geometry_flags & EEL_GDK_HEIGHT_VALUE)) {
				gtk_window_get_default_size (window,
							     (geometry_flags & EEL_GDK_WIDTH_VALUE) ? NULL : &win_width,
							     (geometry_flags & EEL_GDK_HEIGHT_VALUE) ? NULL : &win_height);
			}

			if (geometry_flags & EEL_GDK_X_NEGATIVE) {
				real_left = screen_width - MAX (win_width, 0) + real_left;
			}
			if (geometry_flags & EEL_GDK_Y_NEGATIVE) {
				real_top = screen_height - MAX (win_height, 0) + real_top;
			}
		}

		sanity_check_window_position (&real_left, &real_top);
		gtk_window_move (window, real_left, real_top);
	}

	if ((geometry_flags & EEL_GDK_WIDTH_VALUE) && (geometry_flags & EEL_GDK_HEIGHT_VALUE)) {
		sanity_check_window_dimensions (&width, &height);
		gtk_window_set_default_size (GTK_WINDOW (window), (int)width, (int)height);
	}
}

/**
 * eel_gtk_window_set_initial_geometry_from_string:
 *
 * Sets the position and size of a GtkWindow before the
 * GtkWindow is shown. The geometry is passed in as a string.
 * It is an error to call this on a window that
 * is already on-screen. Takes into account screen size, and does
 * some sanity-checking on the passed-in values.
 *
 * @window: A non-visible GtkWindow
 * @geometry_string: A string suitable for use with eel_gdk_parse_geometry
 * @minimum_width: If the width from the string is smaller than this,
 * use this for the width.
 * @minimum_height: If the height from the string is smaller than this,
 * use this for the height.
 * @ignore_position: If true position data from string will be ignored.
 */
void
eel_gtk_window_set_initial_geometry_from_string (GtkWindow *window,
						 const char *geometry_string,
						 guint minimum_width,
						 guint minimum_height,
						 gboolean ignore_position)
{
	int left, top;
	guint width, height;
	EelGdkGeometryFlags geometry_flags;

	g_return_if_fail (GTK_IS_WINDOW (window));
	g_return_if_fail (geometry_string != NULL);

	/* Setting the default size doesn't work when the window is already showing.
	 * Someday we could make this move an already-showing window, but we don't
	 * need that functionality yet.
	 */
	g_return_if_fail (!gtk_widget_get_visible (GTK_WIDGET (window)));

	geometry_flags = eel_gdk_parse_geometry (geometry_string, &left, &top, &width, &height);

	/* Make sure the window isn't smaller than makes sense for this window.
	 * Other sanity checks are performed in set_initial_geometry.
	 */
	if (geometry_flags & EEL_GDK_WIDTH_VALUE) {
		width = MAX (width, minimum_width);
	}
	if (geometry_flags & EEL_GDK_HEIGHT_VALUE) {
		height = MAX (height, minimum_height);
	}

	/* Ignore saved window position if requested. */
	if (ignore_position) {
		geometry_flags &= ~(EEL_GDK_X_VALUE | EEL_GDK_Y_VALUE);
	}

	eel_gtk_window_set_initial_geometry (window, geometry_flags, left, top, width, height);
}

gboolean
eel_check_is_wayland (void)
{
    static gboolean using_wayland = FALSE;
#ifdef GDK_WINDOWING_WAYLAND
    static gsize once_init = 0;

    if (g_once_init_enter (&once_init)) {
        using_wayland = GDK_IS_WAYLAND_DISPLAY (gdk_display_get_default ());

        g_once_init_leave (&once_init, 1);
    }
#endif
    return using_wayland;
}

static void
create_popup_rect (GdkWindow *window, GdkRectangle *rect)
{
    GdkSeat *seat;
    GdkDevice *device;

    rect->x = 0;
    rect->y = 0;
    rect->width = 2;
    rect->height = 2;

    seat = gdk_display_get_default_seat (gdk_display_get_default ());

    if (seat != NULL) {
        device = gdk_seat_get_pointer (seat);

        if (device != NULL) {
            gint x, y;

            gdk_window_get_device_position (window, device, &x, &y, NULL);
            rect->x = x;
            rect->y = y;
        }
    }
}

/* The menu key and Ctrl+F10 have no pointer behind them, and the pointer can be
   anywhere - another window, another monitor - so a menu placed there reads as
   the key having done nothing. Put it against whatever holds the focus. */
static void
create_keyboard_popup_rect (GtkWidget *widget, GdkRectangle *rect)
{
    GtkWidget *toplevel = gtk_widget_get_toplevel (widget);
    GtkWidget *anchor = NULL;
    GtkAllocation allocation;
    gint x = 0, y = 0;

    if (GTK_IS_WINDOW (toplevel)) {
        anchor = gtk_window_get_focus (GTK_WINDOW (toplevel));
    }

    if (anchor == NULL || !gtk_widget_is_ancestor (anchor, toplevel)) {
        anchor = widget;
    }

    gtk_widget_get_allocation (anchor, &allocation);
    gtk_widget_translate_coordinates (anchor, toplevel, 0, 0, &x, &y);

    /* A little way in, so the menu does not sit flush against the pane edge. */
    rect->x = x + MIN (16, allocation.width / 2);
    rect->y = y + MIN (16, allocation.height / 2);
    rect->width = 2;
    rect->height = 2;
}

/* The part of an item that is on screen, in the toplevel's coordinates. */
static gboolean
item_popup_rect (GtkWidget *widget, const GdkRectangle *item, GdkRectangle *rect)
{
    GdkRectangle bounds = { 0, 0, 0, 0 };
    GdkRectangle visible;

    if (item == NULL) {
        return FALSE;
    }

    bounds.width = gtk_widget_get_allocated_width (widget);
    bounds.height = gtk_widget_get_allocated_height (widget);
    if (!gdk_rectangle_intersect (item, &bounds, &visible)) {
        return FALSE;
    }
    if (!gtk_widget_translate_coordinates (widget, gtk_widget_get_toplevel (widget),
                                           visible.x, visible.y, &visible.x, &visible.y)) {
        return FALSE;
    }

    *rect = visible;
    return TRUE;
}

/**
 * eel_pop_up_context_menu_at_item:
 * @item: (nullable): the item the menu is for, in @widget's coordinates.
 *
 * Pop up a context menu under the mouse. With no event behind it, the menu
 * opens just below @item, or against the focused widget when @item is NULL
 * or scrolled out of view.
 * The menu is sunk after use, so it will be destroyed unless the
 * caller first ref'ed it.
 **/
void
eel_pop_up_context_menu_at_item (GtkMenu            *menu,
                                 GdkEvent           *event,
                                 GtkWidget          *widget,
                                 const GdkRectangle *item)
{
    g_return_if_fail (GTK_IS_MENU (menu));

    // Using gtk_menu_popup_at_rect exclusively in wayland seems to avoid the problem
    // of being unable to dismiss the menu when clicking to the left of it. See:
    // https://github.com/linuxmint/nemo/issues/3218

#ifdef GDK_WINDOWING_X11
    if (!eel_check_is_wayland () && event && event->type == GDK_BUTTON_PRESS) {
        gtk_menu_popup_at_pointer (menu, event);
    } else
#endif
    {
        GdkWindow *window = gtk_widget_get_window (gtk_widget_get_toplevel (widget));
        GdkGravity rect_anchor = GDK_GRAVITY_NORTH_WEST;
        GdkRectangle rect;

        if (event != NULL) {
            create_popup_rect (window, &rect);
        } else if (item_popup_rect (widget, item, &rect)) {
            /* Below the item, so it stays in sight. GTK flips the menu above
               it when there is no room below. */
            rect_anchor = GDK_GRAVITY_SOUTH_WEST;
        } else {
            create_keyboard_popup_rect (widget, &rect);
        }

        gtk_menu_popup_at_rect (menu,
                                window,
                                &rect,
                                rect_anchor,
                                GDK_GRAVITY_NORTH_WEST,
                                NULL);
    }

	g_object_ref_sink (menu);
	g_object_unref (menu);
}

/**
 * eel_pop_up_context_menu:
 *
 * Pop up a context menu under the mouse, or against the focused widget when
 * there is no event behind it.
 * The menu is sunk after use, so it will be destroyed unless the
 * caller first ref'ed it.
 **/
void
eel_pop_up_context_menu (GtkMenu        *menu,
                         GdkEvent       *event,
                         GtkWidget      *widget)
{
    eel_pop_up_context_menu_at_item (menu, event, widget, NULL);
}

static gboolean
destroy_menu_idle (gpointer data)
{
    gtk_widget_destroy (GTK_WIDGET (data));
    return G_SOURCE_REMOVE;
}

static void
destroy_closed_menu (GtkMenuShell *menu, gpointer data)
{
    /* The chosen item is activated after the menu closes, and destroying the
       menu now would drop the item's handlers before that. */
    g_idle_add_full (G_PRIORITY_DEFAULT_IDLE, destroy_menu_idle,
                     g_object_ref (menu), g_object_unref);
}

/**
 * eel_gtk_menu_destroy_on_close:
 *
 * For a menu built each time it is opened. It is destroyed once it closes,
 * since nothing else frees it, attached or not.
 **/
void
eel_gtk_menu_destroy_on_close (GtkMenu *menu)
{
    g_return_if_fail (GTK_IS_MENU (menu));

    g_signal_connect (menu, "deactivate", G_CALLBACK (destroy_closed_menu), NULL);
}

GtkMenuItem *
eel_gtk_menu_append_separator (GtkMenu *menu)
{
	return eel_gtk_menu_insert_separator (menu, -1);
}

GtkMenuItem *
eel_gtk_menu_insert_separator (GtkMenu *menu, int index)
{
	GtkWidget *menu_item;

	menu_item = gtk_separator_menu_item_new ();
	gtk_widget_show (menu_item);
	gtk_menu_shell_insert (GTK_MENU_SHELL (menu), menu_item, index);

	return GTK_MENU_ITEM (menu_item);
}

void
eel_gtk_message_dialog_set_details_label (GtkMessageDialog *dialog,
				  const gchar *details_text)
{
	GtkWidget *content_area, *expander, *label;

	content_area = gtk_message_dialog_get_message_area (dialog);
	expander = gtk_expander_new_with_mnemonic (_("Show more _details"));
	gtk_expander_set_spacing (GTK_EXPANDER (expander), 6);

	label = gtk_label_new (details_text);
	gtk_label_set_line_wrap (GTK_LABEL (label), TRUE);
	gtk_label_set_selectable (GTK_LABEL (label), TRUE);
	gtk_misc_set_alignment (GTK_MISC (label), 0.0, 0.5);

	gtk_container_add (GTK_CONTAINER (expander), label);
	gtk_box_pack_start (GTK_BOX (content_area), expander, FALSE, FALSE, 0);

	gtk_widget_show (label);
	gtk_widget_show (expander);
}

gulong
eel_gtk_get_window_xid (GtkWindow *window)
{
    g_return_val_if_fail (GTK_IS_WINDOW (window), 0);

#ifdef GDK_WINDOWING_X11
    if (eel_check_is_wayland ()) {
        g_debug ("eel_gtk_get_window_xid: called on Wayland, returning 0");
        return 0;
    }

    GdkWindow *gdkw = gtk_widget_get_window (GTK_WIDGET (window));
    g_return_val_if_fail (GDK_IS_X11_WINDOW (gdkw), 0);

    return gdk_x11_window_get_xid (gdkw);
#else
    return 0;
#endif
}

gboolean
eel_gtk_get_treeview_pointer_location (GtkTreeView *treeview,
                               gint *x, gint *y)
{
    GdkWindow *bin_window;

    gint out_x, out_y;

    *x = *y = 0;

    bin_window = gtk_tree_view_get_bin_window (treeview);

    if (bin_window != NULL) {
        GdkDevice *device = eel_gdk_get_pointer_device ();
        if (device != NULL) {
            gdk_window_get_device_position (bin_window, device, &out_x, &out_y, NULL);

            *x = out_x;
            *y = out_y;

            return TRUE;
        }
    }

    return FALSE;
}

gboolean
eel_gtk_get_treeview_row_text_at_pos (GtkTreeView *tree_view,
                                      gint         x,
                                      gint         y)
{
    GdkRectangle area;
    GtkTreePath *path = NULL;
    GtkTreeViewColumn *column = NULL;

    gboolean inside;

    // A positive is_blank_at_pos is a reliable result.
    if (gtk_tree_view_is_blank_at_pos (tree_view, x, y, &path, &column, NULL, NULL)) {
        gtk_tree_path_free (path);
        return FALSE;
    }

    // It also answers no when the position is past the last row, and hands back no path
    // at all - freeing the uninitialized one crashed a drag over the empty space below
    // a list. Nothing there to be over.
    if (path == NULL) {
        return FALSE;
    }

    // If not, there's an additional check to do, as the small gap (1px?) between rows
    // can cause is_blank_at_pos to incorrectly return FALSE when that's not actually the case.
    // Make sure the position is actually inside the cell's bounds, and ignore edge events.
    gtk_tree_view_get_cell_area (tree_view, path, column, &area);
    inside = (x > area.x && x < area.x + area.width &&
              y > area.y && y < area.y + area.height);

    // This runs on every drag-motion event, so the path cannot be leaked.
    gtk_tree_path_free (path);

    return inside;
}

/* Where a row's cell in column sits, in the tree view's own coordinates, cut
   to the part scrolled into view. FALSE when none of it is, or the row is not
   shown at all, such as one inside a collapsed folder. */
gboolean
eel_gtk_tree_view_get_row_rect (GtkTreeView       *tree_view,
                                GtkTreePath       *path,
                                GtkTreeViewColumn *column,
                                GdkRectangle      *rect)
{
    GdkRectangle cell, row, visible;

    gtk_tree_view_get_cell_area (tree_view, path, column, &cell);
    gtk_tree_view_get_background_area (tree_view, path, column, &row);
    if (row.height <= 0) {
        return FALSE;
    }
    /* The whole row height, so the menu clears the row and not just its text. */
    cell.y = row.y;
    cell.height = row.height;

    gtk_tree_view_get_visible_rect (tree_view, &visible);
    gtk_tree_view_convert_tree_to_bin_window_coords (tree_view, visible.x, visible.y,
                                                     &visible.x, &visible.y);
    if (!gdk_rectangle_intersect (&cell, &visible, rect)) {
        return FALSE;
    }

    gtk_tree_view_convert_bin_window_to_widget_coords (tree_view, rect->x, rect->y,
                                                       &rect->x, &rect->y);
    return TRUE;
}

/* Escape in a list: nothing selected, and the cursor back on the first row
   so the next arrow key starts over, with the view left where it was. */
void
eel_gtk_tree_view_forget_cursor (GtkTreeView *tree_view)
{
	GtkTreeSelection *selection = gtk_tree_view_get_selection (tree_view);
	GtkTreeModel *model = gtk_tree_view_get_model (tree_view);
	GtkAdjustment *vadjustment;
	GtkTreePath *path;
	gdouble scrolled;

	if (model != NULL && gtk_tree_model_iter_n_children (model, NULL) > 0) {
		/* Setting the cursor selects its row and scrolls to it. Neither is wanted. */
		vadjustment = gtk_scrollable_get_vadjustment (GTK_SCROLLABLE (tree_view));
		scrolled = gtk_adjustment_get_value (vadjustment);
		path = gtk_tree_path_new_first ();
		gtk_tree_view_set_cursor (tree_view, path, NULL, FALSE);
		gtk_tree_path_free (path);
		gtk_adjustment_set_value (vadjustment, scrolled);
	}

	gtk_tree_selection_unselect_all (selection);
}

/* The whole value of a cell too narrow to show it, or NULL if it fits. Widths
   come from the renderers rather than the column, because a column's own size
   answer is its minimum and says nothing about text that can ellipsize. */
gchar *
eel_gtk_tree_view_column_clipped_text (GtkTreeViewColumn *column,
                                       GtkTreeModel      *model,
                                       GtkTreeIter       *iter)
{
	GList *cells, *l;
	gchar *text = NULL;
	gint wanted = 0;

	gtk_tree_view_column_cell_set_cell_data (column, model, iter, FALSE, FALSE);

	cells = gtk_cell_layout_get_cells (GTK_CELL_LAYOUT (column));
	for (l = cells; l != NULL; l = l->next) {
		GtkCellRenderer *cell = l->data;
		gint minimum, natural;

		gtk_cell_renderer_get_preferred_width (cell,
						       gtk_tree_view_column_get_tree_view (column),
						       &minimum, &natural);
		wanted += natural;

		if (text == NULL && GTK_IS_CELL_RENDERER_TEXT (cell)) {
			g_object_get (cell, "text", &text, NULL);
		}
	}
	g_list_free (cells);

	if (text != NULL &&
	    (*text == '\0' || wanted <= gtk_tree_view_column_get_width (column))) {
		g_clear_pointer (&text, g_free);
	}

	return text;
}

gboolean
eel_gtk_get_treeview_row_text_is_under_pointer (GtkTreeView *tree_view)
{
    gint x, y;

    if (eel_gtk_get_treeview_pointer_location (tree_view, &x, &y)) {
        return eel_gtk_get_treeview_row_text_at_pos (tree_view, x, y);
    }

    // If we can't figure out the location, we need to default to allowing the operation. The highlighting will
    // be accurate, so the user knows.
    return TRUE;
}

static void
refuse_focus_cb (GtkWidget  *widget,
                 GParamSpec *pspec,
                 gpointer    user_data)
{
    if (gtk_widget_get_can_focus (widget)) {
        gtk_widget_set_can_focus (widget, FALSE);
    }
}

// GTK turns a column header's can-focus back on whenever the column's title or
// sort arrow changes, so turning it off once does not stick.
void
eel_gtk_widget_refuse_focus (GtkWidget *widget)
{
    gtk_widget_set_can_focus (widget, FALSE);

    if (g_signal_handler_find (widget, G_SIGNAL_MATCH_FUNC, 0, 0, NULL, refuse_focus_cb, NULL) == 0) {
        g_signal_connect (widget, "notify::can-focus", G_CALLBACK (refuse_focus_cb), NULL);
    }
}

static GtkWidget *
current_page (GtkNotebook *notebook)
{
    return gtk_notebook_get_nth_page (notebook, gtk_notebook_get_current_page (notebook));
}

static gboolean
notebook_focus_cb (GtkWidget        *notebook,
                   GtkDirectionType  direction,
                   gpointer          user_data)
{
    GtkWidget *page = current_page (GTK_NOTEBOOK (notebook));

    // The notebook's own handler would stop on the tabs on the way in or out.
    g_signal_stop_emission_by_name (notebook, "focus");

    return page != NULL && gtk_widget_child_focus (page, direction);
}

static void
notebook_grab_focus_cb (GtkWidget *notebook,
                        gpointer   user_data)
{
    GtkWidget *page = current_page (GTK_NOTEBOOK (notebook));
    GtkWidget *toplevel, *focus = NULL;

    g_signal_stop_emission_by_name (notebook, "grab-focus");

    if (page == NULL) {
        return;
    }

    toplevel = gtk_widget_get_toplevel (notebook);
    if (GTK_IS_WINDOW (toplevel)) {
        focus = gtk_window_get_focus (GTK_WINDOW (toplevel));
    }

    if (focus == NULL || !gtk_widget_is_ancestor (focus, page)) {
        gtk_widget_child_focus (page, GTK_DIR_TAB_FORWARD);
    }
}

// Keyboard focus passes through to the page, and a click on a tab puts it there
// too. Action widgets are not handled, since nothing here uses them.
void
eel_gtk_notebook_keep_focus_off_tabs (GtkNotebook *notebook)
{
    g_signal_connect (notebook, "focus", G_CALLBACK (notebook_focus_cb), NULL);
    g_signal_connect (notebook, "grab-focus", G_CALLBACK (notebook_grab_focus_cb), NULL);
}

// Whether the toplevel's focus sits on this widget or inside it.
gboolean
eel_gtk_focus_is_within (GtkWidget *container)
{
    GtkWidget *toplevel, *focus;

    if (container == NULL) {
        return FALSE;
    }

    toplevel = gtk_widget_get_toplevel (container);
    if (!GTK_IS_WINDOW (toplevel)) {
        return FALSE;
    }

    focus = gtk_window_get_focus (GTK_WINDOW (toplevel));
    if (focus == NULL) {
        return FALSE;
    }

    return focus == container || gtk_widget_is_ancestor (focus, container);
}

// GTK uses the same key for shortcuts and for adding to a selection, so both
// ask this. The window picks the display; NULL takes the default one.
GdkModifierType
eel_gtk_primary_mask (GdkWindow *window)
{
    GdkDisplay *display;

    display = window != NULL ? gdk_window_get_display (window) : gdk_display_get_default ();
    if (display == NULL) {
        return GDK_CONTROL_MASK;
    }

    return gdk_keymap_get_modifier_mask (gdk_keymap_get_for_display (display),
                                         GDK_MODIFIER_INTENT_PRIMARY_ACCELERATOR);
}
