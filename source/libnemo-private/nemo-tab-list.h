/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */

/* nemo-tab-list.h - a list of tabs down the side of a stack, sub-tabs and all.

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

/* GtkStackSidebar, but a page with sub-tabs has them listed under it,
 * indented, at any depth. Every row on the way to the page showing gets the
 * selected look, so a parent and its showing sub-tab both do.
 *
 * A page has sub-tabs when it is itself a GtkStack, or when it holds one with
 * the NEMO_TAB_LIST_SUB_TABS style class under something shared by all of its
 * sub-tabs, such as a switch above them. */

#ifndef NEMO_TAB_LIST_H
#define NEMO_TAB_LIST_H

#include <gtk/gtk.h>

G_BEGIN_DECLS

#define NEMO_TAB_LIST_SUB_TABS "sub-tabs"

#define NEMO_TYPE_TAB_LIST (nemo_tab_list_get_type ())
G_DECLARE_FINAL_TYPE (NemoTabList, nemo_tab_list, NEMO, TAB_LIST, GtkBin)

/* Returns: (transfer floating) */
GtkWidget *nemo_tab_list_new       (void);
void       nemo_tab_list_set_stack (NemoTabList *list,
				    GtkStack    *stack);

/* Returns: (transfer none) */
GtkStack  *nemo_tab_list_get_stack (NemoTabList *list);

/* Shows a page, with every stack above it turned to it. */
void       nemo_tab_list_show_page (NemoTabList *list,
				    GtkWidget   *page);

/* Moves through the pages with nothing under them, in list order, wrapping
   round. Returns FALSE when there is nowhere to go. */
gboolean   nemo_tab_list_step      (NemoTabList *list,
				    int          step);

/* The label of the row for the innermost page holding widget, or NULL.
   Returns: (transfer none) */
GtkWidget *nemo_tab_list_get_label (NemoTabList *list,
				    GtkWidget   *widget);

G_END_DECLS

#endif /* NEMO_TAB_LIST_H */
