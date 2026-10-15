/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */

/* test-ole2-writer.h - a small compound file writer for the tests.

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

/* Writes what the old Word, Excel and PowerPoint files are kept in, with no
 * library, for the tests of the app's own reader. Small streams go in the
 * mini stream, as Office does it. */

#ifndef TEST_OLE2_WRITER_H
#define TEST_OLE2_WRITER_H

#include <glib.h>

G_BEGIN_DECLS

#define FREESECT   0xffffffffu
#define ENDOFCHAIN 0xfffffffeu
#define FATSECT    0xfffffffdu
#define DIFSECT    0xfffffffcu

/* Entries are streams, or storages when data is NULL, under the top storage
   or under an earlier storage. */
typedef struct {
	const char *name;
	GBytes     *data;
	int         parent;	/* -1: the top storage */
} OleEntry;

#define MAX_ENTRIES 8

typedef struct {
	guint   shift;	/* the sector size: 9 or 12 */
	/* Filled in by the writer, for the tests that break a file on purpose. */
	guint32 start[MAX_ENTRIES];
	guint32 fat_sector[256];
	guint   n_fat;
	guint32 dir_start;
} OleLayout;

/* The whole file. @lay's shift is read, the rest is filled in.
   Returns: (transfer full): unref with g_bytes_unref */
GBytes *test_ole2_write (const OleEntry *entries,
			 guint           n_entries,
			 OleLayout      *lay);

G_END_DECLS

#endif
