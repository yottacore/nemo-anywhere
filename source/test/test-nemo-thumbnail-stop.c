/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */

/* test-nemo-thumbnail-stop.c - a thumbnail nothing wants any more stops.

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

/* Leaving a folder dropped its queued thumbnails, but one a thread had
 * started ran to the end: a 42 MB Photoshop file held its thread for about
 * 11 s. Here the same file is dropped partway through, on the only thread,
 * and the next picture has to be made within a few seconds. Nothing may be
 * stored for the dropped one, not even a failure.
 *
 * Each kind of reader is then stopped on its own: a thumbnailer program, a
 * stand-in ImageMagick, and gdk-pixbuf reading from a pipe that is fed
 * slowly. Each would take half a minute or more if left alone. The pipe is
 * POSIX only.
 *
 * Re-runs itself as the thumbnailer when handed --hang or --fail, and as
 * ImageMagick when a copy of it is named magick. */

#include <config.h>

#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include <glib/gstdio.h>
#include <gtk/gtk.h>

#include <libnemo-private/nemo-cache-db.h>
#include <libnemo-private/nemo-desktop-thumbnail.h>
#include <libnemo-private/nemo-directory.h>
#include <libnemo-private/nemo-file.h>
#include <libnemo-private/nemo-file-attributes.h>
#include <libnemo-private/nemo-file-private.h>
#include <libnemo-private/nemo-global-preferences.h>
#include <libnemo-private/nemo-magick.h>
#include <libnemo-private/nemo-thumbnails.h>

#include "test-scratch.h"
#include "test-check.h"

#ifdef G_OS_WIN32
#include <windows.h>
#endif

#define HANG_MIME "image/x-nemo-stop-test"

/* How long a stopped reader may take to give its thread back. */
#define STOP_LIMIT_US (3 * G_USEC_PER_SEC)

#define PSD_SIDE 30000

static char *tmp_dir;

/* Says who it is, then never finishes. */
static int
run_hung (const char *pid_file)
{
	g_autofree char *pid = g_strdup_printf ("%d", (int) getpid ());

	g_file_set_contents (pid_file, pid, -1, NULL);
	g_usleep (60 * G_USEC_PER_SEC);

	return 0;
}

static void
spin (int ms)
{
	int spins;

	for (spins = 0; spins < ms; spins++) {
		g_main_context_iteration (NULL, FALSE);
		g_usleep (1000);
	}
}

static gboolean
spin_until_exists (const char *path, int limit_ms)
{
	int spins;

	for (spins = 0; spins < limit_ms && !g_file_test (path, G_FILE_TEST_EXISTS); spins++) {
		g_usleep (1000);
	}

	return g_file_test (path, G_FILE_TEST_EXISTS);
}

static void
set_old_mtime (const char *path)
{
	g_autoptr (GFile) location = g_file_new_for_path (path);

	/* Or it is left alone for being changed in the last two seconds. */
	check (g_file_set_attribute_uint64 (location, G_FILE_ATTRIBUTE_TIME_MODIFIED,
					    (guint64) (g_get_real_time () / G_USEC_PER_SEC) - 3600,
					    G_FILE_QUERY_INFO_NONE, NULL, NULL));
}

static void
put_u16 (FILE *out, guint value)
{
	guint8 bytes[2] = { (guint8) (value >> 8), (guint8) value };

	fwrite (bytes, 1, 2, out);
}

static void
put_u32 (FILE *out, guint32 value)
{
	guint8 bytes[4] = { (guint8) (value >> 24), (guint8) (value >> 16),
			    (guint8) (value >> 8), (guint8) value };

	fwrite (bytes, 1, 4, out);
}

/* An RGB Photoshop file of PSD_SIDE square, packed with the fewest bytes a
 * row may have, which is about 42 MB. Its size is real, so all of it gets
 * decoded. */
static gboolean
write_big_psd (const char *path)
{
	static const guint8 reserved[6] = { 0 };
	guint8 row[1024];
	gsize row_len = 0, left = PSD_SIDE;
	guint i, channels = 3;
	FILE *out;

	while (left > 0) {
		gsize run = MIN (left, 127);

		row[row_len++] = (guint8) (1 - (int) run);
		row[row_len++] = 0x80;
		left -= run;
	}

	out = fopen (path, "wb");
	if (out == NULL) {
		return FALSE;
	}

	fwrite ("8BPS", 1, 4, out);
	put_u16 (out, 1);
	fwrite (reserved, 1, sizeof reserved, out);
	put_u16 (out, channels);
	put_u32 (out, PSD_SIDE);
	put_u32 (out, PSD_SIDE);
	put_u16 (out, 8);
	put_u16 (out, 3);	/* RGB */
	put_u32 (out, 0);	/* color mode data */
	put_u32 (out, 0);	/* image resources */
	put_u32 (out, 0);	/* layers and masks */
	put_u16 (out, 1);	/* PackBits */

	for (i = 0; i < channels * PSD_SIDE; i++) {
		put_u16 (out, (guint) row_len);
	}
	for (i = 0; i < channels * PSD_SIDE; i++) {
		fwrite (row, 1, row_len, out);
	}

	return fclose (out) == 0;
}

static void
write_png (const char *path, int width, int height)
{
	GdkPixbuf *pixbuf = gdk_pixbuf_new (GDK_COLORSPACE_RGB, FALSE, 8, width, height);

	gdk_pixbuf_fill (pixbuf, 0x336699ff);
	check (gdk_pixbuf_save (pixbuf, path, "png", NULL, NULL));
	g_object_unref (pixbuf);
}

static NemoDirectory *
watch_folder (const char *path, gpointer client)
{
	g_autofree char *uri = g_filename_to_uri (path, NULL, NULL);
	NemoDirectory *directory = nemo_directory_get_by_uri (uri);
	int i;

	nemo_directory_file_monitor_add (directory, client, TRUE,
					 NEMO_FILE_ATTRIBUTES_FOR_ICON, NULL, NULL);

	for (i = 0; i < 5000 && !nemo_directory_are_all_files_seen (directory); i++) {
		g_main_context_iteration (NULL, FALSE);
		g_usleep (2000);
	}
	check (nemo_directory_are_all_files_seen (directory));

	return directory;
}

static gboolean
held (NemoFile *file)
{
	return file->details->thumbnail != NULL && !file->details->is_thumbnailing;
}

/* The case the item was filed for, through the queue the window uses. */
static void
check_dropped_mid_read (void)
{
	g_autofree char *big_dir = g_build_filename (tmp_dir, "big", NULL);
	g_autofree char *small_dir = g_build_filename (tmp_dir, "small", NULL);
	g_autofree char *big_path = g_build_filename (big_dir, "big.psd", NULL);
	g_autofree char *small_path = g_build_filename (small_dir, "small.png", NULL);
	g_autofree char *big_uri = g_filename_to_uri (big_path, NULL, NULL);
	g_autofree char *small_uri = g_filename_to_uri (small_path, NULL, NULL);
	NemoThumbnailRecord record = { 0 };
	NemoDirectory *big_folder, *small_folder;
	NemoFile *big, *small;
	NemoFileId big_id;
	GList *list;
	gint64 dropped, took;
	int big_client, small_client, spins;

	check (g_mkdir (big_dir, 0755) == 0);
	check (g_mkdir (small_dir, 0755) == 0);
	check (write_big_psd (big_path));
	write_png (small_path, 640, 480);
	set_old_mtime (big_path);
	set_old_mtime (small_path);

	big_folder = watch_folder (big_dir, &big_client);
	big = nemo_file_get_by_uri (big_uri);
	check (nemo_file_wants_thumbnail_ahead (big));
	nemo_thumbnail_file_id (big, &big_id);

	list = g_list_append (NULL, big);
	nemo_thumbnail_render_ahead (list, 128);
	nemo_file_set_load_deferred_attrs (big, NEMO_FILE_LOAD_DEFERRED_ATTRS_YES);
	g_list_free (list);

	for (spins = 0; spins < 10000 && nemo_thumbnail_running_jobs () == 0; spins++) {
		spin (1);
	}
	check (nemo_thumbnail_running_jobs () == 1);

	/* Well into the read, and nowhere near its end. */
	spin (300);
	check (nemo_thumbnail_running_jobs () == 1);

	/* The folder is left: the view lets go, and the file goes with it. */
	g_object_add_weak_pointer (G_OBJECT (big), (gpointer *) &big);
	nemo_file_unref (big);
	nemo_directory_file_monitor_remove (big_folder, &big_client);
	nemo_directory_unref (big_folder);
	for (spins = 0; spins < 2000 && big != NULL; spins++) {
		spin (1);
	}
	check (big == NULL);
	dropped = g_get_monotonic_time ();

	/* One thread, so this one waits until the big one lets go of it. */
	small_folder = watch_folder (small_dir, &small_client);
	small = nemo_file_get_by_uri (small_uri);
	list = g_list_append (NULL, small);
	nemo_thumbnail_render_ahead (list, 128);
	nemo_file_set_load_deferred_attrs (small, NEMO_FILE_LOAD_DEFERRED_ATTRS_YES);
	g_list_free (list);

	for (spins = 0; spins < 30000 && !held (small); spins++) {
		spin (1);
	}
	took = g_get_monotonic_time () - dropped;
	g_print ("  next picture made %.2f s after the drop\n", took / 1e6);
	check (held (small));
	check (took < STOP_LIMIT_US);

	/* Neither a picture nor a failure, or it never gets one later. */
	spin (200);
	check (!nemo_cache_db_thumbnail_lookup (nemo_cache_db_get (), big_uri, &big_id, &record, NULL));
	check (nemo_thumbnail_running_jobs () == 0);

	nemo_file_unref (small);
	nemo_directory_file_monitor_remove (small_folder, &small_client);
	nemo_directory_unref (small_folder);
	g_remove (big_path);
}

typedef struct {
	GCancellable *cancellable;
	char         *started;	/* a file to wait for, or NULL */
	int           delay_ms;
} Canceller;

static gpointer
cancel_later (gpointer data)
{
	Canceller *canceller = data;

	if (canceller->started != NULL) {
		spin_until_exists (canceller->started, 10000);
	}
	g_usleep ((gulong) canceller->delay_ms * 1000);
	g_cancellable_cancel (canceller->cancellable);

	return NULL;
}

static gboolean
process_gone (const char *pid_file)
{
	g_autofree char *text = NULL;
	int pid;

	if (!g_file_get_contents (pid_file, &text, NULL, NULL)) {
		return FALSE;
	}
	pid = atoi (text);
	if (pid <= 1) {
		return FALSE;
	}

#ifdef G_OS_WIN32
	{
		HANDLE process = OpenProcess (SYNCHRONIZE | PROCESS_TERMINATE, FALSE, (DWORD) pid);
		gboolean gone;

		if (process == NULL) {
			return TRUE;
		}
		gone = WaitForSingleObject (process, 2000) == WAIT_OBJECT_0;
		if (!gone) {
			TerminateProcess (process, 1);
		}
		CloseHandle (process);

		return gone;
	}
#else
	{
		int spins;

		for (spins = 0; spins < 2000; spins++) {
			if (kill ((pid_t) pid, 0) == -1 && errno == ESRCH) {
				return TRUE;
			}
			g_usleep (1000);
		}
		kill ((pid_t) pid, SIGKILL);

		return FALSE;
	}
#endif
}

/* Calls the factory, cancels once `started` shows up, and times how long
 * the call takes past the cancel. */
static GdkPixbuf *
generate_and_cancel (NemoDesktopThumbnailFactory *factory, const char *uri, const char *mime,
		     const char *started, int delay_ms, gint64 *past_cancel)
{
	g_autoptr (GCancellable) cancellable = g_cancellable_new ();
	Canceller canceller = { cancellable, (char *) started, delay_ms };
	GThread *thread;
	GdkPixbuf *pixbuf;
	gint64 start;

	start = g_get_monotonic_time ();
	thread = g_thread_new ("cancel-later", cancel_later, &canceller);
	pixbuf = nemo_desktop_thumbnail_factory_generate_thumbnail_at_size (factory, uri, mime, 128, cancellable);
	*past_cancel = g_get_monotonic_time () - start;
	g_thread_join (thread);

	/* cppcheck-suppress memleak ; g_thread_join frees the thread */
	return pixbuf;
}

static void
check_helper_ended (NemoDesktopThumbnailFactory *factory)
{
	g_autofree char *pid_file = g_build_filename (tmp_dir, "hang.pid", NULL);
	g_autofree char *path = g_build_filename (tmp_dir, "hang.png", NULL);
	g_autofree char *uri = g_filename_to_uri (path, NULL, NULL);
	GdkPixbuf *pixbuf;
	gint64 took;

	write_png (path, 64, 64);

	pixbuf = generate_and_cancel (factory, uri, HANG_MIME, pid_file, 200, &took);
	g_print ("  thumbnailer program stopped after %.2f s\n", took / 1e6);
	check (pixbuf == NULL);
	check (took < STOP_LIMIT_US);
	check (process_gone (pid_file));
	g_clear_object (&pixbuf);
}

static void
check_magick_ended (NemoDesktopThumbnailFactory *factory)
{
	g_autofree char *pid_file = g_build_filename (tmp_dir, "magick.pid", NULL);
	g_autofree char *path = g_build_filename (tmp_dir, "slow.jp2", NULL);
	g_autofree char *uri = g_filename_to_uri (path, NULL, NULL);
	GdkPixbuf *pixbuf;
	gint64 took;

	check (g_file_set_contents (path, "not really a jp2", -1, NULL));
	check (nemo_magick_type_ok (uri));

	pixbuf = generate_and_cancel (factory, uri, "image/jp2", pid_file, 200, &took);
	g_print ("  ImageMagick stopped after %.2f s\n", took / 1e6);
	check (pixbuf == NULL);
	check (took < STOP_LIMIT_US);
	check (process_gone (pid_file));
	g_clear_object (&pixbuf);
}

#ifndef G_OS_WIN32
typedef struct {
	char    *path;
	char    *opened;
	gboolean gave_up;	/* the reader closed its end */
} SlowFeed;

/* A 4000 by 4000 BMP, a block every 10 ms, so a whole read takes minutes. */
static gpointer
feed_slowly (gpointer data)
{
	SlowFeed *feed = data;
	guint8 header[54] = { 'B', 'M' };
	guint8 block[4096] = { 0 };
	guint32 side = 4000, pixels = side * side * 3;
	int fd, i;

	header[2] = (guint8) (pixels + 54);
	header[3] = (guint8) ((pixels + 54) >> 8);
	header[4] = (guint8) ((pixels + 54) >> 16);
	header[5] = (guint8) ((pixels + 54) >> 24);
	header[10] = 54;
	header[14] = 40;
	for (i = 0; i < 4; i++) {
		header[18 + i] = (guint8) (side >> (8 * i));
		header[22 + i] = (guint8) (side >> (8 * i));
	}
	header[26] = 1;
	header[28] = 24;

	fd = open (feed->path, O_WRONLY);
	if (fd < 0) {
		return NULL;
	}
	g_file_set_contents (feed->opened, "", 0, NULL);

	if (write (fd, header, sizeof header) == (ssize_t) sizeof header) {
		for (i = 0; i < (int) (pixels / sizeof block); i++) {
			if (write (fd, block, sizeof block) < 0) {
				feed->gave_up = errno == EPIPE;
				break;
			}
			g_usleep (10000);
		}
	}
	close (fd);

	return NULL;
}

static gboolean
can_read_bmp (void)
{
	GSList *formats = gdk_pixbuf_get_formats ();
	GSList *l;
	gboolean found = FALSE;

	for (l = formats; l != NULL && !found; l = l->next) {
		g_autofree char *name = gdk_pixbuf_format_get_name (l->data);

		found = g_strcmp0 (name, "bmp") == 0;
	}
	g_slist_free (formats);

	return found;
}

static void
check_pixbuf_read_stops (NemoDesktopThumbnailFactory *factory)
{
	g_autofree char *path = g_build_filename (tmp_dir, "slow.bmp", NULL);
	g_autofree char *opened = g_build_filename (tmp_dir, "slow.opened", NULL);
	g_autofree char *uri = g_filename_to_uri (path, NULL, NULL);
	SlowFeed feed = { path, opened, FALSE };
	GThread *feeder;
	GdkPixbuf *pixbuf;
	gint64 took;

	/* gdk-pixbuf 2.44 as FreeBSD builds it has no BMP loader. The read then
	   fails before it opens the pipe, and the feeder waits on it forever. */
	if (!can_read_bmp ()) {
		g_print ("  gdk-pixbuf read skipped: no BMP loader\n");
		return;
	}

	check (mkfifo (path, 0644) == 0);
	feeder = g_thread_new ("feed-slowly", feed_slowly, &feed);

	pixbuf = generate_and_cancel (factory, uri, "image/bmp", opened, 300, &took);
	g_print ("  gdk-pixbuf read stopped after %.2f s\n", took / 1e6);
	check (pixbuf == NULL);
	check (took < STOP_LIMIT_US);

	g_thread_join (feeder);
	check (feed.gave_up);
	g_clear_object (&pixbuf);
}
#endif

/* Exec lines are shell-parsed, so a backslash would be read as an escape. */
static char *
forward_slashes (const char *path)
{
	char *copy = g_strdup (path);

	g_strdelimit (copy, "\\", '/');

	return copy;
}

static void
write_thumbnailer (const char *dir, const char *name, const char *contents)
{
	g_autofree char *path = g_build_filename (dir, name, NULL);

	check (g_file_set_contents (path, contents, -1, NULL));
}

int
main (int argc, char **argv)
{
	g_autofree char *thumbnailers = NULL;
	g_autofree char *bin = NULL;
	g_autofree char *self = NULL;
	g_autofree char *self_exec = NULL;
	g_autofree char *tmp_exec = NULL;
	g_autofree char *entry = NULL;
	g_autofree char *magick = NULL;
	g_autofree char *magick_pid = NULL;
	g_autofree char *path = NULL;
	NemoDesktopThumbnailFactory *factory;

	{
		g_autofree char *name = g_path_get_basename (argv[0]);

		if (g_str_has_prefix (name, "magick")) {
			return run_hung (g_getenv ("NEMO_TEST_MAGICK_PID"));
		}
	}
	if (argc == 5 && g_strcmp0 (argv[1], "--hang") == 0) {
		return run_hung (argv[2]);
	}
	if (argc >= 2 && g_strcmp0 (argv[1], "--fail") == 0) {
		return 1;
	}

	/* Set before any glib call that would cache the real ones. */
	tmp_dir = test_scratch_config_home ("nemo-thumbnail-stop-XXXXXX");
	if (tmp_dir == NULL) {
		g_printerr ("FAIL could not make a temp dir\n");
		return EXIT_FAILURE;
	}
	g_setenv ("XDG_DATA_HOME", tmp_dir, TRUE);

	/* A stand-in ImageMagick that never finishes, found before any real one. */
	self = g_canonicalize_filename (argv[0], NULL);
	bin = g_build_filename (tmp_dir, "bin", NULL);
	check (g_mkdir (bin, 0755) == 0);
#ifdef G_OS_WIN32
	/* The DLLs are found on the PATH the test runs with. */
	magick = g_build_filename (bin, "magick.exe", NULL);
	{
		g_autoptr (GFile) from = g_file_new_for_path (self);
		g_autoptr (GFile) to = g_file_new_for_path (magick);

		check (g_file_copy (from, to, G_FILE_COPY_NONE, NULL, NULL, NULL, NULL));
	}
#else
	/* A link, since a copy loses the libraries found beside the original. */
	magick = g_build_filename (bin, "magick", NULL);
	check (symlink (self, magick) == 0);
#endif
	magick_pid = g_build_filename (tmp_dir, "magick.pid", NULL);
	g_setenv ("NEMO_TEST_MAGICK_PID", magick_pid, TRUE);
	path = g_strdup_printf ("%s%c%s", bin, G_SEARCHPATH_SEPARATOR,
				g_getenv ("PATH") != NULL ? g_getenv ("PATH") : "");
	g_setenv ("PATH", path, TRUE);

	/* The pipe case is about gdk-pixbuf, so the BMP thumbnailer the system
	   has fails at once and hands over to it. */
	thumbnailers = g_build_filename (tmp_dir, "thumbnailers", NULL);
	check (g_mkdir (thumbnailers, 0755) == 0);
	self_exec = forward_slashes (self);
	tmp_exec = forward_slashes (tmp_dir);
	entry = g_strdup_printf ("[Thumbnailer Entry]\nExec=\"%s\" --hang \"%s/hang.pid\" %%i %%o\nMimeType=%s;\n",
				 self_exec, tmp_exec, HANG_MIME);
	write_thumbnailer (thumbnailers, "hang.thumbnailer", entry);
	g_free (entry);
	entry = g_strdup_printf ("[Thumbnailer Entry]\nExec=\"%s\" --fail %%i %%o\nMimeType=image/bmp;\n", self_exec);
	write_thumbnailer (thumbnailers, "fail.thumbnailer", entry);

#ifndef G_OS_WIN32
	/* A reader that has gone away closes the pipe under the feeder. */
	signal (SIGPIPE, SIG_IGN);
#endif

	gtk_init_check (&argc, &argv);
	nemo_global_preferences_init ();

	nemo_config_set_int (nemo_preferences, NEMO_PREFERENCES_DEFERRED_ATTR_PRELOAD_LIMIT, 0);
	nemo_config_set_int (nemo_preferences, NEMO_PREFERENCES_MAX_THUMBNAIL_THREADS, 1);

	factory = nemo_desktop_thumbnail_factory_new (NEMO_DESKTOP_THUMBNAIL_SIZE_LARGE);
	check_helper_ended (factory);
	check_magick_ended (factory);
#ifndef G_OS_WIN32
	check_pixbuf_read_stops (factory);
#endif
	g_object_unref (factory);

	check_dropped_mid_read ();

	if (failures == 0)
		g_print ("nemo-thumbnail-stop: all checks passed\n");

	return failures == 0 ? 0 : 1;
}
