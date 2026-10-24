/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */

/* arc-verify.c - the check an archive passes before its originals may go.

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

#include "arc-verify.h"

#include <string.h>

#include <glib/gi18n.h>
#include <archive.h>
#include <archive_entry.h>

#include "arc-entry.h"

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

		return path != NULL && !arc_entry_leads_somewhere (path);
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
	gboolean long_target;	/* a link libarchive can't list, see expect_link */
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
	gboolean      reads_rar;
	ArcLinkChoice             symlinks;
	ArcLinkChoice             junctions;
	GHashTable               *listing;
	GHashTable               *folders;
	const ArcHost            *host;		/* lists the folders */
} VerifyWalk;

static Expected *
expect (VerifyWalk *walk,
	const char *rel_path,
	gint64      size,
	gboolean    is_dir)
{
	Expected *item = g_new0 (Expected, 1);

	item->size = size;
	item->is_dir = is_dir;

	g_hash_table_replace (walk->expected, g_strdup (rel_path), item);
	return item;
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
   and can't tell one from a mount point. target gets a link's target, owned
   by the caller. */
static LinkSort
link_sort (GFile     *file,
	   GFileInfo *info,
	   char     **target)
{
#ifdef G_OS_WIN32
	ArcEntry entry = arc_entry_read (g_file_peek_path (file), target);

	(void) info;
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
	if (!g_file_info_get_is_symlink (info)) {
		return NOT_A_LINK;
	}
	*target = g_strdup (g_file_info_get_symlink_target (info));
	return A_SYMLINK;
#endif
}

/* The target as rar keeps it. On Windows that's the link's own text, with
   \??\ ahead of a whole path, which arc_entry_read takes off. */
static gsize
stored_target_length (const char *target)
{
	gsize length = strlen (target);

#ifdef G_OS_WIN32
	if (target[0] == '\\' && target[1] == '\\') {
		return length + 6;	/* \\server is \??\UNC\server */
	}
	if (g_ascii_isalpha (target[0]) && target[1] == ':') {
		return length + 4;
	}
#endif
	return length;
}

gboolean
arc_rar_target_unreadable (gsize stored_length)
{
	return stored_length >= 128 && archive_version_number () < 3008010;
}

gboolean
arc_rar_stopped_at_long_target (struct archive       *a,
				struct archive_entry *entry)
{
	const char *target;

	if (entry == NULL ||
	    (archive_format (a) & ARCHIVE_FORMAT_BASE_MASK) != ARCHIVE_FORMAT_RAR_V5) {
		return FALSE;
	}

	/* rar keeps the target as UTF-8. */
	target = archive_entry_symlink_utf8 (entry);
	if (target == NULL) {
		target = archive_entry_hardlink_utf8 (entry);
	}
	return target != NULL && arc_rar_target_unreadable (strlen (target));
}

static void
expect_link (VerifyWalk *walk,
	     const char *rel_path,
	     const char *target)
{
	Expected *item;

	walk->locked_link = walk->locked_link || walk->links_locked;
	item = expect (walk, rel_path, -1, FALSE);
	item->long_target = walk->reads_rar && target != NULL &&
			    arc_rar_target_unreadable (stored_target_length (target));
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

	children = arc_walk_children (walk->host, dir, SCAN_ATTRIBUTES,
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
	g_autofree char *target = NULL;

	if (g_cancellable_is_cancelled (walk->cancellable)) {
		return;
	}

	sort = link_sort (file, info, &target);
	switch (sort) {
	case A_SYMLINK:
	case A_JUNCTION:
		choice = sort == A_SYMLINK ? walk->symlinks : walk->junctions;
		if (choice == ARC_LINK_IGNORE) {
			left_out (walk, info);
			return;
		}
		if (choice != ARC_LINK_FOLLOW) {
			expect_link (walk, rel_path, target);
			return;
		}
		effective = g_file_query_info (file, SCAN_ATTRIBUTES, G_FILE_QUERY_INFO_NONE,
					       walk->cancellable, NULL);
		if (leads_nowhere (file, effective)) {
			g_clear_object (&effective);
			expect_link (walk, rel_path, target);
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
		/* cppcheck-suppress leakNoVarFunctionCall ; folders owns its keys and frees them */
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
cannot_read_back (const ArcOptions *options)
{
	if (options->split_size > 0) {
		return _("An archive split into volumes can't be checked.");
	}
	if (options->encrypt_names && options->password != NULL && options->password[0] != '\0') {
		return _("An archive with its file names encrypted can't be checked.");
	}
	return NULL;
}

/* 7-Zip keeps a link's target in the file's data, which the password locks,
   and libarchive can't unlock a 7z: it ends the listing at the first link. */
static gboolean
links_locked (const ArcOptions *options)
{
	return options->format == ARC_FORMAT_7Z &&
	       options->password != NULL && options->password[0] != '\0';
}

/* Whether an archive written this way could be read back. Nothing is removed
   on the writer's word alone, so this is also the answer to whether the
   Compress dialog may offer to delete the originals. A 7z with a password
   that keeps links can't be: the dialog hands over the Store choice only for
   a selection that can hold a link. */
gboolean
arc_can_verify (const ArcOptions *options)
{
	g_return_val_if_fail (options != NULL, FALSE);

	if (cannot_read_back (options) != NULL) {
		return FALSE;
	}

	return !(links_locked (options) &&
		 arc_link_options_symlinks (&options->links,
					    arc_options_link_stores (options->format, options)) ==
		 ARC_LINK_STORE_SYMLINK);
}

/* Whether the password has to be typed a second time before the job starts.
   Deleting the originals is the reason: a mistyped password still writes a
   perfectly good archive, the read-back still passes, and the files still go,
   leaving an archive nobody can open. Either half on its own is recoverable,
   so it is only the two together. */
gboolean
arc_should_confirm_password (const ArcOptions *options)
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
		g_string_append_printf (said, ngettext ("%s that was selected isn't in the archive.",
							"%s that were selected aren't in the archive.",
							missing),
					words);
		g_free (words);
	}
	if (extra > 0) {
		if (said->len > 0) {
			g_string_append_c (said, ' ');
		}
		words = count_words (extra_folders, extra_files);
		g_string_append_printf (said, ngettext ("%s in the archive wasn't selected.",
							"%s in the archive weren't selected.",
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
			g_string_append_printf (said, ngettext ("%'u file in the archive isn't the size of the one selected.",
								"%'u files in the archive aren't the size of the ones selected.",
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
arc_verify (const ArcHost    *host,
	    GFile            *archive_file,
	    GList            *sources,
	    const ArcOptions *options,
	    ArcBackend        backend,
	    GCancellable     *cancellable,
	    char            **reason)
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
		trouble = g_strdup (_("The archive isn't on this computer, so it can't be read back."));
		goto out;
	}

	listing = archive_listing (path, options->password, &folders);
	g_free (path);

	if (listing == NULL) {
		trouble = g_strdup (_("The archive couldn't be read back."));
		goto out;
	}

	walk.expected = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, g_free);
	walk.seen_dirs = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);
	walk.host = host;
	walk.cancellable = cancellable;
	walk.whole = TRUE;
	walk.links_locked = links_locked (options) &&
		(arc_backend_caps (options->format, backend) & ARC_CAP_STORE_LINKS);
	walk.reads_rar = options->format == ARC_FORMAT_RAR;
	{
		guint stores = arc_backend_link_stores (options->format, backend);

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
		trouble = g_strdup (_("A 7z with a password can't be checked when it keeps links as links."));
	} else if (!walk.whole) {
		trouble = g_strdup (_("Something in the selection couldn't go into an archive, so not all of it is in there."));
	}

	if (trouble == NULL) {
		guint missing_folders = walk.left_folders;
		guint missing_files = walk.left_files;
		guint extra_folders = 0;
		guint extra_files = 0;
		guint64 selected_bytes = 0;
		guint64 archived_bytes = 0;
		guint resized = 0;
		guint unlisted = 0;	/* links with a target too long to read back */

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
			/* Listed or not, the reader can't be trusted past it. */
			if (item->long_target) {
				unlisted++;
			} else if (stored == NULL) {
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

		/* The listing stops at such a link, so nothing after it would
		   count right either. */
		if (unlisted > 0) {
			trouble = g_strdup_printf (ngettext ("%'u link has a target too long to read back from a rar, so the archive can't be checked.",
							     "%'u links have targets too long to read back from a rar, so the archive can't be checked.",
							     unlisted),
						   unlisted);
		} else {
			trouble = mismatch_reason (missing_folders, missing_files, extra_folders, extra_files,
						   selected_bytes, archived_bytes, resized);
		}
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

