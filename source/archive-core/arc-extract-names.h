/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */

/* arc-extract-names.h - which names unpack, and where to.

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

#ifndef ARC_EXTRACT_NAMES_H
#define ARC_EXTRACT_NAMES_H

#include <gio/gio.h>

G_BEGIN_DECLS

/* Whether the name looks like something we can unpack. Deliberately by name:
   it is the only answer available on win32, where a file's "mime type" is its
   extension, and it is what decides whether the menu items appear at all. */
gboolean arc_extract_is_archive_name (const char *name);

/* The folder an archive unpacks into when each gets its own - the name with
   its archive suffix taken off. Returns NULL when nothing is left. */
char    *arc_extract_folder_name     (const char *archive_name);

/* "notes.txt" at attempt 1 -> "notes (1).txt". The extension is kept so a
   renamed file still opens with the same program. */
char    *arc_extract_unique_name     (const char *name,
				      guint       attempt);

/* An archive can name an entry anything at all, including a path that climbs
   out of the folder being unpacked into. Returns a relative, '/'-separated
   path with the root, any drive letter and every ".." component taken off, or
   NULL when nothing usable is left. */
char    *arc_extract_sanitize_path   (const char *entry_path);

/* Where unpacking a split archive has to start: any volume answers with its
   ".001" sibling. */
GFile   *arc_extract_first_volume    (GFile      *archive);

/* Whether the name is a rar, which goes to a rar program ahead of 7-Zip. */
gboolean arc_extract_is_rar_name     (const char *name);

G_END_DECLS

#endif /* ARC_EXTRACT_NAMES_H */
