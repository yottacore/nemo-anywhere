/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */

/* arc-mounts.h - same filesystem, a nested one, or another one.

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

/* Answered from a copy of the mount table and the text of the paths, so a
 * path on a share is never visited. "Nested and other filesystems" in
 * project/design_docs/20260929-101432_compression.md.
 *
 * Nested is the same pool or volume on both sides of the mount: one ZFS pool,
 * one Btrfs filesystem, one APFS container, or one disk mounted in 2 places.
 * Windows has no nested kind: a volume mounted in a folder is another
 * filesystem.
 */

#ifndef ARC_MOUNTS_H
#define ARC_MOUNTS_H

#include <glib.h>

G_BEGIN_DECLS

typedef enum {
	ARC_FS_SAME,
	ARC_FS_NESTED,
	ARC_FS_OTHER,
} ArcFsKind;

typedef struct {
	const char *mount_path;
	/* What was mounted: a block device, a ZFS dataset, or on Windows the
	   volume's \\?\Volume{...}\ name. NULL or a made-up name, as tmpfs
	   has, is never nested with anything. */
	const char *device;
	const char *fs_type;
} ArcMountEntry;

typedef struct _ArcMountTable ArcMountTable;

/* A table from entries handed in, for a test. Copies them. A later entry at
   the same place is mounted over an earlier one. windows compares paths the
   Windows way: either slash, any case, and \\?\ forms read as plain ones.
   Returns: (transfer full): free with arc_mount_table_free */
ArcMountTable *arc_mount_table_new            (const ArcMountEntry *entries,
					       gsize                n_entries,
					       gboolean             windows);
/* The system's table as it is now. Read again for a new scan.
   Returns: (transfer full): free with arc_mount_table_free */
ArcMountTable *arc_mount_table_read           (void);
void           arc_mount_table_free           (ArcMountTable *table);

/* Where path is, next to base, the folder the selection is in. Both are
   absolute and canonical, as the scan has them. A folder, or where a link
   leads. */
ArcFsKind      arc_mount_table_kind           (const ArcMountTable *table,
					       const char          *base,
					       const char          *path);
/* TRUE when path is where something is mounted. */
gboolean       arc_mount_table_is_mount_point (const ArcMountTable *table,
					       const char          *path);
/* For a Windows reparse point's target. A folder mount point and a junction
   are the same kind there, and one whose target names a whole volume, as
   \??\Volume{...}\, is a mount point only. One to a drive's root is still a
   junction, or a junction to C:\ would be walked with junctions off. */
gboolean       arc_target_is_volume           (const char          *target);

G_END_DECLS

#endif /* ARC_MOUNTS_H */
