/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */

/* nemo-external-drive.h - whether a file is on a drive that plugs in.

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

#ifndef NEMO_EXTERNAL_DRIVE_H
#define NEMO_EXTERNAL_DRIVE_H

#include <gio/gio.h>

G_BEGIN_DECLS

/* True for a file on a USB stick or disk, a FireWire disk, an SD card, or other removable media. A share, a fixed disk, or anything that can't be told apart from one is false. Safe on any thread, and never asks a volume monitor. */
gboolean nemo_file_is_on_external_drive (GFile *file);

/* Linux only: read block devices from this folder rather than /sys, laid out the same way. NULL puts /sys back. For tests. */
void nemo_external_drive_set_sysfs_for_test (const char *root);

G_END_DECLS

#endif /* NEMO_EXTERNAL_DRIVE_H */
