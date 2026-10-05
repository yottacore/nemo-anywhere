/* Scratch directories for the tests, removed when the test exits.
 *
 * Only a directory this process made through here is removed, and only while
 * it still sits directly under the temp dir (or the base it was made in) as a
 * real directory. The walk never goes through a link, a junction or onto
 * another filesystem. Home has been lost twice on the dev box; a test helper
 * is not going to be the third. */

#include <config.h>

#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <gio/gio.h>

#ifndef G_OS_WIN32
#include <unistd.h>
#endif

#include <libnemo-private/nemo-dir-enum.h>

#include "test-scratch.h"

#define WALK_ATTRIBUTES \
	G_FILE_ATTRIBUTE_STANDARD_NAME "," \
	G_FILE_ATTRIBUTE_STANDARD_TYPE "," \
	G_FILE_ATTRIBUTE_STANDARD_IS_SYMLINK "," \
	G_FILE_ATTRIBUTE_UNIX_DEVICE "," \
	G_FILE_ATTRIBUTE_UNIX_UID "," \
	G_FILE_ATTRIBUTE_DOS_REPARSE_POINT_TAG

static GPtrArray *made = NULL;
static GPtrArray *made_in = NULL;
static gboolean registered = FALSE;
#ifndef G_OS_WIN32
static pid_t made_by = 0;
#endif

static gboolean
is_real_dir (GFileInfo *info)
{
	return nemo_dir_enum_file_type (info) == G_FILE_TYPE_DIRECTORY &&
	       !g_file_info_get_attribute_boolean (info, G_FILE_ATTRIBUTE_STANDARD_IS_SYMLINK) &&
	       g_file_info_get_attribute_uint32 (info, G_FILE_ATTRIBUTE_DOS_REPARSE_POINT_TAG) == 0;
}

static guint32
device_of (GFileInfo *info)
{
	return g_file_info_get_attribute_uint32 (info, G_FILE_ATTRIBUTE_UNIX_DEVICE);
}

static void
remove_tree (GFile *dir, guint32 device)
{
	GFileEnumerator *children;
	GFileInfo *info;

	children = nemo_enumerate_children (dir, WALK_ATTRIBUTES,
					    G_FILE_QUERY_INFO_NOFOLLOW_SYMLINKS, NULL, NULL);
	if (children != NULL) {
		while ((info = g_file_enumerator_next_file (children, NULL, NULL)) != NULL) {
			GFile *child = g_file_get_child (dir, g_file_info_get_name (info));

			if (!is_real_dir (info)) {
				g_file_delete (child, NULL, NULL);
			} else if (device_of (info) == device) {
				remove_tree (child, device);
			}
			/* A directory on another device is a mount, and is left. */

			g_object_unref (child);
			g_object_unref (info);
		}
		g_object_unref (children);
	}

	g_file_delete (dir, NULL, NULL);
}

static void
remove_made (const char *path, const char *base)
{
	GFile *file, *tmp;
	GFileInfo *info, *tmp_info;
	GError *error = NULL;
	char *parent, *tmp_path;
	gboolean owned;

	file = g_file_new_for_path (path);
	info = g_file_query_info (file, WALK_ATTRIBUTES,
				  G_FILE_QUERY_INFO_NOFOLLOW_SYMLINKS, NULL, &error);
	if (info == NULL) {
		/* The test cleaned up after itself. */
		if (!g_error_matches (error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND)) {
			g_printerr ("scratch: left %s: %s\n", path, error->message);
		}
		g_error_free (error);
		g_object_unref (file);
		return;
	}

	tmp_path = g_canonicalize_filename (base, NULL);
	tmp = g_file_new_for_path (tmp_path);
	tmp_info = g_file_query_info (tmp, WALK_ATTRIBUTES, 0, NULL, NULL);
	parent = g_path_get_dirname (path);

	owned = g_path_is_absolute (path) &&
		strcmp (parent, tmp_path) == 0 &&
		is_real_dir (info) &&
		tmp_info != NULL &&
		device_of (info) == device_of (tmp_info);
#ifndef G_OS_WIN32
	owned = owned && g_file_info_get_attribute_uint32 (info, G_FILE_ATTRIBUTE_UNIX_UID) == getuid ();
#endif

	if (owned) {
		remove_tree (file, device_of (info));
		if (g_file_query_exists (file, NULL)) {
			g_printerr ("scratch: could not remove all of %s\n", path);
		}
	} else {
		g_printerr ("scratch: left %s, it is no longer the directory made there\n", path);
	}

	g_free (parent);
	g_clear_object (&tmp_info);
	g_object_unref (tmp);
	g_free (tmp_path);
	g_object_unref (info);
	g_object_unref (file);
}

/* TRUE while canon sits inside one of the directories this process made. */
static gboolean
inside_a_made_dir (const char *canon)
{
	guint i;

	for (i = 0; made != NULL && i < made->len; i++) {
		const char *root = g_ptr_array_index (made, i);
		gsize len = strlen (root);

		if (strncmp (canon, root, len) == 0 && G_IS_DIR_SEPARATOR (canon[len])) {
			return TRUE;
		}
	}
	return FALSE;
}

gboolean
test_scratch_remove_tree (const char *path)
{
	GFile *file;
	GFileInfo *info;
	char *canon;
	gboolean ok = FALSE;

	canon = g_canonicalize_filename (path, NULL);
	if (!inside_a_made_dir (canon)) {
		g_printerr ("scratch: refused to remove %s, it is not in a scratch directory\n", path);
		g_free (canon);
		return FALSE;
	}

	file = g_file_new_for_path (canon);
	info = g_file_query_info (file, WALK_ATTRIBUTES,
				  G_FILE_QUERY_INFO_NOFOLLOW_SYMLINKS, NULL, NULL);
	if (info != NULL && is_real_dir (info)) {
		remove_tree (file, device_of (info));
		ok = !g_file_query_exists (file, NULL);
	}

	g_clear_object (&info);
	g_object_unref (file);
	g_free (canon);
	return ok;
}

void
test_scratch_cleanup (void)
{
	guint i;

	if (made == NULL) {
		return;
	}

#ifndef G_OS_WIN32
	/* A forked child inherits the exit handler, and must not take the parent's
	   directories out from under it. */
	if (getpid () != made_by) {
		return;
	}
#endif

	for (i = 0; i < made->len; i++) {
		remove_made (g_ptr_array_index (made, i), g_ptr_array_index (made_in, i));
	}

	g_ptr_array_free (made, TRUE);
	g_ptr_array_free (made_in, TRUE);
	made = NULL;
	made_in = NULL;
}

static char *
make_in (const char *base, const char *tmpl, GError **error)
{
	char *path;

	/* g_mkdtemp_full fills in the template where it stands. */
	path = g_build_filename (base, tmpl, NULL);
	if (g_mkdtemp_full (path, 0700) == NULL) {
		int saved = errno;

		g_set_error (error, G_FILE_ERROR, g_file_error_from_errno (saved),
			     "could not make a directory in %s: %s", base, g_strerror (saved));
		g_free (path);
		return NULL;
	}

	if (made == NULL) {
		made = g_ptr_array_new_with_free_func (g_free);
		made_in = g_ptr_array_new_with_free_func (g_free);
	}
	g_ptr_array_add (made, g_canonicalize_filename (path, NULL));
	g_ptr_array_add (made_in, g_canonicalize_filename (base, NULL));

	if (!registered) {
#ifndef G_OS_WIN32
		made_by = getpid ();
#endif
		atexit (test_scratch_cleanup);
		registered = TRUE;
	}

	return path;
}

/* Returns: (transfer full): free with g_free */
char *
test_scratch_dir (const char *tmpl, GError **error)
{
	return make_in (g_get_tmp_dir (), tmpl, error);
}

/* Returns: (transfer full): free with g_free */
char *
test_scratch_dir_in (const char *base, const char *tmpl, GError **error)
{
	return make_in (base, tmpl, error);
}

void
test_scratch_point_config_at (const char *dir)
{
	g_setenv ("HOME", dir, TRUE);
	g_setenv ("APPDATA", dir, TRUE);	/* the config root on Windows */
	g_setenv ("XDG_CONFIG_HOME", dir, TRUE);

	/* The cache root is picked separately from the config one, so a test that
	 * only pointed the config vars would still write a thumbnail database into
	 * the real cache dir. */
	g_setenv ("LOCALAPPDATA", dir, TRUE);
	g_setenv ("XDG_CACHE_HOME", dir, TRUE);
}

/* Returns: (transfer full): free with g_free */
char *
test_scratch_config_home (const char *tmpl)
{
	char *dir = make_in (g_get_tmp_dir (), tmpl, NULL);

	g_return_val_if_fail (dir != NULL, NULL);

	test_scratch_point_config_at (dir);

	return dir;
}

void
test_own_display (int argc, char **argv, const char *screen)
{
#ifndef G_OS_WIN32
	char *xvfb_run;
	char **relaunch;
	int i, n = 0;

	if (g_getenv ("NEMO_TEST_OWN_DISPLAY") != NULL) {
		return;
	}
	xvfb_run = g_find_program_in_path ("xvfb-run");
	if (xvfb_run == NULL) {
		return;
	}
	g_setenv ("NEMO_TEST_OWN_DISPLAY", "1", TRUE);

	relaunch = g_new0 (char *, argc + 5);
	relaunch[n++] = xvfb_run;
	relaunch[n++] = (char *) "-a";
	if (screen != NULL) {
		relaunch[n++] = (char *) "-s";
		relaunch[n++] = g_strdup_printf ("-screen 0 %s", screen);
	}
	for (i = 0; i < argc; i++) {
		relaunch[n++] = argv[i];
	}

	execv (xvfb_run, relaunch);

	/* Only here if the exec failed; the shared display will have to do. */
	if (screen != NULL) {
		g_free (relaunch[3]);
	}
	g_free (relaunch);
	g_free (xvfb_run);
#else
	(void) argc;
	(void) argv;
	(void) screen;
#endif
}
