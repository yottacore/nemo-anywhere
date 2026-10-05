/* A small file is copied with plain writes, so the file system never gets the
 * chance to clone it. Everything the helper will not take has to come back
 * with nothing written, since the ordinary copy runs next and must find the
 * target exactly as it was.
 */

#include <config.h>

#include <stdlib.h>
#include <string.h>
#include <glib/gstdio.h>
#include <gtk/gtk.h>

#include <libnemo-private/nemo-config.h>
#include <libnemo-private/nemo-global-preferences.h>
#include <libnemo-private/nemo-small-copy.h>

#include "test-scratch.h"
#include "test-check.h"

#ifndef G_OS_WIN32
#include <unistd.h>
#include <sys/stat.h>
#endif
#ifdef __linux__
#include <fcntl.h>
#include <sys/ioctl.h>
#include <linux/fs.h>
#include <linux/fiemap.h>
#endif

#define LIMIT (64 * 1024)

static char *scratch;

static char *
path_of (const char *name)
{
	return g_build_filename (scratch, name, NULL);
}

static void
write_sized (const char *name, gsize size)
{
	char *path = path_of (name);
	char *body = g_malloc (size);
	gsize i;

	for (i = 0; i < size; i++) {
		body[i] = (char) g_random_int ();
	}
	check (g_file_set_contents (path, body, size, NULL));

	g_free (body);
	g_free (path);
}

static gboolean
same_contents (const char *a, const char *b)
{
	char *pa = path_of (a), *pb = path_of (b);
	char *ca = NULL, *cb = NULL;
	gsize la = 0, lb = 0;
	gboolean same;

	same = g_file_get_contents (pa, &ca, &la, NULL) &&
	       g_file_get_contents (pb, &cb, &lb, NULL) &&
	       la == lb && memcmp (ca, cb, la) == 0;

	g_free (ca);
	g_free (cb);
	g_free (pa);
	g_free (pb);

	return same;
}

static gboolean
exists (const char *name)
{
	char *path = path_of (name);
	gboolean there = g_file_test (path, G_FILE_TEST_EXISTS | G_FILE_TEST_IS_SYMLINK);

	g_free (path);

	return there;
}

static NemoSmallCopyResult
copy (const char *from, const char *to, GFileCopyFlags flags, goffset limit, goffset *copied)
{
	char *pf = path_of (from), *pt = path_of (to);
	GFile *src = g_file_new_for_path (pf);
	GFile *dest = g_file_new_for_path (pt);
	GError *error = NULL;
	NemoSmallCopyResult result;

	*copied = -1;
	result = nemo_small_copy (src, dest, flags, limit, NULL, copied, &error);
	check (result == NEMO_SMALL_COPY_FAILED || error == NULL);
	g_clear_error (&error);

	g_object_unref (src);
	g_object_unref (dest);
	g_free (pf);
	g_free (pt);

	return result;
}

#ifndef G_OS_WIN32
static void
copy_attributes (const char *from, const char *to)
{
	char *pf = path_of (from), *pt = path_of (to);
	GFile *src = g_file_new_for_path (pf);
	GFile *dest = g_file_new_for_path (pt);

	/* Some attribute nearly always refuses, so the answer means nothing here,
	   and both callers ignore it too. */
	g_file_copy_attributes (src, dest, G_FILE_COPY_NONE, NULL, NULL);

	g_object_unref (src);
	g_object_unref (dest);
	g_free (pf);
	g_free (pt);
}

static void
check_modes_and_links (void)
{
	char *path;
	GStatBuf st;
	goffset copied;

	/* Private until the caller copies the mode across; open when default
	   permissions are asked for. */
	umask (022);
	check (copy ("small.bin", "private.bin", G_FILE_COPY_NONE, LIMIT, &copied) == NEMO_SMALL_COPY_DONE);
	path = path_of ("private.bin");
	check (g_stat (path, &st) == 0 && (st.st_mode & 0777) == 0600);
	g_free (path);
	check (copy ("small.bin", "default-perms.bin", G_FILE_COPY_TARGET_DEFAULT_PERMS, LIMIT, &copied) == NEMO_SMALL_COPY_DONE);
	path = path_of ("default-perms.bin");
	check (g_stat (path, &st) == 0 && (st.st_mode & 0777) == 0644);
	g_free (path);

	/* Both callers copy the attributes after, the template one with only the
	   ordinary set. That has to be enough to bring the mode across. */
	path = path_of ("small.bin");
	check (g_chmod (path, 0750) == 0);
	g_free (path);
	check (copy ("small.bin", "mode.bin", G_FILE_COPY_NONE, LIMIT, &copied) == NEMO_SMALL_COPY_DONE);
	copy_attributes ("small.bin", "mode.bin");
	path = path_of ("mode.bin");
	check (g_stat (path, &st) == 0 && (st.st_mode & 0777) == 0750);
	g_free (path);

	/* A link copied as a link is not a small file. Followed, it is. */
	path = path_of ("link.bin");
	check (symlink ("small.bin", path) == 0);
	g_free (path);
	check (copy ("link.bin", "link-copy.bin", G_FILE_COPY_NOFOLLOW_SYMLINKS, LIMIT, &copied) == NEMO_SMALL_COPY_NOT_TRIED);
	check (!exists ("link-copy.bin"));
	check (copy ("link.bin", "link-copy.bin", G_FILE_COPY_NONE, LIMIT, &copied) == NEMO_SMALL_COPY_DONE);
	check (same_contents ("small.bin", "link-copy.bin"));
}
#endif

#ifdef __linux__
/* 1 when some extent of the file is shared, 0 when none is, -1 when the file
   system will not say. */
static int
shared_extents (const char *name)
{
	const guint extentRoom = 16;
	struct fiemap *map;
	char *path = path_of (name);
	int fd, shared = 0;
	guint i;

	fd = open (path, O_RDONLY);
	g_free (path);
	if (fd < 0) {
		return -1;
	}

	/* fm_extents is a flexible array, so the room for it comes with the
	   allocation rather than a struct around it. */
	map = g_malloc0 (sizeof *map + extentRoom * sizeof (struct fiemap_extent));
	map->fm_length = FIEMAP_MAX_OFFSET;
	map->fm_flags = FIEMAP_FLAG_SYNC;
	map->fm_extent_count = extentRoom;
	if (ioctl (fd, FS_IOC_FIEMAP, map) < 0 || map->fm_mapped_extents == 0) {
		close (fd);
		g_free (map);
		return -1;
	}
	close (fd);

	for (i = 0; i < map->fm_mapped_extents; i++) {
		if (map->fm_extents[i].fe_flags & FIEMAP_EXTENT_SHARED) {
			shared = 1;
		}
	}

	g_free (map);
	return shared;
}

/* Only means something where the ordinary copy clones, so that is shown first
   on the same file. Anywhere else there is nothing to tell apart. */
static void
check_not_cloned (void)
{
	char *pf = path_of ("clone-src.bin"), *pt = path_of ("clone-ordinary.bin");
	GFile *src, *dest;
	goffset copied;

	write_sized ("clone-src.bin", 48 * 1024);
	src = g_file_new_for_path (pf);
	dest = g_file_new_for_path (pt);
	g_file_copy (src, dest, G_FILE_COPY_NONE, NULL, NULL, NULL, NULL);
	g_object_unref (src);
	g_object_unref (dest);
	g_free (pf);
	g_free (pt);

	if (shared_extents ("clone-ordinary.bin") != 1) {
		g_print ("note: this file system does not clone; skipped the extent check\n");
		return;
	}

	check (copy ("clone-src.bin", "clone-small.bin", G_FILE_COPY_NONE, LIMIT, &copied) == NEMO_SMALL_COPY_DONE);
	if (shared_extents ("clone-small.bin") != 0) {
		g_printerr ("FAIL the small copy shares extents with its source\n");
		failures++;
	}
}
#endif

int
main (int argc, char **argv)
{
	char *home;
	goffset copied;

	home = test_scratch_config_home ("nemo-smallcopy-home-XXXXXX");

	gtk_init_check (&argc, &argv);
	nemo_global_preferences_init ();

	scratch = test_scratch_dir ("nemo-smallcopy-XXXXXX", NULL);
	if (scratch == NULL) {
		g_printerr ("FAIL could not make a temp dir\n");
		g_free (home);
		return EXIT_FAILURE;
	}

	/* The setting, and where it has none to give. */
#ifdef __linux__
	check (nemo_small_copy_limit () == LIMIT);
	nemo_config_set_int (nemo_config_get_group (NEMO_PERFORMANCE_GROUP),
			     NEMO_PREFERENCES_CLONE_MIN_KIB, 0);
	check (nemo_small_copy_limit () == 0);
	nemo_config_set_int (nemo_config_get_group (NEMO_PERFORMANCE_GROUP),
			     NEMO_PREFERENCES_CLONE_MIN_KIB, 1 << 20);
	check (nemo_small_copy_limit () == 1024 * 1024);
#else
	check (nemo_small_copy_limit () == 0);
#endif

	write_sized ("small.bin", 1000);
	write_sized ("empty.bin", 0);
	write_sized ("big.bin", LIMIT);
	write_sized ("taken.bin", 10);

	check (copy ("small.bin", "small-copy.bin", G_FILE_COPY_NONE, LIMIT, &copied) == NEMO_SMALL_COPY_DONE);
	check (copied == 1000);
	check (same_contents ("small.bin", "small-copy.bin"));

	check (copy ("empty.bin", "empty-copy.bin", G_FILE_COPY_NONE, LIMIT, &copied) == NEMO_SMALL_COPY_DONE);
	check (copied == 0);
	check (same_contents ("empty.bin", "empty-copy.bin"));

	/* At the limit is not under it. */
	check (copy ("big.bin", "big-copy.bin", G_FILE_COPY_NONE, LIMIT, &copied) == NEMO_SMALL_COPY_NOT_TRIED);
	check (!exists ("big-copy.bin"));

	check (copy ("small.bin", "off.bin", G_FILE_COPY_NONE, 0, &copied) == NEMO_SMALL_COPY_NOT_TRIED);
	check (!exists ("off.bin"));

	/* A target already there is left for the ordinary copy to report. */
	check (copy ("small.bin", "taken.bin", G_FILE_COPY_NONE, LIMIT, &copied) == NEMO_SMALL_COPY_NOT_TRIED);
	check (!same_contents ("small.bin", "taken.bin"));
	check (copy ("small.bin", "taken.bin", G_FILE_COPY_OVERWRITE, LIMIT, &copied) == NEMO_SMALL_COPY_NOT_TRIED);
	check (!same_contents ("small.bin", "taken.bin"));

	check (copy ("missing.bin", "missing-copy.bin", G_FILE_COPY_NONE, LIMIT, &copied) == NEMO_SMALL_COPY_NOT_TRIED);
	check (!exists ("missing-copy.bin"));

	check (copy ("", "dir-copy", G_FILE_COPY_NONE, LIMIT, &copied) == NEMO_SMALL_COPY_NOT_TRIED);
	check (!exists ("dir-copy"));

#ifndef G_OS_WIN32
	check_modes_and_links ();
#endif

#ifdef __linux__
	check_not_cloned ();
#endif

	g_free (scratch);
	g_free (home);

	if (failures == 0) {
		g_print ("nemo-small-copy: all checks passed\n");
	}

	return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
