/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */

/* test-nemo-cache-quit.c - what a quit leaves in the file cache.

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

/* Draw counts are held in memory for up to 30 s before they are written, so a
 * quit has to write them, and fold the journal back into the file. The built
 * program opens a folder of pictures and quits soon after, the way a user ends
 * it. First a folder of big pictures, closed while thumbnails are still being
 * made. Then a small one, closed once its thumbnails are made, then again,
 * when they are drawn from the store, and a third time through --quit from
 * another copy. After each the journal is empty, and after the last two the
 * counts went up. A --version run, which never uses the store, leaves none
 * behind.
 *
 * Counts are read with nemo_cache_db_thumbnail_stats, which only reads. Needs
 * the built program (argv[1]) and a display; without a display it skips. The
 * --quit run needs a session bus, and starts one if the environment has none.
 * POSIX: the window is found and closed through X. */

#include <config.h>

#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>
#include <utime.h>

#include <gio/gio.h>
#include <glib/gstdio.h>
#include <gdk-pixbuf/gdk-pixbuf.h>
#include <X11/Xlib.h>
#include <X11/Xatom.h>

#include <libnemo-private/nemo-cache-db.h>

#include "test-scratch.h"
#include "test-check.h"

#define PICTURES 12

/* Enough big pictures that some are still being made when the window closes. */
#define BUSY_PICTURES 64

/* Well under the 30 s the counts wait in memory, and long enough for the
 * thumbnails to come back from the store and be drawn. */
#define DRAW_WAIT_SECS 4

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
launch (const char *exe, const char *arg)
{
	char *argv[] = { (char *) exe, (char *) arg, NULL };
	GError *error = NULL;
	GPid pid = 0;

	if (!g_spawn_async (NULL, argv, NULL, G_SPAWN_DO_NOT_REAP_CHILD, NULL, NULL, &pid, &error)) {
		g_printerr ("spawn: %s\n", error->message);
		g_error_free (error);
	}

	return pid;
}

/* Other tests share the display, so a window from the list can be gone before
 * it is read. Xlib's default handler exits on that BadWindow. */
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

/* The mapped top-level window pid owns, or None. */
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

static gboolean
wait_window (Display *display, GPid pid, int seconds)
{
	int i;

	for (i = 0; i < seconds * 10 && alive (pid); i++) {
		if (find_window (display, pid) != None) {
			return TRUE;
		}
		g_usleep (100 * 1000);
	}

	return FALSE;
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

/* Sizes and shades differ, so no two are the same file. Dated well back, since
 * a file changed in the last moments is not thumbnailed yet. */
static char **
make_pictures (const char *folder, int count, int scale)
{
	char **uris = g_new0 (char *, count + 1);
	struct utimbuf old = { 978307200, 978307200 };	/* 2001-01-01 */
	int i;

	g_mkdir_with_parents (folder, 0700);

	for (i = 0; i < count; i++) {
		int width = (120 + 37 * (i % 12)) * scale + i;
		int height = (300 - 17 * (i % 12)) * scale;
		GdkPixbuf *pixbuf = gdk_pixbuf_new (GDK_COLORSPACE_RGB, FALSE, 8, width, height);
		char *name = g_strdup_printf ("picture-%02d.png", i);
		char *path = g_build_filename (folder, name, NULL);

		gdk_pixbuf_fill (pixbuf, 0x10203000u + (guint32) i * 0x13110700u);
		check (gdk_pixbuf_save (pixbuf, path, "png", NULL, NULL));
		check (g_utime (path, &old) == 0);
		uris[i] = g_filename_to_uri (path, NULL, NULL);

		g_object_unref (pixbuf);
		g_free (path);
		g_free (name);
	}

	return uris;
}

static gboolean
all_stored (NemoCacheDb *db, char **uris)
{
	int i;

	for (i = 0; uris[i] != NULL; i++) {
		if (!nemo_cache_db_thumbnail_stats (db, uris[i], NULL, NULL)) {
			return FALSE;
		}
	}

	return TRUE;
}

static gboolean
wait_stored (NemoCacheDb *db, char **uris, int seconds)
{
	int i;

	for (i = 0; i < seconds * 10; i++) {
		if (all_stored (db, uris)) {
			return TRUE;
		}
		g_usleep (100 * 1000);
	}

	return FALSE;
}

static gboolean
wait_any_stored (NemoCacheDb *db, char **uris, int seconds)
{
	int i, j;

	for (i = 0; i < seconds * 100; i++) {
		for (j = 0; uris[j] != NULL; j++) {
			if (nemo_cache_db_thumbnail_stats (db, uris[j], NULL, NULL)) {
				return TRUE;
			}
		}
		g_usleep (10 * 1000);
	}

	return FALSE;
}

/* Draws counted in the file for each picture. */
static void
read_counts (NemoCacheDb *db, char **uris, gint64 *counts)
{
	int i;

	for (i = 0; uris[i] != NULL; i++) {
		counts[i] = -1;
		check (nemo_cache_db_thumbnail_stats (db, uris[i], &counts[i], NULL));
	}
}

static gboolean
all_went_up (const gint64 *before, const gint64 *after)
{
	int i;

	for (i = 0; i < PICTURES; i++) {
		if (after[i] <= before[i]) {
			g_printerr ("picture %d drawn %" G_GINT64_FORMAT " times, was %" G_GINT64_FORMAT "\n",
				    i, after[i], before[i]);
			return FALSE;
		}
	}

	return TRUE;
}

static gboolean
journal_empty (const char *db_path)
{
	char *wal = g_strconcat (db_path, "-wal", NULL);
	GStatBuf info;
	gboolean empty;

	empty = g_stat (wal, &info) != 0 || info.st_size == 0;
	if (!empty) {
		g_printerr ("%s holds %" G_GINT64_FORMAT " bytes\n", wal, (gint64) info.st_size);
	}
	g_free (wal);

	return empty;
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
	const char *exe;
	char *root, *folder, *busy_folder, *db_path;
	char **uris, **busy_uris;
	Display *display;
	NemoCacheDb *db = NULL;
	gint64 made[PICTURES], drawn[PICTURES], quit[PICTURES];
	gboolean with_bus;
	GPid pid = 0;

	if (argc < 2) {
		g_printerr ("usage: %s <nemo-anywhere>\n", argv[0]);
		return 77;
	}
	exe = argv[1];

	if (g_getenv ("DISPLAY") == NULL) {
		g_print ("SKIP: no display\n");
		return 77;
	}

	with_bus = ensure_session_bus (argc, argv);

	/* The program run here inherits this, so its settings and cache are in
	 * the scratch dir too. */
	root = test_scratch_config_home ("nemo-cache-quit-XXXXXX");
	folder = g_build_filename (root, "pictures", NULL);
	uris = make_pictures (folder, PICTURES, 1);
	busy_folder = g_build_filename (root, "busy", NULL);
	busy_uris = make_pictures (busy_folder, BUSY_PICTURES, 6);
	db_path = nemo_cache_db_path ();
	check (db_path != NULL);
	if (db_path == NULL) {
		return EXIT_FAILURE;
	}

	XSetErrorHandler (ignore_x_error);
	display = XOpenDisplay (NULL);
	if (display == NULL) {
		g_print ("SKIP: display will not open\n");
		return 77;
	}

	/* A run that never touches the store must not make one on its way out. */
	{
		char *version_argv[] = { (char *) exe, (char *) "--version", NULL };
		int status = -1;

		check (g_spawn_sync (NULL, version_argv, NULL,
				     G_SPAWN_STDOUT_TO_DEV_NULL | G_SPAWN_STDERR_TO_DEV_NULL,
				     NULL, NULL, NULL, NULL, &status, NULL));
		check (WIFEXITED (status) && WEXITSTATUS (status) == 0);
		check (!g_file_test (db_path, G_FILE_TEST_EXISTS));
	}

	/* Made before the program starts, so the program's own setup of a new
	 * file is not part of what is timed here. */
	db = nemo_cache_db_get ();
	check (db != NULL);
	if (db == NULL) {
		goto out;
	}

	/* Closed while thumbnails are still being made. The ones in hand are
	 * finished and stored on the way out, and the journal is folded in after
	 * them. */
	pid = launch (exe, busy_folder);
	check (pid > 0);
	check (wait_window (display, pid, 30));
	check (wait_any_stored (db, busy_uris, 60));
	check (!all_stored (db, busy_uris));
	check (close_window (display, pid));
	check (wait_exit (pid, 30));
	pid = 0;
	check (journal_empty (db_path));

	/* First run on the small set makes the thumbnails. Closing it with them
	 * stored leaves rows in the journal that only the quit folds back in. */
	pid = launch (exe, folder);
	check (pid > 0);
	check (wait_window (display, pid, 30));
	check (wait_stored (db, uris, 60));

	check (close_window (display, pid));
	check (wait_exit (pid, 20));
	pid = 0;
	check (journal_empty (db_path));
	read_counts (db, uris, made);

	/* Second run draws them from the store and is closed the same way, well
	 * inside the time the counts would sit in memory. */
	pid = launch (exe, folder);
	check (pid > 0);
	check (wait_window (display, pid, 30));
	g_usleep (DRAW_WAIT_SECS * G_USEC_PER_SEC);

	check (close_window (display, pid));
	check (wait_exit (pid, 20));
	pid = 0;
	check (journal_empty (db_path));
	read_counts (db, uris, drawn);
	check (all_went_up (made, drawn));

	if (!with_bus) {
		g_print ("no session bus: --quit not checked\n");
		goto out;
	}

	/* Third run is taken down by another copy's --quit. */
	pid = launch (exe, folder);
	check (pid > 0);
	check (wait_window (display, pid, 30));
	g_usleep (DRAW_WAIT_SECS * G_USEC_PER_SEC);

	{
		GPid quitter = launch (exe, "--quit");

		check (quitter > 0);
		check (wait_exit (quitter, 20));
	}
	check (wait_exit (pid, 20));
	pid = 0;
	check (journal_empty (db_path));
	read_counts (db, uris, quit);
	check (all_went_up (drawn, quit));

out:
	stop (pid);
	XCloseDisplay (display);
	g_strfreev (uris);
	g_strfreev (busy_uris);
	g_free (busy_folder);
	g_free (db_path);
	g_free (folder);
	g_free (root);

	if (failures > 0) {
		g_printerr ("%d failure(s)\n", failures);
		return EXIT_FAILURE;
	}

	g_print ("OK\n");
	return EXIT_SUCCESS;
}
