/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */

/* nemo-dnd-win32.h - dragging files out to another program on Windows.

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

#ifndef NEMO_DND_WIN32_H
#define NEMO_DND_WIN32_H

#include <gtk/gtk.h>

/* Drag @uri_list as Windows itself would, so programs outside the toolkit see
 * the files. @icon_list is nemo's own payload, carried alongside for our own
 * windows, and may be NULL. @icon is the drag image and may be NULL. @widget is
 * where the drag started. Blocks until the drag ends, and puts what the target
 * did in @performed.
 *
 * A move onto another program removes the originals here, unless that program
 * did the move itself.
 *
 * FALSE means nothing was dragged and the caller should fall back to the
 * toolkit's own drag - anything without a local path cannot go this way. */
gboolean nemo_dnd_win32_drag (GtkWidget       *widget,
			      GdkDragAction    actions,
			      const char      *uri_list,
			      const char      *icon_list,
			      cairo_surface_t *icon,
			      int              hot_x,
			      int              hot_y,
			      GdkDragAction   *performed);

/* Whether drags go out through Windows rather than the toolkit. Off means every
 * call above declines, and nothing here is used. */
gboolean nemo_dnd_win32_enabled (void);

/* The action the held keys and buttons ask for, or 0 for none. Control copies,
 * shift moves, both make a link, and alt or a drag with the right or middle
 * button asks. The toolkit offers copy and move whatever is held, so the keys
 * have to be read here. */
GdkDragAction nemo_dnd_win32_modifier_action (void);

/* The same answer for @keys, a mask of the MK_ flags a drop target is handed,
 * MK_ALT included. */
GdkDragAction nemo_dnd_win32_action_for_keys (guint keys);

/* Called before the toolkit starts, to put it on the protocol our drags speak.
 * Does nothing when the above is off. */
void nemo_dnd_win32_prepare (void);

/* The object a drag hands to the other program, for tests to read back. NULL if
 * the uris have no local paths. Release it with IUnknown::Release. */
gpointer nemo_dnd_win32_data_object (const char    *uri_list,
				     const char    *icon_list,
				     GdkDragAction  actions);

#endif /* NEMO_DND_WIN32_H */
