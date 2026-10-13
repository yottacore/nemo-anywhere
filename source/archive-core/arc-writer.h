/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */

/* arc-writer.h - which writer an archive goes to, and what each can do.

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

/* Two ways to write an archive, picked per request. libarchive is linked in and
 * handles the tar, zip and 7z families; it cannot write rar at all, and has no
 * write support for split volumes, solid blocks, duplicate-as-reference or 7z
 * encryption. Those go out to a 7z or rar command when the box has one. What a
 * given format can offer is therefore the union over the backends actually
 * present, which is why the dialog asks rather than assuming.
 */

#ifndef ARC_WRITER_H
#define ARC_WRITER_H

#include <gio/gio.h>

#include "arc-link-options.h"

G_BEGIN_DECLS

typedef enum {
	ARC_FORMAT_ZIP,
	ARC_FORMAT_TAR,
	ARC_FORMAT_TAR_GZ,
	ARC_FORMAT_TAR_XZ,
	ARC_FORMAT_7Z,
	ARC_FORMAT_RAR,
	ARC_N_FORMATS
} ArcFormat;

/* Which program would write it. NONE means nothing installed here can. */
typedef enum {
	ARC_BACKEND_NONE,
	ARC_BACKEND_LIBARCHIVE,
	ARC_BACKEND_7Z,
	ARC_BACKEND_RAR
} ArcBackend;

typedef enum {
	ARC_CAP_LEVEL         = 1 << 0,	/* compression level is meaningful */
	ARC_CAP_PASSWORD      = 1 << 1,
	ARC_CAP_ENCRYPT_NAMES = 1 << 2,	/* the file list is encrypted too */
	ARC_CAP_SPLIT         = 1 << 3,	/* multi-volume */
	ARC_CAP_SOLID         = 1 << 4,
	ARC_CAP_DEDUPE        = 1 << 5,	/* identical files stored once */
	ARC_CAP_STORE_LINKS   = 1 << 6,	/* symlinks/junctions kept as links */
	ARC_CAP_RECOVERY      = 1 << 7,	/* recovery record */
	ARC_CAP_LOCK          = 1 << 8	/* refuse later modification */
} ArcCaps;

/* Levels are 0-9 as the user sees them; each backend maps that onto its own
 * scale. 0 means store without compressing. */
#define ARC_LEVEL_STORE   0
#define ARC_LEVEL_DEFAULT 5
#define ARC_LEVEL_MAX     9

typedef struct {
	ArcFormat       format;
	gint            level;
	char           *password;		/* NULL or empty means none */
	gboolean        encrypt_names;
	guint64         split_size;		/* bytes per volume; 0 means one file */
	gboolean        solid;
	gboolean        dedupe;
	/* As chosen. A store choice the writer can't do is Ignore. */
	ArcLinkOptions  links;
	gboolean        recovery_record;
	gboolean        lock;
	gboolean        delete_sources;	/* only once the archive verifies */
} ArcOptions;

void        arc_options_init  (ArcOptions *options);
void        arc_options_clear (ArcOptions *options);
void        arc_options_copy  (const ArcOptions *source,
			       ArcOptions       *dest);

const char *arc_format_id        (ArcFormat   format);
const char *arc_format_name      (ArcFormat   format);
const char *arc_format_extension (ArcFormat   format);
gboolean    arc_format_from_id   (const char *id,
				  ArcFormat  *format);

/* What this box can actually do: the union over the backends installed. */
gboolean    arc_format_available (ArcFormat format);
ArcCaps     arc_format_caps      (ArcFormat format);

/* Looks a command up on PATH, and on win32 also under Program Files, since
 * neither archiver's installer puts itself on PATH. Shared with the unpacking
 * side, which goes looking for the same two programs under other names. */
char       *arc_find_command (const char * const *names,
			      const char         *win_subdir);

/* The program a 7z or rar backend runs, probed once. NULL when it isn't
   installed, or is hidden for a test. */
const char *arc_backend_program (ArcBackend backend);

/* The same for unpacking, which takes unrar ahead of rar. Present is always
   TRUE for the library. */
const char *arc_extract_program         (ArcBackend backend);
gboolean    arc_extract_backend_present (ArcBackend backend);

/* Pure, so it can be reasoned about (and tested) without probing the box. */
ArcCaps     arc_backend_caps        (ArcFormat  format,
				     ArcBackend backend);
gboolean    arc_backend_present     (ArcBackend backend);
/* The ArcLinkStores a writer has, from whether it claims to store links. A
   store choice outside them is Ignore for that writer. The format's is the
   union over the writers installed. */
guint       arc_backend_link_stores (ArcFormat  format,
				     ArcBackend backend);
guint       arc_format_link_stores  (ArcFormat  format);
/* The same union, over the writers that can also do what options require: a
   password or volumes. What the Compress dialog offers as store choices. */
guint       arc_options_link_stores (ArcFormat         format,
				     const ArcOptions *options);
ArcBackend  arc_pick_backend        (ArcFormat         format,
				     const ArcOptions *options);
/* TRUE when storing links takes the job off a program onto the library's 7z
   writer, which compresses on one thread. */
gboolean    arc_links_cost_threads  (ArcFormat         format,
				     const ArcOptions *options);
/* Symlinks as the writer can do them, for a line built outside a job. */
gboolean    arc_stores_symlinks     (const ArcOptions *options,
				     ArcFormat         format,
				     ArcBackend        backend);

/* For tests: answer as if the program were not installed, so the library's
   own 7z writer can still be reached on a box that has 7-Zip. Unpacking
   goes by it too. */
void        arc_hide_backend        (ArcBackend backend,
				     gboolean   hidden);
/* For tests: 7-Zip keeps links, or doesn't as on Windows. */
void        arc_set_7z_keeps_links  (gboolean keeps);

/* Name handling for the dialog: the extension follows the format, and a
 * selection suggests a name - or, when the selection is part of a folder rather
 * than one item or all of it, no name at all, which leaves the field for the
 * user to fill in. whole_folder says the selection is everything the folder is
 * showing. */
char       *arc_strip_extension (const char *name);
char       *arc_apply_extension (const char *name,
				 ArcFormat   format);
char       *arc_suggest_name    (GList      *files,
				 gboolean    whole_folder,
				 ArcFormat   format);

/* What one item's archive is called when a selection is compressed separately.
   NULL for a name nothing can be built from, such as a drive root. */
char       *arc_each_name       (const char *item_name,
				 ArcFormat   format);

G_END_DECLS

#endif /* ARC_WRITER_H */
