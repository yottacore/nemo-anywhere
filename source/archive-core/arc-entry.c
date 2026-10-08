/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */

/* arc-entry.c - what a name is, read without going through it.

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

#include <gio/gio.h>

#include "arc-entry.h"

#ifndef G_OS_WIN32
ArcEntry
arc_entry_read (const char  *path,
		char       **target)
{
	GFile *file = g_file_new_for_path (path);
	GFileInfo *info;
	ArcEntry entry = ARC_ENTRY_MISSING;

	*target = NULL;
	info = g_file_query_info (file, G_FILE_ATTRIBUTE_STANDARD_TYPE "," G_FILE_ATTRIBUTE_STANDARD_SYMLINK_TARGET,
				  G_FILE_QUERY_INFO_NOFOLLOW_SYMLINKS, NULL, NULL);
	g_object_unref (file);
	if (info == NULL) {
		return ARC_ENTRY_MISSING;
	}

	if (g_file_info_get_file_type (info) != G_FILE_TYPE_SYMBOLIC_LINK) {
		entry = ARC_ENTRY_PLAIN;
	} else if (g_file_info_get_symlink_target (info) != NULL) {
		*target = g_strdup (g_file_info_get_symlink_target (info));
		entry = ARC_ENTRY_SYMLINK;
	}
	g_object_unref (info);
	return entry;
}
#endif
