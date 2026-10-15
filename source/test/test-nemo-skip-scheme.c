/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */

/* test-nemo-skip-scheme.c - an address pasted into Connect to Server's
   server field loses its scheme when it names the chosen type.

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

/* ssh://host/folder with SSH chosen went out as sftp://ssh://host/folder and
 * failed to connect (2026101417352260). Where gvfs is installed, every alias
 * its mount files declare is checked too. */

#include <config.h>

#include <string.h>
#include <gio/gio.h>

#include <libnemo-private/nemo-share.h>

#include "test-check.h"

static void
check_skip (const char *text, const char *scheme, const char *want)
{
	const char *got = nemo_share_skip_scheme (text, scheme);

	if (strcmp (got, want) != 0) {
		g_printerr ("FAIL %s as %s: got \"%s\", expected \"%s\"\n", text, scheme, got, want);
		failures++;
	}
}

/* Only the scheme goes; the rest is left exactly as typed. */
static void
check_cases (void)
{
	const char *plain = "host";

	check_skip ("sftp://host/folder", "sftp", "host/folder");
	check_skip ("ssh://host/folder", "sftp", "host/folder");
	check_skip ("ssh://host", "sftp", "host");
	check_skip ("ssh://host/", "sftp", "host/");
	check_skip ("SSH://pat@host:2222/a b", "sftp", "pat@host:2222/a b");
	check_skip ("SFTP://host", "sftp", "host");
	check_skip ("ftp://host/pub", "ftp", "host/pub");
	check_skip ("smb://host/share", "smb", "host/share");

	check_skip ("host", "sftp", "host");
	check_skip ("", "sftp", "");
	check_skip ("smb://host/share", "sftp", "smb://host/share");
	check_skip ("ssh://host", "ftp", "ssh://host");
	check_skip ("ssh:host", "sftp", "ssh:host");
	check_skip ("sshx://host", "sftp", "sshx://host");
	check_skip ("host/ssh://x", "sftp", "host/ssh://x");

	check (nemo_share_skip_scheme (plain, "sftp") == plain);
}

static void
check_gvfs_aliases (void)
{
	const char *dir = "/usr/share/gvfs/mounts";
	g_autoptr (GDir) mounts = g_dir_open (dir, 0, NULL);
	const char *name;
	int seen = 0;

	if (mounts == NULL) {
		g_print ("no %s, gvfs aliases not checked\n", dir);
		return;
	}

	while ((name = g_dir_read_name (mounts)) != NULL) {
		g_autofree char *path = NULL;
		g_autofree char *scheme = NULL;
		g_auto (GStrv) aliases = NULL;
		g_autoptr (GKeyFile) keys = g_key_file_new ();
		int i;

		if (!g_str_has_suffix (name, ".mount")) {
			continue;
		}
		path = g_build_filename (dir, name, NULL);
		if (!g_key_file_load_from_file (keys, path, G_KEY_FILE_NONE, NULL)) {
			continue;
		}
		scheme = g_key_file_get_string (keys, "Mount", "Scheme", NULL);
		aliases = g_key_file_get_string_list (keys, "Mount", "SchemeAliases", NULL, NULL);
		if (scheme == NULL || aliases == NULL) {
			continue;
		}

		for (i = 0; aliases[i] != NULL; i++) {
			g_autofree char *text = g_strconcat (aliases[i], "://host/folder", NULL);

			check_skip (text, scheme, "host/folder");
			seen++;
		}
	}

	g_print ("%d gvfs aliases checked\n", seen);
}

int
main (void)
{
	check_cases ();
	check_gvfs_aliases ();

	return failures == 0 ? 0 : 1;
}
