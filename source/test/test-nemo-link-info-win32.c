/* Windows tells us nothing about the type of a link the listing didn't follow:
 * a junction to a folder and a symlink to a picture both come back with no
 * content type at all, and the toolkit then hands out its plain file icon. A
 * folder link was drawn as a document because of it. These checks pin that
 * toolkit behavior, which is why the swap has to exist, and check that a link
 * to a folder gets the folder icon.
 */

#include <config.h>

#include <stdlib.h>
#include <string.h>
#include <gio/gio.h>
#include <glib/gstdio.h>
#include <gtk/gtk.h>

#include <libnemo-private/nemo-file.h>
#include <libnemo-private/nemo-file-private.h>
#include <libnemo-private/nemo-file-utilities.h>
#include <libnemo-private/nemo-shortcut-win32.h>
#include <libnemo-private/nemo-link-win32.h>

#include "test-scratch.h"
#include "test-check.h"

#define WANTED "standard::*,unix::*,time::*,id::filesystem"

/* What the file list sees: the link itself, never its target. */
static GFileInfo *
query_link (const char *path)
{
	GFile *file = g_file_new_for_path (path);
	GFileInfo *info = g_file_query_info (file, WANTED,
					     G_FILE_QUERY_INFO_NOFOLLOW_SYMLINKS,
					     NULL, NULL);

	g_object_unref (file);
	return info;
}

static gboolean
icon_names_include (GIcon *icon, const char *name)
{
	const char * const *names;
	int i;

	if (!G_IS_THEMED_ICON (icon)) {
		return FALSE;
	}

	names = g_themed_icon_get_names (G_THEMED_ICON (icon));
	for (i = 0; names != NULL && names[i] != NULL; i++) {
		if (strcmp (names[i], name) == 0) {
			return TRUE;
		}
	}

	return FALSE;
}

/* A NemoFile with the same info the file list would have applied. */
static NemoFile *
loaded_file (const char *path)
{
	GFile *location = g_file_new_for_path (path);
	NemoFile *file = nemo_file_get (location);
	GFileInfo *info = query_link (path);

	g_object_unref (location);

	if (file != NULL && info != NULL) {
		nemo_file_update_info (file, info);
	}

	g_clear_object (&info);
	return file;
}

/* The premise: without a swap there is nothing to work from. If a future
 * toolkit starts returning a type here, the swap becomes dead weight rather than a
 * fix, and this is where that shows up. */
static void
test_toolkit_reports_nothing (const char *dir_link,
			      const char *file_link)
{
	const char *paths[] = { dir_link, file_link };
	gsize i;

	for (i = 0; i < G_N_ELEMENTS (paths); i++) {
		GFileInfo *info = query_link (paths[i]);

		check (info != NULL);
		if (info != NULL) {
			check (g_content_type_is_unknown (g_file_info_get_content_type (info)));
			check (icon_names_include (g_file_info_get_icon (info), "text-x-generic"));
			g_object_unref (info);
		}
	}
}

static void
test_link_to_folder_is_a_folder (const char *dir_link)
{
	GFileInfo *info = query_link (dir_link);
	NemoFile *file = loaded_file (dir_link);
	char *mime;

	check (info != NULL);
	if (info != NULL) {
		check (g_file_info_get_file_type (info) == G_FILE_TYPE_DIRECTORY);
		check (g_file_info_get_is_symlink (info));
		g_object_unref (info);
	}

	check (file != NULL);
	if (file != NULL) {
		mime = nemo_file_get_mime_type (file);
		check (g_strcmp0 (mime, "inode/directory") == 0);
		g_free (mime);

		check (icon_names_include (file->details->icon, "folder"));
		nemo_file_unref (file);
	}
}

/* A link to a picture reads as a picture, from its name - the extension is all
 * there is to go on once the link isn't followed. */
static void
test_link_to_file_keeps_its_type (const char *file_link)
{
	NemoFile *file = loaded_file (file_link);
	char *mime;

	check (file != NULL);
	if (file != NULL) {
		mime = nemo_file_get_mime_type (file);
		check (mime != NULL && !g_content_type_is_unknown (mime));
		check (icon_names_include (file->details->icon, "image-png"));
		g_free (mime);
		nemo_file_unref (file);
	}
}

int
main (int argc, char *argv[])
{
	char *root, *real_dir, *real_file, *dir_link, *file_link;
	gboolean made_links;

	gtk_init_check (&argc, &argv);

	root = test_scratch_dir ("nemo-link-info-test-XXXXXX", NULL);
	if (root == NULL) {
		g_printerr ("could not make a temporary directory\n");
		return EXIT_FAILURE;
	}

	real_dir = g_build_filename (root, "target", NULL);
	real_file = g_build_filename (root, "target.png", NULL);
	dir_link = g_build_filename (root, "dirlink", NULL);
	file_link = g_build_filename (root, "filelink.png", NULL);

	g_mkdir (real_dir, 0755);

	made_links = g_file_set_contents (real_file, "not really a picture", -1, NULL) &&
		     nemo_win32_link_create_default (real_dir, dir_link, NULL) &&
		     nemo_win32_link_create_default (real_file, file_link, NULL);

	if (!made_links) {
		/* a file symlink needs Developer Mode or an elevated run */
		g_print ("SKIP: this machine will not create a link\n");
	} else {
		test_toolkit_reports_nothing (dir_link, file_link);
		test_link_to_folder_is_a_folder (dir_link);
		test_link_to_file_keeps_its_type (file_link);
	}

	g_free (file_link);
	g_free (dir_link);
	g_free (real_file);
	g_free (real_dir);
	g_free (root);

	if (failures > 0) {
		g_printerr ("%d failure(s)\n", failures);
		return EXIT_FAILURE;
	}
	if (!made_links) {
		return 77;
	}

	g_print ("link type: all checks passed\n");
	return EXIT_SUCCESS;
}
