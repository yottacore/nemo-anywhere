/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */

/* fuzz-dnd.c - the drag payload parser, on arbitrary bytes.

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

/* An x-special/gnome-icon-list payload arrives from whatever the drag came
 * from, which doesn't have to be a file manager or a friendly one. The parser
 * has read off the end of this buffer before, which the guard-page test beside
 * it checks, so it gets a real search rather than the handful of cases that
 * test can list.
 *
 * The NUL below isn't padding. The no-geometry branch reads one byte past the
 * payload, where GtkSelectionData guarantees a terminator, so a harness that
 * left it out would report an overread the real caller can't hit. */

#include <config.h>

#include <stdint.h>
#include <string.h>

#include <glib.h>

#include <libnemo-private/nemo-dnd.h>

int LLVMFuzzerTestOneInput (const uint8_t *data, size_t size);

int
LLVMFuzzerTestOneInput (const uint8_t *data, size_t size)
{
	GList *list;
	guchar *copy;

	/* The length is an int all the way down. */
	if (size > (size_t) G_MAXINT) {
		return 0;
	}

	copy = g_malloc (size + 1);
	memcpy (copy, data, size);
	copy[size] = '\0';

	list = nemo_drag_build_selection_list_from_raw (copy, (int) size);

	nemo_drag_destroy_selection_list (list);
	g_free (copy);

	return 0;
}
