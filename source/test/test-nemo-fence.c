/* The fence stops a test that deletes or moves outside its own folders, and
 * lets the same calls through inside them.
 *
 * Each try runs in a child of this binary, told that only allowed/ is its
 * own. Its target sits beside that in outside/, which this test planted for
 * the purpose: both live in this test's own scratch dir. A refused try has to
 * end the child, say so on stderr, and leave the target where it was. */

#include <config.h>

#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <glib.h>
#include <glib/gstdio.h>
#include <gio/gio.h>

#ifdef G_OS_WIN32
#include <windows.h>
#else
#include <fcntl.h>
#include <unistd.h>
#endif

#include <libnemo-private/nemo-delete-guard.h>
#ifdef G_OS_WIN32
#include <libnemo-private/nemo-trash-win32.h>
#endif

#include "test-fence.h"
#include "test-scratch.h"
#include "test-check.h"

static const char *self;
static char *allowed;
static char *outside;

#ifdef G_OS_WIN32
/* The bin item on the same drive as path whose contents are marker, as its $R
   path. NULL when there's none. */
static char *
bin_item (const char *path, const char *marker)
{
	g_autofree char *bin = g_strdup_printf ("%c:\\$Recycle.Bin", path[0]);
	GDir *sids = g_dir_open (bin, 0, NULL);
	const char *sid;
	char *found = NULL;

	while (sids != NULL && found == NULL && (sid = g_dir_read_name (sids)) != NULL) {
		g_autofree char *sid_dir = g_build_filename (bin, sid, NULL);
		GDir *items = g_dir_open (sid_dir, 0, NULL);
		const char *name;

		while (items != NULL && found == NULL && (name = g_dir_read_name (items)) != NULL) {
			g_autofree char *item = g_build_filename (sid_dir, name, NULL);
			g_autofree char *contents = NULL;

			if (g_str_has_prefix (name, "$R") && g_file_get_contents (item, &contents, NULL, NULL) &&
			    g_strcmp0 (contents, marker) == 0) {
				found = g_steal_pointer (&item);
			}
		}
		if (items != NULL) {
			g_dir_close (items);
		}
	}
	if (sids != NULL) {
		g_dir_close (sids);
	}
	return found;
}

static char *
twin_of (const char *item)
{
	char *twin = g_strdup (item);

	strrchr (twin, '\\')[2] = 'I';
	return twin;
}

/* Recycles path, whose contents are marker, and takes it out of the bin again,
   $I twin and all. */
static int
recycle_and_clear (const char *path, const char *marker)
{
	g_autoptr (GFile) file = g_file_new_for_path (path);
	g_autofree char *item = NULL;
	g_autofree char *twin = NULL;

	if (!nemo_trash_win32_recycle (file, NULL) || (item = bin_item (path, marker)) == NULL) {
		return 3;
	}
	twin = twin_of (item);
	return g_unlink (item) == 0 && g_unlink (twin) == 0 ? 0 : 4;
}

static gboolean
under_wine (void)
{
	HMODULE ntdll = GetModuleHandleW (L"ntdll.dll");

	return ntdll != NULL && GetProcAddress (ntdll, "wine_get_version") != NULL;
}
#endif

/* The child's half. Exits 0 when the call came back, whatever it answered;
   the fence is the only thing here that may end it early. */
static int
child (const char *what, const char *a, const char *b)
{
	if (g_strcmp0 (what, "unlink") == 0) {
		g_unlink (a);
	} else if (g_strcmp0 (what, "rmdir") == 0) {
		g_rmdir (a);
	} else if (g_strcmp0 (what, "remove") == 0) {
		g_remove (a);
	} else if (g_strcmp0 (what, "rename") == 0) {
		g_rename (a, b);
	} else if (g_strcmp0 (what, "gio") == 0) {
		g_autoptr (GFile) file = g_file_new_for_path (a);

		g_file_delete (file, NULL, NULL);
	} else if (g_strcmp0 (what, "tree") == 0) {
		g_autoptr (GFile) file = g_file_new_for_path (a);

		nemo_delete_guard_remove_tree (file, NULL);
	} else if (g_strcmp0 (what, "relative") == 0) {
		if (g_chdir (a) != 0) {
			return 2;
		}
		g_unlink (b);
#ifndef G_OS_WIN32
	} else if (g_strcmp0 (what, "unlinkat") == 0) {
		int dir = open (a, O_RDONLY | O_DIRECTORY);

		if (dir < 0) {
			return 2;
		}
		unlinkat (dir, b, 0);
		close (dir);
#else
	} else if (g_strcmp0 (what, "recycle") == 0) {
		return recycle_and_clear (a, b);
#endif
	} else {
		return 2;
	}
	return 0;
}

/* Runs one try in a child whose own folder is allowed/ alone. TRUE when the
   child ended the way want_refused says, and the error output matched. */
static gboolean
try_it (gboolean want_refused, const char *what, const char *a, const char *b)
{
	g_auto (GStrv) env = g_environ_setenv (g_get_environ (), "NEMO_TEST_SCRATCH", allowed, TRUE);
	/* "-" for no second path: Windows drops an empty argument on the way. */
	const char *argv[] = { self, "--child", what, a, b != NULL ? b : "-", NULL };
	g_autofree char *err = NULL;
	g_autoptr (GError) error = NULL;
	gboolean exited_ok, refused;
	int status = 0;

	env = g_environ_setenv (env, "NEMO_TEST_FENCE_CHILD", "1", TRUE);
	if (!g_spawn_sync (NULL, (char **) argv, env, G_SPAWN_DEFAULT, NULL, NULL, NULL, &err, &status, &error)) {
		g_printerr ("could not start the child: %s\n", error->message);
		return FALSE;
	}
	exited_ok = g_spawn_check_wait_status (status, NULL);
	refused = err != NULL && strstr (err, "fence: refused") != NULL;
	if (want_refused == (!exited_ok && refused)) {
		return TRUE;
	}
	g_printerr ("%s %s %s: wanted %s, got %s\n%s", what, a, b != NULL ? b : "",
		    want_refused ? "refused" : "let through", exited_ok ? "a clean exit" : "a failed child",
		    err != NULL ? err : "");
	return FALSE;
}

static char *
plant (const char *dir, const char *name)
{
	char *path = g_build_filename (dir, name, NULL);

	check (g_file_set_contents (path, "planted", -1, NULL));
	return path;
}

static void
check_refusals (void)
{
	g_autofree char *target = plant (outside, "target.txt");
	g_autofree char *folder = g_build_filename (outside, "folder", NULL);
	g_autofree char *empty = g_build_filename (outside, "empty", NULL);
	g_autofree char *mine = plant (allowed, "mine.txt");
	g_autofree char *moved_to = g_build_filename (allowed, "moved-here.txt", NULL);
	g_autofree char *sneaky = g_build_filename (allowed, "..", "outside", "target.txt", NULL);
	g_autofree char *up = g_build_filename ("..", "outside", "target.txt", NULL);

	check (g_mkdir (folder, 0700) == 0);
	check (g_mkdir (empty, 0700) == 0);
	g_free (plant (folder, "inside.txt"));

	check (try_it (TRUE, "unlink", target, NULL));
	check (try_it (TRUE, "remove", target, NULL));
	check (try_it (TRUE, "rmdir", empty, NULL));
	check (try_it (TRUE, "gio", target, NULL));
	check (try_it (TRUE, "rename", target, moved_to));
	check (try_it (TRUE, "rename", mine, target));
	check (try_it (TRUE, "unlink", sneaky, NULL));
	check (try_it (TRUE, "relative", allowed, up));
	/* The app's own tree removal, which a test drives often. */
	check (try_it (TRUE, "tree", folder, NULL));
#ifndef G_OS_WIN32
	check (try_it (TRUE, "unlinkat", outside, "target.txt"));
#endif

	check (g_file_test (target, G_FILE_TEST_IS_REGULAR));
	check (g_file_test (folder, G_FILE_TEST_IS_DIR));
	check (g_file_test (empty, G_FILE_TEST_IS_DIR));
	check (g_file_test (mine, G_FILE_TEST_IS_REGULAR));
	check (!g_file_test (moved_to, G_FILE_TEST_EXISTS));
}

#ifndef G_OS_WIN32
/* A link inside the child's folder that leads out. The link itself is the
   child's to delete; what is behind it is not. */
static void
check_links (void)
{
	g_autofree char *target = plant (outside, "behind-link.txt");
	g_autofree char *link = g_build_filename (allowed, "to-outside", NULL);
	g_autofree char *through = g_build_filename (link, "behind-link.txt", NULL);

	check (symlink (outside, link) == 0);
	check (try_it (TRUE, "unlink", through, NULL));
	check (g_file_test (target, G_FILE_TEST_IS_REGULAR));

	check (try_it (FALSE, "unlink", link, NULL));
	check (!g_file_test (link, G_FILE_TEST_EXISTS) && !g_file_test (link, G_FILE_TEST_IS_SYMLINK));
	check (g_file_test (target, G_FILE_TEST_IS_REGULAR));
}
#endif

/* Inside its own folder the same calls go through, or the fence would only be
   proving that a child can die. */
static void
check_let_through (void)
{
	g_autofree char *gone = plant (allowed, "gone.txt");
	g_autofree char *moved = plant (allowed, "moved.txt");
	g_autofree char *moved_to = g_build_filename (allowed, "moved-to.txt", NULL);
	g_autofree char *folder = g_build_filename (allowed, "folder", NULL);
	g_autofree char *tree = g_build_filename (allowed, "tree", NULL);

	check (g_mkdir (folder, 0700) == 0);
	check (g_mkdir (tree, 0700) == 0);
	g_free (plant (tree, "leaf.txt"));

	check (try_it (FALSE, "unlink", gone, NULL));
	check (try_it (FALSE, "rename", moved, moved_to));
	check (try_it (FALSE, "rmdir", folder, NULL));
	check (try_it (FALSE, "tree", tree, NULL));

	check (!g_file_test (gone, G_FILE_TEST_EXISTS));
	check (!g_file_test (moved, G_FILE_TEST_EXISTS) && g_file_test (moved_to, G_FILE_TEST_IS_REGULAR));
	check (!g_file_test (folder, G_FILE_TEST_EXISTS));
	check (!g_file_test (tree, G_FILE_TEST_EXISTS));
}

/* Outside, a delete of what isn't there goes through to fail on its own. It
   harms nothing, and fontconfig tries one on every font folder it scans. */
static void
check_not_there (void)
{
	g_autofree char *file = g_build_filename (outside, "never-there.txt", NULL);
	g_autofree char *folder = g_build_filename (outside, "never-there", NULL);

	check (try_it (FALSE, "unlink", file, NULL));
	check (try_it (FALSE, "rmdir", folder, NULL));
	check (!g_file_test (file, G_FILE_TEST_EXISTS) && !g_file_test (folder, G_FILE_TEST_EXISTS));
}

#ifdef G_OS_WIN32
/* What a test recycles is still its own in the bin, and nothing else in there
   is: not even another process's item from the same folder. */
static void
check_recycle_bin (void)
{
	g_autofree char *marker = g_strdup_printf ("fence bin %lu %" G_GINT64_FORMAT,
						   GetCurrentProcessId (), g_get_real_time ());
	g_autofree char *other_marker = g_strconcat (marker, " other", NULL);
	g_autofree char *mine = g_build_filename (allowed, "recycled.txt", NULL);
	g_autofree char *other = g_build_filename (outside, "recycled.txt", NULL);
	g_autoptr (GFile) other_file = g_file_new_for_path (other);
	g_autofree char *item = NULL;
	g_autofree char *twin = NULL;

	if (under_wine ()) {
		g_print ("skipped the Recycle Bin cases: wine keeps its trash elsewhere\n");
		return;
	}

	check (g_file_set_contents (mine, marker, -1, NULL));
	check (try_it (FALSE, "recycle", mine, marker));
	check (!g_file_test (mine, G_FILE_TEST_EXISTS));
	item = bin_item (mine, marker);
	check (item == NULL);
	g_clear_pointer (&item, g_free);

	/* Recycled by this process, so a child doesn't own it. */
	check (g_file_set_contents (other, other_marker, -1, NULL));
	check (nemo_trash_win32_recycle (other_file, NULL));
	item = bin_item (other, other_marker);
	check (item != NULL);
	if (item == NULL) {
		return;
	}
	twin = twin_of (item);
	check (try_it (TRUE, "unlink", item, NULL));
	check (try_it (TRUE, "unlink", twin, NULL));
	check (try_it (TRUE, "rename", item, mine));
	check (g_file_test (item, G_FILE_TEST_IS_REGULAR) && g_file_test (twin, G_FILE_TEST_IS_REGULAR));

	check (g_unlink (item) == 0);
	check (g_unlink (twin) == 0);
}
#endif

int
main (int argc, char **argv)
{
	g_autofree char *scratch = NULL;

	if (argc == 5 && strcmp (argv[1], "--child") == 0) {
		return child (argv[2], argv[3], strcmp (argv[4], "-") == 0 ? NULL : argv[4]);
	}
	/* A child that lost an argument must not start children of its own. */
	if (g_getenv ("NEMO_TEST_FENCE_CHILD") != NULL) {
		g_printerr ("a child was started with %d arguments\n", argc);
		return 2;
	}
	self = argv[0];

	scratch = test_scratch_dir ("nemo-fence-XXXXXX", NULL);
	check (scratch != NULL);
	if (scratch == NULL) {
		return EXIT_FAILURE;
	}
	allowed = g_build_filename (scratch, "allowed", NULL);
	outside = g_build_filename (scratch, "outside", NULL);
	check (g_mkdir (allowed, 0700) == 0);
	check (g_mkdir (outside, 0700) == 0);

	/* This process's own view: its scratch dir is in, the root of the file
	   system is not. */
	check (test_fence_allows (allowed));
	check (!test_fence_allows (G_DIR_SEPARATOR_S));

	check_refusals ();
#ifndef G_OS_WIN32
	check_links ();
#endif
	check_let_through ();
	check_not_there ();
#ifdef G_OS_WIN32
	check_recycle_bin ();
#endif

	g_free (allowed);
	g_free (outside);
	if (failures > 0) {
		g_printerr ("%d failure(s)\n", failures);
		return EXIT_FAILURE;
	}
	g_print ("OK\n");
	return EXIT_SUCCESS;
}
