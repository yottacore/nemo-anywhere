/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */

/* nemo-extract.h - unpack an archive.

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

/* The archive core unpacks them: arc-extract.h has the job, and
 * arc-extract-names.h the names. The app still calls them by the names below,
 * from before they moved. Modularity, in
 * project/design_docs/20260929-101432_compression.md.
 */

#ifndef NEMO_EXTRACT_H
#define NEMO_EXTRACT_H

#include <gtk/gtk.h>
#include <gio/gio.h>

#include "arc-extract.h"
#include "arc-extract-names.h"
#include "arc-writer.h"

typedef ArcExtractLayout NemoExtractLayout;

#define NEMO_EXTRACT_HERE                 ARC_EXTRACT_HERE
#define NEMO_EXTRACT_TO_SUBFOLDER         ARC_EXTRACT_TO_SUBFOLDER

typedef ArcBackend NemoExtractBackend;

#define NEMO_EXTRACT_BACKEND_NONE         ARC_BACKEND_NONE
#define NEMO_EXTRACT_BACKEND_LIBARCHIVE   ARC_BACKEND_LIBARCHIVE
#define NEMO_EXTRACT_BACKEND_7Z           ARC_BACKEND_7Z
#define NEMO_EXTRACT_BACKEND_RAR          ARC_BACKEND_RAR

#define nemo_extract_is_archive_name      arc_extract_is_archive_name
#define nemo_extract_folder_name          arc_extract_folder_name
#define nemo_extract_unique_name          arc_extract_unique_name
#define nemo_extract_sanitize_path        arc_extract_sanitize_path
#define nemo_extract_backend_present      arc_extract_backend_present

typedef void (* NemoExtractCallback) (GFile    *destination_dir,
				      gboolean  success,
				      gpointer  callback_data);

/* arc_build_extract_command with the settings as they are now, and the app's
   host. */
char   **nemo_extract_build_command   (NemoExtractBackend  backend,
				       const char         *program,
				       const char         *archive_path,
				       const char         *destination_path,
				       const char         *password);

/* Queues the job. archives are GFile *; every one of them is unpacked into
   destination_dir, or into a folder of its own beneath it. */
void nemo_extract_files (GList               *archives,
			 GFile               *destination_dir,
			 NemoExtractLayout    layout,
			 GtkWindow           *parent_window,
			 NemoExtractCallback  done_callback,
			 gpointer             done_callback_data);

#endif /* NEMO_EXTRACT_H */
