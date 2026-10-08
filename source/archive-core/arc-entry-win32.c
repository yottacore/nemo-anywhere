/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */

/* arc-entry-win32.c - reparse points, read without going through them.

   Copyright © 2026 t00mietum (CryptogID: ปʬϝღถɔ4რఠΔթะ9ƾǝu).

   This program is free software; you can redistribute it and/or
   modify it under the terms of the GNU General Public License as
   published by the Free Software Foundation; version 2 of the
   License.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, see <http://www.gnu.org/licenses/>.
*/

#include <glib.h>

#ifdef G_OS_WIN32

#include <windows.h>

#include "arc-entry.h"
#include "arc-mounts.h"

#ifndef IO_REPARSE_TAG_MOUNT_POINT
#define IO_REPARSE_TAG_MOUNT_POINT 0xA0000003
#endif
#ifndef IO_REPARSE_TAG_SYMLINK
#define IO_REPARSE_TAG_SYMLINK 0xA000000C
#endif

/* The real one is in ntifs.h, a kernel header. Both kinds start with the
   same 4 offsets; a symlink has a flags word before its names. */
typedef struct {
	DWORD ReparseTag;
	WORD  ReparseDataLength;
	WORD  Reserved;
	WORD  SubstituteNameOffset;
	WORD  SubstituteNameLength;
	WORD  PrintNameOffset;
	WORD  PrintNameLength;
} ReparseHead;

#define SYMLINK_NAMES_AT     (sizeof (ReparseHead) + sizeof (ULONG))
#define MOUNT_POINT_NAMES_AT (sizeof (ReparseHead))

/* \\?\ gets a path past MAX_PATH, and wants backslashes. */
static wchar_t *
long_form (const char *path)
{
	GString *text = g_string_new (NULL);
	wchar_t *wide;
	gsize i;

	if (g_str_has_prefix (path, "\\\\?\\") || g_str_has_prefix (path, "\\\\.\\")) {
		g_string_append (text, path);
	} else if ((path[0] == '\\' || path[0] == '/') && (path[1] == '\\' || path[1] == '/')) {
		g_string_append (text, "\\\\?\\UNC\\");
		g_string_append (text, path + 2);
	} else if (g_ascii_isalpha (path[0]) && path[1] == ':' && (path[2] == '\\' || path[2] == '/')) {
		g_string_append (text, "\\\\?\\");
		g_string_append (text, path);
	} else {
		g_string_append (text, path);
	}
	for (i = 0; i < text->len; i++) {
		if (text->str[i] == '/') {
			text->str[i] = '\\';
		}
	}

	wide = g_utf8_to_utf16 (text->str, -1, NULL, NULL, NULL);
	g_string_free (text, TRUE);
	return wide;
}

/* \??\C:\x is C:\x and \??\UNC\server\x is \\server\x. */
static char *
plain_form (char *target)
{
	char *plain;

	if (g_ascii_strncasecmp (target, "\\??\\UNC\\", 8) == 0 || g_ascii_strncasecmp (target, "\\\\?\\UNC\\", 8) == 0) {
		plain = g_strconcat ("\\\\", target + 8, NULL);
	} else if ((g_str_has_prefix (target, "\\??\\") || g_str_has_prefix (target, "\\\\?\\")) &&
		   g_ascii_isalpha (target[4]) && target[5] == ':') {
		plain = g_strdup (target + 4);
	} else {
		return target;
	}
	g_free (target);
	return plain;
}

ArcEntry
arc_entry_read (const char  *path,
		char       **target)
{
	wchar_t *wide = long_form (path);
	DWORD attributes, returned = 0;
	HANDLE handle;
	guint8 *buffer;
	const ReparseHead *head;
	gsize names_at;
	char *substitute;
	glong units;
	ArcEntry entry;

	*target = NULL;
	if (wide == NULL) {
		return ARC_ENTRY_MISSING;
	}

	attributes = GetFileAttributesW (wide);
	if (attributes == INVALID_FILE_ATTRIBUTES) {
		g_free (wide);
		return ARC_ENTRY_MISSING;
	}
	if (!(attributes & FILE_ATTRIBUTE_REPARSE_POINT)) {
		g_free (wide);
		return ARC_ENTRY_PLAIN;
	}

	handle = CreateFileW (wide, 0, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL,
			      OPEN_EXISTING, FILE_FLAG_OPEN_REPARSE_POINT | FILE_FLAG_BACKUP_SEMANTICS, NULL);
	g_free (wide);
	if (handle == INVALID_HANDLE_VALUE) {
		return ARC_ENTRY_MISSING;
	}
	buffer = g_malloc0 (MAXIMUM_REPARSE_DATA_BUFFER_SIZE);
	if (!DeviceIoControl (handle, FSCTL_GET_REPARSE_POINT, NULL, 0, buffer,
			      MAXIMUM_REPARSE_DATA_BUFFER_SIZE, &returned, NULL) ||
	    returned < sizeof (ReparseHead)) {
		CloseHandle (handle);
		g_free (buffer);
		return ARC_ENTRY_MISSING;
	}
	CloseHandle (handle);

	head = (const ReparseHead *) (const void *) buffer;
	if (head->ReparseTag == IO_REPARSE_TAG_SYMLINK) {
		names_at = SYMLINK_NAMES_AT;
	} else if (head->ReparseTag == IO_REPARSE_TAG_MOUNT_POINT) {
		names_at = MOUNT_POINT_NAMES_AT;
	} else {
		/* Cloud placeholders, dedup and the like: files and folders. */
		g_free (buffer);
		return ARC_ENTRY_PLAIN;
	}
	if (names_at + head->SubstituteNameOffset + head->SubstituteNameLength > returned) {
		g_free (buffer);
		return ARC_ENTRY_MISSING;
	}

	/* The substitute name, not the print name, which some tools leave
	   empty or set to something else. A relative symlink has its own text
	   there. */
	units = head->SubstituteNameLength / sizeof (WCHAR);	/* the length is in bytes */
	substitute = g_utf16_to_utf8 ((const gunichar2 *) (const void *) (buffer + names_at + head->SubstituteNameOffset),
				      units, NULL, NULL, NULL);
	if (substitute == NULL) {
		entry = ARC_ENTRY_MISSING;
	} else if (head->ReparseTag == IO_REPARSE_TAG_MOUNT_POINT && arc_target_is_volume (substitute)) {
		entry = ARC_ENTRY_MOUNT_POINT;
		g_free (substitute);
	} else {
		entry = head->ReparseTag == IO_REPARSE_TAG_SYMLINK ? ARC_ENTRY_SYMLINK : ARC_ENTRY_JUNCTION;
		*target = plain_form (substitute);
	}

	g_free (buffer);
	return entry;
}

#endif /* G_OS_WIN32 */
