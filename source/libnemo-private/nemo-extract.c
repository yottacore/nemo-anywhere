/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */

/* nemo-extract.c - unpack an archive.

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
#include "nemo-extract.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>

#include <glib/gi18n.h>
#include <archive.h>
#include <archive_entry.h>

#include <eel/eel-stock-dialogs.h>

#include "nemo-archive.h"
#include "nemo-archive-commands.h"
#include "nemo-command-template.h"
#include "nemo-delete-guard.h"
#include "nemo-dir-enum.h"
#include "nemo-extract-conflict-dialog.h"
#include "nemo-file-changes-queue.h"
#include "nemo-job-queue.h"
#include "nemo-progress-info.h"

#define READ_BUFFER_SIZE (64 * 1024)

/* Stands for "this folder was skipped" in the folder cache, where a plain NULL
   cannot be told apart from a miss. Its own address, so it can never collide
   with a real GFile and needs no cast from a number. */
static const char skipped_marker;
#define SKIPPED ((gpointer) &skipped_marker)

/* Suffixes we offer to unpack, longest first so ".tar.gz" is not read as ".gz".
   Much wider than the list we can write - libarchive reads a great deal it
   cannot produce - and a name is the only thing there is to go on, since on
   win32 a file's mime type is its extension. */
static const char * const archive_extensions[] = {
	".tar.gz", ".tar.xz", ".tar.bz2", ".tar.zst", ".tar.lz", ".tar.lzma", ".tar.z",
	".tgz", ".txz", ".tbz2", ".tbz", ".tzst",
	".zip", ".jar", ".war", ".ear", ".cbz",
	".7z", ".rar", ".cbr",
	".tar", ".cab", ".iso", ".lha", ".lzh", ".cpio", ".xar", ".pax",
	".gz", ".bz2", ".xz", ".zst", ".lz", ".lzma", ".z",
	NULL
};

typedef enum {
	EXTRACT_OK,
	EXTRACT_UNSUPPORTED,	/* the file would not open; another backend may */
	EXTRACT_FAILED
} ExtractResult;

typedef struct {
	GtkWindow          *parent_window;
	NemoProgressInfo   *progress;
	GCancellable       *cancellable;
	GIOSchedulerJob    *io_job;

	GList              *archives;		/* GFile * */
	GFile              *destination_dir;
	NemoExtractLayout   layout;

	/* Per-archive: where this one is being unpacked, and what its folders
	   turned into once names were resolved. Keyed on the path the archive
	   uses, so a renamed folder carries its children with it. */
	GFile              *current_base;
	GHashTable         *dir_map;		/* char * -> GFile * or SKIPPED */
	char               *current_name;	/* the archive being read */

	/* An answer the user asked to have applied to every conflict. */
	int                 default_response;

	/* One archive going wrong stops that archive, not the rest of them.
	   Cancelling stops everything. */
	gboolean            archive_failed;

	/* Whether this archive has put anything on disk yet. Nothing yet means
	   handing it to a command instead is still free. */
	gboolean            wrote_anything;

	/* Whether libarchive gave up on this one over a password. A command
	   backend can often still open it, given one. */
	gboolean            maybe_encrypted;

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

	NemoExtractCallback done_callback;
	gpointer            done_callback_data;
} ExtractJob;

/* The GInputStream libarchive reads through. Going in via GIO rather than a
   filename keeps one code path for local and remote sources, and sidesteps the
   byte-versus-wide filename split on Windows. */
typedef struct {
	GInputStream *stream;
	GCancellable *cancellable;
	GError       *error;
	char          buffer[READ_BUFFER_SIZE];
} StreamSource;

/*
 * Names
 */

/* 7z numbers a split archive "name.7z.001", so the name to read the format off
   is the one underneath. Three digits is what it writes, including for the only
   volume. Returns how much of the end to ignore, or 0 for an ordinary name. */
static gsize
volume_suffix_len (const char *name, gsize len)
{
	gsize i;

	if (len < 5 || name[len - 4] != '.') {
		return 0;
	}

	for (i = len - 3; i < len; i++) {
		if (!g_ascii_isdigit (name[i])) {
			return 0;
		}
	}

	return 4;
}

static const char *
matching_extension (const char *name)
{
	gsize len;
	int i;

	if (name == NULL) {
		return NULL;
	}

	len = strlen (name);
	len -= volume_suffix_len (name, len);

	for (i = 0; archive_extensions[i] != NULL; i++) {
		gsize ext_len = strlen (archive_extensions[i]);

		/* A suffix only, and never the whole name - ".zip" on its own is
		   a file called .zip, not an archive of nothing. Length-limited,
		   since a volume number may still be sitting past `len`. */
		if (len > ext_len &&
		    g_ascii_strncasecmp (name + len - ext_len, archive_extensions[i], ext_len) == 0) {
			return archive_extensions[i];
		}
	}

	return NULL;
}

gboolean
nemo_extract_is_archive_name (const char *name)
{
	return matching_extension (name) != NULL;
}

/* Where unpacking a split archive has to start. Any other volume answers with
   its ".001" sibling, and a name that is not a volume answers with itself. A
   missing sibling is left alone, so the failure reads as the archive it was. */
static GFile *
first_volume (GFile *archive)
{
	char *name = g_file_get_basename (archive);
	gsize len = name != NULL ? strlen (name) : 0;
	GFile *parent;
	GFile *first = NULL;
	char *wanted;

	if (name == NULL || volume_suffix_len (name, len) == 0 ||
	    strcmp (name + len - 3, "001") == 0) {
		g_free (name);
		return g_object_ref (archive);
	}

	parent = g_file_get_parent (archive);
	if (parent == NULL) {
		g_free (name);
		return g_object_ref (archive);
	}

	wanted = g_strdup_printf ("%.*s001", (int) (len - 3), name);
	first = g_file_get_child (parent, wanted);

	if (!g_file_query_exists (first, NULL)) {
		g_clear_object (&first);
		first = g_object_ref (archive);
	}

	g_free (wanted);
	g_free (name);
	g_object_unref (parent);

	return first;
}

char *
nemo_extract_folder_name (const char *archive_name)
{
	const char *extension;
	char *base;
	gsize len;

	g_return_val_if_fail (archive_name != NULL, NULL);

	extension = matching_extension (archive_name);
	if (extension == NULL) {
		return NULL;
	}

	len = strlen (archive_name);
	len -= volume_suffix_len (archive_name, len);

	base = g_strndup (archive_name, len - strlen (extension));

	if (base[0] == '\0') {
		g_free (base);
		return NULL;
	}

	return base;
}

char *
nemo_extract_unique_name (const char *name,
			  guint       attempt)
{
	const char *dot;

	g_return_val_if_fail (name != NULL, NULL);

	/* A leading dot is the start of a hidden name, not an extension. */
	dot = name[0] != '\0' ? strrchr (name + 1, '.') : NULL;

	if (dot == NULL) {
		return g_strdup_printf ("%s (%u)", name, attempt);
	}

	return g_strdup_printf ("%.*s (%u)%s", (int) (dot - name), name, attempt, dot);
}

char *
nemo_extract_sanitize_path (const char *entry_path)
{
	char *normalized;
	char *valid = NULL;
	char **parts;
	GPtrArray *kept;
	char *result;
	int i;

	g_return_val_if_fail (entry_path != NULL, NULL);

	if (!g_utf8_validate (entry_path, -1, NULL)) {
		valid = g_utf8_make_valid (entry_path, -1);
		entry_path = valid;
	}

	/* Archives written on Windows sometimes separate with backslashes, and
	   a backslash is not a name character on either platform once it is a
	   path inside an archive. */
	normalized = g_strdelimit (g_strdup (entry_path), "\\", '/');
	parts = g_strsplit (normalized, "/", -1);
	kept = g_ptr_array_new ();

	for (i = 0; parts[i] != NULL; i++) {
		const char *part = parts[i];
		gsize len = strlen (part);

		/* Empty means a leading, trailing or doubled separator; "." is
		   nothing; ".." would climb out of the folder being unpacked
		   into, and a drive letter would leave it altogether. */
		if (len == 0 || g_strcmp0 (part, ".") == 0 || g_strcmp0 (part, "..") == 0) {
			continue;
		}
		if (len == 2 && part[1] == ':' && g_ascii_isalpha (part[0])) {
			continue;
		}

		g_ptr_array_add (kept, (gpointer) part);
	}

	g_ptr_array_add (kept, NULL);
	result = g_strjoinv ("/", (char **) kept->pdata);

	g_ptr_array_free (kept, TRUE);
	g_strfreev (parts);
	g_free (normalized);
	g_free (valid);

	if (result[0] == '\0') {
		g_free (result);
		return NULL;
	}

	return result;
}

/*
 * Backends
 */

static const char *
backend_program (NemoExtractBackend backend)
{
	static const char * const seven_zip_names[] = { "7z", "7zz", "7za", NULL };
	static const char * const rar_names[] = { "unrar", "rar", NULL };
	static GMutex probe_lock;
	static gboolean probed[4];
	static char *probed_program[4];

	if (backend != NEMO_EXTRACT_BACKEND_7Z && backend != NEMO_EXTRACT_BACKEND_RAR) {
		return NULL;
	}

	g_mutex_lock (&probe_lock);

	if (!probed[backend]) {
		if (backend == NEMO_EXTRACT_BACKEND_7Z) {
			probed_program[backend] = nemo_archive_find_command (seven_zip_names, "7-Zip");
		} else {
			probed_program[backend] = nemo_archive_find_command (rar_names, "WinRAR");
		}
		probed[backend] = TRUE;
	}

	g_mutex_unlock (&probe_lock);

	return probed_program[backend];
}

gboolean
nemo_extract_backend_present (NemoExtractBackend backend)
{
	if (backend == NEMO_EXTRACT_BACKEND_LIBARCHIVE) {
		return TRUE;
	}

	return backend_program (backend) != NULL;
}

char **
nemo_extract_build_command (NemoExtractBackend  backend,
			    const char         *program,
			    const char         *archive_path,
			    const char         *destination_path,
			    const char         *password)
{
	char *program_v[2]     = { NULL, NULL };
	char *password_v[2]    = { NULL, NULL };
	char *archive_v[2]     = { NULL, NULL };
	char *folder_v[2]      = { NULL, NULL };
	char *folder_sep_v[2]  = { NULL, NULL };
	const char *key, *fallback;
	GError *error = NULL;
	char **argv;
	char *text;
	gboolean has_password;

	g_return_val_if_fail (program != NULL, NULL);
	g_return_val_if_fail (archive_path != NULL, NULL);
	g_return_val_if_fail (destination_path != NULL, NULL);

	has_password = password != NULL && password[0] != '\0';

	if (backend == NEMO_EXTRACT_BACKEND_7Z) {
		key = NEMO_EXTRACT_COMMAND_KEY_7Z;
		fallback = NEMO_EXTRACT_COMMAND_7Z_DEFAULT;

		/* Always answered, never asked: with no password switch at all
		   an encrypted archive stops for one on a console we do not
		   have, and the job would hang instead of failing. */
		password_v[0] = has_password ? g_strconcat ("-p", password, NULL)
					    : g_strdup ("-p");
	} else if (backend == NEMO_EXTRACT_BACKEND_RAR) {
		key = NEMO_EXTRACT_COMMAND_KEY_RAR;
		fallback = NEMO_EXTRACT_COMMAND_RAR_DEFAULT;

		password_v[0] = has_password ? g_strconcat ("-p", password, NULL)
					    : g_strdup ("-p-");
	} else {
		return NULL;
	}

	program_v[0] = g_strdup (program);
	archive_v[0] = g_strdup (archive_path);
	folder_v[0] = g_strdup (destination_path);
	folder_sep_v[0] = g_strconcat (destination_path, G_DIR_SEPARATOR_S, NULL);

	{
		const NemoCommandToken tokens[] = {
			{ "PROGRAM",                      (const char *const *) program_v,    FALSE },
			{ "PASSWORD",                     (const char *const *) password_v,   TRUE },
			{ "SOURCE_ARCHIVE",               (const char *const *) archive_v,    FALSE },
			{ "TARGET_FOLDER",                (const char *const *) folder_v,     FALSE },
			{ "TARGET_FOLDER_WITH_SEPARATOR", (const char *const *) folder_sep_v, FALSE },
			{ NULL, NULL, FALSE }
		};

		text = nemo_command_template_from_config (NEMO_ARCHIVE_COMMANDS_GROUP, key, fallback);
		argv = nemo_command_template_expand (text, tokens, &error);

		if (argv == NULL) {
			g_warning ("The %s command line cannot be run as written (%s): %s",
				   key, error->message, text);
			g_clear_error (&error);
		} else {
			/* Dropping {{PASSWORD}} is the one that bites: with no
			   switch at all an encrypted archive waits for a console
			   that is not there, and the job hangs. */
			nemo_command_template_warn_unused (key, text, tokens);
		}
	}

	g_free (text);
	g_free (program_v[0]);
	g_free (password_v[0]);
	g_free (archive_v[0]);
	g_free (folder_v[0]);
	g_free (folder_sep_v[0]);

	return argv;
}

/* Which commands are worth trying, best first: a rar file goes to a rar tool
   ahead of 7-Zip, anything else the other way round. Both are listed rather
   than one picked, because being installed is no promise of being able to read
   the file - an old build on the PATH can be years behind the format. */
static guint
command_backends_for (const char         *name,
		      NemoExtractBackend *order)
{
	const char *extension = matching_extension (name);
	gboolean is_rar = g_strcmp0 (extension, ".rar") == 0 || g_strcmp0 (extension, ".cbr") == 0;
	NemoExtractBackend first = is_rar ? NEMO_EXTRACT_BACKEND_RAR : NEMO_EXTRACT_BACKEND_7Z;
	NemoExtractBackend second = is_rar ? NEMO_EXTRACT_BACKEND_7Z : NEMO_EXTRACT_BACKEND_RAR;
	guint count = 0;

	if (nemo_extract_backend_present (first)) {
		order[count++] = first;
	}
	if (nemo_extract_backend_present (second)) {
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

typedef struct {
	ExtractJob *job;
	const char *entry_name;
	gboolean    entry_is_dir;
	guint64     entry_size;
	gint64      entry_mtime;
	GFile      *destination;
	GFile      *destination_dir;

	int         response;
	char       *new_name;
	gboolean    apply_to_all;
} ConflictData;

static gboolean
do_run_conflict_dialog (gpointer user_data)
{
	ConflictData *data = user_data;
	GtkWidget *dialog;

	dialog = nemo_extract_conflict_dialog_new (data->job->parent_window,
						   data->job->current_name,
						   data->entry_name,
						   data->entry_is_dir,
						   data->entry_size,
						   data->entry_mtime,
						   data->destination,
						   data->destination_dir);

	data->response = gtk_dialog_run (GTK_DIALOG (dialog));

	if (data->response == CONFLICT_RESPONSE_RENAME) {
		data->new_name = nemo_extract_conflict_dialog_get_new_name (dialog);
	} else if (data->response != GTK_RESPONSE_CANCEL && data->response != GTK_RESPONSE_NONE) {
		data->apply_to_all = nemo_extract_conflict_dialog_get_apply_to_all (dialog);
	}

	gtk_widget_destroy (dialog);

	return FALSE;
}

static void
run_conflict_dialog (ExtractJob   *job,
		     ConflictData *data)
{
	nemo_progress_info_pause (job->progress);
	g_io_scheduler_job_send_to_mainloop (job->io_job, do_run_conflict_dialog, data, NULL);
	nemo_progress_info_resume (job->progress);
}

typedef struct {
	ExtractJob *job;
	char       *password;
} PasswordData;

static gboolean
do_ask_password (gpointer user_data)
{
	PasswordData *data = user_data;
	GtkWidget *dialog;
	GtkWidget *content;
	GtkWidget *box;
	GtkWidget *label;
	GtkWidget *entry;
	char *text;

	dialog = gtk_dialog_new_with_buttons (_("Password required"),
					      data->job->parent_window,
					      GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
					      _("_Cancel"), GTK_RESPONSE_CANCEL,
					      _("_Unlock"), GTK_RESPONSE_OK,
					      NULL);
	gtk_dialog_set_default_response (GTK_DIALOG (dialog), GTK_RESPONSE_OK);

	content = gtk_dialog_get_content_area (GTK_DIALOG (dialog));
	box = gtk_box_new (GTK_ORIENTATION_VERTICAL, 6);
	gtk_container_set_border_width (GTK_CONTAINER (box), 12);
	gtk_box_pack_start (GTK_BOX (content), box, FALSE, FALSE, 0);

	text = g_strdup_printf (_("\"%s\" is protected. Enter its password to unpack it."),
				data->job->current_name);
	label = gtk_label_new (text);
	gtk_label_set_line_wrap (GTK_LABEL (label), TRUE);
	gtk_label_set_xalign (GTK_LABEL (label), 0.0);
	gtk_box_pack_start (GTK_BOX (box), label, FALSE, FALSE, 0);
	g_free (text);

	entry = gtk_entry_new ();
	gtk_entry_set_visibility (GTK_ENTRY (entry), FALSE);
	gtk_entry_set_activates_default (GTK_ENTRY (entry), TRUE);
	gtk_box_pack_start (GTK_BOX (box), entry, FALSE, FALSE, 0);

	gtk_widget_show_all (content);

	if (gtk_dialog_run (GTK_DIALOG (dialog)) == GTK_RESPONSE_OK) {
		data->password = g_strdup (gtk_entry_get_text (GTK_ENTRY (entry)));
	}

	gtk_widget_destroy (dialog);

	return FALSE;
}

/* Asked once per job: an archive set usually shares one password, and asking
   again for every entry of a big one would be unusable. */
static void
ask_password (ExtractJob *job)
{
	PasswordData data = { job, NULL };

	if (job->password_asked) {
		return;
	}
	job->password_asked = TRUE;

	nemo_progress_info_pause (job->progress);
	g_io_scheduler_job_send_to_mainloop (job->io_job, do_ask_password, &data, NULL);
	nemo_progress_info_resume (job->progress);

	if (data.password != NULL && data.password[0] != '\0') {
		job->password = data.password;
	} else {
		g_free (data.password);
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
		ConflictData data = { NULL, };
		int response;

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

		if (job->default_response != 0) {
			response = job->default_response;
		} else {
			data.job = job;
			data.entry_name = candidate;
			data.entry_is_dir = entry_is_dir;
			data.entry_size = entry_size;
			data.entry_mtime = entry_mtime;
			data.destination = target;
			data.destination_dir = dir;

			run_conflict_dialog (job, &data);
			response = data.response;

			if (data.apply_to_all && response != CONFLICT_RESPONSE_RENAME) {
				job->default_response = response;
			}
		}

		if (response == CONFLICT_RESPONSE_REPLACE) {
			nemo_delete_guard_remove_tree (target, job->cancellable);
			g_free (candidate);
			g_free (data.new_name);
			return target;
		}

		g_object_unref (target);

		if (response == CONFLICT_RESPONSE_SKIP) {
			g_free (candidate);
			g_free (data.new_name);
			return NULL;
		}

		if (response == CONFLICT_RESPONSE_RENAME && data.new_name != NULL &&
		    data.new_name[0] != '\0') {
			g_free (candidate);
			candidate = data.new_name;
			data.new_name = NULL;
			continue;
		}

		g_free (data.new_name);

		if (response == CONFLICT_RESPONSE_AUTO_RENAME) {
			attempt++;
			g_free (candidate);
			candidate = nemo_extract_unique_name (name, attempt);
			continue;
		}

		/* Cancel, or the window closed - the whole job stops. */
		nemo_progress_info_cancel (job->progress);
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
		nemo_file_changes_queue_file_added (file);
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
		if (next == ARCHIVE_EOF) {
			break;
		}
		if (next < ARCHIVE_WARN) {
			/* A refused password is the user's answer, not a fault. */
			if (job->password_declined) {
				nemo_progress_info_cancel (job->progress);
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

		rel = path != NULL ? nemo_extract_sanitize_path (path) : NULL;
		if (rel == NULL) {
			continue;
		}

		filetype = archive_entry_filetype (entry);
		mtime = archive_entry_mtime_is_set (entry) ? (gint64) archive_entry_mtime (entry) : 0;
		size = archive_entry_size_is_set (entry) ? (guint64) archive_entry_size (entry) : 0;

		nemo_progress_info_set_details (job->progress, rel);
		if (job->total_bytes > 0) {
			nemo_progress_info_set_progress (job->progress,
							 (double) (job->done_bytes + (guint64) archive_filter_bytes (a, -1)),
							 (double) job->total_bytes);
		} else {
			nemo_progress_info_pulse_progress (job->progress);
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
			char *link_rel = nemo_extract_sanitize_path (archive_entry_hardlink (entry));
			GFile *source = link_rel != NULL ?
				target_for_entry (job, link_rel, FALSE, size, mtime) : NULL;

			if (source != NULL) {
				if (g_file_copy (source, target, G_FILE_COPY_OVERWRITE | G_FILE_COPY_ALL_METADATA,
						 job->cancellable, NULL, NULL, NULL)) {
					note_created (job, target);
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

/* Moves everything a command left in the staging folder into place, asking the
   same questions the entry-by-entry path asks. */
static void
place_staged_tree (ExtractJob *job,
		   GFile      *staging_dir,
		   const char *rel_prefix)
{
	GFileEnumerator *children;

	children = nemo_enumerate_children (staging_dir,
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
		rel = rel_prefix[0] != '\0' ? g_strconcat (rel_prefix, "/", name, NULL) : g_strdup (name);

		nemo_progress_info_set_details (job->progress, rel);

		if (g_file_info_get_file_type (info) == G_FILE_TYPE_DIRECTORY) {
			if (dir_for_rel (job, rel) != NULL) {
				place_staged_tree (job, child, rel);
			}
		} else {
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
		    NemoExtractBackend  backend,
		    GString            *tail)
{
	GSubprocessLauncher *launcher;
	GSubprocess *process;
	GInputStream *out;
	GError *error = NULL;
	char buffer[4096];
	gboolean ran;

	launcher = g_subprocess_launcher_new (G_SUBPROCESS_FLAGS_STDIN_PIPE |
					      G_SUBPROCESS_FLAGS_STDOUT_PIPE |
					      G_SUBPROCESS_FLAGS_STDERR_MERGE);
	g_subprocess_launcher_set_cwd (launcher, base_path);

	process = g_subprocess_launcher_spawnv (launcher, (const gchar * const *) argv, &error);
	g_object_unref (launcher);

	if (process == NULL) {
		g_string_assign (tail, error != NULL ? error->message : "");
		g_clear_error (&error);
		return FALSE;
	}

	/* Nothing is going to answer a prompt, so hand it an ended stdin rather
	   than a pipe nobody ever writes to. */
	g_output_stream_close (g_subprocess_get_stdin_pipe (process), NULL, NULL);

	nemo_progress_info_take_status (job->progress,
					g_strdup_printf (_("Unpacking with %s"),
							 backend == NEMO_EXTRACT_BACKEND_RAR ? "rar" : "7z"));

	out = g_subprocess_get_stdout_pipe (process);

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
			nemo_progress_info_set_progress (job->progress, percent, 100);
		}
	}

	if (job_aborted (job)) {
		g_subprocess_force_exit (process);
	}

	ran = g_subprocess_wait_check (process, NULL, &error);

	if (!ran && tail->len == 0 && error != NULL) {
		g_string_assign (tail, error->message);
	}

	g_clear_error (&error);
	g_object_unref (process);

	return ran;
}

/* One command's attempt. Failing is not the archive's verdict - another command
   may still manage it - so what went wrong comes back in out_details for the
   caller to report once everything has been tried. */
static ExtractResult
extract_with_command (ExtractJob         *job,
		      GFile              *archive,
		      NemoExtractBackend  backend,
		      char              **out_details)
{
	const char *program = backend_program (backend);
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
	if (backend == NEMO_EXTRACT_BACKEND_RAR && strpbrk (archive_path, "*?") != NULL) {
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
		argv = nemo_extract_build_command (backend, program, archive_path,
						   staging_path, job->password);
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
		nemo_progress_info_set_status (job->progress, _("Moving the unpacked files into place"));
		place_staged_tree (job, staging, "");
	}
	nemo_delete_guard_remove_tree (staging, NULL);

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

	g_free (job->current_name);
	job->current_name = g_file_get_basename (archive);

	nemo_progress_info_take_status (job->progress,
					g_strdup_printf (_("Unpacking %s"), job->current_name));

	if (job->layout == NEMO_EXTRACT_TO_SUBFOLDER) {
		char *folder = nemo_extract_folder_name (job->current_name);
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
		NemoExtractBackend order[2];
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

static gboolean
extract_job_done (gpointer user_data)
{
	ExtractJob *job = user_data;

	nemo_file_changes_consume_changes (TRUE);

	if (job->error_message != NULL) {
		eel_show_error_dialog (job->error_message, job->error_details, job->parent_window);
	}

	if (job->done_callback != NULL) {
		job->done_callback (job->destination_dir, job->success, job->done_callback_data);
	}

	nemo_progress_info_finish (job->progress);

	if (job->parent_window != NULL) {
		g_object_remove_weak_pointer (G_OBJECT (job->parent_window),
					      (gpointer *) &job->parent_window);
	}

	g_list_free_full (job->archives, g_object_unref);
	g_clear_object (&job->destination_dir);
	g_clear_object (&job->progress);
	g_clear_object (&job->cancellable);
	g_clear_pointer (&job->dir_map, g_hash_table_destroy);
	g_free (job->current_name);
	g_free (job->password);
	g_free (job->error_message);
	g_free (job->error_details);
	g_free (job);

	return FALSE;
}

static gboolean
extract_job (GIOSchedulerJob *io_job,
	     GCancellable    *cancellable,
	     gpointer         user_data)
{
	ExtractJob *job = user_data;
	GArray *sizes;
	GList *l;
	guint index;

	(void) cancellable;

	job->io_job = io_job;
	nemo_progress_info_start (job->progress);

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

	g_io_scheduler_job_send_to_mainloop_async (io_job, extract_job_done, job, NULL);

	return FALSE;
}

void
nemo_extract_files (GList               *archives,
		    GFile               *destination_dir,
		    NemoExtractLayout    layout,
		    GtkWindow           *parent_window,
		    NemoExtractCallback  done_callback,
		    gpointer             done_callback_data)
{
	ExtractJob *job;
	GList *l;

	g_return_if_fail (archives != NULL);
	g_return_if_fail (G_IS_FILE (destination_dir));

	job = g_new0 (ExtractJob, 1);

	{
		/* Selecting every part of a split archive is the natural thing to
		   do, and unpacking has to start from the first volume whichever
		   one was clicked, so they all come back to the same job. */
		GHashTable *seen = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);

		for (l = archives; l != NULL; l = l->next) {
			GFile *archive = first_volume (G_FILE (l->data));
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
	job->done_callback = done_callback;
	job->done_callback_data = done_callback_data;

	job->parent_window = parent_window;
	if (parent_window != NULL) {
		g_object_add_weak_pointer (G_OBJECT (parent_window), (gpointer *) &job->parent_window);
	}

	job->progress = nemo_progress_info_new ();
	job->cancellable = nemo_progress_info_get_cancellable (job->progress);
	g_object_ref (job->cancellable);

	nemo_progress_info_set_status (job->progress, _("Preparing to unpack"));
	nemo_progress_info_take_initial_details (job->progress,
						 g_file_get_basename (G_FILE (archives->data)));

	nemo_job_queue_add_new_job (nemo_job_queue_get (), extract_job, job,
				    job->cancellable, job->progress, TRUE);
}
