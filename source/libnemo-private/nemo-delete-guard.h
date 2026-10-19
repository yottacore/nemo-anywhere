/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */

/* nemo-delete-guard.h - what a trash or delete may never take.

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

/* What stops a trash or delete from taking home with it, however the job came
 * about. The dialogs are one layer, and stray typing into a window that had just
 * come up got past them twice, so these rules sit where files are actually
 * removed. A path that never shows a dialog is held to them too.
 *
 * Home, any folder above it, and a folder where a drive or share is mounted are
 * never removed, and nothing walks into them. A tree removal never follows a
 * link to a folder. One job may not take most of what sits directly in home.
 * A delete key that arrives right after a window came up or took focus is
 * ignored, since that is typing meant for somewhere else.
 */

#ifndef NEMO_DELETE_GUARD_H
#define NEMO_DELETE_GUARD_H

#include <gio/gio.h>
#include <gtk/gtk.h>

gboolean nemo_delete_guard_is_protected (GFile        *file);

/* A folder a tree removal may walk into: not a symlink, not a junction.
   info may be NULL; if given it must have been read without following. */
gboolean nemo_delete_guard_is_real_folder (GFile        *file,
					   GFileInfo    *info,
					   GCancellable *cancellable);

gboolean nemo_delete_guard_check        (GFile        *file,
					 GError      **error);
gboolean nemo_delete_guard_remove_tree  (GFile        *file,
					 GCancellable *cancellable);

/* Take away a symlink or junction, and only that. Anything else, and a
   protected folder, is refused. Logged, but not asked about: it is for a link
   being put back as another one, not for a delete. */
gboolean nemo_delete_guard_remove_link  (GFile        *file,
					 GError      **error);
gboolean nemo_delete_guard_sweeps_home  (GList        *files);
gboolean nemo_delete_guard_must_ask     (gboolean      by_user,
					 guint         count,
					 gint          many);

gboolean nemo_delete_guard_in_grace     (gint64        focused_at,
					 gint64        now);
/* TRUE when the focus a window just took came from another of the program's
   own windows, such as a dialog closing. */
gboolean nemo_delete_guard_handed_back  (gint64        left_at,
					 gconstpointer left_from,
					 gconstpointer window,
					 gint64        now);
void     nemo_delete_guard_watch_window (GtkWindow    *window);
gboolean nemo_delete_guard_key_too_soon (GtkWidget    *widget);

void     nemo_delete_guard_log          (const char   *format,
					 ...) G_GNUC_PRINTF (1, 2);

#endif /* NEMO_DELETE_GUARD_H */
