/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */

/* nemo-delete-guard.c - what a trash or delete may never take.

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

#include "nemo-delete-guard.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <glib/gi18n.h>
#include <glib/gstdio.h>

#include "nemo-delete-guard-win32.h"
#include "nemo-delete-testguard.h"
#include "nemo-link-copy.h"
#include "nemo-dir-enum.h"
#include "nemo-link-win32.h"

/* Fewer than this taken from home is never a sweep, whatever the share. */
#define SWEEP_MIN_ITEMS 5

#define SETTLE_USEC G_USEC_PER_SEC
#define FOCUSED_AT_KEY "nemo-delete-guard-focused-at"

/* GLib calls a junction or a folder symlink a folder on Windows even when told
   not to follow links, so there the directory entry is asked too. */
gboolean
nemo_delete_guard_is_real_folder (GFile        *file,
				  GFileInfo    *info,
				  GCancellable *cancellable)
{
	GFileType type;
	gboolean real;

	if (info != NULL && g_file_info_has_attribute (info, G_FILE_ATTRIBUTE_STANDARD_TYPE)) {
		type = g_file_info_get_file_type (info);
	} else {
		type = g_file_query_file_type (file, G_FILE_QUERY_INFO_NOFOLLOW_SYMLINKS, cancellable);
	}
	real = type == G_FILE_TYPE_DIRECTORY;

#ifdef G_OS_WIN32
	if (real) {
		const char *path = g_file_peek_path (file);

		real = path == NULL || nemo_win32_link_kind (path) == NEMO_LINK_NONE;
	}
#endif

	return real;
}

#ifndef G_OS_WIN32

typedef struct {
	dev_t dev;
	ino_t ino;
} Node;

/* Home and the folders above it by device and inode, so a path that reaches one
   through a link is caught as surely as the plain spelling. Jobs run on worker
   threads, hence the lock. */
G_LOCK_DEFINE_STATIC (home_nodes);
static char *nodes_for;
static gboolean have_home;
static Node home_node;
static GArray *above_home;

static void
add_parents (const char *start)
{
	char *dir = g_path_get_dirname (start);

	for (;;) {
		GStatBuf st;
		char *parent;

		if (g_stat (dir, &st) == 0) {
			Node node = { st.st_dev, st.st_ino };
			g_array_append_val (above_home, node);
		}

		parent = g_path_get_dirname (dir);
		if (strcmp (parent, dir) == 0) {
			g_free (parent);
			break;
		}
		g_free (dir);
		dir = parent;
	}

	g_free (dir);
}

static void
refresh_home_nodes (void)
{
	const char *home = g_get_home_dir ();
	GStatBuf st;
	char *real;

	if (above_home != NULL && g_strcmp0 (nodes_for, home) == 0) {
		return;
	}

	g_clear_pointer (&above_home, g_array_unref);
	g_free (nodes_for);
	nodes_for = g_strdup (home);
	above_home = g_array_new (FALSE, FALSE, sizeof (Node));

	have_home = g_stat (home, &st) == 0;
	if (have_home) {
		home_node.dev = st.st_dev;
		home_node.ino = st.st_ino;
	}

	add_parents (home);
	real = realpath (home, NULL);
	if (real != NULL) {
		add_parents (real);
		free (real);
	}
}

static gboolean
node_is_home (const GStatBuf *st, gboolean or_above)
{
	gboolean found;
	guint i;

	G_LOCK (home_nodes);
	refresh_home_nodes ();

	found = have_home && home_node.dev == st->st_dev && home_node.ino == st->st_ino;
	for (i = 0; or_above && !found && i < above_home->len; i++) {
		const Node *node = &g_array_index (above_home, Node, i);

		found = node->dev == st->st_dev && node->ino == st->st_ino;
	}

	G_UNLOCK (home_nodes);

	return found;
}

gboolean
nemo_delete_guard_is_protected (GFile *file)
{
	GStatBuf st, parent_st;
	char *path, *parent;
	gboolean protected;

	path = g_file_get_path (file);
	if (path == NULL) {
		return FALSE;
	}

	/* Only a real folder can be any of these. A link to one is just a link. */
	if (g_lstat (path, &st) != 0 || !S_ISDIR (st.st_mode)) {
		g_free (path);
		return FALSE;
	}

	protected = node_is_home (&st, TRUE);

	if (!protected) {
		/* A mount is on another device from the folder holding it, and the
		   root is its own parent. The parent is followed, because it may be
		   a link to another file system with this folder really inside it. */
		parent = g_path_get_dirname (path);
		protected = strcmp (parent, path) == 0 ||
			    g_stat (parent, &parent_st) != 0 ||
			    parent_st.st_dev != st.st_dev;
		g_free (parent);
	}

	g_free (path);

	return protected;
}

static gboolean
dir_is_home (GFile *dir)
{
	GStatBuf st;
	char *path;
	gboolean home;

	path = g_file_get_path (dir);
	home = path != NULL && g_stat (path, &st) == 0 && node_is_home (&st, FALSE);
	g_free (path);

	return home;
}

#else /* G_OS_WIN32 */

/* A volume mounted inside a folder is not caught. */
gboolean
nemo_delete_guard_is_protected (GFile *file)
{
	char *path, *canon;
	const char *rest;
	gboolean protected;

	if (!nemo_delete_guard_is_real_folder (file, NULL, NULL)) {
		return FALSE;
	}

	path = g_file_get_path (file);
	if (path == NULL) {
		return FALSE;
	}

	canon = g_canonicalize_filename (path, NULL);
	rest = g_path_skip_root (canon);
	protected = rest == NULL || *rest == '\0' || nemo_delete_guard_win32_is_home (canon, TRUE);

	g_free (canon);
	g_free (path);

	return protected;
}

static gboolean
dir_is_home (GFile *dir)
{
	char *path;
	gboolean home;

	path = g_file_get_path (dir);
	home = path != NULL && nemo_delete_guard_win32_is_home (path, FALSE);
	g_free (path);

	return home;
}

#endif /* G_OS_WIN32 */

gboolean
nemo_delete_guard_check (GFile *file, GError **error)
{
	char *name;

	if (!nemo_delete_guard_is_protected (file)) {
		return TRUE;
	}

	name = g_file_get_parse_name (file);
	nemo_delete_guard_log ("refused to remove %s", name);
	g_set_error (error, G_IO_ERROR, G_IO_ERROR_PERMISSION_DENIED,
		     _("\"%s\" is the home folder, a folder above it, or where a drive is mounted. It is never removed."),
		     name);
	g_free (name);

	return FALSE;
}

gboolean
nemo_delete_guard_remove_link (GFile *file, GError **error)
{
	NemoLinkKind kind;
	char *path, *name;
	int result, saved;

	if (!nemo_delete_guard_check (file, error)) {
		return FALSE;
	}

	kind = nemo_link_kind (file, NULL);
	path = g_file_get_path (file);
	name = g_file_get_parse_name (file);
	if (kind == NEMO_LINK_NONE || path == NULL || nemo_delete_guard_is_real_folder (file, NULL, NULL)) {
		nemo_delete_guard_log ("refused to remove %s as a link, since it is not one", name);
		g_set_error (error, G_IO_ERROR, G_IO_ERROR_NOT_SUPPORTED,
			     _("\"%s\" is not a link, so it was left alone."), name);
		g_free (path);
		g_free (name);
		return FALSE;
	}

#ifdef G_OS_WIN32
	/* A folder link is a folder to Windows. Removing it takes the link and
	   nothing under it. */
	result = kind == NEMO_LINK_FILE_SYMLINK ? g_unlink (path) : g_rmdir (path);
#else
	result = g_unlink (path);
#endif
	saved = errno;
	if (result != 0) {
		g_set_error (error, G_IO_ERROR, g_io_error_from_errno (saved),
			     _("Could not remove the link \"%s\": %s"), name, g_strerror (saved));
	} else {
		nemo_delete_guard_log ("removed the link %s", name);
	}
	g_free (path);
	g_free (name);

	return result == 0;
}

gboolean
nemo_delete_guard_remove_tree (GFile *file, GCancellable *cancellable)
{
	GFileEnumerator *children;
	GFileInfo *info;

	if (!nemo_delete_guard_check (file, NULL)) {
		return FALSE;
	}

	/* Asked once for the whole tree: the recursion below sees the mark and
	   stays quiet. */
	if (!nemo_delete_testguard_ask_one ("Delete tree", file)) {
		return FALSE;
	}
	nemo_delete_testguard_begin ();

	/* NOFOLLOW on the enumerate covers the children only. Opening a link to a
	   folder still lists what it points at, so only a real folder is walked. */
	if (nemo_delete_guard_is_real_folder (file, NULL, cancellable)) {
		children = nemo_enumerate_children (file, G_FILE_ATTRIBUTE_STANDARD_NAME,
						    G_FILE_QUERY_INFO_NOFOLLOW_SYMLINKS,
						    cancellable, NULL);
		while (children != NULL &&
		       (info = g_file_enumerator_next_file (children, cancellable, NULL)) != NULL) {
			GFile *child = g_file_get_child (file, g_file_info_get_name (info));

			nemo_delete_guard_remove_tree (child, cancellable);
			g_object_unref (child);
			g_object_unref (info);
		}

		if (children != NULL) {
			g_file_enumerator_close (children, cancellable, NULL);
			g_object_unref (children);
		}
	}

	nemo_delete_testguard_end ();

	return g_file_delete (file, cancellable, NULL);
}

/* Most of home in one job is refused rather than asked about: nobody clears a
   home folder from a file manager on purpose, and a question is one Enter away
   from yes. Hidden entries are counted apart as well, so the share comes out
   the same whether or not the window was showing them. */
gboolean
nemo_delete_guard_sweeps_home (GList *files)
{
	GHashTable *taken;
	GFile *last_parent = NULL, *home;
	GFileEnumerator *children;
	GFileInfo *info;
	gboolean last_was_home = FALSE, sweeps;
	guint all_present = 0, all_taken = 0, shown_present = 0, shown_taken = 0;
	GList *l;

	taken = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);

	for (l = files; l != NULL; l = l->next) {
		GFile *parent = g_file_get_parent (l->data);

		if (parent == NULL) {
			continue;
		}

		if (last_parent == NULL || !g_file_equal (parent, last_parent)) {
			last_was_home = dir_is_home (parent);
			g_clear_object (&last_parent);
			last_parent = g_object_ref (parent);
		}

		if (last_was_home) {
			g_hash_table_add (taken, g_file_get_basename (l->data));
		}

		g_object_unref (parent);
	}

	g_clear_object (&last_parent);

	if (g_hash_table_size (taken) < SWEEP_MIN_ITEMS) {
		g_hash_table_unref (taken);
		return FALSE;
	}

	home = g_file_new_for_path (g_get_home_dir ());
	children = nemo_enumerate_children (home,
					    G_FILE_ATTRIBUTE_STANDARD_NAME ","
					    G_FILE_ATTRIBUTE_STANDARD_IS_HIDDEN,
					    G_FILE_QUERY_INFO_NOFOLLOW_SYMLINKS,
					    NULL, NULL);

	while (children != NULL &&
	       (info = g_file_enumerator_next_file (children, NULL, NULL)) != NULL) {
		gboolean shown = !g_file_info_get_is_hidden (info);
		gboolean is_taken = g_hash_table_contains (taken, g_file_info_get_name (info));

		all_present++;
		all_taken += is_taken;
		shown_present += shown;
		shown_taken += shown && is_taken;

		g_object_unref (info);
	}

	if (children != NULL) {
		g_file_enumerator_close (children, NULL, NULL);
		g_object_unref (children);
	}

	sweeps = (all_taken >= SWEEP_MIN_ITEMS && all_taken * 2 >= all_present) ||
		 (shown_taken >= SWEEP_MIN_ITEMS && shown_taken * 2 >= shown_present);

	g_object_unref (home);
	g_hash_table_unref (taken);

	return sweeps;
}

/* The two confirmation preferences are the person's to turn off. This is not:
   a job no trash or delete command asked for, or one big enough that a slip
   takes a whole folder, asks regardless. */
gboolean
nemo_delete_guard_must_ask (gboolean by_user, guint count, gint many)
{
	return !by_user || (many > 0 && count >= (guint) many);
}

gboolean
nemo_delete_guard_in_grace (gint64 focused_at, gint64 now)
{
	return focused_at > 0 && now >= focused_at && now - focused_at < SETTLE_USEC;
}

static gboolean
note_focus (GtkWidget *widget, G_GNUC_UNUSED GdkEvent *event, G_GNUC_UNUSED gpointer user_data)
{
	gint64 *focused_at = g_object_get_data (G_OBJECT (widget), FOCUSED_AT_KEY);

	if (focused_at != NULL) {
		*focused_at = g_get_monotonic_time ();
	}

	return GDK_EVENT_PROPAGATE;
}

void
nemo_delete_guard_watch_window (GtkWindow *window)
{
	g_object_set_data_full (G_OBJECT (window), FOCUSED_AT_KEY, g_new0 (gint64, 1), g_free);
	g_signal_connect (window, "map-event", G_CALLBACK (note_focus), NULL);
	g_signal_connect (window, "focus-in-event", G_CALLBACK (note_focus), NULL);
}

gboolean
nemo_delete_guard_key_too_soon (GtkWidget *widget)
{
	GtkWidget *toplevel;
	GdkEvent *event;
	gint64 *focused_at;
	gboolean keyed;

	event = gtk_get_current_event ();
	keyed = event != NULL &&
		(gdk_event_get_event_type (event) == GDK_KEY_PRESS ||
		 gdk_event_get_event_type (event) == GDK_KEY_RELEASE);
	if (event != NULL) {
		gdk_event_free (event);
	}

	if (!keyed) {
		return FALSE;
	}

	toplevel = gtk_widget_get_toplevel (widget);
	focused_at = toplevel != NULL ? g_object_get_data (G_OBJECT (toplevel), FOCUSED_AT_KEY) : NULL;

	if (focused_at == NULL || !nemo_delete_guard_in_grace (*focused_at, g_get_monotonic_time ())) {
		return FALSE;
	}

	nemo_delete_guard_log ("ignored a delete key right after the window took focus");

	return TRUE;
}

void
nemo_delete_guard_log (const char *format, ...)
{
	va_list args;
	char *message;

	va_start (args, format);
	message = g_strdup_vprintf (format, args);
	va_end (args);

	g_message ("%s", message);

	/* stderr is usually a log under home, which goes with home. The journal
	   does not. Skipped when stderr already is the journal. */
	if (!g_log_writer_is_journald (fileno (stderr))) {
		const GLogField fields[] = {
			{ "MESSAGE", message, -1 },
			{ "PRIORITY", "5", -1 },
			{ "SYSLOG_IDENTIFIER", g_get_prgname () != NULL ? g_get_prgname () : "nemo-anywhere", -1 },
		};

		g_log_writer_journald (G_LOG_LEVEL_MESSAGE, fields, G_N_ELEMENTS (fields), NULL);
	}

	g_free (message);
}
