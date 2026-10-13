/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */

/* arc-extract.c - unpacking archives, as a job.

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

#include "arc-extract.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>

#include <glib/gi18n.h>
#include <archive.h>
#include <archive_entry.h>

#include "arc-command.h"
#include "arc-compress.h"	/* the tools' stderr reader */
#include "arc-extract-names.h"
#include "arc-verify.h"
#include "arc-writer.h"

#define READ_BUFFER_SIZE (64 * 1024)

/* Stands for "this folder was skipped" in the folder cache, where a plain NULL
   cannot be told apart from a miss. Its own address, so it can never collide
   with a real GFile and needs no cast from a number. */
static const char skipped_marker;
#define SKIPPED ((gpointer) &skipped_marker)

typedef enum {
	EXTRACT_OK,
	EXTRACT_UNSUPPORTED,	/* the file would not open; another backend may */
	EXTRACT_FAILED
} ExtractResult;

struct _ArcExtract {
	ArcHost             host;
	ArcSettings         settings;
	GCancellable       *cancellable;

	GList              *archives;		/* GFile * */
	GFile              *destination_dir;
	ArcExtractLayout    layout;

	/* Per-archive: where this one is being unpacked, and what its folders
	   turned into once names were resolved. Keyed on the path the archive
	   uses, so a renamed folder carries its children with it. */
	GFile              *current_base;
	GHashTable         *dir_map;		/* char * -> GFile * or SKIPPED */
	char               *current_name;	/* the archive being read */

	/* An answer the user asked to have applied to every conflict. */
	gboolean            have_default;
	ArcConflictResponse default_response;

	/* One archive going wrong stops that archive, not the rest of them.
	   Cancelling stops everything. */
	gboolean            archive_failed;

	/* Whether this archive has put anything on disk yet. Nothing yet means
	   handing it to a command instead is still free. */
	gboolean            wrote_anything;

	/* Whether libarchive gave up on this one over a password. A command
	   backend can often still open it, given one. */
	gboolean            maybe_encrypted;

	/* Where libarchive stopped at a rar link it can't read, and what it
	   had placed and passed by then. A command then unpacks the whole
	   archive, and only what libarchive never got to is placed from it. */
	char               *long_link;
	guint               placed;
	GHashTable         *handled;		/* entry paths, cleaned */

	char               *password;
	gboolean            password_asked;
	gboolean            password_declined;

	guint64             total_bytes;	/* over every archive */
	guint64             done_bytes;		/* archives finished so far */
	guint               done_count;
	guint               failed_count;

	char               *error_message;
	char               *error_details;
	gboolean            success;
};

typedef ArcExtract ExtractJob;

/* The GInputStream libarchive reads through. Going in via GIO rather than a
   filename keeps one code path for local and remote sources, and sidesteps the
   byte-versus-wide filename split on Windows. */
typedef struct {
	GInputStream *stream;
	GCancellable *cancellable;
	GError       *error;
	char          buffer[READ_BUFFER_SIZE];
} StreamSource;

/* Which commands are worth trying, best first: a rar file goes to a rar tool
   ahead of 7-Zip, anything else the other way round. Both are listed rather
   than one picked, because being installed is no promise of being able to read
   the file - an old build on the PATH can be years behind the format. */
static guint
command_backends_for (const char *name,
		      ArcBackend *order)
{
	gboolean is_rar = arc_extract_is_rar_name (name);
	ArcBackend first = is_rar ? ARC_BACKEND_RAR : ARC_BACKEND_7Z;
	ArcBackend second = is_rar ? ARC_BACKEND_7Z : ARC_BACKEND_RAR;
	guint count = 0;

	if (arc_extract_backend_present (first)) {
		order[count++] = first;
	}
	if (arc_extract_backend_present (second)) {
		order[count++] = second;
	}

	return count;
}

/* The job's own bookkeeping */

static gboolean
job_cancelled (ExtractJob *job)
{
	return g_cancellable_is_cancelled (job->cancellable);
}

static gboolean
job_aborted (ExtractJob *job)
{
	return job->archive_failed || job_cancelled (job);
}

static void
job_fail (ExtractJob *job,
	  const char *message,
	  const char *details)
{
	job->archive_failed = TRUE;

	/* The first thing that went wrong is the one worth reporting; what
	   follows it is usually a consequence. */
	if (job->error_message != NULL) {
		return;
	}

	job->error_message = g_strdup (message);
	job->error_details = g_strdup (details);
}

/*
 * Asking the user, from the job thread
 */

/* Whether a message is about a password. Nothing reports that in an exit code -
   libarchive and both commands only ever say so in words. */
static gboolean
looks_like_password_trouble (const char *text)
{
	char *folded;
	gboolean found;

	if (text == NULL) {
		return FALSE;
	}

	folded = g_ascii_strdown (text, -1);
	found = strstr (folded, "password") != NULL || strstr (folded, "encrypt") != NULL;
	g_free (folded);

	return found;
}

/* Asked once per job: an archive set usually shares one password, and asking
   again for every entry of a big one would be unusable. */
static void
ask_password (ExtractJob *job)
{
	char *password;

	if (job->password_asked) {
		return;
	}
	job->password_asked = TRUE;

	password = arc_ask_password (&job->host, job->current_name);

	if (password != NULL && password[0] != '\0') {
		job->password = password;
	} else {
		g_free (password);
		job->password_declined = TRUE;
	}
}

/*
 * Placing things on disk
 */

/* Picks the file an entry should be written to, asking about anything already
   there. Returns NULL when the entry is to be skipped, or when the job was
   cancelled - job_aborted tells the two apart. */
static GFile *
resolve_target (ExtractJob *job,
		GFile      *dir,
		const char *name,
		gboolean    entry_is_dir,
		guint64     entry_size,
		gint64      entry_mtime)
{
	char *candidate = g_strdup (name);
	guint attempt = 0;

	for (;;) {
		GFile *target;
		GFileInfo *info;
		ArcConflictAnswer answer = { 0 };
		ArcConflictResponse response;

		if (job_aborted (job)) {
			g_free (candidate);
			return NULL;
		}

		target = g_file_get_child (dir, candidate);
		info = g_file_query_info (target, G_FILE_ATTRIBUTE_STANDARD_TYPE,
					  G_FILE_QUERY_INFO_NOFOLLOW_SYMLINKS, job->cancellable, NULL);

		if (info == NULL) {
			g_free (candidate);
			return target;
		}

		/* A folder arriving on a folder is a merge, which is what every
		   archive with a shared top-level folder expects. */
		if (entry_is_dir && g_file_info_get_file_type (info) == G_FILE_TYPE_DIRECTORY) {
			g_object_unref (info);
			g_free (candidate);
			return target;
		}

		g_object_unref (info);

		if (job->have_default) {
			response = job->default_response;
		} else {
			ArcConflict question = {
				.archive_name = job->current_name,
				.entry_name = candidate,
				.entry_is_dir = entry_is_dir,
				.entry_size = entry_size,
				.entry_mtime = entry_mtime,
				.destination = target,
				.destination_dir = dir,
			};

			arc_ask_conflict (&job->host, &question, &answer);
			response = answer.response;

			if (answer.apply_to_all && response != ARC_CONFLICT_RENAME) {
				job->default_response = response;
				job->have_default = TRUE;
			}
		}

		if (response == ARC_CONFLICT_REPLACE) {
			arc_remove_tree (&job->host, target, job->cancellable);
			g_free (candidate);
			g_free (answer.new_name);
			return target;
		}

		g_object_unref (target);

		if (response == ARC_CONFLICT_SKIP) {
			g_free (candidate);
			g_free (answer.new_name);
			return NULL;
		}

		if (response == ARC_CONFLICT_RENAME && answer.new_name != NULL &&
		    answer.new_name[0] != '\0') {
			g_free (candidate);
			candidate = answer.new_name;
			answer.new_name = NULL;
			continue;
		}

		g_free (answer.new_name);

		if (response == ARC_CONFLICT_AUTO_RENAME) {
			attempt++;
			g_free (candidate);
			candidate = arc_extract_unique_name (name, attempt);
			continue;
		}

		/* Cancel, or the window closed - the whole job stops. */
		g_cancellable_cancel (job->cancellable);
		g_free (candidate);
		return NULL;
	}
}

static void
note_created (ExtractJob *job,
	      GFile      *file)
{
	GFile *parent = g_file_get_parent (file);

	job->wrote_anything = TRUE;

	/* Only what arrives directly in the folder being unpacked into is news to
	   a view; anything deeper is inside something already reported. */
	if (parent != NULL && job->current_base != NULL && g_file_equal (parent, job->current_base)) {
		arc_file_added (&job->host, file);
	}

	g_clear_object (&parent);
}

static GFile *dir_for_rel (ExtractJob *job, const char *rel_dir);

static GFile *
make_child_dir (ExtractJob *job,
		GFile      *parent,
		const char *name)
{
	GFile *target;
	GError *error = NULL;

	target = resolve_target (job, parent, name, TRUE, 0, 0);
	if (target == NULL) {
		return NULL;
	}

	if (!g_file_make_directory (target, job->cancellable, &error)) {
		if (!g_error_matches (error, G_IO_ERROR, G_IO_ERROR_EXISTS)) {
			g_clear_error (&error);
			g_object_unref (target);
			return NULL;
		}
		g_clear_error (&error);
	} else {
		note_created (job, target);
	}

	return target;
}

/* The real folder an archive's folder path turned into, created on the way.
   The answer is remembered, so a folder renamed once keeps every later entry
   underneath it without any path rewriting at the call sites. */
static GFile *
dir_for_rel (ExtractJob *job,
	     const char *rel_dir)
{
	gpointer cached;
	const char *name;
	char *parent_rel;
	char *slash;
	GFile *parent_dir;
	GFile *dir;

	if (rel_dir == NULL || rel_dir[0] == '\0') {
		return job->current_base;
	}

	cached = g_hash_table_lookup (job->dir_map, rel_dir);
	if (cached != NULL) {
		return cached == SKIPPED ? NULL : cached;
	}

	slash = strrchr (rel_dir, '/');
	if (slash != NULL) {
		parent_rel = g_strndup (rel_dir, slash - rel_dir);
		name = slash + 1;
	} else {
		parent_rel = g_strdup ("");
		name = rel_dir;
	}

	parent_dir = dir_for_rel (job, parent_rel);
	g_free (parent_rel);

	dir = parent_dir != NULL ? make_child_dir (job, parent_dir, name) : NULL;

	g_hash_table_insert (job->dir_map, g_strdup (rel_dir), dir != NULL ? dir : SKIPPED);

	return dir;
}

/* Splits an entry path into the folder it lives in and its own name, resolving
   the folder. Returns NULL when anything along the way was skipped. */
static GFile *
target_for_entry (ExtractJob  *job,
		  const char  *rel_path,
		  gboolean     is_dir,
		  guint64      size,
		  gint64       mtime)
{
	char *slash;
	char *rel_dir;
	GFile *dir;
	GFile *target;

	if (is_dir) {
		dir = dir_for_rel (job, rel_path);

		return dir != NULL ? g_object_ref (dir) : NULL;
	}

	slash = strrchr (rel_path, '/');
	rel_dir = slash != NULL ? g_strndup (rel_path, slash - rel_path) : g_strdup ("");

	dir = dir_for_rel (job, rel_dir);
	g_free (rel_dir);

	if (dir == NULL) {
		return NULL;
	}

	target = resolve_target (job, dir, slash != NULL ? slash + 1 : rel_path, FALSE, size, mtime);

	return target;
}

static void
apply_entry_metadata (ExtractJob *job,
		      GFile      *target,
		      gint64      mtime,
		      guint       mode)
{
	if (mtime > 0) {
		g_file_set_attribute_uint64 (target, G_FILE_ATTRIBUTE_TIME_MODIFIED,
					     (guint64) mtime, G_FILE_QUERY_INFO_NOFOLLOW_SYMLINKS,
					     job->cancellable, NULL);
	}

#ifndef G_OS_WIN32
	/* Worth carrying across for the executable bit alone; win32 has no
	   mode to set and fabricates one on the way back out. */
	if (mode != 0) {
		g_file_set_attribute_uint32 (target, G_FILE_ATTRIBUTE_UNIX_MODE, mode,
					     G_FILE_QUERY_INFO_NOFOLLOW_SYMLINKS,
					     job->cancellable, NULL);
	}
#else
	(void) mode;
#endif
}

/*
 * Reading with libarchive
 */

static la_ssize_t
source_read (struct archive *a,
	     void           *client_data,
	     const void    **buff)
{
	StreamSource *src = client_data;
	gssize count;

	count = g_input_stream_read (src->stream, src->buffer, sizeof (src->buffer),
				     src->cancellable, &src->error);
	if (count < 0) {
		archive_set_error (a, EIO, "%s",
				   src->error != NULL ? src->error->message : "read failed");
		return -1;
	}

	*buff = src->buffer;

	return count;
}

static la_int64_t
source_seek (struct archive *a,
	     void           *client_data,
	     la_int64_t      offset,
	     int             whence)
{
	StreamSource *src = client_data;
	GSeekType type;

	(void) a;

	switch (whence) {
	case SEEK_SET:	type = G_SEEK_SET; break;
	case SEEK_CUR:	type = G_SEEK_CUR; break;
	case SEEK_END:	type = G_SEEK_END; break;
	default:	return ARCHIVE_FATAL;
	}

	if (!g_seekable_seek (G_SEEKABLE (src->stream), offset, type, src->cancellable, NULL)) {
		return ARCHIVE_FATAL;
	}

	return g_seekable_tell (G_SEEKABLE (src->stream));
}

static int
source_close (struct archive *a,
	      void           *client_data)
{
	StreamSource *src = client_data;

	(void) a;

	g_input_stream_close (src->stream, NULL, NULL);

	return ARCHIVE_OK;
}

static const char *
passphrase_cb (struct archive *a,
	       void           *client_data)
{
	ExtractJob *job = client_data;

	(void) a;

	if (job->password == NULL && !job->password_declined) {
		ask_password (job);
	}

	return job->password;
}

static gboolean
write_entry_data (ExtractJob     *job,
		  struct archive *a,
		  GFile          *target,
		  GError        **error)
{
	GFileOutputStream *out;
	guint64 written = 0;
	gboolean ok = TRUE;

	out = g_file_replace (target, NULL, FALSE, G_FILE_CREATE_REPLACE_DESTINATION,
			      job->cancellable, error);
	if (out == NULL) {
		return FALSE;
	}

	for (;;) {
		const void *block;
		size_t size;
		la_int64_t offset;
		int result;

		result = archive_read_data_block (a, &block, &size, &offset);

		if (result == ARCHIVE_EOF) {
			break;
		}
		if (result < ARCHIVE_WARN) {
			g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_FAILED,
					     archive_error_string (a));
			ok = FALSE;
			break;
		}

		/* A sparse file arrives as blocks with gaps between them. Skip
		   the gap where the stream can, and pad it where it cannot. */
		if ((guint64) offset > written) {
			if (g_seekable_can_seek (G_SEEKABLE (out))) {
				if (!g_seekable_seek (G_SEEKABLE (out), offset, G_SEEK_SET,
						      job->cancellable, error)) {
					ok = FALSE;
					break;
				}
			} else {
				static const char zeros[4096] = { 0 };
				guint64 gap = (guint64) offset - written;

				while (gap > 0 && ok) {
					gsize chunk = MIN (gap, sizeof (zeros));

					ok = g_output_stream_write_all (G_OUTPUT_STREAM (out), zeros,
									chunk, NULL, job->cancellable, error);
					gap -= chunk;
				}
				if (!ok) {
					break;
				}
			}
			written = (guint64) offset;
		}

		if (!g_output_stream_write_all (G_OUTPUT_STREAM (out), block, size, NULL,
						job->cancellable, error)) {
			ok = FALSE;
			break;
		}

		written += size;
	}

	if (!g_output_stream_close (G_OUTPUT_STREAM (out), NULL, ok ? error : NULL)) {
		ok = FALSE;
	}

	g_object_unref (out);

	return ok;
}

/* Nothing after the link can be read, so a command takes the archive over,
   whatever is on disk already. */
static ExtractResult
stopped_at_long_link (ExtractJob           *job,
		      struct archive_entry *entry)
{
	const char *path = archive_entry_pathname_utf8 (entry);

	if (path == NULL) {
		path = archive_entry_pathname (entry);
	}
	job->long_link = path != NULL ? arc_extract_sanitize_path (path) : NULL;
	if (job->long_link == NULL) {
		job->long_link = g_strdup ("");
	}

	return EXTRACT_UNSUPPORTED;
}

/* Says where the reader stopped and what had been unpacked by then, so a
   part of the archive doesn't pass for all of it. */
static char *
long_link_text (ExtractJob *job,
		gboolean    installed)
{
	GString *text = g_string_new (NULL);

	g_string_append_printf (text, _("\"%s\" has a link whose target is too long for the built-in reader."),
				job->current_name);
	if (job->placed > 0) {
		g_string_append_c (text, ' ');
		g_string_append_printf (text, ngettext ("Unpacking stopped at \"%s\", after %'u file.",
							"Unpacking stopped at \"%s\", after %'u files.",
							job->placed),
					job->long_link, job->placed);
	}
	if (!installed) {
		g_string_append_c (text, ' ');
		g_string_append (text, _("Install 7-Zip or RAR to unpack all of it."));
	}

	return g_string_free (text, FALSE);
}

static ExtractResult
extract_with_libarchive (ExtractJob *job,
			 GFile      *archive)
{
	struct archive *a;
	struct archive_entry *entry;
	StreamSource src;
	GFileInputStream *in;
	GError *error = NULL;
	ExtractResult result = EXTRACT_OK;

	in = g_file_read (archive, job->cancellable, &error);
	if (in == NULL) {
		job_fail (job, _("The archive could not be opened."),
			  error != NULL ? error->message : NULL);
		g_clear_error (&error);
		return EXTRACT_FAILED;
	}

	src.stream = G_INPUT_STREAM (in);
	src.cancellable = job->cancellable;
	src.error = NULL;

	a = archive_read_new ();
	archive_read_support_filter_all (a);
	archive_read_support_format_all (a);
	archive_read_set_passphrase_callback (a, job, passphrase_cb);

	/* zip and 7z are laid out to be read backwards from a trailing index,
	   and are far happier when they can seek than when they cannot. */
	if (g_seekable_can_seek (G_SEEKABLE (in))) {
		archive_read_set_seek_callback (a, source_seek);
	}

	if (archive_read_open2 (a, &src, NULL, source_read, NULL, source_close) != ARCHIVE_OK) {
		job->maybe_encrypted = looks_like_password_trouble (archive_error_string (a));
		archive_read_free (a);
		g_clear_error (&src.error);
		g_object_unref (in);
		return EXTRACT_UNSUPPORTED;
	}

	while (result == EXTRACT_OK) {
		const char *path;
		char *rel;
		unsigned int filetype;
		gint64 mtime;
		guint64 size;
		GFile *target;
		int next;

		if (job_aborted (job)) {
			break;
		}

		next = archive_read_next_header (a, &entry);
		if ((next == ARCHIVE_EOF || next < ARCHIVE_WARN) && !job->password_declined &&
		    arc_rar_stopped_at_long_target (a, entry)) {
			result = stopped_at_long_link (job, entry);
			break;
		}
		if (next == ARCHIVE_EOF) {
			break;
		}
		if (next < ARCHIVE_WARN) {
			/* A refused password is the user's answer, not a fault. */
			if (job->password_declined) {
				g_cancellable_cancel (job->cancellable);
				result = EXTRACT_FAILED;
			} else if (!job->wrote_anything) {
				job->maybe_encrypted =
					looks_like_password_trouble (archive_error_string (a));
				/* Nothing is written yet, so a command backend
				   can still have a go - which is what covers
				   rar, and anything else with a header
				   libarchive can see but not read. */
				result = EXTRACT_UNSUPPORTED;
			} else {
				job_fail (job, _("The archive could not be read."),
					  archive_error_string (a));
				result = EXTRACT_FAILED;
			}
			break;
		}

		path = archive_entry_pathname_utf8 (entry);
		if (path == NULL) {
			path = archive_entry_pathname (entry);
		}

		rel = path != NULL ? arc_extract_sanitize_path (path) : NULL;
		if (rel == NULL) {
			continue;
		}

		filetype = archive_entry_filetype (entry);
		if (filetype != AE_IFDIR) {
			g_hash_table_add (job->handled, g_strdup (rel));
		}
		mtime = archive_entry_mtime_is_set (entry) ? (gint64) archive_entry_mtime (entry) : 0;
		size = archive_entry_size_is_set (entry) ? (guint64) archive_entry_size (entry) : 0;

		arc_details (&job->host, rel);
		if (job->total_bytes > 0) {
			arc_fraction (&job->host,
				      (double) (job->done_bytes + (guint64) archive_filter_bytes (a, -1)),
				      (double) job->total_bytes);
		} else {
			arc_pulse (&job->host);
		}

		if (filetype == AE_IFDIR) {
			target = target_for_entry (job, rel, TRUE, 0, mtime);
			g_clear_object (&target);
			g_free (rel);
			continue;
		}

		if (filetype == AE_IFLNK) {
#ifdef G_OS_WIN32
			/* No links on this platform; the rest of the archive is
			   still worth having. */
			g_free (rel);
			continue;
#else
			const char *link_target = archive_entry_symlink_utf8 (entry);

			if (link_target == NULL) {
				link_target = archive_entry_symlink (entry);
			}

			target = link_target != NULL ? target_for_entry (job, rel, FALSE, 0, mtime) : NULL;
			if (target != NULL) {
				if (g_file_make_symbolic_link (target, link_target, job->cancellable, NULL)) {
					note_created (job, target);
					job->placed++;
				}
				g_object_unref (target);
			}
			g_free (rel);
			continue;
#endif
		}

		if (filetype != AE_IFREG) {
			/* Devices, sockets and fifos have no place in a folder a
			   person is unpacking into. */
			g_free (rel);
			continue;
		}

		target = target_for_entry (job, rel, FALSE, size, mtime);
		if (target == NULL) {
			g_free (rel);
			if (job_aborted (job)) {
				break;
			}
			continue;
		}

		if (archive_entry_hardlink (entry) != NULL) {
			/* No data follows a hard link; it names something else
			   in the same archive, which is copied instead. */
			const char *other = archive_entry_hardlink_utf8 (entry);
			char *link_rel = arc_extract_sanitize_path (other != NULL ? other : archive_entry_hardlink (entry));
			GFile *source = link_rel != NULL ?
				target_for_entry (job, link_rel, FALSE, size, mtime) : NULL;

			if (source != NULL) {
				if (g_file_copy (source, target, G_FILE_COPY_OVERWRITE | G_FILE_COPY_ALL_METADATA,
						 job->cancellable, NULL, NULL, NULL)) {
					note_created (job, target);
					job->placed++;
				}
				g_object_unref (source);
			}
			g_free (link_rel);
		} else if (!write_entry_data (job, a, target, &error)) {
			if (!g_error_matches (error, G_IO_ERROR, G_IO_ERROR_CANCELLED)) {
				job_fail (job, _("The archive could not be unpacked."),
					  error != NULL ? error->message : NULL);
				result = EXTRACT_FAILED;
			}
			g_clear_error (&error);
		} else {
			apply_entry_metadata (job, target, mtime, archive_entry_perm (entry));
			note_created (job, target);
			job->placed++;
		}

		g_object_unref (target);
		g_free (rel);
	}

	if (result == EXTRACT_OK && src.error != NULL) {
		job_fail (job, _("The archive could not be read."), src.error->message);
		result = EXTRACT_FAILED;
	}

	g_clear_error (&src.error);
	archive_read_free (a);
	g_object_unref (in);

	return result;
}

/*
 * Reading with a command
 */

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

/* Placed, or asked about, before libarchive stopped at a link it can't read. */
static gboolean
handled_before_stop (ExtractJob *job,
		     const char *rel)
{
	return job->long_link != NULL && g_hash_table_contains (job->handled, rel);
}

/* Moves everything a command left in the staging folder into place, asking the
   same questions the entry-by-entry path asks. */
static void
place_staged_tree (ExtractJob *job,
		   GFile      *staging_dir,
		   const char *rel_prefix)
{
	GFileEnumerator *children;

	children = arc_walk_children (&job->host, staging_dir,
				      G_FILE_ATTRIBUTE_STANDARD_NAME ","
				      G_FILE_ATTRIBUTE_STANDARD_TYPE ","
				      G_FILE_ATTRIBUTE_STANDARD_SIZE ","
				      G_FILE_ATTRIBUTE_TIME_MODIFIED,
				      G_FILE_QUERY_INFO_NOFOLLOW_SYMLINKS,
				      job->cancellable, NULL);
	if (children == NULL) {
		return;
	}

	for (;;) {
		GFileInfo *info;
		GFile *child;
		char *rel;
		const char *name;

		if (job_aborted (job)) {
			break;
		}

		info = g_file_enumerator_next_file (children, job->cancellable, NULL);
		if (info == NULL) {
			break;
		}

		name = g_file_info_get_name (info);
		child = g_file_get_child (staging_dir, name);
		/* The names are the program's, so they get the same cleaning an
		   entry read here gets. */
		{
			char *joined = rel_prefix[0] != '\0' ? g_strconcat (rel_prefix, "/", name, NULL)
							     : g_strdup (name);

			rel = arc_extract_sanitize_path (joined);
			g_free (joined);
		}

		if (rel == NULL) {
			g_object_unref (child);
			g_object_unref (info);
			continue;
		}

		arc_details (&job->host, rel);

		if (g_file_info_get_file_type (info) == G_FILE_TYPE_DIRECTORY) {
			if (dir_for_rel (job, rel) != NULL) {
				place_staged_tree (job, child, rel);
			}
		} else if (!handled_before_stop (job, rel)) {
			GFile *target = target_for_entry (job, rel, FALSE,
							  (guint64) g_file_info_get_size (info),
							  (gint64) g_file_info_get_attribute_uint64 (info, G_FILE_ATTRIBUTE_TIME_MODIFIED));

			if (target != NULL) {
				if (g_file_move (child, target,
						 G_FILE_COPY_OVERWRITE | G_FILE_COPY_NOFOLLOW_SYMLINKS |
						 G_FILE_COPY_ALL_METADATA,
						 job->cancellable, NULL, NULL, NULL)) {
					note_created (job, target);
				}
				g_object_unref (target);
			}
		}

		g_free (rel);
		g_object_unref (child);
		g_object_unref (info);
	}

	g_file_enumerator_close (children, job->cancellable, NULL);
	g_object_unref (children);
}

/* Runs one attempt. Whatever the tool said on its way out is left in tail, both
   for the error message and to work out why it stopped. */
static gboolean
run_unpack_command (ExtractJob         *job,
		    char              **argv,
		    const char         *base_path,
		    ArcBackend          backend,
		    GString            *tail)
{
	ArcRun *process;
	GInputStream *out;
	GThread *err_reader;
	char *err_text;
	GError *error = NULL;
	char buffer[4096];
	gboolean ran;

	/* The stdin pipe comes already ended, since nothing is going to answer
	   a prompt. */
	process = arc_run_start (&job->host, (const gchar * const *) argv, base_path,
				 G_SUBPROCESS_FLAGS_STDIN_PIPE |
				 G_SUBPROCESS_FLAGS_STDOUT_PIPE |
				 arc_tool_stderr_flag (),
				 &error);
	if (process == NULL) {
		g_string_assign (tail, error != NULL ? error->message : "");
		g_clear_error (&error);
		return FALSE;
	}

	{
		char *status = g_strdup_printf (_("Unpacking with %s"),
						backend == ARC_BACKEND_RAR ? "rar" : "7z");

		arc_status (&job->host, status);
		g_free (status);
	}

	out = arc_run_stdout (&job->host, process);
	err_reader = arc_tool_stderr_start (&job->host, process);

	for (;;) {
		gssize count = g_input_stream_read (out, buffer, sizeof (buffer), job->cancellable, NULL);
		int percent = -1;

		if (count <= 0) {
			break;
		}

		g_string_append_len (tail, buffer, count);
		if (tail->len > 4096) {
			g_string_erase (tail, 0, tail->len - 4096);
		}

		if (scan_percent (buffer, (gsize) count, &percent)) {
			arc_fraction (&job->host, percent, 100);
		}
	}

	if (job_aborted (job)) {
		arc_run_force_exit (&job->host, process);
	}

	/* Both tools say a wrong password on stderr. */
	err_text = arc_tool_stderr_finish (err_reader);
	if (err_text != NULL && err_text[0] != '\0') {
		if (tail->len > 0 && tail->str[tail->len - 1] != '\n') {
			g_string_append_c (tail, '\n');
		}
		g_string_append (tail, err_text);
		if (tail->len > 4096) {
			g_string_erase (tail, 0, tail->len - 4096);
		}
	}
	g_free (err_text);

	ran = arc_run_wait_check (&job->host, process, &error);

	if (!ran && tail->len == 0 && error != NULL) {
		g_string_assign (tail, error->message);
	}

	g_clear_error (&error);
	arc_run_free (&job->host, process);

	return ran;
}

/* One command's attempt. Failing is not the archive's verdict - another command
   may still manage it - so what went wrong comes back in out_details for the
   caller to report once everything has been tried. */
static ExtractResult
extract_with_command (ExtractJob         *job,
		      GFile              *archive,
		      ArcBackend          backend,
		      char              **out_details)
{
	const char *program = arc_extract_program (backend);
	char *archive_path;
	char *base_path;
	char *staging_path = NULL;
	GFile *staging = NULL;
	char **argv = NULL;
	GString *tail;
	guint counter;
	guint attempt;
	ExtractResult result = EXTRACT_FAILED;

	archive_path = g_file_get_path (archive);
	base_path = g_file_get_path (job->current_base);

	if (program == NULL || archive_path == NULL || base_path == NULL) {
		*out_details = g_strdup (_("This archive needs a program that is not installed, "
					   "or a folder on this computer."));
		g_free (archive_path);
		g_free (base_path);
		return EXTRACT_FAILED;
	}

	/* rar reads * and ? anywhere in the path as a wildcard and would unpack
	   the archives beside this one too. 7z, the other command, reads rar and
	   is told to take names as they are. */
	if (backend == ARC_BACKEND_RAR && strpbrk (archive_path, "*?") != NULL) {
		*out_details = g_strdup_printf (_("The path to \"%s\" has * or ? in it, which rar reads as a wildcard."),
						job->current_name);
		g_free (archive_path);
		g_free (base_path);
		return EXTRACT_UNSUPPORTED;
	}

	/* A command has no per-entry hook to hang the collision questions off,
	   so it unpacks somewhere of its own and what it produced is placed
	   afterwards - which is also what keeps a failed run from scattering. */
	for (counter = 0; counter < 100; counter++) {
		char *name = g_strdup_printf (".nemo-extract-%u", g_random_int ());

		staging = g_file_get_child (job->current_base, name);
		g_free (name);

		if (g_file_make_directory (staging, job->cancellable, NULL)) {
			break;
		}

		g_clear_object (&staging);
	}

	if (staging == NULL) {
		*out_details = g_strdup (_("A temporary folder could not be created."));
		g_free (archive_path);
		g_free (base_path);
		return EXTRACT_FAILED;
	}

	staging_path = g_file_get_path (staging);
	tail = g_string_new (NULL);

	for (attempt = 0; attempt < 2; attempt++) {
		g_strfreev (argv);
		argv = arc_build_extract_command (&job->settings, &job->host, backend, program,
						  archive_path, staging_path, job->password);
		g_string_truncate (tail, 0);

		if (run_unpack_command (job, argv, base_path, backend, tail)) {
			result = EXTRACT_OK;
			break;
		}

		if (job_aborted (job)) {
			break;
		}

		/* A locked archive is a question rather than a failure - but it
		   is only worth asking when the tool says that is what stopped
		   it, and ask_password only ever asks once per job anyway. */
		/* The tools do not always get a word in - launched from a window
		   with no console, unrar's complaint reaches nobody - so what
		   libarchive made of the archive counts too. */
		if (attempt == 0 && job->password == NULL && !job->password_declined &&
		    (job->maybe_encrypted || looks_like_password_trouble (tail->str))) {
			ask_password (job);

			if (job->password != NULL) {
				continue;
			}
		}

		/* Both tools write to a console, and launched from a window there
		   is none - so a bare exit code is often all there is to go on. */
		*out_details = tail->len > 0 ? g_strdup (tail->str) : NULL;
		break;
	}

	g_string_free (tail, TRUE);

	if (result == EXTRACT_OK && !job_aborted (job)) {
		arc_status (&job->host, _("Moving the unpacked files into place"));
		place_staged_tree (job, staging, "");
	}
	arc_remove_tree (&job->host, staging, NULL);

	g_strfreev (argv);
	g_object_unref (staging);
	g_free (staging_path);
	g_free (archive_path);
	g_free (base_path);

	return result;
}

/*
 * The job
 */

static void
dir_map_value_free (gpointer value)
{
	if (value != NULL && value != SKIPPED) {
		g_object_unref (value);
	}
}

static gboolean
extract_one (ExtractJob *job,
	     GFile      *archive)
{
	GFile *base;
	ExtractResult result;

	g_hash_table_remove_all (job->dir_map);
	job->archive_failed = FALSE;
	job->wrote_anything = FALSE;
	job->maybe_encrypted = FALSE;
	g_clear_pointer (&job->long_link, g_free);
	job->placed = 0;
	g_hash_table_remove_all (job->handled);

	g_free (job->current_name);
	job->current_name = g_file_get_basename (archive);

	{
		char *status = g_strdup_printf (_("Unpacking %s"), job->current_name);

		arc_status (&job->host, status);
		g_free (status);
	}

	if (job->layout == ARC_EXTRACT_TO_SUBFOLDER) {
		char *folder = arc_extract_folder_name (job->current_name);
		GFile *made;

		if (folder == NULL) {
			return FALSE;
		}

		job->current_base = job->destination_dir;
		made = dir_for_rel (job, folder);
		g_free (folder);

		if (made == NULL) {
			job->current_base = NULL;
			return !job_aborted (job);	/* a skipped folder is not a failure */
		}

		base = g_object_ref (made);
		g_hash_table_remove_all (job->dir_map);

		/* The folder is ours, not the archive's, so a command backend
		   may still take over when libarchive cannot read it. */
		job->wrote_anything = FALSE;
	} else {
		base = g_object_ref (job->destination_dir);
	}

	job->current_base = base;

	result = extract_with_libarchive (job, archive);

	if (result == EXTRACT_UNSUPPORTED) {
		ArcBackend order[2];
		guint count = command_backends_for (job->current_name, order);
		char *details = NULL;
		char *passed = NULL;	/* why a command was not even tried */
		guint i;

		result = EXTRACT_FAILED;

		for (i = 0; i < count && !job_cancelled (job); i++) {
			char *said = NULL;

			result = extract_with_command (job, archive, order[i], &said);

			if (result == EXTRACT_OK) {
				g_free (said);
				break;
			}
			if (result == EXTRACT_UNSUPPORTED && passed == NULL) {
				passed = said;
			} else {
				g_free (details);
				details = said;
			}
		}

		if (result != EXTRACT_OK && !job_cancelled (job)) {
			char *text = passed != NULL && details != NULL
				? g_strconcat (passed, "\n\n", details, NULL)
				: g_strdup (passed != NULL ? passed : details);

			if (job->long_link != NULL) {
				char *note = long_link_text (job, count > 0);
				char *both = text != NULL ? g_strconcat (note, "\n\n", text, NULL)
							  : g_strdup (note);

				g_free (note);
				g_free (text);
				text = both;
			}

			job_fail (job, _("The archive could not be unpacked."),
				  text != NULL ? text :
				  _("Nothing installed here can read this archive."));
			g_free (text);
		}

		g_free (passed);
		g_free (details);
	}

	job->current_base = NULL;
	g_object_unref (base);

	return result == EXTRACT_OK;
}

/* Returns: (transfer full): free with arc_extract_free */
ArcExtract *
arc_extract_new (const ArcHost     *host,
		 const ArcSettings *settings,
		 GList             *archives,
		 GFile             *destination_dir,
		 ArcExtractLayout   layout,
		 GCancellable      *cancellable)
{
	ExtractJob *job;
	GList *l;

	g_return_val_if_fail (host != NULL, NULL);
	g_return_val_if_fail (settings != NULL, NULL);
	g_return_val_if_fail (archives != NULL, NULL);
	g_return_val_if_fail (G_IS_FILE (destination_dir), NULL);

	job = g_new0 (ExtractJob, 1);
	job->host = *host;
	arc_settings_copy (&job->settings, settings);
	job->cancellable = cancellable != NULL ? g_object_ref (cancellable) : g_cancellable_new ();

	{
		/* Selecting every part of a split archive is the natural thing to
		   do, and unpacking has to start from the first volume whichever
		   one was clicked, so they all come back to the same job. */
		GHashTable *seen = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);

		for (l = archives; l != NULL; l = l->next) {
			GFile *archive = arc_extract_first_volume (G_FILE (l->data));
			char *uri = g_file_get_uri (archive);

			if (g_hash_table_contains (seen, uri)) {
				g_free (uri);
				g_object_unref (archive);
				continue;
			}

			g_hash_table_add (seen, uri);
			job->archives = g_list_prepend (job->archives, archive);
		}

		g_hash_table_destroy (seen);
	}
	job->archives = g_list_reverse (job->archives);

	job->destination_dir = g_object_ref (destination_dir);
	job->layout = layout;
	job->dir_map = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, dir_map_value_free);
	job->handled = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);

	return job;
}

void
arc_extract_free (ArcExtract *job)
{
	if (job == NULL) {
		return;
	}

	g_list_free_full (job->archives, g_object_unref);
	g_clear_object (&job->destination_dir);
	g_clear_object (&job->cancellable);
	g_clear_pointer (&job->dir_map, g_hash_table_destroy);
	g_clear_pointer (&job->handled, g_hash_table_destroy);
	g_free (job->long_link);
	g_free (job->current_name);
	g_free (job->password);
	g_free (job->error_message);
	g_free (job->error_details);
	arc_settings_clear (&job->settings);
	g_free (job);
}

void
arc_extract_run (ArcExtract *job)
{
	GArray *sizes;
	GList *l;
	guint index;

	g_return_if_fail (job != NULL);

	/* One pass for the sizes first: the compressed bytes read out of each
	   archive are what the progress bar is measured against, and they are
	   the only total available without unpacking everything twice. */
	sizes = g_array_new (FALSE, FALSE, sizeof (guint64));

	for (l = job->archives; l != NULL; l = l->next) {
		GFileInfo *info;
		guint64 size = 0;

		info = g_file_query_info (G_FILE (l->data), G_FILE_ATTRIBUTE_STANDARD_SIZE,
					  G_FILE_QUERY_INFO_NONE, job->cancellable, NULL);
		if (info != NULL) {
			size = (guint64) g_file_info_get_size (info);
			g_object_unref (info);
		}

		g_array_append_val (sizes, size);
		job->total_bytes += size;
	}

	for (l = job->archives, index = 0; l != NULL && !job_cancelled (job); l = l->next, index++) {
		if (extract_one (job, G_FILE (l->data))) {
			job->done_count++;
		} else {
			job->failed_count++;
		}

		job->done_bytes += g_array_index (sizes, guint64, index);
	}

	g_array_free (sizes, TRUE);

	job->success = job->failed_count == 0 && !job_cancelled (job);
}

void
arc_extract_finish (ArcExtract *job)
{
	g_return_if_fail (job != NULL);

	if (job->error_message != NULL) {
		arc_show_error (&job->host, job->error_message, job->error_details);
	}
}

gboolean
arc_extract_succeeded (ArcExtract *job)
{
	g_return_val_if_fail (job != NULL, FALSE);

	return job->success;
}

/* Returns: (transfer none) */
GFile *
arc_extract_destination (ArcExtract *job)
{
	g_return_val_if_fail (job != NULL, NULL);

	return job->destination_dir;
}
