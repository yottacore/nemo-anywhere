/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */

/* arc-mounts.c - same filesystem, a nested one, or another one.

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

#include <string.h>

#include <glib.h>
#ifndef G_OS_WIN32
#include <gio/gunixmounts.h>
#endif

#include "arc-mounts.h"

typedef struct {
	char *where;	/* as compare_form makes it */
	char *device;	/* the same, on Windows */
	char *pool;	/* NULL when it can't be nested with anything */
} Mount;

struct _ArcMountTable {
	Mount   *mounts;
	gsize    n_mounts;
	gboolean windows;
};

/* Paths as text that compares with strcmp: no slash at the end, so / is the
   empty string and C:\ is C:. On Windows a backslash, folded case, and the
   long and NT forms (\\?\C:, \??\C:, \\?\UNC\) read as the plain ones. */
static char *
compare_form (const char *path,
	      gboolean    windows)
{
	GString *text = g_string_new (path);
	char *folded;
	gsize i;

	if (windows) {
		for (i = 0; i < text->len; i++) {
			if (text->str[i] == '/') {
				text->str[i] = '\\';
			}
		}
		if (g_str_has_prefix (text->str, "\\??\\")) {
			text->str[1] = '\\';
		}
		if (g_ascii_strncasecmp (text->str, "\\\\?\\UNC\\", 8) == 0) {
			g_string_erase (text, 2, 6);
		} else if ((g_str_has_prefix (text->str, "\\\\?\\") || g_str_has_prefix (text->str, "\\\\.\\")) &&
			   g_ascii_isalpha (text->str[4]) && text->str[5] == ':') {
			g_string_erase (text, 0, 4);
		}
	}
	while (text->len > 0 && text->str[text->len - 1] == (windows ? '\\' : '/')) {
		g_string_truncate (text, text->len - 1);
	}
	if (!windows) {
		return g_string_free (text, FALSE);
	}

	if (g_utf8_validate (text->str, -1, NULL)) {
		folded = g_utf8_casefold (text->str, -1);
	} else {
		folded = g_ascii_strdown (text->str, -1);
	}
	g_string_free (text, TRUE);
	return folded;
}

/* What makes 2 mounts one pool or volume. NULL for anything with no real
   device behind it, such as tmpfs or a share, since the same made-up name
   there says nothing. */
static char *
pool_of (const char *device,
	 const char *fs_type)
{
	gsize n;

	if (device == NULL || fs_type == NULL || device[0] == '\0') {
		return NULL;
	}
	if (strcmp (fs_type, "zfs") == 0) {
		n = strcspn (device, "/@");
		return n > 0 ? g_strdup_printf ("zfs:%.*s", (int) n, device) : NULL;
	}
	if (strcmp (fs_type, "apfs") == 0 && g_str_has_prefix (device, "/dev/disk")) {
		/* /dev/disk3s1s1 is in container /dev/disk3 */
		n = strlen ("/dev/disk");
		while (g_ascii_isdigit (device[n])) {
			n++;
		}
		return g_strdup_printf ("apfs:%.*s", (int) n, device);
	}
	if (device[0] == '/') {
		/* Btrfs subvolumes, and a disk mounted twice. */
		return g_strdup_printf ("%s:%s", fs_type, device);
	}
	return NULL;
}

/* Returns: (transfer full): free with arc_mount_table_free */
ArcMountTable *
arc_mount_table_new (const ArcMountEntry *entries,
		     gsize                n_entries,
		     gboolean             windows)
{
	ArcMountTable *table = g_new0 (ArcMountTable, 1);
	gsize i;

	table->windows = windows;
	table->mounts = g_new0 (Mount, n_entries > 0 ? n_entries : 1);
	for (i = 0; i < n_entries; i++) {
		Mount *mount;

		if (entries[i].mount_path == NULL) {
			continue;
		}
		mount = &table->mounts[table->n_mounts++];
		mount->where = compare_form (entries[i].mount_path, windows);
		if (windows) {
			mount->device = entries[i].device != NULL ? compare_form (entries[i].device, TRUE) : NULL;
		} else {
			mount->pool = pool_of (entries[i].device, entries[i].fs_type);
		}
	}
	return table;
}

void
arc_mount_table_free (ArcMountTable *table)
{
	gsize i;

	if (table == NULL) {
		return;
	}
	for (i = 0; i < table->n_mounts; i++) {
		g_free (table->mounts[i].where);
		g_free (table->mounts[i].device);
		g_free (table->mounts[i].pool);
	}
	g_free (table->mounts);
	g_free (table);
}

static gboolean
is_under (const char *path,
	  const char *where,
	  char        separator)
{
	gsize n = strlen (where);

	return strncmp (path, where, n) == 0 && (path[n] == '\0' || path[n] == separator);
}

/* The mount path is under, the deepest one, and the last of those, which
   is the one on top. -1 when none is. */
static gssize
mount_of (const ArcMountTable *table,
	  const char          *path)
{
	gssize best = -1;
	gsize best_length = 0;
	gsize i;

	for (i = 0; i < table->n_mounts; i++) {
		const char *where = table->mounts[i].where;
		gsize length = strlen (where);

		if (is_under (path, where, table->windows ? '\\' : '/') &&
		    (best < 0 || length >= best_length)) {
			best = (gssize) i;
			best_length = length;
		}
	}
	return best;
}

/* Where a path no mount covers starts: / here, and on Windows the drive or
   the \\server\share, as a mapped drive or a share has no volume. */
static char *
root_of (const char *path,
	 gboolean    windows)
{
	const char *end = path;
	int parts = 0;

	if (!windows) {
		return g_strdup ("");
	}
	if (g_ascii_isalpha (path[0]) && path[1] == ':') {
		return g_strndup (path, 2);
	}
	if (g_str_has_prefix (path, "\\\\")) {
		/* \\server\share, or \\?\Volume{...} */
		for (end = path + 2; *end != '\0'; end++) {
			if (*end == '\\' && ++parts == 2) {
				break;
			}
		}
	}
	return g_strndup (path, (gsize) (end - path));
}

ArcFsKind
arc_mount_table_kind (const ArcMountTable *table,
		      const char          *base,
		      const char          *path)
{
	char *base_text = compare_form (base, table->windows);
	char *path_text = compare_form (path, table->windows);
	gssize base_mount = mount_of (table, base_text);
	gssize path_mount = mount_of (table, path_text);
	ArcFsKind kind = ARC_FS_OTHER;

	if (base_mount < 0 && path_mount < 0) {
		char *base_root = root_of (base_text, table->windows);
		char *path_root = root_of (path_text, table->windows);

		if (strcmp (base_root, path_root) == 0) {
			kind = ARC_FS_SAME;
		}
		g_free (base_root);
		g_free (path_root);
	} else if (base_mount < 0 || path_mount < 0) {
		kind = ARC_FS_OTHER;
	} else if (base_mount == path_mount) {
		kind = ARC_FS_SAME;
	} else if (table->windows) {
		/* One volume mounted in 2 places is still one. */
		const char *a = table->mounts[base_mount].device;
		const char *b = table->mounts[path_mount].device;

		if (a != NULL && b != NULL && strcmp (a, b) == 0) {
			kind = ARC_FS_SAME;
		}
	} else {
		const char *a = table->mounts[base_mount].pool;
		const char *b = table->mounts[path_mount].pool;

		if (a != NULL && b != NULL && strcmp (a, b) == 0) {
			kind = ARC_FS_NESTED;
		}
	}

	g_free (base_text);
	g_free (path_text);
	return kind;
}

gboolean
arc_mount_table_is_mount_point (const ArcMountTable *table,
				const char          *path)
{
	char *text = compare_form (path, table->windows);
	gboolean found = FALSE;
	gsize i;

	for (i = 0; i < table->n_mounts && !found; i++) {
		found = strcmp (table->mounts[i].where, text) == 0;
	}
	g_free (text);
	return found;
}

gboolean
arc_target_is_volume (const char *target)
{
	const char *prefix = "\\\\?\\volume{";
	char *text = compare_form (target, TRUE);
	gboolean volume = FALSE;

	/* Nothing after the volume's name: a folder in it is a junction. */
	if (g_str_has_prefix (text, prefix)) {
		const char *guid = text + strlen (prefix);

		volume = strchr (guid, '\\') == NULL && g_str_has_suffix (guid, "}");
	}
	g_free (text);
	return volume;
}

#ifndef G_OS_WIN32
/* Returns: (transfer full): free with arc_mount_table_free */
ArcMountTable *
arc_mount_table_read (void)
{
	GList *mounts = g_unix_mounts_get (NULL);
	guint n = g_list_length (mounts);
	ArcMountEntry *entries = g_new0 (ArcMountEntry, n > 0 ? n : 1);
	ArcMountTable *table;
	GList *l;
	guint i = 0;

	/* The kernel lists them in mount order, so one on top comes later. */
	for (l = mounts; l != NULL; l = l->next, i++) {
		entries[i].mount_path = g_unix_mount_get_mount_path (l->data);
		entries[i].device = g_unix_mount_get_device_path (l->data);
		entries[i].fs_type = g_unix_mount_get_fs_type (l->data);
	}
	table = arc_mount_table_new (entries, n, FALSE);

	g_free (entries);
	g_list_free_full (mounts, (GDestroyNotify) g_unix_mount_free);
	return table;
}
#endif
