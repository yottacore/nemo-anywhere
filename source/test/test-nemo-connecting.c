/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */

/* test-nemo-connecting.c - the window says it is connecting while a share
   mounts, fades and takes no input but Escape meanwhile, and Escape stops
   it.

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

/* After an smb:// address was typed, nothing showed that the app was working
 * on it until a login prompt or the share turned up (2026100816170921). First
 * the host the sign names is checked for a set of addresses, then the built
 * program runs with test-nemo-connecting-probe preloaded, which holds the
 * mount and looks at the window while it waits.
 *
 * Needs the built program (argv[1]), the probe (argv[2]) and a display. Linux
 * and FreeBSD only, since it works by LD_PRELOAD. */

#include <config.h>

#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

#include <gio/gio.h>
#include <glib/gstdio.h>

#include <libnemo-private/nemo-share.h>

#include "test-scratch.h"
#include "test-check.h"

/* NULL where the visit stays on this machine. */
static void
check_host (const char *uri, const char *want)
{
	GFile *location = g_file_new_for_uri (uri);
	char *host = nemo_share_host_to_reach (location);

	if (g_strcmp0 (host, want) != 0) {
		g_printerr ("FAIL host of %s: got %s, expected %s\n", uri,
			    host != NULL ? host : "(none)", want != NULL ? want : "(none)");
		failures++;
	}
	g_free (host);
	g_object_unref (location);
}

static void
check_hosts (void)
{
	char *root = test_scratch_dir ("nemo-connecting-share-XXXXXX", NULL);
	char *inside = g_build_filename (root, "deep", NULL);
	const char *roots[] = { root, NULL };
	GFile *file;
	char *host;

	check_host ("smb://fakehost/share", "fakehost");
	check_host ("smb://DOMAIN;someone@fakehost/share/folder", "fakehost");
	check_host ("sftp://someone@[::1]:2222/home", "::1");
	check_host ("ftp://someone:pa%40ss@ftp.example.com/pub", "ftp.example.com");
	check_host ("dav://web%2Dbox/files", "web-box");
	check_host ("network:///", "");
	check_host ("smb:///", "");
	check_host ("trash:///", NULL);
	check_host ("recent:///", NULL);
	check_host ("mtp://Phone_1234/", NULL);
	check_host ("file:///", NULL);

	/* A mounted share has no server in its path, so the sign names the mount. */
	nemo_share_set_roots_for_test (roots);
	file = g_file_new_for_path (inside);
	host = nemo_share_host_to_reach (file);
	check (g_strcmp0 (host, root) == 0);
	g_free (host);
	g_object_unref (file);
	nemo_share_set_roots_for_test (NULL);

	file = g_file_new_for_path (inside);
	host = nemo_share_host_to_reach (file);
	check (host == NULL);
	g_free (host);
	g_object_unref (file);

	g_free (inside);
	g_free (root);
}

#if defined (__linux__) || defined (__FreeBSD__)
static gboolean
alive (GPid pid)
{
	int status;

	return pid > 0 && waitpid (pid, &status, WNOHANG) == 0;
}

static void
stop (GPid pid)
{
	int i;

	if (pid <= 0) {
		return;
	}
	if (alive (pid)) {
		kill (pid, SIGTERM);
		for (i = 0; i < 50 && alive (pid); i++) {
			g_usleep (100 * 1000);
		}
		if (alive (pid)) {
			kill (pid, SIGKILL);
			waitpid (pid, NULL, 0);
		}
	}
	g_spawn_close_pid (pid);
}

static void
run_window (const char *exe, const char *probe)
{
	char *root = test_scratch_dir ("nemo-connecting-XXXXXX", NULL);
	char *folder = g_build_filename (root, "start", NULL);
	char *out = g_build_filename (root, "connecting.txt", NULL);
	char *path, *text = NULL;
	char *spawn_argv[4];
	char **envp, **lines;
	GError *error = NULL;
	GPid pid = 0;
	gboolean done = FALSE;
	int i, results = 0;

	g_mkdir_with_parents (folder, 0700);
	path = g_build_filename (folder, "a.txt", NULL);
	check (g_file_set_contents (path, "a", -1, NULL));
	g_free (path);

	envp = g_get_environ ();
	envp = g_environ_setenv (envp, "HOME", root, TRUE);
	path = g_build_filename (root, "config", NULL);
	envp = g_environ_setenv (envp, "XDG_CONFIG_HOME", path, TRUE);
	g_free (path);
	path = g_build_filename (root, "data", NULL);
	envp = g_environ_setenv (envp, "XDG_DATA_HOME", path, TRUE);
	g_free (path);
	path = g_build_filename (root, "cache", NULL);
	envp = g_environ_setenv (envp, "XDG_CACHE_HOME", path, TRUE);
	g_free (path);
	envp = g_environ_setenv (envp, "DBUS_SESSION_BUS_ADDRESS", "disabled:", TRUE);
	/* No gvfs, so smb:// goes nowhere but the probe. */
	envp = g_environ_setenv (envp, "GIO_USE_VFS", "local", TRUE);
	envp = g_environ_setenv (envp, "LD_PRELOAD", probe, TRUE);
	envp = g_environ_setenv (envp, "NEMO_CONNECTING_OUT", out, TRUE);

	spawn_argv[0] = (char *) exe;
	spawn_argv[1] = (char *) "--geometry=800x600";
	spawn_argv[2] = folder;
	spawn_argv[3] = NULL;
	if (!g_spawn_async (NULL, spawn_argv, envp, G_SPAWN_DO_NOT_REAP_CHILD, NULL, NULL,
			    &pid, &error)) {
		g_printerr ("spawn: %s\n", error->message);
		g_clear_error (&error);
		failures++;
		goto out;
	}

	/* The probe gives each step ten seconds, and there are fifteen. */
	for (i = 0; i < 1600 && alive (pid) && !done; i++) {
		g_usleep (100 * 1000);
		g_free (text);
		text = NULL;
		done = g_file_get_contents (out, &text, NULL, NULL) && strstr (text, "done\n") != NULL;
	}
	if (!done) {
		g_printerr ("FAIL the probe never finished (%s)\n",
			    alive (pid) ? "still running" : "the program exited");
		failures++;
	}

	if (text != NULL) {
		lines = g_strsplit (text, "\n", -1);
		for (i = 0; lines[i] != NULL; i++) {
			if (g_str_has_prefix (lines[i], "FAIL ")) {
				g_printerr ("%s\n", lines[i]);
				failures++;
				results++;
			} else if (g_str_has_prefix (lines[i], "ok ")) {
				g_print ("%s\n", lines[i]);
				results++;
			}
		}
		g_strfreev (lines);
	}
	if (done && results != 18) {
		g_printerr ("FAIL %d results, expected 18\n", results);
		failures++;
	}

out:
	stop (pid);
	g_strfreev (envp);
	g_free (text);
	g_free (out);
	g_free (folder);
	g_free (root);
}
#endif

int
main (int argc, char *argv[])
{
#if defined (__linux__) || defined (__FreeBSD__)
	/* The click and drop checks go by what is under the pointer. Before any
	 * scratch dir, since a relaunch never cleans up after this copy. */
	if (argc >= 3) {
		test_own_display (argc, argv, "1280x900x24");
	}
#endif

	check_hosts ();

#if !defined (__linux__) && !defined (__FreeBSD__)
	(void) argc;
	(void) argv;
	g_print ("SKIP: drives the program through LD_PRELOAD, which is Linux and FreeBSD only here\n");
	return failures > 0 ? EXIT_FAILURE : 77;
#else
	if (argc < 3) {
		g_printerr ("usage: %s <nemo-anywhere> <probe module>\n", argv[0]);
		return 77;
	}
	if (g_getenv ("DISPLAY") == NULL) {
		g_print ("SKIP: no display\n");
		return failures > 0 ? EXIT_FAILURE : 77;
	}

	run_window (argv[1], argv[2]);

	if (failures > 0) {
		g_printerr ("%d failure(s)\n", failures);
		return EXIT_FAILURE;
	}

	g_print ("OK\n");
	return EXIT_SUCCESS;
#endif
}
