/* nemo-window-at-point.c - the X11 half. Windows is in nemo-window-at-point-win32.c.
 *
 * Copyright © 2026 t00mietum (CryptogID: ปʬϝღถɔ4რఠΔթะ9ƾǝu)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; version 2 of the License.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 */

#include <config.h>

/* glib is what defines G_OS_WIN32, so it has to come in before the guard. */
#include <glib.h>

#ifndef G_OS_WIN32

#include "nemo-window-at-point.h"

#ifdef GDK_WINDOWING_X11
#include <gdk/gdkx.h>
#include <X11/Xlib.h>
#include <X11/Xatom.h>
#endif

guint64
nemo_window_native_handle (GtkWindow *window)
{
#ifdef GDK_WINDOWING_X11
	GdkWindow *gdk_window = gtk_widget_get_window (GTK_WIDGET (window));

	if (gdk_window != NULL && GDK_IS_X11_WINDOW (gdk_window)) {
		return (guint64) gdk_x11_window_get_xid (gdk_window);
	}
#endif
	return 0;
}

void
nemo_window_allow_to_raise (guint64 handle)
{
}

#ifdef GDK_WINDOWING_X11

/* The child of the root a window hangs from: the frame the window manager put
   around it, or the window itself where nothing did. */
static Window
top_ancestor (Display *xdisplay, Window root, Window window)
{
	Window root_return, parent, *children;
	unsigned int n_children;

	for (;;) {
		if (!XQueryTree (xdisplay, window, &root_return, &parent, &children, &n_children)) {
			return None;
		}
		if (children != NULL) {
			XFree (children);
		}
		if (parent == root || parent == None) {
			return window;
		}
		window = parent;
	}
}

/* Whether the window, and so its frame, is on screen and covers the point. */
static gboolean
covers (Display *xdisplay, Window root, Window window, int x, int y)
{
	XWindowAttributes attrs;
	Window top, child;
	int left, top_edge;

	if (!XGetWindowAttributes (xdisplay, window, &attrs) ||
	    attrs.map_state != IsViewable || attrs.class != InputOutput) {
		return FALSE;
	}

	/* The frame counts too, so a drop on the title bar is not a miss. */
	top = top_ancestor (xdisplay, root, window);
	if (top == None || !XGetWindowAttributes (xdisplay, top, &attrs) ||
	    !XTranslateCoordinates (xdisplay, top, root, 0, 0, &left, &top_edge, &child)) {
		return FALSE;
	}

	return x >= left && y >= top_edge &&
	       x < left + attrs.width + 2 * attrs.border_width &&
	       y < top_edge + attrs.height + 2 * attrs.border_width;
}

/* The window manager's list of what it manages, bottom first. Only real
   windows are on it, so a compositor's own overlay never gets in the way. */
static Window *
stacking_list (Display *xdisplay, Window root, unsigned long *n_windows)
{
	Atom list_atom, type;
	int format;
	unsigned long after;
	unsigned char *data = NULL;

	*n_windows = 0;
	list_atom = XInternAtom (xdisplay, "_NET_CLIENT_LIST_STACKING", True);
	if (list_atom == None ||
	    XGetWindowProperty (xdisplay, root, list_atom, 0, G_MAXLONG, False, XA_WINDOW,
	                        &type, &format, n_windows, &after, &data) != Success) {
		return NULL;
	}
	if (type != XA_WINDOW || format != 32 || *n_windows == 0) {
		if (data != NULL) {
			XFree (data);
		}
		*n_windows = 0;
		return NULL;
	}

	return (Window *) data;
}

static int
find_candidate (Window window, const Window *windows, int n)
{
	int i;

	for (i = 0; i < n; i++) {
		if (windows[i] == window) {
			return i;
		}
	}

	return -1;
}

static int
x11_window_at_pointer (Display *xdisplay, const guint64 *candidates, int n_candidates)
{
	Window root, root_return, child, parent, *windows, *tops, *children = NULL;
	unsigned long n_windows;
	unsigned int n_children, mask;
	int x, y, win_x, win_y, i, found = -1;

	root = DefaultRootWindow (xdisplay);
	if (!XQueryPointer (xdisplay, root, &root_return, &child, &x, &y, &win_x, &win_y, &mask)) {
		return -1;
	}

	windows = stacking_list (xdisplay, root, &n_windows);
	if (windows != NULL) {
		Window *ours = g_new (Window, n_candidates);

		for (i = 0; i < n_candidates; i++) {
			ours[i] = (Window) candidates[i];
		}
		for (i = (int) n_windows - 1; i >= 0; i--) {
			if (covers (xdisplay, root, windows[i], x, y)) {
				found = find_candidate (windows[i], ours, n_candidates);
				break;
			}
		}
		g_free (ours);
		XFree (windows);

		return found;
	}

	/* No window manager, or one that keeps no list: the root's own children
	   are in stacking order, frames where there are frames. Popups set
	   override-redirect and are passed over. */
	if (!XQueryTree (xdisplay, root, &root_return, &parent, &children, &n_children)) {
		return -1;
	}
	tops = g_new (Window, n_candidates);
	for (i = 0; i < n_candidates; i++) {
		tops[i] = top_ancestor (xdisplay, root, (Window) candidates[i]);
	}
	for (i = (int) n_children - 1; i >= 0; i--) {
		XWindowAttributes attrs;

		if (!XGetWindowAttributes (xdisplay, children[i], &attrs) || attrs.override_redirect) {
			continue;
		}
		if (covers (xdisplay, root, children[i], x, y)) {
			found = find_candidate (children[i], tops, n_candidates);
			break;
		}
	}
	g_free (tops);
	if (children != NULL) {
		XFree (children);
	}

	return found;
}

#endif

int
nemo_window_at_pointer (GdkDisplay    *display,
                        const guint64 *candidates,
                        int            n_candidates)
{
#ifdef GDK_WINDOWING_X11
	int found;

	if (n_candidates <= 0 || !GDK_IS_X11_DISPLAY (display)) {
		return -1;
	}

	/* A window can go away between the list and the question about it. */
	gdk_x11_display_error_trap_push (display);
	found = x11_window_at_pointer (GDK_DISPLAY_XDISPLAY (display), candidates, n_candidates);
	gdk_x11_display_error_trap_pop_ignored (display);

	return found;
#else
	return -1;
#endif
}

#endif
