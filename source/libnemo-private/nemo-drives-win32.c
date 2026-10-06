/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */

/* nemo-drives-win32.c - drive letters, named and drawn without asking a share.

   Copyright © 2026 t00mietum (CryptogID: ปʬϝღถɔ4რఠΔթะ9ƾǝu).

   This program is free software; you can redistribute it and/or
   modify it under the terms of the GNU General Public License as
   published by the Free Software Foundation; version 2 of the
   License.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, see <http://www.gnu.org/licenses/>.
*/

#include <config.h>
#include <glib.h>

#ifdef G_OS_WIN32

#include "nemo-drives-win32.h"
#include "nemo-link-win32.h"

#include <string.h>
#include <glib/gi18n.h>

#define COBJMACROS
#include <windows.h>
#include <shlobj.h>
#include <shlwapi.h>

NemoDriveWin32Kind
nemo_drive_win32_kind (char letter)
{
	char root[4] = { letter, ':', '\\', '\0' };
	wchar_t wroot[4] = { (wchar_t) letter, L':', L'\\', L'\0' };

	if (!g_ascii_isalpha (letter)) {
		return NEMO_DRIVE_WIN32_NONE;
	}
	if (nemo_win32_drive_is_remote (root)) {
		return NEMO_DRIVE_WIN32_REMOTE;
	}

	switch (GetDriveTypeW (wroot)) {
	case DRIVE_FIXED:
	case DRIVE_RAMDISK:
		return NEMO_DRIVE_WIN32_FIXED;
	case DRIVE_REMOVABLE:
		return NEMO_DRIVE_WIN32_REMOVABLE;
	case DRIVE_CDROM:
		return NEMO_DRIVE_WIN32_OPTICAL;
	case DRIVE_REMOTE:
		return NEMO_DRIVE_WIN32_REMOTE;
	case DRIVE_NO_ROOT_DIR:
		return NEMO_DRIVE_WIN32_NONE;
	default:
		return NEMO_DRIVE_WIN32_OTHER;
	}
}

static char *
device_of (char letter)
{
	wchar_t drive[3] = { (wchar_t) g_ascii_toupper (letter), L':', L'\0' };
	wchar_t device[1024];

	if (QueryDosDeviceW (drive, device, G_N_ELEMENTS (device)) == 0) {
		return NULL;
	}
	return g_utf16_to_utf8 ((const gunichar2 *) device, -1, NULL, NULL, NULL);
}

/* Entries look like \Device\LanmanRedirector\;Z:0000000000012345\server\share,
   \Device\Mup\;WebDavRedirector\;Z:...\server@SSL\DavWWWRoot, or for a subst
   onto a share \??\UNC\server\share\folder. */
static char *
remote_path (char letter, int depth)
{
	g_autofree char *device = device_of (letter);
	const char *rest = NULL;

	if (device == NULL || depth > 4) {
		return NULL;
	}

	if (g_ascii_strncasecmp (device, "\\??\\UNC\\", 8) == 0) {
		rest = device + 8;
	} else if (strncmp (device, "\\??\\", 4) == 0) {
		g_autofree char *base = NULL;

		if (!g_ascii_isalpha (device[4]) || device[5] != ':') {
			return NULL;
		}
		base = remote_path (device[4], depth + 1);
		return base != NULL ? g_strconcat (base, device + 6, NULL) : NULL;
	} else {
		const char *mark = strrchr (device, ';');

		if (mark != NULL) {
			rest = strchr (mark, '\\');
			rest = rest != NULL ? rest + 1 : NULL;
		}
	}

	if (rest == NULL || *rest == '\0') {
		return NULL;
	}
	return g_strconcat ("\\\\", rest, NULL);
}

/* Returns: (transfer full): free with g_free */
char *
nemo_drive_win32_remote_path (char letter)
{
	if (!g_ascii_isalpha (letter)) {
		return NULL;
	}
	return remote_path (letter, 0);
}

/* The way Explorer names one, "share (\\server) (Z:)". */
static char *
remote_name (char letter)
{
	g_autofree char *path = nemo_drive_win32_remote_path (letter);
	g_auto (GStrv) parts = NULL;
	guint count;

	letter = g_ascii_toupper (letter);
	if (path == NULL) {
		return g_strdup_printf (_("Network drive (%c:)"), letter);
	}

	parts = g_strsplit (path + 2, "\\", -1);
	count = g_strv_length (parts);
	if (count < 2 || *parts[count - 1] == '\0') {
		return g_strdup_printf ("%s (%c:)", path, letter);
	}
	return g_strdup_printf ("%s (\\\\%s) (%c:)", parts[count - 1], parts[0], letter);
}

/* The shell's own name for a local drive, as GLib asks for it. */
static char *
local_name (char letter)
{
	wchar_t wroot[4] = { (wchar_t) letter, L':', L'\\', L'\0' };
	IShellFolder *desktop = NULL;
	PIDLIST_RELATIVE item = NULL;
	STRRET text;
	wchar_t *wide = NULL;
	char *name = NULL;

	if (FAILED (SHGetDesktopFolder (&desktop))) {
		return g_strdup_printf ("%c:\\", letter);
	}
	if (SUCCEEDED (IShellFolder_ParseDisplayName (desktop, NULL, NULL, wroot, NULL, &item, NULL))) {
		text.uType = STRRET_WSTR;
		if (SUCCEEDED (IShellFolder_GetDisplayNameOf (desktop, item, SHGDN_FORADDRESSBAR, &text)) &&
		    SUCCEEDED (StrRetToStrW (&text, item, &wide))) {
			name = g_utf16_to_utf8 ((const gunichar2 *) wide, -1, NULL, NULL, NULL);
			CoTaskMemFree (wide);
		}
		CoTaskMemFree (item);
	}
	IShellFolder_Release (desktop);

	return name != NULL ? name : g_strdup_printf ("%c:\\", letter);
}

static const char *
kind_icon (NemoDriveWin32Kind kind)
{
	switch (kind) {
	case NEMO_DRIVE_WIN32_FIXED:
		return "drive-harddisk";
	case NEMO_DRIVE_WIN32_REMOVABLE:
		return "drive-removable-media";
	case NEMO_DRIVE_WIN32_OPTICAL:
		return "drive-optical";
	case NEMO_DRIVE_WIN32_REMOTE:
		return "folder-remote";
	case NEMO_DRIVE_WIN32_NONE:
	case NEMO_DRIVE_WIN32_OTHER:
	default:
		return "folder";
	}
}

#define NEMO_TYPE_DRIVE_MOUNT (nemo_drive_mount_get_type ())
G_DECLARE_FINAL_TYPE (NemoDriveMount, nemo_drive_mount, NEMO, DRIVE_MOUNT, GObject)

struct _NemoDriveMount {
	GObject parent_instance;

	char root[4];
	NemoDriveWin32Kind kind;
	char *name;
};

static void nemo_drive_mount_iface_init (GMountIface *iface);

G_DEFINE_TYPE_WITH_CODE (NemoDriveMount, nemo_drive_mount, G_TYPE_OBJECT,
			 G_IMPLEMENT_INTERFACE (G_TYPE_MOUNT, nemo_drive_mount_iface_init))

static GFile *
drive_mount_get_root (GMount *mount)
{
	return g_file_new_for_path (NEMO_DRIVE_MOUNT (mount)->root);
}

static char *
drive_mount_get_name (GMount *mount)
{
	return g_strdup (NEMO_DRIVE_MOUNT (mount)->name);
}

static GIcon *
drive_mount_get_icon (GMount *mount)
{
	return g_themed_icon_new_with_default_fallbacks (kind_icon (NEMO_DRIVE_MOUNT (mount)->kind));
}

static GIcon *
drive_mount_get_symbolic_icon (GMount *mount)
{
	g_autofree char *name = g_strconcat (kind_icon (NEMO_DRIVE_MOUNT (mount)->kind), "-symbolic", NULL);

	return g_themed_icon_new_with_default_fallbacks (name);
}

static char *
drive_mount_get_uuid (G_GNUC_UNUSED GMount *mount)
{
	return NULL;
}

static GVolume *
drive_mount_get_volume (G_GNUC_UNUSED GMount *mount)
{
	return NULL;
}

static GDrive *
drive_mount_get_drive (G_GNUC_UNUSED GMount *mount)
{
	return NULL;
}

static gboolean
drive_mount_can_nothing (G_GNUC_UNUSED GMount *mount)
{
	return FALSE;
}

/* A drive letter is not ours to unmount or eject, as with GLib's own. */
static void
drive_mount_refuse (GMount                           *mount,
		    G_GNUC_UNUSED GMountUnmountFlags  flags,
		    G_GNUC_UNUSED GCancellable       *cancellable,
		    GAsyncReadyCallback               callback,
		    gpointer                          user_data)
{
	g_task_report_new_error (mount, callback, user_data, drive_mount_refuse,
				 G_IO_ERROR, G_IO_ERROR_NOT_SUPPORTED,
				 _("Operation not supported"));
}

static gboolean
drive_mount_refuse_finish (G_GNUC_UNUSED GMount *mount,
			   GAsyncResult  *result,
			   GError       **error)
{
	return g_task_propagate_boolean (G_TASK (result), error);
}

static void
drive_mount_finalize (GObject *object)
{
	g_free (NEMO_DRIVE_MOUNT (object)->name);

	G_OBJECT_CLASS (nemo_drive_mount_parent_class)->finalize (object);
}

static void
nemo_drive_mount_init (G_GNUC_UNUSED NemoDriveMount *self)
{
}

static void
nemo_drive_mount_class_init (NemoDriveMountClass *klass)
{
	G_OBJECT_CLASS (klass)->finalize = drive_mount_finalize;
}

static void
nemo_drive_mount_iface_init (GMountIface *iface)
{
	iface->get_root = drive_mount_get_root;
	iface->get_name = drive_mount_get_name;
	iface->get_icon = drive_mount_get_icon;
	iface->get_symbolic_icon = drive_mount_get_symbolic_icon;
	iface->get_uuid = drive_mount_get_uuid;
	iface->get_volume = drive_mount_get_volume;
	iface->get_drive = drive_mount_get_drive;
	iface->can_unmount = drive_mount_can_nothing;
	iface->can_eject = drive_mount_can_nothing;
	iface->unmount = drive_mount_refuse;
	iface->unmount_finish = drive_mount_refuse_finish;
	iface->eject = drive_mount_refuse;
	iface->eject_finish = drive_mount_refuse_finish;
}

/* GetLogicalDrives less any letter the NoDrives policy hides, as GLib reads
   it: the machine's value wins over the user's. */
static DWORD
shown_drives (void)
{
	static const wchar_t policy[] = L"Software\\Microsoft\\Windows\\CurrentVersion\\Policies\\Explorer";
	HKEY roots[] = { HKEY_LOCAL_MACHINE, HKEY_CURRENT_USER };
	DWORD drives = GetLogicalDrives ();
	guint i;

	for (i = 0; i < G_N_ELEMENTS (roots); i++) {
		HKEY key;
		DWORD hidden = 0, size = sizeof hidden, type = REG_DWORD;
		gboolean found = FALSE;

		if (RegOpenKeyExW (roots[i], policy, 0, KEY_READ, &key) != ERROR_SUCCESS) {
			continue;
		}
		if (RegQueryValueExW (key, L"NoDrives", NULL, &type, (LPBYTE) &hidden, &size) == ERROR_SUCCESS &&
		    type == REG_DWORD) {
			drives &= ~hidden;
			found = TRUE;
		}
		RegCloseKey (key);
		if (found) {
			break;
		}
	}

	return drives;
}

/* Returns: (transfer full): free with g_list_free_full and g_object_unref */
GList *
nemo_drives_win32_get_mounts (void)
{
	DWORD drives = shown_drives ();
	GList *mounts = NULL;
	int bit;

	for (bit = 25; bit >= 0; bit--) {
		NemoDriveMount *mount;
		char letter = (char) ('A' + bit);

		if (!(drives & (1u << bit))) {
			continue;
		}

		mount = g_object_new (NEMO_TYPE_DRIVE_MOUNT, NULL);
		mount->root[0] = letter;
		mount->root[1] = ':';
		mount->root[2] = '\\';
		mount->root[3] = '\0';
		mount->kind = nemo_drive_win32_kind (letter);
		mount->name = mount->kind == NEMO_DRIVE_WIN32_REMOTE ? remote_name (letter) : local_name (letter);
		mounts = g_list_prepend (mounts, mount);
	}

	return mounts;
}

#endif /* G_OS_WIN32 */
