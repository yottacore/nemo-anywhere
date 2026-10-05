/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */

/* test-nemo-share-visits.c - nothing goes to a share with nobody asking.

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

/* A scratch folder stands in for a network mount. Each case is a place that
 * used to ask about a file there with no one going to it: the per-file
 * questions behind a link onto it, a bookmark's existence check, an action's
 * folder test on the selection, and a custom icon read for a draw. The share
 * paths named here do not exist, so a check that does go and look answers
 * differently from one that does not. The prune's case is in
 * test-nemo-cache-prune.c. */

#include <config.h>

#include <stdlib.h>
#include <gio/gio.h>
#include <gtk/gtk.h>

#include <libnemo-private/nemo-action.h>
#include <libnemo-private/nemo-bookmark.h>
#include <libnemo-private/nemo-file.h>
#include <libnemo-private/nemo-file-private.h>
#include <libnemo-private/nemo-global-preferences.h>
#include <libnemo-private/nemo-icon-names.h>
#include <libnemo-private/nemo-metadata.h>
#include <libnemo-private/nemo-share.h>

#include "test-scratch.h"
#include "test-check.h"

static char *scratch;
static char *local;
static char *nas;

static void
use_share (const char *root)
{
	const char *roots[] = { root, NULL };

	nemo_share_set_roots_for_test (root != NULL ? roots : NULL);
}

/* The info a listing hands over, with no trip to where a link points. */
static NemoFile *
listed (const char *dir, const char *name, GFileType type, const char *target)
{
	g_autofree char *path = g_build_filename (dir, name, NULL);
	g_autoptr (GFile) location = g_file_new_for_path (path);
	g_autoptr (GFileInfo) info = g_file_info_new ();
	g_autoptr (GIcon) icon = g_themed_icon_new (type == G_FILE_TYPE_DIRECTORY ? "folder" : "text-x-generic");
	NemoFile *file = nemo_file_get (location);

	g_file_info_set_name (info, name);
	g_file_info_set_display_name (info, name);
	g_file_info_set_icon (info, icon);
	g_file_info_set_file_type (info, type);
	g_file_info_set_content_type (info, type == G_FILE_TYPE_DIRECTORY ? "inode/directory" : "text/plain");
	if (target != NULL) {
		g_file_info_set_is_symlink (info, TRUE);
		g_file_info_set_symlink_target (info, target);
	}
	nemo_file_update_info (file, info);

	return file;
}

static gboolean
link_on_share (const char *dir, const char *name, const char *target)
{
	NemoFile *file = listed (dir, name, G_FILE_TYPE_DIRECTORY, target);
	gboolean answer = nemo_file_is_on_a_share (file);

	nemo_file_unref (file);
	return answer;
}

static void
check_paths (void)
{
	g_autofree char *deep = g_build_filename (nas, "a", "b", NULL);
	g_autofree char *twin = g_strconcat (nas, "2", NULL);
	g_autofree char *twin_file = g_build_filename (twin, "a", NULL);

	check (nemo_path_is_on_a_share (nas));
	check (nemo_path_is_on_a_share (deep));
	check (!nemo_path_is_on_a_share (twin_file));
	check (!nemo_path_is_on_a_share (local));
	check (!nemo_path_is_on_a_share (NULL));

	/* The share holding home is local: the whole scratch folder is home. */
	use_share (scratch);
	check (!nemo_path_is_on_a_share (deep));
	use_share (nas);
}

static void
check_links (void)
{
	g_autofree char *far = g_build_filename (nas, "pics", NULL);
	g_autofree char *near = g_build_filename (local, "other", NULL);
	g_autofree char *beside = g_build_filename (nas, "beside", NULL);
	NemoFile *file;

	check (link_on_share (local, "abs", far));
	check (link_on_share (local, "rel", "../nas/pics"));
	check (!link_on_share (local, "home", near));
	check (!link_on_share (local, "climbs-out", "../nas/../local/x"));

	/* Already on that share, by going there: not another visit. */
	check (!link_on_share (nas, "within", beside));
	check (!link_on_share (nas, "back-home", near));

	/* A folder on a mount is somewhere the user went. */
	file = listed (nas, "plain", G_FILE_TYPE_DIRECTORY, NULL);
	check (!nemo_file_is_on_a_share (file));
	nemo_file_unref (file);

	/* An answer kept on the file is asked again once the shares change. */
	file = listed (local, "kept", G_FILE_TYPE_DIRECTORY, far);
	check (nemo_file_is_on_a_share (file));
	use_share (NULL);
	check (!nemo_file_is_on_a_share (file));
	use_share (nas);
	check (nemo_file_is_on_a_share (file));
	nemo_file_unref (file);
}

/* Counting a folder means listing it, so a link onto a share gets no count. */
static void
check_counts (void)
{
	g_autofree char *far = g_build_filename (nas, "pics", NULL);
	g_autofree char *near = g_build_filename (local, "other", NULL);
	NemoFile *to_share = listed (local, "count-far", G_FILE_TYPE_DIRECTORY, far);
	NemoFile *to_local = listed (local, "count-near", G_FILE_TYPE_DIRECTORY, near);

	check (!nemo_file_should_show_directory_item_count (to_share));
	check (nemo_file_should_show_directory_item_count (to_local));

	nemo_file_unref (to_share);
	nemo_file_unref (to_local);
}

static void
check_bookmarks (void)
{
	g_autofree char *far = g_build_filename (nas, "gone", NULL);
	g_autofree char *near = g_build_filename (local, "gone", NULL);
	g_autoptr (GFile) far_location = g_file_new_for_path (far);
	g_autoptr (GFile) near_location = g_file_new_for_path (near);
	NemoBookmark *on_share = nemo_bookmark_new (far_location, "far", NULL, NULL);
	NemoBookmark *at_home = nemo_bookmark_new (near_location, "near", NULL, NULL);
	g_autofree char *far_icon = NULL;
	g_autofree char *near_icon = NULL;

	/* Not asked, so taken as there, with the plain folder icon. */
	check (nemo_bookmark_uri_get_exists (on_share));
	far_icon = nemo_bookmark_get_icon_name (on_share);
	check (g_strcmp0 (far_icon, NEMO_ICON_SYMBOLIC_MISSING_BOOKMARK) != 0);

	/* Local, still looked at. */
	check (!nemo_bookmark_uri_get_exists (at_home));
	near_icon = nemo_bookmark_get_icon_name (at_home);
	check (g_strcmp0 (near_icon, NEMO_ICON_SYMBOLIC_MISSING_BOOKMARK) == 0);

	g_object_unref (on_share);
	g_object_unref (at_home);
}

static NemoAction *
folder_action (void)
{
	g_autofree char *path = g_build_filename (scratch, "dirs.nemo_action", NULL);
	const char *text = "[Nemo Action]\n"
			   "Name=Folders only\n"
			   "Exec=true %F\n"
			   "Selection=any\n"
			   "Extensions=dir;\n";

	check (g_file_set_contents (path, text, -1, NULL));
	return nemo_action_new ("dirs", path);
}

static gboolean
shown_for (NemoAction *action, NemoFile *file, NemoFile *parent)
{
	GList selection = { file, NULL, NULL };

	nemo_action_update_display_state (action, &selection, parent, FALSE, NULL);
	return gtk_action_get_visible (GTK_ACTION (action));
}

/* Neither link is on disk. One onto a share goes by the type the listing
   gave; a local one is still looked at, and is not a folder. */
static void
check_action (void)
{
	g_autofree char *far = g_build_filename (nas, "pics", NULL);
	g_autofree char *near = g_build_filename (local, "other", NULL);
	g_autofree char *uri = g_filename_to_uri (local, NULL, NULL);
	NemoAction *action = folder_action ();
	NemoFile *parent = nemo_file_get_by_uri (uri);
	NemoFile *to_share = listed (local, "act-far", G_FILE_TYPE_DIRECTORY, far);
	NemoFile *to_local = listed (local, "act-near", G_FILE_TYPE_DIRECTORY, near);

	check (action != NULL);
	if (action != NULL) {
		check (shown_for (action, to_share, parent));
		check (!shown_for (action, to_local, parent));
	}

	g_clear_object (&action);
	nemo_file_unref (to_share);
	nemo_file_unref (to_local);
	nemo_file_unref (parent);
}

static gboolean
icon_is_file (GIcon *icon, const char *path)
{
	g_autoptr (GFile) wanted = g_file_new_for_path (path);

	return icon != NULL && G_IS_FILE_ICON (icon) &&
	       g_file_equal (g_file_icon_get_file (G_FILE_ICON (icon)), wanted);
}

static void
check_custom_icon (void)
{
	g_autofree char *far = g_build_filename (nas, "icon.png", NULL);
	g_autofree char *near = g_build_filename (local, "icon.png", NULL);
	g_autofree char *far_uri = g_filename_to_uri (far, NULL, NULL);
	g_autofree char *near_uri = g_filename_to_uri (near, NULL, NULL);
	NemoFile *file = listed (local, "dressed.txt", G_FILE_TYPE_REGULAR, NULL);
	GIcon *icon;

	nemo_file_set_metadata_internal (file, NEMO_METADATA_KEY_CUSTOM_ICON, far_uri, NULL);
	icon = nemo_file_get_gicon (file, 0);
	check (icon != NULL && !icon_is_file (icon, far));
	g_clear_object (&icon);

	nemo_file_set_metadata_internal (file, NEMO_METADATA_KEY_CUSTOM_ICON, near_uri, NULL);
	icon = nemo_file_get_gicon (file, 0);
	check (icon_is_file (icon, near));
	g_clear_object (&icon);

	nemo_file_unref (file);
}

int
main (int argc, char *argv[])
{
	scratch = test_scratch_config_home ("nemo-sharevisits-XXXXXX");
	if (scratch == NULL) {
		g_printerr ("could not make a scratch dir\n");
		return 77;
	}

	if (!gtk_init_check (&argc, &argv)) {
		g_print ("SKIP: no display\n");
		return 77;
	}
	nemo_global_preferences_init ();

	local = g_build_filename (scratch, "local", NULL);
	nas = g_build_filename (scratch, "nas", NULL);
	check (g_mkdir_with_parents (local, 0700) == 0);
	check (g_mkdir_with_parents (nas, 0700) == 0);
	use_share (nas);

	check_paths ();
	check_links ();
	check_counts ();
	check_bookmarks ();
	check_action ();
	check_custom_icon ();

	use_share (NULL);
	g_free (nas);
	g_free (local);
	g_free (scratch);

	if (failures > 0) {
		g_printerr ("%d check(s) failed\n", failures);
		return EXIT_FAILURE;
	}

	g_print ("OK\n");
	return EXIT_SUCCESS;
}
