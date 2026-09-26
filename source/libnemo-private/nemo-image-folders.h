/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */

/* nemo-image-folders.h - which folders are mostly pictures.

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

/* Whether a folder is mostly pictures can only be counted once it has loaded,
 * and by then it is already on screen in whatever view it opened in. So the
 * answer is kept for the folders seen lately, and the folders one level down
 * from the one in front are counted ahead of time, which lets the view be
 * picked right on the first try.
 *
 * All of this runs on the main thread apart from the count itself. */

#ifndef NEMO_IMAGE_FOLDERS_H
#define NEMO_IMAGE_FOLDERS_H

#include <gio/gio.h>

#include "nemo-directory.h"

/* The rule, apart from how the counts were come by. */
gboolean nemo_image_folders_counts_say_mostly (guint images, guint others);

void     nemo_image_folders_note              (GFile *location, gboolean mostly);

/* FALSE when nothing is known about location. */
gboolean nemo_image_folders_known             (GFile *location, gboolean *mostly);

/* Whether folder should open in icon view: mostly images, the switch is on,
   it is not a search, and saved_view (the folder's own saved view, or NULL)
   does not say otherwise. Before its files are in, that is only known if it
   was seen lately or counted ahead from the folder above it, and otherwise it
   is FALSE. */
gboolean nemo_image_folders_wants_icon_view   (NemoFile *folder, const char *saved_view);

/* Counts the folders directly inside directory in a thread, and notes each
 * answer as it comes back. Starting another stops the one before. */
void     nemo_image_folders_look_ahead        (NemoDirectory *directory);

typedef void (*NemoImageFoldersDone) (gpointer user_data);

/* For the test: done runs once the answers are noted. */
void     nemo_image_folders_look_ahead_full   (NemoDirectory        *directory,
					       NemoImageFoldersDone  done,
					       gpointer              user_data);

#endif
