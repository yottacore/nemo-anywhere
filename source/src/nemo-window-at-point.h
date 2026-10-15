/* nemo-window-at-point.h - which of our windows, in any process, is under the pointer.
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

#ifndef NEMO_WINDOW_AT_POINT_H
#define NEMO_WINDOW_AT_POINT_H

#include <gtk/gtk.h>

G_BEGIN_DECLS

/* The window system's own handle for a toplevel: the XID on X11, the HWND on
 * Windows. 0 where there is no such thing, as on Wayland, or before it is
 * realized. Another process can be handed it and find the same window. */
guint64 nemo_window_native_handle (GtkWindow *window);

/* Index of the candidate that is the topmost window under the pointer, or -1
 * when none is, or when something else is on top of it. Menus, tooltips and
 * the drag icon are looked through. -1 as well where the window system will
 * not say, as on Wayland. */
int nemo_window_at_pointer (GdkDisplay    *display,
                            const guint64 *candidates,
                            int            n_candidates);

/* Lets the process that owns the window with this native handle bring it to
 * the front once, as the one the user is working in may. No other process
 * gets the right. Nothing to do off Windows; the X11 timestamp passed along
 * does the same job there. */
void nemo_window_allow_to_raise (guint64 handle);

G_END_DECLS

#endif
