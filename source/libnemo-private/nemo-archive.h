/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */

/* nemo-archive.h - create an archive from a selection.

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

/* The archive core writes them: arc-writer.h has the writers and what each
 * can do. The app still calls them by the names below, from before they
 * moved. Modularity, in project/design_docs/20260929-101432_compression.md.
 */

#ifndef NEMO_ARCHIVE_H
#define NEMO_ARCHIVE_H

#include <gtk/gtk.h>
#include <gio/gio.h>

#include "nemo-tool-run.h"
#include "nemo-progress-info.h"
#include "arc-command.h"
#include "arc-link-options.h"
#include "arc-mounts.h"
#include "arc-volumes.h"
#include "arc-writer.h"

typedef ArcFormat  NemoArchiveFormat;
typedef ArcBackend NemoArchiveBackend;
typedef ArcCaps    NemoArchiveCaps;
typedef ArcOptions NemoArchiveOptions;

#define NEMO_ARCHIVE_FORMAT_ZIP           ARC_FORMAT_ZIP
#define NEMO_ARCHIVE_FORMAT_TAR           ARC_FORMAT_TAR
#define NEMO_ARCHIVE_FORMAT_TAR_GZ        ARC_FORMAT_TAR_GZ
#define NEMO_ARCHIVE_FORMAT_TAR_XZ        ARC_FORMAT_TAR_XZ
#define NEMO_ARCHIVE_FORMAT_7Z            ARC_FORMAT_7Z
#define NEMO_ARCHIVE_FORMAT_RAR           ARC_FORMAT_RAR
#define NEMO_ARCHIVE_N_FORMATS            ARC_N_FORMATS

#define NEMO_ARCHIVE_BACKEND_NONE         ARC_BACKEND_NONE
#define NEMO_ARCHIVE_BACKEND_LIBARCHIVE   ARC_BACKEND_LIBARCHIVE
#define NEMO_ARCHIVE_BACKEND_7Z           ARC_BACKEND_7Z
#define NEMO_ARCHIVE_BACKEND_RAR          ARC_BACKEND_RAR

#define NEMO_ARCHIVE_CAP_LEVEL            ARC_CAP_LEVEL
#define NEMO_ARCHIVE_CAP_PASSWORD         ARC_CAP_PASSWORD
#define NEMO_ARCHIVE_CAP_ENCRYPT_NAMES    ARC_CAP_ENCRYPT_NAMES
#define NEMO_ARCHIVE_CAP_SPLIT            ARC_CAP_SPLIT
#define NEMO_ARCHIVE_CAP_SOLID            ARC_CAP_SOLID
#define NEMO_ARCHIVE_CAP_DEDUPE           ARC_CAP_DEDUPE
#define NEMO_ARCHIVE_CAP_STORE_LINKS      ARC_CAP_STORE_LINKS
#define NEMO_ARCHIVE_CAP_RECOVERY         ARC_CAP_RECOVERY
#define NEMO_ARCHIVE_CAP_LOCK             ARC_CAP_LOCK

#define NEMO_ARCHIVE_LEVEL_STORE          ARC_LEVEL_STORE
#define NEMO_ARCHIVE_LEVEL_DEFAULT        ARC_LEVEL_DEFAULT
#define NEMO_ARCHIVE_LEVEL_MAX            ARC_LEVEL_MAX

#define nemo_archive_options_init         arc_options_init
#define nemo_archive_options_clear        arc_options_clear
#define nemo_archive_options_copy         arc_options_copy
#define nemo_archive_format_id            arc_format_id
#define nemo_archive_format_name          arc_format_name
#define nemo_archive_format_extension     arc_format_extension
#define nemo_archive_format_from_id       arc_format_from_id
#define nemo_archive_format_available     arc_format_available
#define nemo_archive_format_caps          arc_format_caps
#define nemo_archive_find_command         arc_find_command
#define nemo_archive_backend_caps         arc_backend_caps
#define nemo_archive_backend_present      arc_backend_present
#define nemo_archive_backend_link_stores  arc_backend_link_stores
#define nemo_archive_format_link_stores   arc_format_link_stores
#define nemo_archive_options_link_stores  arc_options_link_stores
#define nemo_archive_pick_backend         arc_pick_backend
#define nemo_archive_links_cost_threads   arc_links_cost_threads
#define nemo_archive_hide_backend         arc_hide_backend
#define nemo_archive_set_7z_keeps_links   arc_set_7z_keeps_links
#define nemo_archive_strip_extension      arc_strip_extension
#define nemo_archive_apply_extension      arc_apply_extension
#define nemo_archive_suggest_name         arc_suggest_name
#define nemo_archive_each_name            arc_each_name
#define nemo_archive_add_run_switches     arc_add_run_switches
#define nemo_archive_only_skipped_links   arc_only_skipped_links
#define nemo_archive_volume_name          arc_volume_name
#define nemo_archive_collapse_volume      arc_collapse_volume
#define nemo_archive_volume_preset        arc_volume_preset
#define nemo_archive_volume_preset_find   arc_volume_preset_find
#define nemo_archive_parse_size           arc_parse_size
#define nemo_archive_format_size          arc_format_size

typedef void (* NemoArchiveCallback) (GFile    *archive_file,
				      gboolean  success,
				      gpointer  callback_data);

/* For tests: the job's walk tells filesystems apart by the table this
   makes, called on the job's thread, rather than the system's. NULL puts
   the system's back. */
void               nemo_archive_set_mounts_for_test (ArcMountTable *(*mounts) (void));
/* For tests: called on the job's thread each time its pre-scan reports to
   the progress info, the start and the end included. NULL for none. */
void               nemo_archive_set_prescan_watch_for_test (void (*watch) (NemoProgressInfo *info));

/* arc_build_command and arc_build_links_command with the settings as they
   are now, and the app's host. */
char   **nemo_archive_build_command (NemoArchiveBackend        backend,
				     NemoArchiveFormat         format,
				     const NemoArchiveOptions *options,
				     const char               *program,
				     const char               *archive_path,
				     GList                    *names,
				     GList                    *leave_out);
char   **nemo_archive_build_links_command (NemoArchiveBackend        backend,
					   NemoArchiveFormat         format,
					   const NemoArchiveOptions *options,
					   const char               *program,
					   const char               *archive_path,
					   GList                    *names);

/* A tool's stderr is folded into its stdout on Unix only. Elsewhere it gets
   a pipe of its own, read on a thread from start to finish, and the caller
   puts what it said after the rest. finish takes NULL, and returns NULL where
   there was no pipe; caller frees. */
GSubprocessFlags nemo_archive_tool_stderr_flag   (void);
GThread         *nemo_archive_tool_stderr_start  (NemoToolRun *process);
char            *nemo_archive_tool_stderr_finish (GThread     *reader);

/* Whether an archive written with these options can be read back at all, and
   so whether deleting the originals may be offered for it. A split set, an
   archive with its names encrypted, and a 7z with a password that keeps links
   answer FALSE. */
gboolean nemo_archive_can_verify (const NemoArchiveOptions *options);
gboolean nemo_archive_should_confirm_password (const NemoArchiveOptions *options);

/* Walks the sources again and reads the archive back, so that nothing may be
   deleted on the strength of the writer saying it went well. Answers TRUE only
   when every file that should have gone in is in there under the same relative
   path and the same size, and nothing was left out along the way. On FALSE,
   reason is set to a sentence naming what did not line up; the caller frees it.
   The walk is deliberately its own, rather than the one the job wrote from -
   checking with the same code that did the work proves very little. */
gboolean nemo_archive_verify (GFile                    *archive_file,
			      GList                    *sources,
			      const NemoArchiveOptions *options,
			      NemoArchiveBackend        backend,
			      GCancellable             *cancellable,
			      char                    **reason);

/* Queues the job. sources are GFile *; destination is the archive itself. */
void nemo_archive_create (GList                    *sources,
			  GFile                    *destination,
			  const NemoArchiveOptions *options,
			  GtkWindow                *parent_window,
			  NemoArchiveCallback       done_callback,
			  gpointer                  done_callback_data);

/* One archive per source instead, each named after its item and all written
   into destination_dir, under one progress window and one cancel. The callback
   is handed that folder rather than any one archive. */
void nemo_archive_create_each (GList                    *sources,
			       GFile                    *destination_dir,
			       const NemoArchiveOptions *options,
			       GtkWindow                *parent_window,
			       NemoArchiveCallback       done_callback,
			       gpointer                  done_callback_data);

#endif /* NEMO_ARCHIVE_H */
