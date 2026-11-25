/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */

/* nemo-shortcuts-window.h - the keys the keyboard shortcuts window shows.

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

#ifndef NEMO_SHORTCUTS_WINDOW_H
#define NEMO_SHORTCUTS_WINDOW_H

#include <gtk/gtk.h>

G_BEGIN_DECLS

/* Returns: (transfer none): the keyboard shortcuts window, its rows
   ready for nemo_shortcuts_window_show_keys */
GtkWidget *nemo_shortcuts_window_take   (GtkBuilder *builder);

/* Each row shows the keys in use, and a row with none is hidden. Called once
   the window is shown, and again on its own as a key changes. Ctrl+I is
   Invert selection under Nemo Anywhere, and Properties under the others. */
void       nemo_shortcuts_window_show_keys (GtkWidget *window,
					     gboolean   primary_i_inverts);

/* A row left out whatever its keys, such as Ctrl+Shift+V when the paste
   target setting gives it nothing to do. */
void       nemo_shortcuts_window_set_unused (GtkWidget  *window,
					      const char *row_id,
					      gboolean    unused);

/* For the test: the row ids, NULL-terminated. */
const char *const *nemo_shortcuts_window_row_ids (void);

G_END_DECLS

#endif /* NEMO_SHORTCUTS_WINDOW_H */
