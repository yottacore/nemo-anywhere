/* A small file is copied with plain writes, so the file system never gets the
 * chance to clone it. Everything the helper won't take has to come back
 * with nothing written, since the ordinary copy runs next and must find the
 * target exactly as it was.
 */

#include <config.h>

#include <stdlib.h>
#include <string.h>
#include <glib/gstdio.h>
#include <gtk/gtk.h>

#include <libnemo-private/nemo-cache-db.h>
#include <libnemo-private/nemo-config.h>
#include <libnemo-private/nemo-external-drive.h>
#include <libnemo-private/nemo-file-digest.h>
#include <libnemo-private/nemo-file-xattr.h>
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
#include <sys/sysmacros.h>
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
	result = nemo_small_copy (src, dest, flags, limit, NULL, copied, NULL, &error);
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

	/* Some attribute nearly always refuses, so the result means nothing here,
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

	/* A link copied as a link isn't a small file. Followed, it is. */
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
   system won't say. */
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

#ifdef __linux__
/* Size and time the way the cache keys them. */
static void
file_id (GFile *file, NemoFileId *id)
{
	GFileInfo *info = g_file_query_info (file,
					     G_FILE_ATTRIBUTE_STANDARD_SIZE ","
					     G_FILE_ATTRIBUTE_TIME_MODIFIED ","
					     G_FILE_ATTRIBUTE_TIME_MODIFIED_USEC,
					     G_FILE_QUERY_INFO_NONE, NULL, NULL);

	memset (id, 0, sizeof *id);
	check (info != NULL);
	if (info == NULL) {
		return;
	}
	id->bytes = g_file_info_get_size (info);
	id->mtime = (gint64) g_file_info_get_attribute_uint64 (info, G_FILE_ATTRIBUTE_TIME_MODIFIED) * G_USEC_PER_SEC
		    + g_file_info_get_attribute_uint32 (info, G_FILE_ATTRIBUTE_TIME_MODIFIED_USEC);
	g_object_unref (info);
}

static gboolean
cached_as (GFile *file, const guint8 *want)
{
	g_autofree char *uri = g_file_get_uri (file);
	guint8 got[NEMO_CACHE_DIGEST_LEN];
	NemoFileId id;

	file_id (file, &id);

	return nemo_cache_db_lookup_digest (nemo_cache_db_get (), uri, id.bytes, id.mtime, got) &&
	       memcmp (got, want, sizeof got) == 0;
}

static gboolean
on_file_as (GFile *file, const guint8 *want)
{
	guint8 got[NEMO_CACHE_DIGEST_LEN];
	NemoFileId id;

	file_id (file, &id);

	return nemo_file_digest_read_attr (file, id.bytes, id.mtime, got) &&
	       memcmp (got, want, sizeof got) == 0;
}

static gboolean
xattrs_here (void)
{
	g_autofree char *path = path_of ("xattr-probe");
	g_autoptr (GFile) file = g_file_new_for_path (path);
	g_autofree char *back = NULL;

	check (g_file_set_contents (path, "x", 1, NULL));
	if (!nemo_file_xattr_set (file, "blake3.probe", "1")) {
		return FALSE;
	}
	back = nemo_file_xattr_get (file, "blake3.probe");

	return back != NULL && g_str_equal (back, "1");
}

/* One small copy and the caller's attribute step, then the checksum kept the
   way the copy job keeps it, through sums, or at once for NULL. FALSE if the
   copy never got a checksum. */
static gboolean
copy_and_keep_in (NemoSmallCopySums *sums, const char *from, const char *to, guint8 *want)
{
	g_autofree char *pf = path_of (from);
	g_autofree char *pt = path_of (to);
	g_autoptr (GFile) src = g_file_new_for_path (pf);
	g_autoptr (GFile) dest = g_file_new_for_path (pt);
	g_autofree char *body = NULL;
	gsize len = 0;
	goffset copied;
	NemoFileId sum;

	check (g_file_get_contents (pf, &body, &len, NULL));
	nemo_file_digest_bytes (body, len, want);

	sum.has_digest = TRUE;
	check (nemo_small_copy (src, dest, G_FILE_COPY_NONE, LIMIT, NULL, &copied, &sum, NULL) == NEMO_SMALL_COPY_DONE);
	g_file_copy_attributes (src, dest, G_FILE_COPY_ALL_METADATA, NULL, NULL);
	nemo_small_copy_keep_checksum (sums, src, dest, &sum);

	if (sum.has_digest) {
		check (sum.bytes == (gint64) len);
		check (memcmp (sum.digest, want, NEMO_CACHE_DIGEST_LEN) == 0);
	}

	return sum.has_digest;
}

static gboolean
copy_and_keep (const char *from, const char *to, guint8 *want)
{
	return copy_and_keep_in (NULL, from, to, want);
}

static gboolean
both_cached_as (const char *from, const char *to, const guint8 *want)
{
	g_autofree char *pf = path_of (from);
	g_autofree char *pt = path_of (to);
	g_autoptr (GFile) src = g_file_new_for_path (pf);
	g_autoptr (GFile) dest = g_file_new_for_path (pt);

	return cached_as (src, want) && cached_as (dest, want);
}

#define MANY 300

/* A copy job's checksums go into the cache a batch at a time. Each write is a
   transaction, and a write each made a copy of many small files about 30 times
   slower. */
static void
check_checksums_batched (void)
{
	NemoConfigGroup *group = nemo_config_get_group (NEMO_FILE_CACHE_GROUP);
	NemoSmallCopySums *sums;
	guint8 (*wants)[NEMO_CACHE_DIGEST_LEN] = g_malloc0 (sizeof *wants * MANY);
	guint8 want[NEMO_CACHE_DIGEST_LEN];
	char from[32], to[32];
	gint before, writes;
	int i, missed = 0;

	nemo_config_reset (group, NEMO_FILE_CACHE_CHECKSUM_FULL_READS);
	nemo_config_reset (group, NEMO_FILE_CACHE_SAVE_CHECKSUM);

	/* One copy kept at once is one write for both names. */
	write_sized ("one.bin", 700);
	before = nemo_cache_db_commits ();
	check (copy_and_keep ("one.bin", "one-copy.bin", want));
	check (nemo_cache_db_commits () - before == 1);
	check (both_cached_as ("one.bin", "one-copy.bin", want));

	for (i = 0; i < MANY; i++) {
		g_snprintf (from, sizeof from, "many-%03d.bin", i);
		write_sized (from, 100 + (gsize) i * 7);
	}

	sums = nemo_small_copy_sums_new ();
	before = nemo_cache_db_commits ();
	for (i = 0; i < MANY; i++) {
		g_snprintf (from, sizeof from, "many-%03d.bin", i);
		g_snprintf (to, sizeof to, "many-%03d-copy.bin", i);
		check (copy_and_keep_in (sums, from, to, wants[i]));
	}
	nemo_small_copy_sums_free (sums);
	writes = nemo_cache_db_commits () - before;

	/* 600 names make 2 batches. The held-too-long rule can add a few on a
	   slow box, never one a name. */
	g_print ("note: %d copies kept in %d cache writes\n", MANY, writes);
	check (writes >= 2 && writes <= MANY / 10);

	for (i = 0; i < MANY; i++) {
		g_snprintf (from, sizeof from, "many-%03d.bin", i);
		g_snprintf (to, sizeof to, "many-%03d-copy.bin", i);
		if (!both_cached_as (from, to, wants[i])) {
			missed++;
		}
	}
	check (missed == 0);

	g_free (wants);
}

/* file-cache.checksum-full-reads keeps the checksum of a small copy's source
   and target, and save-checksum puts it onto both as a thumbnail's is. Only
   Linux makes small copies. */
static void
check_checksum_kept (void)
{
	NemoConfigGroup *group = nemo_config_get_group (NEMO_FILE_CACHE_GROUP);
	gboolean xattrs = xattrs_here ();
	guint8 want[NEMO_CACHE_DIGEST_LEN];
	g_autofree char *pf = NULL, *pt = NULL;
	g_autoptr (GFile) src = NULL;
	g_autoptr (GFile) dest = NULL;

	check (nemo_cache_db_get () != NULL);
	if (!xattrs) {
		g_print ("note: no extended attributes here; skipped the on-file checks\n");
	}

	write_sized ("sum-off.bin", 3000);
	write_sized ("sum-cache.bin", 2000);
	write_sized ("sum-save.bin", 5000);

	nemo_config_set_boolean (group, NEMO_FILE_CACHE_CHECKSUM_FULL_READS, FALSE);
	nemo_config_set_boolean (group, NEMO_FILE_CACHE_SAVE_CHECKSUM, TRUE);
	check (!copy_and_keep ("sum-off.bin", "sum-off-copy.bin", want));
	pf = path_of ("sum-off.bin");
	pt = path_of ("sum-off-copy.bin");
	src = g_file_new_for_path (pf);
	dest = g_file_new_for_path (pt);
	check (!cached_as (src, want));
	check (!cached_as (dest, want));
	check (!on_file_as (src, want));
	g_clear_object (&src);
	g_clear_object (&dest);
	g_clear_pointer (&pf, g_free);
	g_clear_pointer (&pt, g_free);

	/* The default, with nothing written onto the files. */
	nemo_config_reset (group, NEMO_FILE_CACHE_CHECKSUM_FULL_READS);
	nemo_config_reset (group, NEMO_FILE_CACHE_SAVE_CHECKSUM);
	check (copy_and_keep ("sum-cache.bin", "sum-cache-copy.bin", want));
	pf = path_of ("sum-cache.bin");
	pt = path_of ("sum-cache-copy.bin");
	src = g_file_new_for_path (pf);
	dest = g_file_new_for_path (pt);
	check (cached_as (src, want));
	check (cached_as (dest, want));
	check (!on_file_as (src, want));
	check (!on_file_as (dest, want));
	g_clear_object (&src);
	g_clear_object (&dest);
	g_clear_pointer (&pf, g_free);
	g_clear_pointer (&pt, g_free);

	nemo_config_set_boolean (group, NEMO_FILE_CACHE_SAVE_CHECKSUM, TRUE);
	check (copy_and_keep ("sum-save.bin", "sum-save-copy.bin", want));
	pf = path_of ("sum-save.bin");
	pt = path_of ("sum-save-copy.bin");
	src = g_file_new_for_path (pf);
	dest = g_file_new_for_path (pt);
	check (cached_as (src, want));
	check (cached_as (dest, want));
	if (xattrs) {
		check (on_file_as (src, want));
		check (on_file_as (dest, want));
	}
	nemo_config_reset (group, NEMO_FILE_CACHE_SAVE_CHECKSUM);
}

/* On an external drive the checksum still goes in the cache, and onto the files only with save-checksum-external on too. The scratch folder's device is made to read as a USB disk. */
static void
check_checksum_external (void)
{
	NemoConfigGroup *group = nemo_config_get_group (NEMO_FILE_CACHE_GROUP);
	gboolean xattrs = xattrs_here ();
	guint8 want[NEMO_CACHE_DIGEST_LEN];
	g_autofree char *sysfs = path_of ("sys");
	g_autofree char *block = g_build_filename (sysfs, "dev", "block", NULL);
	g_autofree char *usb = g_build_filename (sysfs, "devices", "pci0000:00/0000:00:14.0/usb2/2-1/2-1:1.0/host6/target6:0:0/6:0:0:0/block/sdb", NULL);
	g_autofree char *link = NULL;
	g_autofree char *pf = NULL, *pt = NULL;
	g_autoptr (GFile) src = NULL;
	g_autoptr (GFile) dest = NULL;
	GStatBuf st;

	check (g_stat (scratch, &st) == 0);
	check (g_mkdir_with_parents (block, 0700) == 0);
	check (g_mkdir_with_parents (usb, 0700) == 0);
	link = g_strdup_printf ("%s/%u:%u", block, major (st.st_dev), minor (st.st_dev));
	check (symlink (usb, link) == 0);
	nemo_external_drive_set_sysfs_for_test (sysfs);

	write_sized ("ext-off.bin", 4000);
	write_sized ("ext-on.bin", 4500);
	nemo_config_set_boolean (group, NEMO_FILE_CACHE_SAVE_CHECKSUM, TRUE);

	check (copy_and_keep ("ext-off.bin", "ext-off-copy.bin", want));
	pf = path_of ("ext-off.bin");
	pt = path_of ("ext-off-copy.bin");
	src = g_file_new_for_path (pf);
	dest = g_file_new_for_path (pt);
	check (cached_as (src, want));
	check (cached_as (dest, want));
	check (!on_file_as (src, want));
	check (!on_file_as (dest, want));
	g_clear_object (&src);
	g_clear_object (&dest);
	g_clear_pointer (&pf, g_free);
	g_clear_pointer (&pt, g_free);

	nemo_config_set_boolean (group, NEMO_FILE_CACHE_SAVE_CHECKSUM_EXTERNAL, TRUE);
	check (copy_and_keep ("ext-on.bin", "ext-on-copy.bin", want));
	pf = path_of ("ext-on.bin");
	pt = path_of ("ext-on-copy.bin");
	src = g_file_new_for_path (pf);
	dest = g_file_new_for_path (pt);
	check (cached_as (src, want));
	check (cached_as (dest, want));
	if (xattrs) {
		check (on_file_as (src, want));
		check (on_file_as (dest, want));
	}

	nemo_config_reset (group, NEMO_FILE_CACHE_SAVE_CHECKSUM_EXTERNAL);
	nemo_config_reset (group, NEMO_FILE_CACHE_SAVE_CHECKSUM);
	nemo_external_drive_set_sysfs_for_test (NULL);
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

	/* At the limit isn't under it. */
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

#ifdef __linux__
	check_checksum_kept ();
	check_checksum_external ();
	check_checksums_batched ();
#endif

	g_free (scratch);
	g_free (home);

	if (failures == 0) {
		g_print ("nemo-small-copy: all checks passed\n");
	}

	return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
