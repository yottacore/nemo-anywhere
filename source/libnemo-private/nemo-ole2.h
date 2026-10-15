/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */

/* nemo-ole2.h - streams out of an OLE2 compound file.

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

/* The container under .doc, .xls and .ppt from before 2007. Only what the
 * office reader needs: a stream in the top storage, by name, read whole.
 * The file is taken as hostile. Every sector number is checked against the
 * file, chains are walked no further than the stream needs, and the tables
 * and streams read are capped. Blocking, so never on the window's thread. */

#ifndef NEMO_OLE2_H
#define NEMO_OLE2_H

#include <gio/gio.h>

typedef struct _NemoOle2 NemoOle2;

/* The first 8 bytes of every compound file. */
gboolean  nemo_ole2_magic (const guint8 *p,
			   gsize         len);

/* @stream has to seek, and is read from here on, so it must outlive the
 * result. NULL with @error set when it isn't a compound file.
 * Returns: (transfer full): free with nemo_ole2_free */
NemoOle2 *nemo_ole2_open  (GInputStream *stream,
			   GCancellable *cancellable,
			   GError      **error);

/* A stream directly under the top storage, the name matched with ASCII case
 * ignored. At most @cap bytes of it; a longer one comes back cut short.
 * NULL when there is no such stream or it doesn't read.
 * Returns: (transfer full): unref with g_bytes_unref */
GBytes   *nemo_ole2_read  (NemoOle2   *ole,
			   const char *name,
			   gsize       cap);

void      nemo_ole2_free  (NemoOle2 *ole);

G_DEFINE_AUTOPTR_CLEANUP_FUNC (NemoOle2, nemo_ole2_free)

#endif
