/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */

/* nemo-security-win32.c - where a file's Windows permissions come from, and
   who owns it.

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

#include "nemo-security-win32.h"

#include <windows.h>
#include <aclapi.h>
#include <lm.h>
#include <sddl.h>

/* Which way the DACL's entries point: all carried down from a parent folder,
   all set on the file itself, or a mix of the two. That is the whole answer
   the Permissions source column shows, so nothing else is fetched. */
NemoWin32PermSource
nemo_security_win32_permissions_source (const char *path)
{
	wchar_t *wide_path;
	PACL dacl = NULL;
	PSECURITY_DESCRIPTOR descriptor = NULL;
	DWORD status;
	DWORD i;
	guint inherited_count = 0;
	guint local_count = 0;
	NemoWin32PermSource source;

	if (path == NULL) {
		return NEMO_WIN32_PERM_SOURCE_NONE;
	}

	wide_path = (wchar_t *) g_utf8_to_utf16 (path, -1, NULL, NULL, NULL);
	if (wide_path == NULL) {
		return NEMO_WIN32_PERM_SOURCE_NONE;
	}

	status = GetNamedSecurityInfoW (wide_path, SE_FILE_OBJECT,
					DACL_SECURITY_INFORMATION,
					NULL, NULL, &dacl, NULL, &descriptor);
	g_free (wide_path);

	if (status != ERROR_SUCCESS) {
		return NEMO_WIN32_PERM_SOURCE_NONE;
	}

	if (dacl != NULL) {
		for (i = 0; i < dacl->AceCount; i++) {
			ACE_HEADER *ace = NULL;

			if (!GetAce (dacl, i, (LPVOID *) &ace)) {
				continue;
			}

			if (ace->AceFlags & INHERITED_ACE) {
				inherited_count++;
			} else {
				local_count++;
			}
		}
	}

	if (inherited_count > 0 && local_count > 0) {
		source = NEMO_WIN32_PERM_SOURCE_MIXED;
	} else if (inherited_count > 0) {
		source = NEMO_WIN32_PERM_SOURCE_INHERITED;
	} else if (local_count > 0) {
		source = NEMO_WIN32_PERM_SOURCE_LOCAL;
	} else {
		/* A missing or empty DACL: nothing was inherited, and nothing was
		   set either - a FAT volume, mostly. Shown as blank. */
		source = NEMO_WIN32_PERM_SOURCE_NONE;
	}

	LocalFree (descriptor);

	return source;
}

/* Owner SID as a string -> full name, "" for none. One lookup per account
   for the life of the process. */
G_LOCK_DEFINE_STATIC (full_names);
static GHashTable *full_names;

/* Only the local account database is asked, so this never waits on a domain
   controller. A domain account is left without a name, the same as a Linux
   account with an empty GECOS. */
static char *
look_up_full_name (PSID owner)
{
	wchar_t name[UNLEN + 1];
	wchar_t domain[DNLEN + 1];
	DWORD name_len = G_N_ELEMENTS (name);
	DWORD domain_len = G_N_ELEMENTS (domain);
	SID_NAME_USE use;
	USER_INFO_10 *info = NULL;
	BYTE local_sid[SECURITY_MAX_SID_SIZE];
	DWORD local_sid_len = sizeof (local_sid);
	wchar_t local_domain[DNLEN + 1];
	DWORD local_domain_len = G_N_ELEMENTS (local_domain);
	SID_NAME_USE local_use;
	char *full_name = NULL;

	if (!LookupAccountSidW (NULL, owner, name, &name_len, domain, &domain_len, &use) ||
	    use != SidTypeUser) {
		return NULL;
	}

	if (NetUserGetInfo (NULL, name, 10, (LPBYTE *) &info) != NERR_Success) {
		return NULL;
	}

	/* A domain user can share a name with a local one. Only take the local
	   account's full name when it is the same account. */
	if (LookupAccountNameW (NULL, name, local_sid, &local_sid_len,
				local_domain, &local_domain_len, &local_use) &&
	    EqualSid (owner, local_sid) &&
	    info->usri10_full_name != NULL && info->usri10_full_name[0] != L'\0') {
		full_name = g_utf16_to_utf8 ((gunichar2 *) info->usri10_full_name, -1, NULL, NULL, NULL);
	}

	NetApiBufferFree (info);

	return full_name;
}

/* Returns: (transfer full): free with g_free */
char *
nemo_security_win32_owner_full_name (const char *path)
{
	wchar_t *wide_path;
	PSID owner = NULL;
	PSECURITY_DESCRIPTOR descriptor = NULL;
	wchar_t *wide_sid = NULL;
	char *sid;
	const char *cached;
	char *full_name;
	DWORD status;

	if (path == NULL) {
		return NULL;
	}

	wide_path = (wchar_t *) g_utf8_to_utf16 (path, -1, NULL, NULL, NULL);
	if (wide_path == NULL) {
		return NULL;
	}

	status = GetNamedSecurityInfoW (wide_path, SE_FILE_OBJECT,
					OWNER_SECURITY_INFORMATION,
					&owner, NULL, NULL, NULL, &descriptor);
	g_free (wide_path);

	if (status != ERROR_SUCCESS) {
		return NULL;
	}

	if (owner == NULL || !ConvertSidToStringSidW (owner, &wide_sid)) {
		LocalFree (descriptor);
		return NULL;
	}

	sid = g_utf16_to_utf8 ((gunichar2 *) wide_sid, -1, NULL, NULL, NULL);
	LocalFree (wide_sid);

	G_LOCK (full_names);
	if (full_names == NULL) {
		full_names = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, g_free);
	}
	cached = sid != NULL ? g_hash_table_lookup (full_names, sid) : NULL;
	if (cached != NULL) {
		full_name = cached[0] != '\0' ? g_strdup (cached) : NULL;
	} else {
		full_name = look_up_full_name (owner);
		if (sid != NULL) {
			g_hash_table_insert (full_names, g_strdup (sid),
					     g_strdup (full_name != NULL ? full_name : ""));
		}
	}
	G_UNLOCK (full_names);

	g_free (sid);
	LocalFree (descriptor);

	return full_name;
}

#endif /* G_OS_WIN32 */
