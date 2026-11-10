/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */

/* nemo-thumbnail-memory.h - how many thumbnails are held ready to draw.

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

/* A folder of pictures is made top down, and each picture is kept in memory
 * as it is made, so scrolling anywhere finds it already drawn. That stops at
 * file-cache.memory-gib. Past it only the pictures near the view are kept,
 * and the ones used longest ago make room for them.
 *
 * A folder no view shows any more keeps its pictures for a minute, in case it
 * is gone back to, or until another folder of pictures is opened.
 *
 * Main thread only, apart from nemo_thumbnail_memory_full. */

#ifndef NEMO_THUMBNAIL_MEMORY_H
#define NEMO_THUMBNAIL_MEMORY_H

#include <gdk-pixbuf/gdk-pixbuf.h>

#include "nemo-directory.h"
#include "nemo-file.h"

/* The file now has pixbuf, or nothing. Only nemo-file.c calls these. */
void     nemo_thumbnail_memory_held          (NemoFile *file, GdkPixbuf *pixbuf);
void     nemo_thumbnail_memory_released      (NemoFile *file);

/* Drawn just now, so it is the last to go. */
void     nemo_thumbnail_memory_used          (NemoFile *file);

gboolean nemo_thumbnail_memory_has_room      (GdkPixbuf *pixbuf);

/* Safe from any thread. A render thread asks before reading a picture back
 * out of the store only to have it thrown away. */
gboolean nemo_thumbnail_memory_full          (void);

void     nemo_thumbnail_memory_folder_shown  (NemoDirectory *directory);
void     nemo_thumbnail_memory_folder_hidden (NemoDirectory *directory);

/* A new folder of pictures is opened, so the ones kept for a folder no longer
 * shown go now rather than when their minute is up. */
void     nemo_thumbnail_memory_flush_hidden  (void);

/* For the tests. */
gsize    nemo_thumbnail_memory_bytes         (void);
void     nemo_thumbnail_memory_set_times     (guint keep_ms, guint recent_ms);

#endif
