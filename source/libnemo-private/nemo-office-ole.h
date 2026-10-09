/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */

/* nemo-office-ole.h - text and preview of .doc, .xls and .ppt files.

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

/* Used by nemo-office.c, which is what the rest of the app calls. The
 * record parsing is what the nemo-anywhere-*-to-txt converters did, with
 * the text capped. Each text call gives at most @max_len bytes, not always
 * valid UTF-8, with the formats' control characters made spaces and line
 * breaks. */

#ifndef NEMO_OFFICE_OLE_H
#define NEMO_OFFICE_OLE_H

#include "nemo-ole2.h"

/* The text of whichever of Word, Excel or PowerPoint the file's streams say
 * it is. NULL when it has none of their streams.
 * Returns: (transfer full): free with g_free */
char    *nemo_office_ole_text        (NemoOle2     *ole,
				      gsize         max_len);

/* The preview picture kept in the summary stream, as the bytes of a .bmp
 * file. Only a bitmap is taken, never a metafile. NULL when there is none.
 * Returns: (transfer full): unref with g_bytes_unref */
GBytes  *nemo_office_ole_preview     (NemoOle2     *ole);

/* The oldest workbooks are a bare record stream with no container. */
gboolean nemo_office_biff_magic      (const guint8 *p,
				      gsize         len);

/* The parsers alone, on one stream each.
 * Returns: (transfer full): free with g_free */
char    *nemo_office_word_text       (const guint8 *doc,
				      gsize         doc_len,
				      const guint8 *table,
				      gsize         table_len,
				      gsize         max_len);
char    *nemo_office_biff_text       (const guint8 *data,
				      gsize         len,
				      gsize         max_len);
char    *nemo_office_ppt_text        (const guint8 *data,
				      gsize         len,
				      gsize         max_len);

/* Returns: (transfer full): unref with g_bytes_unref */
GBytes  *nemo_office_summary_preview (const guint8 *data,
				      gsize         len);

#endif
