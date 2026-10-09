/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */

/* nemo-office.h - thumbnails and text of zip-based office files.

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

/* OOXML, OpenDocument and EPUB files are zips. Read here through libarchive,
 * so no helper program is started for them. Everything in the file is taken
 * as hostile: no length it gives is trusted, and sizes, counts and path
 * lengths are capped. Blocking, so never on the window's thread. */

#ifndef NEMO_OFFICE_H
#define NEMO_OFFICE_H

#include <gio/gio.h>
#include <gdk-pixbuf/gdk-pixbuf.h>

/* The types read here. On Windows a content type is an extension.
   Safe from any thread. */
gboolean   nemo_office_type_ok       (const char *content_type);

/* The picture the file keeps of itself, fitted to size on its longer side:
 * Thumbnails/thumbnail.png, the OOXML package thumbnail, or an EPUB cover.
 * PNG and JPEG only. NULL when there is none, it doesn't read, or @stream
 * can't seek.
 * Returns: (transfer full): unref with g_object_unref */
GdkPixbuf *nemo_office_thumbnail     (GInputStream *stream,
				      int           size,
				      GCancellable *cancellable);

GdkPixbuf *nemo_office_thumbnail_uri (const char   *uri,
				      int           size,
				      GCancellable *cancellable);

/* The text for content search, tags stripped, at most @max_len bytes and not
 * always valid UTF-8. NULL with @error set when it isn't a zip at all.
 * Returns: (transfer full): free with g_free */
char      *nemo_office_text          (GInputStream *stream,
				      gsize         max_len,
				      GCancellable *cancellable,
				      GError      **error);

char      *nemo_office_text_file     (GFile        *file,
				      gsize         max_len,
				      GCancellable *cancellable,
				      GError      **error);

#endif
