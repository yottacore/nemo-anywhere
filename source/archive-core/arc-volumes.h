/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */

/* arc-volumes.h - volume sizes, and the names a split is written under.

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

#ifndef ARC_VOLUMES_H
#define ARC_VOLUMES_H

#include <gio/gio.h>

#include "arc-writer.h"

G_BEGIN_DECLS

/* The volume sizes offered, with what each is for. Returns the label, or NULL
   past the last; size gets the size alone, as a size the parser reads. */
const char *arc_volume_preset      (guint        index,
				    const char **size);
/* Which preset text names, by its label or its size alone, or -1. */
int         arc_volume_preset_find (const char  *text);

/* "700 MB", "4480m", "1.5 GB" -> bytes. Returns FALSE on anything unreadable.
   A preset's label reads as its size. */
gboolean    arc_parse_size         (const char  *text,
				    guint64     *bytes);
char       *arc_format_size        (guint64      bytes);

/* What the tool actually writes when splitting is on. `digits` is how wide the
   number is: 7z always uses 3, rar picks its own. */
char       *arc_volume_name        (const char  *archive_name,
				    ArcBackend   backend,
				    guint        volume,
				    guint        digits);
GFile      *arc_volume_file        (GFile       *destination,
				    ArcBackend   backend,
				    guint        volume,
				    guint        digits);

/* Renames a split that came out as one volume back to the name asked for.
   Returns the file the archive is in, which the caller owns. */
GFile      *arc_collapse_volume    (GFile       *destination,
				    ArcBackend   backend);

/* Each volume, finished or not, that a split left beside destination. */
void        arc_for_each_volume    (GFile       *destination,
				    ArcBackend   backend,
				    void       (*action) (GFile *file, gpointer data),
				    gpointer     data);

G_END_DECLS

#endif /* ARC_VOLUMES_H */
