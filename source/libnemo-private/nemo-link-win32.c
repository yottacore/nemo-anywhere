/* nemo-link-win32.c - real file-system links on Windows.
 *
 * Copyright © 2026 t00mietum (CryptogID: ปʬϝღถɔ4რఠΔթะ9ƾǝu)
 *
 * This program is free software; you can redistribute it and/or modify it under
 * the terms of the GNU General Public License version 2, as published by the
 * Free Software Foundation.
 */

#include <config.h>
#include "nemo-link-win32.h"

#ifdef G_OS_WIN32

#include <string.h>
#include <gio/gio.h>
#include <glib/gi18n.h>
#include <glib/gstdio.h>

#include <windows.h>

#ifndef SYMBOLIC_LINK_FLAG_ALLOW_UNPRIVILEGED_CREATE
#define SYMBOLIC_LINK_FLAG_ALLOW_UNPRIVILEGED_CREATE 0x2
#endif

#ifndef IO_REPARSE_TAG_MOUNT_POINT
#define IO_REPARSE_TAG_MOUNT_POINT 0xA0000003
#endif
#ifndef IO_REPARSE_TAG_SYMLINK
#define IO_REPARSE_TAG_SYMLINK 0xA000000C
#endif
#ifndef SYMLINK_FLAG_RELATIVE
#define SYMLINK_FLAG_RELATIVE 0x1
#endif

/* The reparse point a link is made of. The real declaration is in ntifs.h, a
   kernel header, so it gets spelled out here the way every user-mode program
   that reads or writes one has to. */
typedef struct {
	DWORD ReparseTag;
	WORD  ReparseDataLength;
	WORD  Reserved;
	union {
		struct {
			WORD  SubstituteNameOffset;
			WORD  SubstituteNameLength;
			WORD  PrintNameOffset;
			WORD  PrintNameLength;
			ULONG Flags;
			WCHAR PathBuffer[1];
		} SymbolicLink;
		struct {
			WORD  SubstituteNameOffset;
			WORD  SubstituteNameLength;
			WORD  PrintNameOffset;
			WORD  PrintNameLength;
			WCHAR PathBuffer[1];
		} MountPoint;
	} u;
} ReparseBuffer;

/* Through PrintNameLength - what precedes the two names in a mount point. */
#define MOUNT_POINT_HEADER_BYTES 16

static gunichar2 *
to_utf16 (const char *s)
{
	if (s == NULL) {
		return NULL;
	}
	return g_utf8_to_utf16 (s, -1, NULL, NULL, NULL);
}

static char *
to_native_separators (const char *path)
{
	char *native, *walk;

	if (path == NULL) {
		return NULL;
	}

	native = g_strdup (path);
	for (walk = native; *walk != '\0'; walk++) {
		if (*walk == '/') {
			*walk = '\\';
		}
	}

	return native;
}

NemoLinkKind
nemo_win32_link_kind (const char *path)
{
	gunichar2 *w_path;
	WIN32_FIND_DATAW found;
	HANDLE handle;
	NemoLinkKind kind = NEMO_LINK_NONE;

	if (path == NULL) {
		return NEMO_LINK_NONE;
	}

	w_path = to_utf16 (path);
	if (w_path == NULL) {
		return NEMO_LINK_NONE;
	}

	/* Reads the directory entry rather than opening the file, so a link whose
	   target is not answering costs nothing. */
	handle = FindFirstFileW ((LPCWSTR) w_path, &found);
	g_free (w_path);
	if (handle == INVALID_HANDLE_VALUE) {
		return NEMO_LINK_NONE;
	}
	FindClose (handle);

	if (!(found.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT)) {
		return NEMO_LINK_NONE;
	}

	if (found.dwReserved0 == IO_REPARSE_TAG_MOUNT_POINT) {
		kind = NEMO_LINK_JUNCTION;
	} else if (found.dwReserved0 == IO_REPARSE_TAG_SYMLINK) {
		kind = (found.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
			? NEMO_LINK_DIR_SYMLINK
			: NEMO_LINK_FILE_SYMLINK;
	}

	return kind;
}

gboolean
nemo_win32_link_read_target (const char  *link_path,
                             char       **target,
                             GError     **error)
{
	gunichar2 *w_path;
	HANDLE handle;
	char buffer[MAXIMUM_REPARSE_DATA_BUFFER_SIZE];
	ReparseBuffer *reparse = (ReparseBuffer *) buffer;
	DWORD returned = 0;
	const WCHAR *names;
	WORD print_offset, print_length, subst_offset, subst_length;
	gsize offset, length;
	glong chars;

	g_return_val_if_fail (target != NULL, FALSE);
	*target = NULL;

	w_path = to_utf16 (link_path);
	if (w_path == NULL) {
		g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_INVALID_FILENAME,
				     _("The link name could not be read."));
		return FALSE;
	}

	handle = CreateFileW ((LPCWSTR) w_path, 0,
			      FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
			      NULL, OPEN_EXISTING,
			      FILE_FLAG_OPEN_REPARSE_POINT | FILE_FLAG_BACKUP_SEMANTICS, NULL);
	g_free (w_path);
	if (handle == INVALID_HANDLE_VALUE) {
		g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED,
			     _("Could not read the link (error %lu)."),
			     (unsigned long) GetLastError ());
		return FALSE;
	}

	if (!DeviceIoControl (handle, FSCTL_GET_REPARSE_POINT, NULL, 0,
			      buffer, sizeof (buffer), &returned, NULL)) {
		g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED,
			     _("Could not read the link (error %lu)."),
			     (unsigned long) GetLastError ());
		CloseHandle (handle);
		return FALSE;
	}
	CloseHandle (handle);

	if (reparse->ReparseTag == IO_REPARSE_TAG_SYMLINK) {
		names = reparse->u.SymbolicLink.PathBuffer;
		print_offset = reparse->u.SymbolicLink.PrintNameOffset;
		print_length = reparse->u.SymbolicLink.PrintNameLength;
		subst_offset = reparse->u.SymbolicLink.SubstituteNameOffset;
		subst_length = reparse->u.SymbolicLink.SubstituteNameLength;
	} else if (reparse->ReparseTag == IO_REPARSE_TAG_MOUNT_POINT) {
		names = reparse->u.MountPoint.PathBuffer;
		print_offset = reparse->u.MountPoint.PrintNameOffset;
		print_length = reparse->u.MountPoint.PrintNameLength;
		subst_offset = reparse->u.MountPoint.SubstituteNameOffset;
		subst_length = reparse->u.MountPoint.SubstituteNameLength;
	} else {
		g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_NOT_SUPPORTED,
				     _("That is not a link."));
		return FALSE;
	}

	/* The print name is the readable spelling and is what a relative symlink
	   carries; some links leave it empty, and then the substitute name with its
	   \??\ prefix taken off is the same thing. */
	if (print_length > 0) {
		offset = print_offset;
		length = print_length;
	} else {
		offset = subst_offset;
		length = subst_length;
	}

	/* The lengths the reparse point records are in bytes; the conversion counts
	   in UTF-16 units. */
	chars = length / sizeof (WCHAR);
	*target = g_utf16_to_utf8 ((const gunichar2 *) ((const char *) names + offset),
				   chars, NULL, NULL, NULL);
	if (*target == NULL) {
		g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_FAILED,
				     _("Could not read the link."));
		return FALSE;
	}

	if (print_length == 0 && g_str_has_prefix (*target, "\\??\\")) {
		char *stripped = g_strdup (*target + 4);
		g_free (*target);
		*target = stripped;
	}

	return TRUE;
}

/* A junction can only point at a fully qualified local path - not a relative
   name and not a share. Returns the target spelled the way the reparse point
   wants it, or NULL when a junction is not an option. */
static char *
junction_target (const char *target_path)
{
	char *native;
	gsize len;

	if (target_path == NULL || !g_ascii_isalpha (target_path[0]) || target_path[1] != ':') {
		return NULL;
	}

	native = to_native_separators (target_path);

	/* A trailing separator makes the mount point resolve oddly. */
	len = strlen (native);
	while (len > 3 && native[len - 1] == '\\') {
		native[--len] = '\0';
	}

	return native;
}

/* Make link_path a junction pointing at target_path. Junctions carry no
   privilege requirement, which is the whole reason to prefer one: a folder link
   works with Developer Mode off and without running elevated. */
static gboolean
try_junction (const char *target_path,
              const char *link_path,
              DWORD      *win_error)
{
	char *native = junction_target (target_path);
	char *prefixed;
	gunichar2 *substitute, *print_name, *w_link;
	ReparseBuffer *buffer;
	gsize substitute_bytes, print_bytes, total;
	HANDLE handle;
	DWORD returned = 0;
	gboolean ok;

	*win_error = ERROR_NOT_SUPPORTED;
	if (native == NULL) {
		return FALSE;
	}

	prefixed = g_strconcat ("\\??\\", native, NULL);
	substitute = to_utf16 (prefixed);
	print_name = to_utf16 (native);
	w_link = to_utf16 (link_path);
	g_free (prefixed);
	g_free (native);

	if (substitute == NULL || print_name == NULL || w_link == NULL) {
		g_free (substitute);
		g_free (print_name);
		g_free (w_link);
		return FALSE;
	}

	if (!CreateDirectoryW ((LPCWSTR) w_link, NULL)) {
		*win_error = GetLastError ();
		g_free (substitute);
		g_free (print_name);
		g_free (w_link);
		return FALSE;
	}

	handle = CreateFileW ((LPCWSTR) w_link, GENERIC_WRITE, 0, NULL, OPEN_EXISTING,
	                      FILE_FLAG_OPEN_REPARSE_POINT | FILE_FLAG_BACKUP_SEMANTICS, NULL);
	if (handle == INVALID_HANDLE_VALUE) {
		*win_error = GetLastError ();
		RemoveDirectoryW ((LPCWSTR) w_link);
		g_free (substitute);
		g_free (print_name);
		g_free (w_link);
		return FALSE;
	}

	/* Both names sit in one buffer, each with a terminator of its own. */
	substitute_bytes = wcslen ((const wchar_t *) substitute) * sizeof (WCHAR);
	print_bytes = wcslen ((const wchar_t *) print_name) * sizeof (WCHAR);
	total = MOUNT_POINT_HEADER_BYTES + substitute_bytes + print_bytes + 2 * sizeof (WCHAR);

	buffer = g_malloc0 (total);
	buffer->ReparseTag = IO_REPARSE_TAG_MOUNT_POINT;
	buffer->ReparseDataLength = (WORD) (total - 8);
	buffer->u.MountPoint.SubstituteNameOffset = 0;
	buffer->u.MountPoint.SubstituteNameLength = (WORD) substitute_bytes;
	buffer->u.MountPoint.PrintNameOffset = (WORD) (substitute_bytes + sizeof (WCHAR));
	buffer->u.MountPoint.PrintNameLength = (WORD) print_bytes;
	wcscpy (buffer->u.MountPoint.PathBuffer, (const wchar_t *) substitute);
	wcscpy (buffer->u.MountPoint.PathBuffer + wcslen ((const wchar_t *) substitute) + 1,
	        (const wchar_t *) print_name);

	SetLastError (0);
	ok = DeviceIoControl (handle, FSCTL_SET_REPARSE_POINT, buffer, (DWORD) total,
	                      NULL, 0, &returned, NULL) != 0;
	*win_error = ok ? 0 : GetLastError ();

	CloseHandle (handle);
	if (!ok) {
		RemoveDirectoryW ((LPCWSTR) w_link);
	}

	g_free (buffer);
	g_free (substitute);
	g_free (print_name);
	g_free (w_link);

	return ok;
}

/* A relative target is stored as given, and Windows does not take / as a
   separator in one, so "sub/x" would lead nowhere. An absolute one is
   rewritten by the call anyway. */
static gboolean
try_symlink (const char *target_path,
             const char *link_path,
             gboolean    target_is_dir,
             DWORD       extra_flags,
             DWORD      *win_error)
{
	char *native = to_native_separators (target_path);
	gunichar2 *w_target = to_utf16 (native);
	gunichar2 *w_link = to_utf16 (link_path);
	DWORD flags = extra_flags;
	gboolean ok;

	g_free (native);

	if (target_is_dir) {
		flags |= SYMBOLIC_LINK_FLAG_DIRECTORY;
	}

	SetLastError (0);
	ok = CreateSymbolicLinkW ((LPCWSTR) w_link, (LPCWSTR) w_target, flags) != 0;
	*win_error = ok ? 0 : GetLastError ();

	g_free (w_target);
	g_free (w_link);

	return ok;
}

static void
set_link_error (GError **error,
                DWORD    win_error)
{
	if (win_error == ERROR_PRIVILEGE_NOT_HELD) {
		g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_PERMISSION_DENIED,
				     _("Windows allows symlinks only with Developer Mode turned on, or when running as administrator."));
	} else if (win_error == ERROR_ALREADY_EXISTS || win_error == ERROR_FILE_EXISTS) {
		/* Spelled as EXISTS so the caller can uniquify the name and try again,
		   the way the POSIX path does. */
		g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_EXISTS,
				     _("A file with that name already exists."));
	} else {
		g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED,
			     _("Could not create the link (error %lu)."),
			     (unsigned long) win_error);
	}
}

gboolean
nemo_win32_link_leads_somewhere (const char *path)
{
	gunichar2 *w_path = to_utf16 (path);
	HANDLE handle;
	DWORD win_error;

	if (w_path == NULL) {
		return FALSE;
	}

	/* No FILE_FLAG_OPEN_REPARSE_POINT, so the open goes through the link,
	   and backup semantics so a folder opens too. */
	handle = CreateFileW ((LPCWSTR) w_path, 0, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
			      NULL, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, NULL);
	win_error = handle == INVALID_HANDLE_VALUE ? GetLastError () : 0;
	g_free (w_path);

	if (handle != INVALID_HANDLE_VALUE) {
		CloseHandle (handle);
		return TRUE;
	}

	/* Something there that will not open, such as a file in use or one
	   that is not ours to read, is still somewhere. A target spelled with /
	   is the invalid name. */
	switch (win_error) {
	case ERROR_FILE_NOT_FOUND:
	case ERROR_PATH_NOT_FOUND:
	case ERROR_INVALID_NAME:
	case ERROR_BAD_NETPATH:
	case ERROR_BAD_NET_NAME:
	case ERROR_CANT_RESOLVE_FILENAME:
		return FALSE;
	default:
		return TRUE;
	}
}

/* A drive letter mapped to a share, or a subst onto one. QueryDosDevice reads
   the drive's entry in the object table, so the share itself is not asked. */
static gboolean
drive_is_remote (const char *path, gint depth)
{
	wchar_t drive[3] = { 0, L':', 0 };
	wchar_t device[1024];
	char *name, *folded;
	gboolean remote;

	if (path == NULL || !g_ascii_isalpha (path[0]) || path[1] != ':' || depth > 4) {
		return FALSE;
	}

	drive[0] = (wchar_t) g_ascii_toupper (path[0]);
	if (QueryDosDeviceW (drive, device, G_N_ELEMENTS (device)) == 0) {
		return FALSE;
	}

	name = g_utf16_to_utf8 ((const gunichar2 *) device, -1, NULL, NULL, NULL);
	if (name == NULL) {
		return FALSE;
	}

	folded = g_ascii_strdown (name, -1);
	if (g_str_has_prefix (folded, "\\??\\unc\\")) {
		remote = TRUE;
	} else if (g_str_has_prefix (folded, "\\??\\")) {
		remote = drive_is_remote (name + 4, depth + 1);
	} else {
		/* \Device\Mup, LanmanRedirector, WebDavRedirector, RdpDr, VBoxMiniRdr */
		remote = g_str_has_prefix (folded, "\\device\\mup") ||
			 strstr (folded, "redirector") != NULL ||
			 strstr (folded, "rdr") != NULL;
	}

	g_free (folded);
	g_free (name);

	return remote;
}

gboolean
nemo_win32_drive_is_remote (const char *path)
{
	return drive_is_remote (path, 0);
}

/* A target that names a share, in any spelling a reparse point or a person
   may use, or a drive letter that is one. */
static gboolean
target_is_remote (const char *target)
{
	const char *rest = target;

	if (g_str_has_prefix (target, "\\??\\") || g_str_has_prefix (target, "\\\\?\\") ||
	    g_str_has_prefix (target, "\\\\.\\")) {
		rest = target + 4;
		if (g_ascii_strncasecmp (rest, "UNC\\", 4) == 0) {
			return TRUE;
		}
	} else if ((target[0] == '\\' || target[0] == '/') && (target[1] == '\\' || target[1] == '/')) {
		return TRUE;
	}

	return drive_is_remote (rest, 0);
}

/* A name's reparse tag, or 0, read from the listing of the folder above it,
   so a link is not followed and nothing behind it is opened. FALSE when
   nothing has that name. */
static gboolean
read_entry (const char *path,
            DWORD      *tag)
{
	gunichar2 *w_path = to_utf16 (path);
	WIN32_FIND_DATAW found;
	HANDLE handle;

	if (w_path == NULL) {
		return FALSE;
	}
	handle = FindFirstFileW ((LPCWSTR) w_path, &found);
	g_free (w_path);
	if (handle == INVALID_HANDLE_VALUE) {
		return FALSE;
	}
	FindClose (handle);

	*tag = (found.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) ? found.dwReserved0 : 0;

	return TRUE;
}

/* Puts a target's parts in front of what is left to walk. */
static void
push_parts (GQueue     *parts,
            const char *path,
            const char *separators)
{
	char **split = g_strsplit_set (path, separators, -1);
	int i;

	for (i = g_strv_length (split) - 1; i >= 0; i--) {
		if (split[i][0] != '\0') {
			g_queue_push_head (parts, split[i]);
		} else {
			g_free (split[i]);
		}
	}
	g_free (split);
}

/* "C:\" for anything on drive C, or NULL when path is not on a drive. */
static char *
drive_root (const char *path)
{
	if (path == NULL || !g_ascii_isalpha (path[0]) || path[1] != ':') {
		return NULL;
	}
	return g_strdup_printf ("%c:\\", path[0]);
}

#define MAX_LINK_HOPS 63	/* what Windows itself follows */

gboolean
nemo_win32_link_leads_nowhere_here (const char *link_path)
{
	GQueue parts = G_QUEUE_INIT;
	char *walked;
	char *part;
	int hops = 0;
	gboolean nowhere = FALSE;

	if (link_path == NULL || target_is_remote (link_path)) {
		return FALSE;
	}

	/* The folder the link sits in is where the user already is. Every name
	   from there on is read from the listing of the folder above it, and
	   each link's target from its own reparse point. */
	walked = g_path_get_dirname (link_path);
	g_queue_push_head (&parts, g_path_get_basename (link_path));

	while ((part = g_queue_pop_head (&parts)) != NULL) {
		char *candidate, *target = NULL, *root;
		const char *rest;
		DWORD tag;

		if (strcmp (part, ".") == 0) {
			g_free (part);
			continue;
		}
		if (strcmp (part, "..") == 0) {
			char *up = g_path_get_dirname (walked);

			g_free (walked);
			walked = up;
			g_free (part);
			continue;
		}

		/* No name holds these. The lookup below would read / as a
		   separator and the rest as a pattern. */
		if (strpbrk (part, "/*?<>\"|") != NULL) {
			nowhere = TRUE;
			g_free (part);
			break;
		}

		candidate = g_build_filename (walked, part, NULL);
		g_free (part);

		if (!read_entry (candidate, &tag)) {
			nowhere = TRUE;
			g_free (candidate);
			break;
		}
		if (tag != IO_REPARSE_TAG_SYMLINK && tag != IO_REPARSE_TAG_MOUNT_POINT) {
			g_free (walked);
			walked = candidate;
			continue;
		}

		if (++hops > MAX_LINK_HOPS) {
			nowhere = TRUE;
			g_free (candidate);
			break;
		}
		/* Not knowing is not broken. */
		if (!nemo_win32_link_read_target (candidate, &target, NULL) ||
		    target_is_remote (target)) {
			g_free (target);
			g_free (candidate);
			break;
		}

		rest = target;
		if (g_str_has_prefix (rest, "\\??\\") || g_str_has_prefix (rest, "\\\\?\\")) {
			rest += 4;
		}
		root = drive_root (rest);
		if (root != NULL) {
			g_free (walked);
			walked = root;
			push_parts (&parts, rest + 2, "\\/");
		} else if (rest[0] == '\\' || rest[0] == '/') {
			root = drive_root (walked);
			if (root == NULL) {
				g_free (target);
				g_free (candidate);
				break;
			}
			g_free (walked);
			walked = root;
			push_parts (&parts, rest + 1, "\\/");
		} else if (rest[0] != '\0' && rest[1] == ':') {
			/* "C:name", relative to a drive's own folder. Nothing makes
			   one, and nothing here can say where it goes. */
			g_free (target);
			g_free (candidate);
			break;
		} else {
			/* Relative: from the link's own folder, which is what walked
			   still is. Only \ splits it, as only \ does for Windows. */
			push_parts (&parts, rest, "\\");
		}

		g_free (target);
		g_free (candidate);
	}

	g_queue_clear_full (&parts, g_free);
	g_free (walked);

	return nowhere;
}

/* Symlinks keep whatever spelling they were given; a junction has to be
   absolute, so a relative one is resolved against the directory the original
   link sat in. */
static char *
resolve_for_kind (const char        *target,
                  const char        *base_dir,
                  NemoLinkKind  kind)
{
	if (kind != NEMO_LINK_JUNCTION || base_dir == NULL) {
		return g_strdup (target);
	}
	if (g_path_is_absolute (target)) {
		return g_strdup (target);
	}
	return g_build_filename (base_dir, target, NULL);
}

gboolean
nemo_win32_link_create (const char         *target,
                        const char         *link_path,
                        const char         *base_dir,
                        NemoLinkKind   kind,
                        GError            **error)
{
	DWORD win_error = 0;
	char *resolved;
	gboolean ok = FALSE;

	g_return_val_if_fail (target != NULL && link_path != NULL, FALSE);

	resolved = resolve_for_kind (target, base_dir, kind);

	if (kind == NEMO_LINK_JUNCTION) {
		ok = try_junction (resolved, link_path, &win_error);
	} else {
		gboolean want_dir = (kind == NEMO_LINK_DIR_SYMLINK);

		ok = try_symlink (resolved, link_path, want_dir,
				  SYMBOLIC_LINK_FLAG_ALLOW_UNPRIVILEGED_CREATE, &win_error);
		/* Windows before 1703 refuses the unprivileged flag itself rather than
		   the call, so a second attempt without it covers those. */
		if (!ok && win_error == ERROR_INVALID_PARAMETER) {
			ok = try_symlink (resolved, link_path, want_dir, 0, &win_error);
		}
	}

	g_free (resolved);

	if (!ok) {
		set_link_error (error, win_error);
	}

	return ok;
}

/* Where a link at w_path finally leads, as a \\?\ path, or NULL with
   *win_error set. */
static WCHAR *
final_path (const gunichar2 *w_path, DWORD *win_error)
{
	HANDLE handle;
	WCHAR *found = NULL;
	DWORD len;

	handle = CreateFileW ((LPCWSTR) w_path, 0, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
			      NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
	if (handle == INVALID_HANDLE_VALUE) {
		*win_error = GetLastError ();
		return NULL;
	}

	len = GetFinalPathNameByHandleW (handle, NULL, 0, FILE_NAME_NORMALIZED | VOLUME_NAME_DOS);
	if (len > 0) {
		found = g_new0 (WCHAR, len + 1);
		if (GetFinalPathNameByHandleW (handle, found, len + 1, FILE_NAME_NORMALIZED | VOLUME_NAME_DOS) > len) {
			g_clear_pointer (&found, g_free);
		}
	}
	if (found == NULL) {
		*win_error = GetLastError ();
	}
	CloseHandle (handle);

	return found;
}

gboolean
nemo_win32_link_create_hard (const char  *existing_path,
                             const char  *link_path,
                             GError     **error)
{
	gunichar2 *w_existing = to_utf16 (existing_path);
	gunichar2 *w_link = to_utf16 (link_path);
	WCHAR *w_final = NULL;
	DWORD attributes, win_error = 0;
	gboolean ok;

	/* CreateHardLinkW names the symlink itself, not what it leads to. */
	attributes = GetFileAttributesW ((LPCWSTR) w_existing);
	if (attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_REPARSE_POINT)) {
		w_final = final_path (w_existing, &win_error);
		if (w_final == NULL) {
			g_free (w_existing);
			g_free (w_link);
			set_link_error (error, win_error);
			return FALSE;
		}
	}

	SetLastError (0);
	ok = CreateHardLinkW ((LPCWSTR) w_link, w_final != NULL ? w_final : (LPCWSTR) w_existing, NULL) != 0;
	if (!ok) {
		win_error = GetLastError ();
	}

	g_free (w_final);

	g_free (w_existing);
	g_free (w_link);

	if (ok) {
		return TRUE;
	}

	if (win_error == ERROR_NOT_SAME_DEVICE) {
		g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_NOT_SUPPORTED,
				     _("A hardlink has to be on the same drive as the original."));
	} else if (win_error == ERROR_INVALID_FUNCTION) {
		g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_NOT_SUPPORTED,
				     _("This drive does not support hardlinks."));
	} else {
		set_link_error (error, win_error);
	}

	return FALSE;
}

gboolean
nemo_win32_link_create_default (const char  *target_path,
                                const char  *link_path,
                                GError     **error)
{
	DWORD win_error = 0;
	gboolean target_is_dir = g_file_test (target_path, G_FILE_TEST_IS_DIR);

	/* A folder gets a junction wherever one will do. It needs no privilege, so
	   a folder link works on a machine where a symlink is refused outright, and
	   nothing downstream can tell the two apart. */
	if (target_is_dir) {
		if (try_junction (target_path, link_path, &win_error)) {
			return TRUE;
		}
		if (win_error == ERROR_ALREADY_EXISTS || win_error == ERROR_FILE_EXISTS) {
			set_link_error (error, win_error);
			return FALSE;
		}
	}

	if (try_symlink (target_path, link_path, target_is_dir,
			 SYMBOLIC_LINK_FLAG_ALLOW_UNPRIVILEGED_CREATE, &win_error)) {
		return TRUE;
	}

	if (win_error == ERROR_INVALID_PARAMETER &&
	    try_symlink (target_path, link_path, target_is_dir, 0, &win_error)) {
		return TRUE;
	}

	set_link_error (error, win_error);
	return FALSE;
}

gboolean
nemo_win32_link_symlinks_allowed (void)
{
	static gsize answer = 0;   /* 1 no, 2 yes */

	if (g_once_init_enter (&answer)) {
		gsize allowed = 1;
		char *dir = g_dir_make_tmp ("nemo-symlink-check-XXXXXX", NULL);

		/* Asking Windows whether the privilege is held means reading a token
		 * and a registry key and getting both right; making one and throwing
		 * it away answers the same question with no room for doubt. */
		if (dir != NULL) {
			char *target = g_build_filename (dir, "target", NULL);
			char *link = g_build_filename (dir, "link", NULL);

			if (g_file_set_contents (target, "", 0, NULL) &&
			    nemo_win32_link_create (target, link, NULL,
						    NEMO_LINK_FILE_SYMLINK, NULL)) {
				allowed = 2;
			}

			g_remove (link);
			g_remove (target);
			g_rmdir (dir);
			g_free (target);
			g_free (link);
			g_free (dir);
		}

		g_once_init_leave (&answer, allowed);
	}

	return answer == 2;
}

guint
nemo_win32_link_kinds_supported (const char *dir_path)
{
	char *native, *root;
	gunichar2 *w_root;
	WCHAR volume_root[MAX_PATH + 1];
	DWORD flags = 0;
	guint kinds = 0;

	if (dir_path == NULL) {
		return 0;
	}

	native = to_native_separators (dir_path);
	w_root = to_utf16 (native);
	g_free (native);
	if (w_root == NULL) {
		return 0;
	}

	if (!GetVolumePathNameW ((LPCWSTR) w_root, volume_root, MAX_PATH + 1)) {
		g_free (w_root);
		return 0;
	}
	g_free (w_root);

	if (!GetVolumeInformationW (volume_root, NULL, 0, NULL, NULL, &flags, NULL, 0)) {
		return 0;
	}
	if (!(flags & FILE_SUPPORTS_REPARSE_POINTS)) {
		return 0;
	}

	root = g_utf16_to_utf8 ((const gunichar2 *) volume_root, -1, NULL, NULL, NULL);
	/* A junction has to name a local drive, so a share cannot hold one that
	   means anything - and Windows will not make one there either. */
	if (root != NULL && g_ascii_isalpha (root[0]) && root[1] == ':') {
		kinds |= NEMO_LINK_JUNCTION;
	}
	g_free (root);

	if (nemo_win32_link_symlinks_allowed ()) {
		kinds |= NEMO_LINK_FILE_SYMLINK | NEMO_LINK_DIR_SYMLINK;
	}

	return kinds;
}

#endif /* G_OS_WIN32 */
