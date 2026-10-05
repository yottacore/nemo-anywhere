/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */

/* test-nemo-startup-clean.c - what a plain start leaves behind.

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

/* Starting with no session bus has to bring a window up with nothing logged at
 * critical level. It used to log a dozen pairs of "invalid (NULL) pointer
 * instance" criticals: signals connected to settings groups that were not open
 * yet, since the store never opened without a bus. With a bus, a start writes
 * the D-Bus activation file into the user's own service folder, naming the
 * program this copy runs from, and leaves one that already does alone.
 *
 * Needs the built program (argv[1]) and a display; without a display it skips.
 * The activation half needs a session bus, and starts one if the environment
 * has none. POSIX: the window is found through X by the pid it carries. */

#include <config.h>

#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
#include <utime.h>

#include <gio/gio.h>
#include <glib/gstdio.h>
#include <X11/Xlib.h>
#include <X11/Xatom.h>

#include "test-scratch.h"
#include "test-check.h"

#define SERVICE_NAME "org.NemoAnywhere.service"

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

/* Home and the XDG folders all inside root, so nothing real is read or written. */
static char **
child_environ (const char *root, gboolean with_bus)
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

	if (!with_bus) {
		envp = g_environ_setenv (envp, "DBUS_SESSION_BUS_ADDRESS", "disabled:", TRUE);
		envp = g_environ_setenv (envp, "G_DEBUG", "fatal-criticals", TRUE);
	}

	return envp;
}

/* Started on a folder of its own, stderr into a file. */
static GPid
launch (const char *exe, const char *root, gboolean with_bus, const char *err_path)
{
	char *folder = g_build_filename (root, "folder", NULL);
	char *argv[] = { (char *) exe, folder, NULL };
	char **envp = child_environ (root, with_bus);
	GError *error = NULL;
	GPid pid = 0;
	int err_fd;

	g_mkdir_with_parents (folder, 0700);
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

/* Other tests share the display, so a window from the list can be gone before
 * it is read. Xlib's default handler exits on that BadWindow; the read just
 * fails instead. */
static int
ignore_x_error (G_GNUC_UNUSED Display *display, G_GNUC_UNUSED XErrorEvent *event)
{
	return 0;
}

static gboolean
window_of (Display *display, Window window, Atom pid_atom, GPid pid)
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

/* A mapped top-level window that says it belongs to pid. */
static gboolean
has_window (GPid pid)
{
	Display *display = XOpenDisplay (NULL);
	Window root, parent, *children = NULL;
	unsigned int n = 0, i;
	gboolean found = FALSE;

	if (display == NULL) {
		return FALSE;
	}

	if (XQueryTree (display, DefaultRootWindow (display), &root, &parent, &children, &n)) {
		Atom pid_atom = XInternAtom (display, "_NET_WM_PID", False);

		for (i = 0; i < n && !found; i++) {
			found = window_of (display, children[i], pid_atom, pid);
		}
		if (children != NULL) {
			XFree (children);
		}
	}
	XCloseDisplay (display);

	return found;
}

static gboolean
wait_window (GPid pid, int seconds)
{
	int i;

	for (i = 0; i < seconds * 10 && alive (pid); i++) {
		if (has_window (pid)) {
			return TRUE;
		}
		g_usleep (100 * 1000);
	}

	return FALSE;
}

static void
check_no_bus (const char *exe)
{
	char *root = test_scratch_dir ("nemo-startup-nobus-XXXXXX", NULL);
	char *err_path = g_build_filename (root, "stderr.txt", NULL);
	char *services = g_build_filename (root, "data", "dbus-1", NULL);
	char *err = NULL;
	GPid pid;

	pid = launch (exe, root, FALSE, err_path);
	check (pid > 0);

	check (wait_window (pid, 30));

	/* Criticals are fatal here, so one that comes late ends the process. */
	g_usleep (3 * G_USEC_PER_SEC);
	check (alive (pid));
	stop (pid);

	if (g_file_get_contents (err_path, &err, NULL, NULL)) {
		check (strstr (err, "CRITICAL") == NULL);
		check (strstr (err, "invalid (NULL) pointer instance") == NULL);
		if (strstr (err, "CRITICAL") != NULL) {
			g_printerr ("%s", err);
		}
	}

	/* Nothing would read an activation file with no bus to read it. */
	check (!g_file_test (services, G_FILE_TEST_EXISTS));

	g_free (err);
	g_free (services);
	g_free (err_path);
	g_free (root);
}

static char *
wanted_service (const char *exe)
{
	char *real = realpath (exe, NULL);
	char *text = g_strdup_printf ("[D-BUS Service]\nName=org.NemoAnywhere\n"
				      "Exec=%s --no-default-window\n", real != NULL ? real : exe);

	free (real);
	return text;
}

static gboolean
wait_contents (const char *path, const char *want, int seconds)
{
	char *got = NULL;
	int i;

	for (i = 0; i < seconds * 10; i++) {
		g_clear_pointer (&got, g_free);
		if (g_file_get_contents (path, &got, NULL, NULL) && g_strcmp0 (got, want) == 0) {
			g_free (got);
			return TRUE;
		}
		g_usleep (100 * 1000);
	}

	g_printerr ("%s holds:\n%s\n", path, got != NULL ? got : "(nothing)");
	g_free (got);

	return FALSE;
}

static void
check_activation_file (const char *exe)
{
	char *root = test_scratch_dir ("nemo-startup-bus-XXXXXX", NULL);
	char *err_path = g_build_filename (root, "stderr.txt", NULL);
	char *dir = g_build_filename (root, "data", "dbus-1", "services", NULL);
	char *path = g_build_filename (dir, SERVICE_NAME, NULL);
	char *want = wanted_service (exe);
	GStatBuf before, after;
	struct utimbuf old = { 978307200, 978307200 };	/* 2001-01-01 */
	GPid pid;

	/* One left by a copy somewhere else, which has to be replaced. */
	g_mkdir_with_parents (dir, 0700);
	check (g_file_set_contents (path, "[D-BUS Service]\nName=org.NemoAnywhere\n"
				    "Exec=/nowhere/nemo-anywhere --no-default-window\n", -1, NULL));

	pid = launch (exe, root, TRUE, err_path);
	check (pid > 0);
	check (wait_contents (path, want, 30));
	stop (pid);

	/* One that already names this copy stays as it is. */
	check (g_utime (path, &old) == 0);
	check (g_stat (path, &before) == 0);

	pid = launch (exe, root, TRUE, err_path);
	check (pid > 0);
	check (wait_window (pid, 30));
	stop (pid);

	check (g_stat (path, &after) == 0);
	check (after.st_mtime == before.st_mtime);
	check (wait_contents (path, want, 1));

	g_free (want);
	g_free (path);
	g_free (dir);
	g_free (err_path);
	g_free (root);
}

/* Same as the instances test: meson turns the bus off for every test. */
static gboolean
ensure_session_bus (int argc, char *argv[])
{
	GDBusConnection *probe;
	char *dbus_run;
	char **relaunch;
	int i;

	probe = g_bus_get_sync (G_BUS_TYPE_SESSION, NULL, NULL);
	if (probe != NULL) {
		g_object_unref (probe);
		return TRUE;
	}

	if (g_getenv ("NEMO_TEST_BUS_RELAUNCHED") != NULL) {
		return FALSE;
	}

	dbus_run = g_find_program_in_path ("dbus-run-session");
	if (dbus_run == NULL) {
		return FALSE;
	}

	g_setenv ("NEMO_TEST_BUS_RELAUNCHED", "1", TRUE);

	relaunch = g_new0 (char *, argc + 3);
	relaunch[0] = dbus_run;
	relaunch[1] = (char *) "--";
	for (i = 0; i < argc; i++) {
		relaunch[i + 2] = argv[i];
	}

	execv (dbus_run, relaunch);

	g_free (relaunch);
	g_free (dbus_run);

	return FALSE;
}

int
main (int argc, char *argv[])
{
	if (argc < 2) {
		g_printerr ("usage: %s <nemo-anywhere>\n", argv[0]);
		return 77;
	}

	if (g_getenv ("DISPLAY") == NULL) {
		g_print ("SKIP: no display\n");
		return 77;
	}

	XSetErrorHandler (ignore_x_error);

	if (ensure_session_bus (argc, argv)) {
		check_activation_file (argv[1]);
	} else {
		g_print ("no session bus: the activation file not checked\n");
	}

	check_no_bus (argv[1]);

	if (failures > 0) {
		g_printerr ("%d failure(s)\n", failures);
		return EXIT_FAILURE;
	}

	g_print ("OK\n");
	return EXIT_SUCCESS;
}
