/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */

/* nemo-magick.c - thumbnails made by ImageMagick, where it is installed.

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

#include <config.h>

#include "nemo-magick.h"

#include <string.h>

#ifdef G_OS_WIN32
#include <glib/gstdio.h>
#include "nemo-launch-win32.h"
#endif

#define TIMEOUT_SECONDS 30

/* ImageMagick holds all of its input in memory before decoding it, so past
 * this the file is left alone. */
#define MAX_FILE_BYTES (256 * 1024 * 1024)

/* Raster formats only. Whether the installed copy can read one depends on how
 * it was built, and one it cannot read just fails like any bad file. */
static const struct {
	const char *extension;
	const char *coder;
} formats[] = {
	{ "jp2",  "JP2" },
	{ "jpf",  "JP2" },
	{ "jpx",  "JP2" },
	{ "j2k",  "J2K" },
	{ "j2c",  "J2C" },
	{ "jpc",  "JPC" },
	{ "jpm",  "JPM" },
	{ "heic", "HEIC" },
	{ "heif", "HEIC" },
	{ "avif", "AVIF" },
	{ "jxl",  "JXL" },
	{ "webp", "WEBP" },
	{ "exr",  "EXR" },
	{ "hdr",  "HDR" },
	{ "dds",  "DDS" },
	{ "tga",  "TGA" },
	{ "pcx",  "PCX" },
	{ "sgi",  "SGI" },
	{ "ras",  "SUN" },
	{ "dpx",  "DPX" },
	{ "cin",  "CIN" },
	{ "fits", "FITS" },
	{ "fit",  "FITS" },
	{ "fts",  "FITS" },
	{ "qoi",  "QOI" },
	{ "xcf",  "XCF" },
	{ "pict", "PICT" },
	{ "pct",  "PICT" },
	{ "jng",  "JNG" },
	{ "miff", "MIFF" },
	{ "pfm",  "PFM" },
	/* Raw files the reader of our own does not know. */
	{ "crw",  "CRW" },
	{ "mrw",  "MRW" },
	{ "x3f",  "X3F" },
};

/* Formats gdk-pixbuf reads itself when it has the loader. Since 2.42.11 a
 * default build leaves these loaders out, and systems put them in a package
 * of their own that may not be there, so these are handed over only when no
 * loader claims the extension. */
static const struct {
	const char *extension;
	const char *coder;
} pixbuf_extras[] = {
	{ "bmp", "BMP" },
	{ "ico", "ICO" },
	{ "cur", "CUR" },
	{ "xpm", "XPM" },
	{ "xbm", "XBM" },
	{ "pnm", "PNM" },
	{ "pbm", "PBM" },
	{ "pgm", "PGM" },
	{ "ppm", "PPM" },
};

static gpointer
find_program (G_GNUC_UNUSED gpointer data)
{
	char *found = g_find_program_in_path ("magick");

#ifndef G_OS_WIN32
	/* ImageMagick 6, which several distributions still ship, has no magick.
	 * On Windows convert.exe is the system's FAT to NTFS converter, so the
	 * old name is never tried there. */
	if (found == NULL) {
		found = g_find_program_in_path ("convert");
	}
#endif

	return found;
}

static gpointer
list_pixbuf_extensions (G_GNUC_UNUSED gpointer data)
{
	GHashTable *found = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);
	GSList *formats = gdk_pixbuf_get_formats ();
	GSList *l;

	for (l = formats; l != NULL; l = l->next) {
		g_auto (GStrv) extensions = NULL;
		guint i;

		if (gdk_pixbuf_format_is_disabled (l->data)) {
			continue;
		}
		extensions = gdk_pixbuf_format_get_extensions (l->data);
		for (i = 0; extensions != NULL && extensions[i] != NULL; i++) {
			char *lower = g_ascii_strdown (extensions[i], -1);

			g_hash_table_add (found, lower);
		}
	}
	g_slist_free (formats);

	return found;
}

static gboolean
pixbuf_reads (const char *extension)
{
	static GOnce once = G_ONCE_INIT;
	g_autofree char *lower = g_ascii_strdown (extension, -1);

	return g_hash_table_contains (g_once (&once, list_pixbuf_extensions, NULL), lower);
}

/* Returns: (transfer none): kept for the life of the process */
const char *
nemo_magick_program (void)
{
	static GOnce once = G_ONCE_INIT;

	return g_once (&once, find_program, NULL);
}

/* Returns: (transfer none): a static string */
const char *
nemo_magick_coder (const char *name)
{
	const char *slash, *dot;
	guint i;

	if (name == NULL) {
		return NULL;
	}

	slash = strrchr (name, '/');
#ifdef G_OS_WIN32
	{
		const char *back = strrchr (name, '\\');

		if (back != NULL && (slash == NULL || back > slash)) {
			slash = back;
		}
	}
#endif
	dot = strrchr (slash != NULL ? slash : name, '.');
	if (dot == NULL) {
		return NULL;
	}

	for (i = 0; i < G_N_ELEMENTS (formats); i++) {
		if (g_ascii_strcasecmp (dot + 1, formats[i].extension) == 0) {
			return formats[i].coder;
		}
	}

	for (i = 0; i < G_N_ELEMENTS (pixbuf_extras); i++) {
		if (g_ascii_strcasecmp (dot + 1, pixbuf_extras[i].extension) == 0) {
			return pixbuf_reads (pixbuf_extras[i].extension) ? NULL : pixbuf_extras[i].coder;
		}
	}

	return NULL;
}

gboolean
nemo_magick_type_ok (const char *uri)
{
	return uri != NULL && g_str_has_prefix (uri, "file:") &&
	       nemo_magick_coder (uri) != NULL && nemo_magick_program () != NULL;
}

/* Returns: (transfer full): free with g_strfreev */
gchar **
nemo_magick_argv (const char *program, const char *coder, int size)
{
	GPtrArray *argv = g_ptr_array_new ();

	g_ptr_array_add (argv, g_strdup (program));

	/* One huge or hostile file gives up rather than taking the machine with
	 * it. The time limit ends it before the thumbnailer's own timeout does. */
	g_ptr_array_add (argv, g_strdup ("-limit"));
	g_ptr_array_add (argv, g_strdup ("memory"));
	g_ptr_array_add (argv, g_strdup ("256MiB"));
	g_ptr_array_add (argv, g_strdup ("-limit"));
	g_ptr_array_add (argv, g_strdup ("map"));
	g_ptr_array_add (argv, g_strdup ("512MiB"));
	g_ptr_array_add (argv, g_strdup ("-limit"));
	g_ptr_array_add (argv, g_strdup ("disk"));
	g_ptr_array_add (argv, g_strdup ("1GiB"));
	g_ptr_array_add (argv, g_strdup ("-limit"));
	g_ptr_array_add (argv, g_strdup ("time"));
	g_ptr_array_add (argv, g_strdup ("25"));

	/* The file comes in on stdin and the PNG goes out on stdout. A file name
	 * is never seen, since ImageMagick reads things into one: a prefix as a
	 * format, and %d as a frame number, which 6 and 7 even escape
	 * differently. The format is named rather than guessed, and only the
	 * first frame of a file of many is read. */
	g_ptr_array_add (argv, g_strdup_printf ("%s:-[0]", coder));

	g_ptr_array_add (argv, g_strdup ("-auto-orient"));
	g_ptr_array_add (argv, g_strdup ("-strip"));
	g_ptr_array_add (argv, g_strdup ("-thumbnail"));
	g_ptr_array_add (argv, g_strdup_printf ("%dx%d>", size, size));
	g_ptr_array_add (argv, g_strdup ("png:-"));
	g_ptr_array_add (argv, NULL);

	return (gchar **) g_ptr_array_free (argv, FALSE);
}

#ifndef G_OS_WIN32
typedef struct {
	GSubprocess *proc;
	GMainLoop   *loop;
	GBytes      *out;
	gboolean     timed_out;
	gboolean     stopped;
} Run;

static gboolean
run_timed_out (gpointer data)
{
	Run *run = data;

	run->timed_out = TRUE;
	g_subprocess_force_exit (run->proc);

	return G_SOURCE_REMOVE;
}

/* Nothing wants the picture any more. A cancelled communicate would leave
 * the program running, so it is ended instead, the same way as a timeout. */
static gboolean
run_cancelled (G_GNUC_UNUSED GCancellable *cancellable, gpointer data)
{
	Run *run = data;

	run->stopped = TRUE;
	g_subprocess_force_exit (run->proc);

	return G_SOURCE_REMOVE;
}

static void
run_finished (GObject *source, GAsyncResult *result, gpointer data)
{
	Run *run = data;

	g_subprocess_communicate_finish (G_SUBPROCESS (source), result, &run->out, NULL, NULL);
	g_main_loop_quit (run->loop);
}
#endif

static GdkPixbuf *
decode_png (GBytes *bytes)
{
	g_autoptr (GdkPixbufLoader) loader = gdk_pixbuf_loader_new_with_type ("png", NULL);
	gboolean ok, closed;
	GdkPixbuf *pixbuf;

	if (loader == NULL) {
		return NULL;
	}
	ok = gdk_pixbuf_loader_write_bytes (loader, bytes, NULL);
	closed = gdk_pixbuf_loader_close (loader, NULL);
	if (!ok || !closed) {
		return NULL;
	}
	pixbuf = gdk_pixbuf_loader_get_pixbuf (loader);

	return pixbuf != NULL ? g_object_ref (pixbuf) : NULL;
}

#ifdef G_OS_WIN32
/* GLib starts a program through a helper of its own, which has no console, so
 * each run opened a console window. Packed into the single exe the helper never
 * starts the program at all and the thumbnail waits on it for good. */
static GdkPixbuf *
run_magick (char **argv, const char *path, GCancellable *cancellable)
{
	g_autoptr (GBytes) out = NULL;
	GStatBuf info;
	gboolean late = FALSE;

	if (g_stat (path, &info) != 0 || info.st_size > MAX_FILE_BYTES) {
		return NULL;
	}

	if (!nemo_launch_win32_pipe ((const gchar * const *) argv, path, TIMEOUT_SECONDS,
				     cancellable, &out, &late)) {
		if (late) {
			g_warning ("ImageMagick took longer than %d seconds, gave up on %s",
				   TIMEOUT_SECONDS, path);
		}
		return NULL;
	}

	return decode_png (out);
}
#else
static GdkPixbuf *
run_magick (char **argv, const char *path, GCancellable *cancellable)
{
	g_autoptr (GMappedFile) mapped = NULL;
	g_autoptr (GBytes) input = NULL;
	GMainContext *context;
	GSource *timeout;
	GSource *stop = NULL;
	GdkPixbuf *pixbuf = NULL;
	Run run = { 0 };

	/* Mapped rather than read, so the file is not copied on its way down the
	 * pipe. A stdin that is the file itself would be cheaper still, but GLib
	 * only offers that on Unix. */
	mapped = g_mapped_file_new (path, FALSE, NULL);
	if (mapped == NULL || g_mapped_file_get_length (mapped) > MAX_FILE_BYTES) {
		return NULL;
	}
	input = g_mapped_file_get_bytes (mapped);

	/* Private context so the wait and the timeout run here rather than on
	 * whatever context this worker thread happens to be running under. */
	context = g_main_context_new ();
	g_main_context_push_thread_default (context);

	run.proc = g_subprocess_newv ((const gchar * const *) argv,
				      G_SUBPROCESS_FLAGS_STDIN_PIPE | G_SUBPROCESS_FLAGS_STDOUT_PIPE |
				      G_SUBPROCESS_FLAGS_STDERR_SILENCE, NULL);
	if (run.proc != NULL) {
		run.loop = g_main_loop_new (context, FALSE);
		g_subprocess_communicate_async (run.proc, input, NULL, run_finished, &run);

		timeout = g_timeout_source_new_seconds (TIMEOUT_SECONDS);
		g_source_set_callback (timeout, run_timed_out, &run, NULL);
		g_source_attach (timeout, context);

		if (cancellable != NULL) {
			stop = g_cancellable_source_new (cancellable);
			g_source_set_callback (stop, G_SOURCE_FUNC (run_cancelled), &run, NULL);
			g_source_attach (stop, context);
		}

		/* A killed child closes its output, so the loop still ends. */
		g_main_loop_run (run.loop);

		if (run.timed_out) {
			g_warning ("ImageMagick took longer than %d seconds, gave up on %s",
				   TIMEOUT_SECONDS, path);
		} else if (!run.stopped && run.out != NULL && g_subprocess_get_successful (run.proc)) {
			pixbuf = decode_png (run.out);
		}

		if (stop != NULL) {
			g_source_destroy (stop);
			g_source_unref (stop);
		}
		g_source_destroy (timeout);
		g_source_unref (timeout);
		g_main_loop_unref (run.loop);
		g_object_unref (run.proc);
	}

	g_clear_pointer (&run.out, g_bytes_unref);
	g_main_context_pop_thread_default (context);
	g_main_context_unref (context);

	return pixbuf;
}
#endif

/* Returns: (transfer full): unref with g_object_unref */
GdkPixbuf *
nemo_magick_load_uri (const char *uri, int size, GCancellable *cancellable)
{
	g_autofree char *path = NULL;
	g_auto (GStrv) argv = NULL;

	if (!nemo_magick_type_ok (uri) || g_cancellable_is_cancelled (cancellable)) {
		return NULL;
	}
	path = g_filename_from_uri (uri, NULL, NULL);
	if (path == NULL) {
		return NULL;
	}

	argv = nemo_magick_argv (nemo_magick_program (), nemo_magick_coder (path),
				 CLAMP (size, 1, 4096));

	return run_magick (argv, path, cancellable);
}
