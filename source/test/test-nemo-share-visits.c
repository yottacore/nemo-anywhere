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
 * folder test on the selection, a custom icon read for a draw, and the folder
 * listing following a link onto it. The share paths named here mostly do not
 * exist, so a check that does go and look answers differently from one that
 * does not. A folder on the share counts as a share too, so counts and
 * thumbnails are off there by default. The prune's case is in
 * test-nemo-cache-prune.c. */

#include <config.h>

#include <stdlib.h>
#include <unistd.h>
#include <gio/gio.h>
#include <gtk/gtk.h>

#include <libnemo-private/nemo-action.h>
#include <libnemo-private/nemo-bookmark.h>
#include <libnemo-private/nemo-dir-enum.h>
#include <libnemo-private/nemo-directory.h>
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
listed_full (const char *dir, const char *name, GFileType type, const char *target,
	     gboolean mountpoint)
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
	g_file_info_set_attribute_boolean (info, G_FILE_ATTRIBUTE_UNIX_IS_MOUNTPOINT, mountpoint);
	nemo_file_update_info (file, info);

	return file;
}

static NemoFile *
listed (const char *dir, const char *name, GFileType type, const char *target)
{
	return listed_full (dir, name, type, target, FALSE);
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

	/* Already on that share, by going there: not another visit, so the
	   listing follows these two. */
	/* Commented out 20261005: a folder on a share now counts as a share
	   (decision 20261005 on 2026093010493450), so a file there is on it
	   whatever it points at. What these meant is now the listing's question,
	   checked just below and in check_listing.
	check (!link_on_share (nas, "within", beside));
	check (!link_on_share (nas, "back-home", near));
	*/
	check (!nemo_share_link_leaves_for_a_share (nas, beside));
	check (!nemo_share_link_leaves_for_a_share (nas, near));
	check (link_on_share (nas, "within", beside));

	/* Commented out 20261005, same decision: a folder on a mount was taken as
	   somewhere the user went, and is now a share like a UNC path.
	file = listed (nas, "plain", G_FILE_TYPE_DIRECTORY, NULL);
	check (!nemo_file_is_on_a_share (file));
	nemo_file_unref (file);
	*/

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

/* A folder on the share is a share, like a UNC path, so the speed settings
   leave counts and thumbnails off there by default. So is its mount point,
   seen from the folder above it, and a folder reached through a link onto it. */
static void
check_folders (void)
{
	g_autofree char *far = g_build_filename (nas, "pics", NULL);
	g_autofree char *via_path = g_build_filename (local, "via", NULL);
	NemoFile *inside = listed (nas, "sub", G_FILE_TYPE_DIRECTORY, NULL);
	NemoFile *picture = listed (nas, "pic.txt", G_FILE_TYPE_REGULAR, NULL);
	NemoFile *home_picture = listed (local, "pic.txt", G_FILE_TYPE_REGULAR, NULL);
	NemoFile *home_folder = listed (local, "sub", G_FILE_TYPE_DIRECTORY, NULL);
	NemoFile *mount = listed_full (scratch, "nas", G_FILE_TYPE_DIRECTORY, NULL, TRUE);
	NemoFile *local_mount = listed_full (scratch, "local", G_FILE_TYPE_DIRECTORY, NULL, TRUE);
	NemoFile *via = listed (local, "via", G_FILE_TYPE_DIRECTORY, far);
	NemoFile *through = listed (via_path, "deep", G_FILE_TYPE_DIRECTORY, NULL);

	check (nemo_file_is_on_a_share (inside));
	check (nemo_file_is_on_a_share (picture));
	check (!nemo_file_should_show_directory_item_count (inside));
	check (!nemo_file_should_show_thumbnail (picture));

	check (!nemo_file_is_on_a_share (home_picture));
	check (nemo_file_should_show_directory_item_count (home_folder));
	check (nemo_file_should_show_thumbnail (home_picture));

	check (nemo_file_is_on_a_share (mount));
	check (!nemo_file_should_show_directory_item_count (mount));
	check (!nemo_file_is_on_a_share (local_mount));

	check (nemo_file_is_on_a_share (through));
	check (!nemo_file_should_show_directory_item_count (through));

	/* Kept per folder, and asked again once the shares change. */
	use_share (NULL);
	check (!nemo_file_is_on_a_share (inside));
	check (!nemo_file_is_on_a_share (through));
	use_share (nas);
	check (nemo_file_is_on_a_share (inside));

	nemo_file_unref (inside);
	nemo_file_unref (picture);
	nemo_file_unref (home_picture);
	nemo_file_unref (home_folder);
	nemo_file_unref (mount);
	nemo_file_unref (local_mount);
	nemo_file_unref (via);
	nemo_file_unref (through);
}

static gboolean loaded;

static void
ready (G_GNUC_UNUSED NemoDirectory *directory, G_GNUC_UNUSED GList *files, G_GNUC_UNUSED gpointer data)
{
	loaded = TRUE;
}

static void
file_ready (G_GNUC_UNUSED NemoFile *file, G_GNUC_UNUSED gpointer data)
{
	loaded = TRUE;
}

static gboolean
give_up (G_GNUC_UNUSED gpointer data)
{
	loaded = TRUE;
	return G_SOURCE_CONTINUE;
}

static void
wait_loaded (void)
{
	guint timeout = g_timeout_add_seconds (20, give_up, NULL);

	while (!loaded) {
		g_main_context_iteration (NULL, TRUE);
	}
	g_source_remove (timeout);
}

static NemoFile *
find_named (GList *files, const char *name)
{
	GList *l;

	for (l = files; l != NULL; l = l->next) {
		if (g_strcmp0 (nemo_file_peek_name (l->data), name) == 0) {
			return l->data;
		}
	}
	return NULL;
}

static gboolean
reads_as_plain_link (NemoFile *file)
{
	g_autofree char *type = NULL;

	if (file == NULL) {
		return FALSE;
	}
	type = nemo_file_get_type_as_string (file);

	return nemo_file_link_is_unfollowed (file) && nemo_file_is_symbolic_link (file) &&
	       !nemo_file_is_directory (file) && !nemo_file_is_broken_symbolic_link (file) &&
	       g_strcmp0 (type, "link") == 0;
}

/* Real links this time. One onto the share is listed as itself, so the
   listing never reads the share; it sorts with the files and reads as a link
   until someone opens it. One onto a local folder is followed, as before. */
static void
check_listing (void)
{
	g_autofree char *dir = g_build_filename (scratch, "listing", NULL);
	g_autofree char *pics = g_build_filename (nas, "pics", NULL);
	g_autofree char *other = g_build_filename (local, "other", NULL);
	g_autofree char *to_nas = g_build_filename (dir, "to-nas", NULL);
	g_autofree char *to_nas_rel = g_build_filename (dir, "to-nas-rel", NULL);
	g_autofree char *to_near = g_build_filename (dir, "to-near", NULL);
	g_autofree char *uri = g_filename_to_uri (dir, NULL, NULL);
	g_autoptr (GFile) to_nas_file = g_file_new_for_path (to_nas);
	GFileInfo *info;
	NemoDirectory *directory;
	NemoFile *file;
	GList *files;
	static int client;

	check (g_mkdir_with_parents (dir, 0700) == 0);
	check (g_mkdir_with_parents (pics, 0700) == 0);
	check (g_mkdir_with_parents (other, 0700) == 0);
	check (symlink (pics, to_nas) == 0);
	check (symlink ("../nas/pics", to_nas_rel) == 0);
	check (symlink (other, to_near) == 0);

	/* One file at a time, the way a changed file is read again. */
	info = nemo_query_listing_info (to_nas_file, NEMO_FILE_DEFAULT_ATTRIBUTES, FALSE, NULL, NULL);
	check (info != NULL && nemo_dir_enum_file_type (info) == G_FILE_TYPE_SYMBOLIC_LINK &&
	       g_file_info_get_attribute_boolean (info, NEMO_FILE_ATTRIBUTE_LINK_UNFOLLOWED));
	g_clear_object (&info);
	info = nemo_query_listing_info (to_nas_file, NEMO_FILE_DEFAULT_ATTRIBUTES, TRUE, NULL, NULL);
	check (info != NULL && nemo_dir_enum_file_type (info) == G_FILE_TYPE_DIRECTORY &&
	       !g_file_info_get_attribute_boolean (info, NEMO_FILE_ATTRIBUTE_LINK_UNFOLLOWED));
	g_clear_object (&info);

	directory = nemo_directory_get_by_uri (uri);
	nemo_directory_file_monitor_add (directory, &client, TRUE,
					 NEMO_FILE_ATTRIBUTE_INFO, NULL, NULL);
	loaded = FALSE;
	nemo_directory_call_when_ready (directory, NEMO_FILE_ATTRIBUTE_INFO, TRUE, ready, NULL);
	wait_loaded ();

	files = nemo_directory_get_file_list (directory);
	check (reads_as_plain_link (find_named (files, "to-nas")));
	check (reads_as_plain_link (find_named (files, "to-nas-rel")));

	file = find_named (files, "to-near");
	check (file != NULL && nemo_file_is_directory (file) && !nemo_file_link_is_unfollowed (file));

	/* Opened: from now on it is what it points at. */
	file = find_named (files, "to-nas");
	if (file != NULL) {
		nemo_file_look_at_link (file);
		loaded = FALSE;
		nemo_file_call_when_ready (file, NEMO_FILE_ATTRIBUTE_INFO, file_ready, NULL);
		wait_loaded ();
		check (nemo_file_is_directory (file) && !nemo_file_link_is_unfollowed (file));
	}
	nemo_file_list_free (files);

	nemo_directory_file_monitor_remove (directory, &client);
	nemo_directory_unref (directory);
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
	check_folders ();
	check_listing ();
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
