/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */

/* nemo-paste-target.c - which folder a paste or a new item goes to.

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


#include <config.h>

#include <glib/gi18n.h>

#include "nemo-paste-target.h"

/* The deepest row that is a parent of every selected row, or NULL when that's
   the viewed folder. A row's parent is the row above it in the tree, so that way an
   "(Empty)" row counts as inside its folder. */
static GtkTreePath *
lowest_common_parent (GList *paths, gboolean same_folder_only)
{
	GtkTreePath *first = paths->data;
	gint *first_ix;
	gint first_depth;
	gint common;
	GList *l;

	first_ix = gtk_tree_path_get_indices_with_depth (first, &first_depth);
	common = first_depth - 1;

	for (l = paths->next; l != NULL && common > 0; l = l->next) {
		gint depth;
		gint *ix = gtk_tree_path_get_indices_with_depth (l->data, &depth);
		gint n = MIN (common, depth - 1);
		gint i = 0;

		while (i < n && ix[i] == first_ix[i]) {
			i++;
		}
		if (same_folder_only && (i != common || depth - 1 != common)) {
			return NULL;
		}
		common = i;
	}

	if (common <= 0) {
		return NULL;
	}
	return gtk_tree_path_new_from_indicesv (first_ix, (gsize) common);
}

/* Returns: (transfer full) (nullable): free with gtk_tree_path_free */
GtkTreePath *
nemo_paste_target_row (NemoPasteTarget              model,
		       GList                       *selected_paths,
		       NemoPasteTargetExpandedFunc  is_expanded_folder,
		       gpointer                     user_data)
{
	if (selected_paths == NULL || model == NEMO_PASTE_TARGET_VIEWED_FOLDER) {
		return NULL;
	}

	/* One expanded folder alone takes it. Along with anything else it's only one more object. */
	if (selected_paths->next == NULL &&
	    is_expanded_folder (selected_paths->data, user_data)) {
		return gtk_tree_path_copy (selected_paths->data);
	}

	return lowest_common_parent (selected_paths, model == NEMO_PASTE_TARGET_CLASSIC);
}

/* Returns: (transfer full) (nullable): free with gtk_tree_path_free */
GtkTreePath *
nemo_paste_target_beside_row (NemoPasteTarget              model,
			      GList                       *selected_paths,
			      NemoPasteTargetExpandedFunc  is_expanded_folder,
			      gpointer                     user_data)
{
	if (model == NEMO_PASTE_TARGET_CLASSIC) {
		return nemo_paste_target_row (model, selected_paths, is_expanded_folder, user_data);
	}
	if (selected_paths == NULL) {
		return NULL;
	}
	return lowest_common_parent (selected_paths, FALSE);
}

static const NemoPasteKey key_viewed = {
	N_("Paste into vie_wed folder"),
	N_("Paste into viewed folder"),
	N_("Move or copy the files last cut or copied into the folder being viewed, whatever is selected"),
};

static const NemoPasteKey key_into_folder = {
	N_("Paste into fol_der"),
	N_("Paste into folder"),
	N_("Move or copy the files last cut or copied into the selected folder"),
};

/* Returns: (transfer none) (nullable) */
const NemoPasteKey *
nemo_paste_target_key (NemoPasteTarget model)
{
	switch (model) {
	case NEMO_PASTE_TARGET_ANYWHERE:
		return &key_viewed;
	case NEMO_PASTE_TARGET_CLASSIC:
		return NULL;
	case NEMO_PASTE_TARGET_VIEWED_FOLDER:
		return &key_into_folder;
	}
	g_return_val_if_reached (&key_viewed);
}
