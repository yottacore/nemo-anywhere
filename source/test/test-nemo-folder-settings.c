/* Which folder's saved view settings apply: nothing while remembering is off,
 * a folder's own set first, else the nearest parent with a set when inheriting
 * is on. A change in a folder that was inheriting keeps the values it was
 * using, and a set whose values match the defaults still stops inheritance. */

#include <config.h>

#include <stdlib.h>
#include <string.h>
#include <glib/gstdio.h>
#include <gtk/gtk.h>

#include <libnemo-private/nemo-file.h>
#include <libnemo-private/nemo-folder-settings.h>
#include <libnemo-private/nemo-global-preferences.h>
#include <libnemo-private/nemo-metadata.h>
#include <libnemo-private/nemo-metadata-store.h>

#include "test-scratch.h"
#include "test-check.h"

static void
drain (void)
{
	while (g_main_context_iteration (NULL, FALSE)) {
	}
}

static void
set_prefs (gboolean remember, gboolean inherit)
{
	nemo_config_set_boolean (nemo_preferences, NEMO_PREFERENCES_REMEMBER_FOLDER_SETTINGS, remember);
	nemo_config_set_boolean (nemo_preferences, NEMO_PREFERENCES_INHERIT_VIEW_SETTINGS, inherit);
	drain ();
}

static char *
make_dir (const char *root, const char *relative)
{
	char *path = g_build_filename (root, relative, NULL);

	g_mkdir_with_parents (path, 0700);
	return path;
}

static NemoFile *
folder_for (const char *path)
{
	char *uri = g_filename_to_uri (path, NULL, NULL);
	NemoFile *file = nemo_file_get_by_uri (uri);

	g_free (uri);
	return file;
}

static gboolean
source_is (NemoFile *folder, NemoFile *expected)
{
	char *source, *want;
	gboolean same;

	source = nemo_folder_settings_source_uri (folder);
	want = expected != NULL ? nemo_file_get_uri (expected) : NULL;
	same = g_strcmp0 (source, want) == 0;
	if (!same) {
		g_printerr ("  source %s, wanted %s\n", source ? source : "(none)", want ? want : "(none)");
	}
	g_free (source);
	g_free (want);

	return same;
}

static gboolean
view_is (NemoFile *folder, const char *expected)
{
	char *view;
	gboolean same;

	view = nemo_folder_settings_get (folder, NEMO_METADATA_KEY_DEFAULT_VIEW, "default");
	same = g_strcmp0 (view, expected) == 0;
	if (!same) {
		g_printerr ("  view %s, wanted %s\n", view, expected);
	}
	g_free (view);

	return same;
}

int
main (int argc, char *argv[])
{
	char *dir, *scratch_config, *top_path, *mid_path, *leaf_path, *other_path;
	NemoFile *top, *mid, *leaf, *other;

	dir = test_scratch_dir ("nemo-foldersettings-XXXXXX", NULL);
	g_assert (dir != NULL);

	scratch_config = g_build_filename (dir, "config", NULL);
	g_mkdir_with_parents (scratch_config, 0700);
	test_scratch_point_config_at (scratch_config);

	gtk_init_check (&argc, &argv);
	nemo_global_preferences_init ();

	top_path = make_dir (dir, "top");
	mid_path = make_dir (dir, "top/mid");
	leaf_path = make_dir (dir, "top/mid/leaf");
	other_path = make_dir (dir, "other");
	top = folder_for (top_path);
	mid = folder_for (mid_path);
	leaf = folder_for (leaf_path);
	other = folder_for (other_path);

	/* Off is the default, and while off nothing is read or written. */
	check (!nemo_global_preferences_get_remember_folder_settings ());
	check (nemo_global_preferences_get_inherit_view_settings ());
	set_prefs (TRUE, TRUE);
	nemo_folder_settings_set (top, NEMO_METADATA_KEY_DEFAULT_VIEW, "default", "list");
	set_prefs (FALSE, TRUE);
	check (source_is (top, NULL));
	check (view_is (top, "default"));
	nemo_folder_settings_set (other, NEMO_METADATA_KEY_DEFAULT_VIEW, "default", "compact");
	set_prefs (TRUE, TRUE);
	check (!nemo_folder_settings_has_own (other));
	check (view_is (other, "default"));

	/* Writing back what a folder already uses is not a change. Views do that
	 * as a folder loads, and it used to give every folder opened a set. */
	{
		GList *columns = NULL;

		nemo_folder_settings_set_int (other, NEMO_METADATA_KEY_LIST_VIEW_ZOOM_LEVEL, 2, 2);
		nemo_folder_settings_set (other, NEMO_METADATA_KEY_LIST_VIEW_SORT_COLUMN, "name", NULL);
		nemo_folder_settings_set_list (other, NEMO_METADATA_KEY_LIST_VIEW_COLUMN_ORDER, NULL);
		check (!nemo_folder_settings_has_own (other));

		nemo_folder_settings_set (leaf, NEMO_METADATA_KEY_DEFAULT_VIEW, "default", "list");
		check (!nemo_folder_settings_has_own (leaf));

		columns = g_list_append (columns, (char *) "name");
		nemo_folder_settings_set_list (other, NEMO_METADATA_KEY_LIST_VIEW_COLUMN_ORDER, columns);
		check (nemo_folder_settings_has_own (other));
		g_list_free (columns);
		nemo_folder_settings_forget (other);
	}

	/* A folder with nothing saved takes the nearest parent's set. */
	check (nemo_folder_settings_has_own (top));
	check (source_is (top, top));
	check (source_is (leaf, top));
	check (view_is (leaf, "list"));

	/* ...but only while inheriting is on. */
	set_prefs (TRUE, FALSE);
	check (source_is (leaf, NULL));
	check (view_is (leaf, "default"));
	set_prefs (TRUE, TRUE);

	/* One change in an inheriting folder keeps the rest of what it used. */
	nemo_folder_settings_set_int (mid, NEMO_METADATA_KEY_LIST_VIEW_ZOOM_LEVEL, 2, 4);
	check (nemo_folder_settings_has_own (mid));
	check (view_is (mid, "list"));
	check (nemo_folder_settings_get_int (mid, NEMO_METADATA_KEY_LIST_VIEW_ZOOM_LEVEL, 2) == 4);
	check (source_is (leaf, mid));
	check (nemo_folder_settings_get_int (top, NEMO_METADATA_KEY_LIST_VIEW_ZOOM_LEVEL, 2) == 2);

	/* Picking the default in a child of a non-default parent still sticks. */
	nemo_folder_settings_set (leaf, NEMO_METADATA_KEY_DEFAULT_VIEW, "default", "default");
	check (nemo_folder_settings_has_own (leaf));
	check (view_is (leaf, "default"));

	/* Lists copy as lists. */
	{
		GList *columns = NULL, *read;

		columns = g_list_append (columns, (char *) "name");
		columns = g_list_append (columns, (char *) "size");
		nemo_folder_settings_set_list (top, NEMO_METADATA_KEY_LIST_VIEW_VISIBLE_COLUMNS, columns);
		g_list_free (columns);

		nemo_folder_settings_forget (mid);
		nemo_folder_settings_set_boolean (mid, NEMO_METADATA_KEY_LIST_VIEW_SORT_REVERSED, FALSE, TRUE);
		read = nemo_folder_settings_get_list (mid, NEMO_METADATA_KEY_LIST_VIEW_VISIBLE_COLUMNS);
		check (g_list_length (read) == 2 &&
		       g_strcmp0 (read->data, "name") == 0 &&
		       g_strcmp0 (read->next->data, "size") == 0);
		g_list_free_full (read, g_free);
		check (nemo_folder_settings_get_boolean (mid, NEMO_METADATA_KEY_LIST_VIEW_SORT_REVERSED, FALSE));
	}

	/* Same, where the view is the only value the parent saved, so nothing but
	 * the marker is left to say the child has a set. */
	{
		char *solo_path = make_dir (dir, "solo");
		char *child_path = make_dir (dir, "solo/child");
		NemoFile *solo = folder_for (solo_path);
		NemoFile *child = folder_for (child_path);

		nemo_folder_settings_set (solo, NEMO_METADATA_KEY_DEFAULT_VIEW, "default", "list");
		nemo_folder_settings_set (child, NEMO_METADATA_KEY_DEFAULT_VIEW, "default", "default");
		check (nemo_folder_settings_has_own (child));
		check (view_is (child, "default"));

		nemo_file_unref (solo);
		nemo_file_unref (child);
		g_free (solo_path);
		g_free (child_path);
	}

	/* Forget drops the folder's own set, and it inherits again. */
	nemo_folder_settings_forget (leaf);
	check (!nemo_folder_settings_has_own (leaf));
	check (source_is (leaf, mid));

	/* Dragged column widths belong to the set: a folder that has saved
	 * nothing else still counts as having one, a child inherits them, and
	 * resetting to the defaults drops them. */
	{
		char *owner_path = make_dir (dir, "widths");
		char *child_path = make_dir (dir, "widths/child");
		NemoFile *owner = folder_for (owner_path);
		NemoFile *child = folder_for (child_path);
		GList *widths = NULL, *read;
		char **left;
		char *uri;

		widths = g_list_append (widths, (char *) "type:146");
		nemo_folder_settings_set_list (owner, NEMO_METADATA_KEY_LIST_VIEW_COLUMN_WIDTHS, widths);
		g_list_free (widths);

		check (nemo_folder_settings_has_own (owner));
		read = nemo_folder_settings_get_list (child, NEMO_METADATA_KEY_LIST_VIEW_COLUMN_WIDTHS);
		check (g_list_length (read) == 1 && g_strcmp0 (read->data, "type:146") == 0);
		g_list_free_full (read, g_free);

		nemo_folder_settings_forget (owner);
		uri = nemo_file_get_uri (owner);
		left = nemo_metadata_store_get_stringv (uri, NEMO_METADATA_KEY_LIST_VIEW_COLUMN_WIDTHS);
		check (left == NULL || left[0] == NULL);
		g_strfreev (left);

		g_free (uri);
		nemo_file_unref (owner);
		nemo_file_unref (child);
		g_free (owner_path);
		g_free (child_path);
	}

	/* The icon view keeps two sizes per folder, one for when the folder is
	 * mostly pictures and one for when it is not, and both inherit. A child
	 * that is full of pictures takes the parent's picture size while its
	 * plain sibling takes the parent's other one. */
	{
		char *gallery_path = make_dir (dir, "top/mid/gallery");
		NemoFile *gallery = folder_for (gallery_path);

		nemo_folder_settings_set_int (mid, NEMO_METADATA_KEY_ICON_VIEW_ZOOM_LEVEL, 64, 96);
		nemo_folder_settings_set_int (mid, NEMO_METADATA_KEY_ICON_VIEW_IMAGE_ZOOM_LEVEL, 320, 448);

		check (source_is (gallery, mid));
		check (nemo_folder_settings_get_int (gallery, NEMO_METADATA_KEY_ICON_VIEW_ZOOM_LEVEL, 64) == 96);
		check (nemo_folder_settings_get_int (gallery, NEMO_METADATA_KEY_ICON_VIEW_IMAGE_ZOOM_LEVEL, 320) == 448);

		/* Changing one leaves the other alone, rather than there being one
		 * size that both kinds of folder have to share. */
		nemo_folder_settings_set_int (gallery, NEMO_METADATA_KEY_ICON_VIEW_IMAGE_ZOOM_LEVEL, 320, 640);
		check (nemo_folder_settings_get_int (gallery, NEMO_METADATA_KEY_ICON_VIEW_IMAGE_ZOOM_LEVEL, 320) == 640);
		check (nemo_folder_settings_get_int (gallery, NEMO_METADATA_KEY_ICON_VIEW_ZOOM_LEVEL, 64) == 96);
		check (nemo_folder_settings_get_int (mid, NEMO_METADATA_KEY_ICON_VIEW_IMAGE_ZOOM_LEVEL, 320) == 448);

		/* A parent that was only ever zoomed on pictures hands its children
		 * nothing for the other kind, so those fall back to the default. */
		nemo_folder_settings_forget (mid);
		nemo_folder_settings_forget (gallery);
		nemo_folder_settings_set_int (mid, NEMO_METADATA_KEY_ICON_VIEW_IMAGE_ZOOM_LEVEL, 320, 448);
		check (nemo_folder_settings_get_int (leaf, NEMO_METADATA_KEY_ICON_VIEW_IMAGE_ZOOM_LEVEL, 320) == 448);
		check (nemo_folder_settings_get_int (leaf, NEMO_METADATA_KEY_ICON_VIEW_ZOOM_LEVEL, 64) == 64);

		nemo_folder_settings_forget (mid);
		nemo_file_unref (gallery);
		g_free (gallery_path);
	}

	/* One sort for every view, kept as a list column. An icon view sort saved
	 * before that still reads, and the first change moves it over, so it
	 * can't come back from under a new one that is the default. */
	{
		char *sorted_path = make_dir (dir, "sorted");
		char *child_path = make_dir (dir, "sorted/child");
		NemoFile *sorted = folder_for (sorted_path);
		NemoFile *child = folder_for (child_path);
		char *column, *uri;

		check (g_strcmp0 (nemo_folder_settings_icon_sort_for_column ("date_modified"), "modification date") == 0);
		check (g_strcmp0 (nemo_folder_settings_column_for_icon_sort ("trashed"), "trashed_on") == 0);
		check (nemo_folder_settings_icon_sort_for_column ("owner") == NULL);
		check (g_strcmp0 (nemo_folder_settings_default_sort_column (sorted, NULL), "name") == 0);

		uri = nemo_file_get_uri (sorted);
		nemo_metadata_store_set_string (uri, NEMO_METADATA_KEY_ICON_VIEW_SORT_BY, "size");
		nemo_metadata_store_set_string (uri, NEMO_METADATA_KEY_ICON_VIEW_SORT_REVERSED, "true");
		column = nemo_folder_settings_get_sort_column (child);
		check (g_strcmp0 (column, "size") == 0);
		g_free (column);
		check (nemo_folder_settings_get_sort_reversed (child, FALSE));

		/* Loading writes back what is in use, which saves nothing. */
		nemo_folder_settings_set_sort_column (child, "size");
		nemo_folder_settings_set_sort_reversed (child, TRUE);
		check (!nemo_folder_settings_has_own (child));

		nemo_folder_settings_set_sort_column (sorted, "name");
		column = nemo_folder_settings_get_sort_column (child);
		check (column == NULL);
		g_free (column);
		check (nemo_folder_settings_get_sort_reversed (child, FALSE));
		column = nemo_metadata_store_get_string (uri, NEMO_METADATA_KEY_ICON_VIEW_SORT_BY);
		check (column == NULL);
		g_free (column);

		nemo_folder_settings_set_sort_column (child, "date_modified");
		check (nemo_folder_settings_has_own (child));
		column = nemo_folder_settings_get_sort_column (sorted);
		check (column == NULL);
		g_free (column);

		nemo_folder_settings_forget_sort (child);
		check (nemo_folder_settings_get_sort_column (child) == NULL);
		check (!nemo_folder_settings_get_sort_reversed (child, FALSE));

		g_free (uri);
		nemo_file_unref (sorted);
		nemo_file_unref (child);
		g_free (sorted_path);
		g_free (child_path);
	}

	/* A set saved before the marker existed still counts. */
	{
		char *uri = nemo_file_get_uri (other);

		nemo_metadata_store_set_string (uri, NEMO_METADATA_KEY_ICON_VIEW_ZOOM_LEVEL, "3");
		check (nemo_folder_settings_has_own (other));
		g_free (uri);
	}

	nemo_file_unref (top);
	nemo_file_unref (mid);
	nemo_file_unref (leaf);
	nemo_file_unref (other);
	g_free (top_path);
	g_free (mid_path);
	g_free (leaf_path);
	g_free (other_path);
	g_free (scratch_config);
	g_free (dir);

	if (failures > 0) {
		g_printerr ("%d failure(s)\n", failures);
		return 1;
	}

	return 0;
}
