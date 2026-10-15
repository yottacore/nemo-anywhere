/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */

/* fuzz-ole2.c - the .doc, .xls and .ppt readers, on arbitrary bytes.

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

/* The first byte picks what reads the rest, modulo 6: a whole file for its
 * text or its thumbnail, or one stream for one parser. A whole file rarely
 * survives mutation as a compound file, so that way the parsers get bytes of their
 * own too. For the Word parser the next 2 bytes say where the document
 * stream ends and the table stream starts, as fuzz-doc.c does. */

#include <config.h>

#include <stdint.h>

#include <glib.h>
#include <gio/gio.h>

#include <libnemo-private/nemo-office.h>
#include <libnemo-private/nemo-office-ole.h>

int LLVMFuzzerTestOneInput (const uint8_t *data, size_t size);

int
LLVMFuzzerTestOneInput (const uint8_t *data, size_t size)
{
	const guint8 *body;
	gsize len;

	if (size < 1) {
		return 0;
	}
	body = data + 1;
	len = size - 1;

	switch (data[0] % 6) {
	case 0:
	case 1: {
		GInputStream *in = g_memory_input_stream_new_from_data (body, (gssize) len, NULL);

		if (data[0] % 6 == 1) {
			GdkPixbuf *pixbuf = nemo_office_thumbnail (in, 16 + data[0] / 6, NULL);

			g_clear_object (&pixbuf);
		} else {
			g_free (nemo_office_text (in, 64 * 1024, NULL, NULL));
		}
		g_object_unref (in);
		break;
	}
	case 2:
		if (len >= 2) {
			gsize split = MIN ((gsize) (body[0] | (body[1] << 8)), len - 2);

			g_free (nemo_office_word_text (body + 2, split, body + 2 + split, len - 2 - split, 64 * 1024));
		}
		break;
	case 3:
		g_free (nemo_office_biff_text (body, len, 64 * 1024));
		break;
	case 4:
		g_free (nemo_office_ppt_text (body, len, 64 * 1024));
		break;
	default: {
		GBytes *bmp = nemo_office_summary_preview (body, len);

		if (bmp != NULL) {
			g_bytes_unref (bmp);
		}
		break;
	}
	}

	return 0;
}
