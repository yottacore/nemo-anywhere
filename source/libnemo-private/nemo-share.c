/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */

/* nemo-share.c - whether a path is on a network share, without asking it.

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

#include <config.h>
#include "nemo-share.h"

#include <string.h>
#include <gio/gio.h>

#ifdef G_OS_UNIX
#include <gio/gunixmounts.h>
#endif

#ifdef G_OS_WIN32
#include "nemo-link-win32.h"
#endif

/* Reading the table is a file read, so it is done again at most this often
   rather than on every question. */
#define TABLE_TTL_USECS (2 * G_USEC_PER_SEC)

static GMutex     share_lock;
#ifdef G_OS_UNIX
static GPtrArray *table_roots;
#endif
static GPtrArray *test_roots;
static char      *home_root;
static gint64     read_at;
static guint      generation = 1;

#ifdef G_OS_UNIX
/* GLib's own remote list plus the other network and cloud file systems.
   autofs is left out: it fronts local disks as often as shares. */
static const char * const network_types[] = {
	"nfs", "nfs4", "cifs", "smb", "smb2", "smb3", "smbfs", "ncp", "ncpfs",
	"afs", "coda", "ceph", "glusterfs", "lustre", "afpfs", "webdav", "davfs",
	"sshfs", "fuse.sshfs", "fuse.rclone", "fuse.s3fs", "fuse.glusterfs",
	"fuse.davfs2", "fuse.smbnetfs", "fuse.curlftpfs", "fuse.afpfs",
	"fuse.gvfsd-fuse",
	NULL
};
#endif

static gboolean
is_sep (char c)
{
#ifdef G_OS_WIN32
	return c == '/' || c == '\\';
#else
	return c == '/';
#endif
}

static gboolean
same_char (char a, char b)
{
#ifdef G_OS_WIN32
	return (is_sep (a) && is_sep (b)) || g_ascii_tolower (a) == g_ascii_tolower (b);
#else
	return a == b;
#endif
}

/* At or below root, on a whole name: /mnt/nas2 is not under /mnt/nas. */
static gboolean
is_under (const char *path, const char *root)
{
	gsize len = strlen (root);
	gsize i;

	if (len == 0) {
		return FALSE;
	}

	for (i = 0; i < len; i++) {
		if (path[i] == '\0' || !same_char (path[i], root[i])) {
			return FALSE;
		}
	}

	return path[len] == '\0' || is_sep (path[len]) || is_sep (root[len - 1]);
}

static gboolean
same_root (const char *a, const char *b)
{
	return strlen (a) == strlen (b) && is_under (a, b);
}

static const char *
longest_under (GPtrArray *roots, const char *path)
{
	const char *best = NULL;
	guint i;

	for (i = 0; roots != NULL && i < roots->len; i++) {
		const char *root = g_ptr_array_index (roots, i);

		if (is_under (path, root) && (best == NULL || strlen (root) > strlen (best))) {
			best = root;
		}
	}

	return best;
}

#ifdef G_OS_WIN32
static gboolean
has_prefix_caseless (const char *s, const char *prefix)
{
	return g_ascii_strncasecmp (s, prefix, strlen (prefix)) == 0;
}

/* server\share\rest -> \\server\share. Any UNC spelling is a share, even
   one cut short. */
static char *
unc_root (const char *server)
{
	const char *share, *end;
	GString *root = g_string_new ("\\\\");

	share = server;
	while (*share != '\0' && !is_sep (*share)) {
		share++;
	}
	g_string_append_len (root, server, share - server);
	if (*share == '\0') {
		return g_string_free (root, FALSE);
	}

	share++;
	end = share;
	while (*end != '\0' && !is_sep (*end)) {
		end++;
	}
	g_string_append_c (root, '\\');
	g_string_append_len (root, share, end - share);

	return g_string_free (root, FALSE);
}

/* Reparse targets come back in the NT spellings, hence the prefixes. */
static char *
real_root_of (const char *path)
{
	const char *rest = path;

	if (has_prefix_caseless (path, "\\??\\UNC\\") || has_prefix_caseless (path, "\\\\?\\UNC\\") ||
	    has_prefix_caseless (path, "\\\\.\\UNC\\")) {
		return unc_root (path + 8);
	}
	if (g_str_has_prefix (path, "\\??\\") || g_str_has_prefix (path, "\\\\?\\") ||
	    g_str_has_prefix (path, "\\\\.\\")) {
		rest = path + 4;
	} else if (is_sep (path[0]) && is_sep (path[1])) {
		return unc_root (path + 2);
	}

	if (g_ascii_isalpha (rest[0]) && rest[1] == ':' && nemo_win32_drive_is_remote (rest)) {
		return g_strdup_printf ("%c:", g_ascii_toupper (rest[0]));
	}

	return NULL;
}
#else
static char *
real_root_of (const char *path)
{
	return g_strdup (longest_under (table_roots, path));
}

static gint
longest_first (gconstpointer a, gconstpointer b)
{
	gsize la = strlen (*(const char * const *) a);
	gsize lb = strlen (*(const char * const *) b);

	return la < lb ? 1 : la > lb ? -1 : 0;
}

/* A network file system mounted at / would make every path a share, so that
   one is left out. */
static GPtrArray *
read_table (void)
{
	GPtrArray *found = g_ptr_array_new_with_free_func (g_free);
	GList *mounts = g_unix_mounts_get (NULL);
	GList *l;

	for (l = mounts; l != NULL; l = l->next) {
		const char *type = g_unix_mount_get_fs_type (l->data);
		const char *where = g_unix_mount_get_mount_path (l->data);

		if (where != NULL && strcmp (where, "/") != 0 && type != NULL &&
		    g_strv_contains (network_types, type)) {
			g_ptr_array_add (found, g_strdup (where));
		}
	}
	g_list_free_full (mounts, (GDestroyNotify) g_unix_mount_free);
	g_ptr_array_sort (found, longest_first);

	return found;
}

static gboolean
same_list (GPtrArray *a, GPtrArray *b)
{
	guint i;

	if (a == NULL || b == NULL || a->len != b->len) {
		return a == b;
	}
	for (i = 0; i < a->len; i++) {
		if (strcmp (g_ptr_array_index (a, i), g_ptr_array_index (b, i)) != 0) {
			return FALSE;
		}
	}
	return TRUE;
}
#endif

static void
bump_generation (void)
{
	if (++generation == 0) {
		generation = 1;
	}
}

/* Before the home exception. Test roots first, since a test makes its shares
   inside a local scratch folder. */
static char *
raw_root_of (const char *path)
{
	const char *faked = longest_under (test_roots, path);

	if (faked != NULL) {
		return g_strdup (faked);
	}

	return real_root_of (path);
}

static void
refresh_locked (void)
{
	gint64 now = g_get_monotonic_time ();
	char *home;

	if (read_at != 0 && now - read_at < TABLE_TTL_USECS) {
		return;
	}
	read_at = now;

#ifdef G_OS_UNIX
	{
		GPtrArray *fresh = read_table ();

		if (same_list (fresh, table_roots)) {
			g_ptr_array_unref (fresh);
		} else {
			g_clear_pointer (&table_roots, g_ptr_array_unref);
			table_roots = fresh;
			bump_generation ();
		}
	}
#endif

	home = raw_root_of (g_get_home_dir ());
	if (g_strcmp0 (home, home_root) != 0) {
		g_free (home_root);
		home_root = home;
		bump_generation ();
	} else {
		g_free (home);
	}
}

static char *
root_of_locked (const char *path)
{
	char *root;

	refresh_locked ();

	root = raw_root_of (path);
	if (root != NULL && home_root != NULL && same_root (root, home_root)) {
		g_clear_pointer (&root, g_free);
	}

	return root;
}

/* Returns: (transfer full): free with g_free */
char *
nemo_share_root_of (const char *path)
{
	char *root;

	if (path == NULL || *path == '\0') {
		return NULL;
	}

	g_mutex_lock (&share_lock);
	root = root_of_locked (path);
	g_mutex_unlock (&share_lock);

	return root;
}

gboolean
nemo_path_is_on_a_share (const char *path)
{
	g_autofree char *root = nemo_share_root_of (path);

	return root != NULL;
}

gboolean
nemo_share_link_leaves_for_a_share (const char *link_folder,
                                    const char *target)
{
	g_autofree char *full = NULL;
	g_autofree char *target_root = NULL;
	g_autofree char *folder_root = NULL;
	gboolean leaves;

	if (target == NULL || *target == '\0') {
		return FALSE;
	}

	/* Placed by text: the kernel would follow any link on the way, and
	   asking it to is the very visit this avoids. */
#ifdef G_OS_WIN32
	if (g_path_is_absolute (target)) {
		full = g_strdup (target);
	}
#else
	if (g_path_is_absolute (target)) {
		full = g_canonicalize_filename (target, NULL);
	}
#endif
	else if (link_folder != NULL) {
		full = g_canonicalize_filename (target, link_folder);
	} else {
		return FALSE;
	}

	g_mutex_lock (&share_lock);
	target_root = root_of_locked (full);
	if (target_root != NULL && link_folder != NULL) {
		folder_root = root_of_locked (link_folder);
	}
	leaves = target_root != NULL &&
		 (folder_root == NULL || !same_root (target_root, folder_root));
	g_mutex_unlock (&share_lock);

	return leaves;
}

guint
nemo_share_generation (void)
{
	guint now;

	g_mutex_lock (&share_lock);
	refresh_locked ();
	now = generation;
	g_mutex_unlock (&share_lock);

	return now;
}

void
nemo_share_set_roots_for_test (const char * const *roots)
{
	g_mutex_lock (&share_lock);

	g_clear_pointer (&test_roots, g_ptr_array_unref);
	if (roots != NULL) {
		test_roots = g_ptr_array_new_with_free_func (g_free);
		for (; *roots != NULL; roots++) {
			g_ptr_array_add (test_roots, g_strdup (*roots));
		}
	}

	read_at = 0;
	bump_generation ();

	g_mutex_unlock (&share_lock);
}
