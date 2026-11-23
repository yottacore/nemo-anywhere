/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */

/* nemo-zoom-resize.h - how much a window grows or shrinks with a zoom.

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

/* A keyboard zoom scales the window's contents area by about as much as the
 * contents themselves grew, so the same files stay in view. The toolbars,
 * side panes and status bar keep their size.
 *
 * Nothing but arithmetic lives here, so it can be checked without a screen.
 */

#ifndef NEMO_ZOOM_RESIZE_H
#define NEMO_ZOOM_RESIZE_H

#include <glib.h>

G_BEGIN_DECLS

typedef enum {
	NEMO_ZOOM_RESIZE_ICON,
	NEMO_ZOOM_RESIZE_COMPACT,
	NEMO_ZOOM_RESIZE_LIST
} NemoZoomResizeKind;

/* Kept per window. A screen edge stops the window short of where the zoom
 * would take it, and the size it would have had is kept, so zooming back out
 * the same number of steps comes back to where it started. A size the window
 * wasn't asked for, such as one dragged by hand, starts over from there. */
typedef struct {
	int asked_width;	/* the size the last zoom asked for, 0 before any */
	int asked_height;
	int before_width;	/* the size when it asked, until the window catches up */
	int before_height;
	int full_width;		/* where the zooms so far would have taken it */
	int full_height;
} NemoZoomResize;

/* How much wider and taller the contents draw after a zoom from one icon size
 * to another. */
void     nemo_zoom_resize_ratios (NemoZoomResizeKind kind,
				  int                old_size,
				  int                new_size,
				  double            *ratio_x,
				  double            *ratio_y);

/* The window size to ask for, given its size now, the part of it the contents
 * take up, and the most it may be, 0 for no limit. A zoom in never shrinks the
 * window and a zoom out never grows it. Returns FALSE when the size stays. */
gboolean nemo_zoom_resize_next   (NemoZoomResize    *state,
				  int                width,
				  int                height,
				  int                content_width,
				  int                content_height,
				  double             ratio_x,
				  double             ratio_y,
				  int                max_width,
				  int                max_height,
				  int               *new_width,
				  int               *new_height);

G_END_DECLS

#endif /* NEMO_ZOOM_RESIZE_H */
