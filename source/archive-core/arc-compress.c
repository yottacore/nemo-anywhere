/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */

/* arc-compress.c - writing an archive, as a job.

   Copyright © 2026 t00mietum (CryptogID: ปʬϝღถɔ4რఠΔթะ9ƾǝu).

   This program is free software; you can redistribute it and/or
   modify it under the terms of the GNU General Public License as
   published by the Free Software Foundation; version 2 of the
   License.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, see <http://www.gnu.org/licenses/>.
*/

#include "arc-compress.h"

#include <errno.h>
#include <string.h>

#include <glib/gi18n.h>
#include <glib/gstdio.h>
#include <archive.h>
#include <archive_entry.h>

#include "arc-command.h"
#include "arc-scan.h"
#include "arc-verify.h"
#include "arc-volumes.h"

#define READ_BUFFER_SIZE (64 * 1024)

/* One archive to write, and what goes in it. A plain compress has one of
   these; compressing a selection separately has one per item. */
typedef struct {
	GList *sources;		/* GFile *, owned */
	GFile *destination;	/* owned */
} ArchiveUnit;

struct _ArcCompress {
	ArcHost             host;		/* the data is the app's, and outlives this */
	ArcSettings         settings;		/* the job's own copy */
	GCancellable       *cancellable;

	GList              *units;		/* ArchiveUnit *, in order */
	guint               unit_count;
	guint               unit_index;		/* which one is being written */
	GFile              *result_file;	/* what the callback is handed */

	/* The unit in hand. sources and destination are borrowed from it. */
	GList              *sources;		/* GFile * */
	GFile              *destination;
	GFile              *base_dir;

	ArcOptions          options;
	ArcBackend          backend;

	ArcScanList        *list;		/* what goes in */
	GList              *passed;		/* char *, links this unit couldn't keep */
	GList              *passed_over;	/* char *, the same for every unit */
	guint64             total_bytes;
	guint64             done_bytes;
	guint               file_count;

	GList              *verified;		/* GFile *, sources safe to remove */
	char               *verify_trouble;	/* why the first one that failed did */
	gboolean            unit_empty;		/* nothing in it could go in */
	gboolean            bar_pulses;		/* a run whose percent isn't the unit's */

	char               *error_message;
	char               *error_details;
	gboolean            success;
};

/* The GOutputStream libarchive writes through. Going out via GIO rather than a
   filename keeps one code path for local and remote destinations, and sidesteps
   the byte-versus-wide filename split on Windows. */
typedef struct {
	GOutputStream *stream;
	GCancellable  *cancellable;
	GError        *error;
	gboolean       fail_after_stop;
} StreamSink;

static ArcMountTable *(*mounts_for_test) (void);

void
arc_compress_set_mounts_for_test (ArcMountTable *(*mounts) (void))
{
	mounts_for_test = mounts;
}

static void
archive_unit_free (ArchiveUnit *unit)
{
	g_list_free_full (unit->sources, g_object_unref);
	g_clear_object (&unit->destination);
	g_free (unit);
}

static gboolean
job_aborted (ArcCompress *job)
{
	return g_cancellable_is_cancelled (job->cancellable);
}

static gboolean
format_pads_entries (ArcFormat format)
{
	return format == ARC_FORMAT_TAR ||
	       format == ARC_FORMAT_TAR_GZ ||
	       format == ARC_FORMAT_TAR_XZ;
}

static la_ssize_t
sink_write (struct archive *a,
	    void           *client_data,
	    const void     *buffer,
	    size_t          length)
{
	StreamSink *sink = client_data;
	GError *error = NULL;
	gsize written = 0;

	/* A stop is not a failed write. libarchive takes a failed write as fatal
	   and then never finishes the entry, which is where the zip writer frees
	   its compressor. The job sees the stop itself and removes the archive,
	   so these bytes can go nowhere. A tar's last writes are the exception,
	   see run_libarchive. */
	if (g_cancellable_is_cancelled (sink->cancellable)) {
		return sink->fail_after_stop ? -1 : (la_ssize_t) length;
	}

	if (!g_output_stream_write_all (sink->stream, buffer, length, &written,
					sink->cancellable, &error)) {
		if (g_error_matches (error, G_IO_ERROR, G_IO_ERROR_CANCELLED)) {
			g_clear_error (&error);
			return (la_ssize_t) length;
		}

		archive_set_error (a, EIO, "%s",
				   error != NULL ? error->message : "write failed");

		/* libarchive writes again on its way out after a failure. The first
		   error is the one that says what happened. */
		if (sink->error == NULL) {
			sink->error = error;
		} else {
			g_clear_error (&error);
		}
		return -1;
	}

	return (la_ssize_t) written;
}

/* The stream is closed by the caller, which is where a flush failure can still
   be turned into a message. */
static int
sink_close (struct archive *a,
	    void           *client_data)
{
	(void) a;
	(void) client_data;

	return ARCHIVE_OK;
}

/* Names go in as wide characters on Windows, where the byte-oriented setters
   would run the name through the ANSI code page and mangle anything outside it. */
static void
entry_set_paths (struct archive_entry *entry,
		 const char           *rel_path,
		 const char           *link_target)
{
#ifdef G_OS_WIN32
	gunichar2 *wide;

	wide = g_utf8_to_utf16 (rel_path, -1, NULL, NULL, NULL);
	if (wide != NULL) {
		archive_entry_copy_pathname_w (entry, (const wchar_t *) wide);
		g_free (wide);
	} else {
		archive_entry_set_pathname (entry, rel_path);
	}

	if (link_target != NULL) {
		wide = g_utf8_to_utf16 (link_target, -1, NULL, NULL, NULL);
		if (wide != NULL) {
			archive_entry_copy_symlink_w (entry, (const wchar_t *) wide);
			g_free (wide);
		} else {
			archive_entry_set_symlink (entry, link_target);
		}
	}
#else
	archive_entry_set_pathname (entry, rel_path);

	if (link_target != NULL) {
		archive_entry_set_symlink (entry, link_target);
	}
#endif
}

static gboolean
configure_writer (struct archive *a,
		  ArcCompress    *job,
		  GError        **error)
{
	const ArcOptions *options = &job->options;
	int level = CLAMP (options->level, 0, ARC_LEVEL_MAX);
	gboolean has_password = options->password != NULL && options->password[0] != '\0';
	char *opts = NULL;

	/* Blocking is a tar convention: libarchive pads the output to 10240 bytes
	   by default, which a tar reader expects and every other format reads as
	   junk after the end of the archive. */
	if (options->format == ARC_FORMAT_ZIP ||
	    options->format == ARC_FORMAT_7Z) {
		archive_write_set_bytes_per_block (a, 0);
	}

	switch (options->format) {
	case ARC_FORMAT_ZIP:
		archive_write_set_format_zip (a);
		if (level == ARC_LEVEL_STORE) {
			opts = g_strdup ("zip:compression=store");
		} else {
			opts = g_strdup_printf ("zip:compression=deflate,zip:compression-level=%d", level);
		}
#ifdef G_OS_WIN32
		{
			/* Otherwise names go in in a local code page, which
			   drops any letter it does not have. */
			char *with_names = g_strconcat (opts, ",zip:hdrcharset=UTF-8", NULL);

			g_free (opts);
			opts = with_names;
		}
#endif
		break;
	case ARC_FORMAT_TAR:
		archive_write_set_format_pax_restricted (a);
		break;
	case ARC_FORMAT_TAR_GZ:
		archive_write_set_format_pax_restricted (a);
		archive_write_add_filter_gzip (a);
		opts = g_strdup_printf ("gzip:compression-level=%d", level);
		break;
	case ARC_FORMAT_TAR_XZ:
		archive_write_set_format_pax_restricted (a);
		archive_write_add_filter_xz (a);
		/* The only built-in format that can be spread over cores. Deflate and
		   libarchive's own lzma2 have no such option, so they are left alone
		   rather than handed one they would refuse. */
		opts = g_strdup_printf ("xz:compression-level=%d,xz:threads=%d",
					level, (int) job->settings.threads);
		break;
	case ARC_FORMAT_7Z:
		archive_write_set_format_7zip (a);
		if (level == ARC_LEVEL_STORE) {
			opts = g_strdup ("7zip:compression=copy");
		} else {
			opts = g_strdup_printf ("7zip:compression=lzma2,7zip:compression-level=%d", level);
		}
		break;
	default:
		g_set_error (error, G_IO_ERROR, G_IO_ERROR_NOT_SUPPORTED,
			     _("This archive format can't be written here."));
		return FALSE;
	}

	if (opts != NULL) {
		/* Tuning only - an option a build of libarchive does not know about
		   costs compression, not correctness. */
		if (archive_write_set_options (a, opts) != ARCHIVE_OK) {
			g_warning ("archive: %s not accepted: %s", opts, archive_error_string (a));
		}
		g_free (opts);
	}

	if (has_password) {
		/* Encryption is not tuning: silently writing a readable archive
		   when one was asked to be locked would be the worst outcome. */
		if (options->format != ARC_FORMAT_ZIP ||
		    archive_write_set_options (a, "zip:encryption=aes256") != ARCHIVE_OK ||
		    archive_write_set_passphrase (a, options->password) != ARCHIVE_OK) {
			g_set_error (error, G_IO_ERROR, G_IO_ERROR_NOT_SUPPORTED,
				     _("This archive format can't be encrypted here."));
			return FALSE;
		}
	}

	return TRUE;
}

static gboolean
write_entry_data (ArcCompress    *job,
		  struct archive *a,
		  GFile          *file,
		  GError        **error)
{
	GFileInputStream *in;
	char buffer[READ_BUFFER_SIZE];
	gssize count;
	gboolean ok = TRUE;

	in = g_file_read (file, job->cancellable, error);
	if (in == NULL) {
		return FALSE;
	}

	while (TRUE) {
		count = g_input_stream_read (G_INPUT_STREAM (in), buffer, sizeof (buffer),
					     job->cancellable, error);
		if (count <= 0) {
			ok = (count == 0);
			break;
		}

		if (archive_write_data (a, buffer, (size_t) count) < 0) {
			g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED, "%s", archive_error_string (a));
			ok = FALSE;
			break;
		}

		job->done_bytes += (guint64) count;
		arc_fraction (&job->host, (double) job->done_bytes, (double) job->total_bytes);
	}

	g_input_stream_close (G_INPUT_STREAM (in), NULL, NULL);
	g_object_unref (in);

	return ok;
}

static gboolean
write_item (ArcCompress    *job,
	    struct archive *a,
	    const ArcItem  *item,
	    GError        **error)
{
	struct archive_entry *ae = archive_entry_new ();
	guint32 mode = item->mode & 0777;
	gboolean ok = TRUE;

	entry_set_paths (ae, item->name, item->kind == ARC_ITEM_LINK ? item->target : NULL);

	switch (item->kind) {
	case ARC_ITEM_LINK:
		archive_entry_set_filetype (ae, AE_IFLNK);
		archive_entry_set_size (ae, 0);
		if (mode == 0) {
			mode = 0777;
		}
		break;
	case ARC_ITEM_FOLDER:
		archive_entry_set_filetype (ae, AE_IFDIR);
		archive_entry_set_size (ae, 0);
		if (mode == 0) {
			mode = 0755;
		}
		break;
	case ARC_ITEM_FILE:
	default:
		archive_entry_set_filetype (ae, AE_IFREG);
		archive_entry_set_size (ae, (la_int64_t) item->bytes);
		/* Windows has no unix mode to read, so give the entry the one a
		   reader on a unix box would expect. */
		if (mode == 0) {
			mode = 0644;
		}
		break;
	}

	archive_entry_set_perm (ae, mode);
	if (item->mtime != 0) {
		archive_entry_set_mtime (ae, (time_t) item->mtime, 0);
	}

	if (archive_write_header (a, ae) != ARCHIVE_OK) {
		g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED, "%s", archive_error_string (a));
		ok = FALSE;
	} else if (item->kind == ARC_ITEM_FILE) {
		GFile *file = g_file_new_for_path (item->path);

		ok = write_entry_data (job, a, file, error);
		g_object_unref (file);
	}

	archive_entry_free (ae);

	return ok;
}

/* The first failure is the one worth showing; anything after it is fallout. */
static void
job_fail (ArcCompress *job,
	  const char  *message,
	  const char  *details)
{
	if (job->error_message != NULL) {
		return;
	}

	job->error_message = g_strdup (message);
	job->error_details = g_strdup (details);
}

static void
job_fail_from_error (ArcCompress *job,
		     GError      *error)
{
	if (error != NULL && g_error_matches (error, G_IO_ERROR, G_IO_ERROR_CANCELLED)) {
		return;
	}

	job_fail (job, _("The archive couldn't be created."),
		  error != NULL ? error->message : NULL);
}

/* With several archives to write, which one is in hand says more than how much
   is in it, so it takes the status line and the per-archive wording is dropped.
   Takes ownership of that wording either way. */
static void
set_unit_status (ArcCompress *job,
		 char        *one_archive_form)
{
	char *name;
	char *status;

	if (job->unit_count < 2) {
		arc_status (&job->host, one_archive_form);
		g_free (one_archive_form);
		return;
	}

	g_free (one_archive_form);

	name = g_file_get_basename (job->destination);
	status = g_strdup_printf (_("Compressing %s (%d of %d)"),
				  name, job->unit_index + 1, job->unit_count);
	arc_status (&job->host, status);
	g_free (status);
	g_free (name);
}

static void
prescan_found (gpointer data,
	       guint    files,
	       guint64  bytes)
{
	ArcCompress *job = data;

	arc_prescan_found (&job->host, files, bytes);
}

/* The job's own walk, through the archive core, by the link and filesystem
   choices as the writer can do them. Never the dialog's: that one stopped at
   OK, and left out links onto a share, which OK says to follow. */
static gboolean
prescan (ArcCompress *job)
{
	GPtrArray *paths = g_ptr_array_new_with_free_func (g_free);
	char *base = g_file_get_path (job->base_dir);
	GList *l;

	arc_status (&job->host, _("Preparing to compress"));

	for (l = job->sources; l != NULL; l = l->next) {
		char *path = g_file_get_path (G_FILE (l->data));

		if (path == NULL) {
			break;
		}
		g_ptr_array_add (paths, path);
	}
	g_ptr_array_add (paths, NULL);

	if (base == NULL || l != NULL) {
		job_fail (job, _("The archive couldn't be created."),
			  _("The selection isn't on this computer, so it can't be compressed here."));
	} else {
		arc_prescan_start (&job->host);
		prescan_found (job, 0, 0);
		job->list = arc_scan_list (&job->host, base, (const char * const *) paths->pdata,
					   &job->options.links,
					   arc_backend_link_stores (job->options.format, job->backend),
					   mounts_for_test != NULL ? mounts_for_test () : NULL,
					   job->cancellable, prescan_found, job);
		if (job->list != NULL) {
			prescan_found (job, job->list->files, job->list->bytes);
		}
		arc_prescan_end (&job->host);
	}

	g_ptr_array_unref (paths);
	g_free (base);
	return job->list != NULL && !job_aborted (job);
}

/* Nothing to put in. One archive per item, where the options leave the
   item out, has no archive for that one and goes on with the rest. */
static gboolean
list_has_nothing (ArcCompress *job)
{
	job->total_bytes = job->list->bytes;
	job->file_count = job->list->files;

	if (job->list->items->len > 0) {
		return FALSE;
	}

	if (job->unit_count > 1 && job->list->left_out->len > 0) {
		job->unit_empty = TRUE;
		return TRUE;
	}

	job_fail (job, _("The archive couldn't be created."),
		  job->list->left_out->len > 0
		  ? _("The link options leave out everything in the selection.")
		  : _("Nothing in the selection could be read."));
	return TRUE;
}

static gboolean
run_libarchive (ArcCompress *job)
{
	struct archive *a;
	StreamSink sink;
	GFileOutputStream *out;
	GError *error = NULL;
	guint i;
	gboolean ok = TRUE;
	gboolean closed;

	if (!prescan (job)) {
		return FALSE;
	}
	if (list_has_nothing (job)) {
		return job->unit_empty;
	}

	out = g_file_replace (job->destination, NULL, FALSE,
			      G_FILE_CREATE_REPLACE_DESTINATION, job->cancellable, &error);
	if (out == NULL) {
		job_fail_from_error (job, error);
		g_clear_error (&error);
		return FALSE;
	}

	sink.stream = G_OUTPUT_STREAM (out);
	sink.cancellable = job->cancellable;
	sink.error = NULL;
	sink.fail_after_stop = FALSE;

	a = archive_write_new ();

	if (!configure_writer (a, job, &error)) {
		job_fail (job, _("The archive couldn't be created."),
			  error != NULL ? error->message : NULL);
		g_clear_error (&error);
		ok = FALSE;
	} else if (archive_write_open2 (a, &sink, NULL, sink_write, sink_close, NULL) != ARCHIVE_OK) {
		job_fail (job, _("The archive couldn't be created."), archive_error_string (a));
		ok = FALSE;
	}

	if (ok) {
		set_unit_status (job, g_strdup_printf (ngettext ("Compressing %'d file",
								 "Compressing %'d files",
								 job->file_count),
						       job->file_count));
	}

	for (i = 0; ok && i < job->list->items->len; i++) {
		const ArcItem *item = g_ptr_array_index (job->list->items, i);

		if (job_aborted (job)) {
			ok = FALSE;
			break;
		}

		arc_details (&job->host, item->name);

		ok = write_item (job, a, item, &error);
		if (!ok) {
			job_fail_from_error (job, error);
			g_clear_error (&error);
		}
	}

	closed = ok;
	if (ok && archive_write_close (a) != ARCHIVE_OK) {
		job_fail (job, _("The archive couldn't be created."), archive_error_string (a));
		ok = FALSE;
	}

	/* The sink drops what is written after a stop instead of failing it. */
	if (ok && job_aborted (job)) {
		ok = FALSE;
	}

	/* Freed after a stop, a tar writer fills the rest of the open entry with
	   zeros, through the compressor, which on a big file takes as long as the
	   rest of the job would have. A failed write ends that at once, and the
	   gzip and xz filters still end their compressor on close. Not so the zip
	   writer, which is why only the tar formats get it. */
	if (job_aborted (job) && format_pads_entries (job->options.format)) {
		sink.fail_after_stop = TRUE;
	}

	/* The 7z writer pads the same way, but into a temporary file of its own
	   that the sink never sees. Marked failed, the close skips the open entry
	   and still frees the output buffers, and the free ends the compressor. */
	if (job_aborted (job) && !closed && job->options.format == ARC_FORMAT_7Z) {
		archive_write_fail (a);
		archive_write_close (a);
	}

	archive_write_free (a);

	if (!g_output_stream_close (G_OUTPUT_STREAM (out), NULL, &error) && ok) {
		job_fail_from_error (job, error);
		ok = FALSE;
	}
	g_clear_error (&error);
	g_object_unref (out);

	if (sink.error != NULL) {
		job_fail_from_error (job, sink.error);
		g_clear_error (&sink.error);
		ok = FALSE;
	}

	return ok;
}

/* Both tools write a running "NN%". The newest one in the buffer wins. */
static gboolean
scan_percent (const char *text,
	      gsize       length,
	      int        *percent)
{
	gboolean found = FALSE;
	gsize i;

	for (i = 0; i < length; i++) {
		gsize start;
		int value;

		if (!g_ascii_isdigit (text[i])) {
			continue;
		}

		start = i;
		value = 0;
		while (i < length && g_ascii_isdigit (text[i])) {
			value = value * 10 + (text[i] - '0');
			if (value > 100) {
				value = 101;	/* too big to be a percentage */
			}
			i++;
		}

		if (i > start && i < length && text[i] == '%' && value <= 100) {
			*percent = value;
			found = TRUE;
		}
	}

	return found;
}

static void
delete_one (GFile    *file,
	    gpointer  data)
{
	ArcCompress *job = data;

	arc_remove_own (&job->host, file);
}

static void
queue_one (GFile    *file,
	   gpointer  data)
{
	ArcCompress *job = data;

	arc_file_added (&job->host, file);
}

/* What a tool printed, as a terminal would leave it. Both redraw their
   progress with backspaces, which would otherwise pile up for as long as the
   job runs. Past the limit the rest is dropped, and output_full says so. */
#define OUTPUT_LIMIT (1024 * 1024)

static void
take_output (GString    *output,
	     const char *buffer,
	     gsize       count)
{
	gsize i;

	for (i = 0; i < count && output->len < OUTPUT_LIMIT; i++) {
		if (buffer[i] != '\b') {
			g_string_append_c (output, buffer[i]);
			continue;
		}

		/* One character back, not one byte, and never onto the line above. */
		while (output->len > 0 && output->str[output->len - 1] != '\n') {
			gboolean first_byte = ((guchar) output->str[output->len - 1] & 0xC0) != 0x80;

			g_string_truncate (output, output->len - 1);
			if (first_byte) {
				break;
			}
		}
	}
}

static gboolean
output_full (GString *output)
{
	return output->len >= OUTPUT_LIMIT;
}

/* The end of what the tool said, for the error dialog. Caller frees. */
static char *
output_tail (GString *output)
{
	const char *start = output->str;

	if (output->len > 4096) {
		start = output->str + output->len - 4096;
		while (*start != '\0' && ((guchar) *start & 0xC0) == 0x80) {
			start++;
		}
	}

	return g_strstrip (g_strdup (start));
}

static gboolean
archive_written (ArcCompress *job)
{
	guint digits;

	if (g_file_query_exists (job->destination, NULL)) {
		return TRUE;
	}
	if (job->options.split_size == 0) {
		return FALSE;
	}

	for (digits = 1; digits <= 3; digits++) {
		GFile *first = arc_volume_file (job->destination, job->backend, 1, digits);
		gboolean there = first != NULL && g_file_query_exists (first, NULL);

		g_clear_object (&first);
		if (there) {
			return TRUE;
		}
	}

	return FALSE;
}

/* What a run left out on purpose, or knew it couldn't read, is passed over
   by both tools with a warning status when they meet it anyway, as with a
   link that leads nowhere. A warning about anything else still fails it. */
static gboolean
only_known_warned (ArcCompress *job,
		   ArcRun      *process,
		   GString     *output,
		   GList       *known)
{
	if (known == NULL || output_full (output) || !arc_run_if_exited (&job->host, process)) {
		return FALSE;
	}

	return arc_only_skipped_links (job->backend,
				       arc_run_exit_status (&job->host, process),
				       output->str, known) &&
	       archive_written (job);
}

GSubprocessFlags
arc_tool_stderr_flag (void)
{
#ifdef G_OS_WIN32
	return G_SUBPROCESS_FLAGS_STDERR_PIPE;
#else
	return G_SUBPROCESS_FLAGS_STDERR_MERGE;
#endif
}

/* Read to the end even past the limit, or a tool with a lot to say would
   block on a full pipe. */
static gpointer
read_stderr (gpointer data)
{
	GInputStream *in = data;
	GString *text = g_string_new (NULL);
	char buffer[4096];
	gssize count;

	while ((count = g_input_stream_read (in, buffer, sizeof (buffer), NULL, NULL)) > 0) {
		if (text->len < OUTPUT_LIMIT) {
			g_string_append_len (text, buffer, MIN ((gsize) count, OUTPUT_LIMIT - text->len));
		}
	}

	g_object_unref (in);
	return g_string_free (text, FALSE);
}

/* Returns: (transfer full): hand it to arc_tool_stderr_finish, which joins it */
GThread *
arc_tool_stderr_start (const ArcHost *host,
		       ArcRun        *process)
{
	GInputStream *in = arc_run_stderr (host, process);

	if (in == NULL) {
		return NULL;
	}

	return g_thread_new ("tool-stderr", read_stderr, g_object_ref (in));
}

/* Returns: (transfer full): free with g_free */
char *
arc_tool_stderr_finish (GThread *reader)
{
	return reader != NULL ? g_thread_join (reader) : NULL;
}

/* Starts a tool line in the base folder and reads what it prints until it
   ends, moving the progress bar as it goes. The caller waits on what comes
   back. NULL if it could not be started. */
static ArcRun *
start_tool (ArcCompress *job,
	    char       **argv,
	    const char  *base_path,
	    GString     *output,
	    GError     **error)
{
	ArcRun *process;
	GInputStream *out;
	GThread *err_reader;
	char *err_text;
	char buffer[4096];

	process = arc_run_start (&job->host, (const gchar * const *) argv, base_path,
				 G_SUBPROCESS_FLAGS_STDOUT_PIPE | arc_tool_stderr_flag (),
				 error);
	if (process == NULL) {
		return NULL;
	}

	out = arc_run_stdout (&job->host, process);
	err_reader = arc_tool_stderr_start (&job->host, process);

	while (TRUE) {
		gssize count = g_input_stream_read (out, buffer, sizeof (buffer),
						    job->cancellable, NULL);
		int percent = -1;

		if (count <= 0) {
			break;
		}

		take_output (output, buffer, (gsize) count);

		if (job->bar_pulses) {
			arc_pulse (&job->host);
		} else if (scan_percent (buffer, (gsize) count, &percent)) {
			arc_fraction (&job->host, percent, 100);
		}
	}

	if (job_aborted (job)) {
		arc_run_force_exit (&job->host, process);
	}

	err_text = arc_tool_stderr_finish (err_reader);
	if (err_text != NULL && err_text[0] != '\0') {
		if (output->len > 0 && output->str[output->len - 1] != '\n') {
			take_output (output, "\n", 1);
		}
		take_output (output, err_text, strlen (err_text));
	}
	g_free (err_text);

	return process;
}

/* A name a list file would read as another: 7-Zip trims each line at both
   ends and rar at its end, and a line break ends one. Those go on the line. */
static gboolean
fits_a_list (const char *name)
{
	gsize length = strlen (name);

	return length > 0 && !g_ascii_isspace (name[0]) && !g_ascii_isspace (name[length - 1]) &&
	       strpbrk (name, "\r\n") == NULL;
}

/* Returns: (transfer full): the name as handed to a program here */
static char *
handed_name (const char *name)
{
	char *out = g_strdup (name);

#ifdef G_OS_WIN32
	g_strdelimit (out, "/", '\\');
#endif
	return out;
}

/* A list file this job made. */
static void
remove_list (const ArcHost *host,
	     const char    *path)
{
	GFile *file = g_file_new_for_path (path);

	arc_remove_own (host, file);
	g_object_unref (file);
}

/* One name to a line, UTF-8. Both programs take a list file, so a selection
   of any size stays under the command line's limit.
   Returns: (transfer full): the file's path, or NULL with error set */
static char *
write_list (const ArcHost  *host,
	    GPtrArray      *names,
	    GError        **error)
{
	GString *text = g_string_new (NULL);
	char *path = NULL;
	int fd;
	guint i;

	for (i = 0; i < names->len; i++) {
		char *name = handed_name (g_ptr_array_index (names, i));

		g_string_append (text, name);
		g_string_append_c (text, '\n');
		g_free (name);
	}

	fd = g_file_open_tmp ("nemo-archive-XXXXXX.lst", &path, error);
	if (fd >= 0) {
		g_close (fd, NULL);
		if (!g_file_set_contents (path, text->str, (gssize) text->len, error)) {
			remove_list (host, path);
			g_clear_pointer (&path, g_free);
		}
	}

	g_string_free (text, TRUE);
	return path;
}

/* Names that fit go in a list file, the rest on the line. lists gets the
   files made, for the caller to remove. */
static gboolean
hand_names (const ArcHost  *host,
	    ArcBackend      backend,
	    GPtrArray      *roots,
	    GPtrArray      *skips,
	    ArcToolNames   *out,
	    GPtrArray      *lists,
	    GError        **error)
{
	gboolean seven = backend == ARC_BACKEND_7Z;
	GPtrArray *listed = g_ptr_array_new ();
	gboolean charset = FALSE;
	gboolean ok = TRUE;
	guint pass, i;

	arc_tool_names_init (out);

	for (pass = 0; pass < 2 && ok; pass++) {
		GPtrArray *names = pass == 0 ? roots : skips;
		char *path;

		g_ptr_array_set_size (listed, 0);
		for (i = 0; i < names->len; i++) {
			const char *name = g_ptr_array_index (names, i);
			char *word;

			if (fits_a_list (name)) {
				g_ptr_array_add (listed, (gpointer) name);
				continue;
			}
			word = handed_name (name);
			if (pass == 0) {
				g_ptr_array_add (out->sources, seven ? g_strdup (word) : arc_rar_name ("", word));
			} else {
				g_ptr_array_add (out->skips, seven ? g_strconcat ("-x!", word, NULL)
								   : arc_rar_name ("-x", word));
			}
			g_free (word);
		}
		if (listed->len == 0) {
			continue;
		}

		path = write_list (host, listed, error);
		if (path == NULL) {
			ok = FALSE;
			continue;
		}
		g_ptr_array_add (lists, path);
		if (!charset) {
			g_ptr_array_add (out->moved, g_strdup (seven ? "-scsUTF-8" : "-scfl"));
			charset = TRUE;
		}
		/* rar takes @list after a "--", 7-Zip only before one. */
		if (pass == 1) {
			g_ptr_array_add (out->skips, g_strconcat ("-x@", path, NULL));
		} else if (seven) {
			g_ptr_array_add (out->moved, g_strconcat ("-i@", path, NULL));
		} else {
			g_ptr_array_add (out->sources, g_strconcat ("@", path, NULL));
		}
	}

	g_ptr_array_unref (listed);
	return ok;
}

/* How the list goes to 7-Zip or rar. Neither keeps some links and follows
   the rest, so where both are wanted the links go in first, by a run of
   their own that keeps links, and the real run adds the rest, with every
   link left out. All names are the list's, so borrowed. */
typedef struct {
	GPtrArray *roots;	/* what the run is handed */
	GPtrArray *skips;	/* what it leaves out, below a root */
	GPtrArray *links;	/* the run ahead, links by name */
	gboolean   keep;	/* the one run keeps links, as they're all stored */
} RunPlan;

static void
plan_runs (ArcCompress *job,
	   RunPlan     *plan)
{
	const ArcScanList *list = job->list;
	const GPtrArray *not_taken[3] = { list->left_out, list->unreadable, list->again };
	gboolean can_keep = (arc_backend_caps (job->options.format, job->backend) &
			     ARC_CAP_STORE_LINKS) != 0;
	gboolean followed = FALSE;
	gboolean any_link = FALSE;
	guint i, k;

	plan->roots = g_ptr_array_new ();
	plan->skips = g_ptr_array_new ();
	plan->links = g_ptr_array_new ();
	plan->keep = FALSE;

	for (i = 0; i < list->items->len; i++) {
		const ArcItem *item = g_ptr_array_index (list->items, i);

		if (item->kind == ARC_ITEM_LINK) {
			any_link = TRUE;
		} else if (item->needs & (ARC_FOLLOW_SYMLINKS | ARC_FOLLOW_JUNCTIONS)) {
			followed = TRUE;
		}
	}
	plan->keep = any_link && can_keep && !followed;

	/* Top-level names are the selection, and one left out just isn't
	   handed over. */
	for (i = 0; i < list->items->len; i++) {
		const ArcItem *item = g_ptr_array_index (list->items, i);

		if (strchr (item->name, '/') == NULL && (item->kind != ARC_ITEM_LINK || plan->keep)) {
			g_ptr_array_add (plan->roots, item->name);
		}
	}
	for (k = 0; k < G_N_ELEMENTS (not_taken); k++) {
		for (i = 0; i < not_taken[k]->len; i++) {
			char *name = g_ptr_array_index (not_taken[k], i);

			if (strchr (name, '/') != NULL) {
				g_ptr_array_add (plan->skips, name);
			}
		}
	}
	if (!any_link || plan->keep) {
		return;
	}

	/* Neither adds to a split set, 7-Zip on Windows keeps no links, and
	   7-Zip won't keep one it reaches through a linked folder. */
	for (i = 0; i < list->items->len; i++) {
		const ArcItem *item = g_ptr_array_index (list->items, i);
		gboolean odd;

		if (item->kind != ARC_ITEM_LINK) {
			continue;
		}
		/* rar would read * or ? in it as a pattern. One that leads
		   nowhere it just can't open, so it's passed over, as the
		   library writer passes over one under Ignore. */
		odd = job->backend == ARC_BACKEND_RAR && item->nowhere &&
		      strpbrk (item->name, "*?") != NULL;
		if (strchr (item->name, '/') != NULL && !odd) {
			g_ptr_array_add (plan->skips, item->name);
		}
		if (!can_keep || job->options.split_size > 0 || odd ||
		    (job->backend == ARC_BACKEND_7Z &&
		     (item->needs & (ARC_FOLLOW_SYMLINKS | ARC_FOLLOW_JUNCTIONS)))) {
			job->passed = g_list_prepend (job->passed, g_strdup (item->name));
		} else {
			g_ptr_array_add (plan->links, item->name);
		}
	}
}

static void
run_plan_clear (RunPlan *plan)
{
	g_clear_pointer (&plan->roots, g_ptr_array_unref);
	g_clear_pointer (&plan->skips, g_ptr_array_unref);
	g_clear_pointer (&plan->links, g_ptr_array_unref);
}

/* rar reads * and ? in any name it is handed as a wildcard, and has no switch
   to say otherwise. What it finds in a folder by itself is taken as it is. */
static const char *
first_pattern (const RunPlan *plan)
{
	const GPtrArray *sets[3] = { plan->roots, plan->skips, plan->links };
	guint k, i;

	for (k = 0; k < G_N_ELEMENTS (sets); k++) {
		for (i = 0; i < sets[k]->len; i++) {
			const char *name = g_ptr_array_index (sets[k], i);

			if (strpbrk (name, "*?") != NULL) {
				return name;
			}
		}
	}

	return NULL;
}

/* One run of the program over roots, leaving out skips. known is what it
   may warn about and still pass. */
static gboolean
run_tool (ArcCompress *job,
	  const char  *program,
	  const char  *base_path,
	  const char  *archive_path,
	  GPtrArray   *roots,
	  GPtrArray   *skips,
	  gboolean     keep_links,
	  gboolean     links_only,
	  GList       *known,
	  GString     *output,
	  GError     **error)
{
	GPtrArray *lists = g_ptr_array_new_with_free_func (g_free);
	ArcRun *process = NULL;
	ArcToolNames words = { NULL, NULL, NULL };
	char **argv = NULL;
	gboolean ok = FALSE;
	guint i;

	if (hand_names (&job->host, job->backend, roots, skips, &words, lists, error)) {
		argv = arc_build_tool_command (&job->settings, &job->host, job->backend, job->options.format,
					       &job->options, program, archive_path, &words,
					       keep_links, links_only, error);
	}
	if (argv != NULL) {
		process = start_tool (job, argv, base_path, output, error);
	}
	if (process != NULL) {
		ok = arc_run_wait_check (&job->host, process, error);
		if (!ok && !job_aborted (job) && only_known_warned (job, process, output, known)) {
			g_clear_error (error);
			ok = TRUE;
		}
		arc_run_free (&job->host, process);
	}

	/* Ours, made above. */
	for (i = 0; i < lists->len; i++) {
		remove_list (&job->host, g_ptr_array_index (lists, i));
	}
	g_ptr_array_unref (lists);
	arc_tool_names_clear (&words);
	g_strfreev (argv);

	return ok;
}

static GList *
names_to_list (GList *list, GPtrArray *names)
{
	guint i;

	for (i = 0; i < names->len; i++) {
		list = g_list_prepend (list, g_ptr_array_index (names, i));
	}
	return list;
}

static gboolean
run_command (ArcCompress *job)
{
	const char *program = arc_backend_program (job->backend);
	char *base_path = NULL;
	char *archive_path = NULL;
	const char *pattern;
	RunPlan plan = { NULL, NULL, NULL, FALSE };
	GList *known = NULL;
	GError *error = NULL;
	GString *output;
	gboolean ok = FALSE;
	gboolean any = FALSE;

	base_path = g_file_get_path (job->base_dir);
	archive_path = g_file_get_path (job->destination);

	if (program == NULL || base_path == NULL || archive_path == NULL) {
		job_fail (job, _("The archive couldn't be created."),
			  _("This format needs a program that isn't installed, or a folder on this computer."));
		goto out;
	}

	if (!prescan (job)) {
		goto out;
	}
	if (list_has_nothing (job)) {
		ok = job->unit_empty;
		goto out;
	}
	plan_runs (job, &plan);

	pattern = job->backend == ARC_BACKEND_RAR ? first_pattern (&plan) : NULL;
	if (pattern != NULL) {
		char *details = g_strdup_printf (
			_("\"%s\" has * or ? in its name, which rar reads as a wildcard, so other files could go in or be left out with it. Choose another format, or rename it."),
			pattern);

		job_fail (job, _("The archive couldn't be created."), details);
		g_free (details);
		goto out;
	}

	/* Both tools ADD to an archive that is already there rather than
	   replacing it, so an overwrite has to be a delete first. */
	if (g_file_query_exists (job->destination, NULL)) {
		arc_remove_own (&job->host, job->destination);
	}

	set_unit_status (job, g_strdup_printf (_("Compressing with %s"),
					       job->backend == ARC_BACKEND_RAR ? "rar" : "7z"));

	output = g_string_new (NULL);
	if (plan.links->len > 0) {
		GPtrArray *none = g_ptr_array_new ();

		/* Its 0 to 100 would only go back to 0 for the rest. */
		job->bar_pulses = plan.roots->len > 0;
		any = run_tool (job, program, base_path, archive_path, plan.links, none,
				TRUE, TRUE, NULL, output, NULL);
		job->bar_pulses = FALSE;
		g_ptr_array_unref (none);
		/* Left out after all, and named. */
		if (!any && !job_aborted (job)) {
			char *said = output_tail (output);
			guint i;

			g_warning ("Links couldn't be kept, so they are left out: %s", said);
			g_free (said);
			arc_remove_own (&job->host, job->destination);
			for (i = 0; i < plan.links->len; i++) {
				job->passed = g_list_prepend (job->passed,
							      g_strdup (g_ptr_array_index (plan.links, i)));
			}
		}
		g_string_truncate (output, 0);
	}
	if (job_aborted (job)) {
		g_string_free (output, TRUE);
		goto out;
	}

	/* rar fails a run that adds nothing. */
	if (plan.roots->len == 0) {
		ok = any;
		if (!ok) {
			job_fail (job, _("The archive couldn't be created."),
				  _("Nothing in the selection could go in."));
		}
		g_string_free (output, TRUE);
		goto out;
	}

	known = names_to_list (known, plan.skips);
	/* cppcheck-suppress leakNoVarFunctionCall ; the copy is the head of the list concat returns */
	known = g_list_concat (g_list_copy (job->passed), known);
	ok = run_tool (job, program, base_path, archive_path, plan.roots, plan.skips,
		       plan.keep, FALSE, known, output, &error);
	if (!ok && !job_aborted (job)) {
		char *said = output_tail (output);

		/* rar can end with nothing but blank lines, which says nothing. */
		job_fail (job, _("The archive couldn't be created."),
			  said[0] != '\0' ? said : (error != NULL ? error->message : NULL));
		g_free (said);
	}
	g_clear_error (&error);
	g_string_free (output, TRUE);

 out:
	g_list_free (known);
	run_plan_clear (&plan);
	g_free (base_path);
	g_free (archive_path);

	return ok && !job_aborted (job);
}

/* Nothing is removed on the writer's word alone. Only the sources behind an
   archive that read back whole go on the list, and the first check that would
   not run is what gets said - a dialog per unit would be no help. */
static void
verify_unit (ArcCompress *job)
{
	char *trouble = NULL;
	GList *l;

	arc_status (&job->host, _("Checking the archive"));

	if (arc_verify (&job->host, job->destination, job->sources, &job->options,
				 job->backend, job->cancellable, &trouble)) {
		for (l = job->sources; l != NULL; l = l->next) {
			job->verified = g_list_prepend (job->verified,
							g_object_ref (G_FILE (l->data)));
		}
		return;
	}

	if (job->verify_trouble == NULL) {
		job->verify_trouble = trouble;
	} else {
		g_free (trouble);
	}
}

/* The links a job couldn't keep, named for the warning. NULL if none. */
static char *
passed_over_note (GList *names)
{
	guint count = g_list_length (names);
	guint shown = 0;
	GString *note;
	GList *l;

	if (count == 0) {
		return NULL;
	}

	note = g_string_new (ngettext ("It can't go in as a link with these options:",
				       "They can't go in as links with these options:",
				       count));
	for (l = names; l != NULL && shown < 10; l = l->next, shown++) {
		g_string_append_printf (note, "\n%s", (const char *) l->data);
	}
	if (count > shown) {
		g_string_append_c (note, '\n');
		g_string_append_printf (note, ngettext ("and %u more", "and %u more", count - shown),
					count - shown);
	}

	return g_string_free (note, FALSE);
}

/* Main thread. */
void
arc_compress_finish (ArcCompress *job)
{
	char *note;

	g_return_if_fail (job != NULL);

	if (job->error_message != NULL) {
		arc_show_error (&job->host, job->error_message, job->error_details);
	}

	/* Only a delete sets verify_trouble. One dialog, not two, when the
	   originals stayed because of the links. */
	note = passed_over_note (job->passed_over);
	if (job->verify_trouble != NULL) {
		char *details = note != NULL
			? g_strconcat (job->verify_trouble, "\n\n", note, NULL)
			: g_strdup (job->verify_trouble);

		arc_show_warning (&job->host, _("The original files were kept."), details);
		g_free (details);
	} else if (job->success && note != NULL) {
		arc_show_warning (&job->host,
				  ngettext ("A link was left out of the archive.",
					    "Some links were left out of the archive.",
					    g_list_length (job->passed_over)),
				  note);
	}
	g_free (note);

	/* The box was ticked in the Compress dialog, so this is a delete a person
	   asked for, and it goes through the ordinary trash-or-delete with its own
	   confirmation rather than anything quieter here. A cancelled job removes
	   nothing, even where an earlier archive of it did check out. */
	if (job->options.delete_sources) {
		if (job->verified != NULL && !job_aborted (job)) {
			job->verified = g_list_reverse (job->verified);
			arc_trash_by_user (&job->host, job->verified);
		}
	}
}

void
arc_compress_free (ArcCompress *job)
{
	if (job == NULL) {
		return;
	}

	g_clear_pointer (&job->list, arc_scan_list_free);
	g_list_free_full (job->passed, g_free);
	g_list_free_full (job->units, (GDestroyNotify) archive_unit_free);
	g_list_free_full (job->verified, g_object_unref);
	g_list_free_full (job->passed_over, g_free);
	g_free (job->verify_trouble);
	g_clear_object (&job->result_file);
	g_clear_object (&job->base_dir);
	g_clear_object (&job->cancellable);
	arc_options_clear (&job->options);
	arc_settings_clear (&job->settings);
	g_free (job->error_message);
	g_free (job->error_details);
	g_free (job);
}

/* Per-archive state, taken up and put down around each unit so nothing from
   one archive is still standing when the next starts. */
static void
start_unit (ArcCompress *job,
	    ArchiveUnit *unit)
{
	char *name;

	job->sources = unit->sources;
	job->destination = unit->destination;
	job->base_dir = unit->sources != NULL ?
		g_file_get_parent (G_FILE (unit->sources->data)) : NULL;

	job->total_bytes = 0;
	job->done_bytes = 0;
	job->file_count = 0;
	/* Not the last archive's full bar while this one's pre-scan runs. */
	arc_fraction (&job->host, 0, 1);

	name = g_file_get_basename (unit->destination);
	arc_details (&job->host, name);
	g_free (name);
}

static void
finish_unit (ArcCompress *job)
{
	g_clear_pointer (&job->list, arc_scan_list_free);
	job->passed_over = g_list_concat (job->passed_over, g_list_reverse (job->passed));
	job->passed = NULL;
	job->unit_empty = FALSE;

	g_clear_object (&job->base_dir);
	job->sources = NULL;
	job->destination = NULL;
}

void
arc_compress_run (ArcCompress *job)
{
	GList *l;

	g_return_if_fail (job != NULL);

	job->success = TRUE;

	for (l = job->units; l != NULL && !job_aborted (job); l = l->next) {
		gboolean ok;

		start_unit (job, l->data);

		if (job->base_dir == NULL) {
			job_fail (job, _("The archive couldn't be created."),
				  _("The selection has no folder above it to compress from."));
			ok = FALSE;
		} else if (job->backend == ARC_BACKEND_LIBARCHIVE) {
			ok = run_libarchive (job);
		} else {
			ok = run_command (job);
		}

		if (ok && job->unit_empty) {
			/* Nothing was written, so there is nothing to check, and
			   the item stays where it is. */
			if (job->options.delete_sources && job->verify_trouble == NULL) {
				char *name = g_file_get_basename (G_FILE (job->sources->data));

				job->verify_trouble = g_strdup_printf (
					_("\"%s\" is a link or a mounted folder the options leave out, so it has no archive."),
					name);
				g_free (name);
			}
			finish_unit (job);
			job->unit_index++;
			continue;
		}

		/* A half-written archive is worse than none: it looks like a
		   result. Splitting means clearing the volumes, not the name. */
		if (!ok) {
			arc_remove_own (&job->host, job->destination);
			if (job->options.split_size > 0) {
				arc_for_each_volume (job->destination, job->backend, delete_one, job);
			}
		} else if (job->options.split_size > 0) {
			GFile *written = arc_collapse_volume (job->destination,
							      job->backend);

			arc_file_added (&job->host, written);
			arc_for_each_volume (job->destination, job->backend, queue_one, job);

			/* One unit means the callback's file is this one, and a
			   collapsed volume is under a different name than asked. */
			if (job->units->next == NULL) {
				g_set_object (&job->result_file, written);
			}
			g_object_unref (written);
		} else {
			arc_file_added (&job->host, job->destination);
		}

		if (ok && job->options.delete_sources) {
			verify_unit (job);
		}

		/* One archive failing does not take the rest of them with it -
		   the first thing that went wrong is what gets reported. */
		job->success = job->success && ok;

		finish_unit (job);
		job->unit_index++;
	}

	if (job_aborted (job)) {
		job->success = FALSE;
	}
}

static ArchiveUnit *
archive_unit_new (GList *sources,
		  GFile *destination)
{
	ArchiveUnit *unit = g_new0 (ArchiveUnit, 1);
	GList *l;

	for (l = sources; l != NULL; l = l->next) {
		unit->sources = g_list_prepend (unit->sources, g_object_ref (G_FILE (l->data)));
	}
	unit->sources = g_list_reverse (unit->sources);
	unit->destination = g_object_ref (destination);

	return unit;
}

/* Returns: (transfer full): free with arc_compress_free */
ArcCompress *
arc_compress_new (const ArcHost     *host,
		  const ArcSettings *settings,
		  const ArcOptions  *options,
		  GCancellable      *cancellable,
		  GFile             *result_file)
{
	ArcCompress *job;

	g_return_val_if_fail (host != NULL, NULL);
	g_return_val_if_fail (settings != NULL, NULL);
	g_return_val_if_fail (options != NULL, NULL);
	g_return_val_if_fail (G_IS_FILE (result_file), NULL);

	job = g_new0 (ArcCompress, 1);
	job->host = *host;
	arc_settings_copy (&job->settings, settings);
	arc_options_copy (options, &job->options);
	job->backend = arc_pick_backend (options->format, options);
	job->cancellable = cancellable != NULL ? g_object_ref (cancellable) : g_cancellable_new ();
	job->result_file = g_object_ref (result_file);

	return job;
}

void
arc_compress_add (ArcCompress *job,
		  GList       *sources,
		  GFile       *destination)
{
	g_return_if_fail (job != NULL);
	g_return_if_fail (sources != NULL);
	g_return_if_fail (G_IS_FILE (destination));

	job->units = g_list_append (job->units, archive_unit_new (sources, destination));
	job->unit_count++;
}

guint
arc_compress_count (ArcCompress *job)
{
	g_return_val_if_fail (job != NULL, 0);

	return job->unit_count;
}

/* Returns: (transfer none) */
GFile *
arc_compress_destination (ArcCompress *job,
			  guint        index)
{
	ArchiveUnit *unit;

	g_return_val_if_fail (job != NULL, NULL);

	unit = g_list_nth_data (job->units, index);
	return unit != NULL ? unit->destination : NULL;
}

ArcBackend
arc_compress_backend (ArcCompress *job)
{
	g_return_val_if_fail (job != NULL, ARC_BACKEND_NONE);

	return job->backend;
}

gboolean
arc_compress_can_start (ArcCompress *job)
{
	g_return_val_if_fail (job != NULL, FALSE);

	if (job->backend == ARC_BACKEND_NONE) {
		job_fail (job, _("The archive couldn't be created."),
			  _("Nothing installed here can write this archive with the options chosen."));
		return FALSE;
	}
	return TRUE;
}

gboolean
arc_compress_succeeded (ArcCompress *job)
{
	g_return_val_if_fail (job != NULL, FALSE);

	return job->success;
}

/* Returns: (transfer none) */
GFile *
arc_compress_result (ArcCompress *job)
{
	g_return_val_if_fail (job != NULL, NULL);

	return job->result_file;
}
