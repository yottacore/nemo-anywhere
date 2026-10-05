/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */

/* test-nemo-held-view.c - a closed tab's view, still held, and the settings.

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

/* A view whose tab has closed can stay in memory while an unmount, eject or
 * rename still has a ref on it. Its widgets are gone by then, so no handler on a
 * settings group may still have the view or a widget inside it as its data.
 * One that did called into a freed tree view the next time row shading was
 * changed.
 *
 * The built program (argv[1]) opens two folders in tabs with
 * test-nemo-held-view-hooks (argv[2]) preloaded, which keeps each view the
 * program destroys and reports any handler left. A tab is closed with Ctrl+W.
 * In the list view, row shading, its color and folder expansion are then
 * changed in settings.shcl, and each change has to reach the open tab with
 * the program still running. Rows drawn shaded show that row shading is on.
 * The path separator is changed last, and the window has to spell its path
 * again, which takes the path bar's buttons down. The icon view gets the
 * handler checks only.
 *
 * A handler connected once for the whole process, with no data or a static,
 * has to outlive the tab. Freeing the icon view's container used to remove
 * the ones for its captions and label lengths from every other icon view.
 *
 * Runs on an X server of its own, since it types into the window. Linux only,
 * since it works by LD_PRELOAD. */

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

#define SHCL_IMPLEMENTATION
#include "shcl.h"

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
		if (alive (pid)) {
			kill (pid, SIGKILL);
			waitpid (pid, NULL, 0);
		}
	}
	g_spawn_close_pid (pid);
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

/* The mapped top-level windows of pid, and the first of them. */
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

/* The title starts with the folder's name; what follows is the program's. */
static gboolean
title_shows (Window window, const char *folder)
{
	Atom type;
	int format;
	unsigned long count, after;
	unsigned char *data = NULL;
	size_t len = strlen (folder);
	gboolean shows = FALSE;

	if (XGetWindowProperty (display, window, XInternAtom (display, "_NET_WM_NAME", False),
				0, 1024, False, XInternAtom (display, "UTF8_STRING", False),
				&type, &format, &count, &after, &data) == Success && data != NULL) {
		shows = count >= len && strncmp ((const char *) data, folder, len) == 0 &&
			(count == len || data[len] == ' ');
	}
	if (data != NULL) {
		XFree (data);
	}

	return shows;
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
make_folder (const char *root, const char *name)
{
	char *path = g_build_filename (root, name, "sub", NULL);
	char *file = g_build_filename (root, name, "notes.txt", NULL);

	g_mkdir_with_parents (path, 0700);
	check (g_file_set_contents (file, name, -1, NULL));
	g_free (file);
	g_free (path);
}

static char *
read_report (const char *path)
{
	char *text = NULL;

	return g_file_get_contents (path, &text, NULL, NULL) ? text : g_strdup ("");
}

static int
lines_starting (const char *text, const char *start)
{
	char **lines = g_strsplit (text, "\n", -1);
	int i, n = 0;

	for (i = 0; lines[i] != NULL; i++) {
		if (g_str_has_prefix (lines[i], start)) {
			n++;
		}
	}
	g_strfreev (lines);

	return n;
}

static gint64
count_of (const char *path, const char *name)
{
	char *report = read_report (path);
	char *text = g_strconcat ("\n", report, NULL);
	char *prefix = g_strconcat ("\n", name, " ", NULL);
	char *at = strstr (text, prefix);
	gint64 value = at != NULL ? g_ascii_strtoll (at + strlen (prefix), NULL, 10) : -1;

	g_free (prefix);
	g_free (text);
	g_free (report);

	return value;
}

static int
destroyed_views (const char *path)
{
	char *text = read_report (path);
	int n = lines_starting (text, "destroyed ");

	g_free (text);

	return n;
}

/* Every handler the hooks found left on a group, printed. */
static int
report_hits (const char *path, const char *viewer)
{
	char *text = read_report (path);
	char **lines = g_strsplit (text, "\n", -1);
	int i, hits = 0;

	for (i = 0; lines[i] != NULL; i++) {
		if (g_str_has_prefix (lines[i], "hit ")) {
			g_printerr ("%s view: handler left after its tab closed: %s\n", viewer, lines[i] + 4);
			hits++;
		}
	}
	g_strfreev (lines);
	g_free (text);

	return hits;
}

static int
report_lost (const char *path, const char *viewer)
{
	char *text = read_report (path);
	char **lines = g_strsplit (text, "\n", -1);
	int i, lost = 0;

	for (i = 0; lines[i] != NULL; i++) {
		if (g_str_has_prefix (lines[i], "lost ")) {
			g_printerr ("%s view: a handler for the whole process went with the tab: %s\n",
				    viewer, lines[i] + 5);
			lost++;
		}
	}
	g_strfreev (lines);
	g_free (text);

	return lost;
}

/* Waits for the count to stop moving, so a move after a change is the change. */
static gint64
settled (const char *path, const char *name)
{
	gint64 last = -2, now;
	int i, still = 0;

	for (i = 0; i < 100 && still < 10; i++) {
		g_usleep (100 * 1000);
		now = count_of (path, name);
		still = now == last ? still + 1 : 0;
		last = now;
	}

	return last;
}

static gboolean
moved (const char *path, const char *name, gint64 before, GPid pid)
{
	int i;

	for (i = 0; i < 100 && alive (pid); i++) {
		if (count_of (path, name) > before) {
			return TRUE;
		}
		g_usleep (100 * 1000);
	}

	return FALSE;
}

static gboolean
file_has (const char *path, const char *line)
{
	char *text = NULL;
	gboolean has = g_file_get_contents (path, &text, NULL, NULL) && strstr (text, line) != NULL;

	g_free (text);

	return has;
}

/* A key already there is changed on its own line. A second line for it
   reads as a list, and the key then reads as its default. */
static void
set_line (GString *lines, const char *key, const char *line)
{
	char *text = g_strconcat ("\n", lines->str, NULL);
	char *start = g_strconcat ("\n", key, ":", NULL);
	char *at = strstr (text, start);

	if (at != NULL) {
		gssize pos = at - text;

		g_string_erase (lines, pos, strcspn (lines->str + pos, "\n") + 1);
		g_string_insert (lines, pos, line);
	} else {
		g_string_append (lines, line);
	}

	g_free (start);
	g_free (text);
}

/* Through the parser the program reads the file with, so a step whose text
   does not mean what it says fails here. */
static gboolean
reads_back (const char *text, const char *key, const char *value)
{
	shcl_doc *doc = shcl_parse (text, strlen (text));
	shcl_read_str read = shcl_read_string (doc, key, strlen (key));
	gboolean same = shcl_diag_count (doc) == 0 && read.status == SHCL_GOOD &&
			read.value.n == strlen (value) &&
			memcmp (read.value.p, value, read.value.n) == 0;

	shcl_free (doc);

	return same;
}

/* The whole file each time, as a person editing it would leave it. The
   program's own debounced save can come between the write and the reload and
   put the old value back, which is a lost edit and not what this test is
   about, so that case writes again. A line still on disk that never reached
   the tab is a failure. */
static void
change_setting (const char *viewer, const char *settings, GString *lines, const char *key,
		const char *value, const char *out, const char *count, GPid pid)
{
	char *line = g_strdup_printf ("%s: %s\n", key, value);
	gboolean reached = FALSE;
	int tries;

	set_line (lines, key, line);
	if (!reads_back (lines->str, key, value)) {
		g_printerr ("%s view: settings.shcl does not read back %s as %s\n", viewer, key, value);
		failures++;
		g_free (line);
		return;
	}
	for (tries = 0; tries < 3 && !reached; tries++) {
		gint64 before = settled (out, count);

		check (g_file_set_contents (settings, lines->str, -1, NULL));
		reached = moved (out, count, before, pid);
		if (!reached) {
			if (!alive (pid) || file_has (settings, line)) {
				break;
			}
			g_print ("%s view: settings.shcl was saved over by the program, writing it again\n",
				 viewer);
		}
	}
	if (!reached) {
		g_printerr ("%s view: \"%s: %s\" never reached the open tab\n", viewer, key, value);
		failures++;
	}
	g_free (line);
}

static GPid
launch (const char *exe, const char *module, const char *root, const char *out,
	const char *err_path)
{
	char *alpha = g_build_filename (root, "alpha", NULL);
	char *beta = g_build_filename (root, "beta", NULL);
	char *argv[] = { (char *) exe, "--geometry=800x600", "--tabs", alpha, beta, NULL };
	char **envp = g_get_environ ();
	char *path;
	GError *error = NULL;
	GPid pid = 0;
	int err_fd;

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
	envp = g_environ_setenv (envp, "LD_PRELOAD", module, TRUE);
	envp = g_environ_setenv (envp, "NEMO_HELD_VIEW_OUT", out, TRUE);

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
	g_free (beta);
	g_free (alpha);

	return pid;
}

static void
check_held_view (const char *exe, const char *module, const char *viewer)
{
	char *root = test_scratch_dir ("nemo-held-view-XXXXXX", NULL);
	char *config = g_build_filename (root, "config", "nemo-anywhere", NULL);
	char *settings = g_build_filename (config, "settings.shcl", NULL);
	char *out = g_build_filename (root, "report.txt", NULL);
	char *err_path = g_build_filename (root, "stderr.txt", NULL);
	gboolean list = strcmp (viewer, "list") == 0;
	GString *lines = g_string_new (list ? "list-view.row-shading: false\n"
					    : "preferences.default-folder-viewer: icon-view\n");
	Window window = None;
	int failed_before = failures;
	GPid pid;
	int i, tries;

	make_folder (root, "alpha");
	make_folder (root, "beta");
	g_mkdir_with_parents (config, 0700);
	check (g_file_set_contents (settings, lines->str, -1, NULL));

	pid = launch (exe, module, root, out, err_path);
	check (pid > 0);

	for (i = 0; i < 300 && alive (pid) && windows_of (pid, &window) == 0; i++) {
		g_usleep (100 * 1000);
	}
	for (i = 0; i < 100 && window != None && alive (pid) &&
		    !title_shows (window, "alpha") && !title_shows (window, "beta"); i++) {
		g_usleep (100 * 1000);
	}
	if (window == None || i == 100 || !alive (pid)) {
		g_printerr ("%s view: the window never showed either folder\n", viewer);
		failures++;
		goto out;
	}
	/* Both tabs are made before the window is shown, so this is only the
	   loading settling down. */
	g_usleep (1000 * 1000);
	check (destroyed_views (out) == 0);

	XSetInputFocus (display, window, RevertToParent, CurrentTime);
	XSync (display, False);
	g_usleep (500 * 1000);

	for (tries = 0; tries < 2 && destroyed_views (out) == 0 && alive (pid); tries++) {
		press (XK_Control_L, XK_w);
		for (i = 0; i < 100 && destroyed_views (out) == 0 && alive (pid); i++) {
			g_usleep (100 * 1000);
		}
	}
	if (destroyed_views (out) != 1) {
		g_printerr ("%s view: closing a tab destroyed %d views, not one\n",
			    viewer, destroyed_views (out));
		failures++;
		goto out;
	}
	if (!alive (pid) || windows_of (pid, NULL) == 0) {
		g_printerr ("%s view: the window went with the tab\n", viewer);
		failures++;
		goto out;
	}

	/* Read now, before any change can crash a build that still has them. */
	failures += report_hits (out, viewer);

	if (list) {
		change_setting (viewer, settings, lines, "list-view.row-shading", "true",
				out, "shaded_rows", pid);
		change_setting (viewer, settings, lines, "list-view.row-shading-color", "red",
				out, "draws", pid);
		change_setting (viewer, settings, lines, "list-view.enable-folder-expansion", "false",
				out, "expander_sets", pid);
		/* The window, not the view, but this is where a running window is. */
		change_setting (viewer, settings, lines, "windows.path-separator", "slash",
				out, "pathbar_removes", pid);
		g_usleep (500 * 1000);
		if (!alive (pid)) {
			g_printerr ("list view: the program died after the settings changed\n");
			failures++;
		}
	}

	/* A container can be freed a little after its view is destroyed. */
	g_usleep (1000 * 1000);
	failures += report_lost (out, viewer);

out:
	if (failures > failed_before && !alive (pid)) {
		char *err = NULL;

		if (g_file_get_contents (err_path, &err, NULL, NULL)) {
			g_printerr ("%s", err);
		}
		g_free (err);
	}
	stop (pid);
	g_string_free (lines, TRUE);
	g_free (err_path);
	g_free (out);
	g_free (settings);
	g_free (config);
	g_free (root);
}

int
main (int argc, char *argv[])
{
#ifndef __linux__
	g_print ("SKIP: keeps the view through LD_PRELOAD, which is Linux only here\n");
	return 77;
#else
	int event, error, major, minor;

	if (argc < 3) {
		g_printerr ("usage: %s <nemo-anywhere> <hooks module>\n", argv[0]);
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

	check_held_view (argv[1], argv[2], "list");
	check_held_view (argv[1], argv[2], "icon");

	XCloseDisplay (display);

	if (failures > 0) {
		g_printerr ("%d failure(s)\n", failures);
		return EXIT_FAILURE;
	}

	g_print ("OK\n");
	return EXIT_SUCCESS;
#endif
}
