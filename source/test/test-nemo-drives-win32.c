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
 * dead share that is the one that waits. */

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
