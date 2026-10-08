/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */

/* nemo-archive-host.h - the app's side of the archive core.

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

/* The core never reads the settings file. This is where its values come from
 * and go back to. Modularity in
 * project/design_docs/20260929-101432_compression.md.
 */

#ifndef NEMO_ARCHIVE_HOST_H
#define NEMO_ARCHIVE_HOST_H

#include <glib.h>

#include "arc-host.h"
#include "arc-link-options.h"

G_BEGIN_DECLS

/* The link and filesystem choices the Compress dialog starts from. Remembered
   store links and follow links values are mapped over, then dropped. */
void nemo_archive_link_options_load (ArcLinkOptions       *options);
void nemo_archive_link_options_save (const ArcLinkOptions *options);

/* An ArcHost with what the core has been given so far: the folder walk,
   which gets past MAX_PATH on Windows, and the share check. */
void nemo_archive_host_init         (ArcHost              *host);

G_END_DECLS

#endif /* NEMO_ARCHIVE_HOST_H */
