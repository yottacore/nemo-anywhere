/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*-

   nemo-thumbnails.h: Thumbnail code for icon factory.
 
   Copyright (C) 2000 Eazel, Inc.
  
   This program is free software; you can redistribute it and/or
   modify it under the terms of the GNU General Public License as
   published by the Free Software Foundation; either version 2 of the
   License, or (at your option) any later version.
  
   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   General Public License for more details.
  
   You should have received a copy of the GNU General Public
   License along with this program; if not, write to the
   Free Software Foundation, Inc., 51 Franklin Street - Suite 500,
   Boston, MA 02110-1335, USA.
  
   Author: Andy Hertzfeld <andy@eazel.com>
*/

#ifndef NEMO_THUMBNAILS_H
#define NEMO_THUMBNAILS_H

#include <gdk-pixbuf/gdk-pixbuf.h>
#include <libnemo-private/nemo-file.h>
#include <libnemo-private/nemo-cache-db.h>

/* Cool-off period between last file modification time and thumbnail creation */
#define THUMBNAIL_CREATION_DELAY_SECS 3

/* Thumbnails are made and loaded at a multiple of this, so a zoom that moves a
 * few pixels at a time does not render or reload on every step. */
#define NEMO_THUMBNAIL_SIZE_STEP 128

/* What a load found. `pixbuf` is NULL when there is nothing to show. */
typedef struct {
	GdkPixbuf *pixbuf;
	gboolean   capped;		/* decoded smaller than the image it came from */
	gboolean   from_store;
	int        stored_size;		/* size the stored copy was rendered for */
	gboolean   stored_capped;	/* and it was cut down to fit that size */
	gboolean   failed;		/* the store says this could not be drawn */
	time_t     shared_mtime;	/* stamp on a freedesktop cache thumbnail */
} NemoThumbnailLoaded;

/* Queues a render at `size` pixels, rounded up to a step. A file already
 * queued keeps its place and takes the larger of the two sizes. One that a
 * folder queued ahead has placed goes in that place. */
void       nemo_create_thumbnail                (NemoFile *file, int size);

/* Places every file in `files` in that order, and renders the ones not yet
 * on screen into the store, first to last. Scrolling cannot move one up. The
 * picture is only held for a file that came into view meanwhile. */
void       nemo_thumbnail_render_ahead          (GList *files, int size);
/* Reads back pictures already made for files near the view, to hold them.
 * Used once the memory for pictures is used up. */
void       nemo_thumbnail_load_near_view        (GList *files, int size);
int        nemo_thumbnail_size_step             (int size);

/* The store first, and the freedesktop cache only if the store has nothing.
 * Decodes no larger than `max_size`. Blocks on the disk, so it belongs on a
 * worker thread. */
void       nemo_thumbnail_load                  (const char          *uri,
                                                 const NemoFileId    *id,
                                                 const char          *shared_path,
                                                 int                  max_size,
                                                 NemoThumbnailLoaded *out);
void       nemo_thumbnail_file_id               (NemoFile *file, NemoFileId *id);

/* JPEG at quality 90, or PNG when the picture has see-through parts, which JPEG
 * cannot carry. NULL if it could not be encoded. */
GBytes    *nemo_thumbnail_encode                (GdkPixbuf           *pixbuf,
                                                 NemoThumbnailFormat *format);

gboolean   nemo_can_thumbnail                   (NemoFile *file);
gboolean   nemo_can_thumbnail_internally        (NemoFile *file);
void       nemo_thumbnail_frame_image           (GdkPixbuf **pixbuf);
void       nemo_thumbnail_pad_top_and_bottom    (GdkPixbuf **pixbuf,
                                                 gint        extra_height);
/* Queue handling: */
void       nemo_thumbnail_remove_from_queue     (const char   *file_uri);
/* Jobs a worker thread has right now, for tests. */
guint      nemo_thumbnail_running_jobs          (void);

/* Progress, for the status bar. Only nemo-file.c notes a job, as the file's
 * thumbnailing flag goes on or off. `func` is called as the queue starts on
 * a new batch. */
typedef void (*NemoThumbnailJobsFunc) (gpointer data);

void       nemo_thumbnail_note_job              (gboolean               started);
void       nemo_thumbnail_jobs                  (guint                 *done,
                                                 guint                 *waiting);
/* Caps the rendered count at the share of the batch built so far. */
void       nemo_thumbnail_rendered_for_bar      (guint                  done,
                                                 guint                  waiting,
                                                 guint                 *shown,
                                                 guint                 *wanted);
void       nemo_thumbnail_watch_jobs            (NemoThumbnailJobsFunc  func,
                                                 gpointer               data);
void       nemo_thumbnail_unwatch_jobs          (NemoThumbnailJobsFunc  func,
                                                 gpointer               data);

gboolean   nemo_thumbnail_factory_check_status          (void);

#endif /* NEMO_THUMBNAILS_H */
