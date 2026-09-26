/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */

/* nemo-row-hover.h - the tint on a list row under the pointer.

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

#ifndef NEMO_ROW_HOVER_H
#define NEMO_ROW_HOVER_H

#include <gtk/gtk.h>

G_BEGIN_DECLS

/* The tint for a row under the pointer, on rows of color base, for a theme
 * whose selected rows are selected. Comes back see-through, so it also sits
 * right on a sidebar whose rows are a shade off base. */
void nemo_row_hover_pick   (const GdkRGBA *base,
			    const GdkRGBA *selected,
			    GdkRGBA       *hover);

/* Keeps a tree view's hover tint in step with its theme and the setting. */
void nemo_row_hover_attach (GtkWidget *tree_view);

/* The tint for every other row when rows are shaded, for this tree view's
 * theme and the setting. */
void nemo_row_shading_pick (GtkWidget *tree_view,
			    GdkRGBA   *shading);

G_END_DECLS

#endif /* NEMO_ROW_HOVER_H */
