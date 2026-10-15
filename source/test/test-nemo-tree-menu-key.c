/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */

/* test-nemo-tree-menu-key.c - the tree pane's menu from the keyboard.

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

/* Shift+F10 in the tree pane has to open its menu. It used to crash the
 * program: the keyboard asks with no mouse event, and the menu looked for the
 * row under the pointer in that event.
 *
 * Needs the built program (argv[1]) and a display, and runs on an X server of
 * its own, since it types into the window. The tree gets the keyboard the way
 * a person would give it, with F6. Down then moves to another folder, and
 * the window following it is how the test knows the keys reached the tree. */

#include <config.h>

#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

#include <glib.h>
#include <glib/gstdio.h>
#include <X11/Xlib.h>
#include <X11/Xatom.h>
#include <X11/keysym.h>
#include <X11/extensions/XTest.h>

#include "test-scratch.h"
#include "test-check.h"

static Display *display;

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

static char **
child_environ (const char *root)
{
	char **envp = g_get_environ ();
	char *path;

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

	return envp;
}

static GPid
launch (const char *exe, const char *root, const char *err_path)
{
	char *folder = g_build_filename (root, "folder", NULL);
	char *argv[] = { (char *) exe, folder, NULL };
	char **envp = child_environ (root);
	GError *error = NULL;
	GPid pid = 0;
	int err_fd;

	err_fd = open (err_path, O_WRONLY | O_CREAT | O_TRUNC, 0600);

	if (err_fd < 0 ||
	    !g_spawn_async_with_fds (NULL, argv, envp, G_SPAWN_DO_NOT_REAP_CHILD, NULL, NULL,
				     &pid, -1, -1, err_fd, &error)) {
		g_printerr ("spawn: %s\n", error != NULL ? error->message : g_strerror (errno));
		g_clear_error (&error);
		pid = 0;
	}

	if (err_fd >= 0) {
		close (err_fd);
	}
	g_strfreev (envp);
	g_free (folder);

	return pid;
}

static int
ignore_x_error (G_GNUC_UNUSED Display *d, G_GNUC_UNUSED XErrorEvent *event)
{
	return 0;
}

static gboolean
window_of (Window window, Atom pid_atom, GPid pid)
{
	XWindowAttributes attributes;
	Atom type;
	int format;
	unsigned long count, after;
	unsigned char *data = NULL;
	gboolean ours = FALSE;

	if (XGetWindowProperty (display, window, pid_atom, 0, 1, False, XA_CARDINAL,
				&type, &format, &count, &after, &data) == Success &&
	    data != NULL && count == 1 && format == 32) {
		ours = (GPid) *(unsigned long *) data == pid;
	}
	if (data != NULL) {
		XFree (data);
	}

	return ours && XGetWindowAttributes (display, window, &attributes) &&
	       attributes.map_state == IsViewable;
}

/* The mapped top-level windows of pid, and the first of them. A menu on show
   is one of these as well. */
static int
windows_of (GPid pid, Window *first)
{
	Window root, parent, *children = NULL;
	unsigned int n = 0, i;
	Atom pid_atom = XInternAtom (display, "_NET_WM_PID", False);
	int found = 0;

	XSync (display, False);
	if (XQueryTree (display, DefaultRootWindow (display), &root, &parent, &children, &n)) {
		for (i = 0; i < n; i++) {
			if (window_of (children[i], pid_atom, pid)) {
				if (found == 0 && first != NULL) {
					*first = children[i];
				}
				found++;
			}
		}
		if (children != NULL) {
			XFree (children);
		}
	}

	return found;
}

static char *
title_of (Window window)
{
	Atom type;
	int format;
	unsigned long count, after;
	unsigned char *data = NULL;
	char *title = NULL;

	if (XGetWindowProperty (display, window, XInternAtom (display, "_NET_WM_NAME", False),
				0, 1024, False, XInternAtom (display, "UTF8_STRING", False),
				&type, &format, &count, &after, &data) == Success && data != NULL) {
		title = g_strndup ((const char *) data, count);
	}
	if (data != NULL) {
		XFree (data);
	}

	return title;
}

/* The title starts with the folder's name; what follows is the program's.
   With away set, waits for it to show some other folder instead. */
static gboolean
wait_title (Window window, const char *folder, int tenths, gboolean away)
{
	size_t len = strlen (folder);
	int i;

	for (i = 0; i < tenths; i++) {
		char *title = title_of (window);
		gboolean shows = title != NULL && strncmp (title, folder, len) == 0 &&
				 (title[len] == '\0' || title[len] == ' ');
		gboolean other = title != NULL && title[0] != '\0' && !shows;

		g_free (title);
		if (away ? other : shows) {
			return TRUE;
		}
		g_usleep (100 * 1000);
	}

	return FALSE;
}

static void
press (KeySym modifier, KeySym key)
{
	KeyCode mod_code = modifier != NoSymbol ? XKeysymToKeycode (display, modifier) : 0;
	KeyCode key_code = XKeysymToKeycode (display, key);

	if (mod_code != 0) {
		XTestFakeKeyEvent (display, mod_code, True, CurrentTime);
	}
	XTestFakeKeyEvent (display, key_code, True, CurrentTime);
	XTestFakeKeyEvent (display, key_code, False, CurrentTime);
	if (mod_code != 0) {
		XTestFakeKeyEvent (display, mod_code, False, CurrentTime);
	}
	XFlush (display);
	g_usleep (300 * 1000);
}

static void
make_dir (const char *root, const char *relative)
{
	char *path = g_build_filename (root, relative, NULL);

	g_mkdir_with_parents (path, 0700);
	g_free (path);
}

static void
check_menu_key (const char *exe)
{
	char *root = test_scratch_dir ("nemo-tree-menu-XXXXXX", NULL);
	char *err_path = g_build_filename (root, "stderr.txt", NULL);
	char *settings = g_build_filename (root, "config", "nemo-anywhere", "settings.shcl", NULL);
	Window window = None;
	gboolean in_tree = FALSE, menu = FALSE;
	GPid pid;
	int i;

	make_dir (root, "folder/sub");
	make_dir (root, "folder2");
	make_dir (root, "config/nemo-anywhere");
	check (g_file_set_contents (settings, "window-state.start-with-tree: true\n", -1, NULL));

	pid = launch (exe, root, err_path);
	check (pid > 0);

	for (i = 0; i < 300 && alive (pid) && windows_of (pid, &window) == 0; i++) {
		g_usleep (100 * 1000);
	}
	if (window == None || !wait_title (window, "folder", 100, FALSE)) {
		g_printerr ("the window never showed the start folder\n");
		failures++;
		goto out;
	}

	XSetInputFocus (display, window, RevertToParent, CurrentTime);
	XSync (display, False);
	g_usleep (500 * 1000);

	/* Down in the folder only moves the selection, so the window going to
	   another folder means the tree has the keys. Usually that is folder2,
	   right below the start folder. On a slow box the tree can still be on
	   its way to the start folder, and Down goes from wherever it is. */
	for (i = 0; i < 8 && !in_tree && alive (pid); i++) {
		press (NoSymbol, XK_F6);
		press (NoSymbol, XK_Down);
		in_tree = wait_title (window, "folder", 20, TRUE);
	}
	check (in_tree);
	if (!in_tree) {
		g_printerr ("F6 never gave the tree the keyboard\n");
		goto out;
	}

	press (XK_Shift_L, XK_F10);
	for (i = 0; i < 30 && !menu && alive (pid); i++) {
		menu = windows_of (pid, NULL) > 1;
		if (!menu) {
			g_usleep (100 * 1000);
		}
	}
	check (alive (pid));
	check (menu);

	press (NoSymbol, XK_Escape);
	check (alive (pid));

out:
	if (!alive (pid)) {
		char *err = NULL;

		if (g_file_get_contents (err_path, &err, NULL, NULL)) {
			g_printerr ("%s", err);
		}
		g_free (err);
	}
	stop (pid);
	g_free (settings);
	g_free (err_path);
	g_free (root);
}

int
main (int argc, char *argv[])
{
	int event, error, major, minor;

	if (argc < 2) {
		g_printerr ("usage: %s <nemo-anywhere>\n", argv[0]);
		return 77;
	}

	test_own_display (argc, argv, "1280x900x24");

	if (g_getenv ("DISPLAY") == NULL || (display = XOpenDisplay (NULL)) == NULL) {
		g_print ("SKIP: no display\n");
		return 77;
	}
	if (!XTestQueryExtension (display, &event, &error, &major, &minor)) {
		g_print ("SKIP: no XTest on this display\n");
		return 77;
	}

	XSetErrorHandler (ignore_x_error);

	check_menu_key (argv[1]);

	XCloseDisplay (display);

	if (failures > 0) {
		g_printerr ("%d failure(s)\n", failures);
		return EXIT_FAILURE;
	}

	g_print ("OK\n");
	return EXIT_SUCCESS;
}
