/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */

/* fm-list-view.c - implementation of list view of directory.

   Copyright (C) 2000 Eazel, Inc.
   Copyright (C) 2001, 2002 Anders Carlsson <andersca@gnu.org>

   The Gnome Library is free software; you can redistribute it and/or
   modify it under the terms of the GNU Library General Public License as
   published by the Free Software Foundation; either version 2 of the
   License, or (at your option) any later version.

   The Gnome Library is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   Library General Public License for more details.

   You should have received a copy of the GNU Library General Public
   License along with the Gnome Library; see the file COPYING.LIB.  If not,
   write to the Free Software Foundation, Inc., 51 Franklin Street - Suite 500,
   Boston, MA 02110-1335, USA.

   Authors: John Sullivan <sullivan@eazel.com>
            Anders Carlsson <andersca@gnu.org>
	    David Emory Watson <dwatson@cs.ucr.edu>
*/

#include <config.h>
#include "nemo-list-view.h"

#include "nemo-application.h"
#include "nemo-list-model.h"

#ifdef G_OS_WIN32
#include <libnemo-private/nemo-dnd-win32.h>
#endif
#include "nemo-error-reporting.h"
#include "nemo-view-dnd.h"
#include "nemo-view-factory.h"
#include "nemo-window.h"

#include <string.h>
#include <eel/eel-vfs-extensions.h>
#include <eel/eel-gdk-extensions.h>
#include <eel/eel-gtk-extensions.h>
#include <eel/eel-glib-extensions.h>
#include <gdk/gdk.h>
#include <gdk/gdkkeysyms.h>
#include <gtk/gtk.h>
#include <libegg/eggtreemultidnd.h>
#include <glib/gi18n.h>
#include <glib-object.h>
#include <libnemo-extension/nemo-column-provider.h>
#include <libnemo-private/nemo-clipboard-monitor.h>
#include <libnemo-private/nemo-column-chooser.h>
#include <libnemo-private/nemo-column-utilities.h>
#include <libnemo-private/nemo-dnd.h>
#include <libnemo-private/nemo-file-dnd.h>
#include <libnemo-private/nemo-file-utilities.h>
#include <libnemo-private/nemo-ui-utilities.h>
#include <libnemo-private/nemo-global-preferences.h>
#include <libnemo-private/nemo-icon-dnd.h>
#include <libnemo-private/nemo-metadata.h>
#include <libnemo-private/nemo-folder-settings.h>
#include <libnemo-private/nemo-module.h>
#include <libnemo-private/nemo-search-directory.h>
#include <libnemo-private/nemo-thumbnails.h>
#include <libnemo-private/nemo-tree-view-drag-dest.h>
#include <libnemo-private/nemo-clipboard.h>
#include <libnemo-private/nemo-row-hover.h>

#include "nemo-column-layout.h"

#define DEBUG_FLAG NEMO_DEBUG_LIST_VIEW
#include <libnemo-private/nemo-debug.h>

struct NemoListViewDetails {
	GtkTreeView *tree_view;
	NemoListModel *model;
	GtkActionGroup *list_action_group;
	guint list_merge_id;

	GtkTreeViewColumn   *file_name_column;
	int file_name_column_num;

	GtkCellRendererPixbuf *pixbuf_cell;
	GtkCellRendererText   *file_name_cell;
	GList *cells;
	GtkCellEditable *editable_widget;

	gint icon_size;

	NemoTreeViewDragDest *drag_dest;

	GtkTreePath *double_click_path[2]; /* Both clicks in a double click need to be on the same row */

	GtkTreePath *new_selection_path;   /* Path of the new selection after removing a file */

	GtkTreePath *hover_path;

	guint drag_button;
	int drag_x;
	int drag_y;

    gint ok_to_load_deferred_attrs;
    guint update_visible_icons_id;

    gboolean rename_on_release;
	gboolean drag_started;
	gboolean ignore_button_release;
	gboolean row_selected_on_button_down;
	gboolean menus_ready;
	gboolean active;

    gboolean rubber_banding;

	/* Escape leaves the cursor parked on the first row with its outline hidden,
	   since GTK has no way to take the cursor away. */
	gboolean cursor_forgotten;

	GHashTable *columns;
	GtkWidget *column_editor;

	/* Column auto-sizing. samples holds, per column id, what has been seen in
	 * it - grown a row at a time as the model fills in, walked whole only in
	 * the case under resample_rows_cb, and thrown away when the folder
	 * changes. laid_out_width is the width the current widths were worked
	 * out for, so a column dragged wider by hand survives until something
	 * really changes. */
	GHashTable *samples;
	guint resize_columns_id;
	guint resample_id;
	gboolean samples_stale;
	gint column_floor;
	gint column_pad;
	gint column_ellipsis;

	/* Theme sizes the measuring needs. Read once and dropped on a style
	   change, since a style_get is not cheap and this runs per row. */
	gboolean style_metrics_valid;
	gint column_separator_px;
	gint name_indent_base;
	gint name_indent_step;

	/* Everything above plus the font, as one string. A style change that
	   leaves this alone cannot have changed any width we measured. */
	char *measure_style_id;

	/* The columns, kept rather than copied again for every row measured.
	   Dropped whenever the tree view says the set has changed. */
	GList *measure_columns;

	gint laid_out_width;
	gint own_width;		/* our last allocation, and what the tree view got out of it - */
	gint tree_inset;	/* so the next one can be laid out before the tree view sees it */

	/* Hand-dragged widths. applying_layout marks our own set_fixed_width
	 * calls so the notify handler can tell a drag from a relayout;
	 * pending_user_widths holds what the drag left behind, per column,
	 * until it settles. user_widths holds the settled ones for the folder
	 * in view, and is emptied when the folder changes. */
	gboolean applying_layout;
	guint user_width_settle_id;
	GHashTable *pending_user_widths;
	GHashTable *user_widths;

	char *original_name;

	NemoFile *renaming_file;
	gboolean rename_done;
	guint renaming_file_activate_timeout;

	gulong clipboard_handler_id;

	GQuark last_sort_attr;

    gboolean tooltip_flags;
    gboolean show_tooltips;

    gboolean click_to_rename;

    GList *current_selection;
    gint current_selection_count;

    gboolean overlay_scrolling;

    /* Every other row is tinted with this when row_shading is on. */
    gboolean row_shading;
    GdkRGBA row_shading_color;

    /* Parity belongs to the row but is asked for once per cell, so the first
       cell works it out and the rest of that row reuse it. shade_pass moves
       whenever the rows might have shifted under us, which retires the answer. */
    guint shade_pass;
    guint shade_node_pass;
    gpointer shade_node;
    gboolean shade_tint;

    /* Where the search being shown started from. Group row labels are spelled
     * relative to it. Worked out on the first result and dropped on clear. */
    GFile *search_root;
};

struct SelectionForeachData {
	GList *list;
	GtkTreeSelection *selection;
};

/*
 * The row height should be large enough to not clip emblems.
 * Computing this would be costly, so we just choose a number
 * that works well with the set of emblems we've designed.
 */
#define LIST_VIEW_MINIMUM_ROW_HEIGHT	28

/* Air between the window edge and the first and last columns, so a name does not
   start hard against the frame. Device-independent pixels: GTK multiplies these
   by the display scale, so the gap looks the same at any resolution. */
#define LIST_VIEW_EDGE_PADDING 8

/* We wait two seconds after row is collapsed to unload the subdirectory */
#define COLLAPSE_TO_UNLOAD_DELAY 2

/* Wait for the rename to end when activating a file being renamed */
#define WAIT_FOR_RENAME_ON_ACTIVATE 200

#define INITIAL_UPDATE_VISIBLE_DELAY 300
#define NORMAL_UPDATE_VISIBLE_DELAY 50

static GdkCursor *              hand_cursor = NULL;

static GtkTargetList *          source_target_list = NULL;

static void   resize_columns_soon                            (NemoListView *view);
static void   forget_samples                                 (NemoListView *view);
static void   remeasure_rows                                 (NemoListView *view);
static void   style_metrics                                  (NemoListView *view);
static GList *nemo_list_view_get_selection                   (NemoView   *view);
static void   nemo_list_view_update_selection                (NemoView *view);
static GList *nemo_list_view_get_selection_for_file_transfer (NemoView   *view);
static void   nemo_list_view_set_icon_size                   (NemoListView        *view,
								  gint               new_size,
								  gboolean           always_emit);
static void   nemo_list_view_scale_font_size                 (NemoListView        *view,
								  gint               new_size);
static void   nemo_list_view_scroll_to_file                  (NemoListView        *view,
								  NemoFile      *file);
static void   nemo_list_view_rename_callback                 (NemoFile      *file,
								  GFile             *result_location,
								  GError            *error,
								  gpointer           callback_data);

static void nemo_list_view_start_renaming_file               (NemoView *view,
                                                              NemoFile *file,
                                                              gboolean  select_all);

static void   apply_columns_settings                             (NemoListView *list_view,
                                                                  char **column_order,
                                                                  char **visible_columns);
static char **get_visible_columns                                (NemoListView *list_view);
static char **get_default_visible_columns                        (NemoListView *list_view);
static char **get_column_order                                   (NemoListView *list_view);
static char **get_default_column_order                           (NemoListView *list_view);

static void   set_columns_settings_from_metadata_and_preferences (NemoListView *list_view);
static void   queue_update_visible_icons (NemoListView *view, gint delay);
static gint   nemo_list_view_get_icon_size (NemoView *view);
static void   mark_visible_files (NemoListView *view);

G_DEFINE_TYPE (NemoListView, nemo_list_view, NEMO_TYPE_VIEW);

static gint click_policy = NEMO_CLICK_POLICY_SINGLE;

static const char * default_trash_visible_columns[] = {
	"name", "size", "type", "trashed_on", "trash_orig_path", NULL
};

static const char * default_trash_columns_order[] = {
	"name", "size", "type", "trashed_on", "trash_orig_path", NULL
};

static const char * default_recent_visible_columns[] = {
    "name", "size", "type", "date_accessed", NULL
};

static const char * default_recent_columns_order[] = {
    "name", "size", "type", "date_accessed", NULL
};

static const char * default_favorites_visible_columns[] = {
    "name", "size", "date_modified", NULL
};

static const char * default_favorites_columns_order[] = {
    "name", "size", "date_modified", NULL
};

/* Just the two: Name and Location share the row. Anything else is the user's
   own addition, saved under search-visible-columns. */
static const char * default_search_columns[] = {
    "name", "where", NULL
};

/* Search results are either one flat list with a Location column, or one row per
   folder holding a match with the matches nested under it. The grouped rows are
   built here rather than by the model's own subfolder machinery: nobody opened
   those folders, so they must not be monitored or read. */

static gboolean
grouping_search_results (NemoListView *view)
{
	NemoFile *dir_file;

	dir_file = nemo_view_get_directory_as_file (NEMO_VIEW (view));

	return dir_file != NULL &&
	       nemo_file_is_in_search (dir_file) &&
	       nemo_config_get_boolean (nemo_search_preferences,
					NEMO_PREFERENCES_SEARCH_GROUP_BY_FOLDER);
}

static GFile *
search_root (NemoListView *view)
{
	NemoDirectory *directory;
	NemoQuery *query;
	char *uri;

	if (view->details->search_root != NULL) {
		return view->details->search_root;
	}

	directory = nemo_view_get_model (NEMO_VIEW (view));
	if (!NEMO_IS_SEARCH_DIRECTORY (directory)) {
		return NULL;
	}

	query = nemo_search_directory_get_query (NEMO_SEARCH_DIRECTORY (directory));
	if (query == NULL) {
		return NULL;
	}

	uri = nemo_query_get_location (query);
	if (uri != NULL) {
		view->details->search_root = g_file_new_for_uri (uri);
		g_free (uri);
	}

	g_object_unref (query);

	return view->details->search_root;
}

static char *
search_group_label (GFile *root, GFile *folder)
{
	char *relative;

	relative = g_file_get_relative_path (root, folder);
	if (relative != NULL) {
		return relative;
	}

	if (g_file_equal (root, folder)) {
		return g_file_get_basename (folder);
	}

	/* A match from outside the folder searched. Nothing to be relative to. */
	return g_file_get_parse_name (folder);
}

static void
expand_search_group (NemoListView *view, NemoFile *dir_file)
{
	GtkTreeIter iter;
	GtkTreePath *path;

	if (!nemo_list_model_get_tree_iter_from_file (view->details->model, dir_file,
						      NULL, &iter)) {
		return;
	}

	path = gtk_tree_model_get_path (GTK_TREE_MODEL (view->details->model), &iter);
	gtk_tree_view_expand_row (view->details->tree_view, path, FALSE);
	gtk_tree_path_free (path);
}

/* The group row @file belongs under, made if it is not there yet. Returns a ref,
   or NULL when results are not being grouped. */
static NemoDirectory *
search_group_for_file (NemoListView *view, NemoFile *file, gboolean create)
{
	NemoDirectory *group;
	NemoFile *parent_file;
	GFile *location, *root;
	char *label;
	gboolean created;

	if (!grouping_search_results (view)) {
		return NULL;
	}

	root = search_root (view);
	if (root == NULL) {
		return NULL;
	}

	location = nemo_file_get_parent_location (file);
	if (location == NULL) {
		return NULL;
	}

	if (!create) {
		group = nemo_directory_get (location);
		g_object_unref (location);
		return group;
	}

	parent_file = nemo_file_get (location);
	label = search_group_label (root, location);

	group = nemo_list_model_add_search_group (view->details->model, parent_file,
						  label, &created);
	if (group != NULL) {
		nemo_directory_ref (group);
		if (created) {
			expand_search_group (view, parent_file);
		}
	}

	g_free (label);
	nemo_file_unref (parent_file);
	g_object_unref (location);

	return group;
}

static gchar **
string_array_from_string_glist (GList *list)
{
    GPtrArray *res;
    GList *l;
    gchar **ret;

    res = g_ptr_array_new ();

    for (l = list; l != NULL; l = l->next) {
        g_ptr_array_add (res, g_strdup (l->data));
    }

    g_ptr_array_add (res, NULL);

    ret = (char **) g_ptr_array_free (res, FALSE);

    return ret;
}

static const gchar*
get_default_sort_order (NemoFile *file, gboolean *reversed)
{
	NemoFileSortType default_sort_order;
	gboolean default_sort_reversed;
	const gchar *retval;
	const char *attributes[] = {
		"name", /* is really "manually" which doesn't apply to lists */
		"name",
		"size",
		"type",
		"detailed_type",
		"date_modified",
		"date_accessed",
		"trashed_on",
		NULL
	};

	retval = nemo_file_get_default_sort_attribute (file, reversed);

	if (retval == NULL) {
		default_sort_order = nemo_config_get_enum (nemo_preferences,
							  NEMO_PREFERENCES_DEFAULT_SORT_ORDER);
		default_sort_reversed = nemo_config_get_boolean (nemo_preferences,
								NEMO_PREFERENCES_DEFAULT_SORT_IN_REVERSE_ORDER);

		retval = attributes[default_sort_order];
		*reversed = default_sort_reversed;
	}

	return retval;
}

static void
tooltip_prefs_changed_callback (NemoListView *view)
{
    view->details->show_tooltips = nemo_config_get_boolean (nemo_preferences,
                                                           NEMO_PREFERENCES_TOOLTIPS_LIST_VIEW);

    view->details->tooltip_flags = nemo_global_preferences_get_tooltip_flags ();
}

/* The tree view's own flag is what the rest of the view reads, so a folder
 * with expanders saved differently from the preference behaves the same way. */
static gboolean
expanders_enabled (NemoListView *view)
{
    return gtk_tree_view_get_show_expanders (view->details->tree_view);
}

static void
expanders_enabled_changed_cb (NemoListView *view)
{
    gboolean enabled;

    g_return_if_fail (NEMO_IS_LIST_VIEW (view));
    g_return_if_fail (GTK_IS_TREE_VIEW (view->details->tree_view) && view->details->tree_view != NULL);

    enabled = nemo_folder_settings_get_boolean (nemo_view_get_directory_as_file (NEMO_VIEW (view)),
                                                NEMO_METADATA_KEY_LIST_VIEW_ENABLE_EXPANSION,
                                                nemo_config_get_boolean (nemo_list_view_preferences,
                                                                         NEMO_PREFERENCES_LIST_VIEW_ENABLE_EXPANSION));

    if (enabled != expanders_enabled (view)) {
        gtk_tree_view_collapse_all (view->details->tree_view);
        gtk_tree_view_set_show_expanders (view->details->tree_view, enabled);
    }
}

static void
remember_cursor (NemoListView *view)
{
	if (!view->details->cursor_forgotten) {
		return;
	}

	view->details->cursor_forgotten = FALSE;
	gtk_style_context_remove_class (gtk_widget_get_style_context (GTK_WIDGET (view->details->tree_view)),
					"nemo-cursor-forgotten");
}

/* Escape. Nothing is selected and the cursor starts over at the top, as in a
   folder just opened, with nothing on screen to say where it is. */
static void
forget_cursor (NemoListView *view)
{
	GtkTreeView *tree_view = view->details->tree_view;

	eel_gtk_tree_view_forget_cursor (tree_view);

	view->details->cursor_forgotten = TRUE;
	gtk_style_context_add_class (gtk_widget_get_style_context (GTK_WIDGET (tree_view)),
				     "nemo-cursor-forgotten");
}

/* The first up or down key after Escape goes to the top row rather than the one
   below it. With Ctrl it only shows the cursor there, as Ctrl moves it anyway. */
static gboolean
start_cursor_over (NemoListView *view, GdkEventKey *event)
{
	GtkTreePath *path;

	switch (event->keyval) {
	case GDK_KEY_Up:
	case GDK_KEY_KP_Up:
	case GDK_KEY_Down:
	case GDK_KEY_KP_Down:
	case GDK_KEY_Page_Up:
	case GDK_KEY_KP_Page_Up:
	case GDK_KEY_Page_Down:
	case GDK_KEY_KP_Page_Down:
	case GDK_KEY_Home:
	case GDK_KEY_KP_Home:
		break;
	default:
		return FALSE;
	}

	if ((event->state & GDK_CONTROL_MASK) != 0) {
		remember_cursor (view);
	} else {
		path = gtk_tree_path_new_first ();
		gtk_tree_view_set_cursor (view->details->tree_view, path, NULL, FALSE);
		gtk_tree_path_free (path);
	}

	return TRUE;
}

static void
cursor_changed_callback (G_GNUC_UNUSED GtkTreeView *tree_view, gpointer user_data)
{
	remember_cursor (NEMO_LIST_VIEW (user_data));
}

static void
list_selection_changed_callback (GtkTreeSelection *selection, gpointer user_data)
{
	NemoView *view;

	view = NEMO_VIEW (user_data);

	/* Ctrl+A or a rubber band picks rows without moving the cursor. */
	if (NEMO_LIST_VIEW (view)->details->cursor_forgotten &&
	    gtk_tree_selection_count_selected_rows (selection) > 0) {
		remember_cursor (NEMO_LIST_VIEW (view));
	}

	nemo_view_notify_selection_changed (view);
}

/* Move these to eel? */

static void
tree_selection_foreach_set_boolean (G_GNUC_UNUSED GtkTreeModel *model,
				    G_GNUC_UNUSED GtkTreePath *path,
				    G_GNUC_UNUSED GtkTreeIter *iter,
				    gpointer callback_data)
{
	* (gboolean *) callback_data = TRUE;
}

static gboolean
tree_selection_not_empty (GtkTreeSelection *selection)
{
	gboolean not_empty;

	not_empty = FALSE;
	gtk_tree_selection_selected_foreach (selection,
					     tree_selection_foreach_set_boolean,
					     &not_empty);
	return not_empty;
}

static gboolean
tree_view_has_selection (GtkTreeView *view)
{
	return tree_selection_not_empty (gtk_tree_view_get_selection (view));
}

static void
preview_selected_items (NemoListView *view)
{
	GList *file_list;

	file_list = nemo_list_view_get_selection (NEMO_VIEW (view));

	if (file_list != NULL) {
		nemo_view_preview_files (NEMO_VIEW (view),
					     file_list, NULL);
		nemo_file_list_free (file_list);
	}
}

static void activate_selected_items (NemoListView *view);

/* Proper GSourceFunc: activate_selected_items is void, so casting it to a
 * source func left the timeout's repeat behavior undefined. Clear the id and
 * fire once; if still renaming, activate_selected_items re-arms a fresh one. */
static gboolean
activate_selected_items_timeout (gpointer data)
{
	NemoListView *view = data;

	view->details->renaming_file_activate_timeout = 0;
	activate_selected_items (view);
	return G_SOURCE_REMOVE;
}

static void
activate_selected_items (NemoListView *view)
{
	GList *file_list;

	file_list = nemo_list_view_get_selection (NEMO_VIEW (view));


	if (view->details->renaming_file) {
		/* We're currently renaming a file, wait until the rename is
		   finished, or the activation uri will be wrong */
		if (view->details->renaming_file_activate_timeout == 0) {
			view->details->renaming_file_activate_timeout =
				g_timeout_add (WAIT_FOR_RENAME_ON_ACTIVATE, activate_selected_items_timeout, view);
		}
		nemo_file_list_free (file_list);
		return;
	}

	if (view->details->renaming_file_activate_timeout != 0) {
		g_source_remove (view->details->renaming_file_activate_timeout);
		view->details->renaming_file_activate_timeout = 0;
	}

	nemo_view_activate_files (NEMO_VIEW (view),
				      file_list,
				      0, TRUE);
	nemo_file_list_free (file_list);

}

static void
activate_selected_items_alternate (NemoListView *view,
				   NemoFile *file,
				   gboolean open_in_tab)
{
	GList *file_list;
	NemoWindowOpenFlags flags;

	flags = 0;

	if (nemo_config_get_boolean (nemo_preferences,
				    NEMO_PREFERENCES_ALWAYS_USE_BROWSER)) {
		if (open_in_tab) {
			flags |= NEMO_WINDOW_OPEN_FLAG_NEW_TAB;
		} else {
			flags |= NEMO_WINDOW_OPEN_FLAG_NEW_WINDOW;
		}
	} else {
		flags |= NEMO_WINDOW_OPEN_FLAG_CLOSE_BEHIND;
	}

	if (file != NULL) {
		nemo_file_ref (file);
		file_list = g_list_prepend (NULL, file);
	} else {
		file_list = nemo_list_view_get_selection (NEMO_VIEW (view));
	}
	nemo_view_activate_files (NEMO_VIEW (view),
				      file_list,
				      flags,
				      TRUE);
	nemo_file_list_free (file_list);

}

static gboolean
button_event_modifies_selection (GdkEventButton *event)
{
	return (event->state & (eel_gtk_primary_mask (event->window) | GDK_SHIFT_MASK)) != 0;
}

static void
nemo_list_view_did_not_drag (NemoListView *view,
				 GdkEventButton *event)
{
	GtkTreeView *tree_view;
	GtkTreeSelection *selection;
	GtkTreePath *path;

	tree_view = view->details->tree_view;
	selection = gtk_tree_view_get_selection (tree_view);

	if (gtk_tree_view_get_path_at_pos (tree_view, event->x, event->y,
					   &path, NULL, NULL, NULL)) {
		if ((event->button == 1 || event->button == 2)
		    && ((event->state & eel_gtk_primary_mask (event->window)) != 0 ||
			(event->state & GDK_SHIFT_MASK) == 0)
		    && view->details->row_selected_on_button_down) {
			if (!button_event_modifies_selection (event)) {
				gtk_tree_selection_unselect_all (selection);
				gtk_tree_selection_select_path (selection, path);
			} else {
				gtk_tree_selection_unselect_path (selection, path);
			}
		}

		if ((click_policy == NEMO_CLICK_POLICY_SINGLE)
		    && !button_event_modifies_selection(event)) {
			if (event->button == 1) {
				activate_selected_items (view);
			} else if (event->button == 2) {
				activate_selected_items_alternate (view, NULL, TRUE);
			}
		}

        if (view->details->rename_on_release) {
            NemoFile *file = nemo_list_model_file_for_path (view->details->model, path);
            nemo_list_view_start_renaming_file (NEMO_VIEW (view),
                                                file,
                                                nemo_file_is_directory (file));
            nemo_file_unref (file);
            view->details->rename_on_release = FALSE;
        }

		gtk_tree_path_free (path);
	}

}

static void
drag_data_get_callback (GtkWidget *widget,
			GdkDragContext *context,
			GtkSelectionData *selection_data,
			G_GNUC_UNUSED guint info,
			G_GNUC_UNUSED guint time)
{
	GtkTreeView *tree_view;
	GtkTreeModel *model;
	GList *ref_list;

	tree_view = GTK_TREE_VIEW (widget);

	model = gtk_tree_view_get_model (tree_view);

	if (model == NULL) {
		return;
	}

	ref_list = g_object_get_data (G_OBJECT (context), "drag-info");

	if (ref_list == NULL) {
		return;
	}

	if (EGG_IS_TREE_MULTI_DRAG_SOURCE (model)) {
		egg_tree_multi_drag_source_drag_data_get (EGG_TREE_MULTI_DRAG_SOURCE (model),
							  ref_list,
							  selection_data);
	}
}

static void
filtered_selection_foreach (GtkTreeModel *model,
			    GtkTreePath *path,
			    GtkTreeIter *iter,
			    gpointer data)
{
	struct SelectionForeachData *selection_data;
	GtkTreeIter parent;
	GtkTreeIter child;

	selection_data = data;

	/* If the parent folder is also selected, don't include this file in the
	 * file operation, since that would copy it to the toplevel target instead
	 * of keeping it as a child of the copied folder
	 */
	child = *iter;
	while (gtk_tree_model_iter_parent (model, &parent, &child)) {
		if (gtk_tree_selection_iter_is_selected (selection_data->selection,
							 &parent)) {
			return;
		}
		child = parent;
	}

	selection_data->list = g_list_prepend (selection_data->list,
					       gtk_tree_row_reference_new (model, path));
}

static GList *
get_filtered_selection_refs (GtkTreeView *tree_view)
{
	struct SelectionForeachData selection_data;

	selection_data.list = NULL;
	selection_data.selection = gtk_tree_view_get_selection (tree_view);

	gtk_tree_selection_selected_foreach (selection_data.selection,
					     filtered_selection_foreach,
					     &selection_data);
	return g_list_reverse (selection_data.list);
}

static void
ref_list_free (GList *ref_list)
{
	g_list_free_full (ref_list, (GDestroyNotify) gtk_tree_row_reference_free);
}

static void
stop_drag_check (NemoListView *view)
{
	view->details->drag_button = 0;
}

static cairo_surface_t *
get_drag_surface (NemoListView *view)
{
	GtkTreeModel *model;
	GtkTreePath *path;
	GtkTreeIter iter;
	cairo_surface_t *ret;
	GdkRectangle cell_area;

	ret = NULL;

	if (gtk_tree_view_get_path_at_pos (view->details->tree_view,
					   view->details->drag_x,
					   view->details->drag_y,
					   &path, NULL, NULL, NULL)) {
		model = gtk_tree_view_get_model (view->details->tree_view);
		gtk_tree_model_get_iter (model, &iter, path);
		gtk_tree_model_get (model, &iter,
				    nemo_list_model_get_column_id_for_icon_size (view->details->icon_size),
				    &ret,
				    -1);

		gtk_tree_view_get_cell_area (view->details->tree_view,
					     path,
					     view->details->file_name_column,
					     &cell_area);

		gtk_tree_path_free (path);
	}

	return ret;
}

static void
drag_begin_callback (GtkWidget *widget,
		     GdkDragContext *context,
		     NemoListView *view)
{
	GList *ref_list;

    cairo_surface_t *surface;

    surface = get_drag_surface (view);
    if (surface) {
        gtk_drag_set_icon_surface (context, surface);
        cairo_surface_destroy (surface);
	} else {
		gtk_drag_set_icon_default (context);
	}

	stop_drag_check (view);
	view->details->drag_started = TRUE;

	ref_list = get_filtered_selection_refs (GTK_TREE_VIEW (widget));
	g_object_set_data_full (G_OBJECT (context),
				"drag-info",
				ref_list,
				(GDestroyNotify)ref_list_free);
}

static void
drag_end_callback (G_GNUC_UNUSED GtkWidget *widget,
             G_GNUC_UNUSED GdkDragContext *context,
             NemoListView *view)
{
    view->details->drag_started = FALSE;
}

#ifdef G_OS_WIN32
/* Hand the drag to Windows rather than the toolkit, so a drop on another program
 * reaches it. Runs the whole drag, so what drag-begin and drag-end would have
 * done sits either side of it. */
static gboolean
win32_drag (NemoListView *view,
	    GtkWidget    *widget)
{
	GList *ref_list;
	NemoListModel *model;
	char *uri_list, *icon_list;
	cairo_surface_t *surface;
	gboolean dragged;

	if (!nemo_dnd_win32_enabled ()) {
		return FALSE;
	}

	ref_list = get_filtered_selection_refs (GTK_TREE_VIEW (widget));
	if (ref_list == NULL) {
		return FALSE;
	}

	model = NEMO_LIST_MODEL (view->details->model);

	uri_list = nemo_list_model_drag_payload (model, ref_list, NEMO_ICON_DND_URI_LIST);
	if (uri_list == NULL) {
		ref_list_free (ref_list);
		return FALSE;
	}

	icon_list = nemo_list_model_drag_payload (model, ref_list,
						  NEMO_ICON_DND_GNOME_ICON_LIST);
	surface = get_drag_surface (view);

	stop_drag_check (view);
	view->details->drag_started = TRUE;

	dragged = nemo_dnd_win32_drag (widget,
				       GDK_ACTION_MOVE | GDK_ACTION_COPY | GDK_ACTION_LINK,
				       uri_list, icon_list, surface, 0, 0, NULL);

	view->details->drag_started = FALSE;
	view->details->rubber_banding = FALSE;

	if (surface != NULL) {
		cairo_surface_destroy (surface);
	}
	g_free (uri_list);
	g_free (icon_list);
	ref_list_free (ref_list);

	return dragged;
}
#endif

static gboolean
motion_notify_callback (GtkWidget *widget,
			GdkEventMotion *event,
			gpointer callback_data)
{
	NemoListView *view;

	view = NEMO_LIST_VIEW (callback_data);

    if (event->window != gtk_tree_view_get_bin_window (GTK_TREE_VIEW (widget))) {
        return GDK_EVENT_PROPAGATE;
    }

	if (click_policy == NEMO_CLICK_POLICY_SINGLE) {
		GtkTreePath *old_hover_path;

		old_hover_path = view->details->hover_path;
		gtk_tree_view_get_path_at_pos (GTK_TREE_VIEW (widget),
					       event->x, event->y,
					       &view->details->hover_path,
					       NULL, NULL, NULL);

		if ((old_hover_path != NULL) != (view->details->hover_path != NULL)) {
			if (view->details->hover_path != NULL) {
				gdk_window_set_cursor (gtk_widget_get_window (widget), hand_cursor);
			} else {
				gdk_window_set_cursor (gtk_widget_get_window (widget), NULL);
			}
		}

		if (old_hover_path != NULL) {
			gtk_tree_path_free (old_hover_path);
		}
	}

    /* If we're already rubber-banding, we can skip all of this logic and just let the parent
     * class continue to handle selection */
    if (view->details->drag_button != 0 && !view->details->rubber_banding) {
        GtkTreePath *path;
        GtkTreeSelection *selection;
        gboolean is_new_self_selection;

        selection = gtk_tree_view_get_selection (GTK_TREE_VIEW (widget));

        gtk_tree_view_get_path_at_pos (GTK_TREE_VIEW (widget),
                                       view->details->drag_x,
                                       view->details->drag_y,
                                       &path,
                                       NULL, NULL, NULL);

        /* This looks complicated but it's just verbose:  We'll only consider allowing rubber-banding
         * to begin if the following are TRUE: a) The current row is the only row currently selected,
         * and  b) This is the first click that's been made on this row - meaning, the button-press-event
         * that preceded this motion-event was the one that caused this row to be selected. */
        is_new_self_selection = gtk_tree_selection_count_selected_rows (selection) == 1 &&
                                gtk_tree_selection_path_is_selected (selection, path) &&
                                (!view->details->double_click_path[1] ||
                                (view->details->double_click_path[1] &&
                                gtk_tree_path_compare (view->details->double_click_path[0],
                                                       view->details->double_click_path[1]) != 0));

        gtk_tree_path_free (path);

        /* We also want to further restrict rubber-banding to be initiated only in blank areas of the row.
         * This allows DnD to operate on a new selection like before, when the motion begins over text or
         * icons */
        if (is_new_self_selection && gtk_tree_view_is_blank_at_pos (GTK_TREE_VIEW (widget),
                                                                    view->details->drag_x,
                                                                    view->details->drag_y,
                                                                    NULL, NULL, NULL, NULL)) {
            /* If this is a candidate for rubber-banding, track that state in the view, and allow the event
             * to continue into Gtk (which handles rubber-band selection for us) */
            view->details->rubber_banding = TRUE;

            return GDK_EVENT_PROPAGATE;
        }

        /* All other cases, allow DnD to potentially begin */
        if (!source_target_list) {
            source_target_list = nemo_list_model_get_drag_target_list ();
        }

        if (gtk_drag_check_threshold (widget,
                                      view->details->drag_x,
                                      view->details->drag_y,
                                      event->x,
                                      event->y)) {
#ifdef G_OS_WIN32
            if (win32_drag (view, widget)) {
                return GDK_EVENT_STOP;
            }
#endif
            gtk_drag_begin (widget,
                            source_target_list,
                            GDK_ACTION_MOVE | GDK_ACTION_COPY | GDK_ACTION_LINK | GDK_ACTION_ASK,
                            view->details->drag_button,
                            (GdkEvent*) event);
        }

        /* The event is handled by the DnD begin, don't propagate further */
        return GDK_EVENT_STOP;
    }

    return GDK_EVENT_PROPAGATE;
}

static gboolean
query_tooltip_callback (GtkWidget *widget,
                        gint x,
                        gint y,
                        gboolean kb_mode,
                        GtkTooltip *tooltip,
                        gpointer user_data)
{
    NemoListView *list_view;
    GtkTreeModel *model;
    GtkTreeViewColumn *column;
    GtkTreePath *path;
    GtkTreeIter iter;
    gchar *clipped;
    gboolean ret;

    list_view = NEMO_LIST_VIEW (user_data);
    model = GTK_TREE_MODEL (list_view->details->model);
    column = NULL;
    path = NULL;
    clipped = NULL;
    ret = FALSE;

    if (!gtk_tree_view_get_tooltip_context (GTK_TREE_VIEW (widget), &x, &y,
                                           kb_mode, &model, &path, &iter)) {
        return FALSE;
    }

    /* A value the column has cut off is worth showing whatever the item tooltip
       preference says - that preference is about the file, this is about reading
       what is already on screen. Keyboard mode has no column under a pointer. */
    if (!kb_mode) {
        gtk_tree_view_get_path_at_pos (GTK_TREE_VIEW (widget), x, y,
                                       NULL, &column, NULL, NULL);
    }
    if (column != NULL) {
        clipped = eel_gtk_tree_view_column_clipped_text (column,
                                                         GTK_TREE_MODEL (list_view->details->model),
                                                         &iter);
    }

    if (clipped != NULL) {
        gtk_tooltip_set_text (tooltip, clipped);
        gtk_tree_view_set_tooltip_cell (GTK_TREE_VIEW (widget), tooltip, path, column, NULL);
        g_free (clipped);

        ret = TRUE;
    } else if (list_view->details->show_tooltips &&
               !gtk_tree_view_is_blank_at_pos (GTK_TREE_VIEW (widget), x, y, NULL, NULL, NULL, NULL)) {
        NemoFile *file;

        gtk_tree_model_get (model, &iter, NEMO_LIST_MODEL_FILE_COLUMN, &file, -1);
        if (file != NULL) {
            gchar *tooltip_text;

            tooltip_text = nemo_file_construct_tooltip (file,
                                                        list_view->details->tooltip_flags,
                                                        nemo_view_get_model (NEMO_VIEW (list_view)));
            gtk_tooltip_set_markup (tooltip, tooltip_text);
            gtk_tree_view_set_tooltip_cell (GTK_TREE_VIEW (widget), tooltip, path, NULL, NULL);
            g_free (tooltip_text);

            ret = TRUE;
        }

        nemo_file_unref (file);
    }

    gtk_tree_path_free (path);

    return ret;
}

static gboolean
leave_notify_callback (G_GNUC_UNUSED GtkWidget *widget,
		       G_GNUC_UNUSED GdkEventCrossing *event,
		       gpointer callback_data)
{
	NemoListView *view;

	view = NEMO_LIST_VIEW (callback_data);

	if (click_policy == NEMO_CLICK_POLICY_SINGLE &&
	    view->details->hover_path != NULL) {
		gtk_tree_path_free (view->details->hover_path);
		view->details->hover_path = NULL;
	}

	return FALSE;
}

static gboolean
enter_notify_callback (GtkWidget *widget,
		       GdkEventCrossing *event,
		       gpointer callback_data)
{
	NemoListView *view;

	view = NEMO_LIST_VIEW (callback_data);

	if (click_policy == NEMO_CLICK_POLICY_SINGLE) {
		if (view->details->hover_path != NULL) {
			gtk_tree_path_free (view->details->hover_path);
		}

		gtk_tree_view_get_path_at_pos (GTK_TREE_VIEW (widget),
					       event->x, event->y,
					       &view->details->hover_path,
					       NULL, NULL, NULL);

		if (view->details->hover_path != NULL) {
			gdk_window_set_cursor (gtk_widget_get_window (widget), hand_cursor);
		}
	}

	return FALSE;
}

/* The cursor row when it is selected, since that is where the keyboard is,
   else the first selected row in sight. */
static gboolean
nemo_list_view_get_selection_menu_rect (NemoView *view, GdkRectangle *rect)
{
	NemoListView *list_view = NEMO_LIST_VIEW (view);
	GtkTreeView *tree_view = list_view->details->tree_view;
	GtkTreeViewColumn *column = list_view->details->file_name_column;
	GtkTreeSelection *selection = gtk_tree_view_get_selection (tree_view);
	GtkTreePath *cursor = NULL;
	GList *rows, *l;
	gboolean found = FALSE;

	gtk_tree_view_get_cursor (tree_view, &cursor, NULL);
	if (cursor != NULL && gtk_tree_selection_path_is_selected (selection, cursor)) {
		found = eel_gtk_tree_view_get_row_rect (tree_view, cursor, column, rect);
	}
	gtk_tree_path_free (cursor);

	if (!found) {
		rows = gtk_tree_selection_get_selected_rows (selection, NULL);
		for (l = rows; l != NULL && !found; l = l->next) {
			found = eel_gtk_tree_view_get_row_rect (tree_view, l->data, column, rect);
		}
		g_list_free_full (rows, (GDestroyNotify) gtk_tree_path_free);
	}

	return found &&
	       gtk_widget_translate_coordinates (GTK_WIDGET (tree_view), GTK_WIDGET (view),
						 rect->x, rect->y, &rect->x, &rect->y);
}

static void
do_popup_menu (GtkWidget *widget, NemoListView *view, GdkEventButton *event)
{
 	if (tree_view_has_selection (GTK_TREE_VIEW (widget))) {
		nemo_view_pop_up_selection_context_menu (NEMO_VIEW (view), event);
	} else {
                nemo_view_pop_up_background_context_menu (NEMO_VIEW (view), event);
	}
}

static void
row_activated_callback (G_GNUC_UNUSED GtkTreeView *treeview, G_GNUC_UNUSED GtkTreePath *path,
			G_GNUC_UNUSED GtkTreeViewColumn *column, NemoListView *view)
{
	activate_selected_items (view);
}

static void
columns_reordered_callback (G_GNUC_UNUSED AtkObject *atk,
                            gpointer user_data)
{
    NemoListView *view = NEMO_LIST_VIEW (user_data);

    gchar **columns;
    GList *vis_columns = NULL;
    int i;
    NemoFile *file = nemo_view_get_directory_as_file (NEMO_VIEW (view));

    columns = get_visible_columns (view);

    for (i = 0; columns[i] != NULL; ++i) {
        vis_columns = g_list_prepend (vis_columns, columns[i]);
    }

    vis_columns = g_list_reverse (vis_columns);

    GList *tv_list, *iter, *l;
    GList *list = NULL;

    tv_list = gtk_tree_view_get_columns (view->details->tree_view);

    for (iter = tv_list; iter != NULL; iter = iter->next) {
        for (l = vis_columns; l != NULL; l = l->next) {
            if (iter->data == g_hash_table_lookup (view->details->columns, l->data))
                list = g_list_prepend (list, g_strdup ((gchar *) l->data));
        }
    }

    list = g_list_reverse (list);

    if (!nemo_global_preferences_get_remember_folder_settings ()) {
        nemo_window_set_ignore_meta_column_order (nemo_view_get_nemo_window (NEMO_VIEW (view)), list);
    } else if (nemo_file_is_in_search (file)) {
        gchar **column_array = string_array_from_string_glist (list);

        nemo_config_set_strv (nemo_search_preferences,
                             NEMO_PREFERENCES_SEARCH_VISIBLE_COLUMNS,
                             (const gchar **) column_array);
        g_strfreev (column_array);
    } else {
        nemo_folder_settings_set_list (file,
                                       NEMO_METADATA_KEY_LIST_VIEW_COLUMN_ORDER,
                                       list);
    }
    /* list owns copies now: vis_columns only borrows from columns, and any
       column that didn't survive the match used to be freed by nobody. */
    g_list_free_full (list, g_free);
    g_strfreev (columns);
    g_list_free (vis_columns);
    g_list_free (tv_list);
}

static gboolean
clicked_on_text_in_name_cell (NemoListView *view, GtkTreePath *path, GdkEventButton *event)
{
    gboolean ret = FALSE;

    NemoListViewDetails *details = view->details;
    int x_col_offset, x_cell_offset, width, expander_size, horizontal_separator, expansion_offset;

    x_col_offset = gtk_tree_view_column_get_x_offset (details->file_name_column);

    gtk_tree_view_column_cell_get_position (details->file_name_column,
                                            GTK_CELL_RENDERER (details->file_name_cell),
                                            &x_cell_offset, &width);

    if (expanders_enabled (view)) {
        gtk_widget_style_get (GTK_WIDGET (details->tree_view),
                                          "expander-size", &expander_size,
                                          "horizontal-separator", &horizontal_separator,
                                          NULL);

        expander_size += 4;
        expansion_offset = ((horizontal_separator / 2) + gtk_tree_path_get_depth (path) * expander_size);
    } else {
        expansion_offset = 0;
    }

    ret = (event->x > (expansion_offset + x_col_offset + x_cell_offset) &&
           event->x < (x_col_offset + x_cell_offset + width)) &&
           !gtk_tree_view_is_blank_at_pos (GTK_TREE_VIEW (view->details->tree_view),
                                                          event->x, event->y,
                                                          NULL, NULL, NULL, NULL);

    return ret;
}

static gboolean
clicked_within_double_click_interval (NemoListView *view)
{
    static gint64 last_click_time = 0;
    static int click_count = 0;

    gint64 current_time;
    gint interval;

    /* fetch system double-click time */
    g_object_get (G_OBJECT (gtk_widget_get_settings (GTK_WIDGET (view))),
              "gtk-double-click-time", &interval,
              NULL);

    current_time = g_get_monotonic_time ();
    if (current_time - last_click_time < interval * 1000) {
        click_count++;
    } else {
        click_count = 0;
    }

    /* Stash time for next compare */
    last_click_time = current_time;

    /* Only allow double click */
    if (click_count == 1) {
        click_count = 0;
        last_click_time = 0;
        return TRUE;
    } else {
        return FALSE;
    }
}

static gboolean
clicked_within_slow_click_interval_on_text (NemoListView *view, GtkTreePath *path, GdkEventButton *event)
{
    static gint64 last_slow_click_time = 0;
    static gint slow_click_count = 0;
    gint64 current_time;
    gint interval;
    gint double_click_interval;

    /* fetch system double-click time */
    g_object_get (G_OBJECT (gtk_widget_get_settings (GTK_WIDGET (view))),
                  "gtk-double-click-time", &double_click_interval,
                  NULL);

    /* slow click interval is always 800ms longer than the system
     * double-click interval. */

    interval = double_click_interval + 800;

    current_time = g_get_monotonic_time ();
    if (current_time - last_slow_click_time < interval * 1000) {
        slow_click_count = 1;
    } else {
        slow_click_count = 0;
    }

    /* Stash time for next compare */
    last_slow_click_time = current_time;

    GtkTreeSelection *selection = gtk_tree_view_get_selection (GTK_TREE_VIEW (view->details->tree_view));

    GList *selected = gtk_tree_selection_get_selected_rows (selection, NULL);
    gint selected_count = g_list_length (selected);

    g_list_free_full (selected, (GDestroyNotify) gtk_tree_path_free);

    if (selected_count != 1)
        return FALSE;

    /* Only allow second click on text to trigger this */
    if (slow_click_count == 1 && view->details->double_click_path[1] &&
        gtk_tree_path_compare (view->details->double_click_path[0], view->details->double_click_path[1]) == 0 &&
        clicked_on_text_in_name_cell (view, path, event)) {
        slow_click_count = 0;

        return TRUE;
    } else {
        return FALSE;
    }
}

static gboolean
handle_icon_double_click (NemoListView *view, GtkTreePath *path, GdkEventButton *event, gboolean on_expander)
{
    /* Ignore double click if we are in single click mode */
    if (click_policy == NEMO_CLICK_POLICY_SINGLE) {
        return FALSE;
    }

    if (event->button == GDK_BUTTON_SECONDARY) {
        return FALSE;
    }

    if (clicked_within_double_click_interval (view) &&
        view->details->double_click_path[1] &&
        gtk_tree_path_compare (view->details->double_click_path[0], view->details->double_click_path[1]) == 0 &&
        !on_expander) {
        /* NOTE: Activation can actually destroy the view if we're switching */
        if (!button_event_modifies_selection (event)) {
            if (event->button == 1) {
                activate_selected_items (view);
            } else if (event->button == 2) {
                activate_selected_items_alternate (view, NULL, TRUE);
            }

            return TRUE;
        } else if (event->button == 1 &&
               (event->state & GDK_SHIFT_MASK) != 0) {
            NemoFile *file;
            file = nemo_list_model_file_for_path (view->details->model, path);
            if (file != NULL) {
                activate_selected_items_alternate (view, file, TRUE);
                nemo_file_unref (file);
            }

            return TRUE;
        }
    }

    return FALSE;
}

static gboolean
handle_icon_slow_two_click (NemoListView *view, GtkTreePath *path, GdkEventButton *event)
{
    NemoListViewDetails *details;
    NemoFile *file;
    gboolean can_rename;

    details = view->details;

    if (!details->click_to_rename)
        return FALSE;

    file = nemo_list_model_file_for_path (view->details->model, path);
    can_rename = nemo_file_can_rename (file);
    nemo_file_unref (file);

    if (!can_rename)
        return FALSE;

    if (clicked_within_slow_click_interval_on_text (view, path, event) && !button_event_modifies_selection (event)) {
        return TRUE;
    }

    return FALSE;
}

static gboolean
button_press_callback (GtkWidget *widget, GdkEventButton *event, gpointer callback_data)
{
	NemoListView *view;
	GtkTreeView *tree_view;
	GtkTreePath *path;
	gboolean call_parent;
	GtkTreeSelection *selection;
	GtkWidgetClass *tree_view_class;

	int expander_size, horizontal_separator;
	gboolean on_expander;

	view = NEMO_LIST_VIEW (callback_data);
	tree_view = GTK_TREE_VIEW (widget);
	tree_view_class = GTK_WIDGET_GET_CLASS (tree_view);
	selection = gtk_tree_view_get_selection (tree_view);

	/* Don't handle extra mouse buttons here */
	if (event->button > 5) {
		return GDK_EVENT_PROPAGATE;
	}

    if (event->type == GDK_2BUTTON_PRESS || event->type == GDK_3BUTTON_PRESS) {
        if (nemo_config_get_boolean (nemo_preferences, NEMO_PREFERENCES_CLICK_DOUBLE_PARENT_FOLDER) &&
                                    (event->button == 1)) {
            /* double left click on blank will go to parent folder */
            if (!gtk_tree_view_get_path_at_pos (tree_view, event->x, event->y,
                                                NULL, NULL, NULL, NULL)) {
                NemoWindowSlot *slot = nemo_view_get_nemo_window_slot (NEMO_VIEW (view));
                nemo_window_slot_go_up (slot, 0);
            }
        }

        return GDK_EVENT_STOP;
    }

	if (event->window != gtk_tree_view_get_bin_window (tree_view)) {
		return GDK_EVENT_PROPAGATE;
	}

    if (!nemo_view_get_active (NEMO_VIEW (view)) && gtk_tree_selection_count_selected_rows (selection) > 0) {
        NemoWindowSlot *slot = nemo_view_get_nemo_window_slot (NEMO_VIEW (view));
        nemo_window_slot_make_hosting_pane_active (slot);
        return GDK_EVENT_STOP;
    }

	nemo_list_model_set_drag_view
		(NEMO_LIST_MODEL (gtk_tree_view_get_model (tree_view)),
		 tree_view,
		 event->x, event->y);

	view->details->ignore_button_release = FALSE;

	/* After Escape there is no row for Shift to extend from. */
	if (view->details->cursor_forgotten) {
		event->state &= ~GDK_SHIFT_MASK;
	}

	call_parent = TRUE;
	if (gtk_tree_view_get_path_at_pos (tree_view, event->x, event->y,
					   &path, NULL, NULL, NULL)) {
        if (expanders_enabled (view)) {
    		gtk_widget_style_get (widget,
    				      "expander-size", &expander_size,
    				      "horizontal-separator", &horizontal_separator,
    				      NULL);
    		/* TODO we should not hardcode this extra padding. It is
    		 * EXPANDER_EXTRA_PADDING from GtkTreeView.
    		 */
    		expander_size += 4;
    		on_expander = (event->x <= horizontal_separator / 2 +
    			       gtk_tree_path_get_depth (path) * expander_size);
        } else {
            on_expander = FALSE;
        }
		/* Keep track of path of last click so double clicks only happen
		 * on the same item */
		if ((event->button == 1 || event->button == 2)  &&
		    event->type == GDK_BUTTON_PRESS) {
			if (view->details->double_click_path[1]) {
				gtk_tree_path_free (view->details->double_click_path[1]);
			}
			view->details->double_click_path[1] = view->details->double_click_path[0];
			view->details->double_click_path[0] = gtk_tree_path_copy (path);
		}

		if (handle_icon_double_click (view, path, event, on_expander)) {
			/* Double clicking does not trigger a D&D action. */
			view->details->drag_button = 0;

		} else {
            /* queue up renaming if we've clicked within the slow-click timeframe.  Don't actually
               do it, however, until there's a button release (this allows dragging to occur on
               single items, without triggering rename) */
            view->details->rename_on_release = handle_icon_slow_two_click (view, path, event);

			/* We're going to filter out some situations where
			 * we can't let the default code run because all
			 * but one row would be would be deselected. We don't
			 * want that; we want the right click menu or single
			 * click to apply to everything that's currently selected. */

			if (event->button == 3 &&
			    gtk_tree_selection_path_is_selected (selection, path)) {
				call_parent = FALSE;
			}

			if ((event->button == 1 || event->button == 2) &&
			    ((event->state & eel_gtk_primary_mask (event->window)) != 0 ||
			     (event->state & GDK_SHIFT_MASK) == 0)) {
				view->details->row_selected_on_button_down = gtk_tree_selection_path_is_selected (selection, path);
				if (view->details->row_selected_on_button_down) {
					call_parent = on_expander;
					view->details->ignore_button_release = call_parent;
				} else if ((event->state & eel_gtk_primary_mask (event->window)) != 0) {
					GList *selected_rows;
					GList *l;

					call_parent = FALSE;
					if ((event->state & GDK_SHIFT_MASK) != 0) {
						GtkTreePath *cursor;
						gtk_tree_view_get_cursor (tree_view, &cursor, NULL);
						if (cursor != NULL) {
							gtk_tree_selection_select_range (selection, cursor, path);
						} else {
							gtk_tree_selection_select_path (selection, path);
						}
					} else {
						gtk_tree_selection_select_path (selection, path);
					}
					selected_rows = gtk_tree_selection_get_selected_rows (selection, NULL);

					/* This unselects everything */
					gtk_tree_view_set_cursor (tree_view, path, NULL, FALSE);

					/* So select it again */
					l = selected_rows;
					while (l != NULL) {
						GtkTreePath *p = l->data;
						l = l->next;
						gtk_tree_selection_select_path (selection, p);
						gtk_tree_path_free (p);
					}
					g_list_free (selected_rows);
				} else {
					view->details->ignore_button_release = on_expander;
				}
			}

			if (call_parent) {
				g_signal_handlers_block_by_func (tree_view,
								 row_activated_callback,
								 view);

				tree_view_class->button_press_event (widget, event);

				g_signal_handlers_unblock_by_func (tree_view,
								   row_activated_callback,
								   view);
			} else if (gtk_tree_selection_path_is_selected (selection, path)) {
				gtk_widget_grab_focus (widget);
			}

			if ((event->button == 1 || event->button == 2) &&
			    event->type == GDK_BUTTON_PRESS) {
				view->details->drag_started = FALSE;
				view->details->drag_button = event->button;
				view->details->drag_x = event->x;
				view->details->drag_y = event->y;
			}

			if (event->button == 3) {
				do_popup_menu (widget, view, event);
			}
		}

		gtk_tree_path_free (path);
	} else {
		if ((event->button == 1 || event->button == 2)  &&
		    event->type == GDK_BUTTON_PRESS) {
			if (view->details->double_click_path[1]) {
				gtk_tree_path_free (view->details->double_click_path[1]);
			}
			view->details->double_click_path[1] = view->details->double_click_path[0];
			view->details->double_click_path[0] = NULL;
		}
		/* Deselect if people click outside any row. It's OK to
		   let default code run; it won't reselect anything. */
		gtk_tree_selection_unselect_all (gtk_tree_view_get_selection (tree_view));
		tree_view_class->button_press_event (widget, event);

		if (event->button == 3) {
			do_popup_menu (widget, view, event);
		}
	}

	/* We chained to the default handler in this method, so never
	 * let the default handler run */
	return GDK_EVENT_STOP;
}

static gboolean
button_release_callback (G_GNUC_UNUSED GtkWidget *widget,
			 GdkEventButton *event,
			 gpointer callback_data)
{
	NemoListView *view;

	view = NEMO_LIST_VIEW (callback_data);

    view->details->rubber_banding = FALSE;

	if (event->button == view->details->drag_button) {
		stop_drag_check (view);
		if (!view->details->drag_started &&
		    !view->details->ignore_button_release) {
			nemo_list_view_did_not_drag (view, event);
		}
	}
	return FALSE;
}

static gboolean
popup_menu_callback (GtkWidget *widget, gpointer callback_data)
{
 	NemoListView *view;

	view = NEMO_LIST_VIEW (callback_data);

	do_popup_menu (widget, view, NULL);

	return TRUE;
}

static void
subdirectory_done_loading_callback (NemoDirectory *directory, NemoListView *view)
{
	nemo_list_model_subdirectory_done_loading (view->details->model, directory);

    queue_update_visible_icons (view, INITIAL_UPDATE_VISIBLE_DELAY);
}

static void
row_expanded_callback (G_GNUC_UNUSED GtkTreeView *treeview, G_GNUC_UNUSED GtkTreeIter *iter, GtkTreePath *path, gpointer callback_data)
{
 	NemoListView *view;
 	NemoDirectory *directory;

	view = NEMO_LIST_VIEW (callback_data);

	if (nemo_list_model_load_subdirectory (view->details->model, path, &directory)) {
		char *uri;

		uri = nemo_directory_get_uri (directory);
		DEBUG ("Row expaded callback for uri %s", uri);
		g_free (uri);

		nemo_view_add_subdirectory (NEMO_VIEW (view), directory);
        nemo_list_model_set_expanding (view->details->model, directory);

		if (nemo_directory_are_all_files_seen (directory)) {
			nemo_list_model_subdirectory_done_loading (view->details->model,
								 directory);
		} else {
			g_signal_connect_object (directory, "done_loading",
						 G_CALLBACK (subdirectory_done_loading_callback),
						 view, 0);
		}

		nemo_directory_unref (directory);
	}
}

struct UnloadDelayData {
	NemoFile *file;
	NemoDirectory *directory;
	NemoListView *view;
};

static gboolean
unload_file_timeout (gpointer data)
{
	struct UnloadDelayData *unload_data = data;
	GtkTreeIter iter;
	NemoListModel *model;
	GtkTreePath *path;

	if (unload_data->view != NULL) {
		model = unload_data->view->details->model;
		if (nemo_list_model_get_tree_iter_from_file (model,
							   unload_data->file,
							   unload_data->directory,
							   &iter)) {
			path = gtk_tree_model_get_path (GTK_TREE_MODEL (model), &iter);
			if (!gtk_tree_view_row_expanded (unload_data->view->details->tree_view,
							 path)) {
				nemo_list_model_unload_subdirectory (model, &iter);
			}
			gtk_tree_path_free (path);
		}

		g_object_remove_weak_pointer (G_OBJECT (unload_data->view),
					      (gpointer *) &unload_data->view);
	}

	if (unload_data->directory) {
		nemo_directory_unref (unload_data->directory);
	}
	nemo_file_unref (unload_data->file);
	g_free (unload_data);
	return FALSE;
}

static void
row_collapsed_callback (G_GNUC_UNUSED GtkTreeView *treeview, GtkTreeIter *iter, G_GNUC_UNUSED GtkTreePath *path, gpointer callback_data)
{
 	NemoListView *view;
 	NemoFile *file;
	NemoDirectory *directory;
	GtkTreeIter parent;
	struct UnloadDelayData *unload_data;
	GtkTreeModel *model;
	char *uri;

	view = NEMO_LIST_VIEW (callback_data);
	model = GTK_TREE_MODEL (view->details->model);

	gtk_tree_model_get (model, iter,
			    NEMO_LIST_MODEL_FILE_COLUMN, &file,
			    -1);

	directory = NULL;
	if (gtk_tree_model_iter_parent (model, &parent, iter)) {
		gtk_tree_model_get (model, &parent,
				    NEMO_LIST_MODEL_SUBDIRECTORY_COLUMN, &directory,
				    -1);
	}


	uri = nemo_file_get_uri (file);
	DEBUG ("Row collapsed callback for uri %s", uri);
	g_free (uri);

	unload_data = g_new (struct UnloadDelayData, 1);
	unload_data->view = view;
	unload_data->file = file;
	unload_data->directory = directory;

	g_object_add_weak_pointer (G_OBJECT (unload_data->view),
				   (gpointer *) &unload_data->view);

	g_timeout_add_seconds (COLLAPSE_TO_UNLOAD_DELAY,
			       unload_file_timeout,
			       unload_data);
	/* cppcheck-suppress memleak - unload_file_timeout frees it */
}

static void
subdirectory_unloaded_callback (NemoListModel *model,
				NemoDirectory *directory,
				gpointer callback_data)
{
	NemoListView *view;

	g_return_if_fail (NEMO_IS_LIST_MODEL (model));
	g_return_if_fail (NEMO_IS_DIRECTORY (directory));

	view = NEMO_LIST_VIEW(callback_data);

	g_signal_handlers_disconnect_by_func (directory,
					      G_CALLBACK (subdirectory_done_loading_callback),
					      view);
	nemo_view_remove_subdirectory (NEMO_VIEW (view), directory);

	/* The rows that just left were measured on their way in, so the columns
	   would keep the width they asked for. Once per collapse is cheap. */
	remeasure_rows (view);
}

static gboolean
key_press_callback (GtkWidget *widget, GdkEventKey *event, gpointer callback_data)
{
	NemoView *view;
	gboolean handled;
	GtkTreeView *tree_view;
	GtkTreePath *path;

	tree_view = GTK_TREE_VIEW (widget);

	view = NEMO_VIEW (callback_data);
	handled = FALSE;

    if (event->keyval == GDK_KEY_slash ||
        event->keyval == GDK_KEY_KP_Divide ||
        event->keyval == GDK_KEY_asciitilde) {
        if (gtk_bindings_activate_event (G_OBJECT (nemo_view_get_nemo_window (view)), event)) {
            return GDK_EVENT_STOP;
        }
    }

	if (NEMO_LIST_VIEW (view)->details->cursor_forgotten) {
		if (start_cursor_over (NEMO_LIST_VIEW (view), event)) {
			return GDK_EVENT_STOP;
		}
		/* There is no row to open or close. */
		if (event->keyval == GDK_KEY_Left || event->keyval == GDK_KEY_Right) {
			return GDK_EVENT_STOP;
		}
	}

	switch (event->keyval) {
	case GDK_KEY_F10:
		if (event->state & GDK_CONTROL_MASK) {
			nemo_view_pop_up_background_context_menu (view, NULL);
			handled = TRUE;
		}
		break;
	case GDK_KEY_Right:
        if (!expanders_enabled (NEMO_LIST_VIEW (view)))
            break;

		gtk_tree_view_get_cursor (tree_view, &path, NULL);
		if (path) {
			gtk_tree_view_expand_row (tree_view, path, FALSE);
			gtk_tree_path_free (path);
		}
		handled = TRUE;
		break;
	case GDK_KEY_Left:
        if (!expanders_enabled (NEMO_LIST_VIEW (view)))
            break;

		gtk_tree_view_get_cursor (tree_view, &path, NULL);
		if (path) {
			if (!gtk_tree_view_collapse_row (tree_view, path)) {
				/* if the row is already collapsed or doesn't have any children,
				 * jump to the parent row instead.
				 */
				if ((gtk_tree_path_get_depth (path) > 1) && gtk_tree_path_up (path)) {
					gtk_tree_view_set_cursor (tree_view, path, NULL, FALSE);
				}
			}

			gtk_tree_path_free (path);
		}
		handled = TRUE;
		break;
	case GDK_KEY_space:
		if (event->state & GDK_CONTROL_MASK) {
			handled = FALSE;
			break;
		}
		if (!gtk_widget_has_focus (GTK_WIDGET (NEMO_LIST_VIEW (view)->details->tree_view))) {
			handled = FALSE;
			break;
		}
		if ((event->state & GDK_SHIFT_MASK) != 0) {
			activate_selected_items_alternate (NEMO_LIST_VIEW (view), NULL, TRUE);
		} else {
			preview_selected_items (NEMO_LIST_VIEW (view));
		}
		handled = TRUE;
		break;
	case GDK_KEY_Return:
	case GDK_KEY_KP_Enter:
		if ((event->state & GDK_SHIFT_MASK) != 0) {
			activate_selected_items_alternate (NEMO_LIST_VIEW (view), NULL, TRUE);
		} else {
			activate_selected_items (NEMO_LIST_VIEW (view));
		}
		handled = TRUE;
		break;
	case GDK_KEY_v:
		/* Eat Control + v to not enable type ahead */
		if ((event->state & eel_gtk_primary_mask (event->window)) != 0) {
			handled = TRUE;
		}
		break;
	case GDK_KEY_Escape:
		forget_cursor (NEMO_LIST_VIEW (view));
		handled = TRUE;
		break;

	default:
		handled = FALSE;
	}

	return handled;
}

static void
set_ok_to_load_deferred_attrs (NemoListView  *list_view,
                       gboolean       ok)
{
    list_view->details->ok_to_load_deferred_attrs = ok;

    if (ok) {
        queue_update_visible_icons (list_view, INITIAL_UPDATE_VISIBLE_DELAY);
    }
}

static void
nemo_list_view_reveal_selection (NemoView *view)
{
	GList *selection;

	g_return_if_fail (NEMO_IS_LIST_VIEW (view));

        selection = nemo_view_get_selection (view);

	/* Make sure at least one of the selected items is scrolled into view */
	if (selection != NULL) {
		NemoListView *list_view;
		NemoFile *file;
		GtkTreeIter iter;
		GtkTreePath *path;

		list_view = NEMO_LIST_VIEW (view);
		file = selection->data;
		if (nemo_list_model_get_first_iter_for_file (list_view->details->model, file, &iter)) {
			path = gtk_tree_model_get_path (GTK_TREE_MODEL (list_view->details->model), &iter);

			gtk_tree_view_scroll_to_cell (list_view->details->tree_view, path, NULL, FALSE, 0.0, 0.0);

			gtk_tree_path_free (path);
		}
	}

        nemo_file_list_free (selection);
}

static gboolean
sort_criterion_changes_due_to_user (GtkTreeView *tree_view)
{
	GList *columns, *p;
	GtkTreeViewColumn *column;
	GSignalInvocationHint *ihint;
	gboolean ret;

	ret = FALSE;

	columns = gtk_tree_view_get_columns (tree_view);
	for (p = columns; p != NULL; p = p->next) {
		column = p->data;
		ihint = g_signal_get_invocation_hint (column);
		if (ihint != NULL) {
			ret = TRUE;
			break;
		}
	}
	g_list_free (columns);

	return ret;
}

static void
sort_column_changed_callback (GtkTreeSortable *sortable,
			      NemoListView *view)
{
	NemoFile *file;
	gint sort_column_id, default_sort_column_id;
	GtkSortType reversed;
	GQuark sort_attr, default_sort_attr;
	char *reversed_attr, *default_reversed_attr;
	gboolean default_sort_reversed;

	file = nemo_view_get_directory_as_file (NEMO_VIEW (view));

	gtk_tree_sortable_get_sort_column_id (sortable, &sort_column_id, &reversed);
	sort_attr = nemo_list_model_get_attribute_from_sort_column_id (view->details->model, sort_column_id);

	default_sort_column_id = nemo_list_model_get_sort_column_id_from_attribute (view->details->model,
										  g_quark_from_string (get_default_sort_order (file, &default_sort_reversed)));
	default_sort_attr = nemo_list_model_get_attribute_from_sort_column_id (view->details->model, default_sort_column_id);

        if (!nemo_global_preferences_get_remember_folder_settings ())
                nemo_window_set_ignore_meta_sort_column (nemo_view_get_nemo_window (NEMO_VIEW (view)),
                                                         g_quark_to_string (sort_attr));
        else if (nemo_file_is_in_search (file)) {
            nemo_config_set_string (nemo_search_preferences, NEMO_PREFERENCES_SEARCH_SORT_COLUMN, g_quark_to_string (sort_attr));
        } else {
            nemo_folder_settings_set (file, NEMO_METADATA_KEY_LIST_VIEW_SORT_COLUMN,
                                      g_quark_to_string (default_sort_attr), g_quark_to_string (sort_attr));
        }

	default_reversed_attr = (default_sort_reversed ? (char *)"true" : (char *)"false");

	if (view->details->last_sort_attr != sort_attr &&
	    sort_criterion_changes_due_to_user (view->details->tree_view)) {
		/* at this point, the sort order is always GTK_SORT_ASCENDING, if the sort column ID
		 * switched. Invert the sort order, if it's the default criterion with a reversed preference,
		 * or if it makes sense for the attribute (i.e. date). */
		if (sort_attr == default_sort_attr) {
			/* use value from preferences */
			reversed = nemo_config_get_boolean (nemo_preferences,
							   NEMO_PREFERENCES_DEFAULT_SORT_IN_REVERSE_ORDER);
		} else {
			reversed = nemo_file_is_date_sort_attribute_q (sort_attr);
		}

		if (reversed) {
			g_signal_handlers_block_by_func (sortable, sort_column_changed_callback, view);
			gtk_tree_sortable_set_sort_column_id (GTK_TREE_SORTABLE (view->details->model),
							      sort_column_id,
							      GTK_SORT_DESCENDING);
			g_signal_handlers_unblock_by_func (sortable, sort_column_changed_callback, view);
		}
	}

    if (!nemo_global_preferences_get_remember_folder_settings ()) {
        nemo_window_set_ignore_meta_sort_direction (nemo_view_get_nemo_window (NEMO_VIEW (view)),
                                                    reversed ? SORT_DESCENDING : SORT_ASCENDING);
    } else if (nemo_file_is_in_search (file)) {
        nemo_config_set_boolean (nemo_search_preferences, NEMO_PREFERENCES_SEARCH_REVERSE_SORT, reversed);
    } else {
        reversed_attr = (reversed ? (char *)"true" : (char *)"false");
        nemo_folder_settings_set (file, NEMO_METADATA_KEY_LIST_VIEW_SORT_REVERSED,
                                  default_reversed_attr, reversed_attr);
    }

	/* Make sure selected item(s) is visible after sort */
	nemo_list_view_reveal_selection (NEMO_VIEW (view));

	view->details->last_sort_attr = sort_attr;
}

static gboolean
editable_focus_out_cb (G_GNUC_UNUSED GtkWidget *widget,
		       G_GNUC_UNUSED GdkEvent *event,
		       gpointer user_data)
{
	NemoListView *view = user_data;

	nemo_view_set_is_renaming (NEMO_VIEW (view), FALSE);
	nemo_view_unfreeze_updates (NEMO_VIEW (view));

    return GDK_EVENT_PROPAGATE;
}

static void
cell_renderer_editing_started_cb (G_GNUC_UNUSED GtkCellRenderer *renderer,
				  GtkCellEditable *editable,
				  const gchar *path_str,
				  NemoListView *list_view)
{
	GtkEntry *entry;
	GtkTreePath *path;
	NemoFile *file;

	entry = GTK_ENTRY (editable);
	list_view->details->editable_widget = editable;

	/* The listing leaves a shortcut's extension off the name. The box shows it,
	   so a rename can see what it is keeping. */
	path = gtk_tree_path_new_from_string (path_str);
	file = path != NULL ? nemo_list_model_file_for_path (list_view->details->model, path) : NULL;

	if (file != NULL) {
		char *rename_name = nemo_file_get_rename_name (file);

		if (g_strcmp0 (rename_name, gtk_entry_get_text (entry)) != 0) {
			gtk_entry_set_text (entry, rename_name);
		}

		g_free (rename_name);
		nemo_file_unref (file);
	}

	gtk_tree_path_free (path);

	/* Free a previously allocated original_name */
	g_free (list_view->details->original_name);

	list_view->details->original_name = g_strdup (gtk_entry_get_text (entry));

	g_signal_connect (entry, "focus-out-event",
			  G_CALLBACK (editable_focus_out_cb), list_view);

	nemo_clipboard_set_up_editable
		(GTK_EDITABLE (entry),
		 nemo_view_get_ui_manager (NEMO_VIEW (list_view)),
		 FALSE);
}

static void
cell_renderer_editing_canceled (G_GNUC_UNUSED GtkCellRendererText *cell,
				NemoListView    *view)
{
    view->details->editable_widget = NULL;
	nemo_view_set_is_renaming (NEMO_VIEW (view), FALSE);
	nemo_view_unfreeze_updates (NEMO_VIEW (view));
}

static void
cell_renderer_edited (G_GNUC_UNUSED GtkCellRendererText *cell,
		      const char          *path_str,
		      const char          *new_text,
		      NemoListView    *view)
{
	GtkTreePath *path;
	NemoFile *file;
	GtkTreeIter iter;

	view->details->editable_widget = NULL;
	nemo_view_set_is_renaming (NEMO_VIEW (view), FALSE);

	/* Don't allow a rename with an empty string. Revert to original
	 * without notifying the user.
	 */
	if (new_text[0] == '\0') {
		g_object_set (G_OBJECT (view->details->file_name_cell),
			      "editable", FALSE,
			      NULL);
		nemo_view_unfreeze_updates (NEMO_VIEW (view));
		return;
	}

	path = gtk_tree_path_new_from_string (path_str);

	gtk_tree_model_get_iter (GTK_TREE_MODEL (view->details->model),
				 &iter, path);

	gtk_tree_path_free (path);

	gtk_tree_model_get (GTK_TREE_MODEL (view->details->model),
			    &iter,
			    NEMO_LIST_MODEL_FILE_COLUMN, &file,
			    -1);

	/* Only rename if name actually changed */
	if (strcmp (new_text, view->details->original_name) != 0) {
		view->details->renaming_file = nemo_file_ref (file);
		view->details->rename_done = FALSE;
		nemo_rename_file (file, new_text, nemo_list_view_rename_callback, g_object_ref (view));
		g_free (view->details->original_name);
		view->details->original_name = g_strdup (new_text);
	}

	nemo_file_unref (file);

	/*We're done editing - make the filename-cells readonly again.*/
	g_object_set (G_OBJECT (view->details->file_name_cell),
		      "editable", FALSE,
		      NULL);

	nemo_view_unfreeze_updates (NEMO_VIEW (view));
}

static char *
get_root_uri_callback (G_GNUC_UNUSED NemoTreeViewDragDest *dest,
		       gpointer user_data)
{
	NemoListView *view;

	view = NEMO_LIST_VIEW (user_data);

	return nemo_view_get_uri (NEMO_VIEW (view));
}

// this is confusing... so rename them.
#define ALLOW_EXPAND FALSE
#define PREVENT_EXPAND TRUE

static gboolean
test_expand_row_callback (G_GNUC_UNUSED GtkTreeView *treeview,
                          G_GNUC_UNUSED GtkTreeIter *iter,
                          G_GNUC_UNUSED GtkTreePath *path,
                          gpointer     user_data)
{
    NemoListView *view = NEMO_LIST_VIEW (user_data);

    if (!view->details->drag_started) {
        return ALLOW_EXPAND;
    }

    if (eel_gtk_get_treeview_row_text_is_under_pointer (view->details->tree_view)) {
        return ALLOW_EXPAND;
    }

    return PREVENT_EXPAND;
}

static NemoFile *
get_file_for_path_callback (G_GNUC_UNUSED NemoTreeViewDragDest *dest,
			    GtkTreePath *path,
			    gpointer user_data)
{
    NemoListView *view;

    view = NEMO_LIST_VIEW (user_data);

    if (!eel_gtk_get_treeview_row_text_is_under_pointer (view->details->tree_view)) {
        return NULL;
    }

    return nemo_list_model_file_for_path (view->details->model, path);
}

/* Handles an URL received from Mozilla */
static void
list_view_handle_netscape_url (G_GNUC_UNUSED NemoTreeViewDragDest *dest, const char *encoded_url,
			       const char *target_uri, GdkDragAction action, int x, int y, NemoListView *view)
{
	nemo_view_handle_netscape_url_drop (NEMO_VIEW (view),
						encoded_url, target_uri, action, x, y);
}

static void
list_view_handle_uri_list (G_GNUC_UNUSED NemoTreeViewDragDest *dest, const char *item_uris,
			   const char *target_uri,
			   GdkDragAction action, int x, int y, NemoListView *view)
{
	nemo_view_handle_uri_list_drop (NEMO_VIEW (view),
					    item_uris, target_uri, action, x, y);
}

static void
list_view_handle_text (G_GNUC_UNUSED NemoTreeViewDragDest *dest, const char *text,
		       const char *target_uri,
		       GdkDragAction action, int x, int y, NemoListView *view)
{
	nemo_view_handle_text_drop (NEMO_VIEW (view),
					text, target_uri, action, x, y);
}

static void
list_view_handle_raw (G_GNUC_UNUSED NemoTreeViewDragDest *dest, const char *raw_data,
		      int length, const char *target_uri, const char *direct_save_uri,
		      GdkDragAction action, int x, int y, NemoListView *view)
{
	nemo_view_handle_raw_drop (NEMO_VIEW (view),
				       raw_data, length, target_uri, direct_save_uri,
				       action, x, y);
}

static void
move_copy_items_callback (G_GNUC_UNUSED NemoTreeViewDragDest *dest,
			  const GList *item_uris,
			  const char *target_uri,
			  guint action,
			  int x,
			  int y,
			  gpointer user_data)

{
	NemoView *view = user_data;

	if (!nemo_drag_confirm_drop (GTK_WIDGET (view), action, item_uris, target_uri)) {
		return;
	}

	nemo_clipboard_clear_if_colliding_uris (GTK_WIDGET (view),
						    item_uris,
						    nemo_view_get_copied_files_atom (view));
	nemo_view_move_copy_items (view,
				       item_uris,
				       NULL,
				       target_uri,
				       action,
				       x, y);
}

static void
column_header_menu_toggled (GtkCheckMenuItem *menu_item,
                            NemoListView *list_view)
{
	NemoFile *file;
    char **visible_columns;
    const char *menu_item_column_id;
	GList *list = NULL;
    GList *l, *current_view_columns;
	int i;

    file = nemo_view_get_directory_as_file (NEMO_VIEW (list_view));
    menu_item_column_id = g_object_get_data (G_OBJECT (menu_item), "column-name");

    current_view_columns = gtk_tree_view_get_columns (list_view->details->tree_view);

    for (l = current_view_columns; l != NULL; l = l->next) {
        GtkTreeViewColumn *c = GTK_TREE_VIEW_COLUMN (l->data);

        const char *current_id = g_object_get_data (G_OBJECT (c), "column-id");

        if (g_strcmp0 (current_id, menu_item_column_id) == 0) {
            if (gtk_check_menu_item_get_active (menu_item)) {
                list = g_list_prepend (list, g_strdup (current_id));
            }
        } else {
            if (gtk_tree_view_column_get_visible (c))
                list = g_list_prepend (list, g_strdup (current_id));
        }
    }

    list = g_list_reverse (list);

    if (!nemo_global_preferences_get_remember_folder_settings ()) {
        nemo_window_set_ignore_meta_visible_columns (nemo_view_get_nemo_window (NEMO_VIEW (list_view)), list);
    } else if (nemo_file_is_in_search (file)) {
        gchar **column_array = string_array_from_string_glist (list);

        nemo_config_set_strv (nemo_search_preferences,
                             NEMO_PREFERENCES_SEARCH_VISIBLE_COLUMNS,
                             (const gchar **) column_array);

        g_strfreev (column_array);
    } else
        nemo_folder_settings_set_list (file,
                                       NEMO_METADATA_KEY_LIST_VIEW_VISIBLE_COLUMNS,
                                       list);

    visible_columns = g_new0 (char *, g_list_length (list) + 1);
    for (i = 0, l = list; l != NULL; ++i, l = l->next) {
		visible_columns[i] = l->data;
    }

	/* set view values ourselves, as new metadata could not have been
	 * updated yet.
	 */
    apply_columns_settings (list_view, visible_columns, visible_columns);

    g_list_free (list);
    g_list_free (current_view_columns);
    g_strfreev (visible_columns);
}

static void
column_header_menu_use_default (G_GNUC_UNUSED GtkMenuItem *menu_item,
                                NemoListView *list_view)
{
	NemoFile *file;
	char **default_columns;
	char **default_order;

	file = nemo_view_get_directory_as_file (NEMO_VIEW (list_view));

    g_signal_handlers_block_by_func (list_view->details->tree_view,
                                     columns_reordered_callback,
                                     list_view);

    if (!nemo_global_preferences_get_remember_folder_settings ()) {
        NemoWindow *window = nemo_view_get_nemo_window (NEMO_VIEW (list_view));
        nemo_window_set_ignore_meta_visible_columns (window, NULL);
        nemo_window_set_ignore_meta_column_order (window, NULL);
    } else {
        nemo_folder_settings_set_list (file, NEMO_METADATA_KEY_LIST_VIEW_COLUMN_ORDER, NULL);
        nemo_folder_settings_set_list (file, NEMO_METADATA_KEY_LIST_VIEW_VISIBLE_COLUMNS, NULL);
    }

    if (nemo_file_is_in_search (file)) {
        nemo_config_reset (nemo_search_preferences, NEMO_PREFERENCES_SEARCH_VISIBLE_COLUMNS);
    }

    default_columns = get_default_visible_columns (list_view);

    default_order = get_default_column_order (list_view);

	/* set view values ourselves, as new metadata could not have been
	 * updated yet.
	 */
	apply_columns_settings (list_view, default_order, default_columns);

    g_signal_handlers_unblock_by_func (list_view->details->tree_view,
                                       columns_reordered_callback,
                                       list_view);

	g_strfreev (default_columns);
	g_strfreev (default_order);
}

static void
column_header_menu_disable_sort (GtkMenuItem *menu_item,
                                 NemoListView *list_view)
{
    gboolean active = gtk_check_menu_item_get_active (GTK_CHECK_MENU_ITEM (menu_item));

    nemo_list_model_set_temporarily_disable_sort (list_view->details->model, active);
}

static gboolean
column_header_clicked (G_GNUC_UNUSED GtkWidget *column_button,
                       GdkEventButton *event,
                       NemoListView *list_view)
{
    GList *current_view_columns, *l;
    NemoFile *file;
    GtkWidget *menu;
    GtkWidget *menu_item;

	/* The header never takes the focus, so a click that left it in the path
	   entry or the sidebar would sort one list and type into another. */
	nemo_view_grab_focus (NEMO_VIEW (list_view));

	if (event->button != GDK_BUTTON_SECONDARY) {
		return FALSE;
	}

    file = nemo_view_get_directory_as_file (NEMO_VIEW (list_view));

    menu = gtk_menu_new ();

    current_view_columns = gtk_tree_view_get_columns (list_view->details->tree_view);

    for (l = current_view_columns; l != NULL; l = l->next) {
        const char *name;
        char *label;
        char *lowercase;
        gboolean visible;

        GtkTreeViewColumn *c = GTK_TREE_VIEW_COLUMN (l->data);

        name = g_object_get_data (G_OBJECT (c), "column-id");

        if (!nemo_file_is_in_trash (file)) {
            if (g_strcmp0 (name, "trashed_on") == 0 ||
                g_strcmp0 (name, "trash_orig_path") == 0)
                continue;
        }

        if (!nemo_file_is_in_search (file)) {
            if (g_strcmp0 (name, "search_result_count") == 0 ||
                g_strcmp0 (name, "search_result_snippet") == 0)
                continue;
        }

        g_object_get (G_OBJECT (c),
                      "title", &label,
                      "visible", &visible,
                      NULL);

        lowercase = g_ascii_strdown (name, -1);

        menu_item = gtk_check_menu_item_new_with_label (label);
        gtk_menu_shell_append (GTK_MENU_SHELL (menu), menu_item);

        g_object_set_data_full (G_OBJECT (menu_item),
                                "column-name", g_strdup (name), g_free);

        gtk_check_menu_item_set_active (GTK_CHECK_MENU_ITEM (menu_item), visible);

        /* Don't allow hiding the filename */
        if (g_strcmp0 (lowercase, "name") == 0) {
            gtk_widget_set_sensitive (GTK_WIDGET (menu_item), FALSE);
        }

        g_signal_connect (menu_item,
                          "toggled",
                          G_CALLBACK (column_header_menu_toggled),
                          list_view);

        g_clear_pointer (&lowercase, g_free);
        g_clear_pointer (&label, g_free);
    }

    g_list_free (current_view_columns);

	menu_item = gtk_separator_menu_item_new ();
	gtk_menu_shell_append (GTK_MENU_SHELL (menu), menu_item);

	menu_item = gtk_menu_item_new_with_label (_("Use default"));
	gtk_menu_shell_append (GTK_MENU_SHELL (menu), menu_item);

	g_signal_connect (menu_item,
	                  "activate",
	                  G_CALLBACK (column_header_menu_use_default),
	                  list_view);

    menu_item = gtk_separator_menu_item_new ();
    gtk_menu_shell_append (GTK_MENU_SHELL (menu), menu_item);

    menu_item = gtk_check_menu_item_new_with_label (_("Temporarily disable auto-sort"));
    gtk_menu_shell_append (GTK_MENU_SHELL (menu), menu_item);

    gtk_check_menu_item_set_active (GTK_CHECK_MENU_ITEM (menu_item),
                                    nemo_list_model_get_temporarily_disable_sort (list_view->details->model));

    g_signal_connect (menu_item,
                      "activate",
                      G_CALLBACK (column_header_menu_disable_sort),
                      list_view);

	gtk_widget_show_all (menu);
	eel_gtk_menu_destroy_on_close (GTK_MENU (menu));
	gtk_menu_popup_for_device (GTK_MENU (menu),
	                           gdk_event_get_device ((GdkEvent *) event),
	                           NULL, NULL, NULL, NULL, NULL,
	                           event->button, event->time);

	return TRUE;
}

static void
apply_columns_settings (NemoListView *list_view,
			char **column_order,
			char **visible_columns)
{
	GList *all_columns;
	NemoFile *file;
	GList *old_view_columns, *view_columns;
	GHashTable *visible_columns_hash;
	GList *l;
    gint i;
    gboolean drop_where;

	file = nemo_view_get_directory_as_file (NEMO_VIEW (list_view));
	drop_where = grouping_search_results (list_view);

	/* prepare ordered list of view columns using column_order and visible_columns */
	view_columns = NULL;

	all_columns = nemo_get_columns_for_file (file);
	all_columns = nemo_sort_columns (all_columns, column_order);

	/* hash table to lookup if a given column should be visible */
	visible_columns_hash = g_hash_table_new_full (g_str_hash,
						      g_str_equal,
						      (GDestroyNotify) g_free,
						      (GDestroyNotify) g_free);
	for (i = 0; visible_columns[i] != NULL; ++i) {
		/* Grouped results already say where a match is, on the row above it. */
		if (drop_where && g_ascii_strcasecmp (visible_columns[i], "where") == 0) {
			continue;
		}

		g_hash_table_insert (visible_columns_hash,
				     g_ascii_strdown (visible_columns[i], -1),
				     g_ascii_strdown (visible_columns[i], -1));
	}

	for (l = all_columns; l != NULL; l = l->next) {
		char *name;
		char *lowercase;

		g_object_get (G_OBJECT (l->data), "name", &name, NULL);
		lowercase = g_ascii_strdown (name, -1);

		if (g_hash_table_lookup (visible_columns_hash, lowercase) != NULL) {
			GtkTreeViewColumn *view_column;

			view_column = g_hash_table_lookup (list_view->details->columns, name);
			if (view_column != NULL) {
				view_columns = g_list_prepend (view_columns, view_column);
			}
		}

		g_free (name);
		g_free (lowercase);
	}

	g_hash_table_destroy (visible_columns_hash);
	nemo_column_list_free (all_columns);

	view_columns = g_list_reverse (view_columns);

	/* hide columns that are not present in the configuration */
	old_view_columns = gtk_tree_view_get_columns (list_view->details->tree_view);
	for (l = old_view_columns; l != NULL; l = l->next) {
		if (g_list_find (view_columns, l->data) == NULL) {
			gtk_tree_view_column_set_visible (l->data, FALSE);
		}
	}
	g_list_free (old_view_columns);

    /* see bug: https://github.com/GNOME/gtk/commit/497e877755f1fa1
     * Explanation for branching - move_column_after generates useless logfile spam,
     * and to avoid it, simply removing and adding columns in a different order works
     * just as well.  The problem is, gtk versions < 3.22.25 lack the patch referenced
     * in the above bug report.  An additional problem is that different pre-3.22.25
     * versions behave differently depending on other code changes in GtkTreeViewColumn.
     * Mint 18 (gtk 3.18.9) using the add/remove column method would make a new button
     * widget upon reparenting, losing existing signal handlers.  In 3.22.11, however,
     * (debian stretch, LMDE3,) we get a nice segfault.
     *
     * This may seem a long way to go for a clean log, but the warnings can accumulate
     * quickly...
     */

    if (gtk_check_version (3, 22, 25) == NULL) {
        gint prev_view_column;

        prev_view_column = 0;
        for (l = view_columns; l != NULL; l = l->next) {
            g_signal_handlers_disconnect_by_func (gtk_tree_view_column_get_button (l->data),
                                                  column_header_clicked, list_view);

            gtk_tree_view_remove_column (list_view->details->tree_view, g_object_ref (l->data));
            gtk_tree_view_insert_column (list_view->details->tree_view, l->data, prev_view_column ++);

            g_signal_connect (gtk_tree_view_column_get_button (l->data),
                              "button-press-event",
                              G_CALLBACK (column_header_clicked),
                              list_view);

            gtk_tree_view_column_set_visible (l->data, TRUE);
            g_object_unref (l->data);
        }
    } else {
        GtkTreeViewColumn *prev_view_column;

        /* show new columns from the configuration */
        for (l = view_columns; l != NULL; l = l->next) {
            gtk_tree_view_column_set_visible (l->data, TRUE);
        }

        /* place columns in the correct order */
        prev_view_column = NULL;
        for (l = view_columns; l != NULL; l = l->next) {
            gtk_tree_view_move_column_after (list_view->details->tree_view, l->data, prev_view_column);
            prev_view_column = l->data;
        }
    }

    g_list_free (view_columns);

    /* A column arriving or leaving changes what the rest have to fit into, and a
       column that was hidden was never measured while it was. */
    remeasure_rows (list_view);
}

static GQuark
cell_plain_quark (void)
{
    static GQuark quark = 0;

    if (G_UNLIKELY (quark == 0)) {
        quark = g_quark_from_static_string ("nemo-cell-plain");
    }

    return quark;
}

/* Telling a renderer it has no background emits a notify whether or not that
   is news, and with shading off that is every cell of every redraw. Remember
   what each renderer was last handed instead. */
static void
cell_set_plain (GtkCellRenderer *renderer)
{
    if (g_object_get_qdata (G_OBJECT (renderer), cell_plain_quark ()) != NULL) {
        return;
    }

    g_object_set (renderer, "cell-background-set", FALSE, NULL);
    g_object_set_qdata (G_OBJECT (renderer), cell_plain_quark (), GINT_TO_POINTER (1));
}

/* GTK 3 still has the rules hint but no longer draws it, so the rows are
 * tinted from here. The renderer leaves its background off a selected row by
 * itself, and a see-through tint lets the hover highlight show under it.
 *
 * Called once per cell, so the parity is worked out for the first cell of a
 * row and the rest of that row read it back. shade_pass moves at the start of
 * every redraw and every row measured, which is when a row could have moved. */
static void
shade_row (NemoListView    *view,
           GtkCellRenderer *renderer,
           GtkTreeModel    *model,
           GtkTreeIter     *iter)
{
    GtkTreePath *path;
    GdkRectangle area;
    gint tree_y;
    gboolean tint;

    if (!view->details->row_shading) {
        cell_set_plain (renderer);
        return;
    }

    if (view->details->shade_node == iter->user_data &&
        view->details->shade_node_pass == view->details->shade_pass) {
        tint = view->details->shade_tint;
    } else {
        /* The row's place on screen rather than in the model, so rows an open
           subfolder adds take their turn like any other. */
        path = gtk_tree_model_get_path (model, iter);
        gtk_tree_view_get_background_area (view->details->tree_view, path, NULL, &area);
        gtk_tree_path_free (path);

        if (area.height <= 0) {
            cell_set_plain (renderer);
            return;
        }

        gtk_tree_view_convert_bin_window_to_tree_coords (view->details->tree_view,
                                                         0, area.y, NULL, &tree_y);

        tint = (tree_y / area.height) % 2 == 1;

        view->details->shade_node = iter->user_data;
        view->details->shade_node_pass = view->details->shade_pass;
        view->details->shade_tint = tint;
    }

    if (tint) {
        g_object_set (renderer, "cell-background-rgba", &view->details->row_shading_color, NULL);
        g_object_set_qdata (G_OBJECT (renderer), cell_plain_quark (), NULL);
    } else {
        cell_set_plain (renderer);
    }
}

static void
shade_row_cell_data_func (G_GNUC_UNUSED GtkTreeViewColumn *column,
                          GtkCellRenderer   *renderer,
                          GtkTreeModel      *model,
                          GtkTreeIter       *iter,
                          NemoListView      *view)
{
    shade_row (view, renderer, model, iter);
}

static void
row_shading_changed_callback (NemoListView *view)
{
    view->details->row_shading = nemo_config_get_boolean (nemo_list_view_preferences,
                                                          NEMO_PREFERENCES_LIST_VIEW_ROW_SHADING);
    nemo_row_shading_pick (GTK_WIDGET (view->details->tree_view),
                           &view->details->row_shading_color);
    gtk_widget_queue_draw (GTK_WIDGET (view->details->tree_view));
}

/* What a measured width depends on: the font the cells lay text out in, and
   the two theme sizes measure_row adds around it. */
static char *
measure_style_id (NemoListView *view)
{
    GtkStyleContext *context;
    PangoFontDescription *font = NULL;
    char *font_text;
    char *id;

    style_metrics (view);

    context = gtk_widget_get_style_context (GTK_WIDGET (view->details->tree_view));
    gtk_style_context_get (context, gtk_style_context_get_state (context),
                           GTK_STYLE_PROPERTY_FONT, &font,
                           NULL);

    font_text = font != NULL ? pango_font_description_to_string (font) : g_strdup ("");

    id = g_strdup_printf ("%s|%d|%d", font_text,
                          view->details->column_separator_px,
                          view->details->name_indent_step);

    g_free (font_text);
    if (font != NULL) {
        pango_font_description_free (font);
    }

    return id;
}

/* A theme change brings a new text color, maybe a nemo_row_shading, and new
   values for the sizes the measuring holds on to. It also fires for things
   that change nothing we measured - a state change, a CSS class going on or
   off - and remeasuring 50,000 rows on each of those would cost more than it
   saves, so the widths only go back for a real change. */
static void
tree_view_style_updated (NemoListView *view)
{
    char *id;

    view->details->style_metrics_valid = FALSE;

    id = measure_style_id (view);

    if (g_strcmp0 (id, view->details->measure_style_id) != 0) {
        g_free (view->details->measure_style_id);
        view->details->measure_style_id = id;
        remeasure_rows (view);
    } else {
        g_free (id);
    }

    row_shading_changed_callback (view);
}

/* Rows stand still for the length of one redraw, which is how long the parity
   worked out for a row is good for. */
static gboolean
tree_view_draw_callback (G_GNUC_UNUSED GtkWidget    *widget,
                         G_GNUC_UNUSED cairo_t      *cr,
                         NemoListView *view)
{
    view->details->shade_pass++;

    return FALSE;
}

static void
filename_cell_data_func (G_GNUC_UNUSED GtkTreeViewColumn *column,
			 GtkCellRenderer   *renderer,
			 GtkTreeModel      *model,
			 GtkTreeIter       *iter,
			 NemoListView        *view)
{
	char *text;
	GtkTreePath *path;
	PangoUnderline underline;
    gint weight;

	gtk_tree_model_get (model, iter,
			    view->details->file_name_column_num, &text,
                NEMO_LIST_MODEL_TEXT_WEIGHT_COLUMN, &weight,
			    -1);

	if (click_policy == NEMO_CLICK_POLICY_SINGLE) {
		path = gtk_tree_model_get_path (model, iter);

		if (view->details->hover_path == NULL ||
		    gtk_tree_path_compare (path, view->details->hover_path)) {
			underline = PANGO_UNDERLINE_NONE;
		} else {
			underline = PANGO_UNDERLINE_SINGLE;
		}

		gtk_tree_path_free (path);
	} else {
		underline = PANGO_UNDERLINE_NONE;
	}

	g_object_set (G_OBJECT (renderer),
		      "text", text,
		      "underline", underline,
              "weight", weight,
		      NULL);

    g_free (text);

    shade_row (view, renderer, model, iter);
}

static gboolean
focus_in_event_callback (G_GNUC_UNUSED GtkWidget *widget, G_GNUC_UNUSED GdkEventFocus *event, gpointer user_data)
{
	NemoWindowSlot *slot;
	NemoListView *list_view = NEMO_LIST_VIEW (user_data);

	/* make the corresponding slot (and the pane that contains it) active */
	slot = nemo_view_get_nemo_window_slot (NEMO_VIEW (list_view));
	nemo_window_slot_make_hosting_pane_active (slot);

	return FALSE;
}

static void
mark_visible_files (NemoListView *view)
{
    NemoFile *last_file;
    // GList *queue_list, *l;
    GdkRectangle vrect;
    GtkTreeIter iter;
    GtkTreePath *path;
    gint icon_size, cy, start_y, end_y, stepdown;
    gint bin_y;

    gtk_tree_view_get_visible_rect (view->details->tree_view,
                                    &vrect);
    icon_size = nemo_get_list_icon_size (nemo_list_view_get_icon_size (NEMO_VIEW (view)));

    gtk_tree_view_convert_tree_to_bin_window_coords(view->details->tree_view,
                                                    1, vrect.y,
                                                    NULL, &bin_y);

    stepdown = icon_size * .75;

    start_y = bin_y - (vrect.height / 2);
    end_y = bin_y + vrect.height + (vrect.height / 2);

    last_file = NULL;
    cy = start_y;

    /* Top down, since what is asked for first is read and made first. */
    while (cy < end_y) {
        if (gtk_tree_view_get_path_at_pos (view->details->tree_view,
                                           1, cy,
                                           &path, NULL, NULL, NULL)) {
            NemoFile *file;

            gtk_tree_model_get_iter (GTK_TREE_MODEL (view->details->model),
                                     &iter, path);

            gtk_tree_path_free (path);
            gtk_tree_model_get (GTK_TREE_MODEL (view->details->model),
                                &iter,
                                NEMO_LIST_MODEL_FILE_COLUMN, &file, -1);

            /* We'll catch some files twice, so filter them out */
            if (file != NULL && file != last_file) {
                last_file = file;

                if (nemo_file_get_load_deferred_attrs (file) == NEMO_FILE_LOAD_DEFERRED_ATTRS_NO) {
                    /* First time in view: mark it and pull deferred attrs once,
                     * rather than re-invalidating on every debounced scroll (the
                     * icon-container twin guards the same way). */
                    nemo_file_set_load_deferred_attrs (file, NEMO_FILE_LOAD_DEFERRED_ATTRS_YES);
                    nemo_file_invalidate_attributes (file, NEMO_FILE_DEFERRED_ATTRIBUTES);
                }
            }

            nemo_file_unref (file);
        }

        cy += stepdown;
    }
}

static gboolean
update_visible_icons_cb (NemoListView *view)
{
    mark_visible_files (view);

    view->details->update_visible_icons_id = 0;
    return G_SOURCE_REMOVE;
}

static void
queue_update_visible_icons(NemoListView *view,
                           gint          delay)
{
    if (view->details->update_visible_icons_id > 0) {
        g_source_remove (view->details->update_visible_icons_id);
    }

    view->details->update_visible_icons_id = g_timeout_add (delay, (GSourceFunc) update_visible_icons_cb, view);
}

static void
handle_vadjustment_changed (G_GNUC_UNUSED GtkAdjustment *adjustment,
                            NemoListView  *view)
{
    queue_update_visible_icons (view, NORMAL_UPDATE_VISIBLE_DELAY);
}

static gint
get_icon_scale_callback (G_GNUC_UNUSED NemoListModel *model,
                         NemoListView  *view)
{
   return gtk_widget_get_scale_factor (GTK_WIDGET (view->details->tree_view));
}

static void
on_treeview_realized (G_GNUC_UNUSED GtkWidget *widget,
                      gpointer   user_data)
{
    NemoListView *view = NEMO_LIST_VIEW (user_data);
    GtkAdjustment *adjust;

    adjust = gtk_scrollable_get_vadjustment (GTK_SCROLLABLE (view->details->tree_view));
    g_signal_connect (adjust,
                      "value-changed",
                      G_CALLBACK (handle_vadjustment_changed),
                      view);
}

/* About three characters of the view's own font, one character (the air every
   column gets on its right), and the ellipsis a cut value ends in. Measured
   once and kept, since they only change with the font, which takes the whole
   view down with it. */
static gint
column_floor_width (NemoListView *view)
{
	PangoLayout *layout;
	gint width = 0;
	gint one = 0;

	if (view->details->column_floor > 0) {
		return view->details->column_floor;
	}

	layout = gtk_widget_create_pango_layout (GTK_WIDGET (view->details->tree_view), "MMM");
	pango_layout_get_pixel_size (layout, &width, NULL);
	pango_layout_set_text (layout, "M", -1);
	pango_layout_get_pixel_size (layout, &one, NULL);
	pango_layout_set_text (layout, "\xe2\x80\xa6", -1);
	pango_layout_get_pixel_size (layout, &view->details->column_ellipsis, NULL);
	g_object_unref (layout);

	/* Plus what the cell puts either side of its text. */
	view->details->column_floor = MAX (16, width + 10);
	view->details->column_pad = MAX (6, one);
	view->details->column_ellipsis = MAX (1, view->details->column_ellipsis);

	return view->details->column_floor;
}

static gint
column_pad_width (NemoListView *view)
{
	column_floor_width (view);

	return view->details->column_pad;
}

static gint
column_ellipsis_width (NemoListView *view)
{
	column_floor_width (view);

	return view->details->column_ellipsis;
}

/* The theme sizes measuring a row needs: the gap the tree view leaves either
   side of a cell, and what one level of expander pushes the name over by.
   Read once, because a style_get costs real work and a folder load asks for
   these several times per row per column. Only a theme change moves them. */
static void
style_metrics (NemoListView *view)
{
	gint separator = 0;
	gint expander_size = 0;

	if (view->details->style_metrics_valid) {
		return;
	}

	gtk_widget_style_get (GTK_WIDGET (view->details->tree_view),
			      "horizontal-separator", &separator,
			      "expander-size", &expander_size,
			      NULL);

	view->details->column_separator_px = separator;
	view->details->name_indent_base = separator / 2;
	view->details->name_indent_step = expander_size + 4;
	view->details->style_metrics_valid = TRUE;
}

static gint
column_separator (NemoListView *view)
{
	style_metrics (view);

	return view->details->column_separator_px;
}

static const char *
column_id (GtkTreeViewColumn *column)
{
	return g_object_get_data (G_OBJECT (column), "column-id");
}

/* What one column has seen so far. Every column keeps its widest value. The
   columns whose width is a judgement rather than a fact - Name, and the ones
   with no natural length - keep every value too, so the width that shows most
   of them can be found. Name keeps one per file, since every name counts; a
   type or an owner keeps one per distinct text, so a value repeated down the
   folder counts once. A date or a size keeps only the widest, because it is
   never shown at less. */
typedef struct {
	GHashTable *values;	/* key -> width, or NULL for a column that only needs its widest */
	GHashTable *measured;	/* cell text -> that cell's natural width; NULL for Name */
	gint widest;
	gint fit;		/* cached, -1 once a value has changed */
	gint half;		/* the same for half the values */
	gint fit_percent;	/* the share the cached fit was worked out for */
} ColumnSamples;

/* How many distinct values one column will remember the width of. A type or an
   owner repeats down the whole folder and needs a handful of entries; a date is
   nearly all distinct and would otherwise keep one per row for nothing. Past
   this the column just measures, which is what it did before. */
#define MEASURED_TEXTS_MAX 2048

static void
column_samples_free (gpointer data)
{
	ColumnSamples *samples = data;

	if (samples->values != NULL) {
		g_hash_table_destroy (samples->values);
	}
	if (samples->measured != NULL) {
		g_hash_table_destroy (samples->measured);
	}
	g_free (samples);
}

static gboolean
column_keeps_every_value (NemoListView      *view,
			  GtkTreeViewColumn *column)
{
	return column == view->details->file_name_column ||
	       g_object_get_data (G_OBJECT (column), "unbounded") != NULL;
}

static ColumnSamples *
samples_for (NemoListView      *view,
	     GtkTreeViewColumn *column)
{
	const char *id = column_id (column);
	ColumnSamples *samples;

	if (id == NULL) {
		return NULL;
	}

	samples = g_hash_table_lookup (view->details->samples, id);
	if (samples == NULL) {
		samples = g_new0 (ColumnSamples, 1);
		samples->fit = -1;

		if (column == view->details->file_name_column) {
			samples->values = g_hash_table_new (g_direct_hash, g_direct_equal);
		} else {
			/* Every column but Name shows one run of text per row, so a value
			   seen before lays out to the same width. Name is left out: no two
			   files in a folder share a name, so nothing would ever repeat.
			   NEMO_MEASURE_NOCACHE turns it off, which is how the widths it
			   hands back are checked against measuring every row. */
			if (g_getenv ("NEMO_MEASURE_NOCACHE") == NULL) {
				samples->measured = g_hash_table_new_full (g_str_hash, g_str_equal,
									   g_free, NULL);
			}

			if (column_keeps_every_value (view, column)) {
				samples->values = g_hash_table_new_full (g_str_hash, g_str_equal,
									 g_free, NULL);
			}
		}

		g_hash_table_insert (view->details->samples, g_strdup (id), samples);
	}

	return samples;
}

/* Fold one value in. `file` keys a name and `text` keys anything else; a
   column that keeps only its widest ignores both. TRUE when something the
   layout reads has changed. */
static gboolean
note_sample (ColumnSamples *samples,
	     NemoFile      *file,
	     const char    *text,
	     gint           width)
{
	gboolean changed = FALSE;
	gpointer key;
	gpointer seen;

	if (samples == NULL) {
		return FALSE;
	}

	if (samples->values == NULL) {
		if (width > samples->widest) {
			samples->widest = width;
			changed = TRUE;
		}
		return changed;
	}

	key = file != NULL ? (gpointer) file : (gpointer) text;
	if (key == NULL) {
		return FALSE;
	}

	if (!g_hash_table_lookup_extended (samples->values, key, NULL, &seen) ||
	    GPOINTER_TO_INT (seen) != width) {
		/* The file is only ever a key, never followed, so a file that has
		   gone leaves a stale entry and nothing worse. The text is copied. */
		g_hash_table_insert (samples->values,
				     file != NULL ? key : g_strdup (text),
				     GINT_TO_POINTER (width));
		samples->fit = -1;
		changed = TRUE;
	}

	return changed;
}

/* The width that shows `percent` of what the column has seen, the width that
   shows half of it, and the width that shows all of it. Worked out from the
   values on demand and kept until one changes, so a window being resized does
   not sort a folder per frame. */
static void
samples_measure (ColumnSamples *samples,
		 gint           percent,
		 gint          *fit,
		 gint          *half,
		 gint          *widest)
{
	if (samples == NULL) {
		*fit = 0;
		*half = 0;
		*widest = 0;
		return;
	}

	if (samples->values != NULL &&
	    (samples->fit < 0 || samples->fit_percent != percent)) {
		guint n = g_hash_table_size (samples->values);
		int *widths = g_new (int, MAX (n, 1));
		GHashTableIter iter;
		gpointer value;
		guint i = 0;

		samples->widest = 0;
		g_hash_table_iter_init (&iter, samples->values);
		while (g_hash_table_iter_next (&iter, NULL, &value)) {
			widths[i++] = GPOINTER_TO_INT (value);
			samples->widest = MAX (samples->widest, GPOINTER_TO_INT (value));
		}

		samples->fit = nemo_column_layout_fit (widths, (int) n, percent);
		samples->half = nemo_column_layout_fit (widths, (int) n, 50);
		samples->fit_percent = percent;
		g_free (widths);
	}

	*fit = samples->values != NULL ? samples->fit : samples->widest;
	*half = samples->values != NULL ? samples->half : samples->widest;
	*widest = samples->widest;
}

/* A file leaving the folder takes its name out of the reckoning, or a folder
   emptied of its long names would keep the width they asked for. What it held
   in the other columns stays until resample_rows_cb, since those are kept by
   text and another file may share it. */
static void
drop_name_sample (NemoListView *view,
		  NemoFile     *file)
{
	ColumnSamples *samples;

	if (view->details->samples == NULL) {
		return;
	}

	view->details->samples_stale = TRUE;

	samples = g_hash_table_lookup (view->details->samples, "name");
	if (samples != NULL && samples->values != NULL &&
	    g_hash_table_remove (samples->values, file)) {
		samples->fit = -1;
	}

	resize_columns_soon (view);
}

static void
forget_samples (NemoListView *view)
{
	/* Whatever called this measures again from nothing, or has a new folder. */
	if (view->details->resample_id != 0) {
		g_source_remove (view->details->resample_id);
		view->details->resample_id = 0;
	}

	if (view->details->samples != NULL) {
		g_hash_table_remove_all (view->details->samples);
	}
	view->details->samples_stale = FALSE;

	/* So the next allocation is not mistaken for one that changed nothing. */
	view->details->laid_out_width = -1;
}

static NemoColumnKind
column_class (NemoListView      *view,
	      GtkTreeViewColumn *column)
{
	if (column == view->details->file_name_column ||
	    g_strcmp0 (column_id (column), "where") == 0) {
		return NEMO_COLUMN_KIND_PRIMARY;
	}

	if (g_object_get_data (G_OBJECT (column), "unbounded") != NULL) {
		return NEMO_COLUMN_KIND_MINOR;
	}

	return NEMO_COLUMN_KIND_FIXED;
}

/* A dragged minor column is saved with the folder's settings, as
   column:pixels, and only while "Remember per-folder settings" is on. Name,
   Location and the fixed columns are never saved; a drag on them lasts until
   the folder changes. */
static void
load_user_widths (NemoListView *view)
{
	NemoFile *file;
	GList *saved, *l;

	g_hash_table_remove_all (view->details->user_widths);

	file = nemo_view_get_directory_as_file (NEMO_VIEW (view));
	if (file == NULL || nemo_file_is_in_search (file)) {
		return;
	}

	saved = nemo_folder_settings_get_list (file, NEMO_METADATA_KEY_LIST_VIEW_COLUMN_WIDTHS);

	for (l = saved; l != NULL; l = l->next) {
		const char *entry = l->data;
		const char *colon = strchr (entry, ':');
		gint64 width;

		if (colon == NULL || colon == entry) {
			continue;
		}

		width = g_ascii_strtoll (colon + 1, NULL, 10);
		if (width <= 0 || width > G_MAXINT16) {
			continue;
		}

		g_hash_table_insert (view->details->user_widths,
				     g_strndup (entry, colon - entry),
				     GINT_TO_POINTER ((gint) width));
	}

	g_list_free_full (saved, g_free);
}

static void
save_user_widths (NemoListView *view)
{
	NemoFile *file;
	GList *ids, *l;
	GList *entries = NULL;

	file = nemo_view_get_directory_as_file (NEMO_VIEW (view));
	if (file == NULL || nemo_file_is_in_search (file)) {
		return;
	}

	/* Sorted, so the same widths always write the same list. */
	ids = g_list_sort (g_hash_table_get_keys (view->details->user_widths),
			   (GCompareFunc) g_strcmp0);

	for (l = ids; l != NULL; l = l->next) {
		GtkTreeViewColumn *column = g_hash_table_lookup (view->details->columns, l->data);

		if (column == NULL || column_class (view, column) != NEMO_COLUMN_KIND_MINOR) {
			continue;
		}

		entries = g_list_prepend (entries,
					  g_strdup_printf ("%s:%d", (char *) l->data,
							   GPOINTER_TO_INT (g_hash_table_lookup (view->details->user_widths,
												 l->data))));
	}

	entries = g_list_reverse (entries);
	nemo_folder_settings_set_list (file, NEMO_METADATA_KEY_LIST_VIEW_COLUMN_WIDTHS, entries);

	g_list_free_full (entries, g_free);
	g_list_free (ids);
}

/* A drag is a stream of width changes; the decision is made when it stops. */
#define USER_WIDTH_SETTLE_MSEC 350

static gboolean
user_widths_settled (gpointer user_data)
{
	NemoListView *view = NEMO_LIST_VIEW (user_data);
	gboolean others = FALSE;
	gboolean changed = FALSE;
	gboolean save = FALSE;
	GHashTableIter iter;
	gpointer key, value;

	view->details->user_width_settle_id = 0;

	if (view->details->tree_view == NULL ||
	    view->details->pending_user_widths == NULL) {
		return G_SOURCE_REMOVE;
	}

	/* GTK rewrites the expanding column while another one is dragged, so a
	   change to Name is a drag on Name only when nothing else changed. */
	g_hash_table_iter_init (&iter, view->details->pending_user_widths);
	while (g_hash_table_iter_next (&iter, &key, NULL)) {
		if (g_strcmp0 (key, "name") != 0) {
			others = TRUE;
		}
	}

	g_hash_table_iter_init (&iter, view->details->pending_user_widths);
	while (g_hash_table_iter_next (&iter, &key, &value)) {
		GtkTreeViewColumn *column = g_hash_table_lookup (view->details->columns, key);
		NemoColumnKind class;

		if (column == NULL ||
		    (others && column == view->details->file_name_column)) {
			continue;
		}

		class = column_class (view, column);
		if (class == NEMO_COLUMN_KIND_FIXED) {
			continue;
		}

		g_hash_table_insert (view->details->user_widths, g_strdup (key), value);
		changed = TRUE;
		save = save || class == NEMO_COLUMN_KIND_MINOR;
	}

	g_hash_table_remove_all (view->details->pending_user_widths);

	if (save) {
		save_user_widths (view);
	}

	if (changed) {
		view->details->laid_out_width = -1;
		resize_columns_soon (view);
	}

	return G_SOURCE_REMOVE;
}

static void
column_fixed_width_notify (GObject    *object,
			   G_GNUC_UNUSED GParamSpec *pspec,
			   gpointer    user_data)
{
	NemoListView *view = NEMO_LIST_VIEW (user_data);
	GtkTreeViewColumn *column = GTK_TREE_VIEW_COLUMN (object);
	const char *id;

	GdkWindow *window;
	GdkModifierType mask = 0;

	/* Every width set here arrives under applying_layout - but GTK itself
	   also rewrites the expanding column's width while allocating, so ours
	   being absent is not enough. A drag has the button held; anything with
	   no button down is machinery, not the user. */
	if (view->details->applying_layout ||
	    view->details->tree_view == NULL ||
	    view->details->laid_out_width <= 0 ||
	    !gtk_widget_get_realized (GTK_WIDGET (view->details->tree_view))) {
		return;
	}

	window = gtk_widget_get_window (GTK_WIDGET (view->details->tree_view));
	if (window != NULL) {
		GdkDisplay *display = gdk_window_get_display (window);
		GdkSeat *seat = gdk_display_get_default_seat (display);

		gdk_window_get_device_position (window, gdk_seat_get_pointer (seat),
						NULL, NULL, &mask);
	}
	if ((mask & GDK_BUTTON1_MASK) == 0) {
		return;
	}

	id = column_id (column);
	if (id == NULL) {
		return;
	}

	if (view->details->pending_user_widths == NULL) {
		view->details->pending_user_widths =
			g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);
	}

	g_hash_table_insert (view->details->pending_user_widths, g_strdup (id),
			     GINT_TO_POINTER (gtk_tree_view_column_get_fixed_width (column)));

	if (view->details->user_width_settle_id != 0) {
		g_source_remove (view->details->user_width_settle_id);
	}
	view->details->user_width_settle_id =
		g_timeout_add (USER_WIDTH_SETTLE_MSEC, user_widths_settled, view);
}

/* What the expanders push the name over by on this row. Part of how wide the
   Name column has to be, and it is the deepest row that decides. */
static gint
name_indent_for (NemoListView *view,
		 GtkTreePath  *path)
{
	if (path == NULL ||
	    !gtk_tree_view_get_show_expanders (view->details->tree_view)) {
		return 0;
	}

	style_metrics (view);

	return view->details->name_indent_base +
	       gtk_tree_path_get_depth (path) * view->details->name_indent_step;
}

/* What this column would want to be to show this row whole - and, when asked,
 * the text it shows there, for the columns that count each distinct value once.
 *
 * Deliberately NOT gtk_tree_view_column_cell_get_size, which answers with the
 * cell's MINIMUM width - and a cell that may ellipsize has almost none, so
 * every column measured that way collapses to a few pixels. The renderers are
 * asked for their natural widths instead, which is the full text either way. */
static gint
row_width_for_column (NemoListView      *view,
		      GtkTreeViewColumn *column,
		      ColumnSamples     *samples,
		      GtkTreeModel      *model,
		      GtkTreeIter       *iter,
		      gchar            **text)
{
	GHashTable *measured = samples != NULL ? samples->measured : NULL;
	GList *cells, *c;
	gint total = 0;
	gint shown = 0;

	gtk_tree_view_column_cell_set_cell_data (column, model, iter, FALSE, FALSE);

	cells = gtk_cell_layout_get_cells (GTK_CELL_LAYOUT (column));

	for (c = cells; c != NULL; c = c->next) {
		gchar *cell_text = NULL;
		gpointer seen = NULL;
		gboolean may_cache = FALSE;
		gboolean known = FALSE;
		gint natural = 0;

		if (!gtk_cell_renderer_get_visible (c->data)) {
			continue;
		}

		/* Name asks for neither, so it does not pay for the copy. */
		if (GTK_IS_CELL_RENDERER_TEXT (c->data) &&
		    (measured != NULL || (text != NULL && *text == NULL))) {
			gint weight = NORMAL_TEXT_WEIGHT;

			g_object_get (c->data, "text", &cell_text, "weight", &weight, NULL);

			/* A file shown in a weight of its own lays out wider or narrower at
			   the same text, and there are only ever a few, so those are
			   measured rather than remembered. */
			may_cache = measured != NULL && cell_text != NULL &&
				    weight == NORMAL_TEXT_WEIGHT;
		}

		if (may_cache) {
			known = g_hash_table_lookup_extended (measured, cell_text, NULL, &seen);
		}

		if (known) {
			natural = GPOINTER_TO_INT (seen);
		} else {
			gtk_cell_renderer_get_preferred_width (c->data,
							       GTK_WIDGET (view->details->tree_view),
							       NULL, &natural);

			if (may_cache && g_hash_table_size (measured) < MEASURED_TEXTS_MAX) {
				g_hash_table_insert (measured, g_strdup (cell_text),
						     GINT_TO_POINTER (natural));
			}
		}

		total += natural;
		shown++;

		if (text != NULL && *text == NULL && cell_text != NULL) {
			*text = cell_text;
			cell_text = NULL;
		}

		g_free (cell_text);
	}

	g_list_free (cells);

	if (shown > 1) {
		total += gtk_tree_view_column_get_spacing (column) * (shown - 1);
	}

	/* What the tree view itself puts between one column and the next. */
	return total + column_separator (view);
}

/* The columns in order. Kept, because measuring asks for them once per row and
   the tree view says when the set changes. */
static GList *
measure_columns (NemoListView *view)
{
	if (view->details->measure_columns == NULL) {
		view->details->measure_columns =
			gtk_tree_view_get_columns (view->details->tree_view);
	}

	return view->details->measure_columns;
}

static void
columns_changed_callback (NemoListView *view)
{
	g_clear_pointer (&view->details->measure_columns, g_list_free);
}

/* One row through every visible column, folded into what each has seen. Called
   as rows arrive and as their details fill in, which is a handful of cells at a
   time rather than a walk of the whole folder. */
static void
measure_row (GtkTreeModel *model,
	     GtkTreePath  *path,
	     GtkTreeIter  *iter,
	     gpointer      user_data)
{
	NemoListView *view = NEMO_LIST_VIEW (user_data);
	NemoFile *file = NULL;
	GList *l;
	gboolean grew = FALSE;

	if (view->details->tree_view == NULL) {
		return;
	}

	/* Setting cell data below runs the shading func, and this is one row, so
	   whatever it works out there is good for the rest of this call only. */
	view->details->shade_pass++;

	gtk_tree_model_get (model, iter, NEMO_LIST_MODEL_FILE_COLUMN, &file, -1);

	for (l = measure_columns (view); l != NULL; l = l->next) {
		GtkTreeViewColumn *column = l->data;
		gboolean is_name = column == view->details->file_name_column;
		gboolean by_text = !is_name && column_keeps_every_value (view, column);
		ColumnSamples *samples;
		gchar *text = NULL;
		gint width;

		if (!gtk_tree_view_column_get_visible (column)) {
			continue;
		}

		samples = samples_for (view, column);
		width = row_width_for_column (view, column, samples, model, iter,
					      by_text ? &text : NULL);

		if (is_name) {
			width += name_indent_for (view, path);
		}

		if (note_sample (samples, is_name ? file : NULL, text, width)) {
			grew = TRUE;
		}

		g_free (text);
	}

	nemo_file_unref (file);

	if (grew) {
		resize_columns_soon (view);
	}
}

static gboolean
measure_row_foreach (GtkTreeModel *model,
		     GtkTreePath  *path,
		     GtkTreeIter  *iter,
		     gpointer      user_data)
{
	measure_row (model, path, iter, user_data);

	return FALSE;
}

/* Every row again, from nothing. The incremental measure covers rows as they
   arrive, which is not enough when what a row measures has changed under it -
   a zoom, or a column that was not on screen the first time round. */
static void
remeasure_rows (NemoListView *view)
{
	forget_samples (view);

	if (view->details->model != NULL) {
		gtk_tree_model_foreach (GTK_TREE_MODEL (view->details->model),
					measure_row_foreach, view);
	}

	resize_columns_soon (view);
}

/* Samples only ever grow, so files that leave a folder leave their widths
   behind. A size or a type that is gone can hold its column wide enough to
   put a scrollbar on a folder that no longer needs one, emptied or not. So
   when the minimums overflow and files have left since the last full count,
   what is left is sampled again. Only then: it is a walk of the folder, about
   a third of a second at five thousand rows. The width worked out for each
   text stays, since the text itself has not changed. */
static gboolean
resample_rows_cb (gpointer user_data)
{
	NemoListView *view = NEMO_LIST_VIEW (user_data);
	GHashTableIter iter;
	gpointer value;

	view->details->resample_id = 0;
	view->details->samples_stale = FALSE;

	if (view->details->samples != NULL) {
		g_hash_table_iter_init (&iter, view->details->samples);
		while (g_hash_table_iter_next (&iter, NULL, &value)) {
			ColumnSamples *samples = value;

			if (samples->values != NULL) {
				g_hash_table_remove_all (samples->values);
			}
			samples->widest = 0;
			samples->fit = -1;
		}
	}

	if (view->details->model != NULL) {
		gtk_tree_model_foreach (GTK_TREE_MODEL (view->details->model),
					measure_row_foreach, view);
	}

	resize_columns_soon (view);

	return G_SOURCE_REMOVE;
}

/* Put off while files keep going and the layout keeps coming back to it, so
   a big delete walks the folder once, at the end. */
static void
resample_rows_soon (NemoListView *view)
{
	if (view->details->resample_id != 0) {
		g_source_remove (view->details->resample_id);
	}
	view->details->resample_id = g_timeout_add (300, resample_rows_cb, view);
}

/* Hand every visible column a width for a view `available` wide. Between them
   they come to exactly that wherever the minimums allow it, and to more where
   they do not, in which case the view scrolls sideways. The rule itself is in
   nemo-column-layout.c; this is the measuring that feeds it. */
static void
layout_columns (NemoListView *view,
		gint          available)
{
	NemoColumnLayoutItem *items;
	GtkTreeViewColumn **columns;
	gint *widths;
	GList *all, *l;
	gint n_columns = 0;
	gint percent;
	gint pad;
	gint ellipsis;
	gint narrow;
	gint i = 0;

	all = gtk_tree_view_get_columns (view->details->tree_view);

	for (l = all; l != NULL; l = l->next) {
		if (gtk_tree_view_column_get_visible (l->data)) {
			n_columns++;
		}
	}

	if (n_columns == 0) {
		g_list_free (all);
		return;
	}

	percent = CLAMP (nemo_config_get_int (nemo_list_view_preferences,
					      NEMO_PREFERENCES_LIST_VIEW_COLUMN_FIT_PERCENT),
			 1, 100);
	pad = column_pad_width (view);
	ellipsis = column_ellipsis_width (view);

	/* A minor column whose values are all about four characters or less, like
	   Ext, is too narrow for an ellipsis to leave anything readable. */
	narrow = column_floor_width (view) + pad;

	items = g_new0 (NemoColumnLayoutItem, n_columns);
	columns = g_new0 (GtkTreeViewColumn *, n_columns);
	widths = g_new0 (gint, n_columns);

	for (l = all; l != NULL; l = l->next) {
		GtkTreeViewColumn *column = l->data;
		GtkWidget *button;
		NemoColumnMeasure measure = { 0, 0, 0, 0 };
		gpointer dragged;

		if (!gtk_tree_view_column_get_visible (column)) {
			continue;
		}

		columns[i] = column;

		samples_measure (samples_for (view, column), percent,
				 &measure.fit, &measure.half, &measure.widest);

		button = gtk_tree_view_column_get_button (column);
		if (button != NULL) {
			gtk_widget_get_preferred_width (button, NULL, &measure.heading);
		}

		nemo_column_layout_item_for_kind (column_class (view, column), &measure,
						  pad, ellipsis, narrow,
						  g_hash_table_lookup_extended (view->details->user_widths,
										column_id (column), NULL, &dragged)
						  ? GPOINTER_TO_INT (dragged) : -1,
						  &items[i]);

		i++;
	}

	nemo_column_layout_distribute (items, n_columns, available, widths);

	if (view->details->samples_stale) {
		gint least = 0;

		for (i = 0; i < n_columns; i++) {
			least += items[i].min_width;
		}
		if (least > available) {
			resample_rows_soon (view);
		}
	}

	view->details->applying_layout = TRUE;
	for (i = 0; i < n_columns; i++) {
		if (gtk_tree_view_column_get_fixed_width (columns[i]) != widths[i]) {
			gtk_tree_view_column_set_fixed_width (columns[i], widths[i]);
		}
	}
	view->details->applying_layout = FALSE;

	view->details->laid_out_width = available;

	g_free (items);
	g_free (columns);
	g_free (widths);
	g_list_free (all);
}

/* The columns for the width the tree view has now. */
static void
resize_columns_now (NemoListView *view)
{
	GtkAllocation allocation;

	if (view->details->tree_view == NULL ||
	    !gtk_widget_get_realized (GTK_WIDGET (view->details->tree_view))) {
		return;
	}

	gtk_widget_get_allocation (GTK_WIDGET (view->details->tree_view), &allocation);
	if (allocation.width <= 1) {
		return;
	}

	layout_columns (view, allocation.width);
}

static gboolean
resize_columns_cb (gpointer user_data)
{
	NemoListView *view = NEMO_LIST_VIEW (user_data);

	view->details->resize_columns_id = 0;
	resize_columns_now (view);

	return G_SOURCE_REMOVE;
}

/* Never from inside the tree view's own allocation - setting a width asks for
   another one. */
static void
resize_columns_soon (NemoListView *view)
{
	if (view->details->resize_columns_id == 0) {
		view->details->resize_columns_id =
			g_idle_add_full (G_PRIORITY_HIGH_IDLE, resize_columns_cb, view, NULL);
	}
}

/* The columns are laid out for the width the tree view is about to get, before
   it gets it, so a resize never draws a frame with the old widths - that is
   what put a scrollbar on screen for a moment at every step of a resize. The
   width is ours less what the tree view was inset by last time (edge padding,
   and a real vertical scrollbar where the theme has one). Wrong only in the
   frame a scrollbar appears or goes, and the allocation handler below catches
   that as it always did, and corrects the inset for the next time. */
static void
nemo_list_view_size_allocate (GtkWidget     *widget,
			      GtkAllocation *allocation)
{
	NemoListView *view = NEMO_LIST_VIEW (widget);

	view->details->own_width = allocation->width;

	if (view->details->tree_inset >= 0 &&
	    view->details->tree_view != NULL &&
	    gtk_widget_get_realized (GTK_WIDGET (view->details->tree_view))) {
		gint width = allocation->width - view->details->tree_inset;

		if (width > 1 && width != view->details->laid_out_width) {
			layout_columns (view, width);
		}
	}

	GTK_WIDGET_CLASS (nemo_list_view_parent_class)->size_allocate (widget, allocation);
}

static void
on_size_allocation_changed (G_GNUC_UNUSED GtkWidget    *widget,
                            GdkRectangle *allocation,
                            gpointer      user_data)
{
    NemoListView *view = NEMO_LIST_VIEW (user_data);
    GtkAdjustment *adjustment;
    gdouble page_size, upper;
    gint margin = 0;

    adjustment = gtk_scrollable_get_hadjustment (GTK_SCROLLABLE (view->details->tree_view));
    g_object_get (adjustment, "page-size", &page_size, "upper", &upper, NULL);

    if (view->details->own_width > 0) {
        view->details->tree_inset = view->details->own_width - allocation->width;
    }

    /* An overlay scrollbar sits on top of the last row, so make room for it
       under the rows while there is one. Only when the margin really changes:
       setting it asks for another allocation, and asking on every one was a
       loop that redrew the view at the frame rate. */
    if (view->details->overlay_scrolling && page_size < upper) {
        GtkWidget *hscrollbar = gtk_scrolled_window_get_hscrollbar (GTK_SCROLLED_WINDOW (view));
        gint nat_height;

        gtk_widget_get_preferred_height (hscrollbar, NULL, &nat_height);
        margin = nat_height + 2;
    }
    if (gtk_widget_get_margin_bottom (GTK_WIDGET (view->details->tree_view)) != margin) {
        gtk_widget_set_margin_bottom (GTK_WIDGET (view->details->tree_view), margin);
    }

    /* Only when the width really moved: a column dragged wider by hand should
       survive until the window changes shape or the folder does, and every
       width we set here comes straight back round as another allocation. */
    if (allocation->width != view->details->laid_out_width) {
        view->details->laid_out_width = allocation->width;
        resize_columns_soon (view);
    }
}

static void
create_and_set_up_tree_view (NemoListView *view)
{
	GtkCellRenderer *cell;
	GtkTreeViewColumn *column;
	GtkBindingSet *binding_set;
	AtkObject *atk_obj;
	GList *nemo_columns;
	GList *l;
	gchar **default_column_order, **default_visible_columns;

	view->details->tree_view = GTK_TREE_VIEW (gtk_tree_view_new ());

    /* Headings and rows both sit inside this, and the width the columns are laid
       out against is what is left of the view after it. */
    gtk_widget_set_margin_start (GTK_WIDGET (view->details->tree_view), LIST_VIEW_EDGE_PADDING);
    gtk_widget_set_margin_end (GTK_WIDGET (view->details->tree_view), LIST_VIEW_EDGE_PADDING);

    gtk_tree_view_set_rubber_banding (GTK_TREE_VIEW (view->details->tree_view), TRUE);

    gtk_tree_view_set_show_expanders (view->details->tree_view,
                                      nemo_config_get_boolean (nemo_list_view_preferences,
                                                              NEMO_PREFERENCES_LIST_VIEW_ENABLE_EXPANSION));
    g_signal_connect_object (nemo_list_view_preferences,
                             "changed::" NEMO_PREFERENCES_LIST_VIEW_ENABLE_EXPANSION,
                             G_CALLBACK (expanders_enabled_changed_cb),
                             view, G_CONNECT_SWAPPED);

    row_shading_changed_callback (view);
    g_signal_connect_object (nemo_list_view_preferences,
                             "changed::" NEMO_PREFERENCES_LIST_VIEW_ROW_SHADING,
                             G_CALLBACK (row_shading_changed_callback),
                             view, G_CONNECT_SWAPPED);
    g_signal_connect_object (nemo_list_view_preferences,
                             "changed::" NEMO_PREFERENCES_LIST_VIEW_ROW_SHADING_COLOR,
                             G_CALLBACK (row_shading_changed_callback),
                             view, G_CONNECT_SWAPPED);
    g_signal_connect_swapped (view->details->tree_view, "style-updated",
                              G_CALLBACK (tree_view_style_updated),
                              view);
    nemo_row_hover_attach (GTK_WIDGET (view->details->tree_view));
    g_signal_connect (view->details->tree_view, "draw",
                      G_CALLBACK (tree_view_draw_callback), view);
    g_signal_connect_swapped (view->details->tree_view, "columns-changed",
                              G_CALLBACK (columns_changed_callback), view);

	view->details->columns = g_hash_table_new_full (g_str_hash,
							g_str_equal,
							(GDestroyNotify) g_free,
							NULL);

	view->details->samples = g_hash_table_new_full (g_str_hash,
							g_str_equal,
							(GDestroyNotify) g_free,
							column_samples_free);
	view->details->user_widths = g_hash_table_new_full (g_str_hash, g_str_equal,
							    g_free, NULL);
	view->details->laid_out_width = -1;
	view->details->tree_inset = -1;

	gtk_tree_view_set_enable_search (view->details->tree_view, TRUE);

	/* Don't handle backspace key. It's used to open the parent folder. */
	binding_set = gtk_binding_set_by_class (GTK_WIDGET_GET_CLASS (view->details->tree_view));
	gtk_binding_entry_remove (binding_set, GDK_KEY_BackSpace, 0);

	view->details->drag_dest =
		nemo_tree_view_drag_dest_new (view->details->tree_view, TRUE);

	g_signal_connect_object (view->details->drag_dest,
				 "get_root_uri",
				 G_CALLBACK (get_root_uri_callback),
				 view, 0);
	g_signal_connect_object (view->details->drag_dest,
				 "get_file_for_path",
				 G_CALLBACK (get_file_for_path_callback),
				 view, 0);
	g_signal_connect_object (view->details->drag_dest,
				 "move_copy_items",
				 G_CALLBACK (move_copy_items_callback),
				 view, 0);
	g_signal_connect_object (view->details->drag_dest, "handle_netscape_url",
				 G_CALLBACK (list_view_handle_netscape_url), view, 0);
	g_signal_connect_object (view->details->drag_dest, "handle_uri_list",
				 G_CALLBACK (list_view_handle_uri_list), view, 0);
	g_signal_connect_object (view->details->drag_dest, "handle_text",
				 G_CALLBACK (list_view_handle_text), view, 0);
	g_signal_connect_object (view->details->drag_dest, "handle_raw",
				 G_CALLBACK (list_view_handle_raw), view, 0);

	g_signal_connect_object (gtk_tree_view_get_selection (view->details->tree_view),
				 "changed",
				 G_CALLBACK (list_selection_changed_callback), view, 0);
	g_signal_connect_object (view->details->tree_view, "cursor-changed",
				 G_CALLBACK (cursor_changed_callback), view, 0);

    g_signal_connect_object (GTK_WIDGET (view->details->tree_view), "query-tooltip",
                             G_CALLBACK (query_tooltip_callback), view, 0);

    g_signal_connect_object (view->details->tree_view, "drag_begin",
                 G_CALLBACK (drag_begin_callback), view, 0);
    g_signal_connect_object (view->details->tree_view, "drag-end",
                 G_CALLBACK (drag_end_callback), view, 0);
	g_signal_connect_object (view->details->tree_view, "drag_data_get",
				 G_CALLBACK (drag_data_get_callback), view, 0);
	g_signal_connect_object (view->details->tree_view, "motion_notify_event",
				 G_CALLBACK (motion_notify_callback), view, 0);
	g_signal_connect_object (view->details->tree_view, "enter_notify_event",
				 G_CALLBACK (enter_notify_callback), view, 0);
	g_signal_connect_object (view->details->tree_view, "leave_notify_event",
				 G_CALLBACK (leave_notify_callback), view, 0);
	g_signal_connect_object (view->details->tree_view, "button_press_event",
				 G_CALLBACK (button_press_callback), view, 0);
	g_signal_connect_object (view->details->tree_view, "button_release_event",
				 G_CALLBACK (button_release_callback), view, 0);
	g_signal_connect_object (view->details->tree_view, "key_press_event",
				 G_CALLBACK (key_press_callback), view, 0);
	g_signal_connect_object (view->details->tree_view, "popup_menu",
                                 G_CALLBACK (popup_menu_callback), view, 0);
	g_signal_connect_object (view->details->tree_view, "row_expanded",
                                 G_CALLBACK (row_expanded_callback), view, 0);
	g_signal_connect_object (view->details->tree_view, "row_collapsed",
                                 G_CALLBACK (row_collapsed_callback), view, 0);
	g_signal_connect_object (view->details->tree_view, "row-activated",
                                 G_CALLBACK (row_activated_callback), view, 0);
    g_signal_connect_object (view->details->tree_view, "test-expand-row",
                                 G_CALLBACK (test_expand_row_callback), view, 0);

    	g_signal_connect_object (view->details->tree_view, "focus_in_event",
				 G_CALLBACK(focus_in_event_callback), view, 0);

    g_signal_connect (view->details->tree_view, "realize", G_CALLBACK (on_treeview_realized), view);
    g_signal_connect (view->details->tree_view, "size-allocate", G_CALLBACK (on_size_allocation_changed), view);

	view->details->model = g_object_new (NEMO_TYPE_LIST_MODEL, NULL);
	gtk_tree_view_set_model (view->details->tree_view, GTK_TREE_MODEL (view->details->model));
	/* Need the model for the dnd drop icon "accept" change */
	nemo_list_model_set_drag_view (NEMO_LIST_MODEL (view->details->model),
				     view->details->tree_view,  0, 0);

	/* Widths follow the contents, so every row that arrives or changes is
	 * folded into the widest seen. One row through a handful of columns, never
	 * a walk of the whole folder. */
	g_signal_connect_object (view->details->model, "row-inserted",
				 G_CALLBACK (measure_row), view, 0);
	g_signal_connect_object (view->details->model, "row-changed",
				 G_CALLBACK (measure_row), view, 0);

	g_signal_connect_object (view->details->model, "sort_column_changed",
				 G_CALLBACK (sort_column_changed_callback), view, 0);

	g_signal_connect_object (view->details->model, "subdirectory_unloaded",
				 G_CALLBACK (subdirectory_unloaded_callback), view, 0);

    g_signal_connect_object (view->details->model, "get-icon-scale",
                 G_CALLBACK (get_icon_scale_callback), view, 0);

	gtk_tree_selection_set_mode (gtk_tree_view_get_selection (view->details->tree_view), GTK_SELECTION_MULTIPLE);

	nemo_columns = nemo_get_all_columns ();

	for (l = nemo_columns; l != NULL; l = l->next) {
		NemoColumn *nemo_column;
		int column_num;
		char *name;
		char *label;
		float xalign;
        gint width_chars;
        gboolean ellipsize;
        gboolean unbounded;


		nemo_column = NEMO_COLUMN (l->data);

		g_object_get (nemo_column,
			      "name", &name,
			      "label", &label,
			      "xalign", &xalign,
                  "width-chars", &width_chars,
                  "ellipsize", &ellipsize,
                  "unbounded", &unbounded, NULL);

		column_num = nemo_list_model_add_column (view->details->model,
						       nemo_column);

		/* Created the name column specially, because it
		 * has the icon in it.*/
		if (!strcmp (name, "name")) {
			/* Create the file name column */
			cell = gtk_cell_renderer_pixbuf_new ();
			view->details->pixbuf_cell = (GtkCellRendererPixbuf *)cell;

			view->details->file_name_column = gtk_tree_view_column_new ();
            gtk_tree_view_append_column (view->details->tree_view,
                                         view->details->file_name_column);

            g_object_set_data_full (G_OBJECT (view->details->file_name_column),
                                    "column-id", g_strdup (name),
                                    g_free);

			view->details->file_name_column_num = column_num;

			g_hash_table_insert (view->details->columns,
					     g_strdup ("name"),
					     view->details->file_name_column);

            g_signal_connect (gtk_tree_view_column_get_button (view->details->file_name_column),
                              "button-press-event",
                              G_CALLBACK (column_header_clicked),
                              view);
            eel_gtk_widget_refuse_focus (gtk_tree_view_column_get_button (view->details->file_name_column));

			gtk_tree_view_set_search_column (view->details->tree_view, column_num);

			gtk_tree_view_column_set_sort_column_id (view->details->file_name_column, column_num);
			gtk_tree_view_column_set_title (view->details->file_name_column, _("Name"));
			gtk_tree_view_column_set_resizable (view->details->file_name_column, TRUE);
            /* Widths are worked out for the whole row at once, so every column
             * is fixed and nothing here is left to the tree view. No min-width:
             * it would clamp a width we meant, and the minimums are in the
             * layout. No expanding column either: the layout already hands the
             * leftover to Name, and an expanding column let GTK add width on
             * top of that from a share it had worked out while the view was
             * wider - which is what put a scrollbar on a row that fit. */
            gtk_tree_view_column_set_sizing (view->details->file_name_column,
                                             GTK_TREE_VIEW_COLUMN_FIXED);
            gtk_tree_view_column_set_min_width (view->details->file_name_column, -1);
            gtk_tree_view_column_set_reorderable (view->details->file_name_column, TRUE);
            g_signal_connect (view->details->file_name_column, "notify::fixed-width",
                              G_CALLBACK (column_fixed_width_notify), view);

			gtk_tree_view_column_pack_start (view->details->file_name_column, cell, FALSE);
			gtk_tree_view_column_set_attributes (view->details->file_name_column,
							     cell,
							     "surface", NEMO_LIST_MODEL_SMALLEST_ICON_COLUMN,
							     NULL);
			gtk_tree_view_column_set_cell_data_func (view->details->file_name_column, cell,
								 (GtkTreeCellDataFunc) shade_row_cell_data_func,
								 view, NULL);

			cell = gtk_cell_renderer_text_new ();
			view->details->file_name_cell = (GtkCellRendererText *)cell;
            /* No width-chars here on purpose: it would set a ~40-char minimum on the
             * cell, which the column can never shrink below, and the width the
             * column may go down to is the layout's call. Ellipsizing takes care
             * of the names past it. */
            g_object_set (cell,
                          "xpad", 5,
                          "ellipsize", PANGO_ELLIPSIZE_END,
                          NULL);

            g_object_set_data_full (G_OBJECT (cell),
                                    "column-id", g_strdup ("filename"),
                                    g_free);

			g_signal_connect (cell, "edited", G_CALLBACK (cell_renderer_edited), view);
			g_signal_connect (cell, "editing-canceled", G_CALLBACK (cell_renderer_editing_canceled), view);
			g_signal_connect (cell, "editing-started", G_CALLBACK (cell_renderer_editing_started_cb), view);

			gtk_tree_view_column_pack_start (view->details->file_name_column, cell, TRUE);
			gtk_tree_view_column_set_cell_data_func (view->details->file_name_column, cell,
								 (GtkTreeCellDataFunc) filename_cell_data_func,
								 view, NULL);
		} else {
			cell = gtk_cell_renderer_text_new ();
            /* Every column can be narrowed now, so a value that no longer fits
             * says so rather than being cut off mid-letter. A column that asked
             * for a particular ellipsis keeps it. */
            g_object_set (cell,
                          "xalign", xalign,
                          "xpad", 5,
                          "width-chars", width_chars,
                          "ellipsize", ellipsize == PANGO_ELLIPSIZE_NONE
                                        ? PANGO_ELLIPSIZE_END : ellipsize,
                          NULL);

			view->details->cells = g_list_append (view->details->cells,
							      cell);
            g_object_set_data_full (G_OBJECT (cell),
                                    "column-id", g_strdup (name),
                                    g_free);

            column = gtk_tree_view_column_new ();
            g_object_set_data_full (G_OBJECT (column),
                                    "column-id", g_strdup (name),
                                    g_free);

            gtk_tree_view_column_set_title (column, label);
            gtk_tree_view_column_pack_start (column, cell, TRUE);
            gtk_tree_view_column_set_attributes (column, cell,
                                                 "text", column_num,
                                                 "weight", NEMO_LIST_MODEL_TEXT_WEIGHT_COLUMN,
                                                 NULL);
            gtk_tree_view_column_set_cell_data_func (column, cell,
                                                     (GtkTreeCellDataFunc) shade_row_cell_data_func,
                                                     view, NULL);

            gtk_tree_view_append_column (view->details->tree_view, column);
            gtk_tree_view_column_set_sizing (column, GTK_TREE_VIEW_COLUMN_FIXED);
            gtk_tree_view_column_set_min_width (column, -1);

            /* Values with no length anyone would call normal - a type, a path,
             * an owner. Their width is a judgement, made from every distinct
             * value seen, rather than the widest one. */
            if (unbounded) {
                g_object_set_data (G_OBJECT (column), "unbounded", GINT_TO_POINTER (TRUE));
            }

			gtk_tree_view_column_set_sort_column_id (column, column_num);

            g_hash_table_insert (view->details->columns,
                                 g_strdup (name),
                                 column);

            g_signal_connect (gtk_tree_view_column_get_button (column),
                              "button-press-event",
                              G_CALLBACK (column_header_clicked),
                              view);
            eel_gtk_widget_refuse_focus (gtk_tree_view_column_get_button (column));

			/* A fixed column is always shown whole, so there is nothing to drag. */
			gtk_tree_view_column_set_resizable (column, unbounded);
            gtk_tree_view_column_set_reorderable (column, TRUE);
            g_signal_connect (column, "notify::fixed-width",
                              G_CALLBACK (column_fixed_width_notify), view);
		}
		g_free (name);
		g_free (label);
	}

	nemo_column_list_free (nemo_columns);

	default_visible_columns = nemo_config_get_strv (nemo_list_view_preferences,
						       NEMO_PREFERENCES_LIST_VIEW_DEFAULT_VISIBLE_COLUMNS);
	default_column_order = nemo_config_get_strv (nemo_list_view_preferences,
						    NEMO_PREFERENCES_LIST_VIEW_DEFAULT_COLUMN_ORDER);

	/* Apply the default column order and visible columns, to get it
	 * right most of the time. The metadata will be checked when a
	 * folder is loaded */
	apply_columns_settings (view,
				default_column_order,
				default_visible_columns);

	/* Every column above sizes FIXED, so the tree view has no reason to measure
	 * each row to find its height - and measuring them was about half of what a
	 * big folder cost to load. Only safe while that stays true, which lint-c
	 * checks, since a column left to size itself turns it back off quietly. */
	gtk_tree_view_set_fixed_height_mode (view->details->tree_view, TRUE);

	gtk_widget_show (GTK_WIDGET (view->details->tree_view));
	gtk_container_add (GTK_CONTAINER (view), GTK_WIDGET (view->details->tree_view));

        atk_obj = gtk_widget_get_accessible (GTK_WIDGET (view->details->tree_view));
        atk_object_set_name (atk_obj, _("List view"));

    gtk_widget_set_has_tooltip (GTK_WIDGET (view->details->tree_view), TRUE);

	g_strfreev (default_visible_columns);
	g_strfreev (default_column_order);
}

static void
nemo_list_view_add_file (NemoView *view, NemoFile *file, NemoDirectory *directory)
{
	NemoListModel *model;
	NemoDirectory *group;

    if (nemo_file_has_thumbnail_access_problem (file)) {
        nemo_application_set_cache_flag (nemo_application_get_singleton ());
        nemo_window_slot_check_bad_cache_bar (nemo_view_get_nemo_window_slot (view));
    }

	model = NEMO_LIST_VIEW (view)->details->model;

	group = search_group_for_file (NEMO_LIST_VIEW (view), file, TRUE);
	nemo_list_model_add_file (model, file, group != NULL ? group : directory);
	nemo_directory_unref (group);

    queue_update_visible_icons (NEMO_LIST_VIEW (view), INITIAL_UPDATE_VISIBLE_DELAY);
}

static char **
get_default_visible_columns (NemoListView *list_view)
{
    NemoFile *file;

    file = nemo_view_get_directory_as_file (NEMO_VIEW (list_view));

    if (nemo_file_is_in_trash (file)) {
        return g_strdupv ((gchar **) default_trash_visible_columns);
    }

    if (nemo_file_is_in_recent (file)) {
        return g_strdupv ((gchar **) default_recent_visible_columns);
    }

    if (nemo_file_is_in_favorites (file)) {
        return g_strdupv ((gchar **) default_favorites_visible_columns);
    }

    if (nemo_file_is_in_search (file)) {
        return g_strdupv ((gchar **) default_search_columns);
    }

    return nemo_config_get_strv (nemo_list_view_preferences,
                                NEMO_PREFERENCES_LIST_VIEW_DEFAULT_VISIBLE_COLUMNS);
}

static char **
get_visible_columns (NemoListView *list_view)
{
	NemoFile *file;
	GList *visible_columns;
	char **ret;

	ret = NULL;
    visible_columns = NULL;

	file = nemo_view_get_directory_as_file (NEMO_VIEW (list_view));

    if (!nemo_global_preferences_get_remember_folder_settings ()) {
        visible_columns = nemo_window_get_ignore_meta_visible_columns (nemo_view_get_nemo_window (NEMO_VIEW (list_view)));
    } else {
        if (nemo_file_is_in_search (file)) {
            gchar **modified_cols;

            modified_cols = nemo_config_get_strv (nemo_search_preferences, NEMO_PREFERENCES_SEARCH_VISIBLE_COLUMNS);

            if (g_strv_length (modified_cols) > 0) {
                return modified_cols;
            } else {
                g_strfreev (modified_cols);
            }
        } else {
            visible_columns = nemo_folder_settings_get_list (file, NEMO_METADATA_KEY_LIST_VIEW_VISIBLE_COLUMNS);
        }
    }

	if (visible_columns) {
        ret = string_array_from_string_glist (visible_columns);
        g_list_free_full (visible_columns, g_free);
	}

	if (ret != NULL) {
		return ret;
	}

	return get_default_visible_columns (list_view);
}

static char **
get_default_column_order (NemoListView *list_view)
{
    NemoFile *file;

    file = nemo_view_get_directory_as_file (NEMO_VIEW (list_view));

    if (nemo_file_is_in_trash (file)) {
        return g_strdupv ((gchar **) default_trash_columns_order);
    }

    if (nemo_file_is_in_recent (file)) {
        return g_strdupv ((gchar **) default_recent_columns_order);
    }

    if (nemo_file_is_in_favorites (file)) {
        return g_strdupv ((gchar **) default_favorites_columns_order);
    }

    if (nemo_file_is_in_search (file)) {
        return g_strdupv ((gchar **) default_search_columns);
    }

    return nemo_config_get_strv (nemo_list_view_preferences,
                                NEMO_PREFERENCES_LIST_VIEW_DEFAULT_COLUMN_ORDER);
}

static char **
get_column_order (NemoListView *list_view)
{
	NemoFile *file;
	GList *column_order;
	char **ret;

    column_order = NULL;
	ret = NULL;

	file = nemo_view_get_directory_as_file (NEMO_VIEW (list_view));

    if (!nemo_global_preferences_get_remember_folder_settings ()) {
        column_order = nemo_window_get_ignore_meta_column_order (nemo_view_get_nemo_window (NEMO_VIEW (list_view)));
    } else {
        if (nemo_file_is_in_search (file)) {
            gchar **modified_cols;
            modified_cols = nemo_config_get_strv (nemo_search_preferences, NEMO_PREFERENCES_SEARCH_VISIBLE_COLUMNS);

            if (g_strv_length (modified_cols) > 0) {
                return modified_cols;
            } else {
                g_strfreev (modified_cols);
            }
        } else {
            column_order = nemo_folder_settings_get_list (file, NEMO_METADATA_KEY_LIST_VIEW_COLUMN_ORDER);
        }
    }

    if (column_order) {
        ret = string_array_from_string_glist (column_order);
        g_list_free_full (column_order, g_free);
    }

	if (ret != NULL) {
		return ret;
	}

	return get_default_column_order (list_view);
}

static void
set_columns_settings_from_metadata_and_preferences (NemoListView *list_view)
{
	char **column_order;
	char **visible_columns;

	column_order = get_column_order (list_view);
	visible_columns = get_visible_columns (list_view);

	apply_columns_settings (list_view, column_order, visible_columns);

	g_strfreev (column_order);
	g_strfreev (visible_columns);
}

static void
set_sort_order_from_metadata_and_preferences (NemoListView *list_view)
{
	char *sort_attribute;
	int sort_column_id;
	NemoFile *file;
	gboolean sort_reversed, default_sort_reversed;
	const gchar *default_sort_order;

	file = nemo_view_get_directory_as_file (NEMO_VIEW (list_view));

        if (!nemo_global_preferences_get_remember_folder_settings ())
                sort_attribute = g_strdup (nemo_window_get_ignore_meta_sort_column (nemo_view_get_nemo_window (NEMO_VIEW (list_view))));
        else if (nemo_file_is_in_search (file)) {
            sort_attribute = nemo_config_get_string (nemo_search_preferences, NEMO_PREFERENCES_SEARCH_SORT_COLUMN);
        } else {
            sort_attribute = nemo_folder_settings_get (file,
                                                       NEMO_METADATA_KEY_LIST_VIEW_SORT_COLUMN,
                                                       NULL);
        }
	sort_column_id = nemo_list_model_get_sort_column_id_from_attribute (list_view->details->model,
									  g_quark_from_string (sort_attribute));
	g_free (sort_attribute);

	default_sort_order = get_default_sort_order (file, &default_sort_reversed);

	if (sort_column_id == -1) {
		sort_column_id =
			nemo_list_model_get_sort_column_id_from_attribute (list_view->details->model,
									 g_quark_from_string (default_sort_order));
	}

    if (!nemo_global_preferences_get_remember_folder_settings ()) {
        gint dir = nemo_window_get_ignore_meta_sort_direction (nemo_view_get_nemo_window (NEMO_VIEW (list_view)));
        sort_reversed = dir > SORT_NULL ? dir == SORT_DESCENDING : default_sort_reversed;
    } else if (nemo_file_is_in_search (file)) {
        sort_reversed = nemo_config_get_boolean (nemo_search_preferences, NEMO_PREFERENCES_SEARCH_REVERSE_SORT);
    } else {
        sort_reversed = nemo_folder_settings_get_boolean (file,
                                                          NEMO_METADATA_KEY_LIST_VIEW_SORT_REVERSED,
                                                          default_sort_reversed);
    }
    gtk_tree_sortable_set_sort_column_id (GTK_TREE_SORTABLE (list_view->details->model),
                                                             sort_column_id,
                                                             sort_reversed ? GTK_SORT_DESCENDING : GTK_SORT_ASCENDING);
}

static gboolean
list_view_changed_foreach (GtkTreeModel *model,
              		   GtkTreePath  *path,
			   GtkTreeIter  *iter,
			   G_GNUC_UNUSED gpointer      data)
{
	gtk_tree_model_row_changed (model, path, iter);
	return FALSE;
}

/* The list view is held to the steps rather than taking any size, because a
   row's icon comes out of one of the model's size columns. The backlog carries
   what it would take to lift that. */
static gint
snap_to_step (gint size)
{
	return nemo_icon_size_from_legacy_level (nemo_icon_size_legacy_level (size));
}

static gint
get_default_icon_size (void) {
	gint percent;

	percent = nemo_config_get_int (nemo_list_view_preferences,
				       NEMO_PREFERENCES_LIST_VIEW_DEFAULT_ICON_SIZE);

	return snap_to_step (nemo_icon_size_from_percent (percent));
}

static gint
saved_icon_size (NemoFile *file, gint fallback)
{
	gint saved;

	saved = nemo_folder_settings_get_int (file, NEMO_METADATA_KEY_LIST_VIEW_ZOOM_LEVEL,
					      fallback);

	if (nemo_icon_size_is_legacy_level (saved)) {
		return nemo_icon_size_from_legacy_level (saved);
	}

	return snap_to_step (saved);
}

static void
set_icon_size_from_metadata_and_preferences (NemoListView *list_view)
{
	NemoFile *file;
	int size;

	if (nemo_view_supports_zooming (NEMO_VIEW (list_view))) {
		file = nemo_view_get_directory_as_file (NEMO_VIEW (list_view));
        if (!nemo_global_preferences_get_remember_folder_settings ()) {
            gchar *uri;

            uri = nemo_file_get_uri (file);

            if (eel_uri_is_search (uri)) {
                size = get_default_icon_size ();
            } else {
                gint pinned;
                pinned = nemo_window_get_ignore_meta_list_icon_size (nemo_view_get_nemo_window (NEMO_VIEW (list_view)));

                size = pinned > 0 ? snap_to_step (pinned) : get_default_icon_size ();
            }

            g_free (uri);
        } else {
            size = saved_icon_size (file, get_default_icon_size ());
        }
		nemo_list_view_set_icon_size (list_view, size, TRUE);

		/* updated the rows after updating the font size */
		gtk_tree_model_foreach (GTK_TREE_MODEL (list_view->details->model),
					list_view_changed_foreach, NULL);
	}
}

static void
nemo_list_view_begin_loading (NemoView *view)
{
	NemoListView *list_view;

	list_view = NEMO_LIST_VIEW (view);

	set_sort_order_from_metadata_and_preferences (list_view);
	set_icon_size_from_metadata_and_preferences (list_view);
	set_columns_settings_from_metadata_and_preferences (list_view);
	load_user_widths (list_view);
	expanders_enabled_changed_cb (list_view);

    gtk_widget_set_margin_bottom (GTK_WIDGET (list_view->details->tree_view), 0);

    set_ok_to_load_deferred_attrs (list_view, FALSE);

    nemo_list_model_set_view_directory (list_view->details->model, nemo_view_get_model (view));

    AtkObject *atk = gtk_widget_get_accessible (GTK_WIDGET (NEMO_LIST_VIEW (view)->details->tree_view));

    g_signal_connect_object (atk, "column-reordered",
                             G_CALLBACK (columns_reordered_callback), view, 0);
}

static void
stop_cell_editing (NemoListView *list_view)
{
	GtkTreeViewColumn *column;

	/* Stop an ongoing rename to commit the name changes when the user
	 * changes directories without exiting cell edit mode. It also prevents
	 * the edited handler from being called on the cleared list model.
	 */
	column = list_view->details->file_name_column;
	if (column != NULL && list_view->details->editable_widget != NULL &&
	    GTK_IS_CELL_EDITABLE (list_view->details->editable_widget)) {
		gtk_cell_editable_editing_done (list_view->details->editable_widget);
	}
}

static void
nemo_list_view_clear (NemoView *view)
{
	NemoListView *list_view;
    GtkTreeSelection *tree_selection;

	list_view = NEMO_LIST_VIEW (view);

    list_view->details->ok_to_load_deferred_attrs = FALSE;
    g_clear_object (&list_view->details->search_root);

    if (list_view->details->update_visible_icons_id > 0) {
        g_source_remove (list_view->details->update_visible_icons_id);
        list_view->details->update_visible_icons_id = 0;
    }

    tree_selection = gtk_tree_view_get_selection (list_view->details->tree_view);

    g_signal_handlers_block_by_func (tree_selection, list_selection_changed_callback, view);

	if (list_view->details->model != NULL) {
		stop_cell_editing (list_view);
		nemo_list_model_clear (list_view->details->model);
	}

	/* The names in the last folder say nothing about this one, and a width
	   dragged there does not carry over. */
	forget_samples (list_view);
	g_hash_table_remove_all (list_view->details->user_widths);
	remember_cursor (list_view);

    g_signal_handlers_unblock_by_func (tree_selection, list_selection_changed_callback, view);
}

static void
nemo_list_view_rename_callback (G_GNUC_UNUSED NemoFile *file,
				    G_GNUC_UNUSED GFile *result_location,
				    GError *error,
				    gpointer callback_data)
{
	NemoListView *view;

	view = NEMO_LIST_VIEW (callback_data);

	if (view->details->renaming_file) {
		view->details->rename_done = TRUE;

		if (error != NULL) {
			/* If the rename failed (or was cancelled), kill renaming_file.
			 * We won't get a change event for the rename, so otherwise
			 * it would stay around forever.
			 */
			nemo_file_unref (view->details->renaming_file);
			view->details->renaming_file = NULL;
		}
	}

	g_object_unref (view);
}


static void
nemo_list_view_file_changed (NemoView *view, NemoFile *file, NemoDirectory *directory)
{
	NemoListView *listview;
	NemoDirectory *group;
	GtkTreeIter iter;
	GtkTreePath *file_path;

	listview = NEMO_LIST_VIEW (view);

	group = search_group_for_file (listview, file, FALSE);
	if (group != NULL) {
		directory = group;
	}

	nemo_list_model_file_changed (listview->details->model, file, directory);

	if (listview->details->renaming_file != NULL &&
	    file == listview->details->renaming_file &&
	    listview->details->rename_done) {
		/* This is (probably) the result of the rename operation, and
		 * the tree-view changes above could have resorted the list, so
		 * scroll to the new position
		 */
		if (nemo_list_model_get_tree_iter_from_file (listview->details->model, file, directory, &iter)) {
			file_path = gtk_tree_model_get_path (GTK_TREE_MODEL (listview->details->model), &iter);
			gtk_tree_view_scroll_to_cell (listview->details->tree_view,
						      file_path, NULL,
						      FALSE, 0.0, 0.0);
			gtk_tree_path_free (file_path);
		}

		nemo_file_unref (listview->details->renaming_file);
		listview->details->renaming_file = NULL;
	}

	nemo_directory_unref (group);
}

typedef struct {
	GtkTreePath *path;
	gboolean is_common;
	gboolean is_root;
} HasCommonParentData;

static void
tree_selection_has_common_parent_foreach_func (G_GNUC_UNUSED GtkTreeModel *model,
						GtkTreePath *path,
						G_GNUC_UNUSED GtkTreeIter *iter,
						gpointer user_data)
{
	HasCommonParentData *data;
	GtkTreePath *parent_path;
	gboolean has_parent;

	data = (HasCommonParentData *) user_data;

	parent_path = gtk_tree_path_copy (path);
	gtk_tree_path_up (parent_path);

	has_parent = (gtk_tree_path_get_depth (parent_path) > 0) ? TRUE : FALSE;

	if (!has_parent) {
		data->is_root = TRUE;
	}

	if (data->is_common && !data->is_root) {
		if (data->path == NULL) {
			data->path = gtk_tree_path_copy (parent_path);
		} else if (gtk_tree_path_compare (data->path, parent_path) != 0) {
			data->is_common = FALSE;
		}
	}

	gtk_tree_path_free (parent_path);
}

static void
tree_selection_has_common_parent (GtkTreeSelection *selection,
				  gboolean *is_common,
				  gboolean *is_root)
{
	HasCommonParentData data;

	g_assert (is_common != NULL);
	g_assert (is_root != NULL);

	data.path = NULL;
	data.is_common = *is_common = TRUE;
	data.is_root = *is_root = FALSE;

	gtk_tree_selection_selected_foreach (selection,
					     tree_selection_has_common_parent_foreach_func,
					     &data);

	*is_common = data.is_common;
	*is_root = data.is_root;

	if (data.path != NULL) {
		gtk_tree_path_free (data.path);
	}
}

static char *
nemo_list_view_get_backing_uri (NemoView *view)
{
	NemoListView *list_view;
	NemoListModel *list_model;
	NemoFile *file;
	GtkTreeView *tree_view;
	GtkTreeSelection *selection;
	GtkTreePath *path;
	GList *paths;
	guint length;
	char *uri;

	g_return_val_if_fail (NEMO_IS_LIST_VIEW (view), NULL);

	list_view = NEMO_LIST_VIEW (view);
	list_model = list_view->details->model;
	tree_view = list_view->details->tree_view;

	g_assert (list_model);

	/* We currently handle three common cases here:
	 * (a) if the selection contains non-filesystem items (i.e., the
	 *     "(Empty)" label), we return the uri of the parent.
	 * (b) if the selection consists of exactly one _expanded_ directory, we
	 *     return its URI.
	 * (c) if the selection consists of either exactly one item which is not
	 *     an expanded directory) or multiple items in the same directory,
	 *     we return the URI of the common parent.
	 */

	uri = NULL;

	selection = gtk_tree_view_get_selection (tree_view);
	length = gtk_tree_selection_count_selected_rows (selection);

	if (length == 1) {

		paths = gtk_tree_selection_get_selected_rows (selection, NULL);
		path = (GtkTreePath *) paths->data;

		file = nemo_list_model_file_for_path (list_model, path);
		if (file == NULL) {
			/* The selected item is a label, not a file */
			gtk_tree_path_up (path);
			file = nemo_list_model_file_for_path (list_model, path);
		}

		if (file != NULL) {
			if (nemo_file_is_directory (file) &&
			    gtk_tree_view_row_expanded (tree_view, path)) {
				uri = nemo_file_get_uri (file);
			}
			nemo_file_unref (file);
		}

		gtk_tree_path_free (path);
		g_list_free (paths);
	}

	if (uri == NULL && length > 0) {

		gboolean is_common, is_root;

		/* Check that all the selected items belong to the same
		 * directory and that directory is not the root directory (which
		 * is handled by NemoView::get_backing_directory.) */

		tree_selection_has_common_parent (selection, &is_common, &is_root);

		if (is_common && !is_root) {

			paths = gtk_tree_selection_get_selected_rows (selection, NULL);
			path = (GtkTreePath *) paths->data;

			file = nemo_list_model_file_for_path (list_model, path);
			g_assert (file != NULL);
			uri = nemo_file_get_parent_uri (file);
			nemo_file_unref (file);

			g_list_free_full (paths, (GDestroyNotify) gtk_tree_path_free);
		}
	}

	if (uri != NULL) {
		return uri;
	}

	return NEMO_VIEW_CLASS (nemo_list_view_parent_class)->get_backing_uri (view);
}

static void
nemo_list_view_get_selection_foreach_func (GtkTreeModel *model, G_GNUC_UNUSED GtkTreePath *path, GtkTreeIter *iter, gpointer data)
{
	GList **list;
	NemoFile *file;

	list = data;

	gtk_tree_model_get (model, iter,
			    NEMO_LIST_MODEL_FILE_COLUMN, &file,
			    -1);

	if (file != NULL) {
		(* list) = g_list_prepend ((* list), file);
	}
}

static GList *
nemo_list_view_get_selection (NemoView *view)
{
	GList *list;

	list = NULL;

	gtk_tree_selection_selected_foreach (gtk_tree_view_get_selection (NEMO_LIST_VIEW (view)->details->tree_view),
					     nemo_list_view_get_selection_foreach_func, &list);

	return g_list_reverse (list);
}

static GList *
nemo_list_view_peek_selection (NemoView *view)
{
    NemoListView *list_view = NEMO_LIST_VIEW (view);

    if (list_view->details->current_selection_count == -1) {
        nemo_list_view_update_selection (NEMO_VIEW (list_view));
    }

    return list_view->details->current_selection;
}

static gint
nemo_list_view_get_selection_count (NemoView *view)
{
    NemoListView *list_view = NEMO_LIST_VIEW (view);

    if (list_view->details->current_selection_count == -1) {
        nemo_list_view_update_selection (NEMO_VIEW (list_view));
    }

    return list_view->details->current_selection_count;
}

static void
nemo_list_view_update_selection (NemoView *view)
{
    NemoListView *list_view = NEMO_LIST_VIEW (view);

    if (list_view->details->current_selection != NULL) {
        g_list_free (list_view->details->current_selection);

        list_view->details->current_selection = NULL;
        list_view->details->current_selection_count = 0;
    }

    list_view->details->current_selection = nemo_list_view_get_selection (view);
    list_view->details->current_selection_count = g_list_length (list_view->details->current_selection);
}

static void
nemo_list_view_get_selection_for_file_transfer_foreach_func (GtkTreeModel *model, G_GNUC_UNUSED GtkTreePath *path, GtkTreeIter *iter, gpointer data)
{
	NemoFile *file;
	struct SelectionForeachData *selection_data;
	GtkTreeIter parent, child;

	selection_data = data;

	gtk_tree_model_get (model, iter,
			    NEMO_LIST_MODEL_FILE_COLUMN, &file,
			    -1);

	if (file != NULL) {
		/* If the parent folder is also selected, don't include this file in the
		 * file operation, since that would copy it to the toplevel target instead
		 * of keeping it as a child of the copied folder
		 */
		child = *iter;
		while (gtk_tree_model_iter_parent (model, &parent, &child)) {
			if (gtk_tree_selection_iter_is_selected (selection_data->selection,
								 &parent)) {
				return;
			}
			child = parent;
		}

		nemo_file_ref (file);
		selection_data->list = g_list_prepend (selection_data->list, file);
	}
}


static GList *
nemo_list_view_get_selection_for_file_transfer (NemoView *view)
{
	struct SelectionForeachData selection_data;

	selection_data.list = NULL;
	selection_data.selection = gtk_tree_view_get_selection (NEMO_LIST_VIEW (view)->details->tree_view);

	gtk_tree_selection_selected_foreach (selection_data.selection,
					     nemo_list_view_get_selection_for_file_transfer_foreach_func, &selection_data);

	return g_list_reverse (selection_data.list);
}




static guint
nemo_list_view_get_item_count (NemoView *view)
{
	g_return_val_if_fail (NEMO_IS_LIST_VIEW (view), 0);

	return nemo_list_model_get_length (NEMO_LIST_VIEW (view)->details->model);
}

static gboolean
nemo_list_view_is_empty (NemoView *view)
{
	return nemo_list_model_is_empty (NEMO_LIST_VIEW (view)->details->model);
}

static void
nemo_list_view_end_file_changes (NemoView *view)
{
	NemoListView *list_view;

	list_view = NEMO_LIST_VIEW (view);

	if (list_view->details->new_selection_path) {
		gtk_tree_view_set_cursor (list_view->details->tree_view,
					  list_view->details->new_selection_path,
					  NULL, FALSE);
		gtk_tree_path_free (list_view->details->new_selection_path);
		list_view->details->new_selection_path = NULL;
	}
}

static void
nemo_list_view_remove_file (NemoView *view, NemoFile *file, NemoDirectory *directory)
{
	GtkTreePath *path;
	GtkTreePath *file_path;
	GtkTreeIter iter;
	GtkTreeIter temp_iter;
	GtkTreeRowReference* row_reference;
	NemoListView *list_view;
	NemoDirectory *group;
	GtkTreeModel* tree_model;
	GtkTreeSelection *selection;

	path = NULL;
	row_reference = NULL;
	list_view = NEMO_LIST_VIEW (view);
	tree_model = GTK_TREE_MODEL(list_view->details->model);

	group = search_group_for_file (list_view, file, FALSE);
	if (group != NULL) {
		directory = group;
	}

	if (nemo_list_model_get_tree_iter_from_file (list_view->details->model, file, directory, &iter)) {
		selection = gtk_tree_view_get_selection (list_view->details->tree_view);
		file_path = gtk_tree_model_get_path (tree_model, &iter);

		if (gtk_tree_selection_path_is_selected (selection, file_path)) {
			/* get reference for next element in the list view. If the element to be deleted is the
			 * last one, get reference to previous element. If there is only one element in view
			 * no need to select anything.
			 */
			temp_iter = iter;

			if (gtk_tree_model_iter_next (tree_model, &iter)) {
				path = gtk_tree_model_get_path (tree_model, &iter);
				row_reference = gtk_tree_row_reference_new (tree_model, path);
			} else {
				path = gtk_tree_model_get_path (tree_model, &temp_iter);
				if (gtk_tree_path_prev (path)) {
					row_reference = gtk_tree_row_reference_new (tree_model, path);
				}
			}
			gtk_tree_path_free (path);
		}

		gtk_tree_path_free (file_path);

		nemo_list_model_remove_file (list_view->details->model, file, directory);
		drop_name_sample (list_view, file);

		if (gtk_tree_row_reference_valid (row_reference)) {
			if (list_view->details->new_selection_path) {
				gtk_tree_path_free (list_view->details->new_selection_path);
			}
			list_view->details->new_selection_path = gtk_tree_row_reference_get_path (row_reference);
		}

		if (row_reference) {
			gtk_tree_row_reference_free (row_reference);
		}
	}

	/* The last match gone means the folder row has nothing left to say. */
	if (group != NULL) {
		if (nemo_list_model_search_group_is_empty (list_view->details->model, group)) {
			nemo_list_model_remove_search_group (list_view->details->model, group);
		}
		nemo_directory_unref (group);
	}
}

static void
nemo_list_view_set_selection (NemoView *view, GList *selection)
{
	NemoListView *list_view;
	GtkTreeSelection *tree_selection;
	GList *node;
	GList *iters, *l;
	NemoFile *file;

	list_view = NEMO_LIST_VIEW (view);
	tree_selection = gtk_tree_view_get_selection (list_view->details->tree_view);

	g_signal_handlers_block_by_func (tree_selection, list_selection_changed_callback, view);

	gtk_tree_selection_unselect_all (tree_selection);
	for (node = selection; node != NULL; node = node->next) {
		file = node->data;
		iters = nemo_list_model_get_all_iters_for_file (list_view->details->model, file);

		for (l = iters; l != NULL; l = l->next) {
			gtk_tree_selection_select_iter (tree_selection,
							(GtkTreeIter *)l->data);
		}
		g_list_free_full (iters, g_free);
	}

	g_signal_handlers_unblock_by_func (tree_selection, list_selection_changed_callback, view);
	nemo_view_notify_selection_changed (view);
}

static void
nemo_list_view_invert_selection (NemoView *view)
{
	NemoListView *list_view;
	GtkTreeSelection *tree_selection;
	GList *node;
	GList *iters, *l;
	NemoFile *file;
	GList *selection = NULL;

	list_view = NEMO_LIST_VIEW (view);
	tree_selection = gtk_tree_view_get_selection (list_view->details->tree_view);

	g_signal_handlers_block_by_func (tree_selection, list_selection_changed_callback, view);

	gtk_tree_selection_selected_foreach (tree_selection,
					     nemo_list_view_get_selection_foreach_func, &selection);

	gtk_tree_selection_select_all (tree_selection);

	for (node = selection; node != NULL; node = node->next) {
		file = node->data;
		iters = nemo_list_model_get_all_iters_for_file (list_view->details->model, file);

		for (l = iters; l != NULL; l = l->next) {
			gtk_tree_selection_unselect_iter (tree_selection,
							  (GtkTreeIter *)l->data);
		}
		g_list_free_full (iters, g_free);
	}

	g_list_free (selection);

	g_signal_handlers_unblock_by_func (tree_selection, list_selection_changed_callback, view);
	nemo_view_notify_selection_changed (view);
}

static void
nemo_list_view_select_all (NemoView *view)
{
	gtk_tree_selection_select_all (gtk_tree_view_get_selection (NEMO_LIST_VIEW (view)->details->tree_view));
}

static void
nemo_list_view_merge_menus (NemoView *view)
{
  NemoListView *list_view;
  GtkUIManager *ui_manager;
  GtkActionGroup *action_group;

  list_view = NEMO_LIST_VIEW (view);

  NEMO_VIEW_CLASS (nemo_list_view_parent_class)->merge_menus (view);

  ui_manager = nemo_view_get_ui_manager (view);

  action_group = gtk_action_group_new ("ListViewActions");
  gtk_action_group_set_translation_domain (action_group, GETTEXT_PACKAGE);
  list_view->details->list_action_group = action_group;

  gtk_ui_manager_insert_action_group (ui_manager, action_group, 0);
  g_object_unref (action_group); /* owned by ui manager */

  list_view->details->list_merge_id =
    gtk_ui_manager_add_ui_from_resource (ui_manager, "/org/nemo/nemo-list-view-ui.xml", NULL);

  list_view->details->menus_ready = TRUE;
}

static void
nemo_list_view_unmerge_menus (NemoView *view)
{
  NemoListView *list_view;
  GtkUIManager *ui_manager;

  list_view = NEMO_LIST_VIEW (view);

  NEMO_VIEW_CLASS (nemo_list_view_parent_class)->unmerge_menus (view);

  ui_manager = nemo_view_get_ui_manager (view);
  if (ui_manager != NULL) {
    nemo_ui_unmerge_ui (ui_manager,
          &list_view->details->list_merge_id,
          &list_view->details->list_action_group);
  }
}

static void
nemo_list_view_update_menus (NemoView *view)
{
	NemoListView *list_view;

        list_view = NEMO_LIST_VIEW (view);

	/* don't update if the menus aren't ready */
	if (!list_view->details->menus_ready) {
		return;
	}

	NEMO_VIEW_CLASS (nemo_list_view_parent_class)->update_menus (view);
}

/* Reset sort criteria and zoom level to match defaults */
static void
nemo_list_view_reset_to_defaults (NemoView *view)
{
	NemoFile *file;

	file = nemo_view_get_directory_as_file (view);

    g_signal_handlers_block_by_func (NEMO_LIST_VIEW (view)->details->tree_view,
                                     columns_reordered_callback,
                                     NEMO_LIST_VIEW (view));

    if (!nemo_global_preferences_get_remember_folder_settings ()) {
        NemoWindow *window = nemo_view_get_nemo_window (NEMO_VIEW (view));
        nemo_window_set_ignore_meta_sort_column (window, NULL);
        nemo_window_set_ignore_meta_sort_direction (window, SORT_NULL);
        nemo_window_forget_ignore_meta_icon_sizes (window);
        nemo_window_set_ignore_meta_column_order (window, NULL);
        nemo_window_set_ignore_meta_visible_columns (window, NULL);
    } else if (nemo_file_is_in_search (file)) {
        nemo_config_reset (nemo_search_preferences, NEMO_PREFERENCES_SEARCH_VISIBLE_COLUMNS);
        nemo_config_reset (nemo_search_preferences, NEMO_PREFERENCES_SEARCH_SORT_COLUMN);
        nemo_config_reset (nemo_search_preferences, NEMO_PREFERENCES_SEARCH_REVERSE_SORT);
    } else {
        nemo_folder_settings_forget (file);
    }


    char **default_columns, **default_order;

    default_columns = get_default_visible_columns (NEMO_LIST_VIEW (view));
    default_order = get_default_column_order (NEMO_LIST_VIEW (view));
    apply_columns_settings (NEMO_LIST_VIEW (view), default_order, default_columns);

    /* Dragged widths go back to the rule too. The saved ones went with the
       folder's settings above. */
    g_hash_table_remove_all (NEMO_LIST_VIEW (view)->details->user_widths);
    NEMO_LIST_VIEW (view)->details->laid_out_width = -1;
    resize_columns_soon (NEMO_LIST_VIEW (view));

    g_signal_handlers_unblock_by_func (NEMO_LIST_VIEW (view)->details->tree_view,
                                       columns_reordered_callback,
                                       NEMO_LIST_VIEW (view));
}

/* A list row's text does grow with its size, unlike a name under an icon: in a
   list the row height is most of what the size means. Kept per step, which is
   the ladder the list view is held to. */
static void
nemo_list_view_scale_font_size (NemoListView *view,
				    gint new_size)
{
	GList *l;
	static gboolean first_time = TRUE;
	static double pango_scale[7];
	int medium;
	int i;
	int step;

	if (first_time) {
		first_time = FALSE;
		medium = 1;
		pango_scale[medium] = PANGO_SCALE_MEDIUM;
		for (i = medium; i > 0; i--) {
			pango_scale[i - 1] = (1 / 1.2) * pango_scale[i];
		}
		for (i = medium; i < (int) G_N_ELEMENTS (pango_scale) - 1; i++) {
			pango_scale[i + 1] = 1.2 * pango_scale[i];
		}
	}

	step = nemo_icon_size_legacy_level (new_size);

	g_object_set (G_OBJECT (view->details->file_name_cell),
		      "scale", pango_scale[step],
		      NULL);
	for (l = view->details->cells; l != NULL; l = l->next) {
		g_object_set (G_OBJECT (l->data),
			      "scale", pango_scale[step],
			      NULL);
	}
}

static void
nemo_list_view_set_icon_size (NemoListView *view,
				  gint     new_size,
				  gboolean always_emit)
{
    NemoFile *file;
	int icon_size;
	int column;

	g_return_if_fail (NEMO_IS_LIST_VIEW (view));

	new_size = snap_to_step (new_size);

	if (view->details->icon_size == new_size) {
		if (always_emit) {
			g_signal_emit_by_name (NEMO_VIEW(view), "zoom_level_changed");
		}
		return;
	}

	view->details->icon_size = new_size;
	g_signal_emit_by_name (NEMO_VIEW(view), "zoom_level_changed");

    file = nemo_view_get_directory_as_file (NEMO_VIEW (view));

    if (!nemo_global_preferences_get_remember_folder_settings ()) {
        gchar *uri;

        uri = nemo_file_get_uri (file);

        if (!eel_uri_is_search (uri)) {
            nemo_window_set_ignore_meta_list_icon_size (nemo_view_get_nemo_window (NEMO_VIEW (view)), new_size);
        }

        g_free (uri);
    } else {
        nemo_folder_settings_set_int (file,
                                      NEMO_METADATA_KEY_LIST_VIEW_ZOOM_LEVEL,
                                      get_default_icon_size (),
                                      new_size);
    }

	/* Select correctly scaled icons. */
	column = nemo_list_model_get_column_id_for_icon_size (new_size);
	gtk_tree_view_column_set_attributes (view->details->file_name_column,
					     GTK_CELL_RENDERER (view->details->pixbuf_cell),
					     "surface", column,
					     NULL);

	/* Scale text. */
	nemo_list_view_scale_font_size (view, new_size);

	/* Make all rows the same size. */
	icon_size = nemo_get_list_icon_size (new_size);
	gtk_cell_renderer_set_fixed_size (GTK_CELL_RENDERER (view->details->pixbuf_cell),
					  -1, icon_size);

	nemo_view_update_menus (NEMO_VIEW (view));

	/* Everything measured was measured in the old font at the old icon size. */
	view->details->column_floor = 0;
	remeasure_rows (view);

	/* FIXME: https://bugzilla.gnome.org/show_bug.cgi?id=641518 */
	gtk_tree_view_columns_autosize (view->details->tree_view);
}

static void
nemo_list_view_bump_icon_size (NemoView *view, int direction)
{
	NemoListView *list_view;

	g_return_if_fail (NEMO_IS_LIST_VIEW (view));

	list_view = NEMO_LIST_VIEW (view);
	nemo_list_view_set_icon_size (list_view,
				      nemo_icon_size_step (list_view->details->icon_size, direction),
				      FALSE);
}

static gint
nemo_list_view_get_icon_size (NemoView *view)
{
	NemoListView *list_view;

	g_return_val_if_fail (NEMO_IS_LIST_VIEW (view), NEMO_ICON_SIZE_STANDARD);

	list_view = NEMO_LIST_VIEW (view);

	return list_view->details->icon_size;
}

static void
nemo_list_view_set_icon_size_vfunc (NemoView *view,
				  gint size)
{
	NemoListView *list_view;

	g_return_if_fail (NEMO_IS_LIST_VIEW (view));

	list_view = NEMO_LIST_VIEW (view);

	nemo_list_view_set_icon_size (list_view, size, FALSE);
}

static void
nemo_list_view_restore_default_icon_size (NemoView *view)
{
	NemoListView *list_view;

	g_return_if_fail (NEMO_IS_LIST_VIEW (view));

	list_view = NEMO_LIST_VIEW (view);

	nemo_list_view_set_icon_size (list_view, get_default_icon_size (), FALSE);
}

static gint
nemo_list_view_get_default_icon_size_vfunc (NemoView *view)
{
    g_return_val_if_fail (NEMO_IS_LIST_VIEW (view), NEMO_ICON_SIZE_STANDARD);

    return get_default_icon_size();
}

static gboolean
nemo_list_view_can_zoom_in (NemoView *view)
{
	g_return_val_if_fail (NEMO_IS_LIST_VIEW (view), FALSE);

	return NEMO_LIST_VIEW (view)->details->icon_size < NEMO_ICON_SIZE_LARGEST;
}

static gboolean
nemo_list_view_can_zoom_out (NemoView *view)
{
	g_return_val_if_fail (NEMO_IS_LIST_VIEW (view), FALSE);

	return NEMO_LIST_VIEW (view)->details->icon_size > NEMO_ICON_SIZE_SMALLEST;
}

static void
nemo_list_view_start_renaming_file (NemoView *view,
					NemoFile *file,
					gboolean select_all)
{
	NemoListView *list_view;
	GtkTreeIter iter;
	GtkTreePath *path;

	list_view = NEMO_LIST_VIEW (view);

	/* Select all if we are in renaming mode already */
	if (list_view->details->file_name_column && list_view->details->editable_widget) {
		gtk_editable_select_region (GTK_EDITABLE (list_view->details->editable_widget),
					    0,
					    -1);
		return;
	}

	if (!nemo_list_model_get_first_iter_for_file (list_view->details->model, file, &iter)) {
		return;
	}

	/* call parent class to make sure the right icon is selected */
	NEMO_VIEW_CLASS (nemo_list_view_parent_class)->start_renaming_file (view, file, select_all);

	/* Freeze updates to the view to prevent losing rename focus when the tree view updates */
	nemo_view_freeze_updates (NEMO_VIEW (view));

	path = gtk_tree_model_get_path (GTK_TREE_MODEL (list_view->details->model), &iter);

	/* Make filename-cells editable. */
	g_object_set (G_OBJECT (list_view->details->file_name_cell),
		      "editable", TRUE,
		      NULL);

	gtk_tree_view_scroll_to_cell (list_view->details->tree_view,
				      NULL,
				      list_view->details->file_name_column,
				      TRUE, 0.0, 0.0);
	gtk_tree_view_set_cursor_on_cell (list_view->details->tree_view,
					  path,
					  list_view->details->file_name_column,
					  GTK_CELL_RENDERER (list_view->details->file_name_cell),
					  TRUE);

	/* set cursor also triggers editing-started, where we save the editable widget */
	if (list_view->details->editable_widget != NULL) {
        int start_offset, end_offset;

        nemo_rename_region (list_view->details->original_name, select_all,
                            &start_offset, &end_offset);

		gtk_editable_select_region (GTK_EDITABLE (list_view->details->editable_widget),
					    start_offset, end_offset);
	}

	gtk_tree_path_free (path);
}

static void
nemo_list_view_click_to_rename_mode_changed (NemoView *directory_view)
{
    NemoListView *view;

    g_assert (NEMO_IS_LIST_VIEW (directory_view));

    view = NEMO_LIST_VIEW (directory_view);

    view->details->click_to_rename = nemo_config_get_boolean (nemo_preferences,
                                                                  NEMO_PREFERENCES_CLICK_TO_RENAME);
}

static void
nemo_list_view_click_policy_changed (NemoView *directory_view)
{
	GdkWindow *win;
	GdkDisplay *display;
	NemoListView *view;
	GtkTreeIter iter;
	GtkTreeView *tree;

	view = NEMO_LIST_VIEW (directory_view);

    click_policy = nemo_config_get_enum (nemo_preferences,
                                        NEMO_PREFERENCES_CLICK_POLICY);

	/* ensure that we unset the hand cursor and refresh underlined rows */
	if (click_policy == NEMO_CLICK_POLICY_DOUBLE) {
		if (view->details->hover_path != NULL) {
			if (gtk_tree_model_get_iter (GTK_TREE_MODEL (view->details->model),
						     &iter, view->details->hover_path)) {
				gtk_tree_model_row_changed (GTK_TREE_MODEL (view->details->model),
							    view->details->hover_path, &iter);
			}

			gtk_tree_path_free (view->details->hover_path);
			view->details->hover_path = NULL;
		}

		tree = view->details->tree_view;
		if (gtk_widget_get_realized (GTK_WIDGET (tree))) {
			win = gtk_widget_get_window (GTK_WIDGET (tree));
			gdk_window_set_cursor (win, NULL);

			display = gtk_widget_get_display (GTK_WIDGET (view));
			if (display != NULL) {
				gdk_display_flush (display);
			}
		}

		g_clear_object (&hand_cursor);
	} else if (click_policy == NEMO_CLICK_POLICY_SINGLE) {
		if (hand_cursor == NULL) {
			hand_cursor = gdk_cursor_new(GDK_HAND2);
		}
	}
}

static void
default_sort_order_changed_callback (gpointer callback_data)
{
	NemoListView *list_view;

	list_view = NEMO_LIST_VIEW (callback_data);

	set_sort_order_from_metadata_and_preferences (list_view);
}

/* The callback below is connected per view, so every open tab hears a default
 * change. Only the one being looked at should give up its pinned zoom. */
static gboolean
view_is_frontmost (NemoView *view)
{
	NemoWindow *window = nemo_view_get_nemo_window (view);

	return window != NULL &&
	       nemo_window_get_active_slot (window) == nemo_view_get_nemo_window_slot (view);
}

static void
default_icon_size_changed_callback (gpointer callback_data)
{
	NemoListView *list_view;

	list_view = NEMO_LIST_VIEW (callback_data);

	/* Setting a new default is an instruction about the folder in front of you,
	 * so let go of the zoom this window was holding and take the default. A folder
	 * that remembers its own settings keeps them; those change on the Current tab.
	 */
	if (view_is_frontmost (NEMO_VIEW (list_view)) &&
	    !nemo_global_preferences_get_remember_folder_settings ()) {
		nemo_window_set_ignore_meta_list_icon_size (nemo_view_get_nemo_window (NEMO_VIEW (list_view)), 0);
	}

	set_icon_size_from_metadata_and_preferences (list_view);
}

static void
default_visible_columns_changed_callback (gpointer callback_data)
{
	NemoListView *list_view;

	list_view = NEMO_LIST_VIEW (callback_data);

	set_columns_settings_from_metadata_and_preferences (list_view);
}

static void
column_fit_percent_changed_callback (gpointer callback_data)
{
	NemoListView *list_view;

	list_view = NEMO_LIST_VIEW (callback_data);

	list_view->details->laid_out_width = -1;
	resize_columns_soon (list_view);
}

static void
default_column_order_changed_callback (gpointer callback_data)
{
	NemoListView *list_view;

	list_view = NEMO_LIST_VIEW (callback_data);

	set_columns_settings_from_metadata_and_preferences (list_view);
}

static void
nemo_list_view_sort_directories_first_changed (NemoView *view)
{
	NemoListView *list_view;

	list_view = NEMO_LIST_VIEW (view);

	nemo_list_model_set_should_sort_directories_first (list_view->details->model,
							 nemo_view_should_sort_directories_first (view));
}

static void
nemo_list_view_sort_favorites_first_changed (NemoView *view)
{
	NemoListView *list_view;

	list_view = NEMO_LIST_VIEW (view);

	nemo_list_model_set_should_sort_favorites_first (list_view->details->model,
							 nemo_view_should_sort_favorites_first (view));
}

static int
nemo_list_view_compare_files (NemoView *view, NemoFile *file1, NemoFile *file2)
{
	NemoListView *list_view;

	list_view = NEMO_LIST_VIEW (view);
	return nemo_list_model_compare_func (list_view->details->model, file1, file2);
}

static gboolean
nemo_list_view_using_manual_layout (NemoView *view)
{
	g_return_val_if_fail (NEMO_IS_LIST_VIEW (view), FALSE);

	return FALSE;
}

/* Flat and grouped show the same results a different way, so rebuild from what
   is already loaded rather than running the search again. */
static void
search_grouping_changed_callback (NemoListView *view)
{
	NemoDirectory *directory;
	GList *files, *l;

	directory = nemo_view_get_model (NEMO_VIEW (view));
	if (directory == NULL || !NEMO_IS_SEARCH_DIRECTORY (directory)) {
		return;
	}

	files = nemo_directory_get_file_list (directory);

	stop_cell_editing (view);
	nemo_list_model_clear (view->details->model);
	g_clear_object (&view->details->search_root);

	set_columns_settings_from_metadata_and_preferences (view);

	for (l = files; l != NULL; l = l->next) {
		nemo_list_view_add_file (NEMO_VIEW (view), NEMO_FILE (l->data), directory);
	}

	nemo_file_list_free (files);
}

static void
nemo_list_view_dispose (GObject *object)
{
	NemoListView *list_view;

	list_view = NEMO_LIST_VIEW (object);

	if (list_view->details->model) {
		stop_cell_editing (list_view);
		g_object_unref (list_view->details->model);
		list_view->details->model = NULL;
	}

	if (list_view->details->drag_dest) {
		g_object_unref (list_view->details->drag_dest);
		list_view->details->drag_dest = NULL;
	}

	if (list_view->details->renaming_file_activate_timeout != 0) {
		g_source_remove (list_view->details->renaming_file_activate_timeout);
		list_view->details->renaming_file_activate_timeout = 0;
	}

    if (list_view->details->update_visible_icons_id > 0) {
        g_source_remove (list_view->details->update_visible_icons_id);
        list_view->details->update_visible_icons_id = 0;
    }

    if (list_view->details->resize_columns_id > 0) {
        g_source_remove (list_view->details->resize_columns_id);
        list_view->details->resize_columns_id = 0;
    }

    if (list_view->details->resample_id > 0) {
        g_source_remove (list_view->details->resample_id);
        list_view->details->resample_id = 0;
    }

    if (list_view->details->user_width_settle_id > 0) {
        g_source_remove (list_view->details->user_width_settle_id);
        list_view->details->user_width_settle_id = 0;
    }

	if (list_view->details->clipboard_handler_id != 0) {
		g_signal_handler_disconnect (nemo_clipboard_monitor_get (),
		                             list_view->details->clipboard_handler_id);
		list_view->details->clipboard_handler_id = 0;
	}

    G_OBJECT_CLASS (nemo_list_view_parent_class)->dispose (object);
}

static void
nemo_list_view_finalize (GObject *object)
{
	NemoListView *list_view;

	list_view = NEMO_LIST_VIEW (object);

	g_free (list_view->details->original_name);
	list_view->details->original_name = NULL;

	g_clear_object (&list_view->details->search_root);

	if (list_view->details->double_click_path[0]) {
		gtk_tree_path_free (list_view->details->double_click_path[0]);
	}
	if (list_view->details->double_click_path[1]) {
		gtk_tree_path_free (list_view->details->double_click_path[1]);
	}
	if (list_view->details->new_selection_path) {
		gtk_tree_path_free (list_view->details->new_selection_path);
	}

	g_list_free (list_view->details->cells);
	g_clear_pointer (&list_view->details->measure_columns, g_list_free);
	g_free (list_view->details->measure_style_id);
	g_hash_table_destroy (list_view->details->columns);
	g_hash_table_destroy (list_view->details->samples);
	if (list_view->details->pending_user_widths != NULL) {
		g_hash_table_destroy (list_view->details->pending_user_widths);
	}
	g_hash_table_destroy (list_view->details->user_widths);

	if (list_view->details->hover_path != NULL) {
		gtk_tree_path_free (list_view->details->hover_path);
	}

	if (list_view->details->column_editor != NULL) {
		gtk_widget_destroy (list_view->details->column_editor);
	}

	g_free (list_view->details);

	G_OBJECT_CLASS (nemo_list_view_parent_class)->finalize (object);
}

static char *
nemo_list_view_get_first_visible_file (NemoView *view)
{
	NemoFile *file;
	GtkTreePath *path;
	GtkTreeIter iter;
	NemoListView *list_view;

	list_view = NEMO_LIST_VIEW (view);

	if (gtk_tree_view_get_path_at_pos (list_view->details->tree_view,
					   0, 0,
					   &path, NULL, NULL, NULL)) {
		gtk_tree_model_get_iter (GTK_TREE_MODEL (list_view->details->model),
					 &iter, path);

		gtk_tree_path_free (path);

		gtk_tree_model_get (GTK_TREE_MODEL (list_view->details->model),
				    &iter,
				    NEMO_LIST_MODEL_FILE_COLUMN, &file,
				    -1);
		if (file) {
			char *uri;

			uri = nemo_file_get_uri (file);

			nemo_file_unref (file);

			return uri;
		}
	}

	return NULL;
}

static void
nemo_list_view_scroll_to_file (NemoListView *view,
				   NemoFile *file)
{
	GtkTreePath *path;
	GtkTreeIter iter;

	if (!nemo_list_model_get_first_iter_for_file (view->details->model, file, &iter)) {
		return;
	}

	path = gtk_tree_model_get_path (GTK_TREE_MODEL (view->details->model), &iter);

	gtk_tree_view_scroll_to_cell (view->details->tree_view,
				      path, NULL,
				      TRUE, 0.0, 0.0);

	gtk_tree_path_free (path);
}

static void
list_view_scroll_to_file (NemoView *view,
			  const char *uri)
{
	NemoFile *file;

	if (uri != NULL) {
		/* Only if existing, since we don't want to add the file to
		   the directory if it has been removed since then */
		file = nemo_file_get_existing_by_uri (uri);
		if (file != NULL) {
			nemo_list_view_scroll_to_file (NEMO_LIST_VIEW (view), file);
			nemo_file_unref (file);
		}
	}
}

static void
list_view_notify_clipboard_info (G_GNUC_UNUSED NemoClipboardMonitor *monitor,
                                 NemoClipboardInfo *info,
                                 NemoListView *view)
{
	/* this could be called as a result of _end_loading() being
	 * called after _dispose(), where the model is cleared.
	 */
	if (view->details->model == NULL) {
		return;
	}

	if (info != NULL && info->cut) {
		nemo_list_model_set_highlight_for_files (view->details->model, info->files);
	} else {
		nemo_list_model_set_highlight_for_files (view->details->model, NULL);
	}
}

static void
nemo_list_view_end_loading (NemoView *view,
				G_GNUC_UNUSED gboolean all_files_seen)
{
	NemoClipboardMonitor *monitor;
	NemoClipboardInfo *info;

    set_ok_to_load_deferred_attrs (NEMO_LIST_VIEW (view), TRUE);

	monitor = nemo_clipboard_monitor_get ();
	info = nemo_clipboard_monitor_get_clipboard_info (monitor);

	list_view_notify_clipboard_info (monitor, info, NEMO_LIST_VIEW (view));
}

static const char *
nemo_list_view_get_id (G_GNUC_UNUSED NemoView *view)
{
	return NEMO_LIST_VIEW_ID;
}

static void
nemo_list_view_class_init (NemoListViewClass *class)
{
	NemoViewClass *nemo_view_class;

	nemo_view_class = NEMO_VIEW_CLASS (class);

	G_OBJECT_CLASS (class)->dispose = nemo_list_view_dispose;
	G_OBJECT_CLASS (class)->finalize = nemo_list_view_finalize;

	GTK_WIDGET_CLASS (class)->size_allocate = nemo_list_view_size_allocate;

	nemo_view_class->add_file = nemo_list_view_add_file;
	nemo_view_class->begin_loading = nemo_list_view_begin_loading;
	nemo_view_class->end_loading = nemo_list_view_end_loading;
	nemo_view_class->bump_icon_size = nemo_list_view_bump_icon_size;
	nemo_view_class->can_zoom_in = nemo_list_view_can_zoom_in;
	nemo_view_class->can_zoom_out = nemo_list_view_can_zoom_out;
        nemo_view_class->click_policy_changed = nemo_list_view_click_policy_changed;
	nemo_view_class->clear = nemo_list_view_clear;
	nemo_view_class->file_changed = nemo_list_view_file_changed;
	nemo_view_class->get_backing_uri = nemo_list_view_get_backing_uri;
	nemo_view_class->get_selection = nemo_list_view_get_selection;
    nemo_view_class->peek_selection = nemo_list_view_peek_selection;
    nemo_view_class->get_selection_count = nemo_list_view_get_selection_count;
	nemo_view_class->get_selection_for_file_transfer = nemo_list_view_get_selection_for_file_transfer;
	nemo_view_class->get_item_count = nemo_list_view_get_item_count;
	nemo_view_class->is_empty = nemo_list_view_is_empty;
	nemo_view_class->remove_file = nemo_list_view_remove_file;
    nemo_view_class->merge_menus = nemo_list_view_merge_menus;
    nemo_view_class->unmerge_menus = nemo_list_view_unmerge_menus;
	nemo_view_class->update_menus = nemo_list_view_update_menus;
	nemo_view_class->reset_to_defaults = nemo_list_view_reset_to_defaults;
	nemo_view_class->restore_default_icon_size = nemo_list_view_restore_default_icon_size;
    nemo_view_class->get_default_icon_size = nemo_list_view_get_default_icon_size_vfunc;
	nemo_view_class->reveal_selection = nemo_list_view_reveal_selection;
	nemo_view_class->select_all = nemo_list_view_select_all;
	nemo_view_class->set_selection = nemo_list_view_set_selection;
	nemo_view_class->invert_selection = nemo_list_view_invert_selection;
	nemo_view_class->compare_files = nemo_list_view_compare_files;
	nemo_view_class->sort_directories_first_changed = nemo_list_view_sort_directories_first_changed;
	nemo_view_class->sort_favorites_first_changed = nemo_list_view_sort_favorites_first_changed;
	nemo_view_class->start_renaming_file = nemo_list_view_start_renaming_file;
	nemo_view_class->get_icon_size = nemo_list_view_get_icon_size;
	nemo_view_class->set_icon_size = nemo_list_view_set_icon_size_vfunc;
	nemo_view_class->end_file_changes = nemo_list_view_end_file_changes;
	nemo_view_class->using_manual_layout = nemo_list_view_using_manual_layout;
	nemo_view_class->get_view_id = nemo_list_view_get_id;
	nemo_view_class->get_first_visible_file = nemo_list_view_get_first_visible_file;
	nemo_view_class->get_selection_menu_rect = nemo_list_view_get_selection_menu_rect;
	nemo_view_class->scroll_to_file = list_view_scroll_to_file;
    nemo_view_class->click_to_rename_mode_changed = nemo_list_view_click_to_rename_mode_changed;
}

static void
nemo_list_view_init (NemoListView *list_view)
{
	list_view->details = g_new0 (NemoListViewDetails, 1);

    GtkStyleContext *context = gtk_widget_get_style_context (GTK_WIDGET (list_view));
    gtk_style_context_add_class (context, "view");

	create_and_set_up_tree_view (list_view);

	g_signal_connect_object (nemo_preferences,
				 "changed::" NEMO_PREFERENCES_DEFAULT_SORT_ORDER,
				 G_CALLBACK (default_sort_order_changed_callback),
				 list_view, G_CONNECT_SWAPPED);
	g_signal_connect_object (nemo_preferences,
				 "changed::" NEMO_PREFERENCES_DEFAULT_SORT_IN_REVERSE_ORDER,
				 G_CALLBACK (default_sort_order_changed_callback),
				 list_view, G_CONNECT_SWAPPED);
	g_signal_connect_object (nemo_list_view_preferences,
				 "changed::" NEMO_PREFERENCES_LIST_VIEW_DEFAULT_ICON_SIZE,
				 G_CALLBACK (default_icon_size_changed_callback),
				 list_view, G_CONNECT_SWAPPED);
	g_signal_connect_object (nemo_list_view_preferences,
				 "changed::" NEMO_PREFERENCES_LIST_VIEW_DEFAULT_VISIBLE_COLUMNS,
				 G_CALLBACK (default_visible_columns_changed_callback),
				 list_view, G_CONNECT_SWAPPED);
	g_signal_connect_object (nemo_list_view_preferences,
				 "changed::" NEMO_PREFERENCES_LIST_VIEW_DEFAULT_COLUMN_ORDER,
				 G_CALLBACK (default_column_order_changed_callback),
				 list_view, G_CONNECT_SWAPPED);
	g_signal_connect_object (nemo_list_view_preferences,
				 "changed::" NEMO_PREFERENCES_LIST_VIEW_COLUMN_FIT_PERCENT,
				 G_CALLBACK (column_fit_percent_changed_callback),
				 list_view, G_CONNECT_SWAPPED);
	g_signal_connect_object (nemo_search_preferences,
				 "changed::" NEMO_PREFERENCES_SEARCH_GROUP_BY_FOLDER,
				 G_CALLBACK (search_grouping_changed_callback),
				 list_view, G_CONNECT_SWAPPED);

    g_signal_connect_object (nemo_preferences,
                             "changed::" NEMO_PREFERENCES_TOOLTIPS_LIST_VIEW,
                             G_CALLBACK (tooltip_prefs_changed_callback),
                             list_view, G_CONNECT_SWAPPED);

    g_signal_connect_object (nemo_preferences,
                             "changed::" NEMO_PREFERENCES_TOOLTIP_FILE_TYPE,
                             G_CALLBACK (tooltip_prefs_changed_callback),
                             list_view, G_CONNECT_SWAPPED);

    g_signal_connect_object (nemo_preferences,
                             "changed::" NEMO_PREFERENCES_TOOLTIP_MOD_DATE,
                             G_CALLBACK (tooltip_prefs_changed_callback),
                             list_view, G_CONNECT_SWAPPED);

    g_signal_connect_object (nemo_preferences,
                             "changed::" NEMO_PREFERENCES_TOOLTIP_ACCESS_DATE,
                             G_CALLBACK (tooltip_prefs_changed_callback),
                             list_view, G_CONNECT_SWAPPED);

    g_signal_connect_object (nemo_preferences,
                             "changed::" NEMO_PREFERENCES_TOOLTIP_FULL_PATH,
                             G_CALLBACK (tooltip_prefs_changed_callback),
                             list_view, G_CONNECT_SWAPPED);

    tooltip_prefs_changed_callback (list_view);

	nemo_list_view_click_policy_changed (NEMO_VIEW (list_view));
    nemo_list_view_click_to_rename_mode_changed (NEMO_VIEW (list_view));

	nemo_list_view_sort_directories_first_changed (NEMO_VIEW (list_view));
	nemo_list_view_sort_favorites_first_changed (NEMO_VIEW (list_view));

    list_view->details->current_selection_count = -1;

	/* ensure that the size is always set in begin_loading */
	list_view->details->icon_size = 0;

	list_view->details->hover_path = NULL;
	list_view->details->clipboard_handler_id =
		g_signal_connect (nemo_clipboard_monitor_get (),
		                  "clipboard_info",
		                  G_CALLBACK (list_view_notify_clipboard_info), list_view);

    GtkSettings *gtksettings = gtk_settings_get_default ();
    g_object_get (gtksettings,
                  "gtk-overlay-scrolling", &list_view->details->overlay_scrolling,
                  NULL);
}

static NemoView *
nemo_list_view_create (NemoWindowSlot *slot)
{
	NemoListView *view;

	view = g_object_new (NEMO_TYPE_LIST_VIEW,
			     "window-slot", slot,
			     NULL);
	return NEMO_VIEW (view);
}

static gboolean
nemo_list_view_supports_uri (const char *uri,
				 GFileType file_type,
				 G_GNUC_UNUSED const char *mime_type)
{
	if (file_type == G_FILE_TYPE_DIRECTORY) {
		return TRUE;
	}
	if (g_str_has_prefix (uri, "trash:")) {
		return TRUE;
	}
    if (g_str_has_prefix (uri, "recent:")) {
        return TRUE;
    }
    if (g_str_has_prefix (uri, "favorites:")) {
        return TRUE;
    }
	if (g_str_has_prefix (uri, EEL_SEARCH_URI)) {
		return TRUE;
	}

	return FALSE;
}

static NemoViewInfo nemo_list_view = {
	(char *)NEMO_LIST_VIEW_ID,
	/* translators: this is used in the view selection dropdown
	 * of navigation windows and in the preferences dialog */
	(char *)N_("List view"),
	/* translators: this is used in the view menu */
	(char *)N_("_List"),
	(char *)N_("The list view encountered an error."),
	(char *)N_("The list view encountered an error while starting up."),
	(char *)N_("Display this location with the list view."),
	nemo_list_view_create,
	nemo_list_view_supports_uri
};

void
nemo_list_view_register (void)
{
	nemo_list_view.view_combo_label = _(nemo_list_view.view_combo_label);
	nemo_list_view.view_menu_label_with_mnemonic = _(nemo_list_view.view_menu_label_with_mnemonic);
	nemo_list_view.error_label = _(nemo_list_view.error_label);
	nemo_list_view.startup_error_label = _(nemo_list_view.startup_error_label);
	nemo_list_view.display_location_label = _(nemo_list_view.display_location_label);

	nemo_view_factory_register (&nemo_list_view);
}

/* Returns: (transfer none): owned by @list_view */
GtkTreeView*
nemo_list_view_get_tree_view (NemoListView *list_view)
{
	return list_view->details->tree_view;
}
