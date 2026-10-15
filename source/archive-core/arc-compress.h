/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */

/* arc-compress.h - writing an archive, as a job.

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

/* One archive, or one per item when a selection is compressed separately,
 * written by the writer arc_pick_backend gives. The app queues the job and
 * calls arc_compress_run on the job's thread, then arc_compress_finish on the
 * main thread. "After OK" in
 * project/design_docs/20260929-101432_compression.md.
 */

#ifndef ARC_COMPRESS_H
#define ARC_COMPRESS_H

#include <gio/gio.h>

#include "arc-host.h"
#include "arc-mounts.h"
#include "arc-settings.h"
#include "arc-writer.h"

G_BEGIN_DECLS

typedef struct _ArcCompress ArcCompress;

/* host and settings are copied; the host's data has to outlive the job.
   result_file is what arc_compress_result hands back: the archive, or the
   folder the archives of one per item go in. */
ArcCompress *arc_compress_new         (const ArcHost     *host,
				       const ArcSettings *settings,
				       const ArcOptions  *options,
				       GCancellable      *cancellable,
				       GFile             *result_file);
void         arc_compress_free        (ArcCompress       *job);

/* One archive to write. sources are GFile *, and destination is the
   archive itself. */
void         arc_compress_add         (ArcCompress       *job,
				       GList             *sources,
				       GFile             *destination);
guint        arc_compress_count       (ArcCompress       *job);
GFile       *arc_compress_destination (ArcCompress       *job,
				       guint              index);
ArcBackend   arc_compress_backend     (ArcCompress       *job);

/* FALSE, with the job failed, when nothing installed can write it. Then
   there is nothing to run, only arc_compress_finish. */
gboolean     arc_compress_can_start   (ArcCompress       *job);

/* The job's thread. Writes each archive, checks it where the originals are
   to go, and keeps what went wrong for arc_compress_finish. */
void         arc_compress_run         (ArcCompress       *job);

/* The main thread, after the run. Says what went wrong, and sends the
   originals of the archives that checked out to the trash. */
void         arc_compress_finish      (ArcCompress       *job);

gboolean     arc_compress_succeeded   (ArcCompress       *job);
GFile       *arc_compress_result      (ArcCompress       *job);

/* A tool's stderr is folded into its stdout on Unix only. Elsewhere it gets
   a pipe of its own, read on a thread from start to finish, and the caller
   puts what it said after the rest. finish takes NULL, and returns NULL where
   there was no pipe; caller frees. */
GSubprocessFlags arc_tool_stderr_flag   (void);
GThread         *arc_tool_stderr_start  (const ArcHost *host,
					 ArcRun        *process);
char            *arc_tool_stderr_finish (GThread       *reader);

/* For tests: the job's walk tells filesystems apart by the table this
   makes, called on the job's thread, rather than the system's. NULL puts
   the system's back. */
void         arc_compress_set_mounts_for_test (ArcMountTable *(*mounts) (void));

G_END_DECLS

#endif /* ARC_COMPRESS_H */
