/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */

/* nemo-external-drive.c - whether a file is on a drive that plugs in.

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

#include "nemo-external-drive.h"

#include <string.h>
#include <glib/gstdio.h>

#ifdef G_OS_WIN32
#include <windows.h>
#include <winioctl.h>
#elif defined(__linux__)
#include <stdlib.h>
#include <sys/stat.h>
#include <sys/sysmacros.h>
#else
#include <sys/stat.h>
#endif

#ifdef G_OS_WIN32

/* A USB disk is a fixed drive to GetDriveType, so the bus is asked as well. The volume is opened with no access at all, which needs no rights and never spins anything up. */
static gboolean
on_external_bus (const wchar_t *root)
{
	wchar_t volume[MAX_PATH];
	size_t len;
	HANDLE handle;
	STORAGE_PROPERTY_QUERY query = { StorageDeviceProperty, PropertyStandardQuery, { 0 } };
	union {
		STORAGE_DEVICE_DESCRIPTOR desc;
		BYTE bytes[1024];
	} out;
	DWORD got = 0;
	BOOL ok;

	if (!GetVolumeNameForVolumeMountPointW (root, volume, G_N_ELEMENTS (volume))) {
		return FALSE;
	}
	/* CreateFile opens the volume itself only without the trailing backslash. */
	len = wcslen (volume);
	if (len > 0 && volume[len - 1] == L'\\') {
		volume[len - 1] = L'\0';
	}

	handle = CreateFileW (volume, 0, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
	if (handle == INVALID_HANDLE_VALUE) {
		return FALSE;
	}
	memset (&out, 0, sizeof (out));
	ok = DeviceIoControl (handle, IOCTL_STORAGE_QUERY_PROPERTY, &query, sizeof (query), &out, sizeof (out), &got, NULL);
	CloseHandle (handle);
	if (!ok || got < offsetof (STORAGE_DEVICE_DESCRIPTOR, BusType) + sizeof (out.desc.BusType)) {
		return FALSE;
	}

	switch (out.desc.BusType) {
	case BusTypeUsb:
	case BusType1394:
	case BusTypeSd:
	case BusTypeMmc:
		return TRUE;
	default:
		return out.desc.RemovableMedia != FALSE;
	}
}

static gboolean
path_is_external (const char *path)
{
	g_autofree wchar_t *wpath = (wchar_t *) g_utf8_to_utf16 (path, -1, NULL, NULL, NULL);
	wchar_t root[MAX_PATH + 1];
	DWORD old_mode = 0;
	gboolean external = FALSE;

	if (wpath == NULL) {
		return FALSE;
	}

	/* An empty card reader must not bring up the insert-disk prompt. */
	SetThreadErrorMode (SEM_FAILCRITICALERRORS | SEM_NOOPENFILEERRORBOX, &old_mode);
	if (GetVolumePathNameW (wpath, root, G_N_ELEMENTS (root))) {
		switch (GetDriveTypeW (root)) {
		case DRIVE_REMOVABLE:
			external = TRUE;
			break;
		case DRIVE_FIXED:
			external = on_external_bus (root);
			break;
		default:
			break;
		}
	}
	SetThreadErrorMode (old_mode, NULL);

	return external;
}

void
nemo_external_drive_set_sysfs_for_test (const char *root)
{
	(void) root;
}

#elif defined(__linux__)

static char *sysfs_root;

void
nemo_external_drive_set_sysfs_for_test (const char *root)
{
	g_free (sysfs_root);
	sysfs_root = g_strdup (root);
}

static gboolean
file_says (const char *dir, const char *name, const char *want)
{
	g_autofree char *path = g_build_filename (dir, name, NULL);
	g_autofree char *text = NULL;

	if (!g_file_get_contents (path, &text, NULL, NULL)) {
		return FALSE;
	}
	g_strstrip (text);

	return strcmp (text, want) == 0;
}

/* A FireWire disk sits under a node named fw and a number, such as fw1/fw1.0. */
static gboolean
has_firewire_node (const char *path)
{
	const char *at;

	for (at = strstr (path, "/fw"); at != NULL; at = strstr (at + 1, "/fw")) {
		if (g_ascii_isdigit (at[3])) {
			return TRUE;
		}
	}

	return FALSE;
}

/* A block device's folder under devices/ names the buses on its way to the CPU, which is how udisks tells a USB disk from a SATA one too. A disk with removable set, such as a card reader's, counts as well. An SD card says SD as its type, where a soldered-in eMMC says MMC. A device mapper or RAID device, such as an encrypted volume, is external when any disk under it is. */
static gboolean
block_dir_is_external (const char *dir, int depth)
{
	g_autofree char *real = realpath (dir, NULL);
	g_autofree char *disk = NULL;
	g_autofree char *slaves = NULL;
	const char *after;
	GDir *entries;
	const char *name;
	gboolean external = FALSE;

	if (real == NULL || depth > 4) {
		return FALSE;
	}
	after = strstr (real, "/devices/");
	if (after == NULL) {
		return FALSE;
	}
	if (strstr (after, "/usb") != NULL || strstr (after, "/memstick") != NULL || has_firewire_node (after)) {
		return TRUE;
	}

	{
		g_autofree char *partition = g_build_filename (real, "partition", NULL);

		disk = g_file_test (partition, G_FILE_TEST_EXISTS) ? g_path_get_dirname (real) : g_strdup (real);
	}
	if (file_says (disk, "removable", "1") || file_says (disk, "device/type", "SD")) {
		return TRUE;
	}

	slaves = g_build_filename (disk, "slaves", NULL);
	entries = g_dir_open (slaves, 0, NULL);
	if (entries == NULL) {
		return FALSE;
	}
	while (!external && (name = g_dir_read_name (entries)) != NULL) {
		g_autofree char *slave = g_build_filename (slaves, name, NULL);

		external = block_dir_is_external (slave, depth + 1);
	}
	g_dir_close (entries);

	return external;
}

static gboolean
path_is_external (const char *path)
{
	GStatBuf st;
	g_autofree char *dev = NULL;

	if (g_stat (path, &st) != 0) {
		return FALSE;
	}
	/* A share or a ZFS dataset has a device number with no block device behind it, so nothing is found. */
	dev = g_strdup_printf ("%s/dev/block/%u:%u", sysfs_root != NULL ? sysfs_root : "/sys",
			       major (st.st_dev), minor (st.st_dev));

	return block_dir_is_external (dev, 0);
}

#else

void
nemo_external_drive_set_sysfs_for_test (const char *root)
{
	(void) root;
}

/* No sysfs here, and the disk arbitration and geom calls are more than this needs. A drive that plugs in is mounted under one of these by the system's own automounter, so a file there on a file system other than the root's counts. */
static gboolean
path_is_external (const char *path)
{
	static const char *const under[] = { "/Volumes/", "/media/", "/run/media/" };
	GStatBuf st, root_st;
	gboolean below = FALSE;
	guint i;

	for (i = 0; i < G_N_ELEMENTS (under); i++) {
		below = below || g_str_has_prefix (path, under[i]);
	}
	if (!below || g_stat (path, &st) != 0 || g_stat ("/", &root_st) != 0) {
		return FALSE;
	}

	return st.st_dev != root_st.st_dev;
}

#endif

gboolean
nemo_file_is_on_external_drive (GFile *file)
{
	const char *path;

	g_return_val_if_fail (G_IS_FILE (file), FALSE);

	path = g_file_peek_path (file);
	if (path == NULL) {
		return FALSE;
	}

	return path_is_external (path);
}
