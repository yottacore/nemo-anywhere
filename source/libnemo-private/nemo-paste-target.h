/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */

/* nemo-paste-target.h - which folder a paste or a new item goes to.

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


/* Only the list view can show more than one folder, so only it asks. See
 * design_docs/20261020-103804_behavior-models.md, "Where pasted and new items go".
 */

#ifndef NEMO_PASTE_TARGET_H
#define NEMO_PASTE_TARGET_H

#include <gtk/gtk.h>

#include "nemo-global-preferences.h"

/* Says whether the row is a folder shown expanded. */
typedef gboolean (* NemoPasteTargetExpandedFunc) (GtkTreePath *path, gpointer user_data);

/* Takes the selected rows. Returns the row of the folder that takes the
 * paste, or NULL for the viewed folder. Free with gtk_tree_path_free(). */
GtkTreePath *nemo_paste_target_row (NemoPasteTarget              model,
				    GList                       *selected_paths,
				    NemoPasteTargetExpandedFunc  is_expanded_folder,
				    gpointer                     user_data);

/* Where Create link, Compress and Extract here put what they make: the
 * target folder under Nemo Classic, and next to the selected rows otherwise,
 * even when that's one expanded folder. Same return as above. */
GtkTreePath *nemo_paste_target_beside_row (NemoPasteTarget              model,
					   GList                       *selected_paths,
					   NemoPasteTargetExpandedFunc  is_expanded_folder,
					   gpointer                     user_data);

/* What Ctrl+Shift+V is under a model, untranslated. NULL under Nemo Classic,
 * which has no key. */
typedef struct {
	const char *label;
	const char *title;
	const char *tooltip;
} NemoPasteKey;

const NemoPasteKey *nemo_paste_target_key (NemoPasteTarget model);

#endif /* NEMO_PASTE_TARGET_H */
