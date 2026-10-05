/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */

/* nemo-xattr-win32.c - alternate data streams, the Windows answer to xattrs.

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

#include "nemo-xattr-win32.h"
#include "nemo-file-xattr.h"

#include <windows.h>
#include <string.h>

/* Names the stream, with the prefix that lifts the 260-character limit. A
 * relative path cannot take the prefix, so it goes without and takes the
 * limit. */
static wchar_t *
stream_path (const char *path, const char *name)
{
	g_autofree char *spec = NULL;

	if (g_path_is_absolute (path) && !g_str_has_prefix (path, "\\\\?\\")) {
		g_autofree char *win = g_strdup (path);
		char *p;

		for (p = win; *p != '\0'; p++) {
			if (*p == '/')
				*p = '\\';
		}

		spec = g_str_has_prefix (win, "\\\\")
			? g_strconcat ("\\\\?\\UNC", win + 1, ":", name, NULL)
			: g_strconcat ("\\\\?\\", win, ":", name, NULL);
	} else {
		spec = g_strconcat (path, ":", name, NULL);
	}

	return (wchar_t *) g_utf8_to_utf16 (spec, -1, NULL, NULL, NULL);
}

/* Returns: (transfer full): free with g_free */
char *
nemo_xattr_win32_get (const char *path, const char *name)
{
	g_autofree wchar_t *wide = NULL;
	char   buffer[NEMO_FILE_XATTR_MAX];
	HANDLE handle;
	DWORD  got = 0;

	g_return_val_if_fail (path != NULL, NULL);
	g_return_val_if_fail (name != NULL, NULL);

	wide = stream_path (path, name);
	if (wide == NULL)
		return NULL;

	handle = CreateFileW (wide, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
			      NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
	if (handle == INVALID_HANDLE_VALUE)
		return NULL;

	if (!ReadFile (handle, buffer, sizeof (buffer) - 1, &got, NULL))
		got = 0;

	CloseHandle (handle);

	if (got == 0)
		return NULL;

	buffer[got] = '\0';

	/* Anything with a NUL in it was not written by us. */
	if (strlen (buffer) != got)
		return NULL;

	return g_strdup (buffer);
}

gboolean
nemo_xattr_win32_set (const char *path, const char *name, const char *value)
{
	g_autofree wchar_t *wide = NULL;
	const FILETIME keep = { 0xFFFFFFFF, 0xFFFFFFFF };
	HANDLE handle;
	DWORD  len;
	DWORD  written = 0;
	BOOL   ok;

	g_return_val_if_fail (path != NULL, FALSE);
	g_return_val_if_fail (name != NULL, FALSE);
	g_return_val_if_fail (value != NULL, FALSE);

	wide = stream_path (path, name);
	if (wide == NULL)
		return FALSE;

	/* CREATE_ALWAYS truncates, so a shorter value does not leave the tail of
	 * a longer one behind it. */
	handle = CreateFileW (wide, GENERIC_WRITE, FILE_SHARE_READ,
			      NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
	if (handle == INVALID_HANDLE_VALUE)
		return FALSE;

	/* NTFS counts a write to any stream as a change to the file, so without
	 * this the checksum would move the very time it was taken at, and the file
	 * would read as edited. All ones tells the handle to leave a time alone. */
	SetFileTime (handle, NULL, &keep, &keep);

	len = (DWORD) strlen (value);
	ok = WriteFile (handle, value, len, &written, NULL);

	CloseHandle (handle);

	return ok && written == len;
}

#endif /* G_OS_WIN32 */
