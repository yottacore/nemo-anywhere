/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */

/* nemo-drives-win32.h - drive letters, named and drawn without asking a share.

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

#ifndef NEMO_DRIVES_WIN32_H
#define NEMO_DRIVES_WIN32_H

#include <gio/gio.h>

G_BEGIN_DECLS

#ifdef G_OS_WIN32

typedef enum {
	NEMO_DRIVE_WIN32_NONE,
	NEMO_DRIVE_WIN32_FIXED,
	NEMO_DRIVE_WIN32_REMOVABLE,
	NEMO_DRIVE_WIN32_OPTICAL,
	NEMO_DRIVE_WIN32_REMOTE,
	NEMO_DRIVE_WIN32_OTHER
} NemoDriveWin32Kind;

/* What a drive letter is. A letter mapped to a share, or a subst onto one, is
   known from its entry in the object table and never asked; only a local
   drive is asked for its type. */
NemoDriveWin32Kind nemo_drive_win32_kind (char letter);

/* Whether a drive has a disk in it, asked with the shell's insert-disk prompt
   off, so an empty card reader just says no. */
gboolean nemo_drive_win32_has_media (char letter);

/* Whether a drive's bin may be asked about: a fixed drive, or a removable one
   with a disk in it. A share is never asked. */
gboolean nemo_drive_win32_bin_askable (char letter);

/* \\server\share for a letter mapped to a share, read from the same entry.
   NULL when it is not one, or the entry does not say.
   Returns: (transfer full): free with g_free. */
char *nemo_drive_win32_remote_path (char letter);

/* The mounts GLib's volume monitor would list, one per drive letter shown.
   GLib asks the shell for each one's name as the list is made, and for its
   icon when drawn, so a letter mapped to a share that is not answering held
   the side pane for the network timeout. A mapped drive here is named and
   drawn from its letter and its entry instead.
   Returns: (transfer full): free with g_list_free_full and g_object_unref. */
GList *nemo_drives_win32_get_mounts (void);

#endif /* G_OS_WIN32 */

G_END_DECLS

#endif /* NEMO_DRIVES_WIN32_H */
