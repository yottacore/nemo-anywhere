/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */

/* test-nemo-menus-freed.c - a menu built each time it opens goes when it
   closes.

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

/* The tab menu, the Back button's history menu and the list header's column
 * menu are made new on every open. They were never freed, so each open left
 * a menu behind for the life of the window or the program, and the tab menu
 * kept a hold on its tab. Up made an empty menu it never showed. A menu is a
 * window of its own, so the probe opens and closes each one a few times and
 * counts the windows left over.
 *
 * Needs the built program (argv[1]), test-nemo-tab-move-probe (argv[2]) and a
 * display. Linux and FreeBSD only, for the preload. */

#include <config.h>

#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

#include <glib.h>
#include <glib/gstdio.h>

#include "test-scratch.h"
#include "test-check.h"

#define TIMES 5

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
	}
	g_spawn_close_pid (pid);
}

/* The probe's file for the copy, once it has written one. */
static char *
wait_file (const char *path, int seconds)
{
	char *text = NULL;
	int i;

	for (i = 0; i < seconds * 10; i++) {
		if (g_file_get_contents (path, &text, NULL, NULL)) {
			return text;
		}
		g_usleep (100 * 1000);
	}

	return NULL;
}

static int
value_of (const char *report, const char *key)
{
	char *line = g_strdup_printf ("%s=", key);
	const char *at = strstr (report, line);
	int value = at != NULL ? atoi (at + strlen (line)) : -1000;

	g_free (line);
	return value;
}

/* want_shown is how many of the opens should show a menu. */
static void
check_menu (const char *report, const char *name, const char *what, int want_shown)
{
	char *taken_key = g_strdup_printf ("%s-taken", name);
	char *shown_key = g_strdup_printf ("%s-shown", name);
	char *left_key = g_strdup_printf ("%s-left", name);
	int taken = value_of (report, taken_key);
	int shown = value_of (report, shown_key);
	int left = value_of (report, left_key);

	g_print ("%s: taken %d and shown %d of %d, %d left over\n", what, taken, shown,
	         TIMES, left);
	if (taken != TIMES) {
		g_printerr ("FAIL the %s was not reached each time\n", what);
		failures++;
	} else if (shown != want_shown) {
		g_printerr ("FAIL the %s showed %d times, not %d\n", what, shown, want_shown);
		failures++;
	} else if (left > 0) {
		g_printerr ("FAIL the %s left %d windows behind\n", what, left);
		failures++;
	}
	g_free (taken_key);
	g_free (shown_key);
	g_free (left_key);
}

int
main (int argc, char *argv[])
{
	char *home, *tmp, *folder, *probe_dir, *state_path, *do_path, *menus_path;
	char *state, *report, *command;
	char **envp;
	GPid pid = 0;
	GError *error = NULL;

	if (argc < 3) {
		g_printerr ("usage: %s <nemo-anywhere> <probe module>\n", argv[0]);
		return 77;
	}

#if !defined (__linux__) && !defined (__FreeBSD__)
	g_print ("SKIP: the probe is preloaded, which is done on Linux and FreeBSD only\n");
	return 77;
#endif
	test_own_display (argc, argv, "1280x900x24");

	if (g_getenv ("DISPLAY") == NULL) {
		g_print ("SKIP: no display\n");
		return 77;
	}

	home = test_scratch_config_home ("nemo-menus-home-XXXXXX");
	tmp = test_scratch_dir ("nemo-menus-XXXXXX", NULL);
	probe_dir = test_scratch_dir ("nemo-menus-probe-XXXXXX", NULL);
	folder = g_build_filename (tmp, "folder", NULL);
	check (g_mkdir (folder, 0755) == 0);

	envp = g_get_environ ();
	envp = g_environ_setenv (envp, "NEMO_TABMOVE_OUT", probe_dir, TRUE);
	envp = g_environ_setenv (envp, "LD_PRELOAD", argv[2], TRUE);
	envp = g_environ_setenv (envp, "DBUS_SESSION_BUS_ADDRESS", "disabled:", TRUE);
	{
		char *args[] = { argv[1], (char *) "--geometry=800x500", folder, NULL };

		if (!g_spawn_async (NULL, args, envp, G_SPAWN_DO_NOT_REAP_CHILD,
		                    NULL, NULL, &pid, &error)) {
			g_printerr ("spawn: %s\n", error->message);
			g_clear_error (&error);
		}
	}
	g_strfreev (envp);
	check (pid > 0);
	if (pid <= 0) {
		goto out;
	}

	state_path = g_strdup_printf ("%s/%d", probe_dir, (int) pid);
	do_path = g_strdup_printf ("%s/%d.do", probe_dir, (int) pid);
	menus_path = g_strdup_printf ("%s/%d.menus", probe_dir, (int) pid);

	/* The window is up once the probe sees it showing the folder. */
	state = NULL;
	{
		int i;

		for (i = 0; i < 200 && (state == NULL || strstr (state, "title=folder\n") == NULL); i++) {
			g_free (state);
			g_usleep (100 * 1000);
			state = NULL;
			g_file_get_contents (state_path, &state, NULL, NULL);
		}
	}
	if (state == NULL || strstr (state, "view=list\n") == NULL) {
		g_printerr ("FAIL the window never showed the folder in the list view:\n%s",
		            state != NULL ? state : "nothing\n");
		failures++;
		g_free (state);
		goto done;
	}
	g_free (state);

	command = g_strdup_printf ("menus %d", TIMES);
	check (g_file_set_contents (do_path, command, -1, NULL));
	g_free (command);
	check (kill (pid, SIGUSR2) == 0);
	report = wait_file (menus_path, 60);
	if (report == NULL) {
		g_printerr ("FAIL the probe never reported on the menus\n");
		failures++;
		goto done;
	}
	check_menu (report, "tab", "tab menu", TIMES);
	check_menu (report, "back", "Back history menu", TIMES);
	check_menu (report, "up", "right-click on Up", 0);
	check_menu (report, "columns", "column menu", TIMES);
	g_free (report);
	check (alive (pid));

done:
	g_unlink (menus_path);
	g_unlink (do_path);
	g_unlink (state_path);
	g_free (menus_path);
	g_free (do_path);
	g_free (state_path);
out:
	stop (pid);
	g_rmdir (folder);
	g_free (folder);
	g_free (probe_dir);
	g_free (tmp);
	g_free (home);

	if (failures > 0) {
		g_printerr ("%d failure(s)\n", failures);
		return EXIT_FAILURE;
	}
	g_print ("OK\n");
	return EXIT_SUCCESS;
}
