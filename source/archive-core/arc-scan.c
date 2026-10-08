/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */

/* arc-scan.c - the walk behind the size totals.

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

#include "arc-entry.h"
#include "arc-scan.h"

#define REPORT_GAP_USECS  (G_USEC_PER_SEC / 4)
#define PUBLISH_GAP_USECS (G_USEC_PER_SEC / 20)
/* Each folder being walked holds its listing open, which is a file
   descriptor on most systems. Past this many, the one furthest up is read
   to the end and closed. */
#define OPEN_LEVELS 32
/* What Linux allows; Windows allows 63. */
#define MAX_HOPS 40

#define ATTRIBUTES "standard::name,standard::type,standard::size,standard::is-symlink," \
		   "standard::symlink-target,id::file,dos::reparse-point-tag"
/* Answered from the folder alone, so nothing in it is looked at. */
#define NAMES_ONLY "standard::name,standard::type"
#define TARGET_ATTRIBUTES "standard::type,standard::size,id::file"

typedef struct {
	char    *text;		/* the canonical path its files go by */
	guint16  walked;	/* bit n: walked this pass by a path needing n */
} Folder;

typedef struct {
	GFile           *dir;	/* where it really is */
	const char      *text;	/* its Folder's */
	guint            needs;
	gboolean         names_only;
	GFileEnumerator *children;	/* NULL once read to the end */
	GQueue           ahead;
} Frame;

typedef struct {
	GFile      *dir;
	const char *text;
	char       *name;
} Root;

typedef enum {
	PASS_DONE,
	PASS_WIDER,
	PASS_CANCELLED,
} Pass;

typedef enum {
	LEADS_HERE,
	LEADS_NOWHERE,
	LEADS_TO_SHARE,
} Leads;

struct _ArcScan {
	ArcHost          host;
	char            *base;
	char           **paths;
	gboolean         shares;
	ArcMountTable   *mounts;
	GCancellable    *cancellable;

	/* The scan's thread only. */
	gboolean         prepared;
	Root            *roots;
	guint            n_roots;
	ArcPathList     *list;
	GHashTable      *folders;	/* identity -> Folder */
	GHashTable      *share_mounts;	/* shares mounted in a folder */
	GHashTable      *share_parents;	/* the folders they're in */
	GPtrArray       *stack;
	GString         *text;
	guint            follow;	/* what this pass walks by */
	guint            visits;
	gint64           published_at;

	/* Under the lock. wanted is read without it too. */
	GMutex           lock;
	gint             wanted;
	guint            covered;	/* bit n: a pass by n finished */
	gboolean         running;
	GHashTable      *left_shares;	/* path -> needs seen, as bits */
	GHashTable      *unreadable;	/* the same */
	ArcScanReport    snapshot;
	guint            serial;
	guint            sent_serial;

	/* The starting thread's. */
	GThread         *thread;
	GMainContext    *context;
	GSource         *timer;
	gint64           sent_at;
	ArcScanReported  reported;
	gpointer         reported_data;
};

typedef struct {
	GSource  source;
	ArcScan *scan;
} ReportSource;

static gboolean
is_separator (char c)
{
#ifdef G_OS_WIN32
	return c == '\\' || c == '/';
#else
	return c == '/';
#endif
}

static void
set_child_text (GString    *text,
		const char *folder,
		const char *name)
{
	g_string_assign (text, folder);
	if (text->len == 0 || !is_separator (text->str[text->len - 1])) {
		g_string_append_c (text, G_DIR_SEPARATOR);
	}
	g_string_append (text, name);
}

/* Returns: (transfer full) */
static char *
child_text (const char *folder,
	    const char *name)
{
	GString *text = g_string_new (NULL);

	set_child_text (text, folder, name);
	return g_string_free (text, FALSE);
}

/* A mix in mixes lets in everything one that follows less would. */
static gboolean
is_covered (guint mixes,
	    guint follow)
{
	guint mix;

	for (mix = 0; mix < ARC_MIX_COUNT; mix++) {
		if ((mixes & (1u << mix)) && (follow & ~mix) == 0) {
			return TRUE;
		}
	}
	return FALSE;
}

/* A path needing no more than this one already walked the folder. */
static gboolean
walked_by_less (guint walked,
		guint needs)
{
	guint mix;

	for (mix = 0; mix < ARC_MIX_COUNT; mix++) {
		if ((walked & (1u << mix)) && (mix & ~needs) == 0) {
			return TRUE;
		}
	}
	return FALSE;
}

/* Where an absolute path starts, and how much of the text that is. \x is
   on the drive of from, and C:x can't be placed. */
static gboolean
find_root (const char  *text,
	   const char  *from,
	   char       **root,
	   gsize       *skip)
{
	*root = NULL;
	*skip = 0;
#ifdef G_OS_WIN32
	if (g_ascii_isalpha (text[0]) && text[1] == ':') {
		if (!is_separator (text[2])) {
			return FALSE;
		}
		*root = g_strdup_printf ("%c:\\", text[0]);
		*skip = 3;
	} else if (is_separator (text[0]) && is_separator (text[1])) {
		const char *end = text + 2;
		int parts = 0;

		for (; *end != '\0'; end++) {
			if (is_separator (*end) && ++parts == 2) {
				break;
			}
		}
		if (parts == 0) {
			return FALSE;
		}
		*root = g_strndup (text, (gsize) (end - text));
		*skip = (gsize) (end - text);
	} else if (is_separator (text[0])) {
		if (from == NULL || !g_ascii_isalpha (from[0]) || from[1] != ':') {
			return FALSE;
		}
		*root = g_strdup_printf ("%c:\\", from[0]);
		*skip = 1;
	}
#else
	(void) from;
	if (text[0] == '/') {
		*root = g_strdup ("/");
		*skip = 1;
	}
#endif
	return TRUE;
}

static void
push_parts (GQueue     *parts,
	    const char *text,
	    const char *separators)
{
	char **split = g_strsplit_set (text, separators, -1);
	int i;

	for (i = (int) g_strv_length (split) - 1; i >= 0; i--) {
		if (split[i][0] != '\0') {
			g_queue_push_head (parts, split[i]);
		} else {
			g_free (split[i]);
		}
	}
	g_free (split);
}

/* Sets out to walk text from walked, the folder a link is in. */
static gboolean
start_at (const char  *text,
	  char       **walked,
	  gsize       *root_length,
	  GQueue      *parts)
{
	char *root;
	gsize skip;

	if (!find_root (text, *walked, &root, &skip)) {
		return FALSE;
	}
	if (root != NULL) {
		g_free (*walked);
		*walked = root;
		*root_length = strlen (root);
		push_parts (parts, text + skip, G_DIR_SEPARATOR_S "/");
	} else if (*walked == NULL) {
		return FALSE;
	} else {
		/* Only \ splits a relative one on Windows, as only \ does for
		   Windows itself. */
		push_parts (parts, text, G_DIR_SEPARATOR_S);
	}
	return TRUE;
}

/* Returns: (transfer full) */
static char *
left_to_walk (const char *walked,
	      GQueue     *parts)
{
	GString *text = g_string_new (walked);
	GList *l;

	for (l = parts->head; l != NULL; l = l->next) {
		if (text->len == 0 || !is_separator (text->str[text->len - 1])) {
			g_string_append_c (text, G_DIR_SEPARATOR);
		}
		g_string_append (text, l->data);
	}
	return g_string_free (text, FALSE);
}

/* Where target really is, read a name at a time so no link on the way is
   gone through before it's asked about: each one that leads off is checked
   for a share, from folder, the one the first link is in. NULL folder for
   an absolute target. On a share, real is the text that leads there. */
static Leads
resolve (ArcScan     *scan,
	 const char  *folder,
	 const char  *target,
	 gboolean     check_shares,
	 char       **real)
{
	GQueue parts = G_QUEUE_INIT;
	char *walked = g_strdup (folder);
	char *root = NULL;
	gsize root_length = 0;
	gsize skip;
	char *part;
	int hops = 0;
	Leads leads = LEADS_HERE;

	*real = NULL;
	if (folder != NULL && find_root (folder, NULL, &root, &skip) && root != NULL) {
		root_length = strlen (root);
	}
	g_free (root);
	if (!start_at (target, &walked, &root_length, &parts)) {
		leads = LEADS_NOWHERE;
	}

	while (leads == LEADS_HERE && (part = g_queue_pop_head (&parts)) != NULL) {
		char *candidate;
		char *next = NULL;

		if (strcmp (part, ".") == 0) {
			g_free (part);
			continue;
		}
		if (strcmp (part, "..") == 0) {
			if (strlen (walked) > root_length) {
				char *up = g_path_get_dirname (walked);

				if (strlen (up) >= root_length) {
					g_free (walked);
					walked = up;
				} else {
					g_free (up);
				}
			}
			g_free (part);
			continue;
		}
		if (strchr (part, '/') != NULL) {
			/* Windows reads a relative link's / as part of a name, and
			   no name has one. */
			g_free (part);
			leads = LEADS_NOWHERE;
			break;
		}

		candidate = child_text (walked, part);
		g_free (part);
		switch (arc_entry_read (candidate, &next)) {
		case ARC_ENTRY_PLAIN:
		case ARC_ENTRY_MOUNT_POINT:
			g_free (walked);
			walked = candidate;
			break;
		case ARC_ENTRY_SYMLINK:
		case ARC_ENTRY_JUNCTION:
			g_free (candidate);
			if (++hops > MAX_HOPS || !start_at (next, &walked, &root_length, &parts)) {
				leads = LEADS_NOWHERE;
			} else if (check_shares) {
				char *rest = left_to_walk (walked, &parts);

				if (arc_leaves_for_share (&scan->host, folder, rest)) {
					leads = LEADS_TO_SHARE;
					*real = rest;
					rest = NULL;
				}
				g_free (rest);
			}
			break;
		case ARC_ENTRY_MISSING:
		default:
			g_free (candidate);
			leads = LEADS_NOWHERE;
			break;
		}
		g_free (next);
	}

	g_queue_clear_full (&parts, g_free);
	if (leads == LEADS_HERE && walked != NULL) {
		*real = walked;
	} else {
		g_free (walked);
		if (leads == LEADS_HERE) {
			leads = LEADS_NOWHERE;
		}
	}
	return leads;
}

/* Returns: (transfer full) */
static char *
identity_of (GFileInfo  *info,
	     const char *path)
{
	const char *id = g_file_info_get_attribute_string (info, G_FILE_ATTRIBUTE_ID_FILE);

	/* Without one a loop through a mount still ends, by the path text
	   growing past what the system takes. */
	if (id == NULL || id[0] == '\0') {
		return g_strconcat ("path:", path, NULL);
	}
	return g_strdup (id);
}

static void
folder_free (gpointer data)
{
	Folder *folder = data;

	g_free (folder->text);
	g_free (folder);
}

/* The first text a folder is found by is the one it keeps, so a folder
   reached by 2 paths, through a link or a mount, puts its files in the list
   once. Returns: (transfer none) */
static Folder *
folder_for (ArcScan    *scan,
	    const char *identity,
	    const char *text)
{
	Folder *folder = g_hash_table_lookup (scan->folders, identity);

	if (folder == NULL) {
		folder = g_new0 (Folder, 1);
		folder->text = g_strdup (text);
		g_hash_table_insert (scan->folders, g_strdup (identity), folder);
	}
	return folder;
}

/* Returns: (transfer none) */
static Folder *
folder_at (ArcScan *scan,
	   GFile   *dir)
{
	GFileInfo *info = g_file_query_info (dir, G_FILE_ATTRIBUTE_ID_FILE, G_FILE_QUERY_INFO_NONE,
					     scan->cancellable, NULL);
	const char *path = g_file_peek_path (dir);
	char *identity = info != NULL ? identity_of (info, path) : g_strconcat ("path:", path, NULL);
	Folder *folder = folder_for (scan, identity, path);

	g_free (identity);
	g_clear_object (&info);
	return folder;
}

static guint
crossing (ArcScan    *scan,
	  const char *path)
{
	switch (arc_mount_table_kind (scan->mounts, scan->base, path)) {
	case ARC_FS_NESTED:
		return ARC_FOLLOW_NESTED_FS;
	case ARC_FS_OTHER:
		return ARC_FOLLOW_OTHER_FS;
	case ARC_FS_SAME:
	default:
		return 0;
	}
}

static guint
count_left_out (GHashTable *table,
		guint       follow)
{
	GHashTableIter iter;
	gpointer value;
	guint count = 0;

	g_hash_table_iter_init (&iter, table);
	while (g_hash_table_iter_next (&iter, NULL, &value)) {
		guint mix;

		for (mix = 0; mix < ARC_MIX_COUNT; mix++) {
			if ((GPOINTER_TO_UINT (value) & (1u << mix)) && (mix & ~follow) == 0) {
				count++;
				break;
			}
		}
	}
	return count;
}

/* Kept by path and by what the path needs, so the count is right for
   whatever the options are later set to. */
static void
leave_out (ArcScan    *scan,
	   GHashTable *table,
	   const char *path,
	   guint       needs)
{
	gpointer value;
	guint bits;

	g_mutex_lock (&scan->lock);
	bits = g_hash_table_lookup_extended (table, path, NULL, &value) ? GPOINTER_TO_UINT (value) : 0;
	bits |= 1u << needs;
	g_hash_table_insert (table, g_strdup (path), GUINT_TO_POINTER (bits));
	g_mutex_unlock (&scan->lock);
}

/* Counted only when the options would follow it if it weren't on a share.
   Where it leads is told from the text, since it isn't visited. */
static void
leave_out_share (ArcScan    *scan,
		 const char *folder,
		 const char *target,
		 guint       needs)
{
	char *full = g_canonicalize_filename (target, folder);

	needs |= crossing (scan, full);
	if ((needs & ~scan->follow) == 0) {
		leave_out (scan, scan->left_shares, full, needs);
	}
	g_free (full);
}

static void
fill_counts_locked (ArcScan       *scan,
		    ArcScanReport *report)
{
	guint want = (guint) g_atomic_int_get (&scan->wanted);

	report->follow = want;
	report->shares = count_left_out (scan->left_shares, want);
	report->unreadable = count_left_out (scan->unreadable, want);
	report->done = is_covered (scan->covered, want);
}

static gboolean
same_report (const ArcScanReport *a,
	     const ArcScanReport *b)
{
	return memcmp (a->totals, b->totals, sizeof a->totals) == 0 &&
	       a->follow == b->follow && a->files == b->files && a->shares == b->shares &&
	       a->unreadable == b->unreadable && a->done == b->done;
}

static void
publish (ArcScan  *scan,
	 gboolean  pass_done)
{
	ArcScanReport report;

	memset (&report, 0, sizeof report);
	arc_path_list_totals (scan->list, report.totals);
	report.files = arc_path_list_count (scan->list);

	g_mutex_lock (&scan->lock);
	if (pass_done) {
		scan->covered |= 1u << scan->follow;
	}
	fill_counts_locked (scan, &report);
	if (!same_report (&report, &scan->snapshot)) {
		scan->snapshot = report;
		scan->serial++;
	}
	g_mutex_unlock (&scan->lock);

	scan->published_at = g_get_monotonic_time ();
}

static void
maybe_publish (ArcScan *scan)
{
	if ((++scan->visits & 63) == 0 &&
	    g_get_monotonic_time () - scan->published_at >= PUBLISH_GAP_USECS) {
		publish (scan, FALSE);
	}
}

static void
add_file (ArcScan    *scan,
	  const char *folder_text,
	  const char *name,
	  guint64     bytes,
	  guint       needs)
{
	set_child_text (scan->text, folder_text, name);
	arc_path_list_add (scan->list, scan->text->str, bytes, needs);
}

static void
frame_free (gpointer data)
{
	Frame *frame = data;

	if (frame->children != NULL) {
		g_file_enumerator_close (frame->children, NULL, NULL);
		g_object_unref (frame->children);
	}
	g_queue_clear_full (&frame->ahead, g_object_unref);
	g_object_unref (frame->dir);
	g_free (frame);
}

static void
read_ahead (ArcScan *scan,
	    Frame   *frame)
{
	GFileInfo *info;

	if (frame->children == NULL) {
		return;
	}
	while ((info = g_file_enumerator_next_file (frame->children, scan->cancellable, NULL)) != NULL) {
		g_queue_push_tail (&frame->ahead, info);
	}
	g_file_enumerator_close (frame->children, NULL, NULL);
	g_clear_object (&frame->children);
}

/* Returns: (transfer full) */
static GFileInfo *
next_child (ArcScan *scan,
	    Frame   *frame)
{
	if (frame->children != NULL) {
		return g_file_enumerator_next_file (frame->children, scan->cancellable, NULL);
	}
	return g_queue_pop_head (&frame->ahead);
}

static void
enter (ArcScan    *scan,
       GFile      *dir,
       const char *identity,
       const char *text,
       guint       needs)
{
	Folder *folder = folder_for (scan, identity, text);
	const char *path = g_file_peek_path (dir);
	GFileEnumerator *children;
	gboolean names_only;
	Frame *frame;

	if (walked_by_less (folder->walked, needs)) {
		return;
	}
	folder->walked |= (guint16) (1u << needs);

	names_only = !scan->shares && g_hash_table_contains (scan->share_parents, path);
	children = arc_walk_children (&scan->host, dir, names_only ? NAMES_ONLY : ATTRIBUTES,
				      G_FILE_QUERY_INFO_NOFOLLOW_SYMLINKS, scan->cancellable, NULL);
	if (children == NULL) {
		if (!g_cancellable_is_cancelled (scan->cancellable)) {
			leave_out (scan, scan->unreadable, path, needs);
		}
		return;
	}

	frame = g_new0 (Frame, 1);
	frame->dir = g_object_ref (dir);
	frame->text = folder->text;
	frame->needs = needs;
	frame->names_only = names_only;
	frame->children = children;
	g_queue_init (&frame->ahead);
	g_ptr_array_add (scan->stack, frame);

	if (scan->stack->len > OPEN_LEVELS) {
		read_ahead (scan, g_ptr_array_index (scan->stack, scan->stack->len - 1 - OPEN_LEVELS));
	}
}

/* A folder found in a listing, or on Windows one a volume is mounted at,
   which has no info of what's mounted there yet. */
static void
enter_child (ArcScan    *scan,
	     Frame      *frame,
	     GFile      *child,
	     const char *name,
	     GFileInfo  *info)
{
	const char *path = g_file_peek_path (child);
	guint needs = frame->needs;
	GFileInfo *mounted = NULL;
	char *identity;
	char *text;

	/* A share mounted here never gets this far: see visit_by_name. */
	if (info == NULL || arc_mount_table_is_mount_point (scan->mounts, path)) {
		needs |= crossing (scan, path);
	}
	if (needs & ~scan->follow) {
		return;
	}

	if (info == NULL) {
		mounted = g_file_query_info (child, G_FILE_ATTRIBUTE_ID_FILE, G_FILE_QUERY_INFO_NONE,
					     scan->cancellable, NULL);
		if (mounted == NULL) {
			leave_out (scan, scan->unreadable, path, needs);
			return;
		}
		info = mounted;
	}

	identity = identity_of (info, path);
	text = child_text (frame->text, name);
	enter (scan, child, identity, text, needs);
	g_free (text);
	g_free (identity);
	g_clear_object (&mounted);
}

static void
add_target_file (ArcScan   *scan,
		 GFile     *file,
		 GFileInfo *info,
		 guint      needs)
{
	GFile *parent = g_file_get_parent (file);
	char *name = g_file_get_basename (file);

	if (parent != NULL && name != NULL) {
		Folder *folder = folder_at (scan, parent);

		add_file (scan, folder->text, name, (guint64) g_file_info_get_size (info), needs);
	}
	g_free (name);
	g_clear_object (&parent);
}

static void
follow_link (ArcScan    *scan,
	     Frame      *frame,
	     guint       kind,
	     const char *target)
{
	const char *folder = g_file_peek_path (frame->dir);
	guint needs = frame->needs | kind;
	GFileInfo *info;
	GFile *file;
	char *real = NULL;
	char *identity;

	/* Ignore and Store both leave out where it leads. */
	if (needs & ~scan->follow) {
		return;
	}
	if (!scan->shares && arc_leaves_for_share (&scan->host, folder, target)) {
		leave_out_share (scan, folder, target, needs);
		return;
	}
	switch (resolve (scan, folder, target, !scan->shares, &real)) {
	case LEADS_TO_SHARE:
		leave_out_share (scan, folder, real, needs);
		g_free (real);
		return;
	case LEADS_NOWHERE:
		return;
	case LEADS_HERE:
	default:
		break;
	}

	needs |= crossing (scan, real);
	if (needs & ~scan->follow) {
		g_free (real);
		return;
	}

	file = g_file_new_for_path (real);
	info = g_file_query_info (file, TARGET_ATTRIBUTES, G_FILE_QUERY_INFO_NONE, scan->cancellable, NULL);
	if (info != NULL && g_file_info_get_file_type (info) == G_FILE_TYPE_DIRECTORY) {
		identity = identity_of (info, real);
		enter (scan, file, identity, real, needs);
		g_free (identity);
	} else if (info != NULL) {
		add_target_file (scan, file, info, needs);
	}
	g_clear_object (&info);
	g_object_unref (file);
	g_free (real);
}

static gboolean
might_be_link (GFileInfo *info)
{
#ifdef G_OS_WIN32
	/* GLib can call a junction a folder even when not following links, and
	   also a mount point a symlink, so the reparse point says which. A .lnk is
	   none of them. */
	return g_file_info_get_is_symlink (info) ||
	       g_file_info_get_file_type (info) == G_FILE_TYPE_SYMBOLIC_LINK ||
	       g_file_info_get_file_type (info) == G_FILE_TYPE_DIRECTORY ||
	       g_file_info_get_attribute_uint32 (info, G_FILE_ATTRIBUTE_DOS_REPARSE_POINT_TAG) != 0;
#else
	return g_file_info_get_file_type (info) == G_FILE_TYPE_SYMBOLIC_LINK;
#endif
}

static ArcEntry
entry_of (GFileInfo   *info,
	  const char  *path,
	  char       **target)
{
#ifdef G_OS_WIN32
	(void) info;
	return arc_entry_read (path, target);
#else
	const char *text = g_file_info_get_symlink_target (info);

	(void) path;
	if (text == NULL) {
		return ARC_ENTRY_MISSING;
	}
	*target = g_strdup (text);
	return ARC_ENTRY_SYMLINK;
#endif
}

static void
visit (ArcScan   *scan,
       Frame     *frame,
       GFileInfo *info)
{
	const char *name = g_file_info_get_name (info);
	gboolean link = might_be_link (info);
	char *target = NULL;
	GFile *child;

	if (name == NULL) {
		return;
	}
	if (!link && g_file_info_get_file_type (info) != G_FILE_TYPE_DIRECTORY) {
		add_file (scan, frame->text, name, (guint64) g_file_info_get_size (info), frame->needs);
		return;
	}
	child = g_file_get_child (frame->dir, name);

	switch (link ? entry_of (info, g_file_peek_path (child), &target) : ARC_ENTRY_PLAIN) {
	case ARC_ENTRY_SYMLINK:
		follow_link (scan, frame, ARC_FOLLOW_SYMLINKS, target);
		break;
	case ARC_ENTRY_JUNCTION:
		follow_link (scan, frame, ARC_FOLLOW_JUNCTIONS, target);
		break;
	case ARC_ENTRY_MOUNT_POINT:
		enter_child (scan, frame, child, name, NULL);
		break;
	case ARC_ENTRY_PLAIN:
		if (g_file_info_get_file_type (info) == G_FILE_TYPE_DIRECTORY) {
			enter_child (scan, frame, child, name, info);
		} else {
			add_file (scan, frame->text, name, (guint64) g_file_info_get_size (info), frame->needs);
		}
		break;
	case ARC_ENTRY_MISSING:
	default:
		break;
	}

	g_free (target);
	g_object_unref (child);
}

/* For a folder with a share mounted in it, whose listing gave names only,
   and for the selection itself: the share is left out unlooked at. */
static void
visit_by_name (ArcScan    *scan,
	       Frame      *frame,
	       const char *name)
{
	GFile *child = g_file_get_child (frame->dir, name);
	const char *path = g_file_peek_path (child);

	if (!scan->shares && g_hash_table_contains (scan->share_mounts, path)) {
		guint needs = frame->needs | crossing (scan, path);

		if ((needs & ~scan->follow) == 0) {
			leave_out (scan, scan->left_shares, path, needs);
		}
	} else {
		GFileInfo *info = g_file_query_info (child, ATTRIBUTES, G_FILE_QUERY_INFO_NOFOLLOW_SYMLINKS,
						     scan->cancellable, NULL);

		if (info != NULL) {
			visit (scan, frame, info);
			g_object_unref (info);
		}
	}
	g_object_unref (child);
}

static Pass
drain (ArcScan *scan)
{
	Pass end = PASS_DONE;

	while (scan->stack->len > 0) {
		Frame *top;
		GFileInfo *info;
		guint want;

		if (g_cancellable_is_cancelled (scan->cancellable)) {
			end = PASS_CANCELLED;
			break;
		}
		want = (guint) g_atomic_int_get (&scan->wanted);
		if (want != scan->follow) {
			if (want & ~scan->follow) {
				end = PASS_WIDER;
				break;
			}
			/* Less: back out of what it leaves out, below. */
			scan->follow = want;
			publish (scan, FALSE);
		}

		top = g_ptr_array_index (scan->stack, scan->stack->len - 1);
		if (top->needs & ~scan->follow) {
			g_ptr_array_set_size (scan->stack, scan->stack->len - 1);
			continue;
		}
		info = next_child (scan, top);
		if (info == NULL) {
			g_ptr_array_set_size (scan->stack, scan->stack->len - 1);
			continue;
		}

		if (top->names_only) {
			visit_by_name (scan, top, g_file_info_get_name (info));
		} else {
			visit (scan, top, info);
		}
		g_object_unref (info);
		maybe_publish (scan);
	}

	g_ptr_array_set_size (scan->stack, 0);
	return end;
}

static void
forget_walked (gpointer key,
	       gpointer value,
	       gpointer data)
{
	(void) key;
	(void) data;
	((Folder *) value)->walked = 0;
}

static Pass
walk_pass (ArcScan *scan)
{
	guint i;

	g_hash_table_foreach (scan->folders, forget_walked, NULL);

	for (i = 0; i < scan->n_roots; i++) {
		Frame top = { scan->roots[i].dir, scan->roots[i].text, 0, FALSE, NULL, G_QUEUE_INIT };
		Pass end;

		visit_by_name (scan, &top, scan->roots[i].name);
		end = drain (scan);
		if (end != PASS_DONE) {
			return end;
		}
	}
	return PASS_DONE;
}

/* On the scan's thread, since it reads the disk. */
static void
prepare (ArcScan *scan)
{
	char *real = NULL;
	gsize i;

	if (scan->mounts == NULL) {
		scan->mounts = arc_mount_table_read ();
	}

	/* Where the person is, so no share check. */
	if (resolve (scan, NULL, scan->base, FALSE, &real) == LEADS_HERE) {
		g_free (scan->base);
		scan->base = real;
	}

	for (i = 0; i < arc_mount_table_count (scan->mounts); i++) {
		const char *path = arc_mount_table_path (scan->mounts, i);
		char *parent = g_path_get_dirname (path);

		if (!scan->shares && strcmp (parent, path) != 0 && arc_leaves_for_share (&scan->host, parent, path)) {
			char *share = g_strdup (path);

			g_hash_table_add (scan->share_mounts, share);
			g_hash_table_add (scan->share_parents, parent);
		} else {
			g_free (parent);
		}
	}

	scan->n_roots = g_strv_length (scan->paths);
	scan->roots = g_new0 (Root, scan->n_roots + 1);
	for (i = 0; i < scan->n_roots; i++) {
		char *parent = g_path_get_dirname (scan->paths[i]);
		Root *root = &scan->roots[i];

		if (resolve (scan, NULL, parent, FALSE, &real) != LEADS_HERE) {
			real = g_strdup (parent);
		}
		root->dir = g_file_new_for_path (real);
		root->text = folder_at (scan, root->dir)->text;
		root->name = g_path_get_basename (scan->paths[i]);
		g_free (real);
		g_free (parent);
	}
}

static gpointer
scan_thread (gpointer data)
{
	ArcScan *scan = data;

	if (!scan->prepared) {
		prepare (scan);
		scan->prepared = TRUE;
	}

	for (;;) {
		Pass end;
		guint want;

		g_mutex_lock (&scan->lock);
		want = (guint) g_atomic_int_get (&scan->wanted);
		if (g_cancellable_is_cancelled (scan->cancellable) || is_covered (scan->covered, want)) {
			scan->running = FALSE;
			g_mutex_unlock (&scan->lock);
			break;
		}
		g_mutex_unlock (&scan->lock);

		scan->follow = want;
		end = walk_pass (scan);
		if (end != PASS_CANCELLED) {
			publish (scan, end == PASS_DONE);
		}
	}
	return NULL;
}

/* Returns: (transfer full): free with arc_scan_free */
ArcScan *
arc_scan_new (const ArcHost       *host,
	      const char          *base,
	      const char * const  *paths,
	      guint                follow,
	      gboolean             shares,
	      ArcMountTable       *mounts)
{
	ArcScan *scan;

	g_return_val_if_fail (base != NULL, NULL);
	g_return_val_if_fail (paths != NULL, NULL);

	scan = g_new0 (ArcScan, 1);
	if (host != NULL) {
		scan->host = *host;
	}
	scan->base = g_strdup (base);
	scan->paths = g_strdupv ((char **) paths);
	scan->shares = shares;
	scan->mounts = mounts;
	scan->cancellable = g_cancellable_new ();

	scan->list = arc_path_list_new ();
	scan->folders = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, folder_free);
	scan->share_mounts = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);
	scan->share_parents = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);
	scan->stack = g_ptr_array_new_with_free_func (frame_free);
	scan->text = g_string_new (NULL);

	g_mutex_init (&scan->lock);
	scan->wanted = (gint) (follow & ARC_FOLLOW_ALL);
	scan->left_shares = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);
	scan->unreadable = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);
	scan->snapshot.follow = follow & ARC_FOLLOW_ALL;

	return scan;
}

static gboolean
report_dispatch (GSource     *source,
		 GSourceFunc  callback,
		 gpointer     data)
{
	ArcScan *scan = ((ReportSource *) source)->scan;
	ArcScanReported reported = scan->reported;
	gpointer reported_data = scan->reported_data;
	gint64 now = g_get_monotonic_time ();
	ArcScanReport report;
	gboolean send;

	(void) callback;
	(void) data;

	g_mutex_lock (&scan->lock);
	send = scan->serial != scan->sent_serial;
	report = scan->snapshot;
	scan->sent_serial = scan->serial;
	g_mutex_unlock (&scan->lock);

	if (send) {
		scan->sent_at = now;
		g_source_set_ready_time (source, now + REPORT_GAP_USECS);
	} else {
		/* Nothing more is coming until an option changes. */
		g_source_set_ready_time (source, report.done ? -1 : now + REPORT_GAP_USECS);
	}

	/* Last, since it may free the scan. */
	if (send && reported != NULL) {
		reported (reported_data, &report);
	}
	return G_SOURCE_CONTINUE;
}

static GSourceFuncs report_funcs = {
	.dispatch = report_dispatch,
};

void
arc_scan_start (ArcScan         *scan,
		ArcScanReported  reported,
		gpointer         data)
{
	g_return_if_fail (scan != NULL);
	g_return_if_fail (scan->thread == NULL && scan->timer == NULL);

	scan->reported = reported;
	scan->reported_data = data;
	scan->context = g_main_context_ref_thread_default ();
	scan->sent_at = g_get_monotonic_time ();

	scan->timer = g_source_new (&report_funcs, sizeof (ReportSource));
	((ReportSource *) scan->timer)->scan = scan;
	g_source_set_name (scan->timer, "archive scan report");
	g_source_set_ready_time (scan->timer, scan->sent_at + REPORT_GAP_USECS);
	g_source_attach (scan->timer, scan->context);

	scan->running = TRUE;
	scan->thread = g_thread_new ("arc-scan", scan_thread, scan);
}

void
arc_scan_set_follow (ArcScan *scan,
		     guint    follow)
{
	ArcScanReport report;
	gboolean again;

	g_return_if_fail (scan != NULL);

	follow &= ARC_FOLLOW_ALL;
	g_mutex_lock (&scan->lock);
	g_atomic_int_set (&scan->wanted, (gint) follow);
	report = scan->snapshot;
	fill_counts_locked (scan, &report);
	if (!same_report (&report, &scan->snapshot)) {
		scan->snapshot = report;
		scan->serial++;
	}
	again = scan->timer != NULL && !scan->running && !report.done;
	if (again) {
		scan->running = TRUE;
	}
	g_mutex_unlock (&scan->lock);

	if (again) {
		if (scan->thread != NULL) {
			g_thread_join (scan->thread);
		}
		scan->thread = g_thread_new ("arc-scan", scan_thread, scan);
	}
	if (scan->timer != NULL) {
		g_source_set_ready_time (scan->timer, MAX (g_get_monotonic_time (), scan->sent_at + REPORT_GAP_USECS));
	}
}

void
arc_scan_free (ArcScan *scan)
{
	guint i;

	if (scan == NULL) {
		return;
	}

	g_cancellable_cancel (scan->cancellable);
	if (scan->thread != NULL) {
		g_thread_join (scan->thread);
	}
	if (scan->timer != NULL) {
		g_source_destroy (scan->timer);
		g_source_unref (scan->timer);
	}
	if (scan->context != NULL) {
		g_main_context_unref (scan->context);
	}

	for (i = 0; i < scan->n_roots; i++) {
		g_object_unref (scan->roots[i].dir);
		g_free (scan->roots[i].name);
	}
	g_free (scan->roots);
	arc_path_list_free (scan->list);
	g_hash_table_unref (scan->folders);
	g_hash_table_unref (scan->share_mounts);
	g_hash_table_unref (scan->share_parents);
	g_hash_table_unref (scan->left_shares);
	g_hash_table_unref (scan->unreadable);
	g_ptr_array_unref (scan->stack);
	g_string_free (scan->text, TRUE);
	g_mutex_clear (&scan->lock);
	arc_mount_table_free (scan->mounts);
	g_object_unref (scan->cancellable);
	g_strfreev (scan->paths);
	g_free (scan->base);
	g_free (scan);
}

guint64
arc_scan_report_total (const ArcScanReport *report)
{
	return report->totals[report->follow & ARC_FOLLOW_ALL];
}

guint64
arc_scan_report_change (const ArcScanReport *report,
			ArcFollow            option)
{
	guint follow = report->follow & ARC_FOLLOW_ALL;

	if (!(follow & option)) {
		return 0;
	}
	return report->totals[follow] - report->totals[follow & ~(guint) option];
}
