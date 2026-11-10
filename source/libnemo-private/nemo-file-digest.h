/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */

/* nemo-file-digest.h - a checksum of what is in a file.

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

/* blake3, 256 bits of it, which is what the file cache keys contents on.
 *
 * The algorithm was picked for speed rather than for anything cryptographic:
 * this runs while a folder is being listed, so a checksum that costs more than
 * reading the file is a checksum nobody can afford. It is vendored under
 * vendor/blake3 rather than linked, which is the other half of the reason -
 * see vendor/README.md.
 *
 * Nothing here decides when a checksum is worth computing. That is the caller's
 * call, and the answer is usually "only if the file was being read anyway".
 */

#ifndef NEMO_FILE_DIGEST_H
#define NEMO_FILE_DIGEST_H

#include <gio/gio.h>

#include "nemo-cache-db.h"

G_BEGIN_DECLS

/* Text form of a checksum, base64url with no padding, as it goes into an
 * extended attribute, 43 characters plus the terminator. */
#define NEMO_FILE_DIGEST_TEXT_LEN 44

/* Checksums a buffer already in memory. `digest` is NEMO_CACHE_DIGEST_LEN
 * bytes. */
void nemo_file_digest_bytes (gconstpointer data, gsize len, guint8 *digest);

/* Reads the file and checksums it. False on any read error, or if the read was
 * canceled - which the thumbnail queue does on a folder change, so a partial
 * answer must never be mistaken for a real one. */
gboolean nemo_file_digest_file (GFile        *file,
				guint8       *digest,
				GCancellable *cancellable,
				GError      **error);

/* Between the binary form and the text form. `text` is at least
 * NEMO_FILE_DIGEST_TEXT_LEN bytes; from_text refuses anything that isn't
 * exactly one checksum. */
void     nemo_file_digest_to_text   (const guint8 *digest, char *text);
gboolean nemo_file_digest_from_text (const char *text, guint8 *digest);

/* The checksum a previous run left on the file itself, in 3 extended
 * attributes: the checksum, and the size and time it was taken at. False
 * unless all 3 are there and the other 2 still describe the file, since
 * an attribute that survived an edit describes something that is gone.
 *
 * The point of keeping it on the file is that it travels with the file. A copy
 * to another machine, or to a drive this program has never seen, arrives
 * already knowing what it is. */
gboolean nemo_file_digest_read_attr (GFile  *file,
				     gint64  bytes,
				     gint64  mtime,
				     guint8 *digest);

/* Leaves one there. Writing an extended attribute is slow enough to be worth
 * doing after everything else is already done, never in front of a draw. */
gboolean nemo_file_digest_write_attr (GFile        *file,
				      gint64        bytes,
				      gint64        mtime,
				      const guint8 *digest);

G_END_DECLS

#endif /* NEMO_FILE_DIGEST_H */
