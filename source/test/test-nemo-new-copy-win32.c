/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */

/* test-nemo-new-copy-win32.c - a new window, or a tab moved out to one, starts
   the new copy from the app itself, with no spawn helper in between.

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

/* GLib's spawn goes through gspawn-win64-helper.exe here, which is packed in
 * the single exe, and a packed program started from it can fail to load its
 * libraries. nemo-new-process.c names our own exe, so this program stands in
 * for the new copy: started with a location, it writes down who started it
 * and what it was given, into that location's folder.
 *
 * Links from the About box go through the same file now, so a scheme of our
 * own is set up for the user and opened, and one nothing handles must fail
 * without starting anything. */

#include <config.h>

#include <stdio.h>
#include <string.h>

#include <windows.h>
#include <tlhelp32.h>

#include <glib.h>
#include <glib/gstdio.h>
#include <gio/gio.h>

#include <libnemo-private/nemo-dnd-win32.h>
#include <libnemo-private/nemo-file-utilities.h>
#include <libnemo-private/nemo-launch-win32.h>

#include "nemo-new-process.h"
#include "test-scratch.h"
#include "test-check.h"

#define WAIT_SECONDS 30
#define REPORT "copy-report.txt"
#define USERS_BUS "nemo-test:the-users-own"
#define PASSED_ON "NEMO_TEST_NEW_COPY_PASSED_ON"

static DWORD
parent_of (DWORD pid)
{
	HANDLE snapshot = CreateToolhelp32Snapshot (TH32CS_SNAPPROCESS, 0);
	PROCESSENTRY32 entry;
	DWORD parent = 0;

	if (snapshot == INVALID_HANDLE_VALUE) {
		return 0;
	}
	entry.dwSize = sizeof entry;
	for (BOOL more = Process32First (snapshot, &entry); more; more = Process32Next (snapshot, &entry)) {
		if (entry.th32ProcessID == pid) {
			parent = entry.th32ParentProcessID;
		}
	}
	CloseHandle (snapshot);
	return parent;
}

/* Any spawn helper started by @pid that is still around. */
static int
helpers_of (DWORD pid)
{
	HANDLE snapshot = CreateToolhelp32Snapshot (TH32CS_SNAPPROCESS, 0);
	PROCESSENTRY32 entry;
	int found = 0;

	if (snapshot == INVALID_HANDLE_VALUE) {
		return 0;
	}
	entry.dwSize = sizeof entry;
	for (BOOL more = Process32First (snapshot, &entry); more; more = Process32Next (snapshot, &entry)) {
		if (entry.th32ParentProcessID == pid && g_ascii_strncasecmp (entry.szExeFile, "gspawn-", 7) == 0) {
			found++;
		}
	}
	CloseHandle (snapshot);
	return found;
}

static int
report (const char *dir, int argc, char *argv[])
{
	GString *text = g_string_new (NULL);
	char *path = g_build_filename (dir, REPORT, NULL);
	const char *bus = g_getenv ("DBUS_SESSION_BUS_ADDRESS");
	gboolean ok;

	g_string_append_printf (text, "parent=%lu\n", (unsigned long) parent_of (GetCurrentProcessId ()));
	g_string_append_printf (text, "bus=%s\n", bus != NULL ? bus : "(unset)");
	g_string_append_printf (text, "freetype=%s\n", g_getenv ("FREETYPE_PROPERTIES") != NULL ? "set" : "unset");
	g_string_append_printf (text, "passed=%s\n", g_getenv (PASSED_ON) != NULL ? g_getenv (PASSED_ON) : "");
	for (int i = 1; i < argc; i++) {
		g_string_append_printf (text, "arg=%s\n", argv[i]);
	}
	ok = g_file_set_contents (path, text->str, (gssize) text->len, NULL);
	g_string_free (text, TRUE);
	g_free (path);
	return ok ? 0 : 1;
}

/* The folder a started copy writes into: the last argument's, or for --select
 * the folder of the item. NULL when this is not a started copy. */
static char *
copy_folder (int argc, char *argv[])
{
	gboolean select = FALSE;
	char *path;

	if (argc < 2 || !g_str_has_prefix (argv[argc - 1], "file:")) {
		return NULL;
	}
	for (int i = 1; i < argc; i++) {
		select = select || strcmp (argv[i], "--select") == 0;
	}
	path = g_filename_from_uri (argv[argc - 1], NULL, NULL);
	if (path != NULL && select) {
		char *dir = g_path_get_dirname (path);

		g_free (path);
		path = dir;
	}
	return path;
}

/* Returns: (transfer full): free with g_strfreev, NULL if nothing came */
static char **
wait_for_report (const char *dir)
{
	char *path = g_build_filename (dir, REPORT, NULL);
	gint64 until = g_get_monotonic_time () + WAIT_SECONDS * G_USEC_PER_SEC;
	char *text = NULL;
	char **lines = NULL;

	while (!g_file_get_contents (path, &text, NULL, NULL)) {
		if (g_get_monotonic_time () > until) {
			g_free (path);
			return NULL;
		}
		g_usleep (50 * 1000);
	}
	/* Written in one go, but read again in case it was caught half way. */
	g_usleep (100 * 1000);
	g_free (text);
	if (g_file_get_contents (path, &text, NULL, NULL)) {
		lines = g_strsplit (text, "\n", -1);
		g_free (text);
	}
	g_free (path);
	return lines;
}

static gboolean
has_line (char **lines, const char *line)
{
	return lines != NULL && g_strv_contains ((const char * const *) lines, line);
}

static void
check_copy (const char *how, char **lines)
{
	char *parent = g_strdup_printf ("parent=%lu", (unsigned long) GetCurrentProcessId ());

	if (lines == NULL) {
		g_printerr ("FAIL: %s: the copy never wrote its report\n", how);
		failures++;
		g_free (parent);
		return;
	}
	if (!has_line (lines, parent)) {
		g_printerr ("FAIL: %s: started by something other than the app (%s)\n", how, lines[0]);
		failures++;
	}
	check (has_line (lines, "bus=" USERS_BUS));
	check (has_line (lines, "freetype=unset"));
	check (has_line (lines, "passed=yes"));
	check (helpers_of (GetCurrentProcessId ()) == 0);
	check (g_strcmp0 (g_getenv ("DBUS_SESSION_BUS_ADDRESS"), "disabled:") == 0);
	check (g_getenv ("FREETYPE_PROPERTIES") != NULL);
	g_free (parent);
}

static char *
make_folder (const char *scratch, const char *name)
{
	char *dir = g_build_filename (scratch, name, NULL);

	check (g_mkdir (dir, 0700) == 0);
	return dir;
}

/* Wine reads a scheme from the machine's classes only, where Windows takes
 * the user's too. */
static HKEY
classes_root (void)
{
	HMODULE ntdll = GetModuleHandleW (L"ntdll.dll");

	return ntdll != NULL && GetProcAddress (ntdll, "wine_get_version") != NULL
		? HKEY_CLASSES_ROOT : HKEY_CURRENT_USER;
}

/* A scheme of the test's own, for the user only, that runs this program. */
static gboolean
register_scheme (HKEY root, const wchar_t *key, const char *self)
{
	wchar_t *wself = g_utf8_to_utf16 (self, -1, NULL, NULL, NULL);
	wchar_t command[2048];
	wchar_t sub[256];
	HKEY handle;
	gboolean ok;

	_snwprintf (command, G_N_ELEMENTS (command), L"\"%ls\" --uri \"%%1\"", wself);
	command[G_N_ELEMENTS (command) - 1] = L'\0';
	g_free (wself);

	ok = RegCreateKeyExW (root, key, 0, NULL, 0, KEY_WRITE, NULL, &handle, NULL) == ERROR_SUCCESS;
	if (ok) {
		ok = RegSetValueExW (handle, L"URL Protocol", 0, REG_SZ, (const BYTE *) L"", sizeof (wchar_t)) == ERROR_SUCCESS;
		RegCloseKey (handle);
	}
	_snwprintf (sub, G_N_ELEMENTS (sub), L"%ls\\shell\\open\\command", key);
	sub[G_N_ELEMENTS (sub) - 1] = L'\0';
	if (ok) {
		ok = RegCreateKeyExW (root, sub, 0, NULL, 0, KEY_WRITE, NULL, &handle, NULL) == ERROR_SUCCESS;
	}
	if (ok) {
		ok = RegSetValueExW (handle, NULL, 0, REG_SZ, (const BYTE *) command,
				     (DWORD) ((wcslen (command) + 1) * sizeof (wchar_t))) == ERROR_SUCCESS;
		RegCloseKey (handle);
	}
	return ok;
}

/* The scheme's link holds the report folder in hex, so nothing on the way
 * can change it. */
static int
report_for_uri (const char *uri, int argc, char *argv[])
{
	const char *colon = strchr (uri, ':');
	GString *dir = g_string_new (NULL);
	int code;

	for (const char *p = colon != NULL ? colon + 1 : ""; g_ascii_isxdigit (p[0]) && g_ascii_isxdigit (p[1]); p += 2) {
		g_string_append_c (dir, (char) (g_ascii_xdigit_value (p[0]) * 16 + g_ascii_xdigit_value (p[1])));
	}
	code = dir->len > 0 ? report (dir->str, argc, argv) : 1;
	g_string_free (dir, TRUE);
	return code;
}

int
main (int argc, char *argv[])
{
	g_autofree char *scratch = NULL;
	g_autofree char *self = NULL;
	g_autofree char *started_in = copy_folder (argc, argv);
	GError *error = NULL;
	char **lines;

	if (started_in != NULL) {
		return report (started_in, argc, argv);
	}
	if (argc == 3 && strcmp (argv[1], "--uri") == 0) {
		return report_for_uri (argv[2], argc, argv);
	}

	scratch = test_scratch_dir ("nemo-new-copy-XXXXXX", NULL);
	self = g_canonicalize_filename (argv[0], NULL);
	if (scratch == NULL) {
		g_printerr ("FAIL: no scratch folder\n");
		return 1;
	}

	/* As the user had it before the app started, then as nemo-main.c sets it. */
	g_setenv ("DBUS_SESSION_BUS_ADDRESS", USERS_BUS, TRUE);
	g_unsetenv ("FREETYPE_PROPERTIES");
	g_unsetenv ("GDK_WIN32_USE_EXPERIMENTAL_OLE2_DND");
	nemo_setup_runtime_environment ();
	nemo_setenv_own ("FREETYPE_PROPERTIES", "truetype:interpreter-version=35", FALSE);
	nemo_dnd_win32_prepare ();
	g_setenv (PASSED_ON, "yes", TRUE);

	/* A folder opened in a new window. */
	{
		g_autofree char *dir = make_folder (scratch, "window");
		GFile *location = g_file_new_for_path (dir);

		check (nemo_new_process_spawn (location, NULL, &error));
		g_clear_error (&error);
		lines = wait_for_report (dir);
		check_copy ("new window", lines);
		g_strfreev (lines);
		g_object_unref (location);
	}

	/* An item opened in a new window shows its folder with it selected. */
	{
		g_autofree char *dir = make_folder (scratch, "with select");
		g_autofree char *item = g_build_filename (dir, "picked.txt", NULL);
		GFile *location = g_file_new_for_path (dir);
		GFile *selection = g_file_new_for_path (item);

		check (g_file_set_contents (item, "x", 1, NULL));
		check (nemo_new_process_spawn (location, selection, &error));
		g_clear_error (&error);
		lines = wait_for_report (dir);
		check_copy ("new window with a selection", lines);
		check (has_line (lines, "arg=--select"));
		g_strfreev (lines);
		g_object_unref (selection);
		g_object_unref (location);
	}

	/* A tab moved out to a window of its own keeps its view and selection. */
	{
		g_autofree char *dir = make_folder (scratch, "tab 100%");
		GFile *location = g_file_new_for_path (dir);
		char *selected[] = { (char *) "a \"quoted\" name.txt", NULL };

		check (nemo_new_process_spawn_tab (location, "OAFIID:Nemo_File_Manager_List_View", selected, &error));
		g_clear_error (&error);
		lines = wait_for_report (dir);
		check_copy ("tab moved out", lines);
		check (has_line (lines, "arg=--tab-view"));
		check (has_line (lines, "arg=OAFIID:Nemo_File_Manager_List_View"));
		check (has_line (lines, "arg=a \"quoted\" name.txt"));
		g_strfreev (lines);
		g_object_unref (location);
	}

	/* A link whose scheme nothing handles fails, and starts nothing. */
	{
		check (!nemo_launch_win32_open_uri ("nemo-test-nothing-here:x", &error));
		check (g_error_matches (error, G_IO_ERROR, G_IO_ERROR_NOT_SUPPORTED));
		g_clear_error (&error);
		check (!nemo_launch_win32_open_uri ("help:gnome-help/files", &error));
		g_clear_error (&error);
		check (helpers_of (GetCurrentProcessId ()) == 0);
	}

	/* A link the user has a program for opens in it. */
	{
		g_autofree char *dir = make_folder (scratch, "link");
		HKEY root = classes_root ();
		wchar_t key[96];
		wchar_t *wscheme;
		GString *uri = g_string_new (NULL);

		g_string_printf (uri, "nemo-test-%lu:", (unsigned long) GetCurrentProcessId ());
		wscheme = g_utf8_to_utf16 (uri->str, (glong) uri->len - 1, NULL, NULL, NULL);
		_snwprintf (key, G_N_ELEMENTS (key), root == HKEY_CURRENT_USER ? L"Software\\Classes\\%ls" : L"%ls",
			    wscheme);
		key[G_N_ELEMENTS (key) - 1] = L'\0';
		g_free (wscheme);
		for (const char *p = dir; *p != '\0'; p++) {
			g_string_append_printf (uri, "%02x", (guchar) *p);
		}

		if (register_scheme (root, key, self)) {
			check (nemo_launch_win32_open_uri (uri->str, &error));
			if (error != NULL) {
				g_printerr ("%s\n", error->message);
			}
			g_clear_error (&error);
			lines = wait_for_report (dir);
			check (lines != NULL);
			g_strfreev (lines);
			check (helpers_of (GetCurrentProcessId ()) == 0);
		} else {
			g_printerr ("FAIL: could not set up a scheme for the user\n");
			failures++;
		}
		RegDeleteTreeW (root, key);
		g_string_free (uri, TRUE);
	}

	if (failures == 0) {
		g_print ("new copy start: all checks passed\n");
	}
	return failures == 0 ? 0 : 1;
}
