/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */

/* fuzz-xls.c - the BIFF record walker, on arbitrary bytes.

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

/* A .xls is whatever the file the search touched happens to contain, and the
 * record walk below it reads lengths and offsets straight out of that. It has
 * been wrong once already: the shared-string loop trusted a count read from
 * the file and tested the wrong end, so a truncated workbook spun for four
 * billion passes. The bytes go to the app's Excel parser as one bare record
 * stream, the way it gets the Workbook stream out of a compound file. */

#include <config.h>

#include <stdint.h>

#include <glib.h>

#include <libnemo-private/nemo-office-ole.h>

int LLVMFuzzerTestOneInput (const uint8_t *data, size_t size);

int
LLVMFuzzerTestOneInput (const uint8_t *data, size_t size)
{
	g_free (nemo_office_biff_text (data, size, 64 * 1024));

	return 0;
}
