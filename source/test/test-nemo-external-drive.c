/* Telling a file on a drive that plugs in from one on a fixed disk, and the setting that keeps checksums off the first kind.
 *
 * On Linux the block device folders come from a made-up sysfs, laid out as the kernel lays them out, with the scratch file's own device number pointing into it. Elsewhere only a scratch file on a fixed disk is checked, which must not count.
 *
 * NEMO_TEST_EXTERNAL_DIR names a folder on a real external drive, such as a USB disk on a Windows test box, and then the real calls have to say so. */

#include <config.h>

#include <stdlib.h>
#include <string.h>
#include <glib.h>
#include <glib/gstdio.h>
#include <gtk/gtk.h>

#include <libnemo-private/nemo-config.h>
#include <libnemo-private/nemo-external-drive.h>
#include <libnemo-private/nemo-global-preferences.h>
#include <libnemo-private/nemo-thumbnails.h>

#include "test-scratch.h"
#include "test-check.h"

#ifdef __linux__
#include <unistd.h>
#include <sys/stat.h>
#include <sys/sysmacros.h>

static char *sysfs;
static char *dev_link;

static void
put_text (const char *dir, const char *name, const char *text)
{
	g_autofree char *path = g_build_filename (dir, name, NULL);
	g_autofree char *parent = g_path_get_dirname (path);

	check (g_mkdir_with_parents (parent, 0700) == 0);
	check (g_file_set_contents (path, text, -1, NULL));
}

/* Points the scratch file's device at devices/<where>, made if it isn't there. Returns that folder. */
static char *
device_at (const char *where)
{
	char *dir = g_build_filename (sysfs, "devices", where, NULL);

	check (g_mkdir_with_parents (dir, 0700) == 0);
	g_unlink (dev_link);
	check (symlink (dir, dev_link) == 0);

	return dir;
}

static void
check_sysfs (GFile *file)
{
	GStatBuf st;
	g_autofree char *block = g_build_filename (sysfs, "dev", "block", NULL);
	g_autofree char *dir = NULL;
	g_autofree char *usb = NULL;
	g_autofree char *sata = NULL;
	g_autofree char *disk = NULL;

	check (g_stat (g_file_peek_path (file), &st) == 0);
	check (g_mkdir_with_parents (block, 0700) == 0);
	dev_link = g_strdup_printf ("%s/%u:%u", block, major (st.st_dev), minor (st.st_dev));
	nemo_external_drive_set_sysfs_for_test (sysfs);

	/* No block device behind it, as for a share or ZFS. */
	check (!nemo_file_is_on_external_drive (file));

	g_free (device_at ("pci0000:00/0000:00:17.0/ata1/host0/target0:0:0/0:0:0:0/block/sda/sda2"));
	check (!nemo_file_is_on_external_drive (file));

	usb = device_at ("pci0000:00/0000:00:14.0/usb2/2-1/2-1:1.0/host6/target6:0:0/6:0:0:0/block/sdb/sdb1");
	put_text (usb, "partition", "1\n");
	check (nemo_file_is_on_external_drive (file));

	/* A card reader on the board, with removable set on the disk rather than the partition. */
	dir = device_at ("pci0000:00/0000:00:1d.0/ata3/host2/target2:0:0/2:0:0:0/block/sdc/sdc1");
	put_text (dir, "partition", "1\n");
	disk = g_path_get_dirname (dir);
	put_text (disk, "removable", "0\n");
	check (!nemo_file_is_on_external_drive (file));
	put_text (disk, "removable", "1\n");
	check (nemo_file_is_on_external_drive (file));
	g_clear_pointer (&dir, g_free);

	/* An SD card says SD, a soldered-in eMMC says MMC. */
	dir = device_at ("platform/fe320000.mmc/mmc_host/mmc1/mmc1:aaaa/block/mmcblk1");
	put_text (dir, "device/type", "MMC\n");
	check (!nemo_file_is_on_external_drive (file));
	put_text (dir, "device/type", "SD\n");
	check (nemo_file_is_on_external_drive (file));
	g_clear_pointer (&dir, g_free);

	g_free (device_at ("pci0000:00/0000:00:1e.0/0000:05:00.0/fw1/fw1.0/host7/target7:0:0/7:0:0:0/block/sdd"));
	check (nemo_file_is_on_external_drive (file));

	/* An encrypted volume goes by the disk under it. */
	sata = g_build_filename (sysfs, "devices", "pci0000:00/0000:00:17.0/ata1/host0/target0:0:0/0:0:0:0/block/sda/sda2", NULL);
	dir = device_at ("virtual/block/dm-0");
	{
		g_autofree char *slaves = g_build_filename (dir, "slaves", NULL);
		g_autofree char *slave = g_build_filename (slaves, "sda2", NULL);

		check (g_mkdir_with_parents (slaves, 0700) == 0);
		check (symlink (sata, slave) == 0);
		check (!nemo_file_is_on_external_drive (file));
		g_unlink (slave);
		check (symlink (usb, slave) == 0);
		check (nemo_file_is_on_external_drive (file));
	}
	g_clear_pointer (&dir, g_free);
}
#endif

/* A file on an external drive takes a checksum only with file-cache.save-checksum-external on too. A file on a fixed disk doesn't care about that one. */
static void
check_setting (GFile *file, gboolean external)
{
	NemoConfigGroup *group = nemo_config_get_group (NEMO_FILE_CACHE_GROUP);

	check (!nemo_config_get_boolean (group, NEMO_FILE_CACHE_SAVE_CHECKSUM_EXTERNAL));
	check (!nemo_thumbnail_saves_checksum_on (file));

	nemo_config_set_boolean (group, NEMO_FILE_CACHE_SAVE_CHECKSUM, TRUE);
	check (nemo_thumbnail_saves_checksum_on (file) == !external);

	nemo_config_set_boolean (group, NEMO_FILE_CACHE_SAVE_CHECKSUM_EXTERNAL, TRUE);
	check (nemo_thumbnail_saves_checksum_on (file));

	/* Without caching on full reads nothing is written anywhere. */
	nemo_config_set_boolean (group, NEMO_FILE_CACHE_CHECKSUM_FULL_READS, FALSE);
	check (!nemo_thumbnail_saves_checksum_on (file));

	nemo_config_reset (group, NEMO_FILE_CACHE_CHECKSUM_FULL_READS);
	nemo_config_reset (group, NEMO_FILE_CACHE_SAVE_CHECKSUM);
	nemo_config_reset (group, NEMO_FILE_CACHE_SAVE_CHECKSUM_EXTERNAL);
}

int
main (int argc, char **argv)
{
	char *home;
	g_autofree char *scratch = NULL;
	g_autofree char *path = NULL;
	g_autoptr (GFile) file = NULL;
	g_autoptr (GFile) remote = NULL;

	/* Settings go to a scratch home, removed at exit. */
	home = test_scratch_config_home ("nemo-external-home-XXXXXX");
	g_free (home);
	gtk_init_check (&argc, &argv);
	nemo_global_preferences_init ();

	scratch = test_scratch_dir ("nemo-external-XXXXXX", NULL);
	if (scratch == NULL) {
		g_printerr ("FAIL could not make a temp dir\n");
		return EXIT_FAILURE;
	}
	path = g_build_filename (scratch, "file.bin", NULL);
	check (g_file_set_contents (path, "x", 1, NULL));
	file = g_file_new_for_path (path);

	if (g_getenv ("NEMO_TEST_EXTERNAL_DIR") != NULL) {
		g_autoptr (GFile) there = g_file_new_for_path (g_getenv ("NEMO_TEST_EXTERNAL_DIR"));

		g_print ("note: checking %s as external\n", g_getenv ("NEMO_TEST_EXTERNAL_DIR"));
		check (nemo_file_is_on_external_drive (there));
	}

	/* No local path, nothing to ask. */
	remote = g_file_new_for_uri ("sftp://example.invalid/file.bin");
	check (!nemo_file_is_on_external_drive (remote));

#ifdef __linux__
	sysfs = g_build_filename (scratch, "sys", NULL);
	check_sysfs (file);

	/* The scratch file reads as on the USB encrypted volume now. */
	check_setting (file, TRUE);

	g_unlink (dev_link);
	g_free (device_at ("pci0000:00/0000:00:17.0/ata1/host0/target0:0:0/0:0:0:0/block/sda/sda2"));
	check_setting (file, FALSE);
	nemo_external_drive_set_sysfs_for_test (NULL);
	g_free (dev_link);
	g_free (sysfs);
#else
	check (!nemo_file_is_on_external_drive (file));
	check_setting (file, FALSE);
#endif

	if (failures > 0) {
		g_printerr ("%d check(s) failed\n", failures);
		return EXIT_FAILURE;
	}

	g_print ("OK\n");
	return EXIT_SUCCESS;
}
