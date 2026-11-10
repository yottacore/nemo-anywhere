/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */

/* nemo-extract-conflict-dialog.h - what to do about something an archive would
   overwrite.

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

/* The same question the copy and move conflict prompt asks, and the same
 * answers - but the incoming side is an entry inside an archive rather than a
 * file on disk, so it is described from what the archive says about it. The
 * responses are the CONFLICT_RESPONSE_* the copy prompt already defines, so a
 * caller that handles one handles both.
 */

#ifndef NEMO_EXTRACT_CONFLICT_DIALOG_H
#define NEMO_EXTRACT_CONFLICT_DIALOG_H

#include <gtk/gtk.h>
#include <gio/gio.h>

#include "nemo-file-conflict-dialog.h"

/* entry_mtime is unix seconds, or 0 when the archive didn't record one. */
GtkWidget *nemo_extract_conflict_dialog_new (GtkWindow  *parent,
					     const char *archive_name,
					     const char *entry_name,
					     gboolean    entry_is_dir,
					     guint64     entry_size,
					     gint64      entry_mtime,
					     GFile      *destination,
					     GFile      *destination_dir);

char     *nemo_extract_conflict_dialog_get_new_name     (GtkWidget *dialog);
gboolean  nemo_extract_conflict_dialog_get_apply_to_all (GtkWidget *dialog);

#endif /* NEMO_EXTRACT_CONFLICT_DIALOG_H */
