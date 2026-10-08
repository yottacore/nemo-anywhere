/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */

/* arc-path-list.h - every file the size scan found, and the size totals.

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

/* One entry per file, by canonical path, with its bytes and which of the 16
 * totals already count it. The scan hands in each path it takes to a file,
 * with the follow options that path needs, and the totals move as it goes.
 * "Path list" and "Size totals" in
 * project/design_docs/20260929-101432_compression.md.
 *
 * No disk access. One thread at a time: the scan owns the list and copies the
 * totals out for whoever shows them.
 */

#ifndef ARC_PATH_LIST_H
#define ARC_PATH_LIST_H

#include <glib.h>

G_BEGIN_DECLS

/* The follow options, and what a path needs of them. Same bits for both, so a
   mix of options lets a path in when it has every bit the path needs. They
   describe the path, not the file: a file reached 2 ways can need 2 sets. */
typedef enum {
	ARC_FOLLOW_SYMLINKS  = 1 << 0,	/* a symlinked file, or below a symlinked folder */
	ARC_FOLLOW_JUNCTIONS = 1 << 1,	/* below a junction */
	ARC_FOLLOW_NESTED_FS = 1 << 2,	/* crossed into a nested filesystem */
	ARC_FOLLOW_OTHER_FS  = 1 << 3,	/* crossed onto another filesystem */
} ArcFollow;

#define ARC_FOLLOW_ALL 0x0fu
/* One total per mix of the options. A mix's number is its ArcFollow bits. */
#define ARC_MIX_COUNT  16

typedef struct _ArcPathList ArcPathList;

/* Returns: (transfer full): free with arc_path_list_free */
ArcPathList *arc_path_list_new    (void);
void         arc_path_list_free   (ArcPathList *list);

/* needs is the ArcFollow bits of the path the scan took. A path already in the
   list keeps the bytes it came with. TRUE when the path is new. */
gboolean     arc_path_list_add    (ArcPathList *list,
				   const char  *path,
				   guint64      bytes,
				   guint        needs);
/* FALSE when the path isn't there. Either out may be NULL. */
gboolean     arc_path_list_find   (const ArcPathList *list,
				   const char        *path,
				   guint64           *bytes,
				   guint             *counted_in);
guint        arc_path_list_count  (const ArcPathList *list);

/* What the options as set would put in the archive. */
guint64      arc_path_list_total  (const ArcPathList *list,
				   guint              follow);
/* What turning one option off would take from that total, the others left as
   set. 0 when it's already off. A file with a path that needs 2 options counts
   in both, so these don't add up to anything. */
guint64      arc_path_list_change (const ArcPathList *list,
				   guint              follow,
				   ArcFollow          option);
void         arc_path_list_totals (const ArcPathList *list,
				   guint64            totals[ARC_MIX_COUNT]);

G_END_DECLS

#endif /* ARC_PATH_LIST_H */
