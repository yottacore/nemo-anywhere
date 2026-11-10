/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */

/* nemo-pane-layout.c - where a divider sits when the window changes width.

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

#include "nemo-pane-layout.h"

int
nemo_pane_layout_scale_position (int position,
				 int old_width,
				 int new_width,
				 int floor_width)
{
	gint64 scaled;
	int lo, hi;

	if (old_width <= 0 || new_width <= 0) {
		return position;
	}

	if (floor_width < 0) {
		floor_width = 0;
	}

	if (position < 0) {
		position = 0;
	}

	/* Rounded to nearest, or a run of one-pixel resizes walks the divider
	   toward whichever end the truncation favors. 64-bit because a wide
	   position times a wide width overflows an int well before either is
	   an implausible number of pixels. */
	scaled = ((gint64) position * new_width + old_width / 2) / old_width;

	lo = floor_width;
	hi = new_width - floor_width;

	if (hi < lo) {
		return new_width / 2;
	}

	if (scaled < lo) {
		return lo;
	}

	if (scaled > hi) {
		return hi;
	}

	return (int) scaled;
}
