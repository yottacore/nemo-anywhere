/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */

/* nemo-archive.h - create an archive from a selection.

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

/* Two ways to write an archive, picked per request. libarchive is linked in and
 * handles the tar, zip and 7z families; it cannot write rar at all, and has no
 * write support for split volumes, solid blocks, duplicate-as-reference or 7z
 * encryption. Those go out to a 7z or rar command when the box has one. What a
 * given format can offer is therefore the union over the backends actually
 * present, which is why the dialog asks rather than assuming.
 */

#ifndef NEMO_ARCHIVE_H
#define NEMO_ARCHIVE_H

#include <gtk/gtk.h>
#include <gio/gio.h>

typedef enum {
	NEMO_ARCHIVE_FORMAT_ZIP,
	NEMO_ARCHIVE_FORMAT_TAR,
	NEMO_ARCHIVE_FORMAT_TAR_GZ,
	NEMO_ARCHIVE_FORMAT_TAR_XZ,
	NEMO_ARCHIVE_FORMAT_7Z,
	NEMO_ARCHIVE_FORMAT_RAR,
	NEMO_ARCHIVE_N_FORMATS
} NemoArchiveFormat;

/* Which program would write it. NONE means nothing installed here can. */
typedef enum {
	NEMO_ARCHIVE_BACKEND_NONE,
	NEMO_ARCHIVE_BACKEND_LIBARCHIVE,
	NEMO_ARCHIVE_BACKEND_7Z,
	NEMO_ARCHIVE_BACKEND_RAR
} NemoArchiveBackend;

typedef enum {
	NEMO_ARCHIVE_CAP_LEVEL         = 1 << 0,	/* compression level is meaningful */
	NEMO_ARCHIVE_CAP_PASSWORD      = 1 << 1,
	NEMO_ARCHIVE_CAP_ENCRYPT_NAMES = 1 << 2,	/* the file list is encrypted too */
	NEMO_ARCHIVE_CAP_SPLIT         = 1 << 3,	/* multi-volume */
	NEMO_ARCHIVE_CAP_SOLID         = 1 << 4,
	NEMO_ARCHIVE_CAP_DEDUPE        = 1 << 5,	/* identical files stored once */
	NEMO_ARCHIVE_CAP_STORE_LINKS   = 1 << 6,	/* symlinks/junctions kept as links */
	NEMO_ARCHIVE_CAP_RECOVERY      = 1 << 7,	/* recovery record */
	NEMO_ARCHIVE_CAP_LOCK          = 1 << 8	/* refuse later modification */
} NemoArchiveCaps;

/* Levels are 0-9 as the user sees them; each backend maps that onto its own
 * scale. 0 means store without compressing. */
#define NEMO_ARCHIVE_LEVEL_STORE   0
#define NEMO_ARCHIVE_LEVEL_DEFAULT 5
#define NEMO_ARCHIVE_LEVEL_MAX     9

typedef struct {
	NemoArchiveFormat format;
	gint              level;
	char             *password;		/* NULL or empty means none */
	gboolean          encrypt_names;
	guint64           split_size;		/* bytes per volume; 0 means one file */
	gboolean          solid;
	gboolean          dedupe;
	gboolean          store_links;
	gboolean          follow_link_dirs;	/* descend into a linked folder */
	gboolean          recovery_record;
	gboolean          lock;
	gboolean          delete_sources;	/* only once the archive verifies */
} NemoArchiveOptions;

typedef void (* NemoArchiveCallback) (GFile    *archive_file,
				      gboolean  success,
				      gpointer  callback_data);

void     nemo_archive_options_init  (NemoArchiveOptions *options);
void     nemo_archive_options_clear (NemoArchiveOptions *options);
void     nemo_archive_options_copy  (const NemoArchiveOptions *source,
				     NemoArchiveOptions       *dest);

const char        *nemo_archive_format_id        (NemoArchiveFormat format);
const char        *nemo_archive_format_name      (NemoArchiveFormat format);
const char        *nemo_archive_format_extension (NemoArchiveFormat format);
gboolean           nemo_archive_format_from_id   (const char        *id,
						  NemoArchiveFormat *format);

/* What this box can actually do: the union over the backends installed. */
gboolean           nemo_archive_format_available (NemoArchiveFormat format);
NemoArchiveCaps    nemo_archive_format_caps      (NemoArchiveFormat format);

/* Looks a command up on PATH, and on win32 also under Program Files, since
 * neither archiver's installer puts itself on PATH. Shared with the unpacking
 * side, which goes looking for the same two programs under other names. */
char    *nemo_archive_find_command (const char * const *names,
				    const char         *win_subdir);

/* Pure, so it can be reasoned about (and tested) without probing the box. */
NemoArchiveCaps    nemo_archive_backend_caps     (NemoArchiveFormat  format,
						  NemoArchiveBackend backend);
gboolean           nemo_archive_backend_present  (NemoArchiveBackend backend);
NemoArchiveBackend nemo_archive_pick_backend     (NemoArchiveFormat         format,
						  const NemoArchiveOptions *options);

/* Name handling for the dialog: the extension follows the format, and a
 * selection suggests a name - or, when the selection is part of a folder rather
 * than one item or all of it, no name at all, which leaves the field for the
 * user to fill in. whole_folder says the selection is everything the folder is
 * showing. */
char    *nemo_archive_strip_extension (const char *name);
char    *nemo_archive_apply_extension (const char       *name,
				       NemoArchiveFormat format);
char    *nemo_archive_suggest_name    (GList            *files,
				       gboolean          whole_folder,
				       NemoArchiveFormat format);

/* What one item's archive is called when a selection is compressed separately.
   NULL for a name nothing can be built from, such as a drive root. */
char    *nemo_archive_each_name       (const char       *item_name,
				       NemoArchiveFormat format);

/* The command line a 7z or rar backend would be run with, in the base folder
   holding the selection. leave_out holds paths relative to that folder, the
   linked folders that are not to be followed. Exposed so the switches can be
   checked without spawning anything. */
char   **nemo_archive_build_command (NemoArchiveBackend        backend,
				     NemoArchiveFormat         format,
				     const NemoArchiveOptions *options,
				     const char               *program,
				     const char               *archive_path,
				     GList                    *names,
				     GList                    *leave_out);

/* The run ahead of that one, which puts in the links named in names, as
   links, while the real run follows links. For the links that lead nowhere. */
char   **nemo_archive_build_links_command (NemoArchiveBackend        backend,
					   NemoArchiveFormat         format,
					   const NemoArchiveOptions *options,
					   const char               *program,
					   const char               *archive_path,
					   GList                    *names);

/* Whether a 7z or rar run that ended on a warning status warned about nothing
   but the links in skipped, which lead nowhere and so had nothing to put in.
   output is what the tool printed, with its backspaces already applied;
   skipped holds paths relative to the base folder. */
gboolean nemo_archive_only_skipped_links (NemoArchiveBackend  backend,
					  int                 exit_status,
					  const char         *output,
					  GList              *skipped);

/* "700 MB", "4480m", "1.5 GB" -> bytes. Returns FALSE on anything unreadable. */
/* What the tool actually writes when splitting is on. `digits` is how wide the
   number is: 7z always uses 3, rar picks its own. */
char    *nemo_archive_volume_name (const char         *archive_name,
				   NemoArchiveBackend  backend,
				   guint               volume,
				   guint               digits);

/* Renames a split that came out as one volume back to the name asked for.
   Returns the file the archive is in, which the caller owns. */
GFile   *nemo_archive_collapse_volume (GFile              *destination,
				       NemoArchiveBackend  backend);

gboolean nemo_archive_parse_size (const char *text,
				  guint64    *bytes);
char    *nemo_archive_format_size (guint64 bytes);

/* How tall the Compress dialog's options may grow before they scroll: what
   the work area leaves under the closed dialog (frame included), and never
   less than a strip that still shows a few rows. */
int      nemo_archive_options_room (int work_height,
				    int closed_height);

/* Whether an archive written with these options can be read back at all, and
   so whether deleting the originals may be offered for it. A split set and an
   archive with its names encrypted both answer FALSE. */
gboolean nemo_archive_can_verify (const NemoArchiveOptions *options);
gboolean nemo_archive_should_confirm_password (const NemoArchiveOptions *options);

/* Walks the sources again and reads the archive back, so that nothing may be
   deleted on the strength of the writer saying it went well. Answers TRUE only
   when every file that should have gone in is in there under the same relative
   path and the same size, and nothing was left out along the way. On FALSE,
   reason is set to a sentence naming what did not line up; the caller frees it.
   The walk is deliberately its own, rather than the one the job wrote from -
   checking with the same code that did the work proves very little. */
gboolean nemo_archive_verify (GFile                    *archive_file,
			      GList                    *sources,
			      const NemoArchiveOptions *options,
			      NemoArchiveBackend        backend,
			      GCancellable             *cancellable,
			      char                    **reason);

/* Queues the job. sources are GFile *; destination is the archive itself. */
void nemo_archive_create (GList                    *sources,
			  GFile                    *destination,
			  const NemoArchiveOptions *options,
			  GtkWindow                *parent_window,
			  NemoArchiveCallback       done_callback,
			  gpointer                  done_callback_data);

/* One archive per source instead, each named after its item and all written
   into destination_dir, under one progress window and one cancel. The callback
   is handed that folder rather than any one archive. */
void nemo_archive_create_each (GList                    *sources,
			       GFile                    *destination_dir,
			       const NemoArchiveOptions *options,
			       GtkWindow                *parent_window,
			       NemoArchiveCallback       done_callback,
			       gpointer                  done_callback_data);

#endif /* NEMO_ARCHIVE_H */
