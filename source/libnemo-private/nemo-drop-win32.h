/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */

/* nemo-drop-win32.h - files dropped in from another program on Windows.

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

#ifndef NEMO_DROP_WIN32_H
#define NEMO_DROP_WIN32_H

#include <gtk/gtk.h>

/* Puts our own drop target in front of the toolkit's on every window it opens
 * a drop target for. Call once, before the toolkit starts. FALSE if it could
 * not, and then drops work as the toolkit has them. */
gboolean nemo_drop_win32_prepare (void);

/* What the program the files came from allows for the drag in progress, or
 * the last one: copy, move and link, as the toolkit's actions. All three
 * before any drag has come in. */
GdkDragAction nemo_drop_win32_offered_actions (void);

/* The drop target Windows hands drags to for @toplevel, as an IDropTarget, or
 * NULL if there's none of ours. For tests, which have no mouse to drag with.
 * Returns: (transfer none) */
gpointer nemo_drop_win32_target (GdkWindow *toplevel);

#endif /* NEMO_DROP_WIN32_H */
