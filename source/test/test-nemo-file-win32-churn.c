/* A refresh that changes nothing must report nothing changed.
 *
 * On Windows gio hands every file the same flat icon, so nemo swaps in a themed
 * one derived from the type. The changed-check used to compare the incoming gio
 * icon against the themed one already stored, which never matches - so every
 * file in the folder reported as changed on every single refresh, and each one
 * dragged a re-sort, a redraw and a thumbnail re-check behind it.
 *
 * nemo_file_update_info is documented to "return FALSE if no change", so that
 * contract is what is checked here: feed the same freshly-queried info twice and
 * the second call must be quiet. Windows-only. */

#include <config.h>

#include <stdlib.h>
#include <string.h>
#include <gio/gio.h>
#include <glib/gstdio.h>
#include <gtk/gtk.h>

#include <libnemo-private/nemo-file.h>
#include <libnemo-private/nemo-file-private.h>

#include "test-scratch.h"
#include "test-check.h"

/* Everything nemo reads off a file during a normal folder refresh. Asking for
 * less would let a churning attribute hide behind an attribute never fetched. */
#define WANTED \
	"standard::*,access::*,mountable::*,time::*,unix::*,owner::*," \
	"selinux::*,thumbnail::*,id::filesystem,trash::orig-path,trash::deletion-date"

static GFileInfo *
fresh_info (const char *path)
{
	GFile *location = g_file_new_for_path (path);
	GFileInfo *info = g_file_query_info (location, WANTED,
					     G_FILE_QUERY_INFO_NOFOLLOW_SYMLINKS,
					     NULL, NULL);

	g_object_unref (location);
	return info;
}

/* Refresh @path @rounds times over and count how many said "changed". */
static int
churn (const char *path, int rounds)
{
	GFile *location = g_file_new_for_path (path);
	NemoFile *file = nemo_file_get (location);
	int changes = 0;
	int i;

	g_object_unref (location);

	if (file == NULL) {
		g_printerr ("  no NemoFile for %s\n", path);
		return -1;
	}

	for (i = 0; i < rounds; i++) {
		GFileInfo *info = fresh_info (path);

		if (info == NULL) {
			g_printerr ("  could not query %s\n", path);
			nemo_file_unref (file);
			return -1;
		}

		if (nemo_file_update_info (file, info)) {
			changes++;
		}
		g_object_unref (info);
	}

	nemo_file_unref (file);
	return changes;
}

static void
write_file (const char *path, const char *contents)
{
	GError *error = NULL;

	if (!g_file_set_contents (path, contents, -1, &error)) {
		g_printerr ("  could not write %s: %s\n", path, error->message);
		g_clear_error (&error);
	}
}

/* A NemoFile loaded once, the way a listing loads it. */
static NemoFile *
loaded (const char *path)
{
	GFile *location = g_file_new_for_path (path);
	NemoFile *file = nemo_file_get (location);
	GFileInfo *info = fresh_info (path);

	g_object_unref (location);
	if (info != NULL) {
		nemo_file_update_info (file, info);
		g_object_unref (info);
	}
	return file;
}

static const char *
first_icon_name (NemoFile *file)
{
	GIcon *icon = file->details->icon;

	if (!G_IS_THEMED_ICON (icon)) {
		return NULL;
	}
	return g_themed_icon_get_names (G_THEMED_ICON (icon))[0];
}

/* gio gives every file the same flat icon on Windows, so the per-type one is
 * ours to derive, and a folder's size of zero once sent its type off to be
 * guessed from the name. */
static void
check_types (const char *dir)
{
	const char *files[] = { "note.txt", "shot.png", "tune.mp3" };
	const char *folders[] = { "docs", ".config" };
	char *seen[G_N_ELEMENTS (files)];
	guint i, j;

	for (i = 0; i < G_N_ELEMENTS (files); i++) {
		char *path = g_build_filename (dir, files[i], NULL);
		NemoFile *file = loaded (path);

		seen[i] = g_strdup (first_icon_name (file));
		g_print ("  %s: icon %s\n", files[i], seen[i] ? seen[i] : "(none)");
		nemo_file_unref (file);
		g_free (path);
	}
	for (i = 0; i < G_N_ELEMENTS (files); i++) {
		check (seen[i] != NULL);
		for (j = i + 1; j < G_N_ELEMENTS (files); j++) {
			check (g_strcmp0 (seen[i], seen[j]) != 0);
		}
	}
	check (g_strcmp0 (seen[1], "image-png") == 0);
	for (i = 0; i < G_N_ELEMENTS (files); i++) {
		g_free (seen[i]);
	}

	for (i = 0; i < G_N_ELEMENTS (folders); i++) {
		char *path = g_build_filename (dir, folders[i], NULL);
		NemoFile *file;
		char *mime, *type;

		g_mkdir (path, 0700);
		file = loaded (path);
		mime = nemo_file_get_mime_type (file);
		type = nemo_file_get_type_as_string (file);
		g_print ("  %s: %s, \"%s\"\n", folders[i], mime, type ? type : "(null)");

		check (g_strcmp0 (mime, "inode/directory") == 0);
		check (nemo_file_is_directory (file));
		check (g_strcmp0 (type, "Folder") == 0);

		g_free (type);
		g_free (mime);
		nemo_file_unref (file);
		g_rmdir (path);
		g_free (path);
	}
}

int
main (int argc, char *argv[])
{
	char *dir;
	/* One per icon family, because the themed icon is derived from the type
	 * and a single type could pass by accident. */
	const char *names[] = { "note.txt", "shot.png", "tune.mp3", "sheet.csv", "blob.zzz" };
	char *paths[G_N_ELEMENTS (names)];
	guint i;

	/* Only so the icon theme nemo-file hooks on startup has a screen to hang
	 * off. Nothing here needs a window, and a box with no display still runs
	 * the checks - it just grumbles on the way in. */
	gtk_init_check (&argc, &argv);

	dir = test_scratch_dir ("nemo-churn-XXXXXX", NULL);
	g_assert (dir != NULL);

	for (i = 0; i < G_N_ELEMENTS (names); i++) {
		paths[i] = g_build_filename (dir, names[i], NULL);
		write_file (paths[i], "x");
	}

	/* Five refreshes. The first legitimately reports a change - it is the
	 * first time the file has been seen - and the rest must be silent. */
	for (i = 0; i < G_N_ELEMENTS (names); i++) {
		int changes = churn (paths[i], 5);

		if (changes != 1) {
			g_printerr ("  %s: %d of 5 refreshes reported a change\n",
				    names[i], changes);
		}
		check (changes == 1);
	}

	/* A directory too: it takes the other branch of the icon override, the
	 * one that leaves gio's icon alone. */
	{
		char *sub = g_build_filename (dir, "subdir", NULL);
		int changes;

		g_mkdir (sub, 0700);
		changes = churn (sub, 5);
		if (changes != 1) {
			g_printerr ("  subdir: %d of 5 refreshes reported a change\n", changes);
		}
		check (changes == 1);

		g_rmdir (sub);
		g_free (sub);
	}

	check_types (dir);

	/* A real change must still come through, or the check above could be
	 * satisfied by a file that reports nothing ever. */
	{
		char *path = g_build_filename (dir, "grows.txt", NULL);
		GFile *location;
		NemoFile *file;
		GFileInfo *info;

		write_file (path, "small");
		location = g_file_new_for_path (path);
		file = nemo_file_get (location);
		g_object_unref (location);

		info = fresh_info (path);
		check (info != NULL);
		if (info != NULL) {
			nemo_file_update_info (file, info);   /* first sighting */
			g_object_unref (info);
		}

		info = fresh_info (path);
		check (info != NULL && !nemo_file_update_info (file, info));
		g_clear_object (&info);

		write_file (path, "very much bigger than it was before");

		info = fresh_info (path);
		check (info != NULL && nemo_file_update_info (file, info));
		g_clear_object (&info);

		nemo_file_unref (file);
		g_unlink (path);
		g_free (path);
	}

	for (i = 0; i < G_N_ELEMENTS (names); i++) {
		g_unlink (paths[i]);
		g_free (paths[i]);
	}
	g_rmdir (dir);
	g_free (dir);

	if (failures == 0) {
		g_print ("file-win32-churn: all checks passed\n");
	}
	return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
