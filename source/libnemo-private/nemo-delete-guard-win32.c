/* nemo-delete-guard-win32.c - home by file id on Windows.

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

#include <glib.h>

#ifdef G_OS_WIN32

#include "nemo-delete-guard-win32.h"

#include <string.h>

#include <windows.h>

/* A folder's volume and file index are the same under every spelling of it,
   the way device and inode are on POSIX. ReFS can repeat a 64-bit index, which
   could only refuse a delete, never allow one. */
typedef struct {
	DWORD volume;
	ULONGLONG index;
} Node;

/* Jobs run on worker threads, hence the lock. */
G_LOCK_DEFINE_STATIC (home_nodes);
static char *nodes_for;
static gboolean have_home;
static Node home_node;
static GArray *above_home;

static HANDLE
open_for_id (const char *path)
{
	gunichar2 *wide;
	HANDLE handle;

	wide = g_utf8_to_utf16 (path, -1, NULL, NULL, NULL);
	if (wide == NULL) {
		return INVALID_HANDLE_VALUE;
	}

	/* Backup semantics is what lets a folder be opened at all, and needs no
	   privilege for attributes alone. */
	handle = CreateFileW ((LPCWSTR) wide, FILE_READ_ATTRIBUTES,
			      FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
			      NULL, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, NULL);
	g_free (wide);

	return handle;
}

static gboolean
node_of (const char *path, Node *node)
{
	HANDLE handle;
	BY_HANDLE_FILE_INFORMATION info;
	gboolean found;

	handle = open_for_id (path);
	if (handle == INVALID_HANDLE_VALUE) {
		return FALSE;
	}

	found = GetFileInformationByHandle (handle, &info);
	if (found) {
		node->volume = info.dwVolumeSerialNumber;
		node->index = ((ULONGLONG) info.nFileIndexHigh << 32) | info.nFileIndexLow;
	}

	CloseHandle (handle);

	return found;
}

static gboolean
same_node (const Node *a, const Node *b)
{
	return a->volume == b->volume && a->index == b->index;
}

/* Where a path really is once every junction and link on the way is followed.
   Caller frees. */
static char *
resolve (const char *path)
{
	HANDLE handle;
	WCHAR *wide = NULL;
	DWORD len;
	char *real = NULL;

	handle = open_for_id (path);
	if (handle == INVALID_HANDLE_VALUE) {
		return NULL;
	}

	len = GetFinalPathNameByHandleW (handle, NULL, 0, FILE_NAME_NORMALIZED | VOLUME_NAME_DOS);
	if (len > 0) {
		wide = g_new0 (WCHAR, len + 1);
		if (GetFinalPathNameByHandleW (handle, wide, len + 1, FILE_NAME_NORMALIZED | VOLUME_NAME_DOS) <= len) {
			real = g_utf16_to_utf8 ((gunichar2 *) wide, -1, NULL, NULL, NULL);
		}
		g_free (wide);
	}

	CloseHandle (handle);

	if (real != NULL && g_str_has_prefix (real, "\\\\?\\UNC\\")) {
		char *unc = g_strconcat ("\\\\", real + 8, NULL);

		g_free (real);
		real = unc;
	} else if (real != NULL && g_str_has_prefix (real, "\\\\?\\")) {
		memmove (real, real + 4, strlen (real + 4) + 1);
	}

	return real;
}

static void
add_parents (const char *start)
{
	char *dir = g_path_get_dirname (start);

	for (;;) {
		Node node;
		char *parent;

		if (node_of (dir, &node)) {
			g_array_append_val (above_home, node);
		}

		parent = g_path_get_dirname (dir);
		if (strcmp (parent, dir) == 0) {
			g_free (parent);
			break;
		}
		g_free (dir);
		dir = parent;
	}

	g_free (dir);
}

static void
refresh_home_nodes (void)
{
	const char *home = g_get_home_dir ();
	char *real;

	if (above_home != NULL && g_strcmp0 (nodes_for, home) == 0) {
		return;
	}

	g_clear_pointer (&above_home, g_array_unref);
	g_free (nodes_for);
	nodes_for = g_strdup (home);
	above_home = g_array_new (FALSE, FALSE, sizeof (Node));

	have_home = node_of (home, &home_node);

	add_parents (home);
	real = resolve (home);
	if (real != NULL) {
		add_parents (real);
		g_free (real);
	}
}

/* The old path compare stays as a floor, for a folder that cannot be opened. */
static gboolean
path_is_home (const char *path, gboolean or_above)
{
	char *canon, *mine, *home;
	gsize len;
	gboolean found;

	canon = g_canonicalize_filename (path, NULL);
	mine = g_utf8_casefold (canon, -1);
	g_free (canon);

	canon = g_canonicalize_filename (g_get_home_dir (), NULL);
	home = g_utf8_casefold (canon, -1);
	g_free (canon);

	len = strlen (mine);
	found = strcmp (mine, home) == 0 ||
		(or_above && strncmp (home, mine, len) == 0 && G_IS_DIR_SEPARATOR (home[len]));

	g_free (mine);
	g_free (home);

	return found;
}

gboolean
nemo_delete_guard_win32_is_home (const char *path, gboolean or_above)
{
	Node node;
	gboolean found;
	guint i;

	if (path_is_home (path, or_above)) {
		return TRUE;
	}

	if (!node_of (path, &node)) {
		return FALSE;
	}

	G_LOCK (home_nodes);
	refresh_home_nodes ();

	found = have_home && same_node (&home_node, &node);
	for (i = 0; or_above && !found && i < above_home->len; i++) {
		found = same_node (&g_array_index (above_home, Node, i), &node);
	}

	G_UNLOCK (home_nodes);

	return found;
}

static gboolean
thread_is_ours (DWORD thread)
{
	HANDLE handle;
	DWORD process;

	if (thread == 0) {
		return FALSE;
	}

	handle = OpenThread (THREAD_QUERY_LIMITED_INFORMATION, FALSE, thread);
	if (handle == NULL) {
		return FALSE;
	}

	process = GetProcessIdOfThread (handle);
	CloseHandle (handle);

	return process == GetCurrentProcessId ();
}

/* Main thread only, from the window procedure. */
static DWORD handed_to;

gboolean
nemo_delete_guard_win32_back_from_own_thread (const void *native)
{
	const MSG *msg = native;
	DWORD thread;

	if (msg == NULL || msg->message != WM_ACTIVATEAPP) {
		return FALSE;
	}

	thread = (DWORD) msg->lParam;

	if (!msg->wParam) {
		handed_to = thread_is_ours (thread) ? thread : 0;
		return FALSE;
	}

	/* No thread means the window that had the focus is gone, as a closed
	   sheet is. And a sheet's thread can end before its close reaches us. */
	if (thread == 0) {
		return handed_to != 0;
	}

	return thread == handed_to || thread_is_ours (thread);
}

#endif /* G_OS_WIN32 */
