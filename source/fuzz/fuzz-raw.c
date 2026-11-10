/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */

/* fuzz-raw.c - the camera raw preview reader, on arbitrary bytes.

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

/* Raw files come off cards and out of downloads, and a thumbnail is made of each
 * one, so this runs the TIFF, RAF, and CR3 walks on whatever turns up. The first
 * byte picks the size asked for, since that decides which preview is tried
 * first; the rest is the file. */

#include <config.h>

#include <stdint.h>

#include <glib.h>
#include <gio/gio.h>

#include <libnemo-private/nemo-raw.h>

int LLVMFuzzerTestOneInput (const uint8_t *data, size_t size);

int
LLVMFuzzerTestOneInput (const uint8_t *data, size_t size)
{
	GInputStream *in;
	GdkPixbuf *pixbuf;

	if (size < 1) {
		return 0;
	}

	in = g_memory_input_stream_new_from_data (data + 1, size - 1, NULL);
	pixbuf = nemo_raw_load (in, 1 + data[0] * 16, NULL);

	g_clear_object (&pixbuf);
	g_object_unref (in);

	return 0;
}
