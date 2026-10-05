/* The delete guard on Windows: home, the folders above it and a drive root are
 * never removed, however the path is spelled, and one job cannot take most of
 * home. Case, slashes, a short 8.3 name and a junction on the way all count. */

#include <config.h>

#include <string.h>
#include <glib.h>
#include <glib/gstdio.h>
#include <gio/gio.h>

#include <windows.h>

#include <libnemo-private/nemo-delete-guard.h>
#include <libnemo-private/nemo-link-win32.h>

#include "test-scratch.h"
#include "test-check.h"
#include "test-guard-common.h"

/* GetShortPathNameW with FALSE, or the long one with TRUE. */
static char *
respell (const char *path, gboolean long_form)
{
	gunichar2 *wide = g_utf8_to_utf16 (path, -1, NULL, NULL, NULL);
	WCHAR buf[MAX_PATH];
	DWORD len = 0;
	char *spelled = NULL;

	if (wide != NULL) {
		len = long_form ? GetLongPathNameW ((LPCWSTR) wide, buf, G_N_ELEMENTS (buf))
				: GetShortPathNameW ((LPCWSTR) wide, buf, G_N_ELEMENTS (buf));
	}
	if (len > 0 && len < G_N_ELEMENTS (buf)) {
		spelled = g_utf16_to_utf8 ((gunichar2 *) buf, len, NULL, NULL, NULL);
	}

	g_free (wide);
	return spelled != NULL ? spelled : g_strdup (path);
}

static gboolean
sweeps (const char *dir, const char *prefix, int count)
{
	GList *list = NULL;
	gboolean swept;
	int i;

	for (i = 0; i < count; i++) {
		char *name = g_strdup_printf ("%s%d", prefix, i);
		char *path = g_build_filename (dir, name, NULL);

		list = g_list_prepend (list, g_file_new_for_path (path));
		g_free (path);
		g_free (name);
	}

	swept = nemo_delete_guard_sweeps_home (list);
	g_list_free_full (list, g_object_unref);

	return swept;
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
	char *made, *root, *home, *docs, *notes, *spelled, *up, *through, *alias;
	char *outside, *precious, *tree, *inner, *link, *short_home;
	char drive[4];
	GFile *file;

	made = test_scratch_dir ("nemo-guard-XXXXXX", NULL);
	/* TEMP is sometimes the short spelling, which would hide the 8.3 case. */
	root = respell (made, TRUE);
	home = make_dir (root, "home");
	g_setenv ("HOME", home, TRUE);

	if (g_strcmp0 (g_get_home_dir (), home) != 0) {
		g_printerr ("SKIP: the home folder was read before HOME could be moved\n");
		return 77;
	}

	check (protected_path (home));
	check (protected_path (root));

	spelled = g_utf8_strup (home, -1);
	check (protected_path (spelled));
	g_free (spelled);

	spelled = g_strdup (home);
	g_strdelimit (spelled, "\\", '/');
	check (protected_path (spelled));
	g_free (spelled);

	spelled = g_build_filename (home, "..", "home", NULL);
	check (protected_path (spelled));
	g_free (spelled);

	if (g_ascii_isalpha (home[0]) && home[1] == ':') {
		drive[0] = home[0];
		drive[1] = ':';
		drive[2] = '\\';
		drive[3] = '\0';
		check (protected_path (drive));
	}

	docs = make_dir (home, "Documents");
	notes = make_file (home, "notes.txt");
	check (!protected_path (docs));
	check (!protected_path (notes));

	short_home = respell (home, FALSE);
	if (g_ascii_strcasecmp (short_home, home) == 0) {
		g_print ("SKIP short names: none are made on this volume\n");
	} else {
		check (protected_path (short_home));
	}

	/* A junction is a link, and never home itself. A path through one can be. */
	up = g_build_filename (root, "up", NULL);
	if (!nemo_win32_link_create (root, up, NULL, NEMO_LINK_JUNCTION, NULL)) {
		g_printerr ("SKIP: no junctions on this volume\n");
		return 77;
	}
	through = g_build_filename (up, "home", NULL);
	check (!protected_path (up));
	check (protected_path (through));
	spelled = g_build_filename (through, "Documents", NULL);
	check (!protected_path (spelled));
	g_free (spelled);

	/* What a junction inside a removed tree points at stays. */
	outside = make_dir (root, "outside");
	precious = make_file (outside, "precious.txt");
	tree = make_dir (root, "tree");
	inner = make_dir (tree, "inner");
	g_free (make_file (inner, "scratch.txt"));
	link = g_build_filename (tree, "link-out", NULL);
	check (nemo_win32_link_create (outside, link, NULL, NEMO_LINK_JUNCTION, NULL));

	/* What every tree walk asks before going in. GIO calls both of the links
	   here a folder. */
	check (real_folder (tree));
	check (!real_folder (link));
	check (!real_folder (up));
	check (!real_folder (precious));

	file = g_file_new_for_path (tree);
	check (nemo_delete_guard_remove_tree (file, NULL));
	g_object_unref (file);
	check (exists (precious));
	check (!exists (tree));

	file = g_file_new_for_path (home);
	check (!nemo_delete_guard_remove_tree (file, NULL));
	g_object_unref (file);
	file = g_file_new_for_path (through);
	check (!nemo_delete_guard_remove_tree (file, NULL));
	g_object_unref (file);
	check (exists (notes));
	check (exists (docs));

	/* Eight entries in home: Documents, notes.txt and six items. */
	make_files (home, "item", 0, 6);
	check (sweeps (home, "item", 5));
	check (!sweeps (home, "item", 3));

	spelled = g_utf8_strup (home, -1);
	check (sweeps (spelled, "item", 5));
	g_free (spelled);

	check (sweeps (through, "item", 5));
	if (g_ascii_strcasecmp (short_home, home) != 0) {
		check (sweeps (short_home, "item", 5));
	}

	alias = g_build_filename (root, "alias", NULL);
	check (nemo_win32_link_create (home, alias, NULL, NEMO_LINK_JUNCTION, NULL));
	check (sweeps (alias, "item", 5));

	make_files (outside, "item", 0, 5);
	check (!sweeps (outside, "item", 5));

	/* Five of twenty is not most of it. */
	make_files (home, "item", 6, 18);
	check (!sweeps (home, "item", 5));

	g_free (alias);
	g_free (link);
	g_free (inner);
	g_free (tree);
	g_free (precious);
	g_free (outside);
	g_free (through);
	g_free (up);
	g_free (short_home);
	g_free (notes);
	g_free (docs);
	g_free (home);
	g_free (root);
	g_free (made);

	return failures == 0 ? 0 : 1;
}
