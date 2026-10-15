/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */

/* test-env-dump.c - writes down who started it and the environment it got,
   for the launch tests.

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

/* Explorer and the management service start a program with their own PATH,
 * which has none of the build's libraries on it, so this one links none. A
 * test program started that way stops at Windows' "was not found" box.
 *
 *   test-env-dump <file>              writes <file>
 *   test-env-dump --uri <scheme:hex>  writes env-dump.txt in the folder the hex
 *                                     spells, as UTF-8 bytes
 *
 * Lines are parent=<pid>, parent_name=<its exe>, arg=<each argument>, then
 * NAME=VALUE for every variable, in UTF-8. Written aside and renamed, so a reader never sees half. */

#include <stdio.h>
#include <string.h>
#include <wchar.h>
#include <windows.h>
#include <shellapi.h>
#include <tlhelp32.h>

#define URI_NAME L"env-dump.txt"

/* The parent's id, and its exe's name in @name. */
static DWORD
parent_of_self (wchar_t *name, size_t size)
{
	HANDLE snapshot = CreateToolhelp32Snapshot (TH32CS_SNAPPROCESS, 0);
	PROCESSENTRY32W entry;
	DWORD self = GetCurrentProcessId (), parent = 0;

	name[0] = L'\0';
	if (snapshot == INVALID_HANDLE_VALUE) {
		return 0;
	}
	entry.dwSize = sizeof entry;
	for (BOOL more = Process32FirstW (snapshot, &entry); more; more = Process32NextW (snapshot, &entry)) {
		if (entry.th32ProcessID == self) {
			parent = entry.th32ParentProcessID;
		}
	}
	entry.dwSize = sizeof entry;
	for (BOOL more = Process32FirstW (snapshot, &entry); more && parent != 0; more = Process32NextW (snapshot, &entry)) {
		if (entry.th32ProcessID == parent) {
			_snwprintf (name, size, L"%ls", entry.szExeFile);
			name[size - 1] = L'\0';
		}
	}
	CloseHandle (snapshot);
	return parent;
}

static void
put_utf8 (FILE *out, const wchar_t *text)
{
	char buffer[32768];
	int size = WideCharToMultiByte (CP_UTF8, 0, text, -1, buffer, sizeof buffer, NULL, NULL);

	if (size > 1) {
		fwrite (buffer, 1, (size_t) size - 1, out);
	}
}

static int
dump (const wchar_t *path, int argc, wchar_t **argv)
{
	wchar_t aside[MAX_PATH + 16];
	wchar_t parent_name[MAX_PATH];
	wchar_t *block = GetEnvironmentStringsW ();
	DWORD parent;
	FILE *out;

	if (_snwprintf (aside, MAX_PATH + 16, L"%ls.part", path) < 0) {
		return 1;
	}
	aside[MAX_PATH + 15] = L'\0';
	out = _wfopen (aside, L"wb");
	if (out == NULL) {
		return 1;
	}

	parent = parent_of_self (parent_name, MAX_PATH);
	fprintf (out, "parent=%lu\nparent_name=", (unsigned long) parent);
	put_utf8 (out, parent_name);
	fputc ('\n', out);
	for (int i = 1; i < argc; i++) {
		fputs ("arg=", out);
		put_utf8 (out, argv[i]);
		fputc ('\n', out);
	}
	/* A leading '=' is cmd's per-drive folder, not a variable. */
	for (const wchar_t *p = block; p != NULL && *p != L'\0'; p += wcslen (p) + 1) {
		if (*p != L'=') {
			put_utf8 (out, p);
			fputc ('\n', out);
		}
	}
	FreeEnvironmentStringsW (block);

	if (fclose (out) != 0) {
		return 1;
	}
	return MoveFileExW (aside, path, MOVEFILE_REPLACE_EXISTING) ? 0 : 1;
}

static int
hexval (wchar_t c)
{
	if (c >= L'0' && c <= L'9') {
		return c - L'0';
	}
	if (c >= L'a' && c <= L'f') {
		return c - L'a' + 10;
	}
	if (c >= L'A' && c <= L'F') {
		return c - L'A' + 10;
	}
	return -1;
}

static int
dump_for_uri (const wchar_t *uri, int argc, wchar_t **argv)
{
	const wchar_t *colon = wcschr (uri, L':');
	char bytes[MAX_PATH];
	wchar_t dir[MAX_PATH], path[MAX_PATH + 16];
	size_t length = 0;

	for (const wchar_t *p = colon != NULL ? colon + 1 : L""; hexval (p[0]) >= 0 && hexval (p[1]) >= 0; p += 2) {
		if (length + 1 >= sizeof bytes) {
			return 1;
		}
		bytes[length++] = (char) (hexval (p[0]) * 16 + hexval (p[1]));
	}
	bytes[length] = '\0';
	if (length == 0 || MultiByteToWideChar (CP_UTF8, 0, bytes, -1, dir, MAX_PATH) == 0) {
		return 1;
	}
	if (_snwprintf (path, MAX_PATH + 16, L"%ls\\%ls", dir, URI_NAME) < 0) {
		return 1;
	}
	path[MAX_PATH + 15] = L'\0';
	return dump (path, argc, argv);
}

int
main (void)
{
	int argc = 0;
	wchar_t **argv = CommandLineToArgvW (GetCommandLineW (), &argc);
	int code = 2;

	if (argv != NULL && argc == 3 && wcscmp (argv[1], L"--uri") == 0) {
		code = dump_for_uri (argv[2], argc, argv);
	} else if (argv != NULL && argc == 2) {
		code = dump (argv[1], argc, argv);
	}
	LocalFree (argv);
	return code;
}
