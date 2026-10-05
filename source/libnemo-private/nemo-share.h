/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */

/* nemo-share.h - whether a path is on a network share, without asking it.

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

#ifndef NEMO_SHARE_H
#define NEMO_SHARE_H

#include <glib.h>

G_BEGIN_DECLS

/* A share is a UNC path or a drive letter mapped to one on Windows, and a
   mount of a network file system elsewhere, read from the mount table. Each
   answer comes from the path's text and that table, never from the share,
   and any thread may ask. The share holding the home folder counts as local,
   since the app works there from its first window on. */

gboolean nemo_path_is_on_a_share (const char *path);

/* The root of the share path is on: a mount point, \\server\share, or a
   drive letter. NULL when the path is local.
   Returns: (transfer full): free with g_free. */
char    *nemo_share_root_of (const char *path);

/* Whether a link sitting in link_folder, whose target reads target, leads
   onto a share other than the one it sits on. The target is placed by its
   text alone, so only the first link of a chain is read. */
gboolean nemo_share_link_leaves_for_a_share (const char *link_folder,
                                             const char *target);

/* Moves on whenever the shares seen change, so an answer kept per file knows
   to ask again. Never 0. */
guint    nemo_share_generation (void);

/* For tests: paths under these count as shares too. NULL clears them. */
void     nemo_share_set_roots_for_test (const char * const *roots);

G_END_DECLS

#endif /* NEMO_SHARE_H */
