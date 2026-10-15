/* The delete guard's floor, checked with no display: home, the folders above it
 * and a mount are never removed, a tree removal never walks through a link, and
 * one job cannot take most of home. */

#include <config.h>

#include <unistd.h>
#include <sys/stat.h>
#include <glib.h>
#include <glib/gstdio.h>
#include <gio/gio.h>

#include "libnemo-private/nemo-delete-guard.h"

#include "test-scratch.h"
#include "test-check.h"
#include "test-guard-common.h"

static GList *
files_in (const char *dir, const char *prefix, int count)
{
	GList *list = NULL;
	int i;

	for (i = 0; i < count; i++) {
		char *name = g_strdup_printf ("%s%d", prefix, i);
		char *path = g_build_filename (dir, name, NULL);

		list = g_list_prepend (list, g_file_new_for_path (path));
		g_free (path);
		g_free (name);
	}

	return list;
}

static void
make_files (const char *dir, const char *prefix, int from, int to)
{
	int i;

	for (i = from; i < to; i++) {
		char *name = g_strdup_printf ("%s%d", prefix, i);

		g_free (make_file (dir, name));
		g_free (name);
	}
}

int
main (void)
{
	char *root, *home, *docs, *notes, *home_link, *outside, *precious, *tree, *inner, *link;
	GStatBuf proc_st, top_st;
	GFile *file;
	GList *list;
	gint64 now;

	root = test_scratch_dir ("nemo-guard-XXXXXX", NULL);
	home = make_dir (root, "home");
	g_setenv ("HOME", home, TRUE);

	if (g_strcmp0 (g_get_home_dir (), home) != 0) {
		g_printerr ("SKIP: the home folder was read before HOME could be moved\n");
		return 77;
	}

	check (protected_path (home));
	check (protected_path (root));
	check (protected_path ("/"));

	docs = make_dir (home, "Documents");
	notes = make_file (home, "notes.txt");
	check (!protected_path (docs));
	check (!protected_path (notes));

	home_link = g_build_filename (root, "home-link", NULL);
	if (symlink (home, home_link) != 0) {
		g_printerr ("SKIP: symlink() unavailable\n");
		return 77;
	}
	check (!protected_path (home_link));

	if (g_lstat ("/proc", &proc_st) == 0 && S_ISDIR (proc_st.st_mode) &&
	    g_stat ("/", &top_st) == 0 && proc_st.st_dev != top_st.st_dev) {
		check (protected_path ("/proc"));
	}

	/* What a link inside a removed tree points at stays. */
	outside = make_dir (root, "outside");
	precious = make_file (outside, "precious.txt");
	tree = make_dir (root, "tree");
	inner = make_dir (tree, "inner");
	g_free (make_file (inner, "scratch.txt"));
	link = g_build_filename (tree, "link-out", NULL);
	check (symlink (outside, link) == 0);

	/* What every tree walk asks before going in. */
	check (real_folder (tree));
	check (!real_folder (link));
	check (!real_folder (notes));
	check (!real_folder (home_link));

	file = g_file_new_for_path (tree);
	check (nemo_delete_guard_remove_tree (file, NULL));
	g_object_unref (file);
	check (exists (precious));
	check (!exists (tree));

	/* Pointed at home, it removes nothing at all. */
	file = g_file_new_for_path (home);
	check (!nemo_delete_guard_remove_tree (file, NULL));
	g_object_unref (file);
	check (exists (notes));
	check (exists (docs));

	/* Eight shown entries in home and two hidden ones. */
	make_files (home, "item", 0, 6);
	g_free (make_file (home, ".hidden1"));
	g_free (make_file (home, ".hidden2"));

	list = files_in (home, "item", 5);
	check (nemo_delete_guard_sweeps_home (list));
	g_list_free_full (list, g_object_unref);

	list = files_in (home, "item", 3);
	check (!nemo_delete_guard_sweeps_home (list));
	g_list_free_full (list, g_object_unref);

	list = files_in (home_link, "item", 5);
	check (nemo_delete_guard_sweeps_home (list));
	g_list_free_full (list, g_object_unref);

	make_files (outside, "item", 0, 5);
	list = files_in (outside, "item", 5);
	check (!nemo_delete_guard_sweeps_home (list));
	g_list_free_full (list, g_object_unref);

	/* Five of twenty is not most of it. */
	make_files (home, "item", 6, 18);
	list = files_in (home, "item", 5);
	check (!nemo_delete_guard_sweeps_home (list));
	g_list_free_full (list, g_object_unref);

	check (nemo_delete_guard_must_ask (FALSE, 1, 0));
	check (!nemo_delete_guard_must_ask (TRUE, 1, 20));
	check (nemo_delete_guard_must_ask (TRUE, 20, 20));
	check (!nemo_delete_guard_must_ask (TRUE, 500, 0));

	now = g_get_monotonic_time ();
	check (nemo_delete_guard_in_grace (now - G_USEC_PER_SEC / 10, now));
	check (!nemo_delete_guard_in_grace (now - G_USEC_PER_SEC * 5, now));
	check (!nemo_delete_guard_in_grace (0, now));

	g_free (link);
	g_free (inner);
	g_free (tree);
	g_free (precious);
	g_free (outside);
	g_free (home_link);
	g_free (notes);
	g_free (docs);
	g_free (home);
	g_free (root);

	return failures == 0 ? 0 : 1;
}
