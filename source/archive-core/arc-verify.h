/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */

/* arc-verify.h - the check an archive passes before its originals may go.

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

#ifndef ARC_VERIFY_H
#define ARC_VERIFY_H

#include <gio/gio.h>

#include "arc-host.h"
#include "arc-writer.h"

G_BEGIN_DECLS

/* Whether an archive written with these options can be read back at all, and
   so whether deleting the originals may be offered for it. A split set, an
   archive with its names encrypted, and a 7z with a password that keeps links
   answer FALSE. */
gboolean arc_can_verify              (const ArcOptions *options);
gboolean arc_should_confirm_password (const ArcOptions *options);

/* Walks the sources again, through the host's walk, and reads the archive
   back, so that nothing may be deleted on the strength of the writer saying
   it went well. Answers TRUE only when every file that should have gone in is
   in there under the same relative path and the same size, and nothing was
   left out along the way. On FALSE, reason is set to a sentence naming what
   did not line up; the caller frees it. The walk is deliberately its own,
   rather than the one the job wrote from - checking with the same code that
   did the work proves very little. */
gboolean arc_verify (const ArcHost    *host,
		     GFile            *archive_file,
		     GList            *sources,
		     const ArcOptions *options,
		     ArcBackend        backend,
		     GCancellable     *cancellable,
		     char            **reason);

/* libarchive 3.8.9 and older reads a rar link's target length as one byte,
   so it can't read a link whose target, as rar keeps it, is 128 bytes or
   more, and reads nothing from there on. Fixed upstream in bb42a8c, after
   3.8.9. Taking 3.8.10 as the first release with the fix is a guess. The
   check before a delete and extracting both go by these, so they move
   together. */
gboolean arc_rar_target_unreadable      (gsize stored_length);

struct archive;
struct archive_entry;

/* Whether the reader is at such a link: entry, as archive_read_next_header
   left it, is a link in a rar with a target that long. Mostly the read ends
   there, on an error or as if at the end. Now and then it hands the link
   back and fails on the next header instead, depending on the bytes after
   it, so a link handed back is a stop too. */
gboolean arc_rar_stopped_at_long_target (struct archive       *a,
					 struct archive_entry *entry);

G_END_DECLS

#endif /* ARC_VERIFY_H */
