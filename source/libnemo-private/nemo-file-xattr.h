/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */

/* nemo-file-xattr.h - small named values kept beside a file's contents.

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

/* The same idea under 2 names. On Linux and the BSDs these are extended
 * attributes in the `user` namespace; on Windows they are alternate data
 * streams, which only NTFS and ReFS have. Plenty of file systems have neither -
 * FAT, exFAT, most network shares - and the answer there is no.
 *
 * Only short text belongs here. A value is read back in one go into a fixed
 * buffer, and anything longer than that reads as absent.
 */

#ifndef NEMO_FILE_XATTR_H
#define NEMO_FILE_XATTR_H

#include <gio/gio.h>

G_BEGIN_DECLS

/* Longest value that can be stored or read back, terminator included. */
#define NEMO_FILE_XATTR_MAX 256

/* Reads one. NULL when there is none, when it is too long, or when the file
 * system has nowhere to keep it. Freed by the caller. */
char *nemo_file_xattr_get (GFile *file, const char *name);

/* Writes one, replacing whatever was there. False when the file system can't
 * hold it or the file can't be written, neither of which is an error worth
 * telling anybody about. */
gboolean nemo_file_xattr_set (GFile *file, const char *name, const char *value);

G_END_DECLS

#endif /* NEMO_FILE_XATTR_H */
