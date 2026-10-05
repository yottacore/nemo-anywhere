/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */

/* nemo-dpi-win32.c - following the DPI of the monitor a window is on.

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

/* glib is what defines G_OS_WIN32, so it has to come in before the guard. */
#include <glib.h>

#ifdef G_OS_WIN32

#include "nemo-dpi-win32.h"

#include <gtk/gtk.h>
#include <gdk/gdkwin32.h>

#include <windows.h>

/* Both are looked up rather than linked: GetDpiForWindow arrived in 1607 and
   GetDpiForMonitor in 8.1, and we still start on what came before. */
typedef UINT    (WINAPI *GetDpiForWindowFn)  (HWND);
typedef HRESULT (WINAPI *GetDpiForMonitorFn) (HMONITOR, int, UINT *, UINT *);

static GetDpiForWindowFn  dpi_for_window;
static GetDpiForMonitorFn dpi_for_monitor;
static gboolean           looked_up;
static guint              recompute_id;

static void
look_up_once (void)
{
	HMODULE mod;

	if (looked_up) {
		return;
	}
	looked_up = TRUE;

	mod = GetModuleHandleW (L"user32.dll");
	if (mod != NULL) {
		dpi_for_window = (GetDpiForWindowFn) (void *)
			GetProcAddress (mod, "GetDpiForWindow");
	}

	/* Left loaded for the life of the process on purpose - GTK has it open
	   too, and unloading it under our own function pointer would be a way
	   to crash for no gain. */
	mod = LoadLibraryW (L"shcore.dll");
	if (mod != NULL) {
		dpi_for_monitor = (GetDpiForMonitorFn) (void *)
			GetProcAddress (mod, "GetDpiForMonitor");
	}
}

/* The DPI of the monitor this window is on, or of the primary one when there
   is no window yet. Never zero. */
static guint
monitor_dpi (HWND hwnd)
{
	UINT dx = 0, dy = 0;
	HMONITOR mon;
	HDC dc;
	int caps;

	look_up_once ();

	if (hwnd != NULL && dpi_for_window != NULL) {
		dx = dpi_for_window (hwnd);
		if (dx > 0) {
			return dx;
		}
	}

	if (dpi_for_monitor != NULL) {
		POINT origin = { 0, 0 };

		mon = hwnd != NULL
			? MonitorFromWindow (hwnd, MONITOR_DEFAULTTOPRIMARY)
			: MonitorFromPoint (origin, MONITOR_DEFAULTTOPRIMARY);

		/* 0 is MDT_EFFECTIVE_DPI: what the user chose, not what the
		   panel physically is. */
		if (mon != NULL && dpi_for_monitor (mon, 0, &dx, &dy) == S_OK && dx > 0) {
			return dx;
		}
	}

	/* Windows 7 and 8 have one DPI for the whole desktop. */
	dc = GetDC (NULL);
	if (dc != NULL) {
		caps = GetDeviceCaps (dc, LOGPIXELSX);
		ReleaseDC (NULL, dc);
		if (caps > 0) {
			return (guint) caps;
		}
	}

	return 96;
}

gint
nemo_dpi_win32_font_dpi (guint monitor_dpi_value,
			 gint  scale_factor)
{
	if (monitor_dpi_value == 0) {
		monitor_dpi_value = 96;
	}
	if (scale_factor < 1) {
		scale_factor = 1;
	}

	/* The toolkit multiplies what it draws by the scale factor already, so
	   dividing it back out of the DPI leaves exactly the fraction the whole
	   steps could not carry. At 100% and at 200% that comes to 96 and the
	   setting does not move; at 150% it comes to 144 and the type grows
	   while the widgets around it do not. */
	return (gint) (((guint64) monitor_dpi_value * 1024 + scale_factor / 2) / (guint) scale_factor);
}

/* The window to take the DPI from: whichever toplevel is on screen. With more
   than one, the first mapped one wins - gtk-xft-dpi is one value for the whole
   application, so monitors of different DPI cannot each have their own. */
static GdkWindow *
pick_window (void)
{
	GList *tops = gtk_window_list_toplevels ();
	GdkWindow *found = NULL;
	GList *l;

	for (l = tops; l != NULL && found == NULL; l = l->next) {
		GtkWidget *widget = GTK_WIDGET (l->data);

		if (gtk_widget_get_mapped (widget)) {
			found = gtk_widget_get_window (widget);
		}
	}

	g_list_free (tops);
	return found;
}

static void
apply_now (void)
{
	GdkWindow *window = pick_window ();
	GtkSettings *settings = gtk_settings_get_default ();
	HWND hwnd = NULL;
	gint scale = 1;
	gint wanted, current = 0;

	if (settings == NULL) {
		return;
	}

	if (window != NULL) {
		hwnd = (HWND) gdk_win32_window_get_handle (window);
		scale = gdk_window_get_scale_factor (window);
	} else {
		/* Before the first window there is still a scale factor to divide
		   back out - reading it as 1 on a 200% display would double the
		   type for as long as the splash is up. */
		GdkDisplay *display = gdk_display_get_default ();
		GdkMonitor *primary = display != NULL
			? gdk_display_get_primary_monitor (display)
			: NULL;

		if (primary != NULL) {
			scale = gdk_monitor_get_scale_factor (primary);
		}
	}

	wanted = nemo_dpi_win32_font_dpi (monitor_dpi (hwnd), scale);

	/* Setting it marks it as the application's choice, which outranks what
	   the backend reports - so this is not a fight that repeats. Only write
	   it when it has actually moved: every write reflows every label. */
	g_object_get (settings, "gtk-xft-dpi", &current, NULL);
	if (current != wanted) {
		g_object_set (settings, "gtk-xft-dpi", wanted, NULL);
	}
}

static gboolean
recompute_cb (G_GNUC_UNUSED gpointer data)
{
	recompute_id = 0;
	apply_now ();
	return G_SOURCE_REMOVE;
}

/* Deferred, because the message that brought us here has to reach GDK first:
   the new scale factor is only right once GDK has finished with it. */
static void
recompute_soon (void)
{
	if (recompute_id == 0) {
		recompute_id = g_idle_add_full (G_PRIORITY_DEFAULT_IDLE, recompute_cb, NULL, NULL);
	}
}

static GdkFilterReturn
message_filter (GdkXEvent *xevent,
		G_GNUC_UNUSED GdkEvent  *event,
		G_GNUC_UNUSED gpointer   data)
{
	MSG *msg = (MSG *) xevent;

	/* Sent when a window moves to a monitor at a different scale, and when
	   the scale of the monitor it is already on is changed. */
	if (msg->message == WM_DPICHANGED) {
		recompute_soon ();
	}

	return GDK_FILTER_CONTINUE;
}

static void
monitors_changed_cb (G_GNUC_UNUSED GdkScreen *screen,
		     G_GNUC_UNUSED gpointer   data)
{
	recompute_soon ();
}

void
nemo_dpi_win32_init (void)
{
	static gboolean started;
	GdkScreen *screen;

	if (started) {
		return;
	}
	started = TRUE;

	gdk_window_add_filter (NULL, message_filter, NULL);

	screen = gdk_screen_get_default ();
	if (screen != NULL) {
		/* A monitor plugged in or unplugged can change which one we are
		   on without any window moving. */
		g_signal_connect (screen, "monitors-changed",
				  G_CALLBACK (monitors_changed_cb), NULL);
	}

	/* No window exists yet, so this reads the primary monitor - right for the
	   splash and for a window that opens where it is expected to. The idle
	   runs once there is a real window, which is what settles a window that
	   opened on a second monitor at another scale. */
	apply_now ();
	recompute_soon ();
}

#endif /* G_OS_WIN32 */
