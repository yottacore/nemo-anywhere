/* A folder that is mostly images opens at its own default size. What counts as
 * mostly images is two rules at once - enough pictures to be a gallery, and
 * a big enough share of the files - and folders are left out of the count. Both
 * limits come from the settings. */

#include <config.h>

#include <stdlib.h>
#include <glib/gstdio.h>
#include <gtk/gtk.h>
#ifndef G_OS_WIN32
#include <unistd.h>
#endif

#include <libnemo-private/nemo-config.h>
#include <libnemo-private/nemo-directory.h>
#include <libnemo-private/nemo-file.h>
#include <libnemo-private/nemo-file-utilities.h>
#include <libnemo-private/nemo-global-preferences.h>
#include <libnemo-private/nemo-image-folders.h>
#include <libnemo-private/nemo-query.h>
#include <libnemo-private/nemo-search-directory.h>

#include "test-scratch.h"
#include "test-check.h"

/* The directory only keeps its file list while something is watching it, which
 * in the program is always the view. A plain call_when_ready hands the files to
 * the callback and lets go of them again. */
static gboolean
wait_for_directory (NemoDirectory *directory)
{
	int spins;

	for (spins = 0; spins < 5000; spins++) {
		if (nemo_directory_are_all_files_seen (directory)) {
			return TRUE;
		}
		g_main_context_iteration (NULL, FALSE);
		g_usleep (2000);
	}

	return FALSE;
}

static void
write_image (const char *dir, int n)
{
	GdkPixbuf *pixbuf;
	char *path;

	path = g_strdup_printf ("%s/shot-%d.png", dir, n);
	pixbuf = gdk_pixbuf_new (GDK_COLORSPACE_RGB, FALSE, 8, 4, 4);
	gdk_pixbuf_fill (pixbuf, 0x336699ff);
	check (gdk_pixbuf_save (pixbuf, path, "png", NULL, NULL));

	g_object_unref (pixbuf);
	g_free (path);
}

static void
write_text (const char *dir, int n)
{
	char *path = g_strdup_printf ("%s/notes-%d.txt", dir, n);

	check (g_file_set_contents (path, "x\n", 2, NULL));
	g_free (path);
}

static void
write_folder (const char *dir, int n)
{
	char *path = g_strdup_printf ("%s/sub-%d", dir, n);

	check (g_mkdir (path, 0755) == 0);
	g_free (path);
}

/* Builds a folder with the given contents and asks. */
static gboolean
mostly_images (const char *base, const char *name,
	       int images, int texts, int folders)
{
	NemoDirectory *directory;
	char *dir, *uri;
	gboolean answer;
	int client;
	int i;

	dir = g_build_filename (base, name, NULL);
	check (g_mkdir (dir, 0755) == 0);

	for (i = 0; i < images; i++) {
		write_image (dir, i);
	}
	for (i = 0; i < texts; i++) {
		write_text (dir, i);
	}
	for (i = 0; i < folders; i++) {
		write_folder (dir, i);
	}

	uri = g_filename_to_uri (dir, NULL, NULL);
	directory = nemo_directory_get_by_uri (uri);

	nemo_directory_file_monitor_add (directory, &client, TRUE,
					 NEMO_FILE_ATTRIBUTE_INFO, NULL, NULL);
	check (wait_for_directory (directory));

	answer = nemo_directory_is_mostly_images (directory);

	nemo_directory_file_monitor_remove (directory, &client);
	nemo_directory_unref (directory);
	g_free (uri);
	g_free (dir);

	return answer;
}

/* What is remembered about base/name, as 0 for no, 1 for yes and -1 for
   nothing known. */
static int
remembered (const char *base, const char *name)
{
	char *path = g_build_filename (base, name, NULL);
	GFile *location = g_file_new_for_path (path);
	gboolean mostly = FALSE;
	int answer;

	answer = nemo_image_folders_known (location, &mostly) ? (mostly ? 1 : 0) : -1;

	g_object_unref (location);
	g_free (path);
	return answer;
}

static void
set_true (gpointer flag)
{
	*(gboolean *) flag = TRUE;
}

/* Loads base/name the way a view would and counts what is inside it. */
static void
look_ahead_at (const char *base, const char *name)
{
	NemoDirectory *directory;
	char *dir, *uri;
	gboolean done = FALSE;
	int client, spins;

	dir = g_build_filename (base, name, NULL);
	uri = g_filename_to_uri (dir, NULL, NULL);
	directory = nemo_directory_get_by_uri (uri);

	nemo_directory_file_monitor_add (directory, &client, TRUE,
					 NEMO_FILE_ATTRIBUTE_INFO, NULL, NULL);
	check (wait_for_directory (directory));

	nemo_image_folders_look_ahead_full (directory, set_true, &done);
	for (spins = 0; spins < 5000 && !done; spins++) {
		g_main_context_iteration (NULL, FALSE);
		g_usleep (2000);
	}
	check (done);

	nemo_directory_file_monitor_remove (directory, &client);
	nemo_directory_unref (directory);
	g_free (uri);
	g_free (dir);
}

/* Whether a folder opens in icon view, asked of base/name while it is loaded,
   with saved_view as the folder's own saved view. */
static gboolean
wants_icon_view (const char *base, const char *name, const char *saved_view)
{
	NemoDirectory *directory;
	NemoFile *folder;
	char *dir, *uri;
	gboolean answer;
	int client;

	dir = g_build_filename (base, name, NULL);
	uri = g_filename_to_uri (dir, NULL, NULL);
	directory = nemo_directory_get_by_uri (uri);
	folder = nemo_file_get_by_uri (uri);

	nemo_directory_file_monitor_add (directory, &client, TRUE,
					 NEMO_FILE_ATTRIBUTE_INFO, NULL, NULL);
	check (wait_for_directory (directory));

	answer = nemo_image_folders_wants_icon_view (folder, saved_view);

	nemo_directory_file_monitor_remove (directory, &client);
	nemo_file_unref (folder);
	nemo_directory_unref (directory);
	g_free (uri);
	g_free (dir);

	return answer;
}

/* The same, for a folder nobody has opened. */
static gboolean
wants_icon_view_unopened (const char *base, const char *name)
{
	char *path = g_build_filename (base, name, NULL);
	GFile *location = g_file_new_for_path (path);
	NemoFile *folder = nemo_file_get (location);
	gboolean answer;

	answer = nemo_image_folders_wants_icon_view (folder, NULL);

	nemo_file_unref (folder);
	g_object_unref (location);
	g_free (path);
	return answer;
}

/* The switch to icon view: on for a folder of pictures, off with the setting
   off, never over a view the folder saved for itself, never for a search, and
   before a folder is read only on what is remembered about it. */
static void
test_wants_icon_view (const char *base)
{
	char *pics = g_build_filename (base, "switch-pics", NULL);
	char *docs = g_build_filename (base, "switch-docs", NULL);
	char *noted, *search_uri, *pics_uri;
	NemoDirectory *search;
	NemoQuery *query;
	gboolean searched = FALSE;
	GList *hits, *l;
	int client;
	NemoFile *search_file;
	GFile *location;
	int i;

	check (g_mkdir (pics, 0755) == 0);
	check (g_mkdir (docs, 0755) == 0);
	for (i = 0; i < 3; i++) {
		write_image (pics, i);
		write_text (docs, i);
	}

	check (wants_icon_view (base, "switch-pics", NULL));
	check (!wants_icon_view (base, "switch-docs", NULL));
	check (!wants_icon_view (base, "switch-pics", NEMO_LIST_VIEW_IID));

	nemo_config_set_boolean (nemo_icon_view_preferences,
				 NEMO_PREFERENCES_ICON_VIEW_IMAGE_FOLDER_SWITCH, FALSE);
	check (!wants_icon_view (base, "switch-pics", NULL));
	nemo_config_set_boolean (nemo_icon_view_preferences,
				 NEMO_PREFERENCES_ICON_VIEW_IMAGE_FOLDER_SWITCH, TRUE);

	noted = g_build_filename (base, "switch-noted", NULL);
	location = g_file_new_for_path (noted);
	nemo_image_folders_note (location, TRUE);
	check (wants_icon_view_unopened (base, "switch-noted"));
	check (!wants_icon_view_unopened (base, "switch-never-seen"));
	g_object_unref (location);

	/* A search stays as it is, even when every hit is a picture. */
	pics_uri = g_filename_to_uri (pics, NULL, NULL);
	query = nemo_query_new ();
	nemo_query_set_location (query, pics_uri);
	nemo_query_set_file_pattern (query, "shot");
	search_uri = nemo_search_directory_generate_new_uri ();
	search = nemo_directory_get_by_uri (search_uri);
	check (NEMO_IS_SEARCH_DIRECTORY (search));
	nemo_search_directory_set_query (NEMO_SEARCH_DIRECTORY (search), query);
	g_signal_connect_swapped (search, "done-loading", G_CALLBACK (set_true), &searched);
	nemo_directory_file_monitor_add (search, &client, TRUE,
					 NEMO_FILE_ATTRIBUTE_INFO, NULL, NULL);
	for (i = 0; i < 5000 && !searched; i++) {
		g_main_context_iteration (NULL, FALSE);
		g_usleep (2000);
	}
	check (searched);
	/* Hits come in with only what the search itself read. The type is
	   read the way the view reads it. */
	hits = nemo_directory_get_file_list (search);
	for (l = hits; l != NULL; l = l->next) {
		nemo_file_invalidate_attributes (l->data, NEMO_FILE_ATTRIBUTE_INFO);
		nemo_file_monitor_add (l->data, &client, NEMO_FILE_ATTRIBUTE_INFO);
		for (i = 0; i < 5000 && !nemo_file_check_if_ready (l->data, NEMO_FILE_ATTRIBUTE_INFO); i++) {
			g_main_context_iteration (NULL, FALSE);
			g_usleep (2000);
		}
	}
	check (nemo_directory_is_mostly_images (search));
	search_file = nemo_directory_get_corresponding_file (search);
	check (!nemo_image_folders_wants_icon_view (search_file, NULL));
	for (l = hits; l != NULL; l = l->next) {
		nemo_file_monitor_remove (l->data, &client);
	}
	nemo_file_list_free (hits);
	nemo_directory_file_monitor_remove (search, &client);
	nemo_file_unref (search_file);
	nemo_directory_unref (search);
	g_object_unref (query);
	g_free (search_uri);
	g_free (pics_uri);

	g_free (noted);
	g_free (docs);
	g_free (pics);
}

int
main (int argc, char **argv)
{
	char *tmp;

	gtk_init_check (&argc, &argv);

	tmp = test_scratch_config_home ("nemo-image-folder-XXXXXX");
	if (tmp == NULL) {
		g_printerr ("FAIL could not make a temp dir\n");
		return EXIT_FAILURE;
	}

	nemo_global_preferences_init ();

	/* On Windows a content type is an extension, which a plain
	   g_content_type_is_a never finds to be an image. */
	{
		g_autofree char *png = g_content_type_guess ("shot.png", NULL, 0, NULL);

		check (nemo_content_type_is_a (png, "image/*"));
		check (nemo_content_type_is_a (png, "image/png"));
		check (!nemo_content_type_is_a (png, "text/*"));
		check (!nemo_content_type_is_a (NULL, "image/*"));
	}

	check (!mostly_images (tmp, "empty", 0, 0, 0));

	/* One picture in a folder is not a gallery, whatever share it is of the
	   files there. */
	check (!mostly_images (tmp, "one-image", 1, 0, 0));
	check (!mostly_images (tmp, "one-of-two", 1, 1, 0));

	check (mostly_images (tmp, "two-images", 2, 0, 0));
	check (mostly_images (tmp, "half-images", 2, 2, 0));
	check (!mostly_images (tmp, "outnumbered", 2, 3, 0));
	check (mostly_images (tmp, "gallery", 6, 2, 0));

	/* Folders are not counted, so a set of pictures filed into sub-folders
	   still reads as a folder of pictures. */
	check (mostly_images (tmp, "with-folders", 2, 0, 5));
	check (!mostly_images (tmp, "folders-and-text", 2, 3, 5));

	/* A whole count is kept, so the folder can open in the right view next
	   time before anything has been read. */
	check (remembered (tmp, "gallery") == 1);
	check (remembered (tmp, "outnumbered") == 0);
	check (remembered (tmp, "never-seen") == -1);

	/* Counted ahead from the folder above, before either is opened. A link is
	   left alone, since it may lead anywhere. */
	{
		char *ahead = g_build_filename (tmp, "ahead", NULL);
		char *pics = g_build_filename (ahead, "pics", NULL);
		char *docs = g_build_filename (ahead, "docs", NULL);
		int i;

		check (g_mkdir (ahead, 0755) == 0);
		check (g_mkdir (pics, 0755) == 0);
		check (g_mkdir (docs, 0755) == 0);
		for (i = 0; i < 3; i++) {
			write_image (pics, i);
			write_text (docs, i);
		}
#ifndef G_OS_WIN32
		{
			char *link = g_build_filename (ahead, "link-pics", NULL);

			check (symlink (pics, link) == 0);
			g_free (link);
		}
#endif
		look_ahead_at (tmp, "ahead");

		check (remembered (ahead, "pics") == 1);
		check (remembered (ahead, "docs") == 0);
#ifndef G_OS_WIN32
		check (remembered (ahead, "link-pics") == -1);
#endif
		g_free (docs);
		g_free (pics);
		g_free (ahead);
	}

	test_wants_icon_view (tmp);

	nemo_config_set_int (nemo_icon_view_preferences,
			     NEMO_PREFERENCES_ICON_VIEW_IMAGE_FOLDER_MIN_IMAGES, 3);
	check (!mostly_images (tmp, "two-of-three", 2, 0, 0));
	check (mostly_images (tmp, "three-of-three", 3, 0, 0));

	nemo_config_set_int (nemo_icon_view_preferences,
			     NEMO_PREFERENCES_ICON_VIEW_IMAGE_FOLDER_MIN_PERCENT, 75);
	check (!mostly_images (tmp, "two-thirds", 4, 2, 0));
	check (mostly_images (tmp, "three-quarters", 3, 1, 0));

	/* Nothing below one picture, whatever the setting says. */
	nemo_config_set_int (nemo_icon_view_preferences,
			     NEMO_PREFERENCES_ICON_VIEW_IMAGE_FOLDER_MIN_IMAGES, 0);
	nemo_config_set_int (nemo_icon_view_preferences,
			     NEMO_PREFERENCES_ICON_VIEW_IMAGE_FOLDER_MIN_PERCENT, 0);
	check (!mostly_images (tmp, "text-only", 0, 3, 0));

	/* The oldest answers go once the list is full, the newest stay. */
	{
		int i;

		for (i = 0; i < 600; i++) {
			char *name = g_strdup_printf ("filler-%d", i);
			char *path = g_build_filename (tmp, name, NULL);
			GFile *location = g_file_new_for_path (path);

			nemo_image_folders_note (location, TRUE);
			g_object_unref (location);
			g_free (path);
			g_free (name);
		}
		check (remembered (tmp, "gallery") == -1);
		check (remembered (tmp, "filler-599") == 1);
	}

	g_free (tmp);

	if (failures == 0)
		g_print ("nemo-image-folder: all checks passed\n");

	return failures == 0 ? 0 : 1;
}
