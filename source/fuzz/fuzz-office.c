/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */

/* fuzz-office.c - the zip-based office readers, on arbitrary bytes.

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

/* Any .docx, .odt or .epub in a download folder gets a thumbnail, and any
 * of them under a content search is read for text. The first byte's low bit
 * picks which reader, and the rest of it the thumbnail size; the rest is the
 * file. */

#include <config.h>

#include <stdint.h>

#include <glib.h>
#include <gio/gio.h>

#include <libnemo-private/nemo-office.h>

int LLVMFuzzerTestOneInput (const uint8_t *data, size_t size);

int
LLVMFuzzerTestOneInput (const uint8_t *data, size_t size)
{
	GInputStream *in;

	if (size < 1) {
		return 0;
	}

	in = g_memory_input_stream_new_from_data (data + 1, (gssize) (size - 1), NULL);

	if (data[0] & 1) {
		GdkPixbuf *pixbuf = nemo_office_thumbnail (in, 16 + (data[0] >> 1), NULL);

		g_clear_object (&pixbuf);
	} else {
		g_free (nemo_office_text (in, 64 * 1024, NULL, NULL));
	}

	g_object_unref (in);

	return 0;
}
