/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */

/* nemo-dir-enum.h - directory enumeration that survives a long path on Windows.

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

#ifndef NEMO_DIR_ENUM_H
#define NEMO_DIR_ENUM_H

#include <gio/gio.h>

G_BEGIN_DECLS

/* Same contract as g_file_enumerate_children and friends. Everywhere except a
   long local path on Windows these hand straight over to GLib.

   The exception exists because GLib's own directory walk loses its way past
   MAX_PATH: it reports success and then lists the process's working directory
   instead of the folder asked for, which reads as an empty folder here and
   would send a recursive walk into the wrong tree. See design.md. */

GFileEnumerator *nemo_enumerate_children        (GFile                *dir,
                                                 const char           *attributes,
                                                 GFileQueryInfoFlags   flags,
                                                 GCancellable         *cancellable,
                                                 GError              **error);

void             nemo_enumerate_children_async  (GFile                *dir,
                                                 const char           *attributes,
                                                 GFileQueryInfoFlags   flags,
                                                 int                   io_priority,
                                                 GCancellable         *cancellable,
                                                 GAsyncReadyCallback   callback,
                                                 gpointer              user_data);

GFileEnumerator *nemo_enumerate_children_finish (GFile                *dir,
                                                 GAsyncResult         *result,
                                                 GError              **error);

/* The folder listing, and a later look at one file in it. Off Windows a link
   is followed to describe what it points at, the way GLib's own listing does,
   except a link onto a share its folder is not on (nemo-share.h). That one is
   described as the link itself, with this attribute set, so nothing goes to
   the share until someone opens the link: follow is TRUE from then on. On
   Windows a link is never followed, since there it already has the type of
   what it points at. The listing finishes with nemo_enumerate_children_finish. */

#define NEMO_FILE_ATTRIBUTE_LINK_UNFOLLOWED "nemo::link-unfollowed"

void             nemo_enumerate_listing_async   (GFile                *dir,
                                                 const char           *attributes,
                                                 int                   io_priority,
                                                 GCancellable         *cancellable,
                                                 GAsyncReadyCallback   callback,
                                                 gpointer              user_data);

GFileInfo       *nemo_query_listing_info        (GFile                *file,
                                                 const char           *attributes,
                                                 gboolean              follow,
                                                 GCancellable         *cancellable,
                                                 GError              **error);

void             nemo_query_listing_info_async  (GFile                *file,
                                                 const char           *attributes,
                                                 gboolean              follow,
                                                 int                   io_priority,
                                                 GCancellable         *cancellable,
                                                 GAsyncReadyCallback   callback,
                                                 gpointer              user_data);

GFileInfo       *nemo_query_listing_info_finish (GFile                *file,
                                                 GAsyncResult         *result,
                                                 GError              **error);

/* An entry the walk could not stat comes back with no type at all - a locked
   file at the root of a Windows drive, say - and GLib criticals on the missing
   attribute rather than answering. Read it through here instead. */

GFileType        nemo_dir_enum_file_type        (GFileInfo            *info);

/* TRUE when dir is a local path long enough that GLib's walk cannot be trusted.
   Exposed for the tests; callers should just use the three above. */
gboolean         nemo_dir_enum_path_is_long     (GFile                *dir);

G_END_DECLS

#endif /* NEMO_DIR_ENUM_H */
