/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */

/* test-nemo-quit-saves.c - what a quit finishes before the program ends.

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

/* A keyboard shortcut change waits 30 s before it is saved, so a quit inside
 * that time has to save it, and a "still unmounting" notice left up at quit
 * has to be taken down. The built program (argv[1]) runs with the hooks
 * (argv[2]) preloaded, which make the change and put the notice up once its
 * window is up. The window is then closed the way a user closes it. The
 * shortcut has to be in the saved file and back on the next start, and the
 * notice has to be withdrawn.
 *
 * This test stands in for the notification server, on a session bus of its
 * own. Without a bus the notice is not checked. POSIX: the window is found and
 * closed through X. */

#include <config.h>

#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

#include <gio/gio.h>
#include <glib/gstdio.h>
#include <X11/Xlib.h>
#include <X11/Xatom.h>

#include "test-scratch.h"
#include "test-check.h"

#define PENDING_ID "unmount-pending"

static GMutex     seen_lock;
static GPtrArray *seen;	/* "add <id>" and "remove <id>", in order */

static gboolean
alive (GPid pid)
{
	int status;

	return pid > 0 && waitpid (pid, &status, WNOHANG) == 0;
}

/* TRUE when it went away inside the time with a clean exit. */
static gboolean
wait_exit (GPid pid, int seconds)
{
	int i, status = 0;

	for (i = 0; i < seconds * 10; i++) {
		pid_t got = waitpid (pid, &status, WNOHANG);

		if (got == pid) {
			return WIFEXITED (status) && WEXITSTATUS (status) == 0;
		}
		if (got < 0) {
			return FALSE;
		}
		g_usleep (100 * 1000);
	}

	return FALSE;
}

static void
stop (GPid pid)
{
	int i;

	if (pid <= 0 || !alive (pid)) {
		return;
	}
	kill (pid, SIGTERM);
	for (i = 0; i < 50 && alive (pid); i++) {
		g_usleep (100 * 1000);
	}
}

static GPid
launch (const char *exe, const char *folder, char **envp)
{
	char *argv[] = { (char *) exe, (char *) folder, NULL };
	GError *error = NULL;
	GPid pid = 0;

	if (!g_spawn_async (NULL, argv, envp, G_SPAWN_DO_NOT_REAP_CHILD, NULL, NULL, &pid, &error)) {
		g_printerr ("spawn: %s\n", error->message);
		g_error_free (error);
	}

	return pid;
}

static int
ignore_x_error (Display *display, XErrorEvent *event)
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

static Window
find_window (Display *display, GPid pid)
{
	Window root, parent, *children = NULL;
	Window found = None;
	unsigned int n = 0, i;

	if (XQueryTree (display, DefaultRootWindow (display), &root, &parent, &children, &n)) {
		Atom pid_atom = XInternAtom (display, "_NET_WM_PID", False);

		for (i = 0; i < n && found == None; i++) {
			if (window_of (display, children[i], pid_atom, pid)) {
				found = children[i];
			}
		}
		if (children != NULL) {
			XFree (children);
		}
	}

	return found;
}

/* What the window manager sends when the close button is clicked. */
static gboolean
close_window (Display *display, GPid pid)
{
	Window window = find_window (display, pid);
	XEvent event;

	if (window == None) {
		return FALSE;
	}

	memset (&event, 0, sizeof (event));
	event.xclient.type = ClientMessage;
	event.xclient.window = window;
	event.xclient.message_type = XInternAtom (display, "WM_PROTOCOLS", False);
	event.xclient.format = 32;
	event.xclient.data.l[0] = (long) XInternAtom (display, "WM_DELETE_WINDOW", False);
	event.xclient.data.l[1] = CurrentTime;

	XSendEvent (display, window, False, NoEventMask, &event);
	XFlush (display);

	return TRUE;
}

/* What the hooks wrote, once they have written it, or NULL. */
static char *
wait_hooks (const char *out, GPid pid, int seconds)
{
	int i;

	for (i = 0; i < seconds * 10 && alive (pid); i++) {
		char *text = NULL;

		if (g_file_get_contents (out, &text, NULL, NULL) && strchr (text, '\n') != NULL) {
			g_strstrip (text);
			return text;
		}
		g_free (text);
		g_usleep (100 * 1000);
	}

	return NULL;
}

static gboolean
was_seen (const char *what)
{
	gboolean found = FALSE;
	guint i;

	g_mutex_lock (&seen_lock);
	for (i = 0; i < seen->len && !found; i++) {
		found = strcmp (g_ptr_array_index (seen, i), what) == 0;
	}
	g_mutex_unlock (&seen_lock);

	return found;
}

static gboolean
wait_seen (const char *what, int seconds)
{
	int i;

	for (i = 0; i < seconds * 10; i++) {
		if (was_seen (what)) {
			return TRUE;
		}
		g_usleep (100 * 1000);
	}

	return FALSE;
}

static void
notification_call (GDBusConnection *connection, const char *sender, const char *path,
		   const char *interface, const char *method, GVariant *parameters,
		   GDBusMethodInvocation *invocation, gpointer user_data)
{
	const char *id = NULL;
	const char *what = strcmp (method, "AddNotification") == 0 ? "add" : "remove";

	g_variant_get_child (parameters, 1, "&s", &id);

	g_mutex_lock (&seen_lock);
	g_ptr_array_add (seen, g_strdup_printf ("%s %s", what, id));
	g_mutex_unlock (&seen_lock);

	g_dbus_method_invocation_return_value (invocation, NULL);
}

static const char notifications_xml[] =
	"<node>"
	"  <interface name='org.gtk.Notifications'>"
	"    <method name='AddNotification'>"
	"      <arg type='s' direction='in'/>"
	"      <arg type='s' direction='in'/>"
	"      <arg type='a{sv}' direction='in'/>"
	"    </method>"
	"    <method name='RemoveNotification'>"
	"      <arg type='s' direction='in'/>"
	"      <arg type='s' direction='in'/>"
	"    </method>"
	"  </interface>"
	"</node>";

/* The program's GLib looks for this server before any other, and talks to
 * it in plain method calls that are easy to record. */
static gpointer
serve_notifications (gpointer user_data)
{
	GAsyncQueue *ready = user_data;
	GMainContext *context = g_main_context_new ();
	GMainLoop *loop = g_main_loop_new (context, FALSE);
	GDBusConnection *bus;
	GDBusNodeInfo *info;
	GDBusInterfaceVTable vtable = { notification_call, NULL, NULL, { 0 } };
	GVariant *reply = NULL;
	guint answer = 0;

	g_main_context_push_thread_default (context);

	bus = g_bus_get_sync (G_BUS_TYPE_SESSION, NULL, NULL);
	info = g_dbus_node_info_new_for_xml (notifications_xml, NULL);
	if (bus != NULL && info != NULL &&
	    g_dbus_connection_register_object (bus, "/org/gtk/Notifications", info->interfaces[0],
					       &vtable, NULL, NULL, NULL) != 0) {
		reply = g_dbus_connection_call_sync (bus, "org.freedesktop.DBus", "/org/freedesktop/DBus",
						     "org.freedesktop.DBus", "RequestName",
						     g_variant_new ("(su)", "org.gtk.Notifications", 4),
						     G_VARIANT_TYPE ("(u)"), G_DBUS_CALL_FLAGS_NONE, -1,
						     NULL, NULL);
	}
	if (reply != NULL) {
		g_variant_get (reply, "(u)", &answer);
		g_variant_unref (reply);
	}

	/* 1 is the primary owner. */
	g_async_queue_push (ready, GUINT_TO_POINTER (answer == 1 ? 1 : 2));
	if (answer == 1) {
		g_main_loop_run (loop);
	}

	g_main_context_pop_thread_default (context);
	if (info != NULL) {
		g_dbus_node_info_unref (info);
	}
	g_clear_object (&bus);
	g_main_loop_unref (loop);
	g_main_context_unref (context);

	return NULL;
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

/* The saved line for the shortcut, uncommented. */
static gboolean
shortcut_saved (const char *accel_file)
{
	char *text = NULL;
	gboolean saved = FALSE;
	char **lines;
	int i;

	if (!g_file_get_contents (accel_file, &text, NULL, NULL)) {
		g_printerr ("%s was not written\n", accel_file);
		return FALSE;
	}

	lines = g_strsplit (text, "\n", -1);
	for (i = 0; lines[i] != NULL && !saved; i++) {
		saved = g_str_has_prefix (lines[i], "(gtk_accel_path \"<nemo-test>/Probe\"") &&
			strstr (lines[i], "F12") != NULL;
	}
	if (!saved) {
		g_printerr ("%s has no line for the changed shortcut\n", accel_file);
	}

	g_strfreev (lines);
	g_free (text);

	return saved;
}

int
main (int argc, char *argv[])
{
	const char *exe, *hooks;
	char *root, *folder, *out, *accel_file, *text;
	char **envp;
	Display *display;
	gboolean with_bus, serving = FALSE;
	GPid pid = 0;

	if (argc < 3) {
		g_printerr ("usage: %s <nemo-anywhere> <hooks.so>\n", argv[0]);
		return 77;
	}
	exe = argv[1];
	hooks = argv[2];

	test_own_display (argc, argv, NULL);
	if (g_getenv ("DISPLAY") == NULL) {
		g_print ("SKIP: no display\n");
		return 77;
	}

	with_bus = ensure_session_bus (argc, argv);

	/* The program inherits this, so its settings and saved shortcuts are in
	 * the scratch dir too. */
	root = test_scratch_config_home ("nemo-quit-saves-XXXXXX");
	folder = g_build_filename (root, "folder", NULL);
	g_mkdir_with_parents (folder, 0700);
	out = g_build_filename (root, "hooks.out", NULL);
	accel_file = g_build_filename (root, ".gnome2", "accels", "nemo", NULL);

	/* The program never makes this folder, so with none it saves nothing at
	 * any time. That is item 2026100315470225. */
	{
		char *accel_dir = g_path_get_dirname (accel_file);

		g_mkdir_with_parents (accel_dir, 0700);
		g_free (accel_dir);
	}

	envp = g_get_environ ();
	envp = g_environ_setenv (envp, "LD_PRELOAD", hooks, TRUE);
	envp = g_environ_setenv (envp, "NEMO_QUIT_OUT", out, TRUE);
	envp = g_environ_unsetenv (envp, "GNOME22_USER_DIR");

	seen = g_ptr_array_new_with_free_func (g_free);
	if (with_bus) {
		GAsyncQueue *ready = g_async_queue_new ();

		g_thread_unref (g_thread_new ("notifications", serve_notifications, ready));
		serving = GPOINTER_TO_UINT (g_async_queue_pop (ready)) == 1;
		g_async_queue_unref (ready);
		check (serving);
	}

	XSetErrorHandler (ignore_x_error);
	display = XOpenDisplay (NULL);
	if (display == NULL) {
		g_print ("SKIP: display will not open\n");
		return 77;
	}

	/* First start: the hooks change the shortcut and put the notice up, and
	 * the window is closed well inside the 30 s. */
	pid = launch (exe, folder, envp);
	check (pid > 0);
	text = wait_hooks (out, pid, 30);
	check (g_strcmp0 (text, "changed") == 0);
	g_free (text);
	check (!g_file_test (accel_file, G_FILE_TEST_EXISTS));
	if (serving) {
		check (wait_seen ("add " PENDING_ID, 10));
	}

	check (close_window (display, pid));
	check (wait_exit (pid, 30));
	stop (pid);

	check (shortcut_saved (accel_file));
	if (serving) {
		check (wait_seen ("remove " PENDING_ID, 10));
	}

	/* Second start: the shortcut is the changed one. */
	g_unlink (out);
	envp = g_environ_setenv (envp, "NEMO_QUIT_CHECK", "1", TRUE);
	pid = launch (exe, folder, envp);
	check (pid > 0);
	text = wait_hooks (out, pid, 30);
	if (text == NULL || strstr (text, "F12") == NULL) {
		g_printerr ("shortcut on the next start: %s\n", text != NULL ? text : "(no answer)");
	}
	check (text != NULL && strstr (text, "F12") != NULL);
	g_free (text);

	check (close_window (display, pid));
	check (wait_exit (pid, 30));
	stop (pid);

	if (!serving) {
		g_print ("no session bus: the notice was not checked\n");
	}

	XCloseDisplay (display);
	g_strfreev (envp);
	g_free (accel_file);
	g_free (out);
	g_free (folder);
	g_free (root);

	if (failures > 0) {
		g_printerr ("%d failure(s)\n", failures);
		return EXIT_FAILURE;
	}

	g_print ("OK\n");
	return EXIT_SUCCESS;
}
