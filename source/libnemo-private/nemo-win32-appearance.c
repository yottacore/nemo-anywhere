/* Win32-only: follow the Windows system light/dark app theme. Own file because
   <windows.h> defines macros (DELETE, ERROR, ...) that collide elsewhere.

   Copyright © 2026 t00mietum (CryptogID: ปʬϝღถɔ4რఠΔթะ9ƾǝu) */

#include <config.h>

#include <glib.h>

#ifdef G_OS_WIN32

#include "nemo-win32-appearance.h"

#include <windows.h>

#define PERSONALIZE_KEY \
	L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize"

static NemoWin32DarkChanged watch_cb = NULL;
static gpointer            watch_data = NULL;

gboolean
nemo_win32_prefers_dark (void)
{
	DWORD value = 1;		/* default to light if the value is missing */
	DWORD size = sizeof (value);

	/* AppsUseLightTheme: 1 = light apps, 0 = dark apps. */
	if (RegGetValueW (HKEY_CURRENT_USER, PERSONALIZE_KEY, L"AppsUseLightTheme",
			  RRF_RT_REG_DWORD, NULL, &value, &size) != ERROR_SUCCESS) {
		return FALSE;
	}

	return value == 0;
}

/* Marshalled onto the main loop by the watcher thread. */
static gboolean
notify_on_main (G_GNUC_UNUSED gpointer data)
{
	if (watch_cb != NULL) {
		watch_cb (nemo_win32_prefers_dark (), watch_data);
	}
	return G_SOURCE_REMOVE;
}

/* Blocks on registry-change notifications; hops back to the main loop on each. */
static gpointer
watch_thread (G_GNUC_UNUSED gpointer data)
{
	HKEY key;

	if (RegOpenKeyExW (HKEY_CURRENT_USER, PERSONALIZE_KEY, 0,
			   KEY_NOTIFY | KEY_QUERY_VALUE, &key) != ERROR_SUCCESS) {
		return NULL;
	}

	for (;;) {
		if (RegNotifyChangeKeyValue (key, FALSE, REG_NOTIFY_CHANGE_LAST_SET,
					     NULL, FALSE) != ERROR_SUCCESS) {
			break;
		}
		g_idle_add (notify_on_main, NULL);
	}

	RegCloseKey (key);
	return NULL;
}

void
nemo_win32_watch_dark (NemoWin32DarkChanged cb, gpointer user_data)
{
	GThread *thread;

	watch_cb = cb;
	watch_data = user_data;

	thread = g_thread_new ("win32-dark-watch", watch_thread, NULL);
	g_thread_unref (thread);	/* fire and forget; runs for process life */
}

#endif /* G_OS_WIN32 */
