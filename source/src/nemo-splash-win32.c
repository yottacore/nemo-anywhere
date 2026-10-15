/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */

/* nemo-splash-win32.c - the window that stands in until the real one is drawn.

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
#include <nemo-build-number.h>
#include <glib.h>

#include "nemo-splash.h"

#ifdef G_OS_WIN32

#include <windows.h>
#include <string.h>
#include <wchar.h>

#define SPLASH_CLASS		L"NemoAnywhereSplash"

/* Logical pixels at 96 dpi; scaled by the screen's own dpi at creation. */
#define SPLASH_W		460
#define SPLASH_H		250
#define SPLASH_PAD		18
#define LOG_LINES		10		/* the window the user asked for */
#define LINE_H			15

/* Kept rather than trimmed to LOG_LINES so a line already half scrolled off
 * still has something to follow it. */
#define MAX_LINES		64

#define ANIM_TIMER		1
#define ANIM_MS			16		/* about 60 a second */

#define WM_SPLASH_LINE		(WM_APP + 1)

static HWND              splash_window;
static HANDLE            splash_thread;
static DWORD             splash_thread_id;
static CRITICAL_SECTION  splash_lock;
static gboolean          splash_lock_ready;
static volatile LONG     splash_up;

/* Ring of what startup has reported, oldest first once it wraps. */
static wchar_t          *log_lines[MAX_LINES];
static int               log_first;		/* index of the oldest */
static int               log_count;

/* Pixels the viewport is still displaced by. A new line starts this at one
 * line height and the timer eases it back to nothing, which is the scroll. */
static double            scroll_px;

static HFONT             font_title;
static HFONT             font_body;
static int               dpi = 96;

/* Under the name, where a second "Starting" would just be the log line again. */
static wchar_t           version_text[64] = L"";

static gboolean          dark;

#define SCALE(v)	MulDiv ((v), dpi, 96)

/* Colors */

/* Read Windows' own light/dark choice. Done here rather than through the
 * appearance code because the splash runs before anything else is up. */
static gboolean
windows_prefers_dark (void)
{
	HKEY  key;
	DWORD value = 1;
	DWORD size = sizeof (value);
	DWORD type = 0;
	gboolean is_dark = FALSE;

	if (RegOpenKeyExW (HKEY_CURRENT_USER,
			   L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
			   0, KEY_QUERY_VALUE, &key) != ERROR_SUCCESS) {
		return FALSE;
	}

	if (RegQueryValueExW (key, L"AppsUseLightTheme", NULL, &type,
			      (LPBYTE) &value, &size) == ERROR_SUCCESS &&
	    type == REG_DWORD) {
		is_dark = (value == 0);
	}

	RegCloseKey (key);
	return is_dark;
}

static COLORREF
color_background (void)
{
	return dark ? RGB (32, 32, 32) : RGB (250, 250, 250);
}

static COLORREF
color_border (void)
{
	return dark ? RGB (64, 64, 64) : RGB (208, 208, 208);
}

static COLORREF
color_title (void)
{
	return dark ? RGB (240, 240, 240) : RGB (28, 28, 28);
}

static COLORREF
color_body (void)
{
	return dark ? RGB (150, 150, 150) : RGB (110, 110, 110);
}

/* The log */

static wchar_t *
wide_dup (const wchar_t *text)
{
	gsize    count;
	wchar_t *copy;

	if (text == NULL) {
		return NULL;
	}

	count = wcslen (text) + 1;
	copy = g_new (wchar_t, count);
	memcpy (copy, text, count * sizeof (wchar_t));

	return copy;
}

static void
push_line (const char *utf8)
{
	wchar_t *wide;
	int      needed;
	int      slot;

	needed = MultiByteToWideChar (CP_UTF8, 0, utf8, -1, NULL, 0);
	if (needed <= 0) {
		return;
	}

	wide = g_new (wchar_t, needed);
	MultiByteToWideChar (CP_UTF8, 0, utf8, -1, wide, needed);

	EnterCriticalSection (&splash_lock);

	if (log_count < MAX_LINES) {
		slot = (log_first + log_count) % MAX_LINES;
		log_count++;
	} else {
		slot = log_first;
		log_first = (log_first + 1) % MAX_LINES;
		g_free (log_lines[slot]);
	}
	log_lines[slot] = wide;

	/* Fills from the top like a terminal, and only scrolls once the ten
	 * lines are used up. Nothing moves while there is still room. */
	if (log_count > LOG_LINES) {
		scroll_px = SCALE (LINE_H);
	}

	LeaveCriticalSection (&splash_lock);
}

static void
free_lines (void)
{
	int i;

	for (i = 0; i < MAX_LINES; i++) {
		g_clear_pointer (&log_lines[i], g_free);
	}
	log_first = 0;
	log_count = 0;
}

/* Painting */

static void
paint (HWND window, HDC target)
{
	RECT     client;
	HDC      dc;
	HBITMAP  bitmap, old_bitmap;
	HBRUSH   brush;
	HPEN     pen, old_pen;
	HFONT    old_font;
	RECT     box;
	int      log_top, log_bottom;
	int      i, first, count;
	gboolean full;
	double   offset;
	wchar_t *shown[LOG_LINES + 1];
	int      shown_count = 0;

	GetClientRect (window, &client);

	/* Double buffered: the log scrolls a pixel at a time and painting
	 * straight to the window flickers the whole panel while it does. */
	dc = CreateCompatibleDC (target);
	bitmap = CreateCompatibleBitmap (target, client.right, client.bottom);
	old_bitmap = (HBITMAP) SelectObject (dc, bitmap);

	brush = CreateSolidBrush (color_background ());
	FillRect (dc, &client, brush);
	DeleteObject (brush);

	pen = CreatePen (PS_SOLID, 1, color_border ());
	old_pen = (HPEN) SelectObject (dc, pen);
	SelectObject (dc, GetStockObject (NULL_BRUSH));
	Rectangle (dc, 0, 0, client.right, client.bottom);
	SelectObject (dc, old_pen);
	DeleteObject (pen);

	SetBkMode (dc, TRANSPARENT);

	old_font = (HFONT) SelectObject (dc, font_title);
	SetTextColor (dc, color_title ());
	box.left = SCALE (SPLASH_PAD);
	box.top = SCALE (SPLASH_PAD);
	box.right = client.right - SCALE (SPLASH_PAD);
	box.bottom = box.top + SCALE (28);
	DrawTextW (dc, L"Nemo Anywhere", -1, &box,
		   DT_SINGLELINE | DT_LEFT | DT_TOP | DT_NOPREFIX);

	SelectObject (dc, font_body);
	SetTextColor (dc, color_body ());
	box.top = box.bottom;
	box.bottom = box.top + SCALE (LINE_H);
	DrawTextW (dc, version_text, -1, &box,
		   DT_SINGLELINE | DT_LEFT | DT_TOP | DT_NOPREFIX);

	/* The log viewport: a fixed ten lines, clipped, never wrapped. */
	log_top = box.bottom + SCALE (10);
	log_bottom = log_top + SCALE (LINE_H) * LOG_LINES;

	EnterCriticalSection (&splash_lock);
	offset = scroll_px;
	/* Once past ten, one extra is carried so the line leaving the top has
	 * something to slide out as. */
	full = log_count > LOG_LINES;
	count = full ? LOG_LINES + 1 : log_count;
	first = (log_first + log_count - count) % MAX_LINES;
	for (i = 0; i < count; i++) {
		shown[shown_count++] = wide_dup (log_lines[(first + i) % MAX_LINES]);
	}
	LeaveCriticalSection (&splash_lock);

	IntersectClipRect (dc, SCALE (SPLASH_PAD), log_top,
			   client.right - SCALE (SPLASH_PAD), log_bottom);

	for (i = 0; i < shown_count; i++) {
		/* Filling: straight down from the top, nothing moving. Full: the
		 * oldest of these sits one line above the top on its way out. */
		int row = full
			? log_top + SCALE (LINE_H) * (i - 1) + (int) (offset + 0.5)
			: log_top + SCALE (LINE_H) * i;

		box.left = SCALE (SPLASH_PAD);
		box.right = client.right - SCALE (SPLASH_PAD);
		box.top = row;
		box.bottom = row + SCALE (LINE_H);

		/* The last line is the one happening now; the rest have faded back. */
		SetTextColor (dc, i == shown_count - 1 ? color_title () : color_body ());

		if (shown[i] != NULL) {
			DrawTextW (dc, shown[i], -1, &box,
				   DT_SINGLELINE | DT_LEFT | DT_TOP |
				   DT_NOPREFIX | DT_END_ELLIPSIS);
		}
		g_free (shown[i]);
	}

	SelectClipRgn (dc, NULL);
	SelectObject (dc, old_font);

	BitBlt (target, 0, 0, client.right, client.bottom, dc, 0, 0, SRCCOPY);

	SelectObject (dc, old_bitmap);
	DeleteObject (bitmap);
	DeleteDC (dc);
}

/* The window */

static LRESULT CALLBACK
splash_proc (HWND window, UINT message, WPARAM wparam, LPARAM lparam)
{
	switch (message) {
	case WM_SPLASH_LINE:
		/* push_line has already decided whether this one scrolls. */
		InvalidateRect (window, NULL, FALSE);
		return 0;

	case WM_TIMER:
		if (wparam == ANIM_TIMER) {
			gboolean moving;

			EnterCriticalSection (&splash_lock);
			moving = scroll_px > 0.5;
			if (moving) {
				/* Ease out: most of the distance in the first few
				 * frames, so it reads as a slide and not a jump. */
				scroll_px -= scroll_px * 0.28 + 0.6;
				if (scroll_px < 0.5) {
					scroll_px = 0;
				}
			}
			LeaveCriticalSection (&splash_lock);

			if (moving) {
				InvalidateRect (window, NULL, FALSE);
			}
		}
		return 0;

	case WM_PAINT: {
		PAINTSTRUCT ps;
		HDC dc = BeginPaint (window, &ps);

		paint (window, dc);
		EndPaint (window, &ps);
		return 0;
	}

	case WM_ERASEBKGND:
		return 1;		/* painted whole in WM_PAINT */

	case WM_DESTROY:
		PostQuitMessage (0);
		return 0;
	}

	return DefWindowProcW (window, message, wparam, lparam);
}

static void
make_fonts (void)
{
	font_title = CreateFontW (-MulDiv (14, dpi, 72), 0, 0, 0, FW_SEMIBOLD,
				  FALSE, FALSE, FALSE, DEFAULT_CHARSET,
				  OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
				  CLEARTYPE_QUALITY, VARIABLE_PITCH, L"Segoe UI");
	font_body = CreateFontW (-MulDiv (9, dpi, 72), 0, 0, 0, FW_NORMAL,
				 FALSE, FALSE, FALSE, DEFAULT_CHARSET,
				 OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
				 CLEARTYPE_QUALITY, VARIABLE_PITCH, L"Segoe UI");
}

static DWORD WINAPI
splash_main (G_GNUC_UNUSED LPVOID data)
{
	WNDCLASSEXW cls;
	HINSTANCE   instance = GetModuleHandleW (NULL);
	HDC         screen;
	RECT        work;
	int         width, height, x, y;
	MSG         message;

	screen = GetDC (NULL);
	if (screen != NULL) {
		dpi = GetDeviceCaps (screen, LOGPIXELSY);
		ReleaseDC (NULL, screen);
	}
	if (dpi <= 0) {
		dpi = 96;
	}

	dark = windows_prefers_dark ();
	make_fonts ();

	MultiByteToWideChar (CP_UTF8, 0, NEMO_VERSION_STRING, -1, version_text,
			     (int) G_N_ELEMENTS (version_text));

	memset (&cls, 0, sizeof (cls));
	cls.cbSize = sizeof (cls);
	cls.lpfnWndProc = splash_proc;
	cls.hInstance = instance;
	cls.hCursor = LoadCursor (NULL, IDC_ARROW);
	cls.lpszClassName = SPLASH_CLASS;
	RegisterClassExW (&cls);

	width = SCALE (SPLASH_W);
	height = SCALE (SPLASH_H);

	if (!SystemParametersInfoW (SPI_GETWORKAREA, 0, &work, 0)) {
		work.left = 0;
		work.top = 0;
		work.right = GetSystemMetrics (SM_CXSCREEN);
		work.bottom = GetSystemMetrics (SM_CYSCREEN);
	}
	x = work.left + (work.right - work.left - width) / 2;
	y = work.top + (work.bottom - work.top - height) / 2;

	/* A tool window so it never takes a taskbar button, and topmost so the
	 * app's own window coming up does not bury it before it goes away. */
	splash_window = CreateWindowExW (WS_EX_TOOLWINDOW | WS_EX_TOPMOST,
					 SPLASH_CLASS, L"Nemo Anywhere",
					 WS_POPUP,
					 x, y, width, height,
					 NULL, NULL, instance, NULL);
	if (splash_window == NULL) {
		return 0;
	}

	SetTimer (splash_window, ANIM_TIMER, ANIM_MS, NULL);

	/* Activating, not SW_SHOWNOACTIVATE. Windows only lets a process call
	 * SetForegroundWindow when it already owns the foreground window, so the
	 * splash taking it is what lets the real window be handed it a moment
	 * later. Without this the app opens behind whatever was in front. */
	ShowWindow (splash_window, SW_SHOW);
	SetForegroundWindow (splash_window);
	UpdateWindow (splash_window);

	while (GetMessageW (&message, NULL, 0, 0) > 0) {
		TranslateMessage (&message);
		DispatchMessageW (&message);
	}

	splash_window = NULL;

	if (font_title != NULL) { DeleteObject (font_title); font_title = NULL; }
	if (font_body != NULL)  { DeleteObject (font_body);  font_body = NULL; }

	return 0;
}

/* What the rest of the program calls */

void
nemo_splash_show (void)
{
	if (InterlockedCompareExchange (&splash_up, 1, 0) != 0) {
		return;
	}

	InitializeCriticalSection (&splash_lock);
	splash_lock_ready = TRUE;

	splash_thread = CreateThread (NULL, 0, splash_main, NULL, 0, &splash_thread_id);
	if (splash_thread == NULL) {
		DeleteCriticalSection (&splash_lock);
		splash_lock_ready = FALSE;
		InterlockedExchange (&splash_up, 0);
	}
}

void
nemo_splash_note (const char *text)
{
	if (!splash_lock_ready || text == NULL || InterlockedCompareExchange (&splash_up, 1, 1) == 0) {
		return;
	}

	push_line (text);

	/* The window may not exist yet - the line is already stored, and the
	 * first paint will show it. */
	if (splash_window != NULL) {
		PostMessageW (splash_window, WM_SPLASH_LINE, 0, 0);
	}
}

gboolean
nemo_splash_is_up (void)
{
	return InterlockedCompareExchange (&splash_up, 1, 1) != 0;
}

void
nemo_splash_hide (void)
{
	if (InterlockedCompareExchange (&splash_up, 0, 1) == 0) {
		return;
	}

	if (splash_window != NULL) {
		PostMessageW (splash_window, WM_CLOSE, 0, 0);
	} else if (splash_thread_id != 0) {
		PostThreadMessageW (splash_thread_id, WM_QUIT, 0, 0);
	}

	if (splash_thread != NULL) {
		/* Bounded: a splash that will not go away must never be what
		 * keeps the app from starting. */
		WaitForSingleObject (splash_thread, 2000);
		CloseHandle (splash_thread);
		splash_thread = NULL;
	}

	if (splash_lock_ready) {
		EnterCriticalSection (&splash_lock);
		free_lines ();
		LeaveCriticalSection (&splash_lock);
		DeleteCriticalSection (&splash_lock);
		splash_lock_ready = FALSE;
	}
}

#else /* !G_OS_WIN32 */

void     nemo_splash_show  (void)                   { }
void     nemo_splash_note  (const char *text)       { (void) text; }
void     nemo_splash_hide  (void)                   { }
gboolean nemo_splash_is_up (void)                   { return FALSE; }

#endif
