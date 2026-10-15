/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */

/* nemo-ui-scale.h - icons for the part of a display scale GTK leaves out.

   Copyright © 2026 t00mietum (CryptogID: ปʬϝღถɔ4რఠΔթะ9ƾǝu).

   This program is free software; you can redistribute it and/or
   modify it under the terms of the GNU General Public License as
   published by the Free Software Foundation; version 2 of the
   License.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, see <http://www.gnu.org/licenses/>.
*/

#ifndef NEMO_UI_SCALE_H
#define NEMO_UI_SCALE_H

#include <gtk/gtk.h>

G_BEGIN_DECLS

/* GTK on Windows scales in whole steps, so at 150% it draws at 1 and the text
   is brought up to size through gtk-xft-dpi (nemo-dpi-win32.c). Icons named
   at a GtkIconSize would stay at the whole step. This sizes them by the
   fraction that is left over. See design.md, "Scaling and startup".

   Off Windows the desktop's font DPI is a text setting, and every GTK3 app
   leaves icons alone under it, so the leftover is always 1 there. */

/* What gtk-xft-dpi leaves over the whole step: 1.5 at 144 dpi, never below 1.
   Values in 1024ths of a dot per inch, as GtkSettings keeps them. */
gdouble nemo_ui_scale_leftover_for_dpi (gint xft_dpi);

/* The leftover now: from GtkSettings on Windows, 1 everywhere else. */
gdouble nemo_ui_scale_leftover (void);

/* A size in pixels at the whole step, brought up by the leftover. */
gint    nemo_ui_scale_pixels (gint pixels, gdouble leftover);

/* A GtkIconSize standing for that size, for a cell renderer's stock-size,
   which has no pixel size of its own. The size itself when nothing is left. */
GtkIconSize nemo_ui_scale_icon_size (GtkIconSize size, gdouble leftover);

/* Sizes one named or GIcon image by the leftover, through its pixel size. An
   image whose pixel size the code set itself is left alone, and a leftover of
   1 puts the theme's own size back. */
void    nemo_ui_scale_image (GtkImage *image, gdouble leftover);

/* Windows only: size every image from here on, and again whenever the font
   DPI moves. */
void    nemo_ui_scale_install (void);

G_END_DECLS

#endif /* NEMO_UI_SCALE_H */
