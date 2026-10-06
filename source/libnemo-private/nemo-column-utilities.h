/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */

/* nemo-column-utilities.h - Utilities related to column specifications

   Copyright (C) 2004 Novell, Inc.

   The Gnome Library is free software; you can redistribute it and/or
   modify it under the terms of the GNU Library General Public License as
   published by the Free Software Foundation; either version 2 of the
   License, or (at your option) any later version.

   The Gnome Library is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   Library General Public License for more details.

   You should have received a copy of the GNU Library General Public
   License along with the Gnome Library; see the column COPYING.LIB.  If not,
   write to the Free Software Foundation, Inc., 51 Franklin Street - Suite 500,
   Boston, MA 02110-1335, USA.

   Authors: Dave Camp <dave@ximian.com>
*/

#ifndef NEMO_COLUMN_UTILITIES_H
#define NEMO_COLUMN_UTILITIES_H

#include <libnemo-extension/nemo-column.h>
#include <libnemo-private/nemo-file.h>

/* Two type columns nobody outside a terminal wants to read: the raw mime type
 * ("application/vnd.oasis.opendocument.text") and the long-form description
 * beside the plain Type column that already says the same thing shorter. Off
 * here rather than deleted, because the attributes behind them are still what
 * the properties window and the sort menu use. Flip to 1 to offer them again. */
#define NEMO_COLUMNS_SHOW_MIME_TYPE		0
#define NEMO_COLUMNS_SHOW_DETAILED_TYPE		0

GList *nemo_get_all_columns       (void);
GList *nemo_get_common_columns    (void);
GList *nemo_get_columns_for_file (NemoFile *file);
GList *nemo_column_list_copy      (GList       *columns);
void   nemo_column_list_free      (GList       *columns);

GList *nemo_sort_columns          (GList       *columns,
				       char       **column_order);

/* The columns find results show, and their order. Kept always, since find
 * results are not a folder; remember-folder-settings has no say. Saving NULL
 * goes back to the defaults. */
char **nemo_search_columns_get_visible         (void);
char **nemo_search_columns_get_order           (void);
char **nemo_search_columns_get_default_visible (void);
char **nemo_search_columns_get_default_order   (void);
void   nemo_search_columns_save                (const char *const *visible_in_order);


#endif /* NEMO_COLUMN_UTILITIES_H */
