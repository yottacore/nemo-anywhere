/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*-

   nemo-metadata.h: #defines and other metadata-related info
 
   Copyright (C) 2000 Eazel, Inc.
  
   This program is free software; you can redistribute it and/or
   modify it under the terms of the GNU General Public License as
   published by the Free Software Foundation; either version 2 of the
   License, or (at your option) any later version.
  
   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   General Public License for more details.
  
   You should have received a copy of the GNU General Public
   License along with this program; if not, write to the
   Free Software Foundation, Inc., 51 Franklin Street - Suite 500,
   Boston, MA 02110-1335, USA.
  
   Author: John Sullivan <sullivan@eazel.com>
*/

#ifndef NEMO_METADATA_H
#define NEMO_METADATA_H

/* Keys for getting/setting Nemo metadata. All metadata used in Nemo
 * should define its key here, so we can keep track of the whole set easily.
 * Any updates here needs to be added in nemo-metadata.c too.
 */

#include <glib.h>

/* Per-file */

/* App-private view/layout state is prefixed with NEMO_APP_SLUG so it never
 * collides with upstream Nemo's metadata::nemo-* keys on the same files.
 * Keys shared with other file managers (custom-icon, emblems, annotation,
 * backgrounds, icon-scale) intentionally keep their common names. */

#define NEMO_METADATA_KEY_DEFAULT_VIEW		 	NEMO_APP_SLUG "-default-view"

#define NEMO_METADATA_KEY_LOCATION_BACKGROUND_COLOR 	"folder-background-color"
#define NEMO_METADATA_KEY_LOCATION_BACKGROUND_IMAGE 	"folder-background-image"

#define NEMO_METADATA_KEY_ICON_VIEW_ZOOM_LEVEL       	NEMO_APP_SLUG "-icon-view-zoom-level"
/* A folder keeps 2 icon sizes: one for when it is mostly pictures and one
 * for when it isn't. Which applies is decided per folder, so a child that
 * inherits both takes whichever matches what is in it. */
#define NEMO_METADATA_KEY_ICON_VIEW_IMAGE_ZOOM_LEVEL	NEMO_APP_SLUG "-icon-view-image-zoom-level"
#define NEMO_METADATA_KEY_ICON_VIEW_AUTO_LAYOUT      	NEMO_APP_SLUG "-icon-view-auto-layout"
#define NEMO_METADATA_KEY_ICON_VIEW_SORT_BY          	NEMO_APP_SLUG "-icon-view-sort-by"
#define NEMO_METADATA_KEY_ICON_VIEW_SORT_REVERSED    	NEMO_APP_SLUG "-icon-view-sort-reversed"
#define NEMO_METADATA_KEY_ICON_VIEW_KEEP_ALIGNED            NEMO_APP_SLUG "-icon-view-keep-aligned"
#define NEMO_METADATA_KEY_ICON_VIEW_LAYOUT_TIMESTAMP	NEMO_APP_SLUG "-icon-view-layout-timestamp"

#define NEMO_METADATA_KEY_LIST_VIEW_ZOOM_LEVEL       	NEMO_APP_SLUG "-list-view-zoom-level"
#define NEMO_METADATA_KEY_LIST_VIEW_SORT_COLUMN      	NEMO_APP_SLUG "-list-view-sort-column"
#define NEMO_METADATA_KEY_LIST_VIEW_SORT_REVERSED    	NEMO_APP_SLUG "-list-view-sort-reversed"
#define NEMO_METADATA_KEY_LIST_VIEW_VISIBLE_COLUMNS    	NEMO_APP_SLUG "-list-view-visible-columns"
#define NEMO_METADATA_KEY_LIST_VIEW_COLUMN_ORDER    	NEMO_APP_SLUG "-list-view-column-order"
#define NEMO_METADATA_KEY_LIST_VIEW_COLUMN_WIDTHS    	NEMO_APP_SLUG "-list-view-column-widths"

#define NEMO_METADATA_KEY_COMPACT_VIEW_ZOOM_LEVEL		NEMO_APP_SLUG "-compact-view-zoom-level"
#define NEMO_METADATA_KEY_COMPACT_VIEW_ALL_COLUMNS_SAME_WIDTH	NEMO_APP_SLUG "-compact-view-all-columns-same-width"
#define NEMO_METADATA_KEY_ICON_VIEW_LABELS_BESIDE_ICONS	NEMO_APP_SLUG "-icon-view-labels-beside-icons"
#define NEMO_METADATA_KEY_LIST_VIEW_ENABLE_EXPANSION	NEMO_APP_SLUG "-list-view-enable-expansion"
#define NEMO_METADATA_KEY_SORT_DIRECTORIES_FIRST	NEMO_APP_SLUG "-sort-directories-first"
#define NEMO_METADATA_KEY_SORT_FAVORITES_FIRST		NEMO_APP_SLUG "-sort-favorites-first"

#define NEMO_METADATA_KEY_WINDOW_GEOMETRY			NEMO_APP_SLUG "-window-geometry"
#define NEMO_METADATA_KEY_WINDOW_SCROLL_POSITION		NEMO_APP_SLUG "-window-scroll-position"
#define NEMO_METADATA_KEY_WINDOW_SHOW_HIDDEN_FILES		NEMO_APP_SLUG "-window-show-hidden-files"
#define NEMO_METADATA_KEY_WINDOW_MAXIMIZED			NEMO_APP_SLUG "-window-maximized"
#define NEMO_METADATA_KEY_WINDOW_STICKY			NEMO_APP_SLUG "-window-sticky"
#define NEMO_METADATA_KEY_WINDOW_KEEP_ABOVE			NEMO_APP_SLUG "-window-keep-above"

#define NEMO_METADATA_KEY_SIDEBAR_BACKGROUND_COLOR   	NEMO_APP_SLUG "-sidebar-background-color"
#define NEMO_METADATA_KEY_SIDEBAR_BACKGROUND_IMAGE   	NEMO_APP_SLUG "-sidebar-background-image"
#define NEMO_METADATA_KEY_SIDEBAR_BUTTONS			NEMO_APP_SLUG "-sidebar-buttons"

#define NEMO_METADATA_KEY_ANNOTATION                    "annotation"

#define NEMO_METADATA_KEY_ICON_POSITION              	NEMO_APP_SLUG "-icon-position"
#define NEMO_METADATA_KEY_ICON_POSITION_TIMESTAMP		NEMO_APP_SLUG "-icon-position-timestamp"
#define NEMO_METADATA_KEY_ICON_SCALE                 	"icon-scale"
#define NEMO_METADATA_KEY_CUSTOM_ICON                	"custom-icon"
#define NEMO_METADATA_KEY_CUSTOM_ICON_NAME                	"custom-icon-name"
#define NEMO_METADATA_KEY_EMBLEMS				"emblems"

#define NEMO_METADATA_KEY_MONITOR               "monitor"
#define NEMO_METADATA_KEY_DESKTOP_GRID_HORIZONTAL  "desktop-horizontal"
#define NEMO_METADATA_KEY_SHOW_THUMBNAILS NEMO_APP_SLUG "-show-thumbnails"
#define NEMO_METADATA_KEY_DESKTOP_GRID_ADJUST      "desktop-grid-adjust"

#define NEMO_METADATA_KEY_PINNED                   NEMO_APP_SLUG "-pinned-to-top"
#define NEMO_METADATA_KEY_FAVORITE                 NEMO_APP_SLUG "-favorite"
#define NEMO_METADATA_KEY_FAVORITE_AVAILABLE     NEMO_APP_SLUG "-favorite-available"

/* Marks a folder as having its own view settings, so a set whose values all
 * match the default still counts as saved and stops inheritance there. */
#define NEMO_METADATA_KEY_FOLDER_SETTINGS_SAVED  NEMO_APP_SLUG "-folder-settings-saved"

guint nemo_metadata_get_id (const char *metadata);

#endif /* NEMO_METADATA_H */
