/* nemo-session-bus-win32.c - the session bus, minus the file it leaves in TEMP
 *
 * When no session bus is up, GLib on Windows starts "gdbus.exe
 * _win32_run_session_bus" from beside its gio dll, and that is all the bundle
 * ever runs gdbus for. So this is shipped under that name.
 *
 * The bus is still GLib's own. Its daemon drops its server without stopping it,
 * and the server's start keeps a reference to itself until a stop, so the
 * nonce file it made in TEMP is never removed. One was left per bus, which
 * means about one per launch. Here the bus gets a TEMP folder of its own,
 * named for this process, removed when the bus ends. A bus that was killed
 * leaves its folder, and the next one clears it.
 *
 * Copyright © 2026 t00mietum (CryptogID: ปʬϝღถɔ4რఠΔթะ9ƾǝu)
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published
 * by the Free Software Foundation.
 */

#include <stdio.h>
#include <string.h>
#include <wchar.h>

#include <glib.h>
#include <windows.h>

#define RUN_SESSION_BUS "_win32_run_session_bus"
#define FOLDER_PREFIX L"nemo-anywhere-bus-"

/* Exported from the gio dll for its own gdbus.exe, though not in any header. */
__declspec(dllimport) void __stdcall
g_win32_run_session_bus (void *hwnd, void *hinst, const char *cmdline, int cmdshow);

/* Only plain files inside, never through a link, and never a folder within. */
static void
remove_folder (const wchar_t *folder)
{
	wchar_t pattern[MAX_PATH], path[MAX_PATH];
	WIN32_FIND_DATAW found;
	HANDLE walk;

	if (_snwprintf (pattern, MAX_PATH, L"%ls\\*", folder) < 0) {
		return;
	}
	pattern[MAX_PATH - 1] = 0;
	walk = FindFirstFileW (pattern, &found);
	if (walk != INVALID_HANDLE_VALUE) {
		do {
			if (found.dwFileAttributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)) {
				continue;
			}
			if (_snwprintf (path, MAX_PATH, L"%ls\\%ls", folder, found.cFileName) >= 0) {
				path[MAX_PATH - 1] = 0;
				DeleteFileW (path);
			}
		} while (FindNextFileW (walk, &found));
		FindClose (walk);
	}
	RemoveDirectoryW (folder);
}

static gboolean
process_alive (DWORD pid)
{
	HANDLE process = OpenProcess (PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
	DWORD code = 0;
	gboolean alive;

	if (process == NULL) {
		/* Refused is someone else's live process. */
		return GetLastError () == ERROR_ACCESS_DENIED;
	}
	alive = !GetExitCodeProcess (process, &code) || code == STILL_ACTIVE;
	CloseHandle (process);
	return alive;
}

/* Folders of buses that were killed. A PID reused by another program only keeps
 * one a while longer; reused by this one, the folder is stale and in the way. */
static void
clear_dead (const wchar_t *temp)
{
	wchar_t pattern[MAX_PATH], path[MAX_PATH];
	WIN32_FIND_DATAW found;
	HANDLE walk;

	if (_snwprintf (pattern, MAX_PATH, L"%ls\\" FOLDER_PREFIX L"*", temp) < 0) {
		return;
	}
	pattern[MAX_PATH - 1] = 0;
	walk = FindFirstFileW (pattern, &found);
	if (walk == INVALID_HANDLE_VALUE) {
		return;
	}
	do {
		const wchar_t *digits = found.cFileName + wcslen (FOLDER_PREFIX);
		wchar_t *end = NULL;
		unsigned long pid = wcstoul (digits, &end, 10);

		if ((found.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) ||
		    !(found.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) ||
		    end == digits || *end != 0 || pid == 0 ||
		    (pid != GetCurrentProcessId () && process_alive ((DWORD) pid))) {
			continue;
		}
		if (_snwprintf (path, MAX_PATH, L"%ls\\%ls", temp, found.cFileName) >= 0) {
			path[MAX_PATH - 1] = 0;
			remove_folder (path);
		}
	} while (FindNextFileW (walk, &found));
	FindClose (walk);
}

int
main (int argc, char **argv)
{
	const char *temp = g_getenv ("TEMP");
	g_autofree wchar_t *wtemp = NULL;
	g_autofree char *folder = NULL;
	g_autofree wchar_t *wfolder = NULL;

	if (argc != 2 || strcmp (argv[1], RUN_SESSION_BUS) != 0) {
		fprintf (stderr, "This gdbus only runs the session bus for nemo-anywhere (%s).\n", RUN_SESSION_BUS);
		return 1;
	}

	/* With no TEMP, GLib uses the drive root, and so does this, as is. */
	if (temp != NULL && *temp != '\0') {
		folder = g_strdup_printf ("%s\\nemo-anywhere-bus-%lu", temp, (unsigned long) GetCurrentProcessId ());
		wtemp = g_utf8_to_utf16 (temp, -1, NULL, NULL, NULL);
		wfolder = g_utf8_to_utf16 (folder, -1, NULL, NULL, NULL);
	}
	if (wtemp != NULL && wfolder != NULL) {
		clear_dead (wtemp);
		if (CreateDirectoryW (wfolder, NULL)) {
			/* Before anything asks GLib for its temp folder, which it keeps. */
			g_setenv ("TEMP", folder, TRUE);
			g_unsetenv ("G_TEST_TMPDIR");
		} else {
			g_clear_pointer (&wfolder, g_free);
		}
	}

	g_win32_run_session_bus (NULL, NULL, NULL, 0);

	if (wfolder != NULL) {
		remove_folder (wfolder);
	}
	return 0;
}
