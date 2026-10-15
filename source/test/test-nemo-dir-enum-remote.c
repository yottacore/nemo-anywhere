/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */

/* test-nemo-dir-enum-remote.c - a share folder that also has a local path lists.

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

/* While gvfsd-fuse runs, every gvfs location (smb, sftp and the rest) also has
 * a path, under /run/user/<uid>/gvfs. gvfs's enumerator refuses its sync
 * next_file once it was opened async. The folder listing took any folder with
 * a path for local and read it through a wrapper that makes that sync call, so
 * every share showed an error and no files.
 *
 * The stand-in here is a non-native file over a scratch folder, with a path,
 * whose enumerator refuses the same way. Its listing has to come back whole,
 * with the link in it followed, the way gvfs follows links on the server. A
 * look at one file there has to go out once, following links. */

#include <config.h>

#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <gio/gio.h>
#include <glib/gstdio.h>

#include <libnemo-private/nemo-dir-enum.h>

#include "test-scratch.h"
#include "test-check.h"

#define FAKE_SCHEME "fakeshare"

static int query_count;
static GFileQueryInfoFlags first_query_flags;

/* The enumerator */

G_DECLARE_FINAL_TYPE (FakeEnum, fake_enum, FAKE, ENUM, GFileEnumerator)

struct _FakeEnum {
	GFileEnumerator parent_instance;

	GFileEnumerator *inner;
	gboolean opened_async;
};

G_DEFINE_TYPE (FakeEnum, fake_enum, G_TYPE_FILE_ENUMERATOR)

static GFileInfo *
fake_enum_next_file (GFileEnumerator  *enumerator,
		     GCancellable     *cancellable,
		     GError          **error)
{
	FakeEnum *self = FAKE_ENUM (enumerator);

	/* gvfs's own words */
	if (self->opened_async) {
		g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_FAILED,
				     "Can't do synchronous next_files() on a file enumerator created asynchronously");
		return NULL;
	}

	return g_file_enumerator_next_file (self->inner, cancellable, error);
}

static void
free_info_list (gpointer list)
{
	g_list_free_full (list, g_object_unref);
}

static void
fake_enum_next_files_async (GFileEnumerator     *enumerator,
			    int                  num_files,
			    G_GNUC_UNUSED int    io_priority,
			    GCancellable        *cancellable,
			    GAsyncReadyCallback  callback,
			    gpointer             user_data)
{
	FakeEnum *self = FAKE_ENUM (enumerator);
	GTask *task = g_task_new (enumerator, cancellable, callback, user_data);
	GList *infos = NULL;
	GError *error = NULL;
	int i;

	for (i = 0; i < num_files; i++) {
		GFileInfo *info = g_file_enumerator_next_file (self->inner, cancellable, &error);

		if (info == NULL) {
			break;
		}
		infos = g_list_prepend (infos, info);
	}

	if (error != NULL) {
		g_list_free_full (infos, g_object_unref);
		g_task_return_error (task, error);
	} else {
		g_task_return_pointer (task, g_list_reverse (infos), free_info_list);
	}
	g_object_unref (task);
}

static GList *
fake_enum_next_files_finish (G_GNUC_UNUSED GFileEnumerator *enumerator,
			     GAsyncResult    *result,
			     GError         **error)
{
	return g_task_propagate_pointer (G_TASK (result), error);
}

static gboolean
fake_enum_close (GFileEnumerator  *enumerator,
		 GCancellable     *cancellable,
		 GError          **error)
{
	FakeEnum *self = FAKE_ENUM (enumerator);

	/* GLib closes an unclosed one from its own finalize, after ours ran */
	return self->inner == NULL || g_file_enumerator_close (self->inner, cancellable, error);
}

static void
fake_enum_finalize (GObject *object)
{
	g_clear_object (&FAKE_ENUM (object)->inner);

	G_OBJECT_CLASS (fake_enum_parent_class)->finalize (object);
}

static void
fake_enum_init (G_GNUC_UNUSED FakeEnum *self)
{
}

static void
fake_enum_class_init (FakeEnumClass *klass)
{
	G_OBJECT_CLASS (klass)->finalize = fake_enum_finalize;
	G_FILE_ENUMERATOR_CLASS (klass)->next_file = fake_enum_next_file;
	G_FILE_ENUMERATOR_CLASS (klass)->next_files_async = fake_enum_next_files_async;
	G_FILE_ENUMERATOR_CLASS (klass)->next_files_finish = fake_enum_next_files_finish;
	G_FILE_ENUMERATOR_CLASS (klass)->close_fn = fake_enum_close;
}

/* The file */

G_DECLARE_FINAL_TYPE (FakeFile, fake_file, FAKE, FILE, GObject)

struct _FakeFile {
	GObject parent_instance;

	GFile *real;
};

static void fake_file_iface_init (GFileIface *iface);

G_DEFINE_TYPE_WITH_CODE (FakeFile, fake_file, G_TYPE_OBJECT,
			 G_IMPLEMENT_INTERFACE (G_TYPE_FILE, fake_file_iface_init))

/* Takes real. */
static GFile *
fake_wrap (GFile *real)
{
	FakeFile *self;

	if (real == NULL) {
		return NULL;
	}

	self = g_object_new (fake_file_get_type (), NULL);
	self->real = real;

	return G_FILE (self);
}

static GFile *
real_of (GFile *file)
{
	return FAKE_FILE (file)->real;
}

static GFile *
fake_dup (GFile *file)
{
	return fake_wrap (g_file_dup (real_of (file)));
}

static guint
fake_hash (GFile *file)
{
	return g_file_hash (real_of (file));
}

static gboolean
fake_equal (GFile *a, GFile *b)
{
	return g_file_equal (real_of (a), real_of (b));
}

static gboolean
fake_is_native (G_GNUC_UNUSED GFile *file)
{
	return FALSE;
}

static gboolean
fake_has_uri_scheme (G_GNUC_UNUSED GFile *file, const char *scheme)
{
	return g_ascii_strcasecmp (scheme, FAKE_SCHEME) == 0;
}

static char *
fake_get_uri_scheme (G_GNUC_UNUSED GFile *file)
{
	return g_strdup (FAKE_SCHEME);
}

static char *
fake_get_basename (GFile *file)
{
	return g_file_get_basename (real_of (file));
}

/* What gvfsd-fuse gives a gvfs location. */
static char *
fake_get_path (GFile *file)
{
	return g_file_get_path (real_of (file));
}

static char *
fake_get_uri (GFile *file)
{
	g_autofree char *path = g_file_get_path (real_of (file));

	return g_strconcat (FAKE_SCHEME, "://", path, NULL);
}

static GFile *
fake_get_parent (GFile *file)
{
	return fake_wrap (g_file_get_parent (real_of (file)));
}

static gboolean
fake_prefix_matches (GFile *prefix, GFile *file)
{
	return g_file_has_prefix (real_of (file), real_of (prefix));
}

static char *
fake_get_relative_path (GFile *parent, GFile *descendant)
{
	return g_file_get_relative_path (real_of (parent), real_of (descendant));
}

static GFile *
fake_resolve_relative_path (GFile *file, const char *relative_path)
{
	return fake_wrap (g_file_resolve_relative_path (real_of (file), relative_path));
}

static GFile *
fake_get_child_for_display_name (GFile *file, const char *name, GError **error)
{
	return fake_wrap (g_file_get_child_for_display_name (real_of (file), name, error));
}

static GFileInfo *
fake_query_info (GFile               *file,
		 const char          *attributes,
		 GFileQueryInfoFlags  flags,
		 GCancellable        *cancellable,
		 GError             **error)
{
	if (query_count++ == 0) {
		first_query_flags = flags;
	}

	return g_file_query_info (real_of (file), attributes, flags, cancellable, error);
}

static GFileEnumerator *
fake_enum_new (GFile *dir, GFileEnumerator *inner, gboolean opened_async)
{
	FakeEnum *self;

	if (inner == NULL) {
		return NULL;
	}

	self = g_object_new (fake_enum_get_type (), "container", dir, NULL);
	self->inner = inner;
	self->opened_async = opened_async;

	return G_FILE_ENUMERATOR (self);
}

static GFileEnumerator *
fake_enumerate_children (GFile               *file,
			 const char          *attributes,
			 GFileQueryInfoFlags  flags,
			 GCancellable        *cancellable,
			 GError             **error)
{
	return fake_enum_new (file, g_file_enumerate_children (real_of (file), attributes, flags,
							       cancellable, error), FALSE);
}

static void
fake_enumerate_children_async (GFile               *file,
			       const char          *attributes,
			       GFileQueryInfoFlags  flags,
			       G_GNUC_UNUSED int    io_priority,
			       GCancellable        *cancellable,
			       GAsyncReadyCallback  callback,
			       gpointer             user_data)
{
	GTask *task = g_task_new (file, cancellable, callback, user_data);
	GError *error = NULL;
	GFileEnumerator *enumerator;

	enumerator = fake_enum_new (file, g_file_enumerate_children (real_of (file), attributes, flags,
								     cancellable, &error), TRUE);
	if (enumerator != NULL) {
		g_task_return_pointer (task, enumerator, g_object_unref);
	} else {
		g_task_return_error (task, error);
	}
	g_object_unref (task);
}

static GFileEnumerator *
fake_enumerate_children_finish (G_GNUC_UNUSED GFile *file,
				GAsyncResult *result,
				GError      **error)
{
	return g_task_propagate_pointer (G_TASK (result), error);
}

static void
fake_file_iface_init (GFileIface *iface)
{
	iface->dup = fake_dup;
	iface->hash = fake_hash;
	iface->equal = fake_equal;
	iface->is_native = fake_is_native;
	iface->has_uri_scheme = fake_has_uri_scheme;
	iface->get_uri_scheme = fake_get_uri_scheme;
	iface->get_basename = fake_get_basename;
	iface->get_path = fake_get_path;
	iface->get_uri = fake_get_uri;
	iface->get_parse_name = fake_get_uri;
	iface->get_parent = fake_get_parent;
	iface->prefix_matches = fake_prefix_matches;
	iface->get_relative_path = fake_get_relative_path;
	iface->resolve_relative_path = fake_resolve_relative_path;
	iface->get_child_for_display_name = fake_get_child_for_display_name;
	iface->query_info = fake_query_info;
	iface->enumerate_children = fake_enumerate_children;
	iface->enumerate_children_async = fake_enumerate_children_async;
	iface->enumerate_children_finish = fake_enumerate_children_finish;
}

static void
fake_file_finalize (GObject *object)
{
	g_clear_object (&FAKE_FILE (object)->real);

	G_OBJECT_CLASS (fake_file_parent_class)->finalize (object);
}

static void
fake_file_init (G_GNUC_UNUSED FakeFile *self)
{
}

static void
fake_file_class_init (FakeFileClass *klass)
{
	G_OBJECT_CLASS (klass)->finalize = fake_file_finalize;
}

/* The listing, read the way the folder code reads it */

typedef struct {
	GMainLoop *loop;
	GFileEnumerator *enumerator;
	GHashTable *types;	/* name -> GFileType */
	GError *error;
} Listing;

static void
got_files (GObject *source, GAsyncResult *result, gpointer user_data)
{
	Listing *listing = user_data;
	GList *infos, *l;

	infos = g_file_enumerator_next_files_finish (G_FILE_ENUMERATOR (source), result, &listing->error);
	if (infos == NULL) {
		g_main_loop_quit (listing->loop);
		return;
	}

	for (l = infos; l != NULL; l = l->next) {
		g_hash_table_insert (listing->types, g_strdup (g_file_info_get_name (l->data)),
				     GINT_TO_POINTER (nemo_dir_enum_file_type (l->data)));
	}
	g_list_free_full (infos, g_object_unref);

	g_file_enumerator_next_files_async (listing->enumerator, 2, G_PRIORITY_DEFAULT, NULL,
					    got_files, listing);
}

static void
opened (GObject *source, GAsyncResult *result, gpointer user_data)
{
	Listing *listing = user_data;

	listing->enumerator = nemo_enumerate_children_finish (G_FILE (source), result, &listing->error);
	if (listing->enumerator == NULL) {
		g_main_loop_quit (listing->loop);
		return;
	}

	g_file_enumerator_next_files_async (listing->enumerator, 2, G_PRIORITY_DEFAULT, NULL,
					    got_files, listing);
}

static gboolean
give_up (gpointer user_data)
{
	g_printerr ("FAIL: the listing never finished\n");
	failures++;
	g_main_loop_quit (user_data);
	return G_SOURCE_REMOVE;
}

static void
check_listing (GFile *dir)
{
	Listing listing = { 0 };
	guint timeout;

	listing.loop = g_main_loop_new (NULL, FALSE);
	listing.types = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);

	nemo_enumerate_listing_async (dir, G_FILE_ATTRIBUTE_STANDARD_NAME "," G_FILE_ATTRIBUTE_STANDARD_TYPE,
				      G_PRIORITY_DEFAULT, NULL, opened, &listing);
	timeout = g_timeout_add_seconds (20, give_up, listing.loop);
	g_main_loop_run (listing.loop);
	g_source_remove (timeout);

	if (listing.error != NULL) {
		g_printerr ("listing error: %s\n", listing.error->message);
	}
	check (listing.error == NULL);
	check (g_hash_table_size (listing.types) == 3);
	check (GPOINTER_TO_INT (g_hash_table_lookup (listing.types, "a.txt")) == G_FILE_TYPE_REGULAR);
	check (GPOINTER_TO_INT (g_hash_table_lookup (listing.types, "b folder")) == G_FILE_TYPE_DIRECTORY);
	/* followed, as gvfs does on the server */
	check (GPOINTER_TO_INT (g_hash_table_lookup (listing.types, "c link")) == G_FILE_TYPE_REGULAR);

	if (listing.enumerator != NULL) {
		check (g_file_enumerator_close (listing.enumerator, NULL, NULL));
	}
	g_clear_error (&listing.error);
	g_clear_object (&listing.enumerator);
	g_hash_table_unref (listing.types);
	g_main_loop_unref (listing.loop);
}

static void
check_one_file (GFile *dir)
{
	g_autoptr (GFile) link = g_file_get_child (dir, "c link");
	g_autoptr (GFileInfo) info = NULL;
	g_autoptr (GError) error = NULL;

	query_count = 0;
	info = nemo_query_listing_info (link, G_FILE_ATTRIBUTE_STANDARD_TYPE, FALSE, NULL, &error);

	check (info != NULL);
	check (query_count == 1);
	check (first_query_flags == G_FILE_QUERY_INFO_NONE);
	check (info != NULL && nemo_dir_enum_file_type (info) == G_FILE_TYPE_REGULAR);
	check (info != NULL && !g_file_info_get_attribute_boolean (info, NEMO_FILE_ATTRIBUTE_LINK_UNFOLLOWED));
}

int
main (void)
{
	g_autofree char *scratch = test_scratch_dir ("nemo-direnum-remote-XXXXXX", NULL);
	g_autofree char *file_a = NULL;
	g_autofree char *folder_b = NULL;
	g_autofree char *link_c = NULL;
	g_autoptr (GFile) dir = NULL;

	if (scratch == NULL) {
		g_printerr ("could not make a scratch dir\n");
		return 77;
	}

	file_a = g_build_filename (scratch, "a.txt", NULL);
	folder_b = g_build_filename (scratch, "b folder", NULL);
	link_c = g_build_filename (scratch, "c link", NULL);
	check (g_file_set_contents (file_a, "hello\n", -1, NULL));
	check (g_mkdir (folder_b, 0700) == 0);
	check (symlink ("a.txt", link_c) == 0);

	dir = fake_wrap (g_file_new_for_path (scratch));

	/* the stand-in has to look like a fuse-backed gvfs folder */
	check (!g_file_is_native (dir));
	check (g_file_peek_path (dir) != NULL);

	check_listing (dir);
	check_one_file (dir);

	if (failures > 0) {
		g_printerr ("%d check(s) failed\n", failures);
		return EXIT_FAILURE;
	}

	g_print ("OK\n");
	return EXIT_SUCCESS;
}
