/* -*- Mode: C; indent-tabs-mode: f; c-basic-offset: 4; tab-width: 4 -*- */

/* nemo-dnd.c - Common Drag & drop handling code shared by the icon container
   and the list view.

   Copyright (C) 2000, 2001 Eazel, Inc.

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

   Authors: Pavel Cisler <pavel@eazel.com>,
   	    Ettore Perazzoli <ettore@gnu.org>
*/

#include <config.h>
#include "nemo-dnd.h"

#include "nemo-program-choosing.h"
#include "nemo-link.h"
#include <eel/eel-glib-extensions.h>
#include <eel/eel-gtk-extensions.h>
#include <eel/eel-stock-dialogs.h>
#include <eel/eel-string.h>
#include <eel/eel-vfs-extensions.h>
#include <gtk/gtk.h>
#include <glib/gi18n.h>
#include <libnemo-private/nemo-file-utilities.h>
#include <libnemo-private/nemo-global-preferences.h>
#include <libnemo-private/nemo-delete-testguard.h>
#ifdef G_OS_WIN32
#include <libnemo-private/nemo-dnd-win32.h>
#endif
#include <stdio.h>
#include <string.h>

/* a set of defines stolen from the eel-icon-dnd.c file.
 * These are in microseconds.
 */
#define AUTOSCROLL_TIMEOUT_INTERVAL 100
#define AUTOSCROLL_INITIAL_DELAY 100000

/* drag this close to the view edge to start auto scroll*/
#define AUTO_SCROLL_MARGIN 30

/* the smallest amount of auto scroll used when we just enter the autoscroll
 * margin
 */
#define MIN_AUTOSCROLL_DELTA 5
	 
/* the largest amount of auto scroll used when we are right over the view
 * edge
 */
#define MAX_AUTOSCROLL_DELTA 50

void
nemo_drag_init (NemoDragInfo     *drag_info,
		    const GtkTargetEntry *drag_types,
		    int                   drag_type_count,
		    gboolean              add_text_targets)
{
	drag_info->target_list = gtk_target_list_new (drag_types,
						   drag_type_count);

	if (add_text_targets) {
		gtk_target_list_add_text_targets (drag_info->target_list,
						  NEMO_ICON_DND_TEXT);
	}

	drag_info->drop_occured = FALSE;
	drag_info->need_to_destroy = FALSE;
    drag_info->source_fs = NULL;
    drag_info->can_delete_source = FALSE;
}

void
nemo_drag_finalize (NemoDragInfo *drag_info)
{
	gtk_target_list_unref (drag_info->target_list);
	nemo_drag_destroy_selection_list (drag_info->selection_list);
    g_free (drag_info->source_fs);
    drag_info->can_delete_source = FALSE;

	g_free (drag_info);
}


/* Functions to deal with NemoDragSelectionItems.  */

/* Returns: (transfer full): freed with its list by nemo_drag_destroy_selection_list */
NemoDragSelectionItem *
nemo_drag_selection_item_new (void)
{
	return g_new0 (NemoDragSelectionItem, 1);
}

static void
drag_selection_item_destroy (NemoDragSelectionItem *item)
{
	g_free (item->uri);
	g_free (item);
}

void
nemo_drag_destroy_selection_list (GList *list)
{
	GList *p;

	if (list == NULL)
		return;

	for (p = list; p != NULL; p = p->next)
		drag_selection_item_destroy (p->data);

	g_list_free (list);
}

/* Returns: (transfer full): free with g_strfreev */
char **
nemo_drag_uri_array_from_selection_list (const GList *selection_list)
{
	GList *uri_list;
	char **uris;

	uri_list = nemo_drag_uri_list_from_selection_list (selection_list);
	uris = nemo_drag_uri_array_from_list (uri_list);
	g_list_free_full (uri_list, g_free);

	return uris;
}

/* Returns: (transfer full): free with g_list_free_full (list, g_free) */
GList *
nemo_drag_uri_list_from_selection_list (const GList *selection_list)
{
	NemoDragSelectionItem *selection_item;
	GList *uri_list;
	const GList *l;

	uri_list = NULL;
	for (l = selection_list; l != NULL; l = l->next) {
		selection_item = (NemoDragSelectionItem *) l->data;
		if (selection_item->uri != NULL) {
			uri_list = g_list_prepend (uri_list, g_strdup (selection_item->uri));
		}
	}

	return g_list_reverse (uri_list);
}

/* Returns: (transfer full): free with g_strfreev */
char **
nemo_drag_uri_array_from_list (const GList *uri_list)
{
	const GList *l;
	char **uris;
	int i;

	if (uri_list == NULL) {
		return NULL;
	}

	/* +1: the NULL terminator below is written at index n, so an n-element
	   allocation put it one past the end. */
	uris = g_new0 (char *, g_list_length ((GList *) uri_list) + 1);
	for (i = 0, l = uri_list; l != NULL; l = l->next) {
		uris[i++] = g_strdup ((char *) l->data);
	}
	uris[i] = NULL;

	return uris;
}

/* Returns: (transfer full): free with g_list_free_full (list, g_free) */
GList *
nemo_drag_uri_list_from_array (const char **uris)
{
	GList *uri_list;
	int i;

	if (uris == NULL) {
		return NULL;
	}

	uri_list = NULL;

	for (i = 0; uris[i] != NULL; i++) {
		uri_list = g_list_prepend (uri_list, g_strdup (uris[i]));
	}

	return g_list_reverse (uri_list);
}

/* Split out from the public entry so the parser can be exercised on raw
   bytes; GtkSelectionData is opaque and cannot be built in a test.
   Returns: (transfer full): free with nemo_drag_destroy_selection_list */
GList *
nemo_drag_build_selection_list_from_raw (const guchar *raw, int size)
{
	GList *result;
	const guchar *p, *oldp;

	result = NULL;
	oldp = raw;

	while (size > 0) {
		NemoDragSelectionItem *item;
		guint len;

		/* The list is in the form:

		   name\rx:y:width:height\r\n

		   The geometry information after the first \r is optional.  */

		/* 1: Decode name. */

		p = memchr (oldp, '\r', size);
		if (p == NULL) {
			break;
		}

		item = nemo_drag_selection_item_new ();

		len = p - oldp;

		item->uri = g_malloc (len + 1);
		memcpy (item->uri, oldp, len);
		item->uri[len] = 0;

		p++;
		/* p may now sit one past the payload, where GtkSelectionData
		   guarantees a NUL. */
		if (*p == '\n' || *p == '\0') {
			result = g_list_prepend (result, item);
			if (*p == '\0') {
				g_debug ("Invalid x-special/gnome-icon-list data received: "
					   "missing newline character.");
				break;
			} else {
				/* same bookkeeping as the geometry path, or the next
				   memchr scans past the end of the data */
				size -= (p + 1) - oldp;
				oldp = p + 1;
				continue;
			}
		}

		size -= p - oldp;
		oldp = p;

		/* 2: Decode geometry information.  */

		item->got_icon_position = sscanf ((const gchar *) p, "%d:%d:%d:%d%*s",
						  &item->icon_x, &item->icon_y,
						  &item->icon_width, &item->icon_height) == 4;
		if (!item->got_icon_position) {
			g_debug ("Invalid x-special/gnome-icon-list data received: "
				   "invalid icon position specification.");
		}

		result = g_list_prepend (result, item);

		p = memchr (p, '\r', size);
		if (p == NULL || p[1] != '\n') {
			g_debug ("Invalid x-special/gnome-icon-list data received: "
				   "missing newline character.");
			if (p == NULL) {
				break;
			}
		} else {
			p += 2;
		}

		size -= p - oldp;
		oldp = p;
	}

	return g_list_reverse (result);
}

/* Returns: (transfer full): free with nemo_drag_destroy_selection_list */
GList *
nemo_drag_build_selection_list (GtkSelectionData *data)
{
	return nemo_drag_build_selection_list_from_raw (
		gtk_selection_data_get_data (data),
		gtk_selection_data_get_length (data));
}

static gboolean
nemo_drag_file_local_internal (const char *target_uri_string,
				   const char *first_source_uri)
{
	/* check if the first item on the list has target_uri_string as a parent
	 * FIXME:
	 * we should really test each item but that would be slow for large selections
	 * and currently dropped items can only be from the same container
	 */
	GFile *target, *item, *parent;
	gboolean result;

	result = FALSE;

	target = g_file_new_for_uri (target_uri_string);

	/* get the parent URI of the first item in the selection */
	item = g_file_new_for_uri (first_source_uri);
	parent = g_file_get_parent (item);
	g_object_unref (item);
	
	if (parent != NULL) {
		result = g_file_equal (parent, target);
		g_object_unref (parent);
	}
	
	g_object_unref (target);
	
	return result;
}	

gboolean
nemo_drag_uris_local (const char *target_uri,
			  const GList *source_uri_list)
{
	/* must have at least one item */
	g_assert (source_uri_list);
	
	return nemo_drag_file_local_internal (target_uri, source_uri_list->data);
}

gboolean
nemo_drag_items_local (const char *target_uri_string,
			   const GList *selection_list)
{
	/* must have at least one item */
	g_assert (selection_list);

	return nemo_drag_file_local_internal (target_uri_string,
						  ((NemoDragSelectionItem *)selection_list->data)->uri);
}

gboolean
nemo_drag_items_in_trash (const GList *selection_list)
{
	/* check if the first item on the list is in trash.
	 * FIXME:
	 * we should really test each item but that would be slow for large selections
	 * and currently dropped items can only be from the same container
	 */
	return eel_uri_is_trash (((NemoDragSelectionItem *)selection_list->data)->uri);
}

gboolean
nemo_drag_items_on_desktop (const GList *selection_list)
{
	char *uri;
	GFile *desktop, *item, *parent;
	gboolean result;
	
	/* check if the first item on the list is in trash.
	 * FIXME:
	 * we should really test each item but that would be slow for large selections
	 * and currently dropped items can only be from the same container
	 */
	uri = ((NemoDragSelectionItem *)selection_list->data)->uri;
	if (eel_uri_is_desktop (uri)) {
		return TRUE;
	}
	
	desktop = nemo_get_desktop_location ();
	
	item = g_file_new_for_uri (uri);
	parent = g_file_get_parent (item);
	g_object_unref (item);

	result = FALSE;
	
	if (parent) {
		result = g_file_equal (desktop, parent);
		g_object_unref (parent);
	}
	g_object_unref (desktop);
	
	return result;
	
}

GdkDragAction
nemo_drag_default_drop_action_for_netscape_url (GdkDragContext *context)
{
	/* Mozilla defaults to copy, but unless thats the
	   only allowed thing (enforced by ctrl) we want to LINK */
	if (gdk_drag_context_get_suggested_action (context) == GDK_ACTION_COPY &&
	    gdk_drag_context_get_actions (context) != GDK_ACTION_COPY) {
		return GDK_ACTION_LINK;
	} else if (gdk_drag_context_get_suggested_action (context) == GDK_ACTION_MOVE) {
		/* Don't support move */
		return GDK_ACTION_COPY;
	}
	
	return gdk_drag_context_get_suggested_action (context);
}

static gboolean
check_same_fs (NemoFile    *target_file,
               NemoFile    *source_file,
               const gchar *source_fs_for_desktop)
{
    char *target_id, *source_id;
    gboolean result;

    result = FALSE;

    if (target_file != NULL && source_file != NULL) {
        source_id = nemo_file_get_filesystem_id (source_file);
        target_id = nemo_file_get_filesystem_id (target_file);

        if (source_id != NULL && target_id != NULL) {
            result = (strcmp (source_id, target_id) == 0);
        }

        g_free (source_id);
        g_free (target_id);
    } else if (target_file != NULL && source_fs_for_desktop != NULL) {
        target_id = nemo_file_get_filesystem_id (target_file);

        if (target_id != NULL) {
            result = (strcmp (source_fs_for_desktop, target_id) == 0);
        }

        g_free (target_id);
    }

    return result;
}

static gboolean
source_is_deletable (GFile *file)
{
	NemoFile *naut_file;
	gboolean ret;

	/* if there's no a cached NemoFile, it returns NULL */
	naut_file = nemo_file_get_existing (file);
	if (naut_file == NULL) {
		return FALSE;
	}
	
	ret = nemo_file_can_delete (naut_file);
	nemo_file_unref (naut_file);

	return ret;
}

/* Anything dragged in from another program is unknown to us - creating a NemoFile
 * for it is async, so there is nothing to compare filesystems with and the drag
 * reads as a copy every time. Look the source filesystem up once and keep it for
 * the rest of the drag; without it a drag from any other file manager copies
 * where it should move. Only asked once per drag, since it touches the disk. */
static void
remember_source_filesystem (const char  *dropped_uri,
                            gchar      **source_fs,
                            gboolean    *can_delete_source)
{
    GFile *source_file;
    GFileInfo *fs_info;
    GError *error = NULL;

    if (*source_fs != NULL) {
        return;
    }

    source_file = g_file_new_for_uri (dropped_uri);

    if (!g_file_is_native (source_file)) {
        g_object_unref (source_file);
        return;
    }

    fs_info = g_file_query_info (source_file,
                                 G_FILE_ATTRIBUTE_ID_FILESYSTEM "," G_FILE_ATTRIBUTE_ACCESS_CAN_DELETE,
                                 G_FILE_QUERY_INFO_NONE,
                                 NULL,
                                 &error);

    if (error != NULL || fs_info == NULL) {
        g_warning ("Cannot read the filesystem of a dragged file: %s",
                   error != NULL ? error->message : "no info returned");
        g_clear_error (&error);
        *source_fs = NULL;
        *can_delete_source = FALSE;
    } else {
        if (g_file_info_has_attribute (fs_info, G_FILE_ATTRIBUTE_ID_FILESYSTEM)) {
            *source_fs = g_strdup (g_file_info_get_attribute_string (fs_info,
                                                                     G_FILE_ATTRIBUTE_ID_FILESYSTEM));
        }

        if (g_file_info_has_attribute (fs_info, G_FILE_ATTRIBUTE_ACCESS_CAN_DELETE)) {
            *can_delete_source = g_file_info_get_attribute_boolean (fs_info,
                                                                    G_FILE_ATTRIBUTE_ACCESS_CAN_DELETE);
        }
    }

    g_clear_object (&fs_info);
    g_object_unref (source_file);
}

void
nemo_drag_default_drop_action_for_icons (GdkDragContext *context,
                                         const char     *target_uri_string,
                                         const GList    *items,
                                         int            *action,
                                         gchar         **source_fs,
                                         gboolean       *can_delete_source)
{
	gboolean same_fs;
	gboolean target_is_source_parent;
	gboolean source_deletable;
	GdkDragAction forced = 0;
	const char *dropped_uri;
	GFile *target, *dropped, *dropped_directory;
	GdkDragAction actions;
	NemoFile *dropped_file, *target_file;
    gboolean fav_target, fav_item;

	if (target_uri_string == NULL) {
		*action = 0;
		return;
	}

    dropped_uri = ((NemoDragSelectionItem *)items->data)->uri;

    fav_target = eel_uri_is_favorite (target_uri_string);
    fav_item = eel_uri_is_favorite (dropped_uri);

    if (fav_item && fav_target) {
        *action = 0;
        return;
    }

    if (fav_item != fav_target) {
        *action = GDK_ACTION_COPY;
        return;
    }

	actions = gdk_drag_context_get_actions (context) & (GDK_ACTION_MOVE | GDK_ACTION_COPY);
	if (actions == 0) {
		 /* We can't use copy or move, just go with the suggested action. */
		*action = gdk_drag_context_get_suggested_action (context);
		return;
	}

	if (gdk_drag_context_get_suggested_action (context) == GDK_ACTION_ASK) {
		/* Don't override ask */
		*action = gdk_drag_context_get_suggested_action (context);
		return;
	}

    dropped_file = nemo_file_get_existing_by_uri (dropped_uri);

    if (dropped_file == NULL) {
        remember_source_filesystem (dropped_uri, source_fs, can_delete_source);
    }

	target_file = nemo_file_get_existing_by_uri (target_uri_string);
	
	/*
	 * Check for trash URI.  We do a find_directory for any Trash directory.
	 * Passing 0 permissions as gnome-vfs would override the permissions
	 * passed with 700 while creating .Trash directory
	 */
	if (eel_uri_is_trash (target_uri_string)) {
		/* Only move to Trash */
		if (actions & GDK_ACTION_MOVE) {
			*action = GDK_ACTION_MOVE;
		}

		nemo_file_unref (dropped_file);
		nemo_file_unref (target_file);
		return;

	} else if (dropped_file != NULL && nemo_file_is_launcher (dropped_file)) {
		if (actions & GDK_ACTION_MOVE) {
			*action = GDK_ACTION_MOVE;
		}
		nemo_file_unref (dropped_file);
		nemo_file_unref (target_file);
		return;
	} else if (eel_uri_is_desktop (target_uri_string)) {
		target = nemo_get_desktop_location ();

		nemo_file_unref (target_file);
		target_file = nemo_file_get (target);

		if (eel_uri_is_desktop (dropped_uri)) {
			/* Only move to Desktop icons */
			if (actions & GDK_ACTION_MOVE) {
				*action = GDK_ACTION_MOVE;
			}
			
			g_object_unref (target);
			nemo_file_unref (dropped_file);
			nemo_file_unref (target_file);
			return;
		}
	} else if (target_file != NULL && nemo_file_is_archive (target_file)) {
		*action = GDK_ACTION_COPY;

		nemo_file_unref (dropped_file);
		nemo_file_unref (target_file);
		return;
	} else {
		target = g_file_new_for_uri (target_uri_string);
	}

	same_fs = check_same_fs (target_file, dropped_file, *source_fs);

	nemo_file_unref (dropped_file);
	nemo_file_unref (target_file);
	
	/* Compare the first dropped uri with the target uri for same fs match. */
	dropped = g_file_new_for_uri (dropped_uri);
	dropped_directory = g_file_get_parent (dropped);
	target_is_source_parent = FALSE;
	if (dropped_directory != NULL) {
		/* If the dropped file is already in the same directory but
		   is in another filesystem we still want to move, not copy
		   as this is then just a move of a mountpoint to another
		   position in the dir */
		target_is_source_parent = g_file_equal (dropped_directory, target);
		g_object_unref (dropped_directory);
	}
	source_deletable = source_is_deletable (dropped);

#ifdef G_OS_WIN32
	/* A held modifier says outright what to do, and on Windows it cannot
	 * reach us any other way - see nemo_dnd_win32_modifier_action. */
	forced = nemo_dnd_win32_modifier_action ();
#endif

	if (forced != 0) {
		*action = forced;
	} else if ((same_fs && (source_deletable || *can_delete_source)) || target_is_source_parent ||
	    g_file_has_uri_scheme (dropped, "trash")) {
		if (actions & GDK_ACTION_MOVE) {
			*action = GDK_ACTION_MOVE;
		} else {
			*action = gdk_drag_context_get_suggested_action (context);
		}
	} else {
		if (actions & GDK_ACTION_COPY) {
			*action = GDK_ACTION_COPY;
		} else {
			*action = gdk_drag_context_get_suggested_action (context);
		}
	}

	g_object_unref (target);
	g_object_unref (dropped);
	
}

/* The first uri a text/uri-list payload names, which is all the move-or-copy
 * decision needs - a drag is one place to another, whatever it holds.
 * Returns: (transfer full): free with g_free */
char *
nemo_drag_first_uri (GtkSelectionData *data)
{
	char **uris;
	char *first = NULL;

	if (data == NULL) {
		return NULL;
	}

	uris = gtk_selection_data_get_uris (data);
	if (uris != NULL && uris[0] != NULL) {
		first = g_strdup (uris[0]);
	}

	g_strfreev (uris);
	return first;
}

/* Files dragged in from a program that is not nemo arrive as a plain uri list,
 * with none of the per-icon detail, so this is the whole decision for them. It
 * has to reach the same answer as the icon-list path above or a drag from
 * another file manager behaves differently from one out of our own window. */
GdkDragAction
nemo_drag_default_drop_action_for_uri_list (GdkDragContext *context,
						const char *target_uri_string,
						const char *dropped_uri,
						gchar **source_fs,
						gboolean *can_delete_source)
{
	return nemo_drag_drop_action_for_uri_list (gdk_drag_context_get_actions (context),
						   gdk_drag_context_get_suggested_action (context),
						   target_uri_string, dropped_uri,
						   source_fs, can_delete_source);
}

GdkDragAction
nemo_drag_drop_action_for_uri_list (GdkDragAction actions,
				    GdkDragAction suggested,
				    const char *target_uri_string,
				    const char *dropped_uri,
				    gchar **source_fs,
				    gboolean *can_delete_source)
{
	GdkDragAction forced = 0;
	NemoFile *target_file;
	gboolean same_fs;

	if (eel_uri_is_trash (target_uri_string) && (actions & GDK_ACTION_MOVE)) {
		/* Only move to Trash */
		return GDK_ACTION_MOVE;
	}

#ifdef G_OS_WIN32
	forced = nemo_dnd_win32_modifier_action ();
#endif

	if (forced != 0) {
		return forced;
	}

	if (dropped_uri == NULL || target_uri_string == NULL) {
		return suggested;
	}

	remember_source_filesystem (dropped_uri, source_fs, can_delete_source);

	target_file = nemo_file_get_existing_by_uri (target_uri_string);
	same_fs = check_same_fs (target_file, NULL, *source_fs);

	nemo_file_unref (target_file);

	if (same_fs && *can_delete_source && (actions & GDK_ACTION_MOVE)) {
		return GDK_ACTION_MOVE;
	}

	return suggested;
}

/* Encode a "x-special/gnome-icon-list" selection.
   Along with the URIs of the dragged files, this encodes
   the location and size of each icon relative to the cursor.
*/
static void
add_one_gnome_icon (const char *uri, G_GNUC_UNUSED const char *path_str, int x, int y, int w, int h,
		    gpointer data)
{
	GString *result;

	result = (GString *) data;
	g_string_append_printf (result, "%s\r%d:%d:%hu:%hu\r\n",
				uri, x, y, w, h);
}

/*
 * Cf. #48423
 */
#ifdef THIS_WAS_REALLY_BROKEN
static gboolean
is_path_that_gnome_uri_list_extract_filenames_can_parse (const char *path)
{
	if (path == NULL || path [0] == '\0') {
		return FALSE;
	}

	/* It strips leading and trailing spaces. So it can't handle
	 * file names with leading and trailing spaces.
	 */
	if (g_ascii_isspace (path [0])) {
		return FALSE;
	}
	if (g_ascii_isspace (path [strlen (path) - 1])) {
		return FALSE;
	}

	/* # works as a comment delimiter, and \r and \n are used to
	 * separate the lines, so it can't handle file names with any
	 * of these.
	 */
	if (strchr (path, '#') != NULL
	    || strchr (path, '\r') != NULL
	    || strchr (path, '\n') != NULL) {
		return FALSE;
	}

	return TRUE;
}

/* Encode a "text/plain" selection; this is a broken URL -- just
 * "file:" with a path after it (no escaping or anything). We are
 * trying to make the old gnome_uri_list_extract_filenames function
 * happy, so this is coded to its idiosyncrasises.
 */
static void
add_one_compatible_uri (const char *uri, int x, int y, int w, int h, gpointer data)
{
	GString *result;
	char *local_path;
	
	result = (GString *) data;

	/* For URLs that do not have a file: scheme, there's no harm
	 * in passing the real URL. But for URLs that do have a file:
	 * scheme, we have to send a URL that will work with the old
	 * gnome-libs function or nothing will be able to understand
	 * it.
	 */
	if (!g_str_has_prefix (uri, "file:")) {
		g_string_append (result, uri);
		g_string_append (result, "\r\n");
	} else {
		local_path = g_filename_from_uri (uri, NULL, NULL);

		/* Check for characters that confuse the old
		 * gnome_uri_list_extract_filenames implementation, and just leave
		 * out any paths with those in them.
		 */
		if (is_path_that_gnome_uri_list_extract_filenames_can_parse (local_path)) {
			g_string_append (result, "file:");
			g_string_append (result, local_path);
			g_string_append (result, "\r\n");
		}

		g_free (local_path);
	}
}
#endif

static void
add_one_uri (const char *uri, G_GNUC_UNUSED const char *path_str, G_GNUC_UNUSED int x, G_GNUC_UNUSED int y, G_GNUC_UNUSED int w, G_GNUC_UNUSED int h, gpointer data)
{
	GString *result;
	
	result = (GString *) data;

	g_string_append (result, uri);
	g_string_append (result, "\r\n");
}

static void
add_one_path (G_GNUC_UNUSED const char *uri, const char *path_str, G_GNUC_UNUSED int x, G_GNUC_UNUSED int y, G_GNUC_UNUSED int w, G_GNUC_UNUSED int h, gpointer data)
{
    GString *result;

    result = (GString *) data;

    g_string_append (result, path_str);
    g_string_append (result, "\r\n");
}

/* Returns: (transfer full): free with g_free */
char *
nemo_drag_selection_payload (guint info,
			     gpointer container_context,
			     NemoDragEachSelectedItemIterator each_selected_item_iterator)
{
	GString *result;
	NemoDragEachSelectedItemDataGet add_one;

	switch (info) {
	case NEMO_ICON_DND_GNOME_ICON_LIST:
		add_one = add_one_gnome_icon;
		break;

	case NEMO_ICON_DND_URI_LIST:
		add_one = add_one_uri;
		break;

	case NEMO_ICON_DND_TEXT:
		add_one = add_one_path;
		break;

	default:
		return NULL;
	}

	result = g_string_new (NULL);
	(* each_selected_item_iterator) (add_one, container_context, result);

	return g_string_free (result, FALSE);
}

/* Common function for drag_data_get_callback calls.
 * Returns FALSE if it doesn't handle drag data */
gboolean
nemo_drag_drag_data_get (G_GNUC_UNUSED GtkWidget *widget,
			G_GNUC_UNUSED GdkDragContext *context,
			GtkSelectionData *selection_data,
			guint info,
			G_GNUC_UNUSED guint32 time,
			gpointer container_context,
			NemoDragEachSelectedItemIterator each_selected_item_iterator)
{
	char *result;

	result = nemo_drag_selection_payload (info, container_context,
					      each_selected_item_iterator);
	if (result == NULL) {
		return FALSE;
	}

	gtk_selection_data_set (selection_data,
				gtk_selection_data_get_target (selection_data),
				8, (guchar *) result, strlen (result));
	g_free (result);

	return TRUE;
}

typedef struct
{
	GMainLoop *loop;
	GdkDragAction chosen;
} DropActionMenuData;

static void
menu_deactivate_callback (G_GNUC_UNUSED GtkWidget *menu,
			  gpointer   data)
{
	DropActionMenuData *damd;
	
	damd = data;

	if (g_main_loop_is_running (damd->loop))
		g_main_loop_quit (damd->loop);
}

static void
drop_action_activated_callback (GtkWidget  *menu_item,
				gpointer    data)
{
	DropActionMenuData *damd;
	
	damd = data;

	damd->chosen = GPOINTER_TO_INT (g_object_get_data (G_OBJECT (menu_item),
							   "action"));

	if (g_main_loop_is_running (damd->loop))
		g_main_loop_quit (damd->loop);
}

static void
append_drop_action_menu_item (GtkWidget          *menu,
			      const char         *text,
			      GdkDragAction       action,
			      gboolean            sensitive,
			      DropActionMenuData *damd)
{
	GtkWidget *menu_item;

	menu_item = gtk_menu_item_new_with_mnemonic (text);
	gtk_widget_set_sensitive (menu_item, sensitive);
	gtk_menu_shell_append (GTK_MENU_SHELL (menu), menu_item);

	g_object_set_data (G_OBJECT (menu_item),
			   "action",
			   GINT_TO_POINTER (action));
	
	g_signal_connect (menu_item, "activate",
			  G_CALLBACK (drop_action_activated_callback),
			  damd);

	gtk_widget_show (menu_item);
}

/* Pops up a menu of actions to perform on dropped files */
GdkDragAction
nemo_drag_drop_action_ask (GtkWidget *widget,
			       GdkDragAction actions)
{
	GtkWidget *menu;
	GtkWidget *menu_item;
	DropActionMenuData damd;
	
	/* Create the menu and set the sensitivity of the items based on the
	 * allowed actions.
	 */
	menu = gtk_menu_new ();
	gtk_menu_set_screen (GTK_MENU (menu), gtk_widget_get_screen (widget));
	
	append_drop_action_menu_item (menu, _("_Move here"),
				      GDK_ACTION_MOVE,
				      (actions & GDK_ACTION_MOVE) != 0,
				      &damd);

	append_drop_action_menu_item (menu, _("_Copy here"),
				      GDK_ACTION_COPY,
				      (actions & GDK_ACTION_COPY) != 0,
				      &damd);
	
	/* Opens the Make link dialog. */
	append_drop_action_menu_item (menu, _("_Link here..."),
				      GDK_ACTION_LINK,
				      (actions & GDK_ACTION_LINK) != 0,
				      &damd);

	eel_gtk_menu_append_separator (GTK_MENU (menu));
	
	menu_item = gtk_menu_item_new_with_mnemonic (_("Cancel"));
	gtk_menu_shell_append (GTK_MENU_SHELL (menu), menu_item);
	gtk_widget_show (menu_item);
	
	damd.chosen = 0;
	damd.loop = g_main_loop_new (NULL, FALSE);

	g_signal_connect (menu, "deactivate",
			  G_CALLBACK (menu_deactivate_callback),
			  &damd);
	
	gtk_grab_add (menu);
	
	gtk_menu_popup (GTK_MENU (menu), NULL, NULL,
			NULL, NULL, 0, GDK_CURRENT_TIME);
	
	g_main_loop_run (damd.loop);

	gtk_grab_remove (menu);
	
	g_main_loop_unref (damd.loop);

	g_object_ref_sink (menu);
	g_object_unref (menu);

	return damd.chosen;
}

gboolean
nemo_drag_confirm_needed (GdkDragAction action,
			      const char   *target_uri)
{
	/* The trash asks for itself, under its own setting. */
	if (target_uri != NULL && eel_uri_is_trash (target_uri)) {
		return FALSE;
	}

	switch (action) {
	case GDK_ACTION_MOVE:
		/* Armed, the test guard asks about every move job, so this would
		   be the same question twice. */
		if (nemo_delete_testguard_armed ()) {
			return FALSE;
		}
		return nemo_config_get_boolean (nemo_preferences,
						NEMO_PREFERENCES_CONFIRM_DRAG_MOVE);
	case GDK_ACTION_COPY:
		return nemo_config_get_boolean (nemo_preferences,
						NEMO_PREFERENCES_CONFIRM_DRAG_COPY);
	case GDK_ACTION_LINK:
	case GDK_ACTION_DEFAULT:
		/* Both end up as links, and the Make link dialog asks. */
		return FALSE;
	default:
		return FALSE;
	}
}

/* Full destination, spelled the way the location bar would. */
static char *
drag_parse_name (const char *uri)
{
	GFile *file;
	char  *path;

	if (uri == NULL) {
		return NULL;
	}

	file = g_file_new_for_uri (uri);
	path = g_file_get_parse_name (file);
	g_object_unref (file);

	return path;
}

/* Basename of a URI, for the prompt. NULL when there is nothing to show. */
static char *
drag_display_name (const char *uri)
{
	GFile *file;
	char  *name;

	if (uri == NULL) {
		return NULL;
	}

	file = g_file_new_for_uri (uri);
	name = g_file_get_basename (file);
	g_object_unref (file);

	if (name != NULL && *name == '\0') {
		g_clear_pointer (&name, g_free);
	}

	return name;
}

gboolean
nemo_drag_confirm_drop (GtkWidget     *parent,
			    GdkDragAction  action,
			    const GList   *item_uris,
			    const char    *target_uri)
{
	char       *folder, *item, *primary, *where;
	const char *verb;
	guint       count;
	int         response;

	count = g_list_length ((GList *) item_uris);

	if (count == 0 || !nemo_drag_confirm_needed (action, target_uri)) {
		return TRUE;
	}

	folder = drag_display_name (target_uri);
	if (folder == NULL) {
		folder = g_strdup (target_uri != NULL ? target_uri : "");
	}
	item = count == 1 ? drag_display_name (item_uris->data) : NULL;

	/* The named form covers one item, so the singular below is never used -
	   it is there for the languages whose plural form varies with the count. */
	switch (action) {
	case GDK_ACTION_MOVE:
		primary = item != NULL
			? g_strdup_printf (_("Move \"%s\" to \"%s\"?"), item, folder)
			: g_strdup_printf (ngettext ("Move %u item to \"%s\"?",
						     "Move %u items to \"%s\"?", count),
					   count, folder);
		verb = _("_Move");
		break;
	case GDK_ACTION_COPY:
		primary = item != NULL
			? g_strdup_printf (_("Copy \"%s\" to \"%s\"?"), item, folder)
			: g_strdup_printf (ngettext ("Copy %u item to \"%s\"?",
						     "Copy %u items to \"%s\"?", count),
					   count, folder);
		verb = _("_Copy");
		break;
	default:
		primary = item != NULL
			? g_strdup_printf (_("Link \"%s\" in \"%s\"?"), item, folder)
			: g_strdup_printf (ngettext ("Link %u item in \"%s\"?",
						     "Link %u items in \"%s\"?", count),
					   count, folder);
		verb = _("_Link");
		break;
	}

	where = drag_parse_name (target_uri);

	response = eel_run_simple_dialog (parent, TRUE, GTK_MESSAGE_QUESTION,
					  primary, where,
					  GTK_STOCK_CANCEL, verb, NULL);

	g_free (primary);
	g_free (where);
	g_free (folder);
	g_free (item);

	return response == 1;
}

gboolean
nemo_drag_autoscroll_in_scroll_region (GtkWidget *widget)
{
	float x_scroll_delta, y_scroll_delta;

	nemo_drag_autoscroll_calculate_delta (widget, &x_scroll_delta, &y_scroll_delta);

	return x_scroll_delta != 0 || y_scroll_delta != 0;
}


void
nemo_drag_autoscroll_calculate_delta (GtkWidget *widget, float *x_scroll_delta, float *y_scroll_delta)
{
	GtkAllocation allocation;
	GdkDeviceManager *manager;
	GdkDevice *pointer;
	int x, y;

	g_assert (GTK_IS_WIDGET (widget));

	manager = gdk_display_get_device_manager (gtk_widget_get_display (widget));
	pointer = gdk_device_manager_get_client_pointer (manager);
	gdk_window_get_device_position (gtk_widget_get_window (widget), pointer,
					&x, &y, NULL);

	/* Find out if we are anywhere close to the tree view edges
	 * to see if we need to autoscroll.
	 */
	*x_scroll_delta = 0;
	*y_scroll_delta = 0;
	
	if (x < AUTO_SCROLL_MARGIN) {
		*x_scroll_delta = (float)(x - AUTO_SCROLL_MARGIN);
	}

	gtk_widget_get_allocation (widget, &allocation);
	if (x > allocation.width - AUTO_SCROLL_MARGIN) {
		if (*x_scroll_delta != 0) {
			/* Already trying to scroll because of being too close to 
			 * the top edge -- must be the window is really short,
			 * don't autoscroll.
			 */
			return;
		}
		*x_scroll_delta = (float)(x - (allocation.width - AUTO_SCROLL_MARGIN));
	}

	if (y < AUTO_SCROLL_MARGIN) {
		*y_scroll_delta = (float)(y - AUTO_SCROLL_MARGIN);
	}

	if (y > allocation.height - AUTO_SCROLL_MARGIN) {
		if (*y_scroll_delta != 0) {
			/* Already trying to scroll because of being too close to 
			 * the top edge -- must be the window is really narrow,
			 * don't autoscroll.
			 */
			return;
		}
		*y_scroll_delta = (float)(y - (allocation.height - AUTO_SCROLL_MARGIN));
	}

	if (*x_scroll_delta == 0 && *y_scroll_delta == 0) {
		/* no work */
		return;
	}

	/* Adjust the scroll delta to the proper acceleration values depending on how far
	 * into the sroll margins we are.
	 * FIXME bugzilla.eazel.com 2486:
	 * we could use an exponential acceleration factor here for better feel
	 */
	if (*x_scroll_delta != 0) {
		*x_scroll_delta /= AUTO_SCROLL_MARGIN;
		*x_scroll_delta *= (MAX_AUTOSCROLL_DELTA - MIN_AUTOSCROLL_DELTA);
		*x_scroll_delta += MIN_AUTOSCROLL_DELTA;
	}
	
	if (*y_scroll_delta != 0) {
		*y_scroll_delta /= AUTO_SCROLL_MARGIN;
		*y_scroll_delta *= (MAX_AUTOSCROLL_DELTA - MIN_AUTOSCROLL_DELTA);
		*y_scroll_delta += MIN_AUTOSCROLL_DELTA;
	}

}



void
nemo_drag_autoscroll_start (NemoDragInfo *drag_info,
				GtkWidget        *widget,
				GSourceFunc       callback,
				gpointer          user_data)
{
	if (nemo_drag_autoscroll_in_scroll_region (widget)) {
		if (drag_info->auto_scroll_timeout_id == 0) {
			drag_info->waiting_to_autoscroll = TRUE;
			drag_info->start_auto_scroll_in = g_get_monotonic_time () 
				+ AUTOSCROLL_INITIAL_DELAY;
			
			drag_info->auto_scroll_timeout_id = g_timeout_add
				(AUTOSCROLL_TIMEOUT_INTERVAL,
				 callback,
			 	 user_data);
		}
	} else {
		if (drag_info->auto_scroll_timeout_id != 0) {
			g_source_remove (drag_info->auto_scroll_timeout_id);
			drag_info->auto_scroll_timeout_id = 0;
		}
	}
}

void
nemo_drag_autoscroll_stop (NemoDragInfo *drag_info)
{
	if (drag_info->auto_scroll_timeout_id != 0) {
		g_source_remove (drag_info->auto_scroll_timeout_id);
		drag_info->auto_scroll_timeout_id = 0;
	}
}

gboolean
nemo_drag_selection_includes_special_link (GList *selection_list)
{
	GList *node;
	char *uri;

	for (node = selection_list; node != NULL; node = node->next) {
		uri = ((NemoDragSelectionItem *) node->data)->uri;

		if (eel_uri_is_desktop (uri)) {
			return TRUE;
		}
	}
	
	return FALSE;
}
