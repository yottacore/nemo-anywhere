/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */

/* test-nemo-perm-source-win32.c - the Permissions source column, read from the ACL.

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

/* A file's DACL entries either came down from its folder, were set on the file
 * itself, or both. Each case is made here with the same calls the Security tab
 * uses, and read back the way the column reads it. */

#include <config.h>

#include <stdlib.h>
#include <string.h>
#include <gio/gio.h>
#include <gtk/gtk.h>

#include <windows.h>
#include <aclapi.h>

#include <libnemo-private/nemo-file.h>
#include <libnemo-private/nemo-file-private.h>
#include <libnemo-private/nemo-security-win32.h>

#include "test-scratch.h"
#include "test-check.h"

static void
make_file (const char *path)
{
	check (g_file_set_contents (path, "x", -1, NULL));
}

/* One full-control entry for the current user, set on the file itself. With
   keep_inherited the folder's entries stay; without, the DACL is protected and
   they go. */
static gboolean
add_own_entry (const char *path, gboolean keep_inherited)
{
	wchar_t *wide = g_utf8_to_utf16 (path, -1, NULL, NULL, NULL);
	EXPLICIT_ACCESS_W access;
	PACL old_dacl = NULL, new_dacl = NULL;
	PSECURITY_DESCRIPTOR descriptor = NULL;
	SECURITY_INFORMATION what = DACL_SECURITY_INFORMATION;
	DWORD status;

	memset (&access, 0, sizeof access);
	access.grfAccessPermissions = GENERIC_ALL;
	access.grfAccessMode = SET_ACCESS;
	access.grfInheritance = NO_INHERITANCE;
	access.Trustee.TrusteeForm = TRUSTEE_IS_NAME;
	access.Trustee.TrusteeType = TRUSTEE_IS_USER;
	access.Trustee.ptstrName = (LPWSTR) L"CURRENT_USER";

	if (keep_inherited) {
		status = GetNamedSecurityInfoW (wide, SE_FILE_OBJECT, DACL_SECURITY_INFORMATION,
						NULL, NULL, &old_dacl, NULL, &descriptor);
		if (status != ERROR_SUCCESS) {
			g_printerr ("  could not read the DACL of %s: %lu\n", path, status);
			g_free (wide);
			return FALSE;
		}
		what |= UNPROTECTED_DACL_SECURITY_INFORMATION;
	} else {
		what |= PROTECTED_DACL_SECURITY_INFORMATION;
	}

	status = SetEntriesInAclW (1, &access, old_dacl, &new_dacl);
	if (status == ERROR_SUCCESS) {
		status = SetNamedSecurityInfoW (wide, SE_FILE_OBJECT, what,
						NULL, NULL, new_dacl, NULL);
	}
	if (status != ERROR_SUCCESS) {
		g_printerr ("  could not set the DACL of %s: %lu\n", path, status);
	}

	LocalFree (new_dacl);
	LocalFree (descriptor);
	g_free (wide);
	return status == ERROR_SUCCESS;
}

static char *
column_text (const char *path)
{
	GFile *location = g_file_new_for_path (path);
	NemoFile *file = nemo_file_get (location);
	GFileInfo *info = g_file_query_info (location, "standard::*",
					     G_FILE_QUERY_INFO_NOFOLLOW_SYMLINKS, NULL, NULL);
	char *text;

	if (info != NULL) {
		nemo_file_update_info (file, info);
		g_object_unref (info);
	}
	text = nemo_file_get_string_attribute (file, "permissions_source");

	nemo_file_unref (file);
	g_object_unref (location);
	return text;
}

/* Wine hands every file a made-up descriptor with no inherited entries, so
   there is nothing real to read there. */
static gboolean
under_wine (void)
{
	HMODULE ntdll = GetModuleHandleA ("ntdll.dll");

	return ntdll != NULL && GetProcAddress (ntdll, "wine_get_version") != NULL;
}

int
main (int argc, char *argv[])
{
	char *dir, *plain, *local, *mixed, *text;

	if (under_wine ()) {
		g_print ("SKIP: no real ACLs under wine\n");
		return 77;
	}

	gtk_init_check (&argc, &argv);

	dir = test_scratch_dir ("nemo-perm-source-XXXXXX", NULL);
	if (dir == NULL) {
		g_printerr ("FAIL could not make a temp dir\n");
		return EXIT_FAILURE;
	}

	plain = g_build_filename (dir, "plain.txt", NULL);
	local = g_build_filename (dir, "local.txt", NULL);
	mixed = g_build_filename (dir, "mixed.txt", NULL);
	make_file (plain);
	make_file (local);
	make_file (mixed);

	/* The temp folder sits in the profile, whose entries every new file
	   takes on. */
	check (nemo_security_win32_permissions_source (plain) == NEMO_WIN32_PERM_SOURCE_INHERITED);
	text = column_text (plain);
	check (g_strcmp0 (text, "Inherited") == 0);
	g_free (text);

	check (add_own_entry (local, FALSE));
	check (nemo_security_win32_permissions_source (local) == NEMO_WIN32_PERM_SOURCE_LOCAL);
	text = column_text (local);
	check (g_strcmp0 (text, "Local") == 0);
	g_free (text);

	check (add_own_entry (mixed, TRUE));
	check (nemo_security_win32_permissions_source (mixed) == NEMO_WIN32_PERM_SOURCE_MIXED);
	text = column_text (mixed);
	check (g_strcmp0 (text, "Mixed") == 0);
	g_free (text);

	check (nemo_security_win32_permissions_source (NULL) == NEMO_WIN32_PERM_SOURCE_NONE);

	g_free (mixed);
	g_free (local);
	g_free (plain);
	g_free (dir);

	if (failures == 0) {
		g_print ("permissions source: all checks passed\n");
	}
	return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
