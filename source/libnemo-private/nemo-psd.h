/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */

/* nemo-psd.h - thumbnails of Photoshop files.

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

/* gdk-pixbuf has no Photoshop loader. A .psd or .psb carries a flattened copy
 * of the whole picture after its layers, which is all a thumbnail needs, so
 * only that part is read. Layers themselves are skipped unread.
 *
 * Reads 8 and 16 bit grayscale, duotone (as gray), indexed, RGB and CMYK, raw
 * or run-length packed. Anything else answers NULL. */

#ifndef NEMO_PSD_H
#define NEMO_PSD_H

#include <gio/gio.h>
#include <gdk-pixbuf/gdk-pixbuf.h>

gboolean   nemo_psd_type_ok  (const char *mime_type);

/* No side of the result is over size. The picture is shrunk while it is
 * decoded, so a large file never sits in memory at full size. */
GdkPixbuf *nemo_psd_load     (GInputStream *stream,
			      int           size,
			      GCancellable *cancellable);

GdkPixbuf *nemo_psd_load_uri (const char   *uri,
			      int           size,
			      GCancellable *cancellable);

#endif
