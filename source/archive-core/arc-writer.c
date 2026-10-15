/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */

/* arc-writer.c - which writer an archive goes to, and what each can do.

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

#include "arc-writer.h"

#include <string.h>

#include <glib/gi18n.h>

typedef struct {
	const char      *id;
	const char      *name;		/* untranslated; run through _() at use */
	const char      *extension;
	gboolean         by_libarchive;
	ArcCaps          caps_libarchive;
	gboolean         by_7z;
	ArcCaps          caps_7z;
	gboolean         by_rar;
	ArcCaps          caps_rar;
} FormatInfo;

/* What each writer can do, established against libarchive 3.8 and the switch
   lists of 7-Zip and rar. libarchive writes no rar at all, and has no write
   support for volumes, solid blocks, duplicate-as-reference or 7z encryption,
   which is the whole reason the commands are still worth reaching for. */
static const FormatInfo formats[ARC_N_FORMATS] = {
	{ "zip", N_("ZIP"), ".zip",
	  TRUE,  ARC_CAP_LEVEL | ARC_CAP_PASSWORD | ARC_CAP_STORE_LINKS,
	  TRUE,  ARC_CAP_LEVEL | ARC_CAP_PASSWORD | ARC_CAP_SPLIT |
		 ARC_CAP_STORE_LINKS,
	  FALSE, 0 },

	{ "tar", N_("TAR, uncompressed"), ".tar",
	  TRUE,  ARC_CAP_STORE_LINKS,
	  FALSE, 0,
	  FALSE, 0 },

	{ "tar.gz", N_("TAR, gzip compressed"), ".tar.gz",
	  TRUE,  ARC_CAP_LEVEL | ARC_CAP_STORE_LINKS,
	  FALSE, 0,
	  FALSE, 0 },

	{ "tar.xz", N_("TAR, xz compressed"), ".tar.xz",
	  TRUE,  ARC_CAP_LEVEL | ARC_CAP_STORE_LINKS,
	  FALSE, 0,
	  FALSE, 0 },

	{ "7z", N_("7z"), ".7z",
	  TRUE,  ARC_CAP_LEVEL | ARC_CAP_STORE_LINKS,
	  TRUE,  ARC_CAP_LEVEL | ARC_CAP_PASSWORD | ARC_CAP_ENCRYPT_NAMES |
		 ARC_CAP_SPLIT | ARC_CAP_SOLID | ARC_CAP_STORE_LINKS,
	  FALSE, 0 },

	{ "rar", N_("RAR"), ".rar",
	  FALSE, 0,
	  FALSE, 0,
	  TRUE,  ARC_CAP_LEVEL | ARC_CAP_PASSWORD | ARC_CAP_ENCRYPT_NAMES |
		 ARC_CAP_SPLIT | ARC_CAP_SOLID | ARC_CAP_DEDUPE |
		 ARC_CAP_STORE_LINKS | ARC_CAP_RECOVERY | ARC_CAP_LOCK },
};

/* Every suffix we recognize as ours, longest first so ".tar.gz" is not read as
   ".gz". Used to swap the extension when the format changes. */
static const char * const known_extensions[] = {
	".tar.gz", ".tar.xz", ".tar.bz2", ".tgz", ".txz", ".tbz2",
	".zip", ".tar", ".7z", ".rar", NULL
};
static gboolean
format_is_valid (ArcFormat format)
{
	return format >= 0 && format < ARC_N_FORMATS;
}

/* Returns: (transfer none): a static string */
const char *
arc_format_id (ArcFormat format)
{
	g_return_val_if_fail (format_is_valid (format), "zip");

	return formats[format].id;
}

/* Returns: (transfer none): a static string */
const char *
arc_format_name (ArcFormat format)
{
	g_return_val_if_fail (format_is_valid (format), "");

	return _(formats[format].name);
}

/* Returns: (transfer none): a static string */
const char *
arc_format_extension (ArcFormat format)
{
	g_return_val_if_fail (format_is_valid (format), ".zip");

	return formats[format].extension;
}

gboolean
arc_format_from_id (const char *id,
		    ArcFormat  *format)
{
	int i;

	if (id == NULL) {
		return FALSE;
	}

	for (i = 0; i < ARC_N_FORMATS; i++) {
		if (g_strcmp0 (id, formats[i].id) == 0) {
			if (format != NULL) {
				*format = (ArcFormat) i;
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
arc_find_command (const char * const *names,
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

/* Returns: (transfer none) */
const char *
arc_backend_program (ArcBackend backend)
{
	static const char * const seven_zip_names[] = { "7z", "7zz", "7za", NULL };
	static const char * const rar_names[] = { "rar", NULL };
	const char *program;

	if (backend != ARC_BACKEND_7Z && backend != ARC_BACKEND_RAR) {
		return NULL;
	}

	g_mutex_lock (&probe_lock);

	if (!probed[backend]) {
		if (backend == ARC_BACKEND_7Z) {
			probed_program[backend] = arc_find_command (seven_zip_names, "7-Zip");
		} else {
			probed_program[backend] = arc_find_command (rar_names, "WinRAR");
		}
		probed[backend] = TRUE;
	}
	program = hidden_backend[backend] ? NULL : probed_program[backend];

	g_mutex_unlock (&probe_lock);

	return program;
}

void
arc_hide_backend (ArcBackend backend,
		  gboolean   hidden)
{
	g_return_if_fail (backend == ARC_BACKEND_7Z || backend == ARC_BACKEND_RAR);

	g_mutex_lock (&probe_lock);
	hidden_backend[backend] = hidden;
	g_mutex_unlock (&probe_lock);
}

void
arc_set_7z_keeps_links (gboolean keeps)
{
	seven_zip_keeps_links = keeps;
}

gboolean
arc_backend_present (ArcBackend backend)
{
	if (backend == ARC_BACKEND_LIBARCHIVE) {
		return TRUE;
	}

	return arc_backend_program (backend) != NULL;
}

/* Unpacking has a probe of its own, since unrar is enough to read a rar. */
static char    *extract_program[4];
static gboolean extract_probed[4];

/* Returns: (transfer none) */
const char *
arc_extract_program (ArcBackend backend)
{
	static const char * const seven_zip_names[] = { "7z", "7zz", "7za", NULL };
	static const char * const rar_names[] = { "unrar", "rar", NULL };
	const char *program;

	if (backend != ARC_BACKEND_7Z && backend != ARC_BACKEND_RAR) {
		return NULL;
	}

	g_mutex_lock (&probe_lock);

	if (!extract_probed[backend]) {
		if (backend == ARC_BACKEND_7Z) {
			extract_program[backend] = arc_find_command (seven_zip_names, "7-Zip");
		} else {
			extract_program[backend] = arc_find_command (rar_names, "WinRAR");
		}
		extract_probed[backend] = TRUE;
	}
	program = hidden_backend[backend] ? NULL : extract_program[backend];

	g_mutex_unlock (&probe_lock);

	return program;
}

gboolean
arc_extract_backend_present (ArcBackend backend)
{
	if (backend == ARC_BACKEND_LIBARCHIVE) {
		return TRUE;
	}

	return arc_extract_program (backend) != NULL;
}

ArcCaps
arc_backend_caps (ArcFormat  format,
		  ArcBackend backend)
{
	const FormatInfo *info;

	g_return_val_if_fail (format_is_valid (format), 0);

	info = &formats[format];

	switch (backend) {
	case ARC_BACKEND_LIBARCHIVE:
		return info->by_libarchive ? info->caps_libarchive : 0;
	case ARC_BACKEND_7Z:
		if (!info->by_7z) {
			return 0;
		}
		return seven_zip_keeps_links ? info->caps_7z
					     : info->caps_7z & ~ARC_CAP_STORE_LINKS;
	case ARC_BACKEND_RAR:
		return info->by_rar ? info->caps_rar : 0;
	case ARC_BACKEND_NONE:
	default:
		return 0;
	}
}

static gboolean
backend_writes_format (ArcFormat  format,
		       ArcBackend backend)
{
	const FormatInfo *info = &formats[format];

	switch (backend) {
	case ARC_BACKEND_LIBARCHIVE:
		return info->by_libarchive;
	case ARC_BACKEND_7Z:
		return info->by_7z;
	case ARC_BACKEND_RAR:
		return info->by_rar;
	case ARC_BACKEND_NONE:
	default:
		return FALSE;
	}
}

/* Preference order: libarchive first, since it needs nothing installed and can
   report real progress; a command only when it is the one that can do the job. */
static const ArcBackend backend_order[] = {
	ARC_BACKEND_LIBARCHIVE,
	ARC_BACKEND_7Z,
	ARC_BACKEND_RAR
};

/* Except 7z, which goes to 7-Zip where it's installed: libarchive's 7z writer
   turns down a threads option, so it compresses on one thread. */
static const ArcBackend backend_order_7z[] = {
	ARC_BACKEND_7Z,
	ARC_BACKEND_LIBARCHIVE,
	ARC_BACKEND_RAR
};

gboolean
arc_format_available (ArcFormat format)
{
	guint i;

	g_return_val_if_fail (format_is_valid (format), FALSE);

	for (i = 0; i < G_N_ELEMENTS (backend_order); i++) {
		if (backend_writes_format (format, backend_order[i]) &&
		    arc_backend_present (backend_order[i])) {
			return TRUE;
		}
	}

	return FALSE;
}

ArcCaps
arc_format_caps (ArcFormat format)
{
	ArcCaps caps = 0;
	guint i;

	g_return_val_if_fail (format_is_valid (format), 0);

	for (i = 0; i < G_N_ELEMENTS (backend_order); i++) {
		if (arc_backend_present (backend_order[i])) {
			caps |= arc_backend_caps (format, backend_order[i]);
		}
	}

	return caps;
}

/* A writer that claims to store links stores a symlink as one. What becomes
   of a junction follows from how it keeps links: the library writes every
   link as a symlink, and rar's -ol keeps a junction a junction. 7-Zip claims
   links only where there are no junctions. */
guint
arc_backend_link_stores (ArcFormat  format,
			 ArcBackend backend)
{
	if ((arc_backend_caps (format, backend) & ARC_CAP_STORE_LINKS) == 0) {
		return 0;
	}

	switch (backend) {
	case ARC_BACKEND_LIBARCHIVE:
		return ARC_STORES_SYMLINKS | ARC_STORES_JUNCTIONS_AS_SYMLINKS;
	case ARC_BACKEND_RAR:
		return ARC_STORES_SYMLINKS | ARC_STORES_JUNCTIONS;
	case ARC_BACKEND_7Z:
		return ARC_STORES_SYMLINKS;
	case ARC_BACKEND_NONE:
	default:
		return 0;
	}
}

guint
arc_format_link_stores (ArcFormat format)
{
	guint stores = 0;
	guint i;

	g_return_val_if_fail (format_is_valid (format), 0);

	for (i = 0; i < G_N_ELEMENTS (backend_order); i++) {
		if (arc_backend_present (backend_order[i])) {
			stores |= arc_backend_link_stores (format, backend_order[i]);
		}
	}

	return stores;
}

/* Two kinds of option. Encryption and splitting change whether the archive is
   what was asked for at all, so a backend that cannot do them is not a
   candidate - writing a readable archive when one was asked to be locked is the
   worst possible outcome. The rest is tuning: preferred where it can be had,
   dropped where it cannot, rather than failing the whole job. */
static ArcCaps
required_caps (const ArcOptions *options)
{
	ArcCaps caps = 0;

	if (options->password != NULL && options->password[0] != '\0') {
		caps |= ARC_CAP_PASSWORD;

		if (options->encrypt_names) {
			caps |= ARC_CAP_ENCRYPT_NAMES;
		}
	}
	if (options->split_size > 0) {
		caps |= ARC_CAP_SPLIT;
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

gboolean
arc_stores_symlinks (const ArcOptions *options,
		     ArcFormat         format,
		     ArcBackend        backend)
{
	return arc_link_options_symlinks (&options->links,
					  arc_backend_link_stores (format, backend)) ==
	       ARC_LINK_STORE_SYMLINK;
}

static ArcCaps
preferred_caps (const ArcOptions *options)
{
	ArcCaps caps = 0;

	if (options->solid) {
		caps |= ARC_CAP_SOLID;
	}
	if (options->dedupe) {
		caps |= ARC_CAP_DEDUPE;
	}
	if (wants_store (&options->links)) {
		caps |= ARC_CAP_STORE_LINKS;
	}
	if (options->recovery_record) {
		caps |= ARC_CAP_RECOVERY;
	}
	if (options->lock) {
		caps |= ARC_CAP_LOCK;
	}

	return caps;
}

ArcBackend
arc_pick_backend (ArcFormat         format,
		  const ArcOptions *options)
{
	const ArcBackend *order;
	ArcCaps needed;
	ArcCaps preferred;
	ArcCaps targets[3];
	guint pass;
	guint i;

	g_return_val_if_fail (format_is_valid (format), ARC_BACKEND_NONE);
	g_return_val_if_fail (options != NULL, ARC_BACKEND_NONE);

	G_STATIC_ASSERT (G_N_ELEMENTS (backend_order_7z) == G_N_ELEMENTS (backend_order));
	order = format == ARC_FORMAT_7Z ? backend_order_7z : backend_order;
	needed = required_caps (options);

	/* A preference nothing can honor for this format is dropped before
	   matching, so an option left on by default - a recovery record, which
	   only rar has - does not steer a tar away from the writer that suits
	   the preferences that ARE available. */
	preferred = preferred_caps (options) & arc_format_caps (format);

	/* Storing links changes what goes in, where the rest only tunes how, so
	   it is the last preference given up. A 7z on Windows that stores links
	   goes to the library rather than 7-Zip, which leaves them out. */
	targets[0] = needed | preferred;
	targets[1] = needed | (preferred & ARC_CAP_STORE_LINKS);
	targets[2] = needed;

	for (pass = 0; pass < G_N_ELEMENTS (targets); pass++) {
		ArcCaps target = targets[pass];

		for (i = 0; i < G_N_ELEMENTS (backend_order); i++) {
			ArcBackend backend = order[i];

			if (!arc_backend_present (backend) ||
			    !backend_writes_format (format, backend)) {
				continue;
			}
			if ((arc_backend_caps (format, backend) & target) == target) {
				return backend;
			}
		}
	}

	return ARC_BACKEND_NONE;
}

/* On Windows a 7z with a password or volumes needs 7-Zip, which keeps no
   links there, so no store choice is left. */
guint
arc_options_link_stores (ArcFormat         format,
			 const ArcOptions *options)
{
	ArcCaps needed;
	guint stores = 0;
	guint i;

	g_return_val_if_fail (format_is_valid (format), 0);
	g_return_val_if_fail (options != NULL, 0);

	needed = required_caps (options);

	for (i = 0; i < G_N_ELEMENTS (backend_order); i++) {
		ArcBackend backend = backend_order[i];

		if (arc_backend_present (backend) &&
		    (arc_backend_caps (format, backend) & needed) == needed) {
			stores |= arc_backend_link_stores (format, backend);
		}
	}

	return stores;
}

gboolean
arc_links_cost_threads (ArcFormat         format,
			const ArcOptions *options)
{
	ArcOptions with_links;
	ArcBackend storing, not_storing;

	g_return_val_if_fail (options != NULL, FALSE);

	if (format != ARC_FORMAT_7Z) {
		return FALSE;
	}

	/* A shallow copy is enough: only the choice changes, and nothing is
	   freed. */
	with_links = *options;
	with_links.links.junctions_set = FALSE;
	with_links.links.symlinks = ARC_LINK_STORE_SYMLINK;
	storing = arc_pick_backend (format, &with_links);
	with_links.links.symlinks = ARC_LINK_IGNORE;
	not_storing = arc_pick_backend (format, &with_links);

	return storing == ARC_BACKEND_LIBARCHIVE && not_storing != storing;
}

void
arc_options_init (ArcOptions *options)
{
	g_return_if_fail (options != NULL);

	memset (options, 0, sizeof (ArcOptions));
	options->format = ARC_FORMAT_ZIP;
	options->level = ARC_LEVEL_DEFAULT;
	arc_link_options_init (&options->links);
	options->recovery_record = TRUE;
}

void
arc_options_clear (ArcOptions *options)
{
	g_return_if_fail (options != NULL);

	g_clear_pointer (&options->password, g_free);
}

void
arc_options_copy (const ArcOptions *source,
		  ArcOptions       *dest)
{
	g_return_if_fail (source != NULL);
	g_return_if_fail (dest != NULL);

	*dest = *source;
	dest->password = g_strdup (source->password);
}

/* Returns: (transfer full): free with g_free */
char *
arc_strip_extension (const char *name)
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
arc_apply_extension (const char *name,
		     ArcFormat   format)
{
	char *base;
	char *result;

	g_return_val_if_fail (name != NULL, NULL);
	g_return_val_if_fail (format_is_valid (format), NULL);

	base = arc_strip_extension (name);
	result = g_strconcat (base, formats[format].extension, NULL);
	g_free (base);

	return result;
}

/* One item names the archive after itself, whether it is a file or a folder.
   Several only borrow the folder's name when they are the whole folder; a part
   of one has no name a user would agree with, so none is offered.
   Returns: (transfer full): free with g_free */
char *
arc_suggest_name (GList    *files,
		  gboolean  whole_folder,
		  ArcFormat format)
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

	result = arc_apply_extension (base, format);
	g_free (base);

	return result;
}

/* Compressing a selection separately names each archive after the item it came
   from, extension and all: "notes.rar" becomes "notes.rar.zip". The name field
   strips a suffix it recognizes, which cannot be done here - re-zipping a zip
   would then write the archive over the file being read.
   Returns: (transfer full): free with g_free */
char *
arc_each_name (const char *item_name,
	       ArcFormat   format)
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
