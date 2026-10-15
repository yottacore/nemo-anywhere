/* nemo-tab-move.h - moving a tab to another window, which is usually another process.
 *
 * Copyright © 2026 t00mietum (CryptogID: ปʬϝღถɔ4რఠΔթะ9ƾǝu)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; version 2 of the License.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 */

#ifndef NEMO_TAB_MOVE_H
#define NEMO_TAB_MOVE_H

#include <gtk/gtk.h>

#include "nemo-window.h"
#include "nemo-window-slot.h"

G_BEGIN_DECLS

/* What goes with a tab. History stays behind, as it does for a new window. */
typedef struct {
	char  *uri;
	char  *view_id;     /* NULL for whatever the folder would get anyway */
	char **selected;    /* URIs; NULL or empty for none */
} NemoTabState;

void nemo_tab_state_free (NemoTabState *state);

/* Opens the tab in the window as its current tab, reusing the empty one a
 * window starts with. A window already showing is raised, at event_time
 * unless that is 0. */
void nemo_window_take_tab (NemoWindow         *window,
                           const NemoTabState *state,
                           guint32             event_time);

/* Serves the list of this process's windows and the hand-over to other
 * copies. Before nemo_instances_start. */
void nemo_tab_move_serve (void);

/* "Move tab to", with a submenu of every other window and a new one. */
GtkWidget *nemo_tab_move_menu_item_new (NemoWindowSlot *slot);

/* The only tab of a window with one pane, which has nowhere new to go: a
 * window of its own would be this one again. */
gboolean nemo_tab_move_is_only_tab (NemoWindowSlot *slot);

/* A tab dragged off its tab bar and dropped outside it: onto another window
 * if there is one of ours under the pointer, otherwise a new one, unless it is
 * the only tab. */
void nemo_tab_move_tear_off (NemoWindowSlot *slot);

G_END_DECLS

#endif
