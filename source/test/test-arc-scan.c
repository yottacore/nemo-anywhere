/* The scan behind the archive size totals, on a scratch tree: folder and
 * file links, a link back up that loops, one to itself, one that leads
 * nowhere, a .lnk, made-up nested, other and share mounts, and on Windows a
 * junction and a junction loop. Each mix's total against what that mix lets
 * in, an option turned off and one turned on mid-scan, the gap between
 * reports, Cancel, and the app's own host on the same tree. */

#include <stdlib.h>
#include <string.h>
#include <glib/gstdio.h>
#include <gio/gio.h>

#include "arc-host.h"
#include "arc-scan.h"
#include <libnemo-private/nemo-archive-host.h>
#include <libnemo-private/nemo-share.h>
#ifdef G_OS_WIN32
#include <libnemo-private/nemo-link-win32.h>
#endif

#include "test-check.h"
#include "test-scratch.h"

#define SYM   ARC_FOLLOW_SYMLINKS
#define JUN   ARC_FOLLOW_JUNCTIONS
#define OTHER ARC_FOLLOW_OTHER_FS
#define NEVER 0x10u		/* needs more than any mix has */

#ifdef G_OS_WIN32
#define NESTED ARC_FOLLOW_OTHER_FS	/* Windows has no nested kind */
#else
#define NESTED ARC_FOLLOW_NESTED_FS
#endif

/* A file in the tree, what its shortest path needs, and the link it is
   reached through, which may not have been made. */
typedef struct {
	const char *path;
	guint64     bytes;
	guint       needs;
	const char *via;
	gboolean    there;
} Piece;

static Piece pieces[] = {
	{ "sel/a.bin",                1, 0,            NULL,      TRUE },
	{ "sel/sub/b.bin",            2, 0,            NULL,      TRUE },
	{ "sel/sub/deeper/c.bin",     4, 0,            NULL,      TRUE },
	{ "sel/shortcut.lnk",         8, 0,            NULL,      TRUE },
	{ "outside/f.bin",           16, SYM,          "flink",   TRUE },
	{ "outside/dir/g.bin",       32, SYM,          "dlink",   TRUE },
	{ "sel/nest/h.bin",          64, NESTED,       NULL,      TRUE },
	{ "sel/other/i.bin",        128, OTHER,        NULL,      TRUE },
	{ "sel/share/j.bin",        256, NEVER,        NULL,      TRUE },
	{ "sel/gate/k.bin",         512, 0,            NULL,      TRUE },
	{ "outside/jdir/l.bin",    1024, JUN,          "jlink",   FALSE },
	{ "nest2/m.bin",           2048, SYM | NESTED, "tonest",  TRUE },
	{ "other2/n.bin",          4096, SYM | OTHER,  "toother", TRUE },
	{ "sel/share/inner/p.bin", 8192, NEVER,        NULL,      TRUE },
	{ "outside/dir/deep/q.bin", 16384, SYM,        "dlink",   TRUE },
	{ "hold/x.bin",           32768, NEVER,        NULL,      TRUE },	/* not in sel */
};

typedef enum {
	FILE_LINK,
	FOLDER_LINK,
	JUNCTION_LINK,
} LinkKind;

typedef struct {
	const char *path;
	const char *target;	/* relative, or from the root for a junction */
	LinkKind    kind;
	gboolean    made;
} Link;

static Link links[] = {
	{ "sel/flink",     "../outside/f.bin", FILE_LINK,     FALSE },
	{ "sel/dlink",     "../outside/dir",   FOLDER_LINK,   FALSE },
	{ "sel/again",     "sub",              FOLDER_LINK,   FALSE },
	{ "sel/sub/loop",  "..",               FOLDER_LINK,   FALSE },
	{ "sel/self",      "self",             FILE_LINK,     FALSE },
	{ "sel/nowhere",   "missing",          FILE_LINK,     FALSE },
	{ "sel/tonest",    "../nest2",         FOLDER_LINK,   FALSE },
	{ "sel/toother",   "../other2",        FOLDER_LINK,   FALSE },
	{ "sel/toshare",   "share/inner",      FOLDER_LINK,   FALSE },
	/* Onto the share by its second hop. */
	{ "outside/toshare2", "../sel/share/inner", FOLDER_LINK, FALSE },
	{ "sel/hop",       "../outside/toshare2", FOLDER_LINK, FALSE },
	{ "lead",          "outside/dir",      FOLDER_LINK,   FALSE },
#ifdef G_OS_WIN32
	{ "sel/jlink",     "outside/jdir",     JUNCTION_LINK, FALSE },
	{ "sel/sub/jloop", "sel",              JUNCTION_LINK, FALSE },
#endif
};

static char *root;

/* Returns: (transfer full) */
static char *
at (const char *relative)
{
	char *path = g_build_filename (root, relative, NULL);
	char *c;

	for (c = path; *c != '\0'; c++) {
		if (*c == '/') {
			*c = G_DIR_SEPARATOR;
		}
	}
	return path;
}

static gboolean
link_made (const char *name)
{
	char *path = g_strconcat ("sel/", name, NULL);
	gboolean made = FALSE;
	gsize i;

	for (i = 0; i < G_N_ELEMENTS (links); i++) {
		if (strcmp (links[i].path, path) == 0 || strcmp (links[i].path, name) == 0) {
			made = links[i].made;
		}
	}
	g_free (path);
	return made;
}

static gboolean
make_link (Link *link)
{
	char *path = at (link->path);
	GFile *file = g_file_new_for_path (path);
	gboolean made = FALSE;
#ifdef G_OS_WIN32
	char *target = link->kind == JUNCTION_LINK ? at (link->target) : g_strdup (link->target);
	char *folder = g_path_get_dirname (path);
	char *c;

	for (c = target; *c != '\0'; c++) {
		if (*c == '/') {
			*c = '\\';
		}
	}
	made = nemo_win32_link_create (target, path, folder,
				       link->kind == JUNCTION_LINK ? NEMO_LINK_JUNCTION
				       : link->kind == FOLDER_LINK ? NEMO_LINK_DIR_SYMLINK
				       : NEMO_LINK_FILE_SYMLINK, NULL);
	g_free (folder);
	g_free (target);
#else
	made = g_file_make_symbolic_link (file, link->target, NULL, NULL);
#endif
	/* wine says yes to a symlink and makes nothing. */
	made = made && g_file_query_file_type (file, G_FILE_QUERY_INFO_NOFOLLOW_SYMLINKS, NULL) != G_FILE_TYPE_UNKNOWN;
	g_object_unref (file);
	g_free (path);
	return made;
}

static gboolean
make_tree (void)
{
	gsize i;

	for (i = 0; i < G_N_ELEMENTS (pieces); i++) {
		char *path = at (pieces[i].path);
		char *dir = g_path_get_dirname (path);
		char *bytes = g_malloc0 (pieces[i].bytes);
		gboolean ok;

		ok = g_mkdir_with_parents (dir, 0755) == 0 &&
		     g_file_set_contents (path, bytes, (gssize) pieces[i].bytes, NULL);
		g_free (bytes);
		g_free (dir);
		g_free (path);
		if (!ok) {
			return FALSE;
		}
	}

	for (i = 0; i < G_N_ELEMENTS (links); i++) {
		links[i].made = make_link (&links[i]);
		if (!links[i].made) {
			g_printerr ("note: no %s here\n", links[i].path);
		}
	}
	for (i = 0; i < G_N_ELEMENTS (pieces); i++) {
		if (pieces[i].via != NULL) {
			pieces[i].there = link_made (pieces[i].via);
		}
	}
	return TRUE;
}

/* What the selection sel puts in for a mix. */
static guint64
expected (guint mix)
{
	guint64 total = 0;
	gsize i;

	for (i = 0; i < G_N_ELEMENTS (pieces); i++) {
		if (pieces[i].there && (pieces[i].needs & ~mix) == 0) {
			total += pieces[i].bytes;
		}
	}
	return total;
}

static guint
expected_files (guint mix)
{
	guint files = 0;
	gsize i;

	for (i = 0; i < G_N_ELEMENTS (pieces); i++) {
		if (pieces[i].there && (pieces[i].needs & ~mix) == 0) {
			files++;
		}
	}
	return files;
}

/* Counted by where they lead. The links onto the share both lead to the
   same place, and are only counted when they would be followed. */
static guint
expected_shares (guint mix)
{
	guint shares = 0;

	if (mix & OTHER) {
		shares++;
		if ((mix & SYM) && (link_made ("toshare") || link_made ("hop"))) {
			shares++;
		}
	}
	return shares;
}

/* Made up, around the real scratch tree. The share is a cifs mount, nest
   and nest2 are in the root's pool, other and other2 on disks of their own.
   Returns: (transfer full) */
static ArcMountTable *
made_up_mounts (void)
{
	char *where[6] = { at (""), at ("sel/nest"), at ("nest2"), at ("sel/other"), at ("other2"), at ("sel/share") };
#ifdef G_OS_WIN32
	ArcMountEntry entries[] = {
		{ where[0], "\\\\?\\Volume{aaaa}\\", NULL },
		{ where[1], "\\\\?\\Volume{bbbb}\\", NULL },
		{ where[2], "\\\\?\\Volume{cccc}\\", NULL },
		{ where[3], "\\\\?\\Volume{dddd}\\", NULL },
		{ where[4], "\\\\?\\Volume{eeee}\\", NULL },
		{ where[5], NULL, NULL },
	};
	gboolean windows = TRUE;
#else
	ArcMountEntry entries[] = {
		{ where[0], "tank/scratch", "zfs" },
		{ where[1], "tank/scratch/nest", "zfs" },
		{ where[2], "tank/nest2", "zfs" },
		{ where[3], "/dev/sdz9", "ext4" },
		{ where[4], "/dev/sdy9", "ext4" },
		{ where[5], "//server/share", "cifs" },
	};
	gboolean windows = FALSE;
#endif
	ArcMountTable *table = arc_mount_table_new (entries, G_N_ELEMENTS (entries), windows);
	gsize i;

	for (i = 0; i < G_N_ELEMENTS (where); i++) {
		g_free (where[i]);
	}
	return table;
}

/* What the fake host saw, and where its walk waits. */
typedef struct {
	GMutex     lock;
	GCond      cond;
	GPtrArray *listed;	/* "path\tattributes" */
	char      *gate;
	gboolean   at_gate;
	gboolean   open;
	char      *share;
} Fake;

static Fake fake;

static gboolean
is_under (const char *path,
	  const char *top)
{
	gsize n = strlen (top);

	return strncmp (path, top, n) == 0 && (path[n] == '\0' || G_IS_DIR_SEPARATOR (path[n]));
}

static GFileEnumerator *
fake_children (gpointer             data,
	       GFile               *dir,
	       const char          *attributes,
	       GFileQueryInfoFlags  flags,
	       GCancellable        *cancellable,
	       GError             **error)
{
	const char *path = g_file_peek_path (dir);

	check (data == &fake);
	g_mutex_lock (&fake.lock);
	g_ptr_array_add (fake.listed, g_strdup_printf ("%s\t%s", path, attributes));
	if (fake.gate != NULL && strcmp (path, fake.gate) == 0) {
		g_clear_pointer (&fake.gate, g_free);
		fake.at_gate = TRUE;
		g_cond_broadcast (&fake.cond);
		while (!fake.open && !g_cancellable_is_cancelled (cancellable)) {
			g_cond_wait_until (&fake.cond, &fake.lock, g_get_monotonic_time () + G_TIME_SPAN_MILLISECOND * 20);
		}
	}
	g_mutex_unlock (&fake.lock);

	return g_file_enumerate_children (dir, attributes, flags, cancellable, error);
}

static gboolean
fake_leaves_for (gpointer    data,
		 const char *folder,
		 const char *path)
{
	char *full = g_canonicalize_filename (path, folder);
	gboolean leaves = is_under (full, fake.share) && !is_under (folder, fake.share);

	check (data == &fake);
	g_free (full);
	return leaves;
}

static const ArcHost fake_host = {
	.walk = { fake_children },
	.shares = { fake_leaves_for },
	.data = &fake,
};

static void
fake_reset (const char *gate)
{
	g_mutex_lock (&fake.lock);
	g_ptr_array_set_size (fake.listed, 0);
	g_free (fake.gate);
	fake.gate = gate != NULL ? at (gate) : NULL;
	fake.at_gate = FALSE;
	fake.open = FALSE;
	g_mutex_unlock (&fake.lock);
}

static void
fake_open (void)
{
	g_mutex_lock (&fake.lock);
	fake.open = TRUE;
	g_cond_broadcast (&fake.cond);
	g_mutex_unlock (&fake.lock);
}

static gboolean
fake_wait_at_gate (void)
{
	gint64 end = g_get_monotonic_time () + (gint64) (10 * test_slowness () * G_USEC_PER_SEC);
	gboolean there;

	g_mutex_lock (&fake.lock);
	while (!fake.at_gate && g_cond_wait_until (&fake.cond, &fake.lock, end)) {
	}
	there = fake.at_gate;
	g_mutex_unlock (&fake.lock);
	return there;
}

/* How many times a folder was listed, and whether with names only. */
static guint
times_listed (const char *relative,
	      gboolean   *names_only)
{
	char *path = at (relative);
	char *line = g_strconcat (path, "\t", NULL);
	guint times = 0;
	guint i;

	g_mutex_lock (&fake.lock);
	for (i = 0; i < fake.listed->len; i++) {
		const char *seen = g_ptr_array_index (fake.listed, i);

		if (g_str_has_prefix (seen, line)) {
			times++;
			if (names_only != NULL) {
				*names_only = strcmp (seen + strlen (line), "standard::name,standard::type") == 0;
			}
		}
	}
	g_mutex_unlock (&fake.lock);
	g_free (line);
	g_free (path);
	return times;
}

static gboolean
listed_under (const char *relative)
{
	char *path = at (relative);
	gboolean found = FALSE;
	guint i;

	g_mutex_lock (&fake.lock);
	for (i = 0; i < fake.listed->len && !found; i++) {
		char *seen = g_strdup (g_ptr_array_index (fake.listed, i));

		*strchr (seen, '\t') = '\0';
		found = is_under (seen, path);
		g_free (seen);
	}
	g_mutex_unlock (&fake.lock);
	g_free (path);
	return found;
}

/* Every report, and the narrowest gap between two. */
typedef struct {
	ArcScanReport last;
	guint         count;
	gint64        at;
	gint64        gap;
	gboolean      free_in_report;
	ArcScan      *scan;
	guint         count_at_free;
} Seen;

static void
saw_report (gpointer             data,
	    const ArcScanReport *report)
{
	Seen *seen = data;
	gint64 now = g_get_monotonic_time ();

	if (seen->count > 0 && now - seen->at < seen->gap) {
		seen->gap = now - seen->at;
	}
	seen->last = *report;
	seen->count++;
	seen->at = now;

	if (seen->free_in_report && report->done) {
		arc_scan_free (seen->scan);
		seen->scan = NULL;
		seen->count_at_free = seen->count;
	}
}

static void
seen_reset (Seen *seen)
{
	memset (seen, 0, sizeof *seen);
	seen->gap = G_MAXINT64;
}

/* Until a report says done, or until the time runs out. */
static gboolean
run_until_done (Seen *seen)
{
	gint64 end = g_get_monotonic_time () + (gint64) (30 * test_slowness () * G_USEC_PER_SEC);

	while (!(seen->count > 0 && seen->last.done) && g_get_monotonic_time () < end) {
		g_main_context_iteration (NULL, FALSE);
		g_usleep (2000);
	}
	return seen->count > 0 && seen->last.done;
}

static void
run_for (gint64 usecs)
{
	gint64 end = g_get_monotonic_time () + usecs;

	while (g_get_monotonic_time () < end) {
		g_main_context_iteration (NULL, FALSE);
		g_usleep (2000);
	}
}

/* Returns: (transfer full): free with arc_scan_free */
static ArcScan *
scan_of (const ArcHost *host,
	 const char    *first,
	 const char    *second,
	 guint          follow)
{
	char *paths[3] = { at (first), second != NULL ? at (second) : NULL, NULL };
	ArcScan *scan = arc_scan_new (host, root, (const char * const *) paths, follow, FALSE, made_up_mounts ());

	g_free (paths[0]);
	g_free (paths[1]);
	return scan;
}

/* The gap is a floor, so it holds under load too. */
#define GAP_FLOOR (G_USEC_PER_SEC / 4 - G_TIME_SPAN_MILLISECOND)

static void
check_report_matches (const ArcScanReport *report,
		      guint                mix)
{
	guint sub;

	check (report->follow == mix);
	check (arc_scan_report_total (report) == expected (mix));
	/* Everything a mix that follows less lets in was walked too. */
	for (sub = 0; sub < ARC_MIX_COUNT; sub++) {
		if ((sub & ~mix) == 0 && report->totals[sub] != expected (sub)) {
			g_printerr ("mix %u, sub-mix %u: %" G_GUINT64_FORMAT " for %" G_GUINT64_FORMAT "\n",
				    mix, sub, report->totals[sub], expected (sub));
			failures++;
		}
	}
	check (report->files == expected_files (mix));
	check (report->shares == expected_shares (mix));
	check (report->unreadable == 0);
}

static void
check_every_mix (void)
{
	guint mix;

	for (mix = 0; mix < ARC_MIX_COUNT; mix++) {
		Seen seen;
		ArcScan *scan;
		gboolean names_only = FALSE;
		guint option;

		seen_reset (&seen);
		fake_reset (NULL);
		scan = scan_of (&fake_host, "sel", NULL, mix);
		arc_scan_start (scan, saw_report, &seen);
		check (run_until_done (&seen));
		check_report_matches (&seen.last, mix);

		for (option = SYM; option <= OTHER; option <<= 1) {
			guint64 change = (mix & option) ? expected (mix) - expected (mix & ~option) : 0;

			check (arc_scan_report_change (&seen.last, (ArcFollow) option) == change);
		}

		/* The share is never listed, nor even looked at in the folder
		   it is mounted in. */
		check (!listed_under ("sel/share"));
		check (times_listed ("sel", &names_only) == 1);
		check (names_only);
		/* Twice when the link to it comes first in the listing: by the
		   link, then again by the path that needs less. */
		check (times_listed ("sel/sub", NULL) == ((mix & SYM) ? times_listed ("sel/sub", NULL) : 1));
		check (times_listed ("sel/sub", NULL) <= 2);
		check (seen.gap >= GAP_FLOOR);

		arc_scan_free (scan);
	}
}

/* Symlinks turned off while the walk waits inside a symlinked folder: it
   backs out without reading that folder, and goes on to the rest. */
static void
check_narrowed_mid_scan (void)
{
	guint all = ARC_FOLLOW_ALL;
	guint less = ARC_FOLLOW_ALL & ~SYM;
	Seen seen;
	ArcScan *scan;

	if (!link_made ("lead")) {
		g_printerr ("note: no symlinks here, so nothing to narrow\n");
		return;
	}

	seen_reset (&seen);
	fake_reset ("outside/dir");
	scan = scan_of (&fake_host, "lead", "sel", all);
	arc_scan_start (scan, saw_report, &seen);
	check (fake_wait_at_gate ());
	check (times_listed ("sel", NULL) == 0);

	arc_scan_set_follow (scan, less);
	fake_open ();
	check (run_until_done (&seen));

	check_report_matches (&seen.last, less);
	/* Nothing went in by a symlink after the change, so the mix with
	   everything on has only what the narrower one has: g and q, behind
	   the gate, never went in. */
	check (seen.last.totals[all] == expected (less));
	check (!listed_under ("outside/dir/deep"));
	check (times_listed ("sel", NULL) == 1);
	check (seen.gap >= GAP_FLOOR);

	arc_scan_free (scan);
}

/* Symlinks and nested turned on while the walk waits in the first folder:
   it starts again, and the list ends with each file once. */
static void
check_widened_mid_scan (void)
{
	guint less = 0;
	guint more = SYM | NESTED;
	Seen seen;
	ArcScan *scan;
	guint64 hold = 32768;

	seen_reset (&seen);
	fake_reset ("hold");
	scan = scan_of (&fake_host, "hold", "sel", less);
	arc_scan_start (scan, saw_report, &seen);
	check (fake_wait_at_gate ());

	arc_scan_set_follow (scan, more);
	fake_open ();
	check (run_until_done (&seen));

	check (seen.last.follow == more);
	check (arc_scan_report_total (&seen.last) == expected (more) + hold);
	check (seen.last.totals[less] == expected (less) + hold);
	check (seen.last.files == expected_files (more) + 1);
	check (times_listed ("hold", NULL) == 2);
	check (seen.gap >= GAP_FLOOR);

	/* Back to less is already walked: a report, and nothing listed. */
	fake_reset (NULL);
	seen.last.done = FALSE;
	arc_scan_set_follow (scan, less);
	check (run_until_done (&seen));
	check (seen.last.follow == less);
	check (arc_scan_report_total (&seen.last) == expected (less) + hold);
	check (times_listed ("hold", NULL) == 0);

	/* And on again too, since a pass by more finished. */
	seen.last.done = FALSE;
	arc_scan_set_follow (scan, more);
	check (run_until_done (&seen));
	check (arc_scan_report_total (&seen.last) == expected (more) + hold);
	check (times_listed ("sel", NULL) == 0);

	/* Something not walked yet starts it again. Not other filesystems,
	   which is what nested is on Windows. */
	seen.last.done = FALSE;
	arc_scan_set_follow (scan, more | JUN);
	check (run_until_done (&seen));
	check (arc_scan_report_total (&seen.last) == expected (more | JUN) + hold);
	check (times_listed ("sel", NULL) == 1);
	check (seen.gap >= GAP_FLOOR);

	arc_scan_free (scan);
}

static guint64
outside_bytes (void)
{
	guint64 total = 0;
	gsize i;

	for (i = 0; i < G_N_ELEMENTS (pieces); i++) {
		if (g_str_has_prefix (pieces[i].path, "outside/")) {
			total += pieces[i].bytes;
		}
	}
	return total;
}

/* A folder reached by a link first and then by a path that needs nothing
   is walked again, so its files count in every mix. The other way round,
   once is enough. Roots go in order, so which comes first is known. */
static void
check_reached_twice (void)
{
	const char *orders[2][2] = { { "lead", "outside" }, { "outside", "lead" } };
	guint i;

	if (!link_made ("lead")) {
		return;
	}
	for (i = 0; i < 2; i++) {
		Seen seen;
		ArcScan *scan;

		seen_reset (&seen);
		fake_reset (NULL);
		scan = scan_of (&fake_host, orders[i][0], orders[i][1], SYM);
		arc_scan_start (scan, saw_report, &seen);
		check (run_until_done (&seen));
		check (seen.last.totals[0] == outside_bytes ());
		check (seen.last.totals[SYM] == outside_bytes ());
		check (times_listed ("outside/dir", NULL) == (i == 0 ? 2 : 1));
		arc_scan_free (scan);
	}
}

/* Cancel while the walk waits: it stops, and nothing is reported after. */
static void
check_cancel (void)
{
	Seen seen;
	ArcScan *scan;
	guint count;
	gint64 start;

	seen_reset (&seen);
	fake_reset ("sel/sub");
	scan = scan_of (&fake_host, "sel", NULL, ARC_FOLLOW_ALL);
	arc_scan_start (scan, saw_report, &seen);
	check (fake_wait_at_gate ());
	run_for (G_USEC_PER_SEC / 2);

	count = seen.count;
	start = g_get_monotonic_time ();
	arc_scan_free (scan);
	check (g_get_monotonic_time () - start < (gint64) (5 * test_slowness () * G_USEC_PER_SEC));
	run_for (G_USEC_PER_SEC / 2);
	check (seen.count == count);

	/* And from inside the report, which is where a dialog's OK may be. */
	seen_reset (&seen);
	fake_reset (NULL);
	seen.free_in_report = TRUE;
	seen.scan = scan_of (&fake_host, "sel", NULL, 0);
	arc_scan_start (seen.scan, saw_report, &seen);
	check (run_until_done (&seen));
	check (seen.scan == NULL);
	run_for (G_USEC_PER_SEC / 2);
	check (seen.count == seen.count_at_free);
}

/* No walk to ask: every folder is unreadable, nothing counts, it ends. With
   no share check, every link and mount might be a share, so each is left
   out. */
static void
check_bare_host (void)
{
	ArcHost walk_only = { .walk = { fake_children }, .data = &fake };
	Seen seen;
	ArcScan *scan;

	seen_reset (&seen);
	scan = scan_of (NULL, "sel", NULL, ARC_FOLLOW_ALL);
	arc_scan_start (scan, saw_report, &seen);
	check (run_until_done (&seen));
	check (arc_scan_report_total (&seen.last) == 0);
	check (seen.last.unreadable == 1);
	arc_scan_free (scan);

	seen_reset (&seen);
	fake_reset (NULL);
	scan = scan_of (&walk_only, "sel", NULL, ARC_FOLLOW_ALL);
	arc_scan_start (scan, saw_report, &seen);
	check (run_until_done (&seen));
	check (arc_scan_report_total (&seen.last) == expected (0));
	check (seen.last.shares >= 3);
	check (!listed_under ("sel/share"));
	check (!listed_under ("sel/nest"));
	check (!listed_under ("outside"));
	arc_scan_free (scan);
}

/* The app's host on the same tree: its walk, and its share check with the
   share folder made one for the test. */
static void
check_app_host (void)
{
	char *share = at ("sel/share");
	const char *shares[] = { share, NULL };
	ArcHost host;
	Seen seen;
	ArcScan *scan;

	nemo_archive_host_init (&host);
	nemo_share_set_roots_for_test (shares);

	seen_reset (&seen);
	scan = scan_of (&host, "sel", NULL, ARC_FOLLOW_ALL);
	arc_scan_start (scan, saw_report, &seen);
	check (run_until_done (&seen));
	check_report_matches (&seen.last, ARC_FOLLOW_ALL);
	arc_scan_free (scan);

	nemo_share_set_roots_for_test (NULL);
	g_free (share);
}

int
main (int    argc,
      char **argv)
{
	GError *error = NULL;
	char *made;

	(void) argc;
	(void) argv;

	made = test_scratch_dir ("arc-scan-XXXXXX", &error);
	if (made == NULL) {
		g_printerr ("no scratch folder: %s\n", error->message);
		return 77;
	}
#ifdef G_OS_WIN32
	root = made;
#else
	{
		/* The scan goes by real paths, and the made-up mounts have to
		   match. */
		char *real = realpath (made, NULL);

		g_free (made);
		if (real == NULL) {
			g_printerr ("no real path for the scratch folder\n");
			return 77;
		}
		root = g_strdup (real);
		free (real);
	}
#endif

	if (!make_tree ()) {
		g_printerr ("could not make the tree\n");
		return EXIT_FAILURE;
	}

	g_mutex_init (&fake.lock);
	g_cond_init (&fake.cond);
	fake.listed = g_ptr_array_new_with_free_func (g_free);
	fake.share = at ("sel/share");

	check_every_mix ();
	check_narrowed_mid_scan ();
	check_widened_mid_scan ();
	check_reached_twice ();
	check_cancel ();
	check_bare_host ();
	check_app_host ();

	g_ptr_array_unref (fake.listed);
	g_free (fake.share);
	g_free (fake.gate);
	g_free (root);

	if (failures > 0) {
		g_printerr ("%d check(s) failed\n", failures);
		return EXIT_FAILURE;
	}
	return EXIT_SUCCESS;
}
