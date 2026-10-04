/* Where a link leads, on Windows.
 *
 * GIO gives a link that leads nowhere the type the link has itself, a file or
 * a folder, so a broken link never read as broken there. The answer now comes
 * from the links themselves, read without opening anything through one, so a
 * link to a share is never looked into. And Windows will not follow a
 * relative symlink spelled with /, so the app writes \ in the ones it makes.
 */

#include <config.h>

#include <stdlib.h>
#include <string.h>
#include <gio/gio.h>
#include <glib/gstdio.h>
#include <gtk/gtk.h>

#include <libnemo-private/nemo-file.h>
#include <libnemo-private/nemo-file-private.h>
#include <libnemo-private/nemo-link-copy.h>
#include <libnemo-private/nemo-link-win32.h>

#include <windows.h>

#include "test-scratch.h"
#include "test-check.h"

#define WANTED "standard::*,unix::*,time::*,id::filesystem"

/* TEST-NET-1, which nothing answers for. Opening through a link to it fails,
   so a check that went there would call the link broken. */
#define SHARE_TARGET "\\\\192.0.2.1\\share\\file.txt"

static char *root;

static char *
at (const char *name)
{
	return g_build_filename (root, name, NULL);
}

/* What the file list sees: the link itself, never its target. */
static GFileInfo *
query_link (const char *path)
{
	GFile *file = g_file_new_for_path (path);
	GFileInfo *info = g_file_query_info (file, WANTED,
					     G_FILE_QUERY_INFO_NOFOLLOW_SYMLINKS,
					     NULL, NULL);

	g_object_unref (file);
	return info;
}

static NemoFile *
loaded_file (const char *path)
{
	GFile *location = g_file_new_for_path (path);
	NemoFile *file = nemo_file_get (location);
	GFileInfo *info = query_link (path);

	g_object_unref (location);
	if (file != NULL && info != NULL) {
		nemo_file_update_info (file, info);
	}
	g_clear_object (&info);

	return file;
}

/* A symlink written the way another program might, with no say from us. */
static gboolean
raw_symlink (const char *link, const char *target, gboolean is_dir)
{
	gunichar2 *w_link = g_utf8_to_utf16 (link, -1, NULL, NULL, NULL);
	gunichar2 *w_target = g_utf8_to_utf16 (target, -1, NULL, NULL, NULL);
	DWORD flags = 0x2;	/* SYMBOLIC_LINK_FLAG_ALLOW_UNPRIVILEGED_CREATE */
	gboolean ok;

	if (is_dir) {
		flags |= SYMBOLIC_LINK_FLAG_DIRECTORY;
	}
	ok = CreateSymbolicLinkW ((LPCWSTR) w_link, (LPCWSTR) w_target, flags) != 0;
	g_free (w_link);
	g_free (w_target);

	return ok;
}

static void
expect_broken (const char *name, gboolean broken)
{
	char *path = at (name);
	NemoFile *file = loaded_file (path);
	gint64 started = g_get_monotonic_time ();
	gboolean answer;

	check (file != NULL);
	if (file == NULL) {
		g_free (path);
		return;
	}

	answer = nemo_file_is_broken_symbolic_link (file);
	if (answer != broken) {
		g_printerr ("FAIL: %s reads as %s\n", name, answer ? "broken" : "not broken");
		failures++;
	}
	/* Nothing it reads is more than a folder listing away. */
	check (g_get_monotonic_time () - started < G_USEC_PER_SEC);

	if (broken) {
		char *type = nemo_file_get_type_as_string (file);

		check (g_strcmp0 (type, "link (broken)") == 0);
		g_free (type);
	}

	nemo_file_unref (file);
	g_free (path);
}

static char *
read_through (const char *name)
{
	char *path = at (name);
	char *contents = NULL;

	g_file_get_contents (path, &contents, NULL, NULL);
	g_free (path);

	return contents;
}

/* The app's own symlinks with / in a relative target. */
static void
check_made_with_slash (void)
{
	char *rel_file = at ("rel-file");
	char *rel_dir = at ("rel-dir");
	char *up_link = at ("other\\up-file");
	char *contents, *target = NULL;

	check (nemo_link_create ("sub/target.txt", rel_file, NULL, NEMO_LINK_FILE_SYMLINK, NULL));
	check (nemo_link_create ("sub/dir", rel_dir, NULL, NEMO_LINK_DIR_SYMLINK, NULL));
	check (nemo_link_create ("../sub/target.txt", up_link, NULL, NEMO_LINK_FILE_SYMLINK, NULL));

	contents = read_through ("rel-file");
	check (g_strcmp0 (contents, "target") == 0);
	g_free (contents);
	contents = read_through ("rel-dir\\in.txt");
	check (g_strcmp0 (contents, "inside") == 0);
	g_free (contents);
	contents = read_through ("other\\up-file");
	check (g_strcmp0 (contents, "target") == 0);
	g_free (contents);

	check (nemo_win32_link_leads_somewhere (rel_file));
	check (nemo_win32_link_leads_somewhere (rel_dir));
	check (nemo_win32_link_read_target (rel_file, &target, NULL));
	check (g_strcmp0 (target, "sub\\target.txt") == 0);
	g_free (target);

	expect_broken ("rel-file", FALSE);
	expect_broken ("rel-dir", FALSE);
	expect_broken ("other\\up-file", FALSE);

	g_free (up_link);
	g_free (rel_dir);
	g_free (rel_file);
}

static void
check_symlinks (void)
{
	char *target = at ("sub\\target.txt");
	char *path;

	/* Leads nowhere. */
	path = at ("gone-file");
	check (nemo_link_create ("nowhere", path, NULL, NEMO_LINK_FILE_SYMLINK, NULL));
	g_free (path);
	path = at ("gone-dir");
	check (nemo_link_create ("nowhere", path, NULL, NEMO_LINK_DIR_SYMLINK, NULL));
	g_free (path);
	path = at ("gone-abs");
	check (nemo_link_create ("C:\\no\\such\\place.txt", path, NULL, NEMO_LINK_FILE_SYMLINK, NULL));
	g_free (path);

	/* Somewhere. */
	path = at ("good-abs");
	check (nemo_link_create (target, path, NULL, NEMO_LINK_FILE_SYMLINK, NULL));
	g_free (path);

	/* Windows follows none of these, so neither may we. */
	path = at ("raw-slash");
	check (raw_symlink (path, "sub/target.txt", FALSE));
	check (!nemo_win32_link_leads_somewhere (path));
	g_free (path);
	path = at ("raw-star");
	check (raw_symlink (path, "sub\\*.txt", FALSE));
	g_free (path);

	/* Through other links. */
	path = at ("chain-1");
	check (nemo_link_create ("chain-2", path, NULL, NEMO_LINK_FILE_SYMLINK, NULL));
	g_free (path);
	path = at ("chain-2");
	check (nemo_link_create ("good-abs", path, NULL, NEMO_LINK_FILE_SYMLINK, NULL));
	g_free (path);
	path = at ("chain-gone");
	check (nemo_link_create ("gone-file", path, NULL, NEMO_LINK_FILE_SYMLINK, NULL));
	g_free (path);
	path = at ("through-dir");
	check (nemo_link_create ("rel-dir\\in.txt", path, NULL, NEMO_LINK_FILE_SYMLINK, NULL));
	g_free (path);
	path = at ("loop-1");
	check (nemo_link_create ("loop-2", path, NULL, NEMO_LINK_FILE_SYMLINK, NULL));
	g_free (path);
	path = at ("loop-2");
	check (nemo_link_create ("loop-1", path, NULL, NEMO_LINK_FILE_SYMLINK, NULL));
	g_free (path);

	/* On a share, directly or by way of another link: not looked into, so
	   not called broken. */
	path = at ("share");
	check (nemo_link_create (SHARE_TARGET, path, NULL, NEMO_LINK_FILE_SYMLINK, NULL));
	g_free (path);
	path = at ("to-share");
	check (nemo_link_create ("share", path, NULL, NEMO_LINK_FILE_SYMLINK, NULL));
	g_free (path);

	expect_broken ("gone-file", TRUE);
	expect_broken ("gone-dir", TRUE);
	expect_broken ("gone-abs", TRUE);
	expect_broken ("good-abs", FALSE);
	expect_broken ("raw-slash", TRUE);
	expect_broken ("raw-star", TRUE);
	expect_broken ("chain-1", FALSE);
	expect_broken ("chain-gone", TRUE);
	expect_broken ("through-dir", FALSE);
	expect_broken ("loop-1", TRUE);
	expect_broken ("share", FALSE);
	expect_broken ("to-share", FALSE);
	expect_broken ("sub\\target.txt", FALSE);

	g_free (target);
}

/* No privilege needed, so this half runs on any box. */
static void
check_junctions (void)
{
	char *real = at ("sub\\dir");
	char *doomed = at ("doomed");
	char *good = at ("junction-good");
	char *gone = at ("junction-gone");

	g_mkdir (doomed, 0700);
	check (nemo_link_create (real, good, NULL, NEMO_LINK_JUNCTION, NULL));
	check (nemo_link_create (doomed, gone, NULL, NEMO_LINK_JUNCTION, NULL));
	check (g_rmdir (doomed) == 0);

	expect_broken ("junction-good", FALSE);
	expect_broken ("junction-gone", TRUE);
	expect_broken ("sub", FALSE);

	g_free (gone);
	g_free (good);
	g_free (doomed);
	g_free (real);
}

/* The answer is kept, since the type column asks on every draw, and asked
   again once fresh info comes in. */
static void
check_kept_until_reload (void)
{
	char *target = at ("fleeting.txt");
	char *link = at ("fleeting-link");
	NemoFile *file;
	GFileInfo *info;

	check (g_file_set_contents (target, "x", 1, NULL));
	check (nemo_link_create (target, link, NULL, NEMO_LINK_FILE_SYMLINK, NULL));

	file = loaded_file (link);
	check (!nemo_file_is_broken_symbolic_link (file));
	check (g_remove (target) == 0);
	check (!nemo_file_is_broken_symbolic_link (file));

	info = query_link (link);
	nemo_file_update_info (file, info);
	g_object_unref (info);
	check (nemo_file_is_broken_symbolic_link (file));

	nemo_file_unref (file);
	g_free (link);
	g_free (target);
}

int
main (int argc, char *argv[])
{
	char *sub, *dir, *other;
	gboolean symlinks;

	gtk_init_check (&argc, &argv);

	root = test_scratch_dir ("nemo-link-end-test-XXXXXX", NULL);
	if (root == NULL) {
		g_printerr ("could not make a temporary directory\n");
		return EXIT_FAILURE;
	}

	sub = at ("sub");
	dir = at ("sub\\dir");
	other = at ("other");
	g_mkdir (sub, 0700);
	g_mkdir (dir, 0700);
	g_mkdir (other, 0700);
	{
		char *target = at ("sub\\target.txt");
		char *inside = at ("sub\\dir\\in.txt");

		check (g_file_set_contents (target, "target", -1, NULL));
		check (g_file_set_contents (inside, "inside", -1, NULL));
		g_free (inside);
		g_free (target);
	}

	check_junctions ();

	symlinks = nemo_win32_link_symlinks_allowed ();
	if (symlinks) {
		check_made_with_slash ();
		check_symlinks ();
		check_kept_until_reload ();
	} else {
		/* Developer Mode or an elevated run */
		g_print ("SKIP: this machine will not make a symlink, so only junctions ran\n");
	}

	g_free (other);
	g_free (dir);
	g_free (sub);
	g_free (root);

	if (failures > 0) {
		g_printerr ("%d failure(s)\n", failures);
		return EXIT_FAILURE;
	}
	if (!symlinks) {
		return 77;
	}

	g_print ("link end: all checks passed\n");
	return EXIT_SUCCESS;
}
