/* nemo-window-at-point-win32.c - the Windows half of nemo-window-at-point.h.
 *
 * Copyright © 2026 t00mietum (CryptogID: ปʬϝღถɔ4რఠΔթะ9ƾǝu)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; version 2 of the License.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 */

#include <config.h>

/* glib is what defines G_OS_WIN32, so it has to come in before the guard. */
#include <glib.h>

#ifdef G_OS_WIN32

#include "nemo-window-at-point.h"

#include <gdk/gdkwin32.h>

#include <windows.h>

/* dwmapi is looked up rather than linked, as the DPI calls are. */
#define CLOAKED_ATTRIBUTE      14   /* DWMWA_CLOAKED */
#define FRAME_BOUNDS_ATTRIBUTE 9    /* DWMWA_EXTENDED_FRAME_BOUNDS */
typedef HRESULT (WINAPI *DwmGetWindowAttributeFn) (HWND, DWORD, PVOID, DWORD);

static DwmGetWindowAttributeFn
dwm_get_window_attribute (void)
{
	static DwmGetWindowAttributeFn fn;
	static gsize looked = 0;

	if (g_once_init_enter (&looked)) {
		HMODULE dwm = LoadLibraryW (L"dwmapi.dll");

		if (dwm != NULL) {
			fn = (DwmGetWindowAttributeFn) (void (*) (void)) GetProcAddress (dwm, "DwmGetWindowAttribute");
		}
		g_once_init_leave (&looked, 1);
	}

	return fn;
}

guint64
nemo_window_native_handle (GtkWindow *window)
{
	GdkWindow *gdk_window = gtk_widget_get_window (GTK_WIDGET (window));

	if (gdk_window == NULL || !GDK_IS_WIN32_WINDOW (gdk_window)) {
		return 0;
	}

	return (guint64) (guintptr) gdk_win32_window_get_handle (gdk_window);
}

void
nemo_window_allow_to_raise (guint64 handle)
{
	DWORD process_id = 0;

	/* A window that has gone, or no handle at all, gets nobody the right. */
	if (handle == 0 || GetWindowThreadProcessId ((HWND) (guintptr) handle, &process_id) == 0 ||
	    process_id == 0) {
		return;
	}

	AllowSetForegroundWindow (process_id);
}

/* A window from a suspended store app is "visible" but not drawn. */
static gboolean
cloaked (HWND window)
{
	DwmGetWindowAttributeFn get = dwm_get_window_attribute ();
	DWORD value = 0;

	return get != NULL &&
	       SUCCEEDED (get (window, CLOAKED_ATTRIBUTE, &value, sizeof value)) &&
	       value != 0;
}

/* The outer rectangle takes in an invisible resize border a few pixels wide on
   each side; the frame bounds are what can be seen. */
static gboolean
visible_rect (HWND window, RECT *rect)
{
	DwmGetWindowAttributeFn get = dwm_get_window_attribute ();

	if (get != NULL && SUCCEEDED (get (window, FRAME_BOUNDS_ATTRIBUTE, rect, sizeof *rect))) {
		return TRUE;
	}

	return GetWindowRect (window, rect);
}

int
nemo_window_at_pointer (GdkDisplay    *display,
                        const guint64 *candidates,
                        int            n_candidates)
{
	POINT point;
	HWND window;
	int i;

	if (n_candidates <= 0 || !GetCursorPos (&point)) {
		return -1;
	}

	/* Top to bottom. Tool windows are what tooltips, menus and the drag icon
	   are made as, so those are looked through. */
	for (window = GetTopWindow (NULL); window != NULL; window = GetWindow (window, GW_HWNDNEXT)) {
		LONG_PTR ex_style;
		RECT rect;

		if (!IsWindowVisible (window) || IsIconic (window) || cloaked (window)) {
			continue;
		}
		ex_style = GetWindowLongPtrW (window, GWL_EXSTYLE);
		if (ex_style & (WS_EX_TOOLWINDOW | WS_EX_TRANSPARENT)) {
			continue;
		}
		if (!visible_rect (window, &rect) || !PtInRect (&rect, point)) {
			continue;
		}

		for (i = 0; i < n_candidates; i++) {
			if ((HWND) (guintptr) candidates[i] == window) {
				return i;
			}
		}
		return -1;
	}

	return -1;
}

#endif
