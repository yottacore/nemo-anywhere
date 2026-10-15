/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */

/* test-nemo-magick-win32.c - how ImageMagick is started on Windows.

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

/* Each thumbnail used to open a console window, since the app has no console
 * and the program it started was given one of its own. A stand-in for magick,
 * found first on PATH, says whether it got a window, what reached its stdin,
 * and which arguments it was handed. A failed run and a stopped one come back
 * empty, and a stopped one ends the program. */

#include <config.h>

#include <string.h>

#include <glib.h>
#include <glib/gstdio.h>
#include <gio/gio.h>
#include <windows.h>

#include <libnemo-private/nemo-magick.h>

#include "test-scratch.h"
#include "test-check.h"

static gpointer
stop_soon (gpointer data)
{
	g_usleep (300 * 1000);
	g_cancellable_cancel (G_CANCELLABLE (data));
	return NULL;
}

static gboolean
reported (const char *report, const char *line)
{
	g_autofree char *text = NULL;
	g_autofree char *wanted = g_strdup_printf ("%s\n", line);

	return g_file_get_contents (report, &text, NULL, NULL) && strstr (text, wanted) != NULL;
}

int
main (int argc, char *argv[])
{
	g_autofree char *dir = NULL, *bin = NULL, *fake = NULL, *pic = NULL;
	g_autofree char *uri = NULL, *report = NULL, *path = NULL;
	g_autoptr (GFile) from = NULL, to = NULL;
	g_autoptr (GCancellable) stop = NULL;
	GdkPixbuf *pixbuf;
	GThread *stopper;
	gint64 started;

	if (argc < 2) {
		g_printerr ("usage: %s <stand-in magick.exe>\n", argv[0]);
		return 1;
	}

	dir = test_scratch_dir ("nemo-magick-win32-XXXXXX", NULL);
	check (dir != NULL);

	/* A space in the folder, so the program's path has to be quoted. */
	bin = g_build_filename (dir, "image magick", NULL);
	check (g_mkdir (bin, 0755) == 0);
	fake = g_build_filename (bin, "magick.exe", NULL);
	from = g_file_new_for_path (argv[1]);
	to = g_file_new_for_path (fake);
	check (g_file_copy (from, to, G_FILE_COPY_NONE, NULL, NULL, NULL, NULL));

	path = g_strconcat (bin, ";", g_getenv ("PATH"), NULL);
	g_setenv ("PATH", path, TRUE);
	check (g_ascii_strcasecmp (nemo_magick_program (), fake) == 0);

	report = g_build_filename (dir, "report.txt", NULL);
	g_setenv ("NEMO_FAKE_MAGICK_REPORT", report, TRUE);
	pic = g_build_filename (dir, "pic one.tga", NULL);
	check (g_file_set_contents (pic, "0123456789", 10, NULL));
	uri = g_filename_to_uri (pic, NULL, NULL);

	/* The app has no console of its own, which is when a child got a window. */
	FreeConsole ();

	pixbuf = nemo_magick_load_uri (uri, 64, NULL);
	check (pixbuf != NULL);
	if (pixbuf != NULL) {
		check (gdk_pixbuf_get_width (pixbuf) == 3 && gdk_pixbuf_get_height (pixbuf) == 2);
		g_object_unref (pixbuf);
	}
	check (reported (report, "window=0"));
	check (reported (report, "bytes=10"));
	check (reported (report, "arg=TGA:-[0]"));
	check (reported (report, "arg=64x64>"));
	check (reported (report, "arg=png:-"));

	g_setenv ("NEMO_FAKE_MAGICK_FAIL", "1", TRUE);
	check (nemo_magick_load_uri (uri, 64, NULL) == NULL);
	g_unsetenv ("NEMO_FAKE_MAGICK_FAIL");

	g_setenv ("NEMO_FAKE_MAGICK_SLEEP", "1", TRUE);
	stop = g_cancellable_new ();
	stopper = g_thread_new ("stop", stop_soon, stop);
	started = g_get_monotonic_time ();
	check (nemo_magick_load_uri (uri, 64, stop) == NULL);
	check (g_get_monotonic_time () - started < 10 * G_USEC_PER_SEC);
	g_thread_join (stopper);
	/* Only goes once nothing runs from it. */
	check (g_remove (fake) == 0);

	return failures == 0 ? 0 : 1;
}
