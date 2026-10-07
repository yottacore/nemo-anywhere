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

#include <config.h>
#include "nemo-column-utilities.h"

#include <string.h>
#include <eel/eel-glib-extensions.h>
#include <glib/gi18n.h>
#include <libnemo-extension/nemo-column-provider.h>
#include <libnemo-private/nemo-module.h>
#include <libnemo-private/nemo-global-preferences.h>

static GList *
get_builtin_columns (void)
{
	GList *columns;

	columns = g_list_append (NULL,
				 g_object_new (NEMO_TYPE_COLUMN,
					       "name", "name",
					       "attribute", "name",
					       "label", _("Name"),
					       "description", _("The name and icon of the file."),
					       NULL));
	columns = g_list_append (columns,
				 g_object_new (NEMO_TYPE_COLUMN,
					       "name", "extension",
					       "attribute", "extension",
					       "unbounded", TRUE,
					       "label", _("Ext"),
					       "description", _("The extension of the file's name."),
					       NULL));
	columns = g_list_append (columns,
				 g_object_new (NEMO_TYPE_COLUMN,
					       "name", "size",
					       "attribute", "size",
					       "label", _("Size"),
					       "description", _("The size of the file."),
					       "xalign", 1.0,
					       NULL));
	columns = g_list_append (columns,
				 g_object_new (NEMO_TYPE_COLUMN,
					       "name", "type",
					       "attribute", "type",
					       "label", _("Type"),
					       "description", _("The general type of the file."),
					       "unbounded", TRUE,
					       NULL));
#if NEMO_COLUMNS_SHOW_DETAILED_TYPE
	columns = g_list_append (columns,
				 g_object_new (NEMO_TYPE_COLUMN,
					       "name", "detailed_type",
					       "attribute", "detailed_type",
					       "label", _("Detailed type"),
					       "description", _("The specific type of the file."),
					       "unbounded", TRUE,
					       NULL));
#endif
	/* Three dates, the same three on every platform. The times themselves
	 * come from GIO, which reads whatever the OS keeps them in - statx birth
	 * time on Linux, the Win32 creation time on Windows - so nothing here is
	 * per-platform. The "- Time" twins these replaced showed the same instant
	 * a second way and doubled the length of the column list. */
	columns = g_list_append (columns,
				 g_object_new (NEMO_TYPE_COLUMN,
					       "name", "date_created",
					       "attribute", "date_created",
					       "label", _("Date created"),
					       "description", _("The date the file was created."),
					       NULL));
	columns = g_list_append (columns,
				 g_object_new (NEMO_TYPE_COLUMN,
					       "name", "date_modified",
					       "attribute", "date_modified",
					       "label", _("Date modified"),
					       "description", _("The date the file was modified."),
					       NULL));
	columns = g_list_append (columns,
				 g_object_new (NEMO_TYPE_COLUMN,
					       "name", "date_accessed",
					       "attribute", "date_accessed",
					       "label", _("Date read"),
					       "description", _("The date the file was last read."),
					       NULL));

	/* Real on Windows too: GIO answers owner::user with the file's actual
	 * owner there, unlike the fabricated mode bits below. */
	columns = g_list_append (columns,
				 g_object_new (NEMO_TYPE_COLUMN,
					       "name", "owner",
					       "attribute", "owner",
					       "label", _("Owner"),
					       "description", _("The user name of the file's owner."),
					       "unbounded", TRUE,
					       NULL));

	columns = g_list_append (columns,
				 g_object_new (NEMO_TYPE_COLUMN,
					       "name", "owner_name",
					       "attribute", "owner_name",
					       "label", _("Owner name"),
					       "description", _("The display name of the file's owner."),
					       "unbounded", TRUE,
					       NULL));
	columns = g_list_append (columns,
				 g_object_new (NEMO_TYPE_COLUMN,
					       "name", "owner_and_name",
					       "attribute", "owner_and_name",
					       "label", _("Owner - name"),
					       "description", _("The user name and display name of the file's owner."),
					       "unbounded", TRUE,
					       NULL));

#ifdef G_OS_WIN32
	columns = g_list_append (columns,
				 g_object_new (NEMO_TYPE_COLUMN,
					       "name", "permissions_source",
					       "attribute", "permissions_source",
					       "label", _("Permissions source"),
					       "description", _("Whether the file's permissions are inherited, set on the file itself, or both."),
					       NULL));
#else
	/* POSIX ownership/mode - meaningless (fabricated) on Windows */
	columns = g_list_append (columns,
				 g_object_new (NEMO_TYPE_COLUMN,
					       "name", "group",
					       "attribute", "group",
					       "label", _("Group"),
					       "description", _("The group of the file."),
					       "unbounded", TRUE,
					       NULL));

	columns = g_list_append (columns,
				 g_object_new (NEMO_TYPE_COLUMN,
					       "name", "permissions",
					       "attribute", "permissions",
					       "label", _("Permissions"),
					       "description", _("The permissions of the file."),
					       NULL));

	columns = g_list_append (columns,
				 g_object_new (NEMO_TYPE_COLUMN,
					       "name", "octal_permissions",
					       "attribute", "octal_permissions",
					       "label", _("Octal permissions"),
					       "description", _("The permissions of the file, in octal notation."),
					       NULL));
#endif

#if NEMO_COLUMNS_SHOW_MIME_TYPE
	columns = g_list_append (columns,
				 g_object_new (NEMO_TYPE_COLUMN,
					       "name", "mime_type",
					       "attribute", "mime_type",
					       "label", _("MIME type"),
					       "description", _("The mime type of the file."),
					       "unbounded", TRUE,
					       NULL));
#endif
#ifdef HAVE_SELINUX
	columns = g_list_append (columns,
				 g_object_new (NEMO_TYPE_COLUMN,
					       "name", "selinux_context",
					       "attribute", "selinux_context",
					       "label", _("SELinux context"),
					       "description", _("The SELinux security context of the file."),
					       "unbounded", TRUE,
					       NULL));
#endif
	columns = g_list_append (columns,
				 g_object_new (NEMO_TYPE_COLUMN,
					       "name", "where",
					       "attribute", "where",
					       "label", _("Location"),
					       "description", _("The location of the file."),
					       "unbounded", TRUE,
                           "ellipsize", PANGO_ELLIPSIZE_END,
					       NULL));

	return columns;
}

static GList *
get_extension_columns (void)
{
	GList *columns;
	GList *providers;
	GList *l;
	
	providers = nemo_module_get_extensions_for_type (NEMO_TYPE_COLUMN_PROVIDER);
	
	columns = NULL;
	
	for (l = providers; l != NULL; l = l->next) {
		NemoColumnProvider *provider;
		GList *provider_columns;
		
		provider = NEMO_COLUMN_PROVIDER (l->data);
		provider_columns = nemo_column_provider_get_columns (provider);
		columns = g_list_concat (columns, provider_columns);
	}

	nemo_module_extension_list_free (providers);

	return columns;
}

static GList *
get_trash_columns (void)
{
	static GList *columns = NULL;

	if (columns == NULL) {
		columns = g_list_append (columns,
					 g_object_new (NEMO_TYPE_COLUMN,
						       "name", "trashed_on",
						       "attribute", "trashed_on",
						       "label", _("Trashed on"),
						       "description", _("Date when file was moved to the Trash"),
						       NULL));
		columns = g_list_append (columns,
			                 g_object_new (NEMO_TYPE_COLUMN,
			                               "name", "trash_orig_path",
			                               "attribute", "trash_orig_path",
			                               "label", _("Original location"),
			                               "description", _("Original location of file before moved to the Trash"),
			                               NULL));
	}

	return nemo_column_list_copy (columns);
}

static GList *
get_search_columns (void)
{
    static GList *columns = NULL;

    if (columns == NULL) {
        // columns = g_list_append (columns,
        //              g_object_new (NEMO_TYPE_COLUMN,
        //                        "name", "search-result-snippet",
        //                        "attribute", "search_result_snippet",
        //                        "label", _("Result"),
        //                        "description", _("A portion of the contents where the string was found"),
        //                        NULL));
        columns = g_list_append (columns,
                             g_object_new (NEMO_TYPE_COLUMN,
                                           "name", "search_result_count",
                                           "attribute", "search_result_count",
                                           "label", _("Hits"),
                                           "description", _("How many times the search string appeared in the file"),
                                           NULL));
    }

    return nemo_column_list_copy (columns);
}


/* Returns: (transfer full): free with nemo_column_list_free */
GList *
nemo_get_common_columns (void)
{
	static GList *columns = NULL;

	if (!columns) {
		columns = g_list_concat (get_builtin_columns (),
		                         get_extension_columns ());
	}

	return nemo_column_list_copy (columns);
}

/* Returns: (transfer full): free with nemo_column_list_free */
GList *
nemo_get_all_columns (void)
{
    GList *columns = NULL;
	GList *with_search_columns = NULL;

	columns = g_list_concat (nemo_get_common_columns (),
	                         get_trash_columns ());

    with_search_columns = g_list_concat (columns, get_search_columns ());

	return with_search_columns;
}

/* Returns: (transfer full): free with nemo_column_list_free */
GList *
nemo_get_columns_for_file (NemoFile *file)
{
	GList *columns;

	columns = nemo_get_common_columns ();

	if (file != NULL && nemo_file_is_in_trash (file)) {
		columns = g_list_concat (columns,
		                         get_trash_columns ());
	} else
    if (file != NULL && nemo_file_is_in_search (file)) {
        columns = g_list_concat (columns,
                                 get_search_columns ());
    }

	return columns;
}

/* Returns: (transfer full): free with nemo_column_list_free */
GList *
nemo_column_list_copy (GList *columns) 
{
	GList *ret;
	GList *l;
	
	ret = g_list_copy (columns);
	
	for (l = ret; l != NULL; l = l->next) {
		g_object_ref (l->data);
	}

	return ret;
}

void
nemo_column_list_free (GList *columns)
{
	GList *l;
	
	for (l = columns; l != NULL; l = l->next) {
		g_object_unref (l->data);
	}
	
	g_list_free (columns);
}

static int
strv_index (char **strv, const char *str)
{
	int i;

	for (i = 0; strv[i] != NULL; ++i) {
		if (strcmp (strv[i], str) == 0)
			return i;
	}

	return -1;
}

static int
column_compare (NemoColumn *a, NemoColumn *b, char **column_order)
{
	int index_a;
	int index_b;
	char *name;
	
	g_object_get (G_OBJECT (a), "name", &name, NULL);
	index_a = strv_index (column_order, name);
	g_free (name);

	g_object_get (G_OBJECT (b), "name", &name, NULL);
	index_b = strv_index (column_order, name);
	g_free (name);

	if (index_a == index_b) {
		int ret;
		char *label_a;
		char *label_b;
		
		g_object_get (G_OBJECT (a), "label", &label_a, NULL);
		g_object_get (G_OBJECT (b), "label", &label_b, NULL);
		ret = strcmp (label_a, label_b);
		g_free (label_a);
		g_free (label_b);
		
		return ret;
	} else if (index_a == -1) {
		return 1;
	} else if (index_b == -1) {
		return -1;
	} else {
		return index_a - index_b;
	}
}

/* Returns: (transfer full): @columns sorted in place, owned as @columns was */
GList *
nemo_sort_columns (GList  *columns, 
		       char  **column_order)
{
	if (!column_order) {
		return NULL;
	}

	return g_list_sort_with_data (columns,
				      (GCompareDataFunc)column_compare,
				      column_order);
}

/* Find results are not a folder, so they keep one set of columns of their own,
 * whatever remember-folder-settings says. The key is the shown columns in
 * their order; empty means these defaults. */
static const char *const search_visible_columns[] = {
	"name", "extension", "size", "date_modified", "where", NULL
};

static const char *const search_column_order[] = {
	"name", "extension", "type", "size", "date_modified", "date_created",
	"date_accessed", "where", NULL
};

static char **
saved_search_columns (void)
{
	char **saved = nemo_config_get_strv (nemo_search_preferences,
					     NEMO_PREFERENCES_SEARCH_VISIBLE_COLUMNS);

	if (saved != NULL && saved[0] != NULL) {
		return saved;
	}

	g_strfreev (saved);
	return NULL;
}

/* Returns: (transfer full): free with g_strfreev */
char **
nemo_search_columns_get_default_visible (void)
{
	return g_strdupv ((char **) search_visible_columns);
}

/* Returns: (transfer full): free with g_strfreev */
char **
nemo_search_columns_get_default_order (void)
{
	return g_strdupv ((char **) search_column_order);
}

/* Returns: (transfer full): free with g_strfreev */
char **
nemo_search_columns_get_visible (void)
{
	char **saved = saved_search_columns ();

	return saved != NULL ? saved : nemo_search_columns_get_default_visible ();
}

/* Returns: (transfer full): free with g_strfreev */
char **
nemo_search_columns_get_order (void)
{
	char **saved = saved_search_columns ();

	return saved != NULL ? saved : nemo_search_columns_get_default_order ();
}

void
nemo_search_columns_save (const char *const *visible_in_order)
{
	if (visible_in_order == NULL) {
		nemo_config_reset (nemo_search_preferences,
				   NEMO_PREFERENCES_SEARCH_VISIBLE_COLUMNS);
		return;
	}

	nemo_config_set_strv (nemo_search_preferences,
			      NEMO_PREFERENCES_SEARCH_VISIBLE_COLUMNS,
			      visible_in_order);
}

/* Returns FALSE with nothing saved, and then *attribute is NULL.
 * Returns: (transfer full) in *attribute: free with g_free */
gboolean
nemo_search_sort_get (char **attribute, gboolean *reversed)
{
	char *saved = nemo_config_get_string (nemo_search_preferences,
					      NEMO_PREFERENCES_SEARCH_SORT_COLUMN);

	if (saved == NULL || saved[0] == '\0') {
		g_free (saved);
		*attribute = NULL;
		return FALSE;
	}

	*attribute = saved;
	*reversed = nemo_config_get_boolean (nemo_search_preferences,
					     NEMO_PREFERENCES_SEARCH_REVERSE_SORT);
	return TRUE;
}

/* Every load of find results comes through here or save, so leave the file alone
 * when nothing changes. */
void
nemo_search_sort_forget (void)
{
	char *saved = nemo_config_get_string (nemo_search_preferences,
					      NEMO_PREFERENCES_SEARCH_SORT_COLUMN);

	if (saved != NULL && saved[0] != '\0') {
		nemo_config_reset (nemo_search_preferences, NEMO_PREFERENCES_SEARCH_SORT_COLUMN);
	}
	g_free (saved);

	if (nemo_config_get_boolean (nemo_search_preferences, NEMO_PREFERENCES_SEARCH_REVERSE_SORT)) {
		nemo_config_reset (nemo_search_preferences, NEMO_PREFERENCES_SEARCH_REVERSE_SORT);
	}
}

/* The default is kept as nothing, as a folder's is, so a new default sort
 * order still reaches find results. */
void
nemo_search_sort_save (const char *attribute,
		       gboolean    reversed,
		       const char *default_attribute,
		       gboolean    default_reversed)
{
	if (attribute == NULL ||
	    (g_strcmp0 (attribute, default_attribute) == 0 && reversed == default_reversed)) {
		nemo_search_sort_forget ();
		return;
	}

	{
		char *saved = NULL;
		gboolean saved_reversed = FALSE;
		gboolean same = nemo_search_sort_get (&saved, &saved_reversed) &&
				g_strcmp0 (saved, attribute) == 0 &&
				saved_reversed == reversed;

		g_free (saved);
		if (same) {
			return;
		}
	}

	nemo_config_set_string (nemo_search_preferences,
				NEMO_PREFERENCES_SEARCH_SORT_COLUMN, attribute);
	nemo_config_set_boolean (nemo_search_preferences,
				 NEMO_PREFERENCES_SEARCH_REVERSE_SORT, reversed);
}
