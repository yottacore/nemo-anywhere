/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */

/* nemo-archive.c - create an archive from a selection.

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
#include "nemo-archive.h"

#include <errno.h>
#include <string.h>
#include <stdlib.h>

#include <glib/gi18n.h>
#include <glib/gstdio.h>
#include <archive.h>
#include <archive_entry.h>

#include <eel/eel-stock-dialogs.h>

#include "arc-entry.h"
#include "arc-scan.h"
#include "nemo-archive-commands.h"
#include "nemo-archive-host.h"
#include "nemo-command-template.h"
#include "nemo-dir-enum.h"
#include "nemo-file-changes-queue.h"
#include "nemo-file-operations.h"
#include "nemo-file-utilities.h"
#include "nemo-global-preferences.h"
#include "nemo-job-queue.h"
#include "nemo-link-win32.h"
#include "nemo-progress-info.h"

#define READ_BUFFER_SIZE (64 * 1024)

/* Attributes one scan pass needs. id::file is what tells us we have already
   been through a directory, which is the loop guard when links are followed. */
#define SCAN_ATTRIBUTES \
	G_FILE_ATTRIBUTE_STANDARD_TYPE "," \
	G_FILE_ATTRIBUTE_STANDARD_NAME "," \
	G_FILE_ATTRIBUTE_STANDARD_SIZE "," \
	G_FILE_ATTRIBUTE_STANDARD_IS_SYMLINK "," \
	G_FILE_ATTRIBUTE_STANDARD_SYMLINK_TARGET "," \
	G_FILE_ATTRIBUTE_UNIX_MODE "," \
	G_FILE_ATTRIBUTE_TIME_MODIFIED "," \
	G_FILE_ATTRIBUTE_UNIX_DEVICE "," \
	G_FILE_ATTRIBUTE_ID_FILE

typedef struct {
	const char      *id;
	const char      *name;		/* untranslated; run through _() at use */
	const char      *extension;
	gboolean         by_libarchive;
	NemoArchiveCaps  caps_libarchive;
	gboolean         by_7z;
	NemoArchiveCaps  caps_7z;
	gboolean         by_rar;
	NemoArchiveCaps  caps_rar;
} FormatInfo;

/* What each writer can do, established against libarchive 3.8 and the switch
   lists of 7-Zip and rar. libarchive writes no rar at all, and has no write
   support for volumes, solid blocks, duplicate-as-reference or 7z encryption,
   which is the whole reason the commands are still worth reaching for. */
static const FormatInfo formats[NEMO_ARCHIVE_N_FORMATS] = {
	{ "zip", N_("ZIP"), ".zip",
	  TRUE,  NEMO_ARCHIVE_CAP_LEVEL | NEMO_ARCHIVE_CAP_PASSWORD | NEMO_ARCHIVE_CAP_STORE_LINKS,
	  TRUE,  NEMO_ARCHIVE_CAP_LEVEL | NEMO_ARCHIVE_CAP_PASSWORD | NEMO_ARCHIVE_CAP_SPLIT |
		 NEMO_ARCHIVE_CAP_STORE_LINKS,
	  FALSE, 0 },

	{ "tar", N_("TAR, uncompressed"), ".tar",
	  TRUE,  NEMO_ARCHIVE_CAP_STORE_LINKS,
	  FALSE, 0,
	  FALSE, 0 },

	{ "tar.gz", N_("TAR, gzip compressed"), ".tar.gz",
	  TRUE,  NEMO_ARCHIVE_CAP_LEVEL | NEMO_ARCHIVE_CAP_STORE_LINKS,
	  FALSE, 0,
	  FALSE, 0 },

	{ "tar.xz", N_("TAR, xz compressed"), ".tar.xz",
	  TRUE,  NEMO_ARCHIVE_CAP_LEVEL | NEMO_ARCHIVE_CAP_STORE_LINKS,
	  FALSE, 0,
	  FALSE, 0 },

	{ "7z", N_("7z"), ".7z",
	  TRUE,  NEMO_ARCHIVE_CAP_LEVEL | NEMO_ARCHIVE_CAP_STORE_LINKS,
	  TRUE,  NEMO_ARCHIVE_CAP_LEVEL | NEMO_ARCHIVE_CAP_PASSWORD | NEMO_ARCHIVE_CAP_ENCRYPT_NAMES |
		 NEMO_ARCHIVE_CAP_SPLIT | NEMO_ARCHIVE_CAP_SOLID | NEMO_ARCHIVE_CAP_STORE_LINKS,
	  FALSE, 0 },

	{ "rar", N_("RAR"), ".rar",
	  FALSE, 0,
	  FALSE, 0,
	  TRUE,  NEMO_ARCHIVE_CAP_LEVEL | NEMO_ARCHIVE_CAP_PASSWORD | NEMO_ARCHIVE_CAP_ENCRYPT_NAMES |
		 NEMO_ARCHIVE_CAP_SPLIT | NEMO_ARCHIVE_CAP_SOLID | NEMO_ARCHIVE_CAP_DEDUPE |
		 NEMO_ARCHIVE_CAP_STORE_LINKS | NEMO_ARCHIVE_CAP_RECOVERY | NEMO_ARCHIVE_CAP_LOCK },
};

/* Every suffix we recognize as ours, longest first so ".tar.gz" is not read as
   ".gz". Used to swap the extension when the format changes. */
static const char * const known_extensions[] = {
	".tar.gz", ".tar.xz", ".tar.bz2", ".tgz", ".txz", ".tbz2",
	".zip", ".tar", ".7z", ".rar", NULL
};

/* One archive to write, and what goes in it. A plain compress has one of
   these; compressing a selection separately has one per item. */
typedef struct {
	GList *sources;		/* GFile *, owned */
	GFile *destination;	/* owned */
} ArchiveUnit;

typedef struct {
	GtkWindow          *parent_window;
	NemoProgressInfo   *progress;
	GCancellable       *cancellable;
	GIOSchedulerJob    *io_job;

	GList              *units;		/* ArchiveUnit *, in order */
	guint               unit_count;
	guint               unit_index;		/* which one is being written */
	GFile              *result_file;	/* what the callback is handed */

	/* The unit in hand. sources and destination are borrowed from it. */
	GList              *sources;		/* GFile * */
	GFile              *destination;
	GFile              *base_dir;

	NemoArchiveOptions  options;
	NemoArchiveBackend  backend;

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

	NemoArchiveCallback done_callback;
	gpointer            done_callback_data;
} ArchiveJob;

/* The GOutputStream libarchive writes through. Going out via GIO rather than a
   filename keeps one code path for local and remote destinations, and sidesteps
   the byte-versus-wide filename split on Windows. */
typedef struct {
	GOutputStream *stream;
	GCancellable  *cancellable;
	GError        *error;
	gboolean       fail_after_stop;
} StreamSink;

static gboolean
format_is_valid (NemoArchiveFormat format)
{
	return format >= 0 && format < NEMO_ARCHIVE_N_FORMATS;
}

/* Returns: (transfer none): a static string */
const char *
nemo_archive_format_id (NemoArchiveFormat format)
{
	g_return_val_if_fail (format_is_valid (format), "zip");

	return formats[format].id;
}

/* Returns: (transfer none): a static string */
const char *
nemo_archive_format_name (NemoArchiveFormat format)
{
	g_return_val_if_fail (format_is_valid (format), "");

	return _(formats[format].name);
}

/* Returns: (transfer none): a static string */
const char *
nemo_archive_format_extension (NemoArchiveFormat format)
{
	g_return_val_if_fail (format_is_valid (format), ".zip");

	return formats[format].extension;
}

gboolean
nemo_archive_format_from_id (const char        *id,
			     NemoArchiveFormat *format)
{
	int i;

	if (id == NULL) {
		return FALSE;
	}

	for (i = 0; i < NEMO_ARCHIVE_N_FORMATS; i++) {
		if (g_strcmp0 (id, formats[i].id) == 0) {
			if (format != NULL) {
				*format = (NemoArchiveFormat) i;
			}
			return TRUE;
		}
	}

	return FALSE;
}

/* PATH does not change under a running app, so one probe per backend is enough.
   Both the dialog on the main thread and the job on its own can ask, hence the
   lock. */
static GMutex   probe_lock;
static char    *probed_program[4];
static gboolean probed[4];
static gboolean hidden_backend[4];

/* 7-Zip on Windows keeps a link as the raw reparse data. Only 7-Zip on Windows
   gives it back as a link; libarchive, which extracts here, reads a file of
   junk and an empty folder. */
#ifdef G_OS_WIN32
static gboolean seven_zip_keeps_links = FALSE;
#else
static gboolean seven_zip_keeps_links = TRUE;
#endif

/* Returns: (transfer full): free with g_free */
char *
nemo_archive_find_command (const char * const *names,
			   const char         *win_subdir)
{
	int i;
	char *found;

#ifdef G_OS_WIN32
	/* The program itself before a script of the same name: cmd ends a .cmd
	   with its own last line's status, so a failed run through one read as
	   a good one. On PATH first, then where its installer puts it. */
	for (i = 0; names[i] != NULL; i++) {
		char *exe = g_strconcat (names[i], ".exe", NULL);

		found = g_find_program_in_path (exe);
		g_free (exe);
		if (found != NULL) {
			return found;
		}
	}

	/* Neither installer puts itself on PATH, and both install under Program
	   Files. */
	{
		static const char * const roots[] = { "ProgramW6432", "ProgramFiles", "ProgramFiles(x86)", NULL };
		int r;

		for (r = 0; roots[r] != NULL; r++) {
			const char *root = g_getenv (roots[r]);

			if (root == NULL) {
				continue;
			}

			for (i = 0; names[i] != NULL; i++) {
				char *exe = g_strconcat (names[i], ".exe", NULL);
				char *path = g_build_filename (root, win_subdir, exe, NULL);

				g_free (exe);

				if (g_file_test (path, G_FILE_TEST_IS_EXECUTABLE)) {
					return path;
				}
				g_free (path);
			}
		}
	}
#else
	(void) win_subdir;
#endif

	/* Anything PATH finds by the name, a wrapper script included. */
	for (i = 0; names[i] != NULL; i++) {
		found = g_find_program_in_path (names[i]);
		if (found != NULL) {
			return found;
		}
	}

	return NULL;
}

static const char *
backend_program (NemoArchiveBackend backend)
{
	static const char * const seven_zip_names[] = { "7z", "7zz", "7za", NULL };
	static const char * const rar_names[] = { "rar", NULL };
	const char *program;

	if (backend != NEMO_ARCHIVE_BACKEND_7Z && backend != NEMO_ARCHIVE_BACKEND_RAR) {
		return NULL;
	}

	g_mutex_lock (&probe_lock);

	if (!probed[backend]) {
		if (backend == NEMO_ARCHIVE_BACKEND_7Z) {
			probed_program[backend] = nemo_archive_find_command (seven_zip_names, "7-Zip");
		} else {
			probed_program[backend] = nemo_archive_find_command (rar_names, "WinRAR");
		}
		probed[backend] = TRUE;
	}
	program = hidden_backend[backend] ? NULL : probed_program[backend];

	g_mutex_unlock (&probe_lock);

	return program;
}

void
nemo_archive_hide_backend (NemoArchiveBackend backend,
			   gboolean           hidden)
{
	g_return_if_fail (backend == NEMO_ARCHIVE_BACKEND_7Z || backend == NEMO_ARCHIVE_BACKEND_RAR);

	g_mutex_lock (&probe_lock);
	hidden_backend[backend] = hidden;
	g_mutex_unlock (&probe_lock);
}

void
nemo_archive_set_7z_keeps_links (gboolean keeps)
{
	seven_zip_keeps_links = keeps;
}

static ArcMountTable *(*mounts_for_test) (void);

void
nemo_archive_set_mounts_for_test (ArcMountTable *(*mounts) (void))
{
	mounts_for_test = mounts;
}

static void (*prescan_watch) (NemoProgressInfo *info);

void
nemo_archive_set_prescan_watch_for_test (void (*watch) (NemoProgressInfo *info))
{
	prescan_watch = watch;
}

gboolean
nemo_archive_backend_present (NemoArchiveBackend backend)
{
	if (backend == NEMO_ARCHIVE_BACKEND_LIBARCHIVE) {
		return TRUE;
	}

	return backend_program (backend) != NULL;
}

NemoArchiveCaps
nemo_archive_backend_caps (NemoArchiveFormat  format,
			   NemoArchiveBackend backend)
{
	const FormatInfo *info;

	g_return_val_if_fail (format_is_valid (format), 0);

	info = &formats[format];

	switch (backend) {
	case NEMO_ARCHIVE_BACKEND_LIBARCHIVE:
		return info->by_libarchive ? info->caps_libarchive : 0;
	case NEMO_ARCHIVE_BACKEND_7Z:
		if (!info->by_7z) {
			return 0;
		}
		return seven_zip_keeps_links ? info->caps_7z
					     : info->caps_7z & ~NEMO_ARCHIVE_CAP_STORE_LINKS;
	case NEMO_ARCHIVE_BACKEND_RAR:
		return info->by_rar ? info->caps_rar : 0;
	case NEMO_ARCHIVE_BACKEND_NONE:
	default:
		return 0;
	}
}

static gboolean
backend_writes_format (NemoArchiveFormat  format,
		       NemoArchiveBackend backend)
{
	const FormatInfo *info = &formats[format];

	switch (backend) {
	case NEMO_ARCHIVE_BACKEND_LIBARCHIVE:
		return info->by_libarchive;
	case NEMO_ARCHIVE_BACKEND_7Z:
		return info->by_7z;
	case NEMO_ARCHIVE_BACKEND_RAR:
		return info->by_rar;
	case NEMO_ARCHIVE_BACKEND_NONE:
	default:
		return FALSE;
	}
}

/* Preference order: libarchive first, since it needs nothing installed and can
   report real progress; a command only when it is the one that can do the job. */
static const NemoArchiveBackend backend_order[] = {
	NEMO_ARCHIVE_BACKEND_LIBARCHIVE,
	NEMO_ARCHIVE_BACKEND_7Z,
	NEMO_ARCHIVE_BACKEND_RAR
};

/* Except 7z, which goes to 7-Zip where it's installed: libarchive's 7z writer
   turns down a threads option, so it compresses on one thread. */
static const NemoArchiveBackend backend_order_7z[] = {
	NEMO_ARCHIVE_BACKEND_7Z,
	NEMO_ARCHIVE_BACKEND_LIBARCHIVE,
	NEMO_ARCHIVE_BACKEND_RAR
};

gboolean
nemo_archive_format_available (NemoArchiveFormat format)
{
	guint i;

	g_return_val_if_fail (format_is_valid (format), FALSE);

	for (i = 0; i < G_N_ELEMENTS (backend_order); i++) {
		if (backend_writes_format (format, backend_order[i]) &&
		    nemo_archive_backend_present (backend_order[i])) {
			return TRUE;
		}
	}

	return FALSE;
}

NemoArchiveCaps
nemo_archive_format_caps (NemoArchiveFormat format)
{
	NemoArchiveCaps caps = 0;
	guint i;

	g_return_val_if_fail (format_is_valid (format), 0);

	for (i = 0; i < G_N_ELEMENTS (backend_order); i++) {
		if (nemo_archive_backend_present (backend_order[i])) {
			caps |= nemo_archive_backend_caps (format, backend_order[i]);
		}
	}

	return caps;
}

/* A writer that claims to store links stores a symlink as one. What becomes
   of a junction follows from how it keeps links: the library writes every
   link as a symlink, and rar's -ol keeps a junction a junction. 7-Zip claims
   links only where there are no junctions. */
guint
nemo_archive_backend_link_stores (NemoArchiveFormat  format,
				  NemoArchiveBackend backend)
{
	if ((nemo_archive_backend_caps (format, backend) & NEMO_ARCHIVE_CAP_STORE_LINKS) == 0) {
		return 0;
	}

	switch (backend) {
	case NEMO_ARCHIVE_BACKEND_LIBARCHIVE:
		return ARC_STORES_SYMLINKS | ARC_STORES_JUNCTIONS_AS_SYMLINKS;
	case NEMO_ARCHIVE_BACKEND_RAR:
		return ARC_STORES_SYMLINKS | ARC_STORES_JUNCTIONS;
	case NEMO_ARCHIVE_BACKEND_7Z:
		return ARC_STORES_SYMLINKS;
	case NEMO_ARCHIVE_BACKEND_NONE:
	default:
		return 0;
	}
}

guint
nemo_archive_format_link_stores (NemoArchiveFormat format)
{
	guint stores = 0;
	guint i;

	g_return_val_if_fail (format_is_valid (format), 0);

	for (i = 0; i < G_N_ELEMENTS (backend_order); i++) {
		if (nemo_archive_backend_present (backend_order[i])) {
			stores |= nemo_archive_backend_link_stores (format, backend_order[i]);
		}
	}

	return stores;
}

/* Two kinds of option. Encryption and splitting change whether the archive is
   what was asked for at all, so a backend that cannot do them is not a
   candidate - writing a readable archive when one was asked to be locked is the
   worst possible outcome. The rest is tuning: preferred where it can be had,
   dropped where it cannot, rather than failing the whole job. */
static NemoArchiveCaps
required_caps (const NemoArchiveOptions *options)
{
	NemoArchiveCaps caps = 0;

	if (options->password != NULL && options->password[0] != '\0') {
		caps |= NEMO_ARCHIVE_CAP_PASSWORD;

		if (options->encrypt_names) {
			caps |= NEMO_ARCHIVE_CAP_ENCRYPT_NAMES;
		}
	}
	if (options->split_size > 0) {
		caps |= NEMO_ARCHIVE_CAP_SPLIT;
	}

	return caps;
}

/* A store choice as chosen, before any writer has it fall back to Ignore.
   Junctions only count on Windows, the only place there are any. */
static gboolean
wants_store (const ArcLinkOptions *links)
{
	if (links->symlinks == ARC_LINK_STORE_SYMLINK) {
		return TRUE;
	}
#ifdef G_OS_WIN32
	return links->junctions_set &&
	       (links->junctions == ARC_LINK_STORE_SYMLINK || links->junctions == ARC_LINK_STORE_JUNCTION);
#else
	return FALSE;
#endif
}

/* Symlinks as the writer can do them, for a line built outside a job. */
static gboolean
stores_symlinks (const NemoArchiveOptions *options,
		 NemoArchiveFormat         format,
		 NemoArchiveBackend        backend)
{
	return arc_link_options_symlinks (&options->links,
					  nemo_archive_backend_link_stores (format, backend)) ==
	       ARC_LINK_STORE_SYMLINK;
}

static NemoArchiveCaps
preferred_caps (const NemoArchiveOptions *options)
{
	NemoArchiveCaps caps = 0;

	if (options->solid) {
		caps |= NEMO_ARCHIVE_CAP_SOLID;
	}
	if (options->dedupe) {
		caps |= NEMO_ARCHIVE_CAP_DEDUPE;
	}
	if (wants_store (&options->links)) {
		caps |= NEMO_ARCHIVE_CAP_STORE_LINKS;
	}
	if (options->recovery_record) {
		caps |= NEMO_ARCHIVE_CAP_RECOVERY;
	}
	if (options->lock) {
		caps |= NEMO_ARCHIVE_CAP_LOCK;
	}

	return caps;
}

NemoArchiveBackend
nemo_archive_pick_backend (NemoArchiveFormat         format,
			   const NemoArchiveOptions *options)
{
	const NemoArchiveBackend *order;
	NemoArchiveCaps needed;
	NemoArchiveCaps preferred;
	NemoArchiveCaps targets[3];
	guint pass;
	guint i;

	g_return_val_if_fail (format_is_valid (format), NEMO_ARCHIVE_BACKEND_NONE);
	g_return_val_if_fail (options != NULL, NEMO_ARCHIVE_BACKEND_NONE);

	G_STATIC_ASSERT (G_N_ELEMENTS (backend_order_7z) == G_N_ELEMENTS (backend_order));
	order = format == NEMO_ARCHIVE_FORMAT_7Z ? backend_order_7z : backend_order;
	needed = required_caps (options);

	/* A preference nothing can honor for this format is dropped before
	   matching, so an option left on by default - a recovery record, which
	   only rar has - does not steer a tar away from the writer that suits
	   the preferences that ARE available. */
	preferred = preferred_caps (options) & nemo_archive_format_caps (format);

	/* Storing links changes what goes in, where the rest only tunes how, so
	   it is the last preference given up. A 7z on Windows that stores links
	   goes to the library rather than 7-Zip, which leaves them out. */
	targets[0] = needed | preferred;
	targets[1] = needed | (preferred & NEMO_ARCHIVE_CAP_STORE_LINKS);
	targets[2] = needed;

	for (pass = 0; pass < G_N_ELEMENTS (targets); pass++) {
		NemoArchiveCaps target = targets[pass];

		for (i = 0; i < G_N_ELEMENTS (backend_order); i++) {
			NemoArchiveBackend backend = order[i];

			if (!nemo_archive_backend_present (backend) ||
			    !backend_writes_format (format, backend)) {
				continue;
			}
			if ((nemo_archive_backend_caps (format, backend) & target) == target) {
				return backend;
			}
		}
	}

	return NEMO_ARCHIVE_BACKEND_NONE;
}

/* On Windows a 7z with a password or volumes needs 7-Zip, which keeps no
   links there, so no store choice is left. */
guint
nemo_archive_options_link_stores (NemoArchiveFormat         format,
				  const NemoArchiveOptions *options)
{
	NemoArchiveCaps needed;
	guint stores = 0;
	guint i;

	g_return_val_if_fail (format_is_valid (format), 0);
	g_return_val_if_fail (options != NULL, 0);

	needed = required_caps (options);

	for (i = 0; i < G_N_ELEMENTS (backend_order); i++) {
		NemoArchiveBackend backend = backend_order[i];

		if (nemo_archive_backend_present (backend) &&
		    (nemo_archive_backend_caps (format, backend) & needed) == needed) {
			stores |= nemo_archive_backend_link_stores (format, backend);
		}
	}

	return stores;
}

gboolean
nemo_archive_links_cost_threads (NemoArchiveFormat         format,
				 const NemoArchiveOptions *options)
{
	NemoArchiveOptions with_links;
	NemoArchiveBackend storing, not_storing;

	g_return_val_if_fail (options != NULL, FALSE);

	if (format != NEMO_ARCHIVE_FORMAT_7Z) {
		return FALSE;
	}

	/* A shallow copy is enough: only the choice changes, and nothing is
	   freed. */
	with_links = *options;
	with_links.links.junctions_set = FALSE;
	with_links.links.symlinks = ARC_LINK_STORE_SYMLINK;
	storing = nemo_archive_pick_backend (format, &with_links);
	with_links.links.symlinks = ARC_LINK_IGNORE;
	not_storing = nemo_archive_pick_backend (format, &with_links);

	return storing == NEMO_ARCHIVE_BACKEND_LIBARCHIVE && not_storing != storing;
}

void
nemo_archive_options_init (NemoArchiveOptions *options)
{
	g_return_if_fail (options != NULL);

	memset (options, 0, sizeof (NemoArchiveOptions));
	options->format = NEMO_ARCHIVE_FORMAT_ZIP;
	options->level = NEMO_ARCHIVE_LEVEL_DEFAULT;
	arc_link_options_init (&options->links);
	options->recovery_record = TRUE;
}

void
nemo_archive_options_clear (NemoArchiveOptions *options)
{
	g_return_if_fail (options != NULL);

	g_clear_pointer (&options->password, g_free);
}

void
nemo_archive_options_copy (const NemoArchiveOptions *source,
			   NemoArchiveOptions       *dest)
{
	g_return_if_fail (source != NULL);
	g_return_if_fail (dest != NULL);

	*dest = *source;
	dest->password = g_strdup (source->password);
}

/* Returns: (transfer full): free with g_free */
char *
nemo_archive_strip_extension (const char *name)
{
	int i;
	gsize len;

	g_return_val_if_fail (name != NULL, NULL);

	len = strlen (name);

	for (i = 0; known_extensions[i] != NULL; i++) {
		gsize ext_len = strlen (known_extensions[i]);

		/* A suffix only, and never the whole name - ".zip" on its own is
		   a file called .zip, not an empty name. */
		if (len > ext_len &&
		    g_ascii_strcasecmp (name + len - ext_len, known_extensions[i]) == 0) {
			return g_strndup (name, len - ext_len);
		}
	}

	return g_strdup (name);
}

/* Returns: (transfer full): free with g_free */
char *
nemo_archive_apply_extension (const char        *name,
			      NemoArchiveFormat  format)
{
	char *base;
	char *result;

	g_return_val_if_fail (name != NULL, NULL);
	g_return_val_if_fail (format_is_valid (format), NULL);

	base = nemo_archive_strip_extension (name);
	result = g_strconcat (base, formats[format].extension, NULL);
	g_free (base);

	return result;
}

/* One item names the archive after itself, whether it is a file or a folder.
   Several only borrow the folder's name when they are the whole folder; a part
   of one has no name a user would agree with, so none is offered.
   Returns: (transfer full): free with g_free */
char *
nemo_archive_suggest_name (GList             *files,
			   gboolean           whole_folder,
			   NemoArchiveFormat  format)
{
	char *base = NULL;
	char *result;

	g_return_val_if_fail (format_is_valid (format), NULL);

	if (files == NULL) {
		return NULL;
	}

	if (files->next == NULL) {
		base = g_file_get_basename (G_FILE (files->data));
	} else if (whole_folder) {
		GFile *parent = g_file_get_parent (G_FILE (files->data));

		if (parent != NULL) {
			base = g_file_get_basename (parent);
			g_object_unref (parent);
		}
	}

	/* A root has no basename worth using, and neither does anything that
	   came back as "." or a bare separator. */
	if (base == NULL || base[0] == '\0' ||
	    g_strcmp0 (base, ".") == 0 || g_strcmp0 (base, G_DIR_SEPARATOR_S) == 0) {
		g_free (base);
		return NULL;
	}

	result = nemo_archive_apply_extension (base, format);
	g_free (base);

	return result;
}

/* Compressing a selection separately names each archive after the item it came
   from, extension and all: "notes.rar" becomes "notes.rar.zip". The name field
   strips a suffix it recognizes, which cannot be done here - re-zipping a zip
   would then write the archive over the file being read.
   Returns: (transfer full): free with g_free */
char *
nemo_archive_each_name (const char        *item_name,
			NemoArchiveFormat  format)
{
	g_return_val_if_fail (format_is_valid (format), NULL);

	if (item_name == NULL || item_name[0] == '\0' ||
	    g_strcmp0 (item_name, ".") == 0 ||
	    strchr (item_name, '/') != NULL ||
	    strchr (item_name, G_DIR_SEPARATOR) != NULL) {
		return NULL;
	}

	return g_strconcat (item_name, formats[format].extension, NULL);
}

/* Binary units, because that is what the sizes mean and what the parser has
   always computed. The label is translated whole, since the note is. */
static const struct {
	const char *size;
	const char *label;
} volume_presets[] = {
	{ "25 MiB",   N_("25 MiB (email)") },
	{ "100 MiB",  N_("100 MiB (upload limits)") },
	{ "700 MiB",  N_("700 MiB (CD)") },
	{ "2 GiB",    N_("2 GiB (old 32-bit tools)") },
	{ "4095 MiB", N_("4,095 MiB (max FAT32 size)") },
	{ "4480 MiB", N_("4,480 MiB (DVD)") },
	{ "8140 MiB", N_("8,140 MiB (dual-layer DVD)") },
	{ "23 GiB",   N_("23 GiB (Blu-ray)") },
};

/* Returns: (transfer none) */
const char *
nemo_archive_volume_preset (guint        index,
			    const char **size)
{
	if (index >= G_N_ELEMENTS (volume_presets)) {
		return NULL;
	}

	if (size != NULL) {
		*size = volume_presets[index].size;
	}

	return _(volume_presets[index].label);
}

/* The untranslated label too, so one remembered under another language still
   reads. */
int
nemo_archive_volume_preset_find (const char *text)
{
	char *trimmed;
	int found = -1;
	guint i;

	if (text == NULL) {
		return -1;
	}

	trimmed = g_strstrip (g_strdup (text));

	for (i = 0; i < G_N_ELEMENTS (volume_presets) && found < 0; i++) {
		if (strcmp (trimmed, volume_presets[i].size) == 0 ||
		    strcmp (trimmed, volume_presets[i].label) == 0 ||
		    strcmp (trimmed, _(volume_presets[i].label)) == 0) {
			found = (int) i;
		}
	}

	g_free (trimmed);

	return found;
}

/* Digits, grouped by commas the way the presets show them or not at all, then
   a point and more digits. "1,5" isn't a grouping, and could mean 1.5, so it
   isn't a number here. No sign, exponent, hex, inf or nan, which strtod would
   take. */
static gboolean
read_plain_number (const char  *text,
		   double      *value,
		   const char **end)
{
	GString *digits = g_string_new (NULL);
	const char *p = text;
	guint run = 0;
	gboolean grouped = FALSE;
	gboolean ok = TRUE;

	for (; g_ascii_isdigit (*p) || *p == ','; p++) {
		if (*p != ',') {
			g_string_append_c (digits, *p);
			run++;
			continue;
		}
		/* The first group is 1 to 3 digits, every one after it 3. */
		if (run == 0 || run > 3 || (grouped && run != 3)) {
			ok = FALSE;
		}
		grouped = TRUE;
		run = 0;
	}
	if (grouped && run != 3) {
		ok = FALSE;
	}

	if (*p == '.') {
		g_string_append_c (digits, *p++);
		while (g_ascii_isdigit (*p)) {
			g_string_append_c (digits, *p++);
		}
	}

	if (!ok || strpbrk (digits->str, "0123456789") == NULL) {
		g_string_free (digits, TRUE);
		return FALSE;
	}

	*value = g_ascii_strtod (digits->str, NULL);
	*end = p;
	g_string_free (digits, TRUE);

	return TRUE;
}

/* Sizes are read the way archivers write them: k, m, g and t are 1024-based,
   and a bare number is bytes. Anything that comes out under a byte, or past
   what a size can hold, is refused, since a size of 0 means no volumes. */
gboolean
nemo_archive_parse_size (const char *text,
			 guint64    *bytes)
{
	const char *p;
	const char *end = NULL;
	double value;
	double total;
	guint64 multiplier;
	int preset;

	if (text == NULL) {
		return FALSE;
	}

	preset = nemo_archive_volume_preset_find (text);
	if (preset >= 0) {
		text = volume_presets[preset].size;
	}

	p = text;
	while (g_ascii_isspace (*p)) {
		p++;
	}
	if (*p == '\0') {
		return FALSE;
	}

	if (!read_plain_number (p, &value, &end) || value <= 0.0) {
		return FALSE;
	}

	while (g_ascii_isspace (*end)) {
		end++;
	}

	switch (g_ascii_tolower (*end)) {
	case '\0':
		multiplier = 1;
		break;
	case 'b':
		multiplier = 1;
		end++;
		break;
	case 'k':
		multiplier = G_GUINT64_CONSTANT (1024);
		end++;
		break;
	case 'm':
		multiplier = G_GUINT64_CONSTANT (1024) * 1024;
		end++;
		break;
	case 'g':
		multiplier = G_GUINT64_CONSTANT (1024) * 1024 * 1024;
		end++;
		break;
	case 't':
		multiplier = G_GUINT64_CONSTANT (1024) * 1024 * 1024 * 1024;
		end++;
		break;
	default:
		return FALSE;
	}

	/* A trailing "b" as in "MB", or "ib" as in "MiB", names the same unit
	   rather than another factor. Both spellings mean the binary one here. */
	if (multiplier > 1 && g_ascii_tolower (*end) == 'i') {
		end++;
	}
	if (multiplier > 1 && g_ascii_tolower (*end) == 'b') {
		end++;
	}
	while (g_ascii_isspace (*end)) {
		end++;
	}
	if (*end != '\0') {
		return FALSE;
	}

	/* 2^63, comfortably past any disk, and exact as a double. */
	total = value * (double) multiplier;
	if (total < 1.0 || total >= 9223372036854775808.0) {
		return FALSE;
	}

	if (bytes != NULL) {
		*bytes = (guint64) total;
	}

	return TRUE;
}

int
nemo_archive_options_room (int work_height,
			   int closed_height)
{
	return MAX (120, work_height - closed_height);
}

/* Returns: (transfer full): free with g_free */
char *
nemo_archive_format_size (guint64 bytes)
{
	const guint64 kb = G_GUINT64_CONSTANT (1024);
	const guint64 mb = kb * 1024;
	const guint64 gb = mb * 1024;

	if (bytes == 0) {
		return g_strdup ("");
	}
	if (bytes >= gb && bytes % gb == 0) {
		return g_strdup_printf ("%llu GiB", (unsigned long long) (bytes / gb));
	}
	if (bytes >= mb && bytes % mb == 0) {
		return g_strdup_printf ("%llu MiB", (unsigned long long) (bytes / mb));
	}
	if (bytes >= kb && bytes % kb == 0) {
		return g_strdup_printf ("%llu KiB", (unsigned long long) (bytes / kb));
	}

	return g_strdup_printf ("%llu", (unsigned long long) bytes);
}

/* effective is what GIO said with the link followed. With nothing at the
   other end it answers for the link itself: as a link on Linux, but on
   Windows with the link's own type, a file or a folder, so there it takes a
   look of its own. */
static gboolean
leads_nowhere (GFile     *file,
	       GFileInfo *effective)
{
	if (effective == NULL ||
	    g_file_info_get_file_type (effective) == G_FILE_TYPE_SYMBOLIC_LINK) {
		return TRUE;
	}
#ifdef G_OS_WIN32
	{
		const char *path = g_file_peek_path (file);

		return path != NULL && !nemo_win32_link_leads_somewhere (path);
	}
#else
	(void) file;
	return FALSE;
#endif
}

/* One thing that should have gone into the archive. size is -1 where only
   being there can be checked: a directory, or something stored as a link. */
typedef struct {
	gint64   size;
	gboolean is_dir;
} Expected;

typedef struct {
	GHashTable   *expected;		/* rel_path -> Expected * */
	GHashTable   *seen_dirs;	/* file ids, so a link loop terminates */
	GCancellable *cancellable;
	gboolean      whole;		/* nothing was passed over on the way */
	/* Left out by the options, so not in the archive, and not read past. */
	guint         left_folders;
	guint         left_files;
	gboolean      links_locked;	/* a link kept as a link can't be listed */
	gboolean      locked_link;	/* and one is expected */
	ArcLinkChoice             symlinks;
	ArcLinkChoice             junctions;
	GHashTable               *listing;
	GHashTable               *folders;
} VerifyWalk;

static void
expect (VerifyWalk *walk,
	const char *rel_path,
	gint64      size,
	gboolean    is_dir)
{
	Expected *item = g_new0 (Expected, 1);

	item->size = size;
	item->is_dir = is_dir;

	g_hash_table_replace (walk->expected, g_strdup (rel_path), item);
}

static gboolean listing_holds_folder (GHashTable *listing, GHashTable *folders,
				      const char *rel_path);

static void
left_out (VerifyWalk *walk,
	  GFileInfo  *info)
{
	if (g_file_info_get_file_type (info) == G_FILE_TYPE_DIRECTORY) {
		walk->left_folders++;
	} else {
		walk->left_files++;
	}
}

typedef enum {
	NOT_A_LINK,
	A_SYMLINK,
	A_JUNCTION,
	A_MOUNT,	/* Windows: a folder a volume is mounted at */
} LinkSort;

/* Read without going through it. On Windows GLib calls a junction a folder,
   and can't tell one from a mount point. */
static LinkSort
link_sort (GFile     *file,
	   GFileInfo *info)
{
#ifdef G_OS_WIN32
	char *target = NULL;
	ArcEntry entry = arc_entry_read (g_file_peek_path (file), &target);

	(void) info;
	g_free (target);
	switch (entry) {
	case ARC_ENTRY_SYMLINK:
		return A_SYMLINK;
	case ARC_ENTRY_JUNCTION:
		return A_JUNCTION;
	case ARC_ENTRY_MOUNT_POINT:
		return A_MOUNT;
	case ARC_ENTRY_MISSING:
	case ARC_ENTRY_PLAIN:
	default:
		return NOT_A_LINK;
	}
#else
	(void) file;
	return g_file_info_get_is_symlink (info) ? A_SYMLINK : NOT_A_LINK;
#endif
}

static void
expect_link (VerifyWalk *walk,
	     const char *rel_path)
{
	walk->locked_link = walk->locked_link || walk->links_locked;
	expect (walk, rel_path, -1, FALSE);
}

static void verify_walk_item (VerifyWalk *walk, GFile *file, const char *rel_path,
			      GFileInfo *info, guint32 device, gboolean via_link);

static void
verify_walk_directory (VerifyWalk *walk,
		       GFile      *dir,
		       const char *rel_path,
		       guint32     device,
		       gboolean    via_link)
{
	GFileEnumerator *children;
	GFileInfo *child_info;

	children = nemo_enumerate_children (dir, SCAN_ATTRIBUTES,
					    G_FILE_QUERY_INFO_NOFOLLOW_SYMLINKS,
					    walk->cancellable, NULL);
	if (children == NULL) {
		walk->whole = FALSE;
		return;
	}

	while (!g_cancellable_is_cancelled (walk->cancellable)) {
		GFile *child;
		char *child_rel;

		child_info = g_file_enumerator_next_file (children, walk->cancellable, NULL);
		if (child_info == NULL) {
			break;
		}

		child = g_file_get_child (dir, g_file_info_get_name (child_info));
		child_rel = g_strconcat (rel_path, "/", g_file_info_get_name (child_info), NULL);

		verify_walk_item (walk, child, child_rel, child_info, device, via_link);

		g_free (child_rel);
		g_object_unref (child);
		g_object_unref (child_info);
	}

	g_file_enumerator_close (children, NULL, NULL);
	g_object_unref (children);
}

/* Once by a link is enough, and once by the plain path. The plain path is
   walked even after a link, as the job's walk does. A folder walked already
   is no miss, and nothing more is expected of it. */
static gboolean
walked_before (VerifyWalk *walk,
	       GFileInfo  *effective,
	       gboolean    via_link)
{
	const char *id;
	guint seen;

	if (g_file_info_get_file_type (effective) != G_FILE_TYPE_DIRECTORY) {
		return FALSE;
	}
	id = g_file_info_get_attribute_string (effective, G_FILE_ATTRIBUTE_ID_FILE);
	if (id == NULL) {
		return FALSE;
	}
	seen = GPOINTER_TO_UINT (g_hash_table_lookup (walk->seen_dirs, id));
	return (seen & 1) || ((seen & 2) && via_link);
}

/* effective is what it's read as, the far end of a followed link. */
static void
verify_walk_found (VerifyWalk *walk,
		   GFile      *file,
		   const char *rel_path,
		   GFileInfo  *effective,
		   gboolean    via_link)
{
	GFileType type = g_file_info_get_file_type (effective);

	if (type == G_FILE_TYPE_DIRECTORY) {
		const char *id = g_file_info_get_attribute_string (effective, G_FILE_ATTRIBUTE_ID_FILE);

		if (id != NULL) {
			guint seen = GPOINTER_TO_UINT (g_hash_table_lookup (walk->seen_dirs, id));

			g_hash_table_replace (walk->seen_dirs, g_strdup (id),
					      GUINT_TO_POINTER (seen | (via_link ? 2u : 1u)));
		}

		expect (walk, rel_path, -1, TRUE);
		verify_walk_directory (walk, file, rel_path,
				       g_file_info_get_attribute_uint32 (effective, G_FILE_ATTRIBUTE_UNIX_DEVICE),
				       via_link);
	} else if (type == G_FILE_TYPE_REGULAR) {
		expect (walk, rel_path, g_file_info_get_size (effective), FALSE);
	} else {
		walk->whole = FALSE;	/* a socket or a device node, and no way to store it */
	}
}

/* The job's rules, arrived at apart from its walk: a link by its choice,
   and what a followed one leads to under the link's own name. A link or a
   mounted folder the job's walk left out for its filesystem isn't in the
   archive, and isn't read here either, so it's a miss. device is the one the
   folder this is in is on, or 0, as on Windows. */
static void
verify_walk_item (VerifyWalk *walk,
		  GFile      *file,
		  const char *rel_path,
		  GFileInfo  *info,
		  guint32     device,
		  gboolean    via_link)
{
	LinkSort sort;
	ArcLinkChoice choice;
	GFileInfo *effective;

	if (g_cancellable_is_cancelled (walk->cancellable)) {
		return;
	}

	sort = link_sort (file, info);
	switch (sort) {
	case A_SYMLINK:
	case A_JUNCTION:
		choice = sort == A_SYMLINK ? walk->symlinks : walk->junctions;
		if (choice == ARC_LINK_IGNORE) {
			left_out (walk, info);
			return;
		}
		if (choice != ARC_LINK_FOLLOW) {
			expect_link (walk, rel_path);
			return;
		}
		effective = g_file_query_info (file, SCAN_ATTRIBUTES, G_FILE_QUERY_INFO_NONE,
					       walk->cancellable, NULL);
		if (leads_nowhere (file, effective)) {
			g_clear_object (&effective);
			expect_link (walk, rel_path);
			return;
		}
		via_link = TRUE;
		break;
	case A_MOUNT:
		effective = g_file_query_info (file, SCAN_ATTRIBUTES, G_FILE_QUERY_INFO_NONE,
					       walk->cancellable, NULL);
		if (effective == NULL) {
			walk->whole = FALSE;
			return;
		}
		break;
	case NOT_A_LINK:
	default:
		effective = g_object_ref (info);
		break;
	}

	if (walked_before (walk, effective, via_link)) {
		g_object_unref (effective);
		return;
	}
	if (sort != NOT_A_LINK ||
	    (g_file_info_get_file_type (effective) == G_FILE_TYPE_DIRECTORY &&
	     g_file_info_get_attribute_uint32 (effective, G_FILE_ATTRIBUTE_UNIX_DEVICE) != device)) {
		if (!listing_holds_folder (walk->listing, walk->folders, rel_path)) {
			left_out (walk, effective);
			g_object_unref (effective);
			return;
		}
	}

	verify_walk_found (walk, file, rel_path, effective, via_link);
	g_object_unref (effective);
}

/* Archive paths come back in whatever the writer stored: a "./" in front from
   tar, a "/" behind on a directory, backslashes from a program on Windows. */
static char *
normal_archive_path (const char *raw)
{
	char *path = g_strdup (raw != NULL ? raw : "");
	char *start = path;
	gsize length;
	char *out;

	for (out = path; *out != '\0'; out++) {
		if (*out == '\\') {
			*out = '/';
		}
	}

	while (g_str_has_prefix (start, "./")) {
		start += 2;
	}
	while (*start == '/') {
		start++;
	}

	out = g_strdup (start);
	g_free (path);

	length = strlen (out);
	while (length > 0 && out[length - 1] == '/') {
		out[--length] = '\0';
	}

	return out;
}

/* Every folder an entry sits under. Collected while the listing is read, so
   that asking whether a folder is in there is a lookup rather than another
   walk of every entry. */
static void
add_ancestors (GHashTable *folders,
	       const char *name)
{
	const char *slash;

	for (slash = strchr (name, '/'); slash != NULL; slash = strchr (slash + 1, '/')) {
		g_hash_table_add (folders, g_strndup (name, slash - name));
	}
}

typedef struct {
	gint64   size;		/* -1 where none is recorded */
	gboolean is_dir;
} Listed;

/* Every entry the archive holds, by path. NULL when the file will not open
   as an archive. @folders_out gets the ancestor set, owned by the caller. */
static GHashTable *
archive_listing (const char *path,
		 const char *password,
		 GHashTable **folders_out)
{
	struct archive *a = archive_read_new ();
	struct archive_entry *entry;
	GHashTable *listing;

	archive_read_support_format_all (a);
	archive_read_support_filter_all (a);

	if (password != NULL && password[0] != '\0') {
		archive_read_add_passphrase (a, password);
	}

	if (archive_read_open_filename (a, path, READ_BUFFER_SIZE) != ARCHIVE_OK) {
		archive_read_free (a);
		return NULL;
	}

	listing = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, g_free);
	*folders_out = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);

	while (archive_read_next_header (a, &entry) == ARCHIVE_OK) {
		/* The plain name is in the locale's code page, which on Windows
		   is not UTF-8, so a name past ASCII would match nothing. */
		const char *raw = archive_entry_pathname_utf8 (entry);
		char *name = normal_archive_path (raw != NULL ? raw : archive_entry_pathname (entry));
		Listed *listed = g_new (Listed, 1);

		/* No size is not a wrong size: rar keeps a second copy of a file
		   as a reference to the first, which is read back with none. */
		listed->size = archive_entry_size_is_set (entry) ?
			(gint64) archive_entry_size (entry) : -1;
		listed->is_dir = archive_entry_filetype (entry) == AE_IFDIR;

		add_ancestors (*folders_out, name);
		g_hash_table_replace (listing, name, listed);
	}

	archive_read_free (a);

	return listing;
}

/* A writer is free to leave the directories themselves out and store only what
   is under them, so a folder counts as there when anything inside it is. */
static gboolean
listing_holds_folder (GHashTable *listing,
		      GHashTable *folders,
		      const char *rel_path)
{
	return g_hash_table_contains (listing, rel_path) ||
	       g_hash_table_contains (folders, rel_path);
}

/* Why an archive written this way can't be read back at all, or NULL. One
   volume of a split set is not an archive on its own, and libarchive will not
   open a -hp rar or a -mhe 7z whatever password it is handed. */
static const char *
cannot_read_back (const NemoArchiveOptions *options)
{
	if (options->split_size > 0) {
		return _("An archive split into volumes cannot be checked.");
	}
	if (options->encrypt_names && options->password != NULL && options->password[0] != '\0') {
		return _("An archive with its file names encrypted cannot be checked.");
	}
	return NULL;
}

/* 7-Zip keeps a link's target in the file's data, which the password locks,
   and libarchive can't unlock a 7z: it ends the listing at the first link. */
static gboolean
links_locked (const NemoArchiveOptions *options)
{
	return options->format == NEMO_ARCHIVE_FORMAT_7Z &&
	       options->password != NULL && options->password[0] != '\0';
}

/* Whether an archive written this way could be read back. Nothing is removed
   on the writer's word alone, so this is also the answer to whether the
   Compress dialog may offer to delete the originals. A 7z with a password
   that keeps links can't be: the dialog hands over the Store choice only for
   a selection that can hold a link. */
gboolean
nemo_archive_can_verify (const NemoArchiveOptions *options)
{
	g_return_val_if_fail (options != NULL, FALSE);

	if (cannot_read_back (options) != NULL) {
		return FALSE;
	}

	return !(links_locked (options) &&
		 arc_link_options_symlinks (&options->links,
					    nemo_archive_options_link_stores (options->format, options)) ==
		 ARC_LINK_STORE_SYMLINK);
}

/* Whether the password has to be typed a second time before the job starts.
   Deleting the originals is the reason: a mistyped password still writes a
   perfectly good archive, the read-back still passes, and the files still go,
   leaving an archive nobody can open. Either half on its own is recoverable,
   so it is only the two together. */
gboolean
nemo_archive_should_confirm_password (const NemoArchiveOptions *options)
{
	g_return_val_if_fail (options != NULL, FALSE);

	return options->delete_sources &&
	       options->password != NULL && options->password[0] != '\0';
}

/* "2 folders and 3 files", leaving out a kind there are none of. */
static char *
count_words (guint folders,
	     guint files)
{
	char *folder_words = g_strdup_printf (ngettext ("%'u folder", "%'u folders", folders), folders);
	char *file_words = g_strdup_printf (ngettext ("%'u file", "%'u files", files), files);
	char *words;

	if (folders > 0 && files > 0) {
		/* TRANSLATORS: "2 folders" and "3 files" */
		words = g_strdup_printf (_("%s and %s"), folder_words, file_words);
	} else {
		words = g_strdup (folders > 0 ? folder_words : file_words);
	}

	g_free (folder_words);
	g_free (file_words);
	return words;
}

/* Why the archive doesn't match the selection, in counts: what was selected
   and isn't in it, and what is in it and wasn't selected. Sizes only when
   the counts match, since a count off already says the sizes will be. NULL
   when it matches. */
static char *
mismatch_reason (guint   missing_folders,
		 guint   missing_files,
		 guint   extra_folders,
		 guint   extra_files,
		 guint64 selected_bytes,
		 guint64 archived_bytes,
		 guint   resized)
{
	GString *said = g_string_new (NULL);
	guint missing = missing_folders + missing_files;
	guint extra = extra_folders + extra_files;
	char *words;

	if (missing > 0) {
		words = count_words (missing_folders, missing_files);
		g_string_append_printf (said, ngettext ("%s that was selected is not in the archive.",
							"%s that were selected are not in the archive.",
							missing),
					words);
		g_free (words);
	}
	if (extra > 0) {
		if (said->len > 0) {
			g_string_append_c (said, ' ');
		}
		words = count_words (extra_folders, extra_files);
		g_string_append_printf (said, ngettext ("%s in the archive was not selected.",
							"%s in the archive were not selected.",
							extra),
					words);
		g_free (words);
	}

	if (missing == 0 && extra == 0) {
		if (selected_bytes != archived_bytes) {
			char *selected = g_format_size_full (selected_bytes, G_FORMAT_SIZE_LONG_FORMAT);
			char *archived = g_format_size_full (archived_bytes, G_FORMAT_SIZE_LONG_FORMAT);

			g_string_append_printf (said, _("The selection is %s, but the archive holds %s."),
						selected, archived);
			g_free (archived);
			g_free (selected);
		} else if (resized > 0) {
			/* The same total from sizes that differ. */
			g_string_append_printf (said, ngettext ("%'u file in the archive is not the size of the one selected.",
								"%'u files in the archive are not the size of the ones selected.",
								resized),
						resized);
		}
	}

	if (said->len == 0) {
		g_string_free (said, TRUE);
		return NULL;
	}
	return g_string_free (said, FALSE);
}

gboolean
nemo_archive_verify (GFile                    *archive_file,
		     GList                    *sources,
		     const NemoArchiveOptions *options,
		     NemoArchiveBackend        backend,
		     GCancellable             *cancellable,
		     char                    **reason)
{
	VerifyWalk walk = { 0 };
	GHashTable *listing = NULL;
	GHashTable *folders = NULL;
	GHashTable *roots = NULL;	/* the selection's own names */
	GHashTableIter iter;
	gpointer key;
	gpointer value;
	char *path;
	char *trouble = NULL;
	GList *l;

	g_return_val_if_fail (G_IS_FILE (archive_file), FALSE);
	g_return_val_if_fail (options != NULL, FALSE);

	if (reason != NULL) {
		*reason = NULL;
	}

	if (cannot_read_back (options) != NULL) {
		trouble = g_strdup (cannot_read_back (options));
		goto out;
	}

	path = g_file_get_path (archive_file);
	if (path == NULL) {
		trouble = g_strdup (_("The archive is not on this computer, so it cannot be read back."));
		goto out;
	}

	listing = archive_listing (path, options->password, &folders);
	g_free (path);

	if (listing == NULL) {
		trouble = g_strdup (_("The archive could not be read back."));
		goto out;
	}

	walk.expected = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, g_free);
	walk.seen_dirs = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);
	walk.cancellable = cancellable;
	walk.whole = TRUE;
	walk.links_locked = links_locked (options) &&
		(nemo_archive_backend_caps (options->format, backend) & NEMO_ARCHIVE_CAP_STORE_LINKS);
	{
		guint stores = nemo_archive_backend_link_stores (options->format, backend);

		walk.symlinks = arc_link_options_symlinks (&options->links, stores);
		walk.junctions = arc_link_options_junctions (&options->links, stores);
	}
	walk.listing = listing;
	walk.folders = folders;
	roots = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);

	for (l = sources; l != NULL; l = l->next) {
		GFile *file = G_FILE (l->data);
		GFile *parent;
		GFileInfo *info, *above;
		char *name;

		g_hash_table_add (roots, g_file_get_basename (file));
		info = g_file_query_info (file, SCAN_ATTRIBUTES,
					  G_FILE_QUERY_INFO_NOFOLLOW_SYMLINKS,
					  cancellable, NULL);
		if (info == NULL) {
			walk.whole = FALSE;
			continue;
		}

		name = g_file_get_basename (file);
		parent = g_file_get_parent (file);
		above = parent != NULL
			? g_file_query_info (parent, G_FILE_ATTRIBUTE_UNIX_DEVICE,
					     G_FILE_QUERY_INFO_NONE, cancellable, NULL)
			: NULL;

		verify_walk_item (&walk, file, name, info,
				  above != NULL ? g_file_info_get_attribute_uint32 (above, G_FILE_ATTRIBUTE_UNIX_DEVICE) : 0,
				  FALSE);
		g_clear_object (&above);
		g_clear_object (&parent);
		g_free (name);
		g_object_unref (info);
	}

	if (g_cancellable_is_cancelled (cancellable)) {
		trouble = g_strdup (_("Checking the archive was stopped."));
	} else if (walk.locked_link) {
		trouble = g_strdup (_("A 7z with a password cannot be checked when it keeps links as links."));
	} else if (!walk.whole) {
		trouble = g_strdup (_("Something in the selection could not go into an archive, so not all of it is in there."));
	}

	if (trouble == NULL) {
		guint missing_folders = walk.left_folders;
		guint missing_files = walk.left_files;
		guint extra_folders = 0;
		guint extra_files = 0;
		guint64 selected_bytes = 0;
		guint64 archived_bytes = 0;
		guint resized = 0;

		g_hash_table_iter_init (&iter, walk.expected);
		while (g_hash_table_iter_next (&iter, &key, &value)) {
			const char *rel_path = key;
			const Expected *item = value;
			const Listed *stored;

			if (item->is_dir) {
				if (!listing_holds_folder (listing, folders, rel_path)) {
					missing_folders++;
				}
				continue;
			}

			stored = g_hash_table_lookup (listing, rel_path);
			if (stored == NULL) {
				missing_files++;
			} else if (item->size >= 0 && stored->size >= 0) {
				selected_bytes += (guint64) item->size;
				archived_bytes += (guint64) stored->size;
				if (stored->size != item->size) {
					resized++;
				}
			}
		}

		/* Under the selection's own names. The job writes a new
		   archive, so anything beside them came from another writer,
		   and says nothing about the originals. */
		g_hash_table_iter_init (&iter, listing);
		while (g_hash_table_iter_next (&iter, &key, &value)) {
			const Listed *stored = value;
			const char *slash = strchr (key, '/');
			char *root = g_strndup (key, slash != NULL ? (gsize) (slash - (const char *) key) : strlen (key));
			gboolean under = g_hash_table_contains (roots, root);

			g_free (root);
			if (under && !g_hash_table_contains (walk.expected, key)) {
				if (stored->is_dir) {
					extra_folders++;
				} else {
					extra_files++;
				}
			}
		}

		trouble = mismatch_reason (missing_folders, missing_files, extra_folders, extra_files,
					   selected_bytes, archived_bytes, resized);
	}

	if (trouble == NULL && g_hash_table_size (walk.expected) == 0) {
		trouble = g_strdup (_("There was nothing to check."));
	}

	g_hash_table_destroy (listing);
	g_hash_table_destroy (folders);

 out:
	if (roots != NULL) {
		g_hash_table_destroy (roots);
	}
	if (walk.expected != NULL) {
		g_hash_table_destroy (walk.expected);
	}
	if (walk.seen_dirs != NULL) {
		g_hash_table_destroy (walk.seen_dirs);
	}

	if (trouble != NULL) {
		if (reason != NULL) {
			*reason = trouble;
		} else {
			g_free (trouble);
		}
		return FALSE;
	}

	return TRUE;
}

static void
archive_unit_free (ArchiveUnit *unit)
{
	g_list_free_full (unit->sources, g_object_unref);
	g_clear_object (&unit->destination);
	g_free (unit);
}

static gboolean
job_aborted (ArchiveJob *job)
{
	return g_cancellable_is_cancelled (job->cancellable);
}

static gboolean
format_pads_entries (NemoArchiveFormat format)
{
	return format == NEMO_ARCHIVE_FORMAT_TAR ||
	       format == NEMO_ARCHIVE_FORMAT_TAR_GZ ||
	       format == NEMO_ARCHIVE_FORMAT_TAR_XZ;
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
configure_writer (struct archive  *a,
		  ArchiveJob      *job,
		  GError         **error)
{
	const NemoArchiveOptions *options = &job->options;
	int level = CLAMP (options->level, 0, NEMO_ARCHIVE_LEVEL_MAX);
	gboolean has_password = options->password != NULL && options->password[0] != '\0';
	char *opts = NULL;

	/* Blocking is a tar convention: libarchive pads the output to 10240 bytes
	   by default, which a tar reader expects and every other format reads as
	   junk after the end of the archive. */
	if (options->format == NEMO_ARCHIVE_FORMAT_ZIP ||
	    options->format == NEMO_ARCHIVE_FORMAT_7Z) {
		archive_write_set_bytes_per_block (a, 0);
	}

	switch (options->format) {
	case NEMO_ARCHIVE_FORMAT_ZIP:
		archive_write_set_format_zip (a);
		if (level == NEMO_ARCHIVE_LEVEL_STORE) {
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
	case NEMO_ARCHIVE_FORMAT_TAR:
		archive_write_set_format_pax_restricted (a);
		break;
	case NEMO_ARCHIVE_FORMAT_TAR_GZ:
		archive_write_set_format_pax_restricted (a);
		archive_write_add_filter_gzip (a);
		opts = g_strdup_printf ("gzip:compression-level=%d", level);
		break;
	case NEMO_ARCHIVE_FORMAT_TAR_XZ:
		archive_write_set_format_pax_restricted (a);
		archive_write_add_filter_xz (a);
		/* The only built-in format that can be spread over cores. Deflate and
		   libarchive's own lzma2 have no such option, so they are left alone
		   rather than handed one they would refuse. */
		opts = g_strdup_printf ("xz:compression-level=%d,xz:threads=%d",
					level, nemo_global_preferences_get_cpu_thread_count ());
		break;
	case NEMO_ARCHIVE_FORMAT_7Z:
		archive_write_set_format_7zip (a);
		if (level == NEMO_ARCHIVE_LEVEL_STORE) {
			opts = g_strdup ("7zip:compression=copy");
		} else {
			opts = g_strdup_printf ("7zip:compression=lzma2,7zip:compression-level=%d", level);
		}
		break;
	default:
		g_set_error (error, G_IO_ERROR, G_IO_ERROR_NOT_SUPPORTED,
			     _("This archive format cannot be written here."));
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
		if (options->format != NEMO_ARCHIVE_FORMAT_ZIP ||
		    archive_write_set_options (a, "zip:encryption=aes256") != ARCHIVE_OK ||
		    archive_write_set_passphrase (a, options->password) != ARCHIVE_OK) {
			g_set_error (error, G_IO_ERROR, G_IO_ERROR_NOT_SUPPORTED,
				     _("This archive format cannot be encrypted here."));
			return FALSE;
		}
	}

	return TRUE;
}

static gboolean
write_entry_data (ArchiveJob      *job,
		  struct archive  *a,
		  GFile           *file,
		  GError         **error)
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
		nemo_progress_info_set_progress (job->progress,
						 (double) job->done_bytes,
						 (double) job->total_bytes);
	}

	g_input_stream_close (G_INPUT_STREAM (in), NULL, NULL);
	g_object_unref (in);

	return ok;
}

static gboolean
write_item (ArchiveJob      *job,
	    struct archive  *a,
	    const ArcItem   *item,
	    GError         **error)
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
job_fail (ArchiveJob *job,
	  const char *message,
	  const char *details)
{
	if (job->error_message != NULL) {
		return;
	}

	job->error_message = g_strdup (message);
	job->error_details = g_strdup (details);
}

static void
job_fail_from_error (ArchiveJob *job,
		     GError     *error)
{
	if (error != NULL && g_error_matches (error, G_IO_ERROR, G_IO_ERROR_CANCELLED)) {
		return;
	}

	job_fail (job, _("The archive could not be created."),
		  error != NULL ? error->message : NULL);
}

/* With several archives to write, which one is in hand says more than how much
   is in it, so it takes the status line and the per-archive wording is dropped.
   Takes ownership of that wording either way. */
static void
set_unit_status (ArchiveJob *job,
		 char       *one_archive_form)
{
	char *name;

	if (job->unit_count < 2) {
		nemo_progress_info_take_status (job->progress, one_archive_form);
		return;
	}

	g_free (one_archive_form);

	name = g_file_get_basename (job->destination);
	nemo_progress_info_take_status (job->progress,
					g_strdup_printf (_("Compressing %s (%d of %d)"),
							 name, job->unit_index + 1, job->unit_count));
	g_free (name);
}

static void
prescan_found (gpointer data,
	       guint    files,
	       guint64  bytes)
{
	ArchiveJob *job = data;

	nemo_progress_info_set_prescan_found (job->progress, files, bytes);
	if (prescan_watch != NULL) {
		prescan_watch (job->progress);
	}
}

/* The job's own walk, through the archive core, by the link and filesystem
   choices as the writer can do them. Never the dialog's: that one stopped at
   OK, and left out links onto a share, which OK says to follow. */
static gboolean
prescan (ArchiveJob *job)
{
	GPtrArray *paths = g_ptr_array_new_with_free_func (g_free);
	char *base = g_file_get_path (job->base_dir);
	ArcHost host;
	GList *l;

	nemo_progress_info_set_status (job->progress, _("Preparing to compress"));

	for (l = job->sources; l != NULL; l = l->next) {
		char *path = g_file_get_path (G_FILE (l->data));

		if (path == NULL) {
			break;
		}
		g_ptr_array_add (paths, path);
	}
	g_ptr_array_add (paths, NULL);

	if (base == NULL || l != NULL) {
		job_fail (job, _("The archive could not be created."),
			  _("The selection is not on this computer, so it can't be compressed here."));
	} else {
		nemo_progress_info_start_prescan (job->progress);
		prescan_found (job, 0, 0);
		nemo_archive_host_init (&host);
		job->list = arc_scan_list (&host, base, (const char * const *) paths->pdata,
					   &job->options.links,
					   nemo_archive_backend_link_stores (job->options.format, job->backend),
					   mounts_for_test != NULL ? mounts_for_test () : NULL,
					   job->cancellable, prescan_found, job);
		if (job->list != NULL) {
			prescan_found (job, job->list->files, job->list->bytes);
		}
		nemo_progress_info_end_prescan (job->progress);
	}

	g_ptr_array_unref (paths);
	g_free (base);
	return job->list != NULL && !job_aborted (job);
}

/* Nothing to put in. One archive per item, where the options leave the
   item out, has no archive for that one and goes on with the rest. */
static gboolean
list_has_nothing (ArchiveJob *job)
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

	job_fail (job, _("The archive could not be created."),
		  job->list->left_out->len > 0
		  ? _("The link options leave out everything in the selection.")
		  : _("Nothing in the selection could be read."));
	return TRUE;
}

static gboolean
run_libarchive (ArchiveJob *job)
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
		job_fail (job, _("The archive could not be created."),
			  error != NULL ? error->message : NULL);
		g_clear_error (&error);
		ok = FALSE;
	} else if (archive_write_open2 (a, &sink, NULL, sink_write, sink_close, NULL) != ARCHIVE_OK) {
		job_fail (job, _("The archive could not be created."), archive_error_string (a));
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

		nemo_progress_info_set_details (job->progress, item->name);

		ok = write_item (job, a, item, &error);
		if (!ok) {
			job_fail_from_error (job, error);
			g_clear_error (&error);
		}
	}

	closed = ok;
	if (ok && archive_write_close (a) != ARCHIVE_OK) {
		job_fail (job, _("The archive could not be created."), archive_error_string (a));
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
	if (job_aborted (job) && !closed && job->options.format == NEMO_ARCHIVE_FORMAT_7Z) {
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

/* 7-Zip takes 0,1,3,5,7,9; rar takes 0-5. Both are mapped from the 0-9 the
   dialog offers so the two ends of the scale mean the same thing everywhere. */
static int
seven_zip_level (int level)
{
	static const int steps[] = { 0, 1, 1, 3, 3, 5, 5, 7, 7, 9 };

	return steps[CLAMP (level, 0, NEMO_ARCHIVE_LEVEL_MAX)];
}

static int
rar_level (int level)
{
	static const int steps[] = { 0, 1, 1, 2, 2, 3, 3, 4, 4, 5 };

	return steps[CLAMP (level, 0, NEMO_ARCHIVE_LEVEL_MAX)];
}

static void
free_values (char **values)
{
	int i;

	for (i = 0; values[i] != NULL; i++) {
		g_free (values[i]);
	}
}

/* rar reads @name as a list file to load, both after -x and as a source when
   nothing by that name can be opened, as with a link that leads nowhere. It
   takes ./@name as the name and stores it without the ./. */
static char *
rar_name (const char *prefix, const char *name)
{
	return g_strconcat (prefix, name[0] == '@' ? "./" : "", name, NULL);
}

/* By the program's own name, so a line pointed at another build still counts,
   and one that runs something else is left alone. On Windows PATH can find a
   wrapper such as rar.cmd before the real exe, so any PATHEXT type counts. */
static gboolean
runs_one_of (char              **argv,
	     const char * const *names)
{
	char *base = g_path_get_basename (argv[0]);
	char *dot = strrchr (base, '.');
	gboolean found = FALSE;
	int i;

#ifdef G_OS_WIN32
	if (dot != NULL && dot != base && nemo_name_is_on_pathext (base, g_getenv ("PATHEXT"))) {
		*dot = '\0';
	}
#else
	if (dot != NULL && dot != base && g_ascii_strcasecmp (dot, ".exe") == 0) {
		*dot = '\0';
	}
#endif
	for (i = 0; names[i] != NULL && !found; i++) {
		found = g_ascii_strcasecmp (base, names[i]) == 0;
	}
	g_free (base);

	return found;
}

/* Returns: (transfer full): free with g_strfreev */
char **
nemo_archive_add_run_switches (char     **argv,
			       gboolean   compressing)
{
	static const char * const seven_zip[] = { "7z", "7za", "7zr", "7zz", "7zg", NULL };
	static const char * const rar[] = { "rar", "winrar", NULL };
	const char *add = NULL;
	char **out;
	guint end, len, i;

	if (argv == NULL || argv[0] == NULL) {
		return argv;
	}

	/* Both programs take a switch anywhere before the "--", so the end of
	   the switches is the last place one goes, and the last -r rar sees is
	   the one it takes. */
	end = 1;
	while (argv[end] != NULL && strcmp (argv[end], "--") != 0) {
		end++;
	}

	if (runs_one_of (argv, seven_zip)) {
		add = "-spd";
		for (i = 1; i < end; i++) {
			if (g_ascii_strcasecmp (argv[i], add) == 0) {
				return argv;
			}
		}
	} else if (compressing && runs_one_of (argv, rar)) {
		add = "-r0";
	} else {
		return argv;
	}

	len = g_strv_length (argv);
	out = g_new (char *, len + 2);
	memcpy (out, argv, end * sizeof (char *));
	out[end] = g_strdup (add);
	memcpy (out + end + 1, argv + end, (len - end + 1) * sizeof (char *));
	g_free (argv);

	return out;
}

/* A password and volumes are requirements, so a line edited past either one
   is refused. Run anyway, it would write a readable archive, or one file. */
static gboolean
line_keeps_requirements (const char              *key,
			 const char              *text,
			 const NemoCommandToken  *tokens,
			 GError                 **error)
{
	char **missing = nemo_command_template_unused (text, tokens);
	gboolean ok = TRUE;
	int i;

	for (i = 0; missing != NULL && missing[i] != NULL && ok; i++) {
		if (strcmp (missing[i], "PASSWORD") == 0) {
			g_set_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT,
				     _("The %s command line in the settings has no {{PASSWORD}}, "
				       "so the archive would not be encrypted."), key);
			ok = FALSE;
		} else if (strcmp (missing[i], "SPLIT") == 0) {
			g_set_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT,
				     _("The %s command line in the settings has no {{SPLIT}}, "
				       "so the archive would not be split into volumes."), key);
			ok = FALSE;
		}
	}

	g_strfreev (missing);
	return ok;
}

/* What a program is handed, each word already in its own form: sources for
   {{SOURCE_ITEMS}}, skips with {{LINKS}}, and moved, switches among the
   sources that have to come before a "--". */
typedef struct {
	GPtrArray *sources;
	GPtrArray *skips;
	GPtrArray *moved;
} ToolNames;

static void
tool_names_init (ToolNames *names)
{
	names->sources = g_ptr_array_new_with_free_func (g_free);
	names->skips = g_ptr_array_new_with_free_func (g_free);
	names->moved = g_ptr_array_new_with_free_func (g_free);
}

static void
tool_names_clear (ToolNames *names)
{
	g_clear_pointer (&names->sources, g_ptr_array_unref);
	g_clear_pointer (&names->skips, g_ptr_array_unref);
	g_clear_pointer (&names->moved, g_ptr_array_unref);
}

/* Names on the line, as the job handed them before it wrote lists. */
static void
tool_names_from_lists (ToolNames          *out,
		       NemoArchiveBackend  backend,
		       GList              *names,
		       GList              *leave_out)
{
	GList *l;

	tool_names_init (out);
	for (l = names; l != NULL; l = l->next) {
		g_ptr_array_add (out->sources, backend == NEMO_ARCHIVE_BACKEND_RAR
				 ? rar_name ("", l->data) : g_strdup (l->data));
	}
	for (l = leave_out; l != NULL; l = l->next) {
		g_ptr_array_add (out->skips, backend == NEMO_ARCHIVE_BACKEND_RAR
				 ? rar_name ("-x", l->data) : g_strconcat ("-x!", (char *) l->data, NULL));
	}
}

/* 7-Zip reads @list after a "--" as a file by that name, and a switch there
   is a name too, so ours go ahead of it wherever the line put them. */
static void
move_before_stop (char **argv, GPtrArray *moved)
{
	guint stop = 1, i, j;

	while (argv[stop] != NULL && strcmp (argv[stop], "--") != 0) {
		stop++;
	}
	if (argv[stop] == NULL || moved == NULL) {
		return;
	}

	for (i = stop + 1; argv[i] != NULL; i++) {
		gboolean ours = FALSE;

		for (j = 0; j < moved->len && !ours; j++) {
			ours = strcmp (argv[i], g_ptr_array_index (moved, j)) == 0;
		}
		if (ours) {
			char *word = argv[i];

			memmove (argv + stop + 1, argv + stop, (i - stop) * sizeof (char *));
			argv[stop] = word;
			stop++;
		}
	}
}

/* keep_links has the program keep links as links. links_only is the run
   ahead of the real one, for links alone: it leaves the switches the real
   run adds, or that would stop it adding, to the real run.
   Returns: (transfer full): free with g_strfreev, or NULL with why in error */
static char **
build_command (NemoArchiveBackend        backend,
	       NemoArchiveFormat         format,
	       const NemoArchiveOptions *options,
	       const char               *program,
	       const char               *archive_path,
	       const ToolNames          *names,
	       gboolean                  keep_links,
	       gboolean                  links_only,
	       GError                  **why)
{
	/* One slot per switch a token stands for, plus the terminator. Only the
	   password ever needs two, and only for 7-Zip. */
	char *program_v[2]  = { NULL, NULL };
	char *format_v[2]   = { NULL, NULL };
	char *level_v[2]    = { NULL, NULL };
	char *threads_v[2]  = { NULL, NULL };
	char *password_v[3] = { NULL, NULL, NULL };
	char *split_v[2]    = { NULL, NULL };
	char *solid_v[2]    = { NULL, NULL };
	char *dedupe_v[2]   = { NULL, NULL };
	char *recovery_v[2] = { NULL, NULL };
	char *lock_v[2]     = { NULL, NULL };
	GPtrArray *links_v;
	char *archive_v[2]  = { NULL, NULL };
	GPtrArray *sources;
	const char *key, *fallback;
	GError *error = NULL;
	char **argv;
	char *text;
	guint i;
	gboolean has_password;
	int level;
	int threads;

	g_return_val_if_fail (options != NULL, NULL);
	g_return_val_if_fail (program != NULL, NULL);
	g_return_val_if_fail (archive_path != NULL, NULL);
	g_return_val_if_fail (names != NULL, NULL);

	has_password = options->password != NULL && options->password[0] != '\0';
	links_v = g_ptr_array_new_with_free_func (g_free);
	level = CLAMP (options->level, 0, NEMO_ARCHIVE_LEVEL_MAX);
	threads = nemo_global_preferences_get_cpu_thread_count ();

	if (backend == NEMO_ARCHIVE_BACKEND_7Z) {
		key = NEMO_ARCHIVE_COMMAND_KEY_7Z;
		fallback = NEMO_ARCHIVE_COMMAND_7Z_DEFAULT;

		format_v[0] = g_strdup (format == NEMO_ARCHIVE_FORMAT_7Z ? "-t7z" : "-tzip");
		level_v[0] = g_strdup_printf ("-mx=%d", seven_zip_level (level));
		threads_v[0] = g_strdup_printf ("-mmt=%d", threads);

		if (has_password) {
			/* A password on a command line is readable in the process
			   list; there is no other way to hand it to either tool.
			   It is a value rather than part of the line, so it never
			   reaches the config file. */
			password_v[0] = g_strconcat ("-p", options->password, NULL);

			if (format == NEMO_ARCHIVE_FORMAT_ZIP) {
				password_v[1] = g_strdup ("-mem=AES256");
			} else if (options->encrypt_names) {
				password_v[1] = g_strdup ("-mhe=on");
			}
		}
		if (options->split_size > 0 && !links_only) {
			split_v[0] = g_strdup_printf ("-v%llub", (unsigned long long) options->split_size);
		}
		if (format == NEMO_ARCHIVE_FORMAT_7Z) {
			solid_v[0] = g_strdup (options->solid ? "-ms=on" : "-ms=off");
		}
		if (keep_links &&
		    (nemo_archive_backend_caps (format, backend) & NEMO_ARCHIVE_CAP_STORE_LINKS)) {
			g_ptr_array_add (links_v, g_strdup ("-snl"));
		}
	} else if (backend == NEMO_ARCHIVE_BACKEND_RAR) {
		key = NEMO_ARCHIVE_COMMAND_KEY_RAR;
		fallback = NEMO_ARCHIVE_COMMAND_RAR_DEFAULT;

		level_v[0] = g_strdup_printf ("-m%d", rar_level (level));
		threads_v[0] = g_strdup_printf ("-mt%d", threads);

		if (has_password) {
			password_v[0] = options->encrypt_names
				? g_strconcat ("-hp", options->password, NULL)
				: g_strconcat ("-p", options->password, NULL);
		}
		if (options->split_size > 0 && !links_only) {
			split_v[0] = g_strdup_printf ("-v%llub", (unsigned long long) options->split_size);
		}
		solid_v[0] = g_strdup (options->solid ? "-s" : "-s-");

		if (options->dedupe) {
			dedupe_v[0] = g_strdup ("-oi");
		}
		/* Both for the real run. A locked archive cannot be added to. */
		if (options->recovery_record && !links_only) {
			recovery_v[0] = g_strdup ("-rr3p");
		}
		if (options->lock && !links_only) {
			lock_v[0] = g_strdup ("-k");
		}

		/* -ol keeps links as links. Without it rar follows them, which is
		   what not keeping them means; -ola would keep them too, with the
		   path made absolute, and -ol- would drop them. */
		if (keep_links) {
			g_ptr_array_add (links_v, g_strdup ("-ol"));
		}
		/* With -r, "sub/name" means every "name" under sub. Only a line
		   edited in the settings still has it, and the last one said is
		   the one rar takes. */
		if (links_only) {
			g_ptr_array_add (links_v, g_strdup ("-r-"));
		}
	} else {
		g_ptr_array_free (links_v, TRUE);
		return NULL;
	}

	/* Left alone, both follow every link, folders too. */
	for (i = 0; names->skips != NULL && i < names->skips->len; i++) {
		g_ptr_array_add (links_v, g_strdup (g_ptr_array_index (names->skips, i)));
	}
	g_ptr_array_add (links_v, NULL);
	program_v[0] = g_strdup (program);
	archive_v[0] = g_strdup (archive_path);

	sources = g_ptr_array_new_with_free_func (g_free);
	for (i = 0; names->moved != NULL && i < names->moved->len; i++) {
		g_ptr_array_add (sources, g_strdup (g_ptr_array_index (names->moved, i)));
	}
	for (i = 0; names->sources != NULL && i < names->sources->len; i++) {
		g_ptr_array_add (sources, g_strdup (g_ptr_array_index (names->sources, i)));
	}
	g_ptr_array_add (sources, NULL);

	{
		const NemoCommandToken tokens[] = {
			/* The last column marks the ones that stand for something
			   the Compress dialog was asked for, so a line edited
			   past one of them can say which control went quiet. */
			{ "PROGRAM",        (const char *const *) program_v,     FALSE },
			{ "FORMAT",         (const char *const *) format_v,      TRUE },
			{ "LEVEL",          (const char *const *) level_v,       TRUE },
			{ "THREADS",        (const char *const *) threads_v,     FALSE },
			{ "PASSWORD",       (const char *const *) password_v,    TRUE },
			{ "SPLIT",          (const char *const *) split_v,       TRUE },
			{ "SOLID",          (const char *const *) solid_v,       TRUE },
			{ "DEDUPE",         (const char *const *) dedupe_v,      TRUE },
			{ "RECOVERY",       (const char *const *) recovery_v,    TRUE },
			{ "LOCK",           (const char *const *) lock_v,        TRUE },
			{ "LINKS",          (const char *const *) links_v->pdata, TRUE },
			{ "TARGET_ARCHIVE", (const char *const *) archive_v,     FALSE },
			{ "SOURCE_ITEMS",   (const char *const *) sources->pdata, FALSE },
			{ NULL, NULL, FALSE }
		};

		text = nemo_command_template_from_config (NEMO_ARCHIVE_COMMANDS_GROUP, key, fallback);
		argv = nemo_command_template_expand (text, tokens, &error);

		if (argv == NULL) {
			g_warning ("The %s command line cannot be run as written (%s): %s",
				   key, error->message, text);
			g_set_error (why, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT,
				     _("The %s command line in the settings can't be run as written: %s"),
				     key, error->message);
			g_clear_error (&error);
		} else if (!line_keeps_requirements (key, text, tokens, &error)) {
			g_warning ("%s", error->message);
			g_propagate_error (why, error);
			error = NULL;
			g_strfreev (argv);
			argv = NULL;
		} else {
			nemo_command_template_warn_unused (key, text, tokens);
			move_before_stop (argv, names->moved);
			argv = nemo_archive_add_run_switches (argv, TRUE);
		}
	}

	g_free (text);
	g_ptr_array_free (sources, TRUE);
	free_values (program_v);
	free_values (format_v);
	free_values (level_v);
	free_values (threads_v);
	free_values (password_v);
	free_values (split_v);
	free_values (solid_v);
	free_values (dedupe_v);
	free_values (recovery_v);
	free_values (lock_v);
	g_ptr_array_free (links_v, TRUE);
	free_values (archive_v);

	return argv;
}

/* Returns: (transfer full): free with g_strfreev */
char **
nemo_archive_build_command (NemoArchiveBackend        backend,
			    NemoArchiveFormat         format,
			    const NemoArchiveOptions *options,
			    const char               *program,
			    const char               *archive_path,
			    GList                    *names,
			    GList                    *leave_out)
{
	ToolNames words;
	char **argv;

	tool_names_from_lists (&words, backend, names, leave_out);
	argv = build_command (backend, format, options, program, archive_path, &words,
			      stores_symlinks (options, format, backend), FALSE, NULL);
	tool_names_clear (&words);
	return argv;
}

/* Returns: (transfer full): free with g_strfreev */
char **
nemo_archive_build_links_command (NemoArchiveBackend        backend,
				  NemoArchiveFormat         format,
				  const NemoArchiveOptions *options,
				  const char               *program,
				  const char               *archive_path,
				  GList                    *names)
{
	ToolNames words;
	char **argv;

	tool_names_from_lists (&words, backend, names, NULL);
	argv = build_command (backend, format, options, program, archive_path, &words,
			      TRUE, TRUE, NULL);
	tool_names_clear (&words);
	return argv;
}

static gboolean
names_skipped (const char *path,
	       gsize       length,
	       GList      *skipped)
{
	GList *l;

	for (l = skipped; l != NULL; l = l->next) {
		const char *name = l->data;

		if (strlen (name) == length && strncmp (name, path, length) == 0) {
			return TRUE;
		}
	}

	return FALSE;
}

/* "WARNING: Cannot open 2 files", which both end on under Windows. It names
   nothing, so it says only how many names have to be there. */
static gboolean
open_count (const char *line,
	    guint      *count)
{
	const char *rest;
	char *end;
	guint64 value;

	if (!g_str_has_prefix (line, "WARNING: Cannot open ")) {
		return FALSE;
	}

	rest = line + strlen ("WARNING: Cannot open ");
	if (!g_ascii_isdigit (*rest)) {
		return FALSE;
	}

	value = g_ascii_strtoull (rest, &end, 10);
	if (strcmp (end, " file") != 0 && strcmp (end, " files") != 0) {
		return FALSE;
	}

	*count = (guint) MIN (value, G_MAXUINT);
	return TRUE;
}

gboolean
nemo_archive_only_skipped_links (NemoArchiveBackend  backend,
				 int                 exit_status,
				 const char         *output,
				 GList              *skipped)
{
	char **lines;
	GHashTable *named;	/* the skipped links named, each once */
	gboolean in_list = FALSE;
	gboolean clean = TRUE;
	guint counted = 0;
	guint count;
	gboolean only;
	guint i;

	if (output == NULL || skipped == NULL) {
		return FALSE;
	}
	/* 7-Zip's 1 is any warning at all; rar's 6 is a file it could not open. */
	if (!(backend == NEMO_ARCHIVE_BACKEND_7Z && exit_status == 1) &&
	    !(backend == NEMO_ARCHIVE_BACKEND_RAR && exit_status == 6)) {
		return FALSE;
	}

	lines = g_strsplit (output, "\n", -1);
	for (i = 0; lines[i] != NULL; i++) {
		g_strstrip (lines[i]);
#ifdef G_OS_WIN32
		g_strdelimit (lines[i], "\\", '/');
#endif
	}

	named = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);

	for (i = 0; clean && lines[i] != NULL; i++) {
		const char *line = lines[i];

		if (open_count (line, &count)) {
			counted = MAX (counted, count);
			continue;
		}

		if (backend == NEMO_ARCHIVE_BACKEND_RAR) {
			/* "Cannot open <name>", then the reason on a line of its own. */
			if (g_str_has_prefix (line, "Cannot open ")) {
				const char *name = line + strlen ("Cannot open ");

				/* Said as it was handed over, see rar_name. */
				if (g_str_has_prefix (name, "./@")) {
					name += 2;
				}
				clean = names_skipped (name, strlen (name), skipped);
				g_hash_table_add (named, g_strdup (name));
			} else if (g_str_has_prefix (line, "Cannot ") ||
				   g_str_has_prefix (line, "WARNING") ||
				   g_str_has_prefix (line, "ERROR")) {
				clean = FALSE;
			}
			continue;
		}

		/* 7-Zip lists them again at the end, "<name> : <reason>", under
		   one heading for the scan and another for files it could not
		   read while writing. Every one has to be a link we know of. */
		if (g_str_has_suffix (line, "WARNINGS for files and folders:") ||
		    g_str_has_suffix (line, "WARNINGS for files:")) {
			in_list = TRUE;
		} else if (in_list) {
			if (g_str_has_prefix (line, "----")) {
				in_list = FALSE;
			} else if (line[0] != '\0') {
				/* A name can hold " : " itself, so try each one. */
				const char *separator = strstr (line, " : ");

				while (separator != NULL &&
				       !names_skipped (line, (gsize) (separator - line), skipped)) {
					separator = strstr (separator + 1, " : ");
				}
				clean = separator != NULL;
				if (clean) {
					g_hash_table_add (named, g_strndup (line, (gsize) (separator - line)));
				}
			}
		} else if (g_str_has_prefix (line, "WARNING")) {
			/* As it happens: "WARNING: <reason>", and the name next. */
			const char *name = lines[i + 1];

			clean = name != NULL && names_skipped (name, strlen (name), skipped);
			if (clean) {
				g_hash_table_add (named, g_strdup (name));
			}
			i++;
		} else if (g_str_has_prefix (line, "ERROR")) {
			clean = FALSE;
		}
	}

	only = clean && g_hash_table_size (named) > 0 && counted <= g_hash_table_size (named);

	g_hash_table_destroy (named);
	g_strfreev (lines);

	return only;
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

/* Neither tool writes the name it was given once splitting is on. 7z appends
   ".001" to it; rar drops the extension and puts "partN" in front of it, with
   as many digits as it feels like. Both number the only volume too, which is
   what nemo_archive_collapse_volume undoes below.
   Returns: (transfer full): free with g_free */
char *
nemo_archive_volume_name (const char         *archive_name,
			  NemoArchiveBackend  backend,
			  guint               volume,
			  guint               digits)
{
	const char *dot;
	char *stem;
	char *name;

	g_return_val_if_fail (archive_name != NULL, NULL);

	if (backend != NEMO_ARCHIVE_BACKEND_RAR) {
		return g_strdup_printf ("%s.%0*u", archive_name, (int) digits, volume);
	}

	dot = strrchr (archive_name, '.');
	if (dot == NULL || dot == archive_name) {
		return g_strdup_printf ("%s.part%0*u", archive_name, (int) digits, volume);
	}

	stem = g_strndup (archive_name, (gsize) (dot - archive_name));
	name = g_strdup_printf ("%s.part%0*u%s", stem, (int) digits, volume, dot);
	g_free (stem);

	return name;
}

static GFile *
volume_file (GFile              *destination,
	     NemoArchiveBackend  backend,
	     guint               volume,
	     guint               digits)
{
	GFile *parent = g_file_get_parent (destination);
	char *base;
	char *name;
	GFile *file;

	if (parent == NULL) {
		return NULL;
	}

	base = g_file_get_basename (destination);
	name = nemo_archive_volume_name (base, backend, volume, digits);
	file = g_file_get_child (parent, name);

	g_free (base);
	g_free (name);
	g_object_unref (parent);

	return file;
}

/* Renames a lone volume back to the name that was asked for. The digit count
   is whatever the first volume turned out to be written with, since rar picks
   it and 7z always uses three. Answers the file the result is in either way.
   Returns: (transfer full): unref with g_object_unref */
GFile *
nemo_archive_collapse_volume (GFile              *destination,
			      NemoArchiveBackend  backend)
{
	guint digits;

	for (digits = 1; digits <= 3; digits++) {
		GFile *first = volume_file (destination, backend, 1, digits);
		GFile *second;

		if (first == NULL) {
			return g_object_ref (destination);
		}

		if (!g_file_query_exists (first, NULL)) {
			g_object_unref (first);
			continue;
		}

		second = volume_file (destination, backend, 2, digits);
		if (second != NULL && g_file_query_exists (second, NULL)) {
			/* A real split. The first volume is where unpacking starts. */
			g_object_unref (second);
			return first;
		}
		g_clear_object (&second);

		/* No overwrite flag: run_command clears the name before it starts,
		   and the tool wrote the volume instead of it. */
		if (g_file_move (first, destination, G_FILE_COPY_NONE,
				 NULL, NULL, NULL, NULL)) {
			g_object_unref (first);
			return g_object_ref (destination);
		}

		/* The rename is a tidy-up, not the job. Leaving the volume named
		   as the tool wrote it beats failing an archive that is fine. */
		return first;
	}

	return g_object_ref (destination);
}

/* 7z keeps the first volume, and the one it is writing, as "<volume>.tmp" until
   the whole run is done, so that is the name a stopped run leaves them under. */
static GFile *
unfinished_volume (GFile *file)
{
	GFile *parent = g_file_get_parent (file);
	char *name = g_file_get_basename (file);
	char *tmp_name = g_strconcat (name, ".tmp", NULL);
	GFile *tmp = g_file_get_child (parent, tmp_name);

	g_free (tmp_name);
	g_free (name);
	g_object_unref (parent);

	return tmp;
}

/* Walks the volumes a split actually left on disk, stopping at the first gap in
   each digit width, since only one width was ever used. */
static void
for_each_volume (GFile              *destination,
		 NemoArchiveBackend  backend,
		 void              (*action) (GFile *))
{
	guint digits;

	for (digits = 1; digits <= 3; digits++) {
		guint volume;

		for (volume = 1; volume < 1000; volume++) {
			GFile *file = volume_file (destination, backend, volume, digits);
			GFile *unfinished;
			gboolean there;

			if (file == NULL) {
				return;
			}

			there = g_file_query_exists (file, NULL);
			if (there) {
				action (file);
			}

			unfinished = unfinished_volume (file);
			if (g_file_query_exists (unfinished, NULL)) {
				action (unfinished);
				there = TRUE;
			}
			g_object_unref (unfinished);
			g_object_unref (file);

			if (!there) {
				break;
			}
		}
	}
}

static void
delete_one (GFile *file)
{
	g_file_delete (file, NULL, NULL);
}

static void
queue_one (GFile *file)
{
	nemo_file_changes_queue_file_added (file);
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
archive_written (ArchiveJob *job)
{
	guint digits;

	if (g_file_query_exists (job->destination, NULL)) {
		return TRUE;
	}
	if (job->options.split_size == 0) {
		return FALSE;
	}

	for (digits = 1; digits <= 3; digits++) {
		GFile *first = volume_file (job->destination, job->backend, 1, digits);
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
only_known_warned (ArchiveJob  *job,
		   NemoToolRun *process,
		   GString     *output,
		   GList       *known)
{
	if (known == NULL || output_full (output) || !nemo_tool_run_get_if_exited (process)) {
		return FALSE;
	}

	return nemo_archive_only_skipped_links (job->backend,
						nemo_tool_run_get_exit_status (process),
						output->str, known) &&
	       archive_written (job);
}

GSubprocessFlags
nemo_archive_tool_stderr_flag (void)
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

/* Returns: (transfer full): hand it to nemo_archive_tool_stderr_finish, which joins it */
GThread *
nemo_archive_tool_stderr_start (NemoToolRun *process)
{
	GInputStream *in = nemo_tool_run_get_stderr (process);

	if (in == NULL) {
		return NULL;
	}

	return g_thread_new ("tool-stderr", read_stderr, g_object_ref (in));
}

/* Returns: (transfer full): free with g_free */
char *
nemo_archive_tool_stderr_finish (GThread *reader)
{
	return reader != NULL ? g_thread_join (reader) : NULL;
}

/* Starts a tool line in the base folder and reads what it prints until it
   ends, moving the progress bar as it goes. The caller waits on what comes
   back. NULL if it could not be started. */
static NemoToolRun *
start_tool (ArchiveJob  *job,
	    char       **argv,
	    const char  *base_path,
	    GString     *output,
	    GError     **error)
{
	NemoToolRun *process;
	GInputStream *out;
	GThread *err_reader;
	char *err_text;
	char buffer[4096];

	process = nemo_tool_run_start ((const gchar * const *) argv, base_path,
				       G_SUBPROCESS_FLAGS_STDOUT_PIPE | nemo_archive_tool_stderr_flag (),
				       error);
	if (process == NULL) {
		return NULL;
	}

	out = nemo_tool_run_get_stdout (process);
	err_reader = nemo_archive_tool_stderr_start (process);

	while (TRUE) {
		gssize count = g_input_stream_read (out, buffer, sizeof (buffer),
						    job->cancellable, NULL);
		int percent = -1;

		if (count <= 0) {
			break;
		}

		take_output (output, buffer, (gsize) count);

		if (job->bar_pulses) {
			nemo_progress_info_pulse_progress (job->progress);
		} else if (scan_percent (buffer, (gsize) count, &percent)) {
			nemo_progress_info_set_progress (job->progress, percent, 100);
		}
	}

	if (job_aborted (job)) {
		nemo_tool_run_force_exit (process);
	}

	err_text = nemo_archive_tool_stderr_finish (err_reader);
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

/* One name to a line, UTF-8. Both programs take a list file, so a selection
   of any size stays under the command line's limit.
   Returns: (transfer full): the file's path, or NULL with error set */
static char *
write_list (GPtrArray  *names,
	    GError    **error)
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
			g_unlink (path);
			g_clear_pointer (&path, g_free);
		}
	}

	g_string_free (text, TRUE);
	return path;
}

/* Names that fit go in a list file, the rest on the line. lists gets the
   files made, for the caller to remove. */
static gboolean
hand_names (NemoArchiveBackend   backend,
	    GPtrArray           *roots,
	    GPtrArray           *skips,
	    ToolNames           *out,
	    GPtrArray           *lists,
	    GError             **error)
{
	gboolean seven = backend == NEMO_ARCHIVE_BACKEND_7Z;
	GPtrArray *listed = g_ptr_array_new ();
	gboolean charset = FALSE;
	gboolean ok = TRUE;
	guint pass, i;

	tool_names_init (out);

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
				g_ptr_array_add (out->sources, seven ? g_strdup (word) : rar_name ("", word));
			} else {
				g_ptr_array_add (out->skips, seven ? g_strconcat ("-x!", word, NULL)
								   : rar_name ("-x", word));
			}
			g_free (word);
		}
		if (listed->len == 0) {
			continue;
		}

		path = write_list (listed, error);
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
plan_runs (ArchiveJob *job,
	   RunPlan    *plan)
{
	const ArcScanList *list = job->list;
	const GPtrArray *not_taken[3] = { list->left_out, list->unreadable, list->again };
	gboolean can_keep = (nemo_archive_backend_caps (job->options.format, job->backend) &
			     NEMO_ARCHIVE_CAP_STORE_LINKS) != 0;
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
		odd = job->backend == NEMO_ARCHIVE_BACKEND_RAR && item->nowhere &&
		      strpbrk (item->name, "*?") != NULL;
		if (strchr (item->name, '/') != NULL && !odd) {
			g_ptr_array_add (plan->skips, item->name);
		}
		if (!can_keep || job->options.split_size > 0 || odd ||
		    (job->backend == NEMO_ARCHIVE_BACKEND_7Z &&
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
run_tool (ArchiveJob  *job,
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
	NemoToolRun *process = NULL;
	ToolNames words = { NULL, NULL, NULL };
	char **argv = NULL;
	gboolean ok = FALSE;
	guint i;

	if (hand_names (job->backend, roots, skips, &words, lists, error)) {
		argv = build_command (job->backend, job->options.format, &job->options,
				      program, archive_path, &words, keep_links, links_only, error);
	}
	if (argv != NULL) {
		process = start_tool (job, argv, base_path, output, error);
	}
	if (process != NULL) {
		ok = nemo_tool_run_wait_check (process, error);
		if (!ok && !job_aborted (job) && only_known_warned (job, process, output, known)) {
			g_clear_error (error);
			ok = TRUE;
		}
		nemo_tool_run_free (process);
	}

	/* Ours, made above. */
	for (i = 0; i < lists->len; i++) {
		g_unlink (g_ptr_array_index (lists, i));
	}
	g_ptr_array_unref (lists);
	tool_names_clear (&words);
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
run_command (ArchiveJob *job)
{
	const char *program = backend_program (job->backend);
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
		job_fail (job, _("The archive could not be created."),
			  _("This format needs a program that is not installed, or a folder on this computer."));
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

	pattern = job->backend == NEMO_ARCHIVE_BACKEND_RAR ? first_pattern (&plan) : NULL;
	if (pattern != NULL) {
		char *details = g_strdup_printf (
			_("\"%s\" has * or ? in its name, which rar reads as a wildcard, so other files could go in or be left out with it. Choose another format, or rename it."),
			pattern);

		job_fail (job, _("The archive could not be created."), details);
		g_free (details);
		goto out;
	}

	/* Both tools ADD to an archive that is already there rather than
	   replacing it, so an overwrite has to be a delete first. */
	if (g_file_query_exists (job->destination, NULL)) {
		g_file_delete (job->destination, NULL, NULL);
	}

	set_unit_status (job, g_strdup_printf (_("Compressing with %s"),
					       job->backend == NEMO_ARCHIVE_BACKEND_RAR ? "rar" : "7z"));

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

			g_warning ("Links could not be kept, so they are left out: %s", said);
			g_free (said);
			g_file_delete (job->destination, NULL, NULL);
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
			job_fail (job, _("The archive could not be created."),
				  _("Nothing in the selection could go in."));
		}
		g_string_free (output, TRUE);
		goto out;
	}

	known = names_to_list (known, plan.skips);
	known = g_list_concat (g_list_copy (job->passed), known);
	ok = run_tool (job, program, base_path, archive_path, plan.roots, plan.skips,
		       plan.keep, FALSE, known, output, &error);
	if (!ok && !job_aborted (job)) {
		char *said = output_tail (output);

		/* rar can end with nothing but blank lines, which says nothing. */
		job_fail (job, _("The archive could not be created."),
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
verify_unit (ArchiveJob *job)
{
	char *trouble = NULL;
	GList *l;

	nemo_progress_info_set_status (job->progress, _("Checking the archive"));

	if (nemo_archive_verify (job->destination, job->sources, &job->options,
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

static gboolean
archive_job_done (gpointer user_data)
{
	ArchiveJob *job = user_data;
	char *note;

	nemo_file_changes_consume_changes (TRUE);

	if (job->error_message != NULL) {
		eel_show_error_dialog (job->error_message, job->error_details, job->parent_window);
	}

	/* Only a delete sets verify_trouble. One dialog, not two, when the
	   originals stayed because of the links. */
	note = passed_over_note (job->passed_over);
	if (job->verify_trouble != NULL) {
		char *details = note != NULL
			? g_strconcat (job->verify_trouble, "\n\n", note, NULL)
			: g_strdup (job->verify_trouble);

		eel_show_warning_dialog (_("The original files were kept."),
					 details, job->parent_window);
		g_free (details);
	} else if (job->success && note != NULL) {
		eel_show_warning_dialog (ngettext ("A link was left out of the archive.",
						   "Some links were left out of the archive.",
						   g_list_length (job->passed_over)),
					 note, job->parent_window);
	}
	g_free (note);

	/* The box was ticked in the Compress dialog, so this is a delete a person
	   asked for, and it goes through the ordinary trash-or-delete with its own
	   confirmation rather than anything quieter here. A cancelled job removes
	   nothing, even where an earlier archive of it did check out. */
	if (job->options.delete_sources) {
		if (job->verified != NULL && !job_aborted (job)) {
			job->verified = g_list_reverse (job->verified);
			nemo_file_operations_trash_or_delete_by_user (job->verified,
								      job->parent_window,
								      NULL, NULL);
		}
	}

	if (job->done_callback != NULL) {
		job->done_callback (job->success ? job->result_file : NULL,
				    job->success, job->done_callback_data);
	}

	nemo_progress_info_finish (job->progress);

	if (job->parent_window != NULL) {
		g_object_remove_weak_pointer (G_OBJECT (job->parent_window),
					      (gpointer *) &job->parent_window);
	}

	g_clear_pointer (&job->list, arc_scan_list_free);
	g_list_free_full (job->passed, g_free);
	g_list_free_full (job->units, (GDestroyNotify) archive_unit_free);
	g_list_free_full (job->verified, g_object_unref);
	g_list_free_full (job->passed_over, g_free);
	g_free (job->verify_trouble);
	g_clear_object (&job->result_file);
	g_clear_object (&job->base_dir);
	g_clear_object (&job->progress);
	g_clear_object (&job->cancellable);
	nemo_archive_options_clear (&job->options);
	g_free (job->error_message);
	g_free (job->error_details);
	g_free (job);

	return FALSE;
}

/* Per-archive state, taken up and put down around each unit so nothing from
   one archive is still standing when the next starts. */
static void
start_unit (ArchiveJob  *job,
	    ArchiveUnit *unit)
{
	job->sources = unit->sources;
	job->destination = unit->destination;
	job->base_dir = unit->sources != NULL ?
		g_file_get_parent (G_FILE (unit->sources->data)) : NULL;

	job->total_bytes = 0;
	job->done_bytes = 0;
	job->file_count = 0;
	/* Not the last archive's full bar while this one's pre-scan runs. */
	nemo_progress_info_set_progress (job->progress, 0, 1);

	nemo_progress_info_take_details (job->progress,
					 g_file_get_basename (unit->destination));
}

static void
finish_unit (ArchiveJob *job)
{
	g_clear_pointer (&job->list, arc_scan_list_free);
	job->passed_over = g_list_concat (job->passed_over, g_list_reverse (job->passed));
	job->passed = NULL;
	job->unit_empty = FALSE;

	g_clear_object (&job->base_dir);
	job->sources = NULL;
	job->destination = NULL;
}

static gboolean
archive_job (GIOSchedulerJob *io_job,
	     GCancellable    *cancellable,
	     gpointer         user_data)
{
	ArchiveJob *job = user_data;
	GList *l;

	(void) cancellable;

	job->io_job = io_job;
	job->success = TRUE;
	nemo_progress_info_start (job->progress);

	for (l = job->units; l != NULL && !job_aborted (job); l = l->next) {
		gboolean ok;

		start_unit (job, l->data);

		if (job->base_dir == NULL) {
			job_fail (job, _("The archive could not be created."),
				  _("The selection has no folder above it to compress from."));
			ok = FALSE;
		} else if (job->backend == NEMO_ARCHIVE_BACKEND_LIBARCHIVE) {
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
			g_file_delete (job->destination, NULL, NULL);
			if (job->options.split_size > 0) {
				for_each_volume (job->destination, job->backend, delete_one);
			}
		} else if (job->options.split_size > 0) {
			GFile *written = nemo_archive_collapse_volume (job->destination,
								       job->backend);

			nemo_file_changes_queue_file_added (written);
			for_each_volume (job->destination, job->backend, queue_one);

			/* One unit means the callback's file is this one, and a
			   collapsed volume is under a different name than asked. */
			if (job->units->next == NULL) {
				g_set_object (&job->result_file, written);
			}
			g_object_unref (written);
		} else {
			nemo_file_changes_queue_file_added (job->destination);
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

	g_io_scheduler_job_send_to_mainloop_async (io_job, archive_job_done, job, NULL);

	return FALSE;
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

/* Everything the two entry points share: the progress window, the backend that
   writes every archive in the job, and the queueing. Takes the units. */
static void
start_job (ArchiveJob               *job,
	   GList                    *units,
	   GFile                    *result_file,
	   const NemoArchiveOptions *options,
	   GtkWindow                *parent_window,
	   NemoArchiveCallback       done_callback,
	   gpointer                  done_callback_data)
{
	char *initial_details;

	job->units = units;
	job->unit_count = g_list_length (units);
	job->result_file = g_object_ref (result_file);

	nemo_archive_options_copy (options, &job->options);
	job->backend = nemo_archive_pick_backend (options->format, options);

	job->done_callback = done_callback;
	job->done_callback_data = done_callback_data;

	job->parent_window = parent_window;
	if (parent_window != NULL) {
		g_object_add_weak_pointer (G_OBJECT (parent_window), (gpointer *) &job->parent_window);
	}

	job->progress = nemo_progress_info_new ();
	/* Comes with a ref, which archive_job_done drops. */
	job->cancellable = nemo_progress_info_get_cancellable (job->progress);

	nemo_progress_info_set_status (job->progress, _("Preparing to compress"));

	if (job->unit_count > 1) {
		initial_details = g_strdup_printf (ngettext ("%d archive", "%d archives",
							     job->unit_count),
						   job->unit_count);
	} else {
		initial_details = g_file_get_basename (((ArchiveUnit *) units->data)->destination);
	}
	nemo_progress_info_take_initial_details (job->progress, initial_details);

	if (job->backend == NEMO_ARCHIVE_BACKEND_NONE) {
		job_fail (job, _("The archive could not be created."),
			  _("Nothing installed here can write this archive with the options chosen."));
		archive_job_done (job);
		return;
	}

	nemo_job_queue_add_new_job (nemo_job_queue_get (), archive_job, job,
				    job->cancellable, job->progress, TRUE);
}

void
nemo_archive_create (GList                    *sources,
		     GFile                    *destination,
		     const NemoArchiveOptions *options,
		     GtkWindow                *parent_window,
		     NemoArchiveCallback       done_callback,
		     gpointer                  done_callback_data)
{
	GList *units;

	g_return_if_fail (sources != NULL);
	g_return_if_fail (G_IS_FILE (destination));
	g_return_if_fail (options != NULL);

	units = g_list_append (NULL, archive_unit_new (sources, destination));

	start_job (g_new0 (ArchiveJob, 1), units, destination, options,
		   parent_window, done_callback, done_callback_data);
}

void
nemo_archive_create_each (GList                    *sources,
			  GFile                    *destination_dir,
			  const NemoArchiveOptions *options,
			  GtkWindow                *parent_window,
			  NemoArchiveCallback       done_callback,
			  gpointer                  done_callback_data)
{
	GList *units = NULL;
	GList *l;

	g_return_if_fail (sources != NULL);
	g_return_if_fail (G_IS_FILE (destination_dir));
	g_return_if_fail (options != NULL);

	for (l = sources; l != NULL; l = l->next) {
		GFile *source = G_FILE (l->data);
		GList *one;
		GFile *destination;
		char *basename;
		char *name;

		basename = g_file_get_basename (source);
		name = nemo_archive_each_name (basename, options->format);
		g_free (basename);

		/* Nothing worth naming an archive after - a root, say. */
		if (name == NULL) {
			continue;
		}

		destination = g_file_get_child_for_display_name (destination_dir, name, NULL);
		if (destination == NULL) {
			destination = g_file_get_child (destination_dir, name);
		}
		g_free (name);

		one = g_list_append (NULL, source);
		units = g_list_append (units, archive_unit_new (one, destination));
		g_list_free (one);
		g_object_unref (destination);
	}

	if (units == NULL) {
		return;
	}

	start_job (g_new0 (ArchiveJob, 1), units, destination_dir, options,
		   parent_window, done_callback, done_callback_data);
}
