/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */

/* nemo-properties-win32.c - the property sheet Windows itself shows.

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

/* glib before the guard: G_OS_WIN32 comes from glibconfig.h, not config.h, so
 * guarding on it without this compiles the whole file away and the link fails. */
#include <config.h>
#include <glib.h>

#ifdef G_OS_WIN32

/* Ahead of anything that pulls in the COM headers, so the I<iface>_<method>
 * spellings below exist - this is C, not C++. */
#define COBJMACROS

#include "nemo-properties-win32.h"

#include <libnemo-private/nemo-file.h>

#include <gio/gio.h>
#include <gdk/gdkwin32.h>

#include <windows.h>
#include <shlobj.h>

/* How long to keep pumping before deciding the sheet was never ours to run.
 * The shell usually puts the sheet on a thread of its own, in which case our
 * thread never owns a window and this is simply how long we idle before going. */
#define SHEET_GRACE_USEC	(3 * G_USEC_PER_SEC)

typedef struct {
	HWND	  owner;
	gunichar2 **paths;	/* NULL-terminated */
} SheetRequest;

static void
sheet_request_free (SheetRequest *request)
{
	g_strfreev ((gchar **) request->paths);
	g_free (request);
}

static BOOL CALLBACK
count_visible_window (HWND window, LPARAM total)
{
	if (IsWindowVisible (window)) {
		(*(gint *) total)++;
	}

	return TRUE;
}

/* Wait out the sheet. Whether InvokeCommand ran it modally, handed it to a
 * shell thread, or parked it on ours, this leaves only once nothing of ours is
 * on screen - so COM is never torn down under a live sheet. */
static void
pump_until_sheet_closes (void)
{
	gint64 deadline = g_get_monotonic_time () + SHEET_GRACE_USEC;

	for (;;) {
		MSG message;
		gint windows = 0;

		while (PeekMessageW (&message, NULL, 0, 0, PM_REMOVE)) {
			TranslateMessage (&message);
			DispatchMessageW (&message);
		}

		EnumThreadWindows (GetCurrentThreadId (), count_visible_window, (LPARAM) &windows);

		if (windows > 0) {
			/* It is up and it is ours; from here an empty thread means it closed. */
			deadline = 0;
		} else if (deadline == 0 || g_get_monotonic_time () > deadline) {
			break;
		}

		MsgWaitForMultipleObjectsEx (0, NULL, 100, QS_ALLINPUT, MWMO_INPUTAVAILABLE);
	}
}

/* Every item has to be a child of one folder - that is the only shape the
 * shell's own multi-item sheet has. Callers guarantee it by directory name;
 * this re-checks against what the shell actually parsed. */
static gboolean
collect_children (ITEMIDLIST_ABSOLUTE **absolute,
		  guint                 count,
		  LPCITEMIDLIST        *children)
{
	ITEMIDLIST_ABSOLUTE *first_parent;
	gboolean same = TRUE;
	guint i;

	first_parent = ILCloneFull (absolute[0]);
	ILRemoveLastID (first_parent);

	for (i = 0; i < count && same; i++) {
		ITEMIDLIST_ABSOLUTE *parent = ILCloneFull (absolute[i]);

		ILRemoveLastID (parent);
		same = ILIsEqual (parent, first_parent);
		ILFree (parent);

		children[i] = ILFindLastID (absolute[i]);
	}

	ILFree (first_parent);

	return same;
}

static void
invoke_properties (IContextMenu *menu,
		   HWND          owner)
{
	CMINVOKECOMMANDINFOEX invoke;
	HMENU popup;

	/* Handlers are entitled to assume the menu was built before a verb is
	 * invoked on it, and several do their setup work in QueryContextMenu. */
	popup = CreatePopupMenu ();
	IContextMenu_QueryContextMenu (menu, popup, 0, 1, 0x7FFF, CMF_NORMAL);

	memset (&invoke, 0, sizeof (invoke));
	invoke.cbSize = sizeof (invoke);
	invoke.fMask = CMIC_MASK_UNICODE;
	invoke.hwnd = owner;
	invoke.lpVerb = "properties";
	invoke.lpVerbW = L"properties";
	invoke.nShow = SW_SHOWNORMAL;

	IContextMenu_InvokeCommand (menu, (CMINVOKECOMMANDINFO *) &invoke);

	DestroyMenu (popup);
}

static gpointer
sheet_thread (gpointer data)
{
	SheetRequest *request = data;
	ITEMIDLIST_ABSOLUTE **absolute;
	LPCITEMIDLIST *children;
	IShellFolder *folder = NULL;
	IContextMenu *menu = NULL;
	guint count, parsed, i;
	HRESULT result;

	CoInitializeEx (NULL, COINIT_APARTMENTTHREADED);

	count = g_strv_length ((gchar **) request->paths);
	absolute = g_new0 (ITEMIDLIST_ABSOLUTE *, count);
	children = g_new0 (LPCITEMIDLIST, count);

	for (parsed = 0; parsed < count; parsed++) {
		result = SHParseDisplayName ((PCWSTR) request->paths[parsed], NULL,
					     &absolute[parsed], 0, NULL);
		if (FAILED (result)) {
			g_warning ("native properties: the shell wouldn't parse item %u (0x%lx)",
				   parsed, (unsigned long) result);
			break;
		}
	}

	if (parsed == count && collect_children (absolute, count, children)) {
		result = SHBindToParent (absolute[0], &IID_IShellFolder, (void **) &folder, NULL);

		if (SUCCEEDED (result)) {
			result = IShellFolder_GetUIObjectOf (folder, request->owner, count, children,
							     &IID_IContextMenu, NULL, (void **) &menu);
		}

		if (SUCCEEDED (result) && menu != NULL) {
			invoke_properties (menu, request->owner);
			pump_until_sheet_closes ();
		} else {
			g_warning ("native properties: no context menu for the selection (0x%lx)",
				   (unsigned long) result);
		}
	}

	if (menu != NULL) {
		IContextMenu_Release (menu);
	}
	if (folder != NULL) {
		IShellFolder_Release (folder);
	}
	for (i = 0; i < parsed; i++) {
		ILFree (absolute[i]);
	}

	g_free (children);
	g_free (absolute);
	sheet_request_free (request);

	CoUninitialize ();

	return NULL;
}

static HWND
owner_window (GtkWidget *parent_widget)
{
	GtkWidget *toplevel;
	GdkWindow *window;

	if (parent_widget == NULL) {
		return NULL;
	}

	toplevel = gtk_widget_get_toplevel (parent_widget);
	if (toplevel == NULL || !gtk_widget_is_toplevel (toplevel)) {
		return NULL;
	}

	window = gtk_widget_get_window (toplevel);
	if (window == NULL) {
		return NULL;
	}

	return (HWND) gdk_win32_window_get_handle (window);
}

/* The wide paths behind @files, or NULL when the shell has no sheet to show for
 * them. NULL-terminated, so it frees with g_strfreev. */
static gunichar2 **
collect_paths (GList *files)
{
	GPtrArray *paths;
	gchar *shared_parent = NULL;
	GList *node;

	if (files == NULL) {
		return NULL;
	}

	paths = g_ptr_array_new_with_free_func (g_free);

	for (node = files; node != NULL; node = node->next) {
		GFile *location = nemo_file_get_location (NEMO_FILE (node->data));
		gchar *path = (location != NULL) ? g_file_get_path (location) : NULL;
		gchar *parent;
		gboolean usable;

		g_clear_object (&location);

		/* A virtual location - our own trash or network scheme, a search
		 * result set, a burn target - has nothing the shell can name. */
		usable = (path != NULL && g_path_is_absolute (path) &&
			  g_file_test (path, G_FILE_TEST_EXISTS));

		if (usable) {
			parent = g_path_get_dirname (path);

			if (shared_parent == NULL) {
				shared_parent = parent;
			} else {
				/* Search results can span folders; the sheet cannot. */
				usable = (g_ascii_strcasecmp (parent, shared_parent) == 0);
				g_free (parent);
			}
		}

		if (!usable) {
			g_free (path);
			g_free (shared_parent);
			g_ptr_array_free (paths, TRUE);
			return NULL;
		}

		g_ptr_array_add (paths, g_utf8_to_utf16 (path, -1, NULL, NULL, NULL));
		g_free (path);
	}

	g_free (shared_parent);
	g_ptr_array_add (paths, NULL);

	return (gunichar2 **) g_ptr_array_free (paths, FALSE);
}

gboolean
nemo_properties_win32_can_show (GList *files)
{
	gunichar2 **paths = collect_paths (files);

	if (paths == NULL) {
		return FALSE;
	}

	g_strfreev ((gchar **) paths);

	return TRUE;
}

gboolean
nemo_properties_win32_show (GList     *files,
			    GtkWidget *parent_widget)
{
	SheetRequest *request;
	GThread *thread;
	gunichar2 **paths = collect_paths (files);

	if (paths == NULL) {
		return FALSE;
	}

	request = g_new0 (SheetRequest, 1);
	request->owner = owner_window (parent_widget);
	request->paths = paths;

	/* Off the main loop: the sheet may run its own modal loop, and a frozen
	 * file manager behind an open property sheet is what that would look like. */
	thread = g_thread_new ("nemo-properties-win32", sheet_thread, request);
	g_thread_unref (thread);

	/* cppcheck-suppress memleak ; request is owned by sheet_thread, which frees it */
	return TRUE;
}

#endif /* G_OS_WIN32 */
