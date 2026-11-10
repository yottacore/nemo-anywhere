/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */

/* fuzz-shcl.c - the settings file parser, on arbitrary bytes.

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

/* settings.shcl is read before anything else and can be edited by hand, so it
 * is the widest way in. The parser is vendored rather than ours, and it
 * promises 2 things that are checked here. Parsing never fails, and a depth cap
 * keeps a hostile document from running the tree walks off the stack. A crash
 * here is a report upstream, not a local patch.
 *
 * shcl_parse takes a length and documents that the text needs no NUL, so the
 * bytes go in exactly as they arrived. */

#define SHCL_IMPLEMENTATION
#include "shcl.h"

#include <stdint.h>

int LLVMFuzzerTestOneInput (const uint8_t *data, size_t size);

int
LLVMFuzzerTestOneInput (const uint8_t *data, size_t size)
{
	shcl_doc *doc = shcl_parse ((const char *) data, size);

	/* Reading the verdict walks the diagnostics the parse just built. */
	if (doc != NULL) {
		(void) shcl_strict_failed (doc);
	}

	shcl_free (doc);

	return 0;
}
