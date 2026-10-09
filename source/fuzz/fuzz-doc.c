/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */

/* fuzz-doc.c - the Word piece table, on arbitrary bytes.

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

/* The piece table sits in one stream and points into another, so the text a
 * .doc yields comes from two buffers that need not agree with each other. The
 * first two bytes of the input say where to cut, which lets the search move
 * the boundary as well as the contents. The first part is the document
 * stream and the rest is the piece table. A Word 97 header is written over
 * the start of the document stream, pointing at the whole table, so every
 * input reaches the piece table rather than only the ones that happen to
 * carry a header. fuzz-ole2.c's Word case takes the streams as they come. */

#include <config.h>

#include <stdint.h>
#include <string.h>

#include <glib.h>

#include <libnemo-private/nemo-office-ole.h>

/* As nemo-office-ole.c reads them. */
#define FIB_LEN     0x1AA
#define FIB_FLAGS   0x0A
#define FIB_FC_CLX  0x1A2
#define FIB_LCB_CLX 0x1A6

static void
set16 (guint8 *p, guint16 v)
{
	p[0] = (guint8) (v & 0xff);
	p[1] = (guint8) (v >> 8);
}

static void
set32 (guint8 *p, guint32 v)
{
	set16 (p, (guint16) (v & 0xffff));
	set16 (p + 2, (guint16) (v >> 16));
}

int LLVMFuzzerTestOneInput (const uint8_t *data, size_t size);

int
LLVMFuzzerTestOneInput (const uint8_t *data, size_t size)
{
	const guint8 *body;
	gsize body_len, split, doc_len;
	guint8 *doc;

	if (size < 2) {
		return 0;
	}

	body = (const guint8 *) data + 2;
	body_len = size - 2;
	split = MIN ((gsize) (data[0] | (data[1] << 8)), body_len);

	doc_len = MAX (split, (gsize) FIB_LEN);
	doc = g_malloc0 (doc_len);
	if (split > 0) {
		memcpy (doc, body, split);
	}
	set16 (doc, 0xA5EC);
	set16 (doc + 2, 0x00C1);
	set16 (doc + FIB_FLAGS, 0);
	set32 (doc + FIB_FC_CLX, 0);
	set32 (doc + FIB_LCB_CLX, (guint32) MIN (body_len - split, (gsize) G_MAXUINT32));

	g_free (nemo_office_word_text (doc, doc_len, body + split, body_len - split, 64 * 1024));
	g_free (doc);

	return 0;
}
