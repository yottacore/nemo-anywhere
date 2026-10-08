/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */

/* arc-mounts-win32.c - the mount table on Windows.

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

#include <glib.h>

#ifdef G_OS_WIN32

#include <windows.h>

#include "arc-mounts.h"

static void
add_entry (GArray    *entries,
	   GPtrArray *texts,
	   char      *mount_path,
	   char      *device)
{
	ArcMountEntry entry = { mount_path, device, NULL };

	g_ptr_array_add (texts, mount_path);
	g_array_append_val (entries, entry);
}

/* Each volume by its \\?\Volume{...}\ name, which a mount point's reparse
   data names, and by each drive letter and folder it's mounted at. Asks the
   mount manager only, so no drive is spun up and no share is asked. A mapped
   drive or a share has no volume, and is told apart by its root.
   Returns: (transfer full): free with arc_mount_table_free */
ArcMountTable *
arc_mount_table_read (void)
{
	GArray *entries = g_array_new (FALSE, TRUE, sizeof (ArcMountEntry));
	GPtrArray *texts = g_ptr_array_new_with_free_func (g_free);
	ArcMountTable *table;
	wchar_t volume[MAX_PATH + 1];
	HANDLE find;

	find = FindFirstVolumeW (volume, G_N_ELEMENTS (volume));
	if (find != INVALID_HANDLE_VALUE) {
		do {
			char *device = g_utf16_to_utf8 (volume, -1, NULL, NULL, NULL);
			DWORD size = 0;
			wchar_t *names;
			const wchar_t *name;

			if (device == NULL) {
				continue;
			}
			add_entry (entries, texts, device, device);

			GetVolumePathNamesForVolumeNameW (volume, NULL, 0, &size);
			if (size == 0) {
				continue;
			}
			names = g_new0 (wchar_t, size + 1);
			if (GetVolumePathNamesForVolumeNameW (volume, names, size, &size)) {
				for (name = names; *name != L'\0'; name += wcslen (name) + 1) {
					char *where = g_utf16_to_utf8 (name, -1, NULL, NULL, NULL);

					if (where != NULL) {
						add_entry (entries, texts, where, device);
					}
				}
			}
			g_free (names);
		} while (FindNextVolumeW (find, volume, G_N_ELEMENTS (volume)));
		FindVolumeClose (find);
	}

	table = arc_mount_table_new ((const ArcMountEntry *) (void *) entries->data, entries->len, TRUE);
	g_array_unref (entries);
	g_ptr_array_unref (texts);
	return table;
}

#endif /* G_OS_WIN32 */
