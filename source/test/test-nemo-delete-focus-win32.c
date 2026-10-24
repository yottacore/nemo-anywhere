/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */

/* test-nemo-delete-focus-win32.c - one delete key is enough right after a
   window on another of the program's threads closes.

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

/* The shell's property sheet runs on a thread of the program's own, outside
 * GTK. A plain window on another thread, owned by the GTK one, stands in for
 * it: once it closes, the delete key counts at once. A fresh window, and focus
 * back from another program (this one again, run with --other), still ignore
 * it for a second. Skips where the window can't get the focus. */

#include <config.h>

#include <string.h>

#include <gtk/gtk.h>
#include <gdk/gdkwin32.h>

#include <windows.h>

#include <libnemo-private/nemo-delete-guard.h>

#include "test-check.h"

static gboolean too_soon;
static HWND sheet;
static HANDLE sheet_up;

static gboolean
on_key (GtkWidget *widget, G_GNUC_UNUSED GdkEventKey *event, G_GNUC_UNUSED gpointer data)
{
	too_soon = nemo_delete_guard_key_too_soon (widget);
	return GDK_EVENT_STOP;
}

static void
press_delete (GtkWidget *window)
{
	GdkEvent *event = gdk_event_new (GDK_KEY_PRESS);
	GdkSeat *seat = gdk_display_get_default_seat (gtk_widget_get_display (window));

	event->key.window = g_object_ref (gtk_widget_get_window (window));
	event->key.send_event = TRUE;
	event->key.time = GDK_CURRENT_TIME;
	event->key.keyval = GDK_KEY_Delete;
	gdk_event_set_device (event, gdk_seat_get_keyboard (seat));

	too_soon = FALSE;
	gtk_main_do_event (event);
	gdk_event_free (event);
}

static void
pump (guint msec)
{
	gint64 until = g_get_monotonic_time () + (gint64) msec * 1000;

	while (g_get_monotonic_time () < until) {
		while (gtk_events_pending ()) {
			gtk_main_iteration_do (FALSE);
		}
		g_usleep (10000);
	}
}

static gboolean
wait_focus (GtkWindow *window, gboolean want)
{
	gint64 until = g_get_monotonic_time () + 5 * G_USEC_PER_SEC;

	while (g_get_monotonic_time () < until) {
		while (gtk_events_pending ()) {
			gtk_main_iteration_do (FALSE);
		}
		if (!!gtk_window_has_toplevel_focus (window) == !!want) {
			return TRUE;
		}
		g_usleep (10000);
	}

	return FALSE;
}

static LRESULT CALLBACK
sheet_proc (HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam)
{
	if (message == WM_DESTROY) {
		PostQuitMessage (0);
		return 0;
	}

	return DefWindowProcW (hwnd, message, wparam, lparam);
}

static HWND
plain_window (HWND owner)
{
	WNDCLASSW klass = { 0 };

	klass.lpfnWndProc = sheet_proc;
	klass.hInstance = GetModuleHandleW (NULL);
	klass.lpszClassName = L"NemoTestSheet";
	RegisterClassW (&klass);

	return CreateWindowExW (0, klass.lpszClassName, L"sheet", WS_OVERLAPPEDWINDOW,
				40, 40, 300, 200, owner, NULL, klass.hInstance, NULL);
}

/* The other program: up front for a little over a second, then gone. */
static int
other_program (void)
{
	HWND other = plain_window (NULL);
	MSG message;

	if (other == NULL) {
		return 1;
	}

	ShowWindow (other, SW_SHOWNORMAL);
	SetForegroundWindow (other);
	SetTimer (other, 1, 1200, NULL);

	while (GetMessageW (&message, NULL, 0, 0) > 0) {
		if (message.message == WM_TIMER) {
			DestroyWindow (other);
		}
		DispatchMessageW (&message);
	}

	return 0;
}

static gboolean
other_program_round (GtkWindow *window)
{
	wchar_t self[MAX_PATH];
	char *argv[3] = { NULL, (char *) "--other", NULL };
	GError *error = NULL;
	gboolean ran;

	if (GetModuleFileNameW (NULL, self, G_N_ELEMENTS (self)) == 0) {
		return FALSE;
	}

	argv[0] = g_utf16_to_utf8 ((gunichar2 *) self, -1, NULL, NULL, NULL);
	AllowSetForegroundWindow (ASFW_ANY);
	ran = g_spawn_async (NULL, argv, NULL, G_SPAWN_DEFAULT, NULL, NULL, NULL, &error);
	g_free (argv[0]);

	if (!ran) {
		g_printerr ("couldn't start the other program: %s\n", error->message);
		g_error_free (error);
		return FALSE;
	}

	if (!wait_focus (window, FALSE)) {
		return FALSE;
	}
	if (!wait_focus (window, TRUE)) {
		gtk_window_present (window);
		return wait_focus (window, TRUE);
	}

	return TRUE;
}

static gpointer
sheet_thread (gpointer owner)
{
	MSG message;

	sheet = plain_window (owner);
	if (sheet != NULL) {
		ShowWindow (sheet, SW_SHOWNORMAL);
		SetForegroundWindow (sheet);
	}
	SetEvent (sheet_up);

	while (sheet != NULL && GetMessageW (&message, NULL, 0, 0) > 0) {
		TranslateMessage (&message);
		DispatchMessageW (&message);
	}

	return NULL;
}

int
main (int argc, char **argv)
{
	GtkWidget *window;
	GThread *thread;
	HWND owner;

	if (argc > 1 && strcmp (argv[1], "--other") == 0) {
		return other_program ();
	}

	if (!gtk_init_check (&argc, &argv)) {
		g_printerr ("SKIP: no display\n");
		return 77;
	}

	window = gtk_window_new (GTK_WINDOW_TOPLEVEL);
	gtk_window_set_default_size (GTK_WINDOW (window), 320, 240);
	nemo_delete_guard_watch_window (GTK_WINDOW (window));
	g_signal_connect (window, "key-press-event", G_CALLBACK (on_key), NULL);
	gtk_widget_show (window);
	gtk_window_present (GTK_WINDOW (window));

	if (!wait_focus (GTK_WINDOW (window), TRUE)) {
		g_printerr ("SKIP: the window never got the focus\n");
		return 77;
	}

	/* A window just opened ignores it, and a second later doesn't. */
	press_delete (window);
	check (too_soon);
	pump (1200);
	press_delete (window);
	check (!too_soon);

	owner = (HWND) gdk_win32_window_get_handle (gtk_widget_get_window (window));
	sheet_up = CreateEventW (NULL, TRUE, FALSE, NULL);
	thread = g_thread_new ("sheet", sheet_thread, owner);
	WaitForSingleObject (sheet_up, 5000);

	if (sheet == NULL || !wait_focus (GTK_WINDOW (window), FALSE)) {
		g_printerr ("SKIP: the other thread's window never got the focus\n");
		return 77;
	}

	/* Past the second the focus-out itself would cover. */
	pump (1200);
	PostMessageW (sheet, WM_CLOSE, 0, 0);
	g_thread_join (thread);

	if (!wait_focus (GTK_WINDOW (window), TRUE)) {
		g_printerr ("FAIL: the focus never came back\n");
		return 1;
	}

	press_delete (window);
	check (!too_soon);

	pump (1200);
	if (other_program_round (GTK_WINDOW (window))) {
		press_delete (window);
		check (too_soon);
	} else {
		g_printerr ("SKIP: focus never went to the other program and back\n");
	}

	gtk_widget_destroy (window);
	CloseHandle (sheet_up);

	return failures > 0 ? 1 : 0;
}
