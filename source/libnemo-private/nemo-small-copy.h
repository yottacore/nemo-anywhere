/* nemo-small-copy.h - copy a small file with plain writes instead of a clone.
 *
 * Copyright © 2026 t00mietum (CryptogID: ปʬϝღถɔ4რఠΔթะ9ƾǝu)
 *
 * This program is free software; you can redistribute it and/or modify it under
 * the terms of the GNU General Public License version 2, as published by the
 * Free Software Foundation.
 */

#ifndef NEMO_SMALL_COPY_H
#define NEMO_SMALL_COPY_H

#include <gio/gio.h>

#include "nemo-cache-db.h"

G_BEGIN_DECLS

typedef enum {
	NEMO_SMALL_COPY_NOT_TRIED,   /* nothing made; do the ordinary copy */
	NEMO_SMALL_COPY_DONE,
	NEMO_SMALL_COPY_FAILED       /* the target was made, then a write failed */
} NemoSmallCopyResult;

/* Below this many bytes a copy skips the clone, from the settings file. 0 means
   always clone. Only Linux has anything to skip, so it is 0 everywhere else. */
goffset nemo_small_copy_limit (void);

/* Copies a regular local file under limit bytes without letting the file
   system clone it. Anything it won't take, or a target it can't make, is
   NOT_TRIED with nothing written, so the ordinary copy gives the usual answer.
   An overwrite is never taken. copied is the byte count on DONE.

   The whole file passes through memory, so with file-cache.checksum-full-reads
   on, sum gets the source's size, time and checksum on DONE. has_digest is
   left FALSE when that is off, or the source changed during the read. sum may
   be NULL. */
NemoSmallCopyResult nemo_small_copy (GFile          *src,
                                     GFile          *dest,
                                     GFileCopyFlags  flags,
                                     goffset         limit,
                                     GCancellable   *cancellable,
                                     goffset        *copied,
                                     NemoFileId     *sum,
                                     GError        **error);

/* Keeps sum for the source and the target in the file cache, and onto both
   files with file-cache.save-checksum on too. Called once the caller has set
   the target's times, since the cache keys a checksum on them. */
void nemo_small_copy_keep_checksum (GFile            *src,
                                    GFile            *dest,
                                    const NemoFileId *sum);

G_END_DECLS

#endif
