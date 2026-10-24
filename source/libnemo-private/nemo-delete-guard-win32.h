/* nemo-delete-guard-win32.h - home by file id on Windows.

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

#ifndef NEMO_DELETE_GUARD_WIN32_H
#define NEMO_DELETE_GUARD_WIN32_H

#include <glib.h>

G_BEGIN_DECLS

/* TRUE when path names the home folder, or with or_above a folder above it,
   whether by case, slashes, a short name or a junction on the way. */
gboolean nemo_delete_guard_win32_is_home (const char *path,
					  gboolean    or_above);

/* Takes a MSG. TRUE for a WM_ACTIVATEAPP that brings the focus back from
   another thread of this process, such as the one the shell runs its property
   sheet on, which isn't a GTK window. */
gboolean nemo_delete_guard_win32_back_from_own_thread (const void *native);

G_END_DECLS

#endif /* NEMO_DELETE_GUARD_WIN32_H */
