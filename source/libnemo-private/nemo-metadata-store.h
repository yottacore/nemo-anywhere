/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */

/* nemo-metadata-store.h - app-owned per-file metadata store.

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

/* Replaces the gvfs metadata daemon: per-file view/sort state, custom
 * icons, emblems and favorite markers persist in one JSON file under
 * the app config dir, identically on every platform. Values written
 * here shadow anything a metadata daemon may still report; keys we
 * never write remain readable from gvfs where it exists.
 */

#ifndef NEMO_METADATA_STORE_H
#define NEMO_METADATA_STORE_H

#include <gio/gio.h>

/* value NULL unsets the key; keys are bare names, without "metadata::" */
void nemo_metadata_store_set_string    (const char *uri,
					const char *key,
					const char *value);
void nemo_metadata_store_set_stringv   (const char *uri,
					const char *key,
					char      **values);

/* copies; NULL when unset. A list entry reads as NULL through get_string
 * and a string entry as NULL through get_stringv. */
char    *nemo_metadata_store_get_string  (const char *uri,
					  const char *key);
char   **nemo_metadata_store_get_stringv (const char *uri,
					  const char *key);

/* TRUE when the uri has at least one of the NULL-terminated keys */
gboolean nemo_metadata_store_has_any     (const char         *uri,
					  const char * const *keys);

/* overlay stored values onto a queried GFileInfo as metadata:: attributes */
/* TRUE when nothing at all is stored, without taking the lock. Lets the
 * per-file info update skip the uri build entirely on a fresh install. */
gboolean nemo_metadata_store_is_empty  (void);

void nemo_metadata_store_apply_to_info (const char *uri,
					GFileInfo  *info);

/* re-key on move/rename; rewrites descendants when a directory moves.
 * Safe to call from worker threads. */
void nemo_metadata_store_rename        (const char *old_uri,
					const char *new_uri);

/* write any pending changes out now (call on shutdown) */
void nemo_metadata_store_flush         (void);

#endif /* NEMO_METADATA_STORE_H */
