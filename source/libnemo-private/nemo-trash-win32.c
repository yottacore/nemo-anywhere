/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */

/* nemo-trash-win32.c - trash:/// backed by the Windows Recycle Bin.

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
#include "nemo-trash-win32.h"

#include "nemo-delete-guard.h"
#include "nemo-delete-testguard.h"
#include "nemo-dir-enum.h"
#include "nemo-drives-win32.h"

#ifdef G_OS_WIN32

#include <string.h>
#include <gio/gio.h>
#include <glib/gi18n.h>
#include <glib/gstdio.h>

#define COBJMACROS
#include <windows.h>
#include <shlobj.h>
#include <shlwapi.h>
#include <oleauto.h>

#define ROOT_URI "trash:///"

/* FMTID_Displaced / PID_DISPLACED_DATE - deletion timestamp column */
static const SHCOLUMNID scid_displaced_date = {
	{ 0x9b174b33, 0x40ff, 0x11d2,
	  { 0xa2, 0x7e, 0x00, 0xc0, 0x4f, 0xc3, 0x08, 0x71 } }, 3
};

/* One recycled item. real_path is the item's actual backing file (the
 * $R... file on Windows, the Trash/files entry under wine) and doubles
 * as the item's identity: item uri = trash:/// + escaped real_path. */
typedef struct {
	char *real_path;
	char *display_name;
	char *orig_path;      /* may be NULL */
	char *deletion_date;  /* iso8601, may be NULL */
} TrashItem;

static GHashTable *trash_items = NULL;  /* real_path -> TrashItem */
static GMutex items_mutex;

/* active monitors get poked when we mutate the bin, plus polled */
static GList *active_monitors = NULL;
static GMutex monitors_mutex;

static void trash_monitor_emit_changed (void);

static void
trash_item_free (gpointer data)
{
	TrashItem *item = data;

	g_free (item->real_path);
	g_free (item->display_name);
	g_free (item->orig_path);
	g_free (item->deletion_date);
	g_free (item);
}

/* Shell enumeration */

static gboolean
com_init (void)
{
	HRESULT hr;

	hr = CoInitializeEx (NULL, COINIT_APARTMENTTHREADED);
	if (hr == RPC_E_CHANGED_MODE) {
		/* thread already MTA; shell folder still works, don't unbalance */
		return FALSE;
	}
	return TRUE;  /* balance with CoUninitialize, even for S_FALSE */
}

static IShellFolder2 *
get_recycle_folder (void)
{
	IShellFolder *desktop = NULL;
	IShellFolder2 *recycle = NULL;
	LPITEMIDLIST pidl = NULL;

	if (FAILED (SHGetDesktopFolder (&desktop))) {
		return NULL;
	}

	if (SUCCEEDED (SHGetSpecialFolderLocation (NULL, CSIDL_BITBUCKET, &pidl))) {
		IShellFolder_BindToObject (desktop, pidl, NULL,
					   &IID_IShellFolder2, (void **) &recycle);
		CoTaskMemFree (pidl);
	}

	IShellFolder_Release (desktop);
	return recycle;
}

static char *
strret_to_utf8 (STRRET *sr, LPITEMIDLIST pidl)
{
	/* Long-path aware: MAX_PATH*2 silently truncated a long original location,
	   and that shortened path is what a restore would have used. */
	WCHAR buffer[32768];
	char *result = NULL;

	if (SUCCEEDED (StrRetToBufW (sr, pidl, buffer, G_N_ELEMENTS (buffer)))) {
		result = g_utf16_to_utf8 ((gunichar2 *) buffer, -1, NULL, NULL, NULL);
	}
	return result;
}

static char *
get_display_name (IShellFolder2 *folder, LPITEMIDLIST pidl, SHGDNF flags)
{
	STRRET sr;

	if (FAILED (IShellFolder2_GetDisplayNameOf (folder, pidl, flags, &sr))) {
		return NULL;
	}
	return strret_to_utf8 (&sr, pidl);
}

static char *
get_details_column (IShellFolder2 *folder, LPITEMIDLIST pidl, UINT column)
{
	SHELLDETAILS details;

	memset (&details, 0, sizeof (details));
	if (FAILED (IShellFolder2_GetDetailsOf (folder, pidl, column, &details))) {
		return NULL;
	}
	return strret_to_utf8 (&details.str, pidl);
}

static char *
get_deletion_date_iso (IShellFolder2 *folder, LPITEMIDLIST pidl)
{
	VARIANT var;
	SYSTEMTIME st;
	char *result = NULL;

	VariantInit (&var);
	if (SUCCEEDED (IShellFolder2_GetDetailsEx (folder, pidl,
						   &scid_displaced_date, &var))) {
		if (var.vt == VT_DATE &&
		    VariantTimeToSystemTime (var.date, &st)) {
			result = g_strdup_printf ("%04d-%02d-%02dT%02d:%02d:%02d",
						  st.wYear, st.wMonth, st.wDay,
						  st.wHour, st.wMinute, st.wSecond);
		}
		VariantClear (&var);
	}
	return result;
}

/* wine maps the bin onto the XDG trash; the sibling .trashinfo has the
 * deletion date when the shell column doesn't */
static char *
xdg_trashinfo_path (const char *real_path)
{
	char *files_marker, *dir, *base, *info;

	files_marker = strstr (real_path, "Trash\\files\\");
	if (files_marker == NULL) {
		files_marker = strstr (real_path, "Trash/files/");
	}
	if (files_marker == NULL) {
		return NULL;
	}

	dir = g_strndup (real_path, files_marker - real_path);
	base = g_path_get_basename (real_path);
	info = g_strdup_printf ("%sTrash%cinfo%c%s.trashinfo",
				dir, files_marker[5], files_marker[5], base);
	g_free (dir);
	g_free (base);
	return info;
}

static char *
get_deletion_date_from_trashinfo (const char *real_path)
{
	char *info_path, *contents = NULL, *line, *result = NULL;

	info_path = xdg_trashinfo_path (real_path);
	if (info_path == NULL) {
		return NULL;
	}

	if (g_file_get_contents (info_path, &contents, NULL, NULL)) {
		line = strstr (contents, "DeletionDate=");
		if (line != NULL) {
			line += strlen ("DeletionDate=");
			result = g_strndup (line, strcspn (line, "\r\n"));
		}
	}

	g_free (contents);
	g_free (info_path);
	return result;
}

/* full re-scan of the bin into trash_items; caller holds items_mutex */
/* Windows extensions are case-insensitive, so ".TXT" already ends with ".txt". */
static gboolean
has_suffix_nocase (const char *str, const char *suffix)
{
	gsize str_len = strlen (str);
	gsize suffix_len = strlen (suffix);

	return str_len >= suffix_len &&
	       g_ascii_strcasecmp (str + str_len - suffix_len, suffix) == 0;
}

/* The shell's in-folder display name can obey Explorer's "hide extensions for
 * known file types", and that shortened name is both what we would list and
 * what restore would write. The backing $R file keeps the real extension, so
 * put it back.
 *
 * The test is whether the name already ENDS with that extension, not whether it
 * contains a dot at all: "report.2026" and "archive.tar" both have one and are
 * both still missing their last suffix. */
static char *
name_with_extension (const char *display_name, const char *real_path)
{
	char *base, *out;
	const char *dot;

	if (display_name == NULL) {
		return NULL;
	}

	base = g_path_get_basename (real_path);
	dot = strrchr (base, '.');

	if (dot != NULL && dot[1] != '\0' && !has_suffix_nocase (display_name, dot)) {
		out = g_strconcat (display_name, dot, NULL);
	} else {
		out = g_strdup (display_name);
	}

	g_free (base);
	return out;
}

static void
refresh_items_locked (void)
{
	IShellFolder2 *folder;
	IEnumIDList *enum_list = NULL;
	LPITEMIDLIST pidl;
	gboolean uninit;

	if (trash_items == NULL) {
		trash_items = g_hash_table_new_full (g_str_hash, g_str_equal,
						     NULL, trash_item_free);
	}
	g_hash_table_remove_all (trash_items);

	uninit = com_init ();

	folder = get_recycle_folder ();
	if (folder == NULL) {
		if (uninit) {
			CoUninitialize ();
		}
		return;
	}

	if (SUCCEEDED (IShellFolder2_EnumObjects (folder, NULL,
						  SHCONTF_FOLDERS | SHCONTF_NONFOLDERS,
						  &enum_list)) && enum_list != NULL) {
		while (IEnumIDList_Next (enum_list, 1, &pidl, NULL) == S_OK) {
			TrashItem *item;
			char *real_path, *name, *orig_dir, *date;

			real_path = get_display_name (folder, pidl, SHGDN_FORPARSING);
			if (real_path != NULL && !g_path_is_absolute (real_path) &&
			    real_path[0] != ':') {
				/* wine gives a bare name, not the backing path;
				 * its bin is the XDG trash under the unix home,
				 * reachable via WINEHOMEDIR (wine-only env var,
				 * NT form "\??\Z:\home\user") */
				const char *wine_home = g_getenv ("WINEHOMEDIR");

				if (wine_home != NULL) {
					char *full;

					if (g_str_has_prefix (wine_home, "\\??\\")) {
						wine_home += 4;
					}
					full = g_strdup_printf ("%s\\.local\\share\\Trash\\files\\%s",
								wine_home, real_path);
					g_free (real_path);
					real_path = full;
				}
			}
			if (real_path == NULL || !g_path_is_absolute (real_path)) {
				/* Shell-namespace-only item; we cannot proxy it. Say so - it
				   is missing from the listing while the bin's own count still
				   includes it, which reads as an item that vanished. */
				g_warning ("nemo-trash: skipping a recycle bin item with no file path%s%s",
					   real_path != NULL ? ": " : "",
					   real_path != NULL ? real_path : "");
				g_free (real_path);
				CoTaskMemFree (pidl);
				continue;
			}

			name = get_display_name (folder, pidl, SHGDN_INFOLDER);
			if (name != NULL) {
				char *full_name = name_with_extension (name, real_path);

				g_free (name);
				name = full_name;
			}
			orig_dir = get_details_column (folder, pidl, 1);
			date = get_deletion_date_iso (folder, pidl);
			if (date == NULL) {
				date = get_deletion_date_from_trashinfo (real_path);
			}

			item = g_new0 (TrashItem, 1);
			item->real_path = real_path;
			item->display_name = name != NULL ? name : g_path_get_basename (real_path);
			if (orig_dir != NULL && orig_dir[0] != '\0') {
				item->orig_path = g_build_filename (orig_dir, item->display_name, NULL);
			}
			item->deletion_date = date;

			g_hash_table_replace (trash_items, item->real_path, item);

			g_free (orig_dir);
			CoTaskMemFree (pidl);
		}
		IEnumIDList_Release (enum_list);
	}

	IShellFolder2_Release (folder);
	if (uninit) {
		CoUninitialize ();
	}
}

/* Each local drive's bin in turn. Asking for every bin at once (a NULL root)
 * takes in mapped drives too, and one that is not answering would hold the
 * caller, the window's poll included, for the network timeout. A removable
 * drive keeps a bin only under the RecycleBinDrives policy, and is asked only
 * with a disk in it. Wine answers for the whole bin whatever root it is
 * given, so the sum runs high there; nothing reads more than empty or not
 * and whether it changed.
 *
 * Count alone misses a change that keeps it the same - one item deleted and
 * another recycled between two polls, or an item replaced by a bigger one.
 * Total size moves in those cases; both together are a usable fingerprint. */
static void
query_bin_state (guint64 *count, guint64 *size)
{
	DWORD drives = GetLogicalDrives ();
	int bit;

	*count = 0;
	*size = 0;
	for (bit = 0; bit < 26; bit++) {
		wchar_t root[4] = { (wchar_t) (L'A' + bit), L':', L'\\', L'\0' };
		SHQUERYRBINFO info;
		DWORD old_mode = 0;
		HRESULT asked;

		if (!(drives & (1u << bit)) || !nemo_drive_win32_bin_askable ((char) ('A' + bit))) {
			continue;
		}

		memset (&info, 0, sizeof (info));
		info.cbSize = sizeof (info);
		/* A card pulled since the check above. */
		SetThreadErrorMode (SEM_FAILCRITICALERRORS | SEM_NOOPENFILEERRORBOX, &old_mode);
		asked = SHQueryRecycleBinW (root, &info);
		SetThreadErrorMode (old_mode, NULL);
		if (asked == S_OK) {
			*count += (guint64) info.i64NumItems;
			*size += (guint64) info.i64Size;
		}
	}
}

static guint64
query_bin_count (void)
{
	guint64 count, size;

	query_bin_state (&count, &size);
	return count;
}

/* remove the metadata sibling after the backing file leaves the bin:
 * $I twin beside a $R file on real Windows, .trashinfo under wine */
static void
remove_metadata_sibling (const char *real_path)
{
	char *base, *info_path;

	base = g_path_get_basename (real_path);
	if (g_str_has_prefix (base, "$R")) {
		char *dir, *twin_base, *twin;

		dir = g_path_get_dirname (real_path);
		twin_base = g_strconcat ("$I", base + 2, NULL);
		twin = g_build_filename (dir, twin_base, NULL);
		g_unlink (twin);
		g_free (twin);
		g_free (twin_base);
		g_free (dir);
	}
	g_free (base);

	info_path = xdg_trashinfo_path (real_path);
	if (info_path != NULL) {
		g_unlink (info_path);
		g_free (info_path);
	}
}

/* GFile implementation */

#define NEMO_TYPE_TRASH_WIN32_FILE (nemo_trash_win32_file_get_type ())
#define NEMO_TRASH_WIN32_FILE(o) (G_TYPE_CHECK_INSTANCE_CAST ((o), NEMO_TYPE_TRASH_WIN32_FILE, NemoTrashWin32File))
#define NEMO_IS_TRASH_WIN32_FILE(o) (G_TYPE_CHECK_INSTANCE_TYPE ((o), NEMO_TYPE_TRASH_WIN32_FILE))

typedef struct {
	GObject parent;
	char *uri;  /* trash:/// or trash:///<escaped real path> */
} NemoTrashWin32File;

typedef struct {
	GObjectClass parent_class;
} NemoTrashWin32FileClass;

static GType nemo_trash_win32_file_get_type (void);
static void nemo_trash_win32_file_iface_init (GFileIface *iface);
static GFile *trash_file_new_for_uri (const char *uri);

G_DEFINE_TYPE_WITH_CODE (NemoTrashWin32File, nemo_trash_win32_file, G_TYPE_OBJECT,
			 G_IMPLEMENT_INTERFACE (G_TYPE_FILE, nemo_trash_win32_file_iface_init))

static gboolean
uri_is_root (const char *uri)
{
	return strcmp (uri, ROOT_URI) == 0 || strcmp (uri, "trash://") == 0 ||
	       strcmp (uri, "trash:") == 0;
}

/* Lexical only - resolves . and .. and settles on one separator, without
 * touching the filesystem. Both the stored keys and anything compared against
 * them go through this, so two spellings of the same path compare equal and a
 * "\.." cannot walk out of a bin entry unnoticed. */
static char *
canonical_path (const char *path)
{
	if (path == NULL) {
		return NULL;
	}
	return g_canonicalize_filename (path, NULL);
}

/* item uri -> backing real path, NULL for the root */
static char *
uri_to_real_path (const char *uri)
{
	char *raw, *canonical;

	if (uri_is_root (uri)) {
		return NULL;
	}

	raw = g_uri_unescape_string (uri + strlen (ROOT_URI), NULL);
	if (raw == NULL) {
		return NULL;
	}

	canonical = canonical_path (raw);
	g_free (raw);
	return canonical;
}

/* TRUE when child names something strictly below parent */
static gboolean
path_has_prefix (const char *parent, const char *child)
{
	gsize len = strlen (parent);

	return strncmp (child, parent, len) == 0 &&
	       (child[len] == '\\' || child[len] == '/');
}

/* TRUE when real_path lives inside one of the bin's entries. The shell only
 * tracks top-level items, so anything deeper has to be recognized this way.
 * Caller holds items_mutex. */
static gboolean
is_inside_item_locked (const char *real_path)
{
	GHashTableIter iter;
	gpointer key;

	g_hash_table_iter_init (&iter, trash_items);
	while (g_hash_table_iter_next (&iter, &key, NULL)) {
		if (path_has_prefix ((const char *) key, real_path)) {
			return TRUE;
		}
	}
	return FALSE;
}

/* Caller holds items_mutex. */
static gboolean
is_bin_path_locked (const char *real_path)
{
	if (trash_items == NULL) {
		refresh_items_locked ();
	}
	if (g_hash_table_lookup (trash_items, real_path) != NULL ||
	    is_inside_item_locked (real_path)) {
		return TRUE;
	}

	/* A miss may just be a stale snapshot. */
	refresh_items_locked ();
	return g_hash_table_lookup (trash_items, real_path) != NULL ||
	       is_inside_item_locked (real_path);
}

/* Every operation that touches a real file goes through here. The path comes
 * out of a uri any local process can construct and hand us over D-Bus, so it
 * has to be shown to name something the bin actually holds before anything is
 * read, moved or permanently deleted. */
static char *
uri_to_verified_real_path (const char *uri, GError **error)
{
	char *real_path;
	gboolean ok;

	real_path = uri_to_real_path (uri);
	if (real_path == NULL) {
		g_set_error (error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND,
			     "Not a recycle bin item");
		return NULL;
	}

	g_mutex_lock (&items_mutex);
	ok = is_bin_path_locked (real_path);
	g_mutex_unlock (&items_mutex);

	if (!ok) {
		g_set_error (error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND,
			     "'%s' is not in the recycle bin", real_path);
		g_free (real_path);
		return NULL;
	}

	return real_path;
}

/* look an item up by uri, refreshing the snapshot if it's unknown;
 * returns a copy the caller frees with trash_item_free */
static TrashItem *
lookup_item_copy (const char *uri)
{
	TrashItem *item, *copy = NULL;
	char *real_path;

	real_path = uri_to_real_path (uri);
	if (real_path == NULL) {
		return NULL;
	}

	g_mutex_lock (&items_mutex);
	if (trash_items == NULL ||
	    (item = g_hash_table_lookup (trash_items, real_path)) == NULL) {
		refresh_items_locked ();
		item = g_hash_table_lookup (trash_items, real_path);
	}
	if (item != NULL) {
		copy = g_new0 (TrashItem, 1);
		copy->real_path = g_strdup (item->real_path);
		copy->display_name = g_strdup (item->display_name);
		copy->orig_path = g_strdup (item->orig_path);
		copy->deletion_date = g_strdup (item->deletion_date);
	} else if (is_inside_item_locked (real_path) &&
		   g_file_test (real_path, G_FILE_TEST_EXISTS)) {
		/* Something inside a trashed folder. It has no bin entry of its own,
		 * so it gets no original location or deletion date - just enough to
		 * be listed and read. */
		copy = g_new0 (TrashItem, 1);
		copy->real_path = g_strdup (real_path);
		copy->display_name = g_path_get_basename (real_path);
	}
	g_mutex_unlock (&items_mutex);

	g_free (real_path);
	return copy;
}

/* TRUE when the uri names a bin entry rather than something inside one */
static gboolean
uri_is_top_level_item (const char *uri)
{
	char *real_path;
	gboolean result;

	real_path = uri_to_real_path (uri);
	if (real_path == NULL) {
		return FALSE;
	}

	g_mutex_lock (&items_mutex);
	if (trash_items == NULL) {
		refresh_items_locked ();
	}
	result = g_hash_table_lookup (trash_items, real_path) != NULL;
	if (!result) {
		/* Refresh on a miss, the way the sibling lookup does. Without it a
		 * just-trashed item is not yet in the snapshot, so it is taken for
		 * something inside a folder and filed under a parent that isn't
		 * there - until the next poll happens to catch up. */
		refresh_items_locked ();
		result = g_hash_table_lookup (trash_items, real_path) != NULL;
	}
	g_mutex_unlock (&items_mutex);

	g_free (real_path);
	return result;
}

/* item identity, and what get_child/resolve_relative_path take back */
static char *
uri_for_real_path (const char *real_path)
{
	char *escaped, *uri;

	escaped = g_uri_escape_string (real_path, NULL, TRUE);
	uri = g_strconcat (ROOT_URI, escaped, NULL);
	g_free (escaped);

	return uri;
}

static GFileInfo *
make_item_info (TrashItem *item)
{
	GFileInfo *info;
	GFile *real;
	GError *query_error = NULL;
	char *escaped;

	/* real file supplies type/size/times/icon; overlay the trash bits */
	real = g_file_new_for_path (item->real_path);
	info = g_file_query_info (real,
				  "standard::*,time::*,access::*,thumbnail::*,preview::*",
				  G_FILE_QUERY_INFO_NOFOLLOW_SYMLINKS, NULL, &query_error);

	if (info == NULL) {
		char *content_type;
		GIcon *icon;
		gboolean is_dir;

		/* Backing file unreadable; guess enough from the name that downstream
		 * never sees an info without type or icon - but do not call a folder a
		 * file, and do not present something that is simply gone as healthy. */
		is_dir = g_file_test (item->real_path, G_FILE_TEST_IS_DIR);

		info = g_file_info_new ();
		g_file_info_set_file_type (info, is_dir ? G_FILE_TYPE_DIRECTORY
							: G_FILE_TYPE_REGULAR);

		if (is_dir) {
			content_type = g_strdup ("inode/directory");
		} else {
			content_type = g_content_type_guess (item->display_name, NULL, 0, NULL);
		}
		g_file_info_set_content_type (info, content_type);
		icon = g_content_type_get_icon (content_type);
		g_file_info_set_icon (info, icon);
		g_object_unref (icon);
		g_free (content_type);

		/* Deleted behind our back: say so rather than listing it as present
		 * until the next full refresh. */
		if (g_error_matches (query_error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND) ||
		    !g_file_query_exists (real, NULL)) {
			g_file_info_set_attribute_boolean (info, G_FILE_ATTRIBUTE_ACCESS_CAN_READ, FALSE);
			g_file_info_set_attribute_boolean (info, G_FILE_ATTRIBUTE_STANDARD_IS_VIRTUAL, TRUE);
		}
	}

	g_clear_error (&query_error);
	g_object_unref (real);

	escaped = g_uri_escape_string (item->real_path, NULL, TRUE);
	g_file_info_set_name (info, escaped);
	g_free (escaped);

	g_file_info_set_display_name (info, item->display_name);
	g_file_info_set_edit_name (info, item->display_name);

	if (item->orig_path != NULL) {
		g_file_info_set_attribute_byte_string (info, "trash::orig-path",
						       item->orig_path);
	}
	if (item->deletion_date != NULL) {
		g_file_info_set_attribute_string (info, "trash::deletion-date",
						  item->deletion_date);
	}

	/* items can be deleted/restored, never renamed in place */
	g_file_info_set_attribute_boolean (info, G_FILE_ATTRIBUTE_ACCESS_CAN_RENAME, FALSE);
	g_file_info_set_attribute_boolean (info, G_FILE_ATTRIBUTE_ACCESS_CAN_DELETE, TRUE);
	g_file_info_set_attribute_boolean (info, G_FILE_ATTRIBUTE_ACCESS_CAN_TRASH, FALSE);
	g_file_info_set_attribute_boolean (info, G_FILE_ATTRIBUTE_ACCESS_CAN_WRITE, FALSE);

	return info;
}

/* GFile vtable */

static GFile *
trash_file_dup (GFile *file)
{
	return trash_file_new_for_uri (NEMO_TRASH_WIN32_FILE (file)->uri);
}

static guint
trash_file_hash (GFile *file)
{
	return g_str_hash (NEMO_TRASH_WIN32_FILE (file)->uri);
}

static gboolean
trash_file_equal (GFile *a, GFile *b)
{
	return g_str_equal (NEMO_TRASH_WIN32_FILE (a)->uri,
			    NEMO_TRASH_WIN32_FILE (b)->uri);
}

static gboolean
trash_file_is_native (G_GNUC_UNUSED GFile *file)
{
	return FALSE;
}

static gboolean
trash_file_has_uri_scheme (G_GNUC_UNUSED GFile *file, const char *scheme)
{
	return g_ascii_strcasecmp (scheme, "trash") == 0;
}

static char *
trash_file_get_uri_scheme (G_GNUC_UNUSED GFile *file)
{
	return g_strdup ("trash");
}

static char *
trash_file_get_basename (GFile *file)
{
	NemoTrashWin32File *self = NEMO_TRASH_WIN32_FILE (file);

	if (uri_is_root (self->uri)) {
		return g_strdup ("/");
	}
	return g_strdup (self->uri + strlen (ROOT_URI));
}

static char *
trash_file_get_path (G_GNUC_UNUSED GFile *file)
{
	return NULL;
}

static char *
trash_file_get_uri (GFile *file)
{
	return g_strdup (NEMO_TRASH_WIN32_FILE (file)->uri);
}

static char *
trash_file_get_parse_name (GFile *file)
{
	return g_strdup (NEMO_TRASH_WIN32_FILE (file)->uri);
}

static GFile *
trash_file_get_parent (GFile *file)
{
	NemoTrashWin32File *self = NEMO_TRASH_WIN32_FILE (file);
	char *real_path, *parent_path, *uri;
	GFile *parent;

	if (uri_is_root (self->uri)) {
		return NULL;
	}

	/* A bin entry hangs off the root; anything inside a trashed folder hangs
	 * off the folder it is in. */
	if (uri_is_top_level_item (self->uri)) {
		return trash_file_new_for_uri (ROOT_URI);
	}

	real_path = uri_to_real_path (self->uri);
	parent_path = g_path_get_dirname (real_path);
	uri = uri_for_real_path (parent_path);
	parent = trash_file_new_for_uri (uri);

	g_free (uri);
	g_free (parent_path);
	g_free (real_path);

	return parent;
}

static gboolean
trash_file_prefix_matches (GFile *parent, GFile *descendant)
{
	NemoTrashWin32File *p = NEMO_TRASH_WIN32_FILE (parent);
	NemoTrashWin32File *d = NEMO_TRASH_WIN32_FILE (descendant);
	char *parent_path, *descendant_path;
	gboolean result;

	if (uri_is_root (d->uri)) {
		return FALSE;
	}
	if (uri_is_root (p->uri)) {
		return TRUE;
	}

	parent_path = uri_to_real_path (p->uri);
	descendant_path = uri_to_real_path (d->uri);
	result = path_has_prefix (parent_path, descendant_path);

	g_free (parent_path);
	g_free (descendant_path);

	return result;
}

/* An item's name is its escaped backing path at every level, so the way back
 * from a relative path is the same wherever it came from. */
static char *
trash_file_get_relative_path (GFile *parent, GFile *descendant)
{
	NemoTrashWin32File *d = NEMO_TRASH_WIN32_FILE (descendant);

	if (!trash_file_prefix_matches (parent, descendant)) {
		return NULL;
	}
	return g_strdup (d->uri + strlen (ROOT_URI));
}

static GFile *
trash_file_resolve_relative_path (GFile *file, const char *relative_path)
{
	char *uri;
	GFile *result;

	if (relative_path == NULL || relative_path[0] == '\0' ||
	    strcmp (relative_path, "/") == 0) {
		return trash_file_dup (file);
	}

	uri = g_strconcat (ROOT_URI, relative_path, NULL);
	result = trash_file_new_for_uri (uri);
	g_free (uri);
	return result;
}

static GFile *
trash_file_get_child_for_display_name (GFile *file, const char *display_name,
				       G_GNUC_UNUSED GError **error)
{
	return trash_file_resolve_relative_path (file, display_name);
}

static GFileInfo *
trash_file_query_info (GFile *file, G_GNUC_UNUSED const char *attributes,
		       G_GNUC_UNUSED GFileQueryInfoFlags flags, G_GNUC_UNUSED GCancellable *cancellable,
		       GError **error)
{
	NemoTrashWin32File *self = NEMO_TRASH_WIN32_FILE (file);

	if (uri_is_root (self->uri)) {
		GFileInfo *info;
		GIcon *icon;

		info = g_file_info_new ();
		g_file_info_set_file_type (info, G_FILE_TYPE_DIRECTORY);
		g_file_info_set_name (info, "/");
		g_file_info_set_display_name (info, _("Trash"));
		g_file_info_set_content_type (info, "inode/directory");

		/* One walk of the bin, not two - this runs on every look at the
		   trash folder. */
		guint64 bin_count = query_bin_count ();

		icon = g_themed_icon_new (bin_count > 0 ?
					  "user-trash-full" : "user-trash");
		g_file_info_set_icon (info, icon);
		g_object_unref (icon);

		g_file_info_set_attribute_uint32 (info, G_FILE_ATTRIBUTE_TRASH_ITEM_COUNT,
						  (guint32) bin_count);
		g_file_info_set_attribute_boolean (info, G_FILE_ATTRIBUTE_ACCESS_CAN_READ, TRUE);
		g_file_info_set_attribute_boolean (info, G_FILE_ATTRIBUTE_ACCESS_CAN_WRITE, FALSE);
		g_file_info_set_attribute_boolean (info, G_FILE_ATTRIBUTE_ACCESS_CAN_DELETE, FALSE);
		g_file_info_set_attribute_boolean (info, G_FILE_ATTRIBUTE_ACCESS_CAN_TRASH, FALSE);
		g_file_info_set_attribute_boolean (info, G_FILE_ATTRIBUTE_ACCESS_CAN_RENAME, FALSE);

		return info;
	} else {
		TrashItem *item;
		GFileInfo *info;

		item = lookup_item_copy (self->uri);
		if (item == NULL) {
			g_set_error (error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND,
				     "No such item in the recycle bin");
			return NULL;
		}

		info = make_item_info (item);
		trash_item_free (item);
		return info;
	}
}

static GFileInfo *
trash_file_query_filesystem_info (G_GNUC_UNUSED GFile *file, G_GNUC_UNUSED const char *attributes,
				  G_GNUC_UNUSED GCancellable *cancellable, G_GNUC_UNUSED GError **error)
{
	GFileInfo *info;

	info = g_file_info_new ();
	g_file_info_set_attribute_string (info, G_FILE_ATTRIBUTE_FILESYSTEM_TYPE, "trash");
	g_file_info_set_attribute_boolean (info, G_FILE_ATTRIBUTE_FILESYSTEM_READONLY, TRUE);
	return info;
}

/* enumerator */

#define NEMO_TYPE_TRASH_WIN32_ENUMERATOR (nemo_trash_win32_enumerator_get_type ())
#define NEMO_TRASH_WIN32_ENUMERATOR(o) (G_TYPE_CHECK_INSTANCE_CAST ((o), NEMO_TYPE_TRASH_WIN32_ENUMERATOR, NemoTrashWin32Enumerator))

typedef struct {
	GFileEnumerator parent;
	GList *items;     /* TrashItem copies */
	GList *position;
} NemoTrashWin32Enumerator;

typedef struct {
	GFileEnumeratorClass parent_class;
} NemoTrashWin32EnumeratorClass;

static GType nemo_trash_win32_enumerator_get_type (void);

G_DEFINE_TYPE (NemoTrashWin32Enumerator, nemo_trash_win32_enumerator,
	       G_TYPE_FILE_ENUMERATOR)

static GFileInfo *
trash_enumerator_next_file (GFileEnumerator *enumerator,
			    G_GNUC_UNUSED GCancellable *cancellable, G_GNUC_UNUSED GError **error)
{
	NemoTrashWin32Enumerator *self = NEMO_TRASH_WIN32_ENUMERATOR (enumerator);
	GFileInfo *info;

	if (self->position == NULL) {
		return NULL;
	}

	info = make_item_info ((TrashItem *) self->position->data);
	self->position = self->position->next;
	return info;
}

static gboolean
trash_enumerator_close (G_GNUC_UNUSED GFileEnumerator *enumerator,
			G_GNUC_UNUSED GCancellable *cancellable, G_GNUC_UNUSED GError **error)
{
	return TRUE;
}

static void
trash_enumerator_finalize (GObject *object)
{
	NemoTrashWin32Enumerator *self = NEMO_TRASH_WIN32_ENUMERATOR (object);

	g_list_free_full (self->items, trash_item_free);

	G_OBJECT_CLASS (nemo_trash_win32_enumerator_parent_class)->finalize (object);
}

static void
nemo_trash_win32_enumerator_init (G_GNUC_UNUSED NemoTrashWin32Enumerator *self)
{
}

static void
nemo_trash_win32_enumerator_class_init (NemoTrashWin32EnumeratorClass *klass)
{
	G_OBJECT_CLASS (klass)->finalize = trash_enumerator_finalize;
	G_FILE_ENUMERATOR_CLASS (klass)->next_file = trash_enumerator_next_file;
	G_FILE_ENUMERATOR_CLASS (klass)->close_fn = trash_enumerator_close;
}

static GFileEnumerator *
trash_file_enumerate_children (GFile *file, G_GNUC_UNUSED const char *attributes,
			       G_GNUC_UNUSED GFileQueryInfoFlags flags,
			       GCancellable *cancellable, GError **error)
{
	NemoTrashWin32File *self = NEMO_TRASH_WIN32_FILE (file);
	NemoTrashWin32Enumerator *enumerator;
	GHashTableIter iter;
	gpointer value;

	/* Inside a trashed folder. Its contents are not bin entries of their own,
	 * but they still have to list: the pre-delete count walks in here, and
	 * without it a trashed folder can neither be opened nor permanently
	 * deleted. A trashed file still answers NOT_DIRECTORY, from the enumerate
	 * of its backing path. */
	if (!uri_is_root (self->uri)) {
		GFileEnumerator *children;
		GFileInfo *child_info;
		TrashItem *item;
		GFile *real;
		char *real_path;

		item = lookup_item_copy (self->uri);
		if (item == NULL) {
			g_set_error (error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND,
				     "No such item in the recycle bin");
			return NULL;
		}
		real_path = g_strdup (item->real_path);
		trash_item_free (item);

		real = g_file_new_for_path (real_path);
		children = nemo_enumerate_children (real, G_FILE_ATTRIBUTE_STANDARD_NAME,
						    G_FILE_QUERY_INFO_NOFOLLOW_SYMLINKS,
						    cancellable, error);
		g_object_unref (real);

		if (children == NULL) {
			g_free (real_path);
			return NULL;
		}

		enumerator = g_object_new (NEMO_TYPE_TRASH_WIN32_ENUMERATOR,
					   "container", file, NULL);

		while ((child_info = g_file_enumerator_next_file (children, cancellable,
								  NULL)) != NULL) {
			TrashItem *copy = g_new0 (TrashItem, 1);

			copy->display_name = g_strdup (g_file_info_get_name (child_info));
			copy->real_path = g_build_filename (real_path, copy->display_name, NULL);
			enumerator->items = g_list_prepend (enumerator->items, copy);
			g_object_unref (child_info);
		}

		g_file_enumerator_close (children, cancellable, NULL);
		g_object_unref (children);
		g_free (real_path);

		enumerator->position = enumerator->items;
		return G_FILE_ENUMERATOR (enumerator);
	}

	enumerator = g_object_new (NEMO_TYPE_TRASH_WIN32_ENUMERATOR,
				   "container", file, NULL);

	g_mutex_lock (&items_mutex);
	refresh_items_locked ();
	g_hash_table_iter_init (&iter, trash_items);
	while (g_hash_table_iter_next (&iter, NULL, &value)) {
		TrashItem *item = value, *copy;

		copy = g_new0 (TrashItem, 1);
		copy->real_path = g_strdup (item->real_path);
		copy->display_name = g_strdup (item->display_name);
		copy->orig_path = g_strdup (item->orig_path);
		copy->deletion_date = g_strdup (item->deletion_date);
		enumerator->items = g_list_prepend (enumerator->items, copy);
	}
	g_mutex_unlock (&items_mutex);

	enumerator->position = enumerator->items;
	return G_FILE_ENUMERATOR (enumerator);
}

/* mutation: delete, restore (move), streams */

/* A trashed folder goes as a unit. nemo's empty-trash walk deliberately stops
 * recursing at a trash:// item (delete_trash_file, nemo-file-operations.c) since
 * the contents of a trashed folder are not themselves trashed items, so the whole
 * tree has to come out here - otherwise a bin holding any folder can neither be
 * emptied nor have that folder permanently deleted. */
static gboolean
delete_real_tree (GFile *real, GCancellable *cancellable, GError **error)
{
	GFileInfo *info;
	GFileEnumerator *children;
	gboolean ok = TRUE;

	/* Asked once for the whole tree: the recursion below sees the mark and
	 * stays quiet. */
	if (!nemo_delete_testguard_ask_one ("Delete from recycle bin", real)) {
		g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_CANCELLED,
				     "called off at the delete test guard");
		return FALSE;
	}
	nemo_delete_testguard_begin ();

	info = g_file_query_info (real, G_FILE_ATTRIBUTE_STANDARD_TYPE,
				  G_FILE_QUERY_INFO_NOFOLLOW_SYMLINKS,
				  cancellable, error);
	if (info == NULL) {
		nemo_delete_testguard_end ();
		return FALSE;
	}

	/* Only a real directory is walked into - a link or a junction is deleted
	 * as the link it is, never followed out of the bin. GIO reports both kinds
	 * of folder link as a directory here, NOFOLLOW or not, so the type alone
	 * let a junction through until 20260918. */
	if (nemo_delete_guard_is_real_folder (real, info, cancellable)) {
		children = nemo_enumerate_children (real, G_FILE_ATTRIBUTE_STANDARD_NAME,
						    G_FILE_QUERY_INFO_NOFOLLOW_SYMLINKS,
						    cancellable, error);
		if (children == NULL) {
			g_object_unref (info);
			nemo_delete_testguard_end ();
			return FALSE;
		}

		while (ok) {
			GError *walk_error = NULL;
			GFileInfo *child_info;
			GFile *child;

			/* next_file answers NULL for both the end of the list and a
			 * failure, so the error has to be asked for separately. */
			child_info = g_file_enumerator_next_file (children, cancellable, &walk_error);
			if (child_info == NULL) {
				if (walk_error != NULL) {
					g_propagate_error (error, walk_error);
					ok = FALSE;
				}
				break;
			}

			child = g_file_get_child (real, g_file_info_get_name (child_info));
			ok = delete_real_tree (child, cancellable, error);
			g_object_unref (child);
			g_object_unref (child_info);
		}

		g_file_enumerator_close (children, cancellable, NULL);
		g_object_unref (children);
	}

	g_object_unref (info);

	nemo_delete_testguard_end ();

	return ok && g_file_delete (real, cancellable, error);
}

static gboolean
trash_file_delete (GFile *file, GCancellable *cancellable, GError **error)
{
	NemoTrashWin32File *self = NEMO_TRASH_WIN32_FILE (file);
	char *real_path;
	GFile *real;
	gboolean result;

	if (uri_is_root (self->uri)) {
		g_set_error (error, G_IO_ERROR, G_IO_ERROR_NOT_SUPPORTED,
			     "Cannot delete the trash root");
		return FALSE;
	}

	real_path = uri_to_verified_real_path (self->uri, error);
	if (real_path == NULL) {
		return FALSE;
	}

	real = g_file_new_for_path (real_path);
	result = delete_real_tree (real, cancellable, error);
	g_object_unref (real);

	if (result) {
		remove_metadata_sibling (real_path);
		trash_monitor_emit_changed ();
	}

	g_free (real_path);
	return result;
}

static gboolean
trash_file_move (GFile *source, GFile *destination, GFileCopyFlags flags,
		 GCancellable *cancellable, GFileProgressCallback progress_callback,
		 gpointer progress_callback_data, GError **error)
{
	NemoTrashWin32File *self;
	char *real_path, *dest_path;
	GFile *real;
	gboolean result;

	if (!NEMO_IS_TRASH_WIN32_FILE (source)) {
		g_set_error (error, G_IO_ERROR, G_IO_ERROR_NOT_SUPPORTED,
			     "Unsupported move");
		return FALSE;
	}
	self = NEMO_TRASH_WIN32_FILE (source);

	if (uri_is_root (self->uri)) {
		g_set_error (error, G_IO_ERROR, G_IO_ERROR_NOT_SUPPORTED,
			     "Cannot move the trash root");
		return FALSE;
	}

	dest_path = g_file_get_path (destination);
	if (dest_path == NULL) {
		/* non-native destination: let GIO fall back to copy+delete */
		g_set_error (error, G_IO_ERROR, G_IO_ERROR_NOT_SUPPORTED,
			     "Move to non-native destination not supported");
		return FALSE;
	}

	real_path = uri_to_verified_real_path (self->uri, error);
	if (real_path == NULL) {
		g_free (dest_path);
		return FALSE;
	}

	real = g_file_new_for_path (real_path);
	result = g_file_move (real, destination, flags, cancellable,
			      progress_callback, progress_callback_data, error);
	g_object_unref (real);

	if (result) {
		remove_metadata_sibling (real_path);
		trash_monitor_emit_changed ();
	}

	g_free (real_path);
	g_free (dest_path);
	return result;
}

static GFileInputStream *
trash_file_read_fn (GFile *file, GCancellable *cancellable, GError **error)
{
	NemoTrashWin32File *self = NEMO_TRASH_WIN32_FILE (file);
	char *real_path;
	GFile *real;
	GFileInputStream *stream;

	if (uri_is_root (self->uri)) {
		g_set_error (error, G_IO_ERROR, G_IO_ERROR_IS_DIRECTORY,
			     "Is a directory");
		return NULL;
	}

	real_path = uri_to_verified_real_path (self->uri, error);
	if (real_path == NULL) {
		return NULL;
	}

	real = g_file_new_for_path (real_path);
	stream = g_file_read (real, cancellable, error);
	g_object_unref (real);
	g_free (real_path);
	return stream;
}

/* monitor: poll the bin while anyone is watching */

#define NEMO_TYPE_TRASH_WIN32_MONITOR (nemo_trash_win32_monitor_get_type ())
#define NEMO_TRASH_WIN32_MONITOR(o) (G_TYPE_CHECK_INSTANCE_CAST ((o), NEMO_TYPE_TRASH_WIN32_MONITOR, NemoTrashWin32Monitor))

typedef struct {
	GFileMonitor parent;
	guint timeout_id;
	guint64 last_count;
	guint64 last_size;
} NemoTrashWin32Monitor;

typedef struct {
	GFileMonitorClass parent_class;
} NemoTrashWin32MonitorClass;

static GType nemo_trash_win32_monitor_get_type (void);

G_DEFINE_TYPE (NemoTrashWin32Monitor, nemo_trash_win32_monitor, G_TYPE_FILE_MONITOR)

static void
monitor_emit (NemoTrashWin32Monitor *monitor)
{
	GFile *root;

	root = trash_file_new_for_uri (ROOT_URI);
	g_file_monitor_emit_event (G_FILE_MONITOR (monitor), root, NULL,
				   G_FILE_MONITOR_EVENT_CHANGED);
	g_object_unref (root);
}

static gboolean
monitor_poll (gpointer user_data)
{
	NemoTrashWin32Monitor *monitor = user_data;
	guint64 count, size;

	query_bin_state (&count, &size);
	if (count != monitor->last_count || size != monitor->last_size) {
		monitor->last_count = count;
		monitor->last_size = size;
		monitor_emit (monitor);
	}
	return G_SOURCE_CONTINUE;
}

static gboolean
emit_changed_idle (G_GNUC_UNUSED gpointer user_data)
{
	GList *snapshot = NULL, *l;

	/* Emission is synchronous, and a handler that opens or closes a trash view
	   drops the last monitor reference - which re-enters cancel and deadlocks
	   on this same non-recursive lock. Take a referenced copy, then let go. */
	g_mutex_lock (&monitors_mutex);
	for (l = active_monitors; l != NULL; l = l->next) {
		NemoTrashWin32Monitor *monitor = l->data;

		query_bin_state (&monitor->last_count, &monitor->last_size);
		snapshot = g_list_prepend (snapshot, g_object_ref (monitor));
	}
	g_mutex_unlock (&monitors_mutex);

	for (l = snapshot; l != NULL; l = l->next) {
		monitor_emit (l->data);
	}
	g_list_free_full (snapshot, g_object_unref);

	return G_SOURCE_REMOVE;
}

/* mutations can happen on GIO worker threads; emit from the main loop */
static void
trash_monitor_emit_changed (void)
{
	g_idle_add (emit_changed_idle, NULL);
}

static gboolean
trash_win32_monitor_cancel (GFileMonitor *file_monitor)
{
	NemoTrashWin32Monitor *monitor = NEMO_TRASH_WIN32_MONITOR (file_monitor);

	if (monitor->timeout_id != 0) {
		g_source_remove (monitor->timeout_id);
		monitor->timeout_id = 0;
	}

	g_mutex_lock (&monitors_mutex);
	active_monitors = g_list_remove (active_monitors, monitor);
	g_mutex_unlock (&monitors_mutex);

	return TRUE;
}

static void
nemo_trash_win32_monitor_init (G_GNUC_UNUSED NemoTrashWin32Monitor *monitor)
{
}

static void
nemo_trash_win32_monitor_class_init (NemoTrashWin32MonitorClass *klass)
{
	G_FILE_MONITOR_CLASS (klass)->cancel = trash_win32_monitor_cancel;
}

static GFileMonitor *
trash_file_monitor (G_GNUC_UNUSED GFile *file, G_GNUC_UNUSED GFileMonitorFlags flags,
		    G_GNUC_UNUSED GCancellable *cancellable, G_GNUC_UNUSED GError **error)
{
	NemoTrashWin32Monitor *monitor;

	monitor = g_object_new (NEMO_TYPE_TRASH_WIN32_MONITOR, NULL);
	query_bin_state (&monitor->last_count, &monitor->last_size);
	monitor->timeout_id = g_timeout_add_seconds (3, monitor_poll, monitor);

	g_mutex_lock (&monitors_mutex);
	active_monitors = g_list_prepend (active_monitors, monitor);
	g_mutex_unlock (&monitors_mutex);

	return G_FILE_MONITOR (monitor);
}

/* Object lifecycle and registration */

static void
trash_file_finalize (GObject *object)
{
	NemoTrashWin32File *self = NEMO_TRASH_WIN32_FILE (object);

	g_free (self->uri);

	G_OBJECT_CLASS (nemo_trash_win32_file_parent_class)->finalize (object);
}

static void
nemo_trash_win32_file_init (G_GNUC_UNUSED NemoTrashWin32File *self)
{
}

static void
nemo_trash_win32_file_class_init (NemoTrashWin32FileClass *klass)
{
	G_OBJECT_CLASS (klass)->finalize = trash_file_finalize;
}

static void
nemo_trash_win32_file_iface_init (GFileIface *iface)
{
	iface->dup = trash_file_dup;
	iface->hash = trash_file_hash;
	iface->equal = trash_file_equal;
	iface->is_native = trash_file_is_native;
	iface->has_uri_scheme = trash_file_has_uri_scheme;
	iface->get_uri_scheme = trash_file_get_uri_scheme;
	iface->get_basename = trash_file_get_basename;
	iface->get_path = trash_file_get_path;
	iface->get_uri = trash_file_get_uri;
	iface->get_parse_name = trash_file_get_parse_name;
	iface->get_parent = trash_file_get_parent;
	iface->prefix_matches = trash_file_prefix_matches;
	iface->get_relative_path = trash_file_get_relative_path;
	iface->resolve_relative_path = trash_file_resolve_relative_path;
	iface->get_child_for_display_name = trash_file_get_child_for_display_name;
	iface->enumerate_children = trash_file_enumerate_children;
	iface->query_info = trash_file_query_info;
	iface->query_filesystem_info = trash_file_query_filesystem_info;
	iface->read_fn = trash_file_read_fn;
	iface->delete_file = trash_file_delete;
	iface->move = trash_file_move;
	iface->monitor_dir = trash_file_monitor;
	iface->monitor_file = trash_file_monitor;
}

static GFile *
trash_file_new_for_uri (const char *uri)
{
	NemoTrashWin32File *self;

	self = g_object_new (NEMO_TYPE_TRASH_WIN32_FILE, NULL);
	self->uri = g_strdup (uri);
	return G_FILE (self);
}

static GFile *
trash_vfs_lookup (G_GNUC_UNUSED GVfs *vfs, const char *identifier, G_GNUC_UNUSED gpointer user_data)
{
	if (g_str_has_prefix (identifier, "trash:")) {
		return trash_file_new_for_uri (identifier);
	}
	return NULL;
}

/* GLib's g_file_trash leaves the shell's confirmation on, so Windows asked
   again for every file - and asked from behind nemo's progress window, which
   made a delete look wedged. NOCONFIRMATION on its own would also silently
   destroy anything the bin cannot hold, so the nuke warning is asked back. */
gboolean
nemo_trash_win32_recycle (GFile *file, GError **error)
{
	SHFILEOPSTRUCTW op;
	char *path;
	wchar_t *wide;
	glong len;
	int res;
	gboolean aborted;

	path = g_file_get_path (file);
	if (path == NULL) {
		g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_NOT_SUPPORTED,
				     _("Only a file on this computer can go to the Recycle Bin."));
		return FALSE;
	}

	/* glong, and no cast: g_utf8_to_utf16 writes a 32-bit long here. */
	wide = (wchar_t *) g_utf8_to_utf16 (path, -1, NULL, &len, NULL);
	if (wide == NULL) {
		g_set_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_FILENAME,
			     _("\"%s\" is not a name Windows can use."), path);
		g_free (path);
		return FALSE;
	}
	g_free (path);

	/* pFrom is a double-NUL terminated list, so it needs one NUL past the string. */
	wide = g_renew (wchar_t, wide, len + 2);
	wide[len] = L'\0';
	wide[len + 1] = L'\0';

	memset (&op, 0, sizeof (op));
	op.wFunc = FO_DELETE;
	op.pFrom = wide;
	op.fFlags = FOF_ALLOWUNDO | FOF_NOCONFIRMATION | FOF_WANTNUKEWARNING |
		    FOF_NOERRORUI | FOF_SILENT;

	res = SHFileOperationW (&op);
	aborted = op.fAnyOperationsAborted;
	g_free (wide);

	if (res != 0) {
		g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED,
			     _("The Recycle Bin refused it (error %d)."), res);
		return FALSE;
	}

	if (aborted) {
		g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_CANCELLED,
				     _("Cancelled."));
		return FALSE;
	}

	trash_monitor_emit_changed ();

	return TRUE;
}

void
nemo_trash_win32_register (void)
{
	static gsize once_init_value = 0;

	if (g_once_init_enter (&once_init_value)) {
		GVfs *vfs;

		vfs = g_vfs_get_default ();
		g_vfs_register_uri_scheme (vfs, "trash",
					   trash_vfs_lookup, NULL, NULL,
					   trash_vfs_lookup, NULL, NULL);

		g_once_init_leave (&once_init_value, 1);
	}
}

#else /* !G_OS_WIN32 */

void
nemo_trash_win32_register (void)
{
}

#endif /* G_OS_WIN32 */
