/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */

/* nemo-dpi-win32.h - following the DPI of the monitor a window is on.

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

/* Windows only. The manifest declares us per-monitor DPI aware, which stops
 * Windows stretching the window as a bitmap on a scaled display - so from then
 * on the scaling is ours to do.
 *
 * GTK does most of it: it reads the awareness we declared, works out a scale
 * factor per monitor and draws at that. What it can't do is a fraction - the
 * factor is a whole number, so a display at 125% or 150% is drawn at 100% and
 * everything on it comes out smaller than the windows beside it. Text is the
 * half of that we can fix: point sizes go through gtk-xft-dpi, which isn't
 * restricted to whole steps, so setting it to the monitor's real DPI - with
 * the whole-number part the toolkit already applied divided back out - leaves
 * the type the size it should be. Widgets and icons stay on the whole step.
 *
 * Nothing here is needed anywhere else: X11 and Wayland desktops publish their
 * own scaling and GTK already follows it.
 */

#ifndef NEMO_DPI_WIN32_H
#define NEMO_DPI_WIN32_H

#include <glib.h>

G_BEGIN_DECLS

/* Set the font DPI from whatever monitor we are on, and keep following it.
 * Call once, after gtk_init. */
void nemo_dpi_win32_init (void);

/* The gtk-xft-dpi that leaves type at its true size on a monitor of this DPI,
 * given the whole-number scale the toolkit has already applied. In 1024ths, as
 * that setting is. Split out from everything that needs a screen so it can be
 * checked on its own. */
gint nemo_dpi_win32_font_dpi (guint monitor_dpi,
			      gint  scale_factor);

G_END_DECLS

#endif /* NEMO_DPI_WIN32_H */
