/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */

/* arc-extract.h - unpacking archives, as a job.

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

/* Reading reaches much further than writing does. libarchive opens the tar,
 * zip, 7z, rar, cab, lha, cpio, xar and iso families and the bare compressors
 * on their own, so most of what a person double-clicks needs nothing installed.
 * A 7z or rar command is still reached for where libarchive will not open the
 * file at all - a multi-volume set, or headers it cannot decrypt - and that
 * path stages into a temporary folder so the same collision handling applies
 * to both.
 *
 * The app queues the job and calls arc_extract_run on the job's thread, then
 * arc_extract_finish on the main thread. What's already there is asked about
 * through the host's conflict question, and a locked archive through its
 * password one.
 */

#ifndef ARC_EXTRACT_H
#define ARC_EXTRACT_H

#include <gio/gio.h>

#include "arc-host.h"
#include "arc-settings.h"

G_BEGIN_DECLS

typedef enum {
	ARC_EXTRACT_HERE,		/* straight into the destination folder */
	ARC_EXTRACT_TO_SUBFOLDER	/* each archive into a folder of its own */
} ArcExtractLayout;

typedef struct _ArcExtract ArcExtract;

/* host and settings are copied; the host's data has to outlive the job.
   archives are GFile *; every one of them is unpacked into destination_dir,
   or into a folder of its own beneath it. Any volume of a split archive
   stands for its first one, and each archive is unpacked once. */
ArcExtract *arc_extract_new         (const ArcHost     *host,
				     const ArcSettings *settings,
				     GList             *archives,
				     GFile             *destination_dir,
				     ArcExtractLayout   layout,
				     GCancellable      *cancellable);
void        arc_extract_free        (ArcExtract        *job);

/* The job's thread. One archive going wrong stops that archive, not the rest
   of them, and what went wrong first is kept for arc_extract_finish. */
void        arc_extract_run         (ArcExtract        *job);

/* The main thread, after the run. Says what went wrong. */
void        arc_extract_finish      (ArcExtract        *job);

gboolean    arc_extract_succeeded   (ArcExtract        *job);
/* Returns: (transfer none) */
GFile      *arc_extract_destination (ArcExtract        *job);

G_END_DECLS

#endif /* ARC_EXTRACT_H */
