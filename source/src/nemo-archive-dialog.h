/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */

/* nemo-archive-dialog.h - ask what archive to make, then make it.

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

#ifndef NEMO_ARCHIVE_DIALOG_H
#define NEMO_ARCHIVE_DIALOG_H

#include <gtk/gtk.h>
#include <gio/gio.h>

#include "arc-mounts.h"

/* files are GFile *; default_dir is where the archive is offered to be put,
   normally the folder being viewed. whole_folder says the files are everything
   the folder is showing, which is what lets a multiple selection borrow the
   folder's name. links_or_folders says some of them are links or folders, so
   the link choices apply. Runs the job itself when accepted. */
void nemo_archive_dialog_show (GtkWindow *parent_window,
			       GList     *files,
			       GFile     *default_dir,
			       gboolean   whole_folder,
			       gboolean   links_or_folders);

/* How tall the Compress dialog's options may grow before they scroll: what
   the work area leaves under the closed dialog (frame included), and never
   less than a strip that still shows a few rows. */
int  nemo_archive_options_room (int work_height,
				int closed_height);

/* For tests: where each size scan gets its mount table, NULL for the
   system's, and how many scans are running. */
void  nemo_archive_dialog_set_mounts_for_test (ArcMountTable *(*mounts) (void));
guint nemo_archive_dialog_scans_for_test      (void);

#endif /* NEMO_ARCHIVE_DIALOG_H */
