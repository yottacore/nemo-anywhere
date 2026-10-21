/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */

/* test-nemo-drives-win32.c - a mapped drive is named, drawn and passed over without asking it.

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

/* A free drive letter is pointed at a share for the length of the run, the
 * way a mapped drive's entry reads. The list of mounts the side pane builds
 * from, and the trash state, must not wait on it. Set NEMO_PROBE_DEAD_SHARE
 * to \\<unused address on the local subnet>\share to time it against one that
 * really does not answer; without it an address in TEST-NET-1 is used, which
 * fails fast, so the timing proves little and the rest still holds.
 *
 * Run with "glib" to put GLib's own mount list through the same checks. With a
 * dead share that is the one that waits.
 *
 * Run with "names" to see that listing the drives asks the shell nothing until
 * a name is wanted.
 *
 * The trash state reads a removable drive's bin only with a disk in it. */

#include <config.h>

#include <stdlib.h>
#include <string.h>
#include <gio/gio.h>
#include <gtk/gtk.h>

#include <libnemo-private/nemo-drives-win32.h>
#include <libnemo-private/nemo-file-utilities.h>
#include <libnemo-private/nemo-trash-win32.h>

#include "test-check.h"

#include <windows.h>
#include <shlobj.h>

static wchar_t mapped[3] = { 0, L':', 0 };
static wchar_t *target;

static gboolean
map_free_letter (const char *share)
{
	DWORD used = GetLogicalDrives ();
	int bit;

	target = g_utf8_to_utf16 (share, -1, NULL, NULL, NULL);
	for (bit = 'Q' - 'A'; bit < 26; bit++) {
		if (used & (1u << bit)) {
			continue;
		}
		mapped[0] = (wchar_t) (L'A' + bit);
		if (DefineDosDeviceW (0, mapped, target)) {
			return TRUE;
		}
	}
	return FALSE;
}

static void
unmap (void)
{
	DefineDosDeviceW (DDD_REMOVE_DEFINITION | DDD_EXACT_MATCH_ON_REMOVE, mapped, target);
}

static double
seconds_since (gint64 started)
{
	return (g_get_monotonic_time () - started) / 1000000.0;
}

static gboolean
icon_has (GIcon *icon, const char *name)
{
	const char * const *names;

	if (icon == NULL || !G_IS_THEMED_ICON (icon)) {
		return FALSE;
	}
	names = g_themed_icon_get_names (G_THEMED_ICON (icon));
	return names != NULL && g_strv_contains (names, name);
}

typedef HRESULT (WINAPI *DesktopFolderFn) (IShellFolder **);

static DesktopFolderFn real_desktop_folder;
static guint shell_asks;

static HRESULT WINAPI
counted_desktop_folder (IShellFolder **folder)
{
	shell_asks++;
	return real_desktop_folder (folder);
}

/* Points this program's own import of SHGetDesktopFolder, the way into the
   shell's namespace, at a counter. The app's code is linked in, so its calls
   go through here. */
static gboolean
count_shell_asks (void)
{
	BYTE *base = (BYTE *) GetModuleHandleW (NULL);
	IMAGE_NT_HEADERS *nt = (IMAGE_NT_HEADERS *) (base + ((IMAGE_DOS_HEADER *) base)->e_lfanew);
	IMAGE_DATA_DIRECTORY *dir = &nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
	IMAGE_IMPORT_DESCRIPTOR *import;
	FARPROC real = GetProcAddress (GetModuleHandleW (L"shell32.dll"), "SHGetDesktopFolder");
	gboolean hooked = FALSE;

	if (real == NULL || dir->VirtualAddress == 0) {
		return FALSE;
	}
	real_desktop_folder = (DesktopFolderFn) (void *) real;
	for (import = (IMAGE_IMPORT_DESCRIPTOR *) (base + dir->VirtualAddress); import->Name != 0; import++) {
		IMAGE_THUNK_DATA *thunk;

		for (thunk = (IMAGE_THUNK_DATA *) (base + import->FirstThunk); thunk->u1.Function != 0; thunk++) {
			DWORD was;

			if (thunk->u1.Function != (ULONG_PTR) real ||
			    !VirtualProtect (&thunk->u1.Function, sizeof thunk->u1.Function, PAGE_READWRITE, &was)) {
				continue;
			}
			thunk->u1.Function = (ULONG_PTR) counted_desktop_folder;
			VirtualProtect (&thunk->u1.Function, sizeof thunk->u1.Function, was, &was);
			hooked = TRUE;
		}
	}
	return hooked;
}

/* 2026101913541101: a bookmark's exists check lists the drives for each one
   it asks about, and only wants their roots. Naming a local drive goes through
   the shell, and that only when the name is asked for. */
static int
check_names_on_ask (void)
{
	g_autoptr (GFile) temp = g_file_new_for_path (g_get_tmp_dir ());
	GVolumeMonitor *monitor = g_volume_monitor_get ();
	char letter = g_ascii_toupper (g_get_tmp_dir ()[0]);
	GList *mounts, *l;
	GMount *found = NULL;

	if (!count_shell_asks ()) {
		g_print ("SKIP: the shell's namespace call can't be counted here\n");
		g_object_unref (monitor);
		return 77;
	}

	mounts = nemo_get_mounts (monitor);
	g_print ("  listing the drives asked the shell %u time(s)\n", shell_asks);
	check (shell_asks == 0);

	check (!nemo_location_is_network_safe (temp));
	g_print ("  the network check on %s asked it %u time(s)\n", g_get_tmp_dir (), shell_asks);
	check (shell_asks == 0);

	for (l = mounts; l != NULL; l = l->next) {
		g_autoptr (GFile) root = g_mount_get_root (l->data);
		g_autofree char *path = g_file_get_path (root);

		if (path != NULL && g_ascii_toupper (path[0]) == letter) {
			found = l->data;
		}
	}
	check (found != NULL);
	if (found != NULL && nemo_drive_win32_kind (letter) != NEMO_DRIVE_WIN32_REMOTE) {
		g_autofree char *name = g_mount_get_name (found);
		g_autofree char *again = g_mount_get_name (found);

		g_print ("  %c: is named '%s', after %u ask(s)\n", letter, name, shell_asks);
		check (shell_asks == 1);
		check (g_strcmp0 (name, again) == 0 && shell_asks == 1);
	}

	g_list_free_full (mounts, g_object_unref);
	g_object_unref (monitor);
	return 0;
}

static void
check_mounts (gboolean glib, char letter, gboolean slow_share)
{
	GVolumeMonitor *monitor = g_volume_monitor_get ();
	GList *mounts, *l;
	GMount *found = NULL;
	gint64 started;
	double seconds;
	char root[4] = { letter, ':', '\\', '\0' };

	started = g_get_monotonic_time ();
	mounts = glib ? g_volume_monitor_get_mounts (monitor) : nemo_get_mounts (monitor);
	for (l = mounts; l != NULL; l = l->next) {
		g_autoptr (GFile) mount_root = g_mount_get_root (l->data);
		g_autofree char *path = g_file_get_path (mount_root);

		if (g_ascii_strcasecmp (path, root) == 0) {
			found = l->data;
		}
	}
	check (found != NULL);
	if (found != NULL) {
		g_autofree char *name = g_mount_get_name (found);
		g_autoptr (GIcon) icon = g_mount_get_icon (found);
		g_autoptr (GIcon) symbolic = g_mount_get_symbolic_icon (found);
		char tail[] = { '(', letter, ':', ')', '\0' };

		g_print ("  %c: is named '%s'\n", letter, name);
		check (name != NULL && strstr (name, tail) != NULL);
		check (icon_has (symbolic, "folder-remote-symbolic"));
		/* GLib's own comes from the shell, which is the trip. */
		if (!glib) {
			check (icon_has (icon, "folder-remote"));
		}
	}
	seconds = seconds_since (started);
	g_print ("  mount list, names and icons took %.3fs\n", seconds);
	if (slow_share) {
		check (seconds < 2.0);
	}

	g_list_free_full (mounts, g_object_unref);
	g_object_unref (monitor);
}

static void
check_trash_state (gboolean slow_share)
{
	g_autoptr (GFile) root = g_file_new_for_uri ("trash:///");
	g_autoptr (GFileInfo) info = NULL;
	gint64 started = g_get_monotonic_time ();
	double seconds;

	info = g_file_query_info (root, G_FILE_ATTRIBUTE_TRASH_ITEM_COUNT, 0, NULL, NULL);
	seconds = seconds_since (started);
	g_print ("  trash state took %.3fs\n", seconds);
	check (info != NULL);
	if (slow_share) {
		check (seconds < 2.0);
	}
}

/* A fixed drive's bin is asked about, and a removable one's only with a disk
 * in it. An empty drive says so at once, with no insert-disk prompt; an empty
 * optical drive stands in for an empty card reader. A share is never asked,
 * not even whether it has a disk. */
static void
check_bins (char mapped_letter)
{
	DWORD drives = GetLogicalDrives ();
	gboolean removable_seen = FALSE, empty_seen = FALSE;
	int bit;

	check (!nemo_drive_win32_bin_askable (mapped_letter));

	for (bit = 0; bit < 26; bit++) {
		char letter = (char) ('A' + bit);
		NemoDriveWin32Kind kind;
		gboolean media, askable;
		gint64 started;
		double seconds;

		if (!(drives & (1u << bit)) || letter == mapped_letter) {
			continue;
		}
		kind = nemo_drive_win32_kind (letter);
		if (kind == NEMO_DRIVE_WIN32_REMOTE) {
			check (!nemo_drive_win32_bin_askable (letter));
			continue;
		}

		started = g_get_monotonic_time ();
		media = nemo_drive_win32_has_media (letter);
		seconds = seconds_since (started);
		askable = nemo_drive_win32_bin_askable (letter);
		g_print ("  %c: kind %d, %s, bin %s, %.3fs\n", letter, kind,
			 media ? "disk in" : "empty", askable ? "asked" : "not asked", seconds);

		if (kind == NEMO_DRIVE_WIN32_FIXED) {
			check (media && askable);
		} else if (kind == NEMO_DRIVE_WIN32_REMOVABLE) {
			removable_seen = TRUE;
			check (askable == media);
		} else {
			check (!askable);
		}
		if (!media) {
			empty_seen = TRUE;
			check (seconds < 2.0);
		}
	}

	if (!removable_seen) {
		g_print ("  no removable drive here\n");
	}
	if (!empty_seen) {
		g_print ("  no empty drive here\n");
	}
}

int
main (int argc, char *argv[])
{
	const char *dead = g_getenv ("NEMO_PROBE_DEAD_SHARE");
	gboolean glib = argc > 1 && strcmp (argv[1], "glib") == 0;
	const char *share = dead != NULL ? dead : "\\\\192.0.2.1\\share";
	g_autofree char *path = NULL;
	char letter;

	gtk_init_check (&argc, &argv);
	nemo_trash_win32_register ();

	if (argc > 1 && strcmp (argv[1], "names") == 0) {
		int skipped = check_names_on_ask ();

		if (failures > 0) {
			g_printerr ("%d check(s) failed\n", failures);
			return EXIT_FAILURE;
		}
		if (skipped == 0) {
			g_print ("OK\n");
		}
		return skipped;
	}

	if (!map_free_letter (share)) {
		g_print ("SKIP: no free drive letter to map\n");
		return 77;
	}
	letter = (char) mapped[0];
	g_print ("  %c: -> %s\n", letter, share);

	check (nemo_drive_win32_kind (letter) == NEMO_DRIVE_WIN32_REMOTE);
	check (nemo_drive_win32_kind ('C') != NEMO_DRIVE_WIN32_REMOTE);

	path = nemo_drive_win32_remote_path (letter);
	g_print ("  remote path '%s'\n", path != NULL ? path : "(null)");
	check (path != NULL && g_ascii_strcasecmp (path, share) == 0);

	check_mounts (glib, letter, dead != NULL);
	if (!glib) {
		check_bins (letter);
		check_trash_state (dead != NULL);
	}

	unmap ();
	g_free (target);

	if (failures > 0) {
		g_printerr ("%d check(s) failed\n", failures);
		return EXIT_FAILURE;
	}
	g_print ("OK\n");
	return EXIT_SUCCESS;
}
