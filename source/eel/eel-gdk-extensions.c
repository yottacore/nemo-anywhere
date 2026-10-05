/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */

/* eel-gdk-extensions.c: Graphics routines to augment what's in gdk.

   Copyright (C) 1999, 2000 Eazel, Inc.

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

   Authors: Darin Adler <darin@eazel.com>, 
            Pavel Cisler <pavel@eazel.com>,
            Ramiro Estrugo <ramiro@eazel.com>
*/

#include <config.h>
#include "eel-gdk-extensions.h"

#include "eel-glib-extensions.h"
#include "eel-string.h"
#include <gdk-pixbuf/gdk-pixbuf.h>
#include <gdk/gdk.h>
#include <stdlib.h>
#include <pango/pango.h>

/* Portable geometry-string parser (was X11's XParseGeometry). Same
 * "=WxH{+-}X{+-}Y" grammar; EelGdkGeometryFlags bits already match the
 * classic X11 mask so we set them directly and stay off libX11. */
static int
read_geometry_int (const char *s, const char **next)
{
	int result = 0;
	int sign = 1;

	if (*s == '+') {
		s++;
	} else if (*s == '-') {
		s++;
		sign = -1;
	}
	for (; *s >= '0' && *s <= '9'; s++) {
		result = (result * 10) + (*s - '0');
	}
	*next = s;
	return sign * result;
}

EelGdkGeometryFlags
eel_gdk_parse_geometry (const char *string, int *x_return, int *y_return,
			     guint *width_return, guint *height_return)
{
	EelGdkGeometryFlags flags = EEL_GDK_NO_VALUE;
	const char *pos, *next;
	int x = 0, y = 0;
	guint width = 0, height = 0;

	g_return_val_if_fail (string != NULL, EEL_GDK_NO_VALUE);
	g_return_val_if_fail (x_return != NULL, EEL_GDK_NO_VALUE);
	g_return_val_if_fail (y_return != NULL, EEL_GDK_NO_VALUE);
	g_return_val_if_fail (width_return != NULL, EEL_GDK_NO_VALUE);
	g_return_val_if_fail (height_return != NULL, EEL_GDK_NO_VALUE);

	if (*string == '\0') {
		return EEL_GDK_NO_VALUE;
	}
	pos = string;
	if (*pos == '=') {
		pos++;
	}

	if (*pos != '+' && *pos != '-' && *pos != 'x' && *pos != 'X') {
		width = read_geometry_int (pos, &next);
		if (pos == next) {
			return EEL_GDK_NO_VALUE;
		}
		pos = next;
		flags |= EEL_GDK_WIDTH_VALUE;
	}
	if (*pos == 'x' || *pos == 'X') {
		pos++;
		height = read_geometry_int (pos, &next);
		if (pos == next) {
			return EEL_GDK_NO_VALUE;
		}
		pos = next;
		flags |= EEL_GDK_HEIGHT_VALUE;
	}
	if (*pos == '+' || *pos == '-') {
		if (*pos == '-') {
			pos++;
			x = -read_geometry_int (pos, &next);
			flags |= EEL_GDK_X_NEGATIVE;
		} else {
			pos++;
			x = read_geometry_int (pos, &next);
		}
		if (pos == next) {
			return EEL_GDK_NO_VALUE;
		}
		pos = next;
		flags |= EEL_GDK_X_VALUE;

		if (*pos == '+' || *pos == '-') {
			if (*pos == '-') {
				pos++;
				y = -read_geometry_int (pos, &next);
				flags |= EEL_GDK_Y_NEGATIVE;
			} else {
				pos++;
				y = read_geometry_int (pos, &next);
			}
			if (pos == next) {
				return EEL_GDK_NO_VALUE;
			}
			pos = next;
			flags |= EEL_GDK_Y_VALUE;
		}
	}
	if (*pos != '\0') {
		return EEL_GDK_NO_VALUE;
	}

	*x_return = x;
	*y_return = y;
	*width_return = width;
	*height_return = height;
	return flags;
}

/* Returns: (transfer none): owned by the display */
GdkDevice *
eel_gdk_get_pointer_device (void)
{
    GdkSeat *seat;

    seat = gdk_display_get_default_seat (gdk_display_get_default ());

    if (seat != NULL) {
        return gdk_seat_get_pointer (seat);
    }

    return NULL;
}
