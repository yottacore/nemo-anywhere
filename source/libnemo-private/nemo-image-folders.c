/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */

/* nemo-image-folders.c - which folders are mostly pictures.

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

#include <config.h>

#include "nemo-image-folders.h"

#include "nemo-dir-enum.h"
#include "nemo-file.h"
#include "nemo-file-utilities.h"
#include "nemo-global-preferences.h"
#include "nemo-search-directory.h"

/* Enough to cover the folders anyone moves between in a sitting. */
#define REMEMBERED_MAX 512

/* A look-ahead is a guess made to save a redraw, so it is kept cheap: a folder
   with more sub-folders than this only gets its first ones counted, and a big
   folder is judged on its first entries. The real count after the load always
   has the last word. */
#define LOOK_AHEAD_FOLDERS 32
#define LOOK_AHEAD_ENTRIES 1000

static GHashTable *remembered;	/* uri -> answer + 1 */
static GQueue remembered_order = G_QUEUE_INIT;	/* newest first, owns the uris */
static GCancellable *look_ahead_cancel;

gboolean
nemo_image_folders_counts_say_mostly (guint images, guint others)
{
	gint min_images, min_percent;

	/* The floor is there because one picture among a pile of other things is
	   not a gallery. */
	min_images = nemo_config_get_int (nemo_icon_view_preferences,
					  NEMO_PREFERENCES_ICON_VIEW_IMAGE_FOLDER_MIN_IMAGES);
	min_percent = nemo_config_get_int (nemo_icon_view_preferences,
					   NEMO_PREFERENCES_ICON_VIEW_IMAGE_FOLDER_MIN_PERCENT);

	return images >= (guint) MAX (min_images, 1) &&
	       (guint64) images * 100 >= (guint64) CLAMP (min_percent, 0, 100) * (images + others);
}

void
nemo_image_folders_note (GFile *location, gboolean mostly)
{
	char *uri;
	GList *old;

	if (remembered == NULL) {
		remembered = g_hash_table_new (g_str_hash, g_str_equal);
	}

	uri = g_file_get_uri (location);

	old = g_queue_find_custom (&remembered_order, uri, (GCompareFunc) g_strcmp0);
	if (old != NULL) {
		g_free (uri);
		uri = old->data;
		g_queue_unlink (&remembered_order, old);
		g_list_free (old);
	}

	g_queue_push_head (&remembered_order, uri);
	g_hash_table_insert (remembered, uri, GINT_TO_POINTER (mostly ? 2 : 1));

	while (g_queue_get_length (&remembered_order) > REMEMBERED_MAX) {
		char *oldest = g_queue_pop_tail (&remembered_order);

		g_hash_table_remove (remembered, oldest);
		g_free (oldest);
	}
}

gboolean
nemo_image_folders_known (GFile *location, gboolean *mostly)
{
	char *uri;
	gint answer;

	if (remembered == NULL) {
		return FALSE;
	}

	uri = g_file_get_uri (location);
	answer = GPOINTER_TO_INT (g_hash_table_lookup (remembered, uri));
	g_free (uri);

	if (answer == 0) {
		return FALSE;
	}

	*mostly = answer == 2;
	return TRUE;
}

gboolean
nemo_image_folders_wants_icon_view (NemoFile *folder, const char *saved_view)
{
	NemoDirectory *directory;
	gboolean wanted;

	if (saved_view != NULL ||
	    !nemo_config_get_boolean (nemo_icon_view_preferences,
				      NEMO_PREFERENCES_ICON_VIEW_IMAGE_FOLDER_SWITCH)) {
		return FALSE;
	}

	directory = nemo_directory_get_for_file (folder);
	if (NEMO_IS_SEARCH_DIRECTORY (directory)) {
		wanted = FALSE;
	} else if (nemo_directory_are_all_files_seen (directory)) {
		wanted = nemo_directory_is_mostly_images (directory);
	} else {
		GFile *location = nemo_file_get_location (folder);

		if (!nemo_image_folders_known (location, &wanted)) {
			wanted = FALSE;
		}
		g_object_unref (location);
	}
	nemo_directory_unref (directory);

	return wanted;
}

typedef struct {
	GPtrArray *folders;	/* GFile */
	GArray *images;		/* guint per folder, filled by the thread */
	GArray *others;
	NemoImageFoldersDone done;
	gpointer user_data;
} LookAhead;

static void
look_ahead_free (LookAhead *job)
{
	g_ptr_array_unref (job->folders);
	g_array_unref (job->images);
	g_array_unref (job->others);
	g_free (job);
}

static void
count_one (GFile *folder, GCancellable *cancellable, guint *images, guint *others)
{
	GFileEnumerator *children;
	GFileInfo *info;
	guint seen = 0;

	*images = 0;
	*others = 0;

	children = nemo_enumerate_children (folder,
					    G_FILE_ATTRIBUTE_STANDARD_TYPE ","
					    G_FILE_ATTRIBUTE_STANDARD_FAST_CONTENT_TYPE,
					    G_FILE_QUERY_INFO_NOFOLLOW_SYMLINKS,
					    cancellable, NULL);
	if (children == NULL) {
		return;
	}

	while (seen < LOOK_AHEAD_ENTRIES &&
	       (info = g_file_enumerator_next_file (children, cancellable, NULL)) != NULL) {
		const char *type;

		seen++;

		if (nemo_dir_enum_file_type (info) == G_FILE_TYPE_DIRECTORY) {
			g_object_unref (info);
			continue;
		}

		type = g_file_info_get_attribute_string (info, G_FILE_ATTRIBUTE_STANDARD_FAST_CONTENT_TYPE);
		if (nemo_content_type_is_a (type, "image/*")) {
			(*images)++;
		} else {
			(*others)++;
		}
		g_object_unref (info);
	}

	g_object_unref (children);
}

static void
look_ahead_thread (GTask *task, G_GNUC_UNUSED gpointer source, gpointer task_data, GCancellable *cancellable)
{
	LookAhead *job = task_data;
	guint i;

	for (i = 0; i < job->folders->len; i++) {
		guint images, others;

		if (g_cancellable_is_cancelled (cancellable)) {
			break;
		}

		count_one (g_ptr_array_index (job->folders, i), cancellable, &images, &others);

		/* A count cut off part way would be a wrong answer, not a rough one. */
		if (g_cancellable_is_cancelled (cancellable)) {
			break;
		}
		g_array_append_val (job->images, images);
		g_array_append_val (job->others, others);
	}

	g_task_return_boolean (task, TRUE);
}

static void
look_ahead_done (G_GNUC_UNUSED GObject *source, GAsyncResult *res, G_GNUC_UNUSED gpointer user_data)
{
	GTask *task = G_TASK (res);
	LookAhead *job = g_task_get_task_data (task);
	guint i;

	/* A cancelled run still answers for the folders it finished. */
	for (i = 0; i < job->images->len; i++) {
		guint images = g_array_index (job->images, guint, i);
		guint others = g_array_index (job->others, guint, i);

		nemo_image_folders_note (g_ptr_array_index (job->folders, i),
					 nemo_image_folders_counts_say_mostly (images, others));
	}

	if (job->done != NULL) {
		job->done (job->user_data);
	}
}

/* Only a plain local folder is counted. A share or a link can cost a network
   timeout per entry, which is a poor trade for a guess. */
static gboolean
worth_counting (NemoFile *file)
{
	GFile *location;
	gboolean mostly;

	if (!nemo_file_is_directory (file) || nemo_file_is_symbolic_link (file) ||
	    !nemo_file_is_local (file) || nemo_file_is_on_a_share (file)) {
		return FALSE;
	}

	location = nemo_file_get_location (file);
	if (!g_file_is_native (location) || nemo_image_folders_known (location, &mostly)) {
		g_object_unref (location);
		return FALSE;
	}
	g_object_unref (location);

	return TRUE;
}

void
nemo_image_folders_look_ahead_full (NemoDirectory *directory, NemoImageFoldersDone done, gpointer user_data)
{
	LookAhead *job;
	GTask *task;
	GList *files, *l;

	if (look_ahead_cancel != NULL) {
		g_cancellable_cancel (look_ahead_cancel);
		g_clear_object (&look_ahead_cancel);
	}

	job = g_new0 (LookAhead, 1);
	job->folders = g_ptr_array_new_with_free_func (g_object_unref);
	job->images = g_array_new (FALSE, FALSE, sizeof (guint));
	job->others = g_array_new (FALSE, FALSE, sizeof (guint));
	job->done = done;
	job->user_data = user_data;

	files = nemo_directory_get_file_list (directory);
	for (l = files; l != NULL && job->folders->len < LOOK_AHEAD_FOLDERS; l = l->next) {
		if (worth_counting (NEMO_FILE (l->data))) {
			g_ptr_array_add (job->folders, nemo_file_get_location (NEMO_FILE (l->data)));
		}
	}
	nemo_file_list_free (files);

	if (job->folders->len == 0) {
		look_ahead_free (job);
		if (done != NULL) {
			done (user_data);
		}
		return;
	}

	look_ahead_cancel = g_cancellable_new ();
	task = g_task_new (NULL, look_ahead_cancel, look_ahead_done, NULL);
	g_task_set_task_data (task, job, (GDestroyNotify) look_ahead_free);
	g_task_set_priority (task, G_PRIORITY_LOW);
	g_task_run_in_thread (task, look_ahead_thread);
	g_object_unref (task);
}

void
nemo_image_folders_look_ahead (NemoDirectory *directory)
{
	nemo_image_folders_look_ahead_full (directory, NULL, NULL);
}
