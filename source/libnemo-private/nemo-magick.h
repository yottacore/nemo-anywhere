/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */

/* nemo-magick.h - thumbnails made by ImageMagick, where it is installed.

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

/* The last resort for picture formats nothing in this process reads, such as
 * JPEG 2000, HEIC and EXR. It runs ImageMagick as a separate program, one file
 * at a time, so nothing new is linked and a crash in a decoder stays there.
 *
 * Only formats on a fixed list are handed over, by extension, and the format
 * is named in the command rather than left to ImageMagick to guess. It reads
 * scripts, vector files and pseudo-files too, and none of those belong here.
 * Nor does it ever see a file name, only the file's bytes. */

#ifndef NEMO_MAGICK_H
#define NEMO_MAGICK_H

#include <gio/gio.h>
#include <gdk-pixbuf/gdk-pixbuf.h>

/* The installed program, looked up once. NULL when there is none. */
const char *nemo_magick_program (void);

/* The ImageMagick format name for a file name or uri, by its extension, or
 * NULL when it is not on the list. A few formats are on it only while
 * gdk-pixbuf has no loader for them. */
const char *nemo_magick_coder   (const char *name);

/* A local file on the list, and a program to hand it to. */
gboolean    nemo_magick_type_ok (const char *uri);

/* The command that reads the file on stdin and writes a PNG, no side over
 * size, to stdout. */
gchar     **nemo_magick_argv    (const char *program,
				 const char *coder,
				 int         size);

/* Runs it, giving up after half a minute, or ending it once @cancellable is
 * cancelled. NULL for any failure: no program, a format the installed copy
 * was built without, a damaged file. */
GdkPixbuf  *nemo_magick_load_uri (const char   *uri,
				  int           size,
				  GCancellable *cancellable);

#endif
