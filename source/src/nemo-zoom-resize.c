/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */

/* nemo-zoom-resize.c - how much a window grows or shrinks with a zoom.

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

#include <config.h>

#include "nemo-zoom-resize.h"

#include <math.h>

#include <libnemo-private/nemo-icon-info.h>

/* The list view scales its text by this much a step, and the text sets the
   column widths and most of the row height. */
#define LIST_TEXT_STEP 1.2

void
nemo_zoom_resize_ratios (NemoZoomResizeKind kind,
			 int                old_size,
			 int                new_size,
			 double            *ratio_x,
			 double            *ratio_y)
{
	int steps;

	*ratio_x = 1.0;
	*ratio_y = 1.0;

	if (old_size <= 0 || new_size <= 0 || old_size == new_size) {
		return;
	}

	switch (kind) {
	case NEMO_ZOOM_RESIZE_LIST:
		steps = nemo_icon_size_legacy_level (new_size) -
			nemo_icon_size_legacy_level (old_size);
		*ratio_x = pow (LIST_TEXT_STEP, steps);
		*ratio_y = *ratio_x;
		break;
	case NEMO_ZOOM_RESIZE_COMPACT:
		/* The names beside the icons keep their size, and they are most of a
		   column's width, so only the rows grow. */
		*ratio_y = (double) new_size / old_size;
		break;
	case NEMO_ZOOM_RESIZE_ICON:
	default:
		*ratio_x = (double) new_size / old_size;
		*ratio_y = *ratio_x;
		break;
	}
}

/* One axis. `full` is where the zooms so far would have put it. */
static int
next_length (int  length,
	     int  content,
	     int *full,
	     double ratio,
	     int  max_length)
{
	int chrome, target;

	chrome = length - content;
	*full = chrome + MAX (1, (int) lround ((*full - chrome) * ratio));

	target = *full;
	if (max_length > 0 && target > max_length) {
		target = max_length;
	}

	if (ratio >= 1.0) {
		return MAX (length, target);
	}

	return MIN (length, target);
}

gboolean
nemo_zoom_resize_next (NemoZoomResize *state,
		       int             width,
		       int             height,
		       int             content_width,
		       int             content_height,
		       double          ratio_x,
		       double          ratio_y,
		       int             max_width,
		       int             max_height,
		       int            *new_width,
		       int            *new_height)
{
	gboolean ours, pending;

	*new_width = width;
	*new_height = height;

	if (content_width <= 0 || content_height <= 0 ||
	    content_width > width || content_height > height) {
		return FALSE;
	}

	/* The window either has the size the last zoom asked for, or hasn't caught
	   up with it yet, as when 2 zooms come quicker than the window manager.
	   Either way the next step starts from the size asked for. */
	ours = state->asked_width > 0 &&
	       width == state->asked_width && height == state->asked_height;
	pending = !ours && state->asked_width > 0 &&
		  width == state->before_width && height == state->before_height;

	if (pending) {
		content_width += state->asked_width - width;
		content_height += state->asked_height - height;
		width = state->asked_width;
		height = state->asked_height;
	} else {
		if (!ours) {
			state->full_width = width;
			state->full_height = height;
		}
		state->before_width = width;
		state->before_height = height;
	}

	*new_width = next_length (width, content_width, &state->full_width,
				  ratio_x, max_width);
	*new_height = next_length (height, content_height, &state->full_height,
				   ratio_y, max_height);

	state->asked_width = *new_width;
	state->asked_height = *new_height;

	return *new_width != width || *new_height != height;
}
