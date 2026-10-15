/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */

/* nemo-raw.h - thumbnails of camera raw files.

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

/* A raw file is sensor data nothing here can develop, but every camera also
 * stores a finished JPEG preview in it for its own screen. That preview is
 * what gets drawn, so a thumbnail reads a few directories and one JPEG, never
 * the sensor data.
 *
 * Reads the TIFF based files (DNG, CR2, NEF, ARW, PEF, RW2, ORF and kin), Fuji
 * RAF and Canon CR3. A file with no usable preview answers NULL. */

#ifndef NEMO_RAW_H
#define NEMO_RAW_H

#include <gio/gio.h>
#include <gdk-pixbuf/gdk-pixbuf.h>

gboolean   nemo_raw_type_ok  (const char *mime_type);

/* The stream has to be seekable. No side of the result is over size, and the
 * camera's orientation is attached as the "orientation" option. */
GdkPixbuf *nemo_raw_load     (GInputStream *stream,
			      int           size,
			      GCancellable *cancellable);

GdkPixbuf *nemo_raw_load_uri (const char   *uri,
			      int           size,
			      GCancellable *cancellable);

#endif
