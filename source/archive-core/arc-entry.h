/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */

/* arc-entry.h - what a name is, read without going through it.

   Copyright © 2026 t00mietum (CryptogID: ปʬϝღถɔ4რఠΔթะ9ƾǝu).

   This program is free software; you can redistribute it and/or
   modify it under the terms of the GNU General Public License as
   published by the Free Software Foundation; version 2 of the
   License.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, see <http://www.gnu.org/licenses/>.
*/

/* The scan has to know a link from what it leads to, and on Windows a
 * junction from a symlink and from a folder a volume is mounted at, which
 * are all the same kind of reparse point. GLib can't tell those apart.
 */

#ifndef ARC_ENTRY_H
#define ARC_ENTRY_H

#include <glib.h>

G_BEGIN_DECLS

typedef enum {
	ARC_ENTRY_MISSING,
	ARC_ENTRY_PLAIN,	/* a file or folder, or a reparse point that isn't a link */
	ARC_ENTRY_SYMLINK,
	ARC_ENTRY_JUNCTION,	/* Windows */
	ARC_ENTRY_MOUNT_POINT,	/* Windows: a folder a whole volume is mounted at */
} ArcEntry;

/* target is set for a SYMLINK or JUNCTION, as the link has it, with the
   Windows \??\ forms read as plain ones. A link that can't be read is
   MISSING, since where it goes can't be told. */
ArcEntry arc_entry_read (const char  *path,
			 char       **target);

G_END_DECLS

#endif /* ARC_ENTRY_H */
