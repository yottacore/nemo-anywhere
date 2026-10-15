/* Edit link: a new target or name for a link, and never a change to what it
 * pointed at before.
 */

#include <config.h>

#include <string.h>
#include <gio/gio.h>
#include <glib/gstdio.h>

#include <libnemo-private/nemo-delete-guard.h>
#include <libnemo-private/nemo-link-copy.h>
#include <libnemo-private/nemo-link-edit.h>
#include <libnemo-private/nemo-lnk.h>

#include "test-scratch.h"
#include "test-check.h"

static char *
target_of (const char *path)
{
	GFile *file = g_file_new_for_path (path);
	char *target = NULL;

	if (nemo_link_kind (file, NULL) == NEMO_LINK_NONE ||
	    !nemo_link_read_target (file, &target, NULL)) {
		target = NULL;
	}
	g_object_unref (file);

	return target;
}

static gboolean
reads_as (const char *path, const char *want)
{
	char *contents = NULL;
	gboolean same = g_file_get_contents (path, &contents, NULL, NULL) && g_strcmp0 (contents, want) == 0;

	g_free (contents);
	return same;
}

static void
check_file_links (const char *dir)
{
	char *one = g_build_filename (dir, "one.txt", NULL);
	char *two = g_build_filename (dir, "two.txt", NULL);
	char *link = g_build_filename (dir, "link", NULL);
	char *renamed = g_build_filename (dir, "renamed", NULL);
	char *taken = g_build_filename (dir, "taken", NULL);
	char *target;
	GError *error = NULL;

	check (g_file_set_contents (one, "1", -1, NULL));
	check (g_file_set_contents (two, "2", -1, NULL));
	check (g_file_set_contents (taken, "t", -1, NULL));
	check (nemo_link_create ("one.txt", link, dir, NEMO_LINK_FILE_SYMLINK, NULL));

	/* A new target under the same name, spelled as typed. */
	check (nemo_link_edit_symlink (link, "link", "two.txt", &error));
	g_clear_error (&error);
	target = target_of (link);
	check (g_strcmp0 (target, "two.txt") == 0);
	g_free (target);
	check (reads_as (link, "2"));

	/* A name only. */
	check (nemo_link_edit_symlink (link, "renamed", "two.txt", NULL));
	check (!g_file_test (link, G_FILE_TEST_EXISTS | G_FILE_TEST_IS_SYMLINK));
	target = target_of (renamed);
	check (g_strcmp0 (target, "two.txt") == 0);
	g_free (target);

	/* Both, back again. */
	check (nemo_link_edit_symlink (renamed, "link", "one.txt", NULL));
	check (!g_file_test (renamed, G_FILE_TEST_IS_SYMLINK));
	check (reads_as (link, "1"));

	/* Neither target was touched along the way. */
	check (reads_as (one, "1") && reads_as (two, "2"));

	/* Refused, and the link stays as it was. */
	check (!nemo_link_edit_symlink (link, "taken", "two.txt", &error));
	check (g_error_matches (error, G_IO_ERROR, G_IO_ERROR_EXISTS));
	g_clear_error (&error);
	check (reads_as (taken, "t"));
	check (!nemo_link_edit_symlink (link, "a/b", "two.txt", &error));
	check (error != NULL);
	g_clear_error (&error);
	check (!nemo_link_edit_symlink (link, "link", "", &error));
	check (error != NULL);
	g_clear_error (&error);
	check (!nemo_link_edit_symlink (one, "one.txt", "two.txt", &error));
	check (error != NULL);
	g_clear_error (&error);
	check (reads_as (link, "1") && reads_as (one, "1"));

	/* A target that is not there yet is allowed, as it is for any symlink. */
	check (nemo_link_edit_symlink (link, "link", "later.txt", NULL));
	target = target_of (link);
	check (g_strcmp0 (target, "later.txt") == 0);
	g_free (target);

	g_remove (link);
	g_free (one);
	g_free (two);
	g_free (link);
	g_free (renamed);
	g_free (taken);
}

/* A folder link, of each kind this machine can make. Whatever was in the
   folder it used to point at is still there. */
static void
check_folder_link (const char *dir, NemoLinkKind kind)
{
	char *first = g_build_filename (dir, "first", NULL);
	char *second = g_build_filename (dir, "second", NULL);
	char *inside = g_build_filename (first, "keep.txt", NULL);
	char *link = g_build_filename (dir, "folder-link", NULL);
	char *moved = g_build_filename (dir, "folder-link-2", NULL);
	char *through = g_build_filename (moved, "keep.txt", NULL);
	GFile *file;

	g_mkdir_with_parents (first, 0700);
	g_mkdir_with_parents (second, 0700);
	check (g_file_set_contents (inside, "k", -1, NULL));
	check (nemo_link_create (first, link, dir, kind, NULL));

	check (nemo_link_edit_symlink (link, "folder-link", second, NULL));
	check (reads_as (inside, "k"));
	check (nemo_link_edit_symlink (link, "folder-link-2", first, NULL));
	check (reads_as (inside, "k"));
	check (reads_as (through, "k"));

	file = g_file_new_for_path (moved);
	check (nemo_link_kind (file, NULL) == kind);
	g_object_unref (file);

#ifdef G_OS_WIN32
	g_rmdir (moved);
#else
	g_remove (moved);
#endif
	g_free (first);
	g_free (second);
	g_free (inside);
	g_free (link);
	g_free (moved);
	g_free (through);
}

static void
check_shortcut (const char *dir)
{
	char *target = g_build_filename (dir, "target.txt", NULL);
	char *lnk = g_build_filename (dir, "short.lnk", NULL);
	char *renamed = g_build_filename (dir, "other.lnk", NULL);
	char *clash = g_build_filename (dir, "clash.lnk", NULL);
	GError *error = NULL;
	NemoLnk read;

	check (g_file_set_contents (target, "x", -1, NULL));
	check (nemo_lnk_write (lnk, target, TRUE, NULL));
	check (g_file_set_contents (clash, "c", -1, NULL));

	/* ".lnk" goes back on, and the paths are left alone when not asked. */
	check (nemo_link_edit_shortcut (lnk, "other", FALSE, NULL, NULL, NULL, &error));
	g_clear_error (&error);
	check (!g_file_test (lnk, G_FILE_TEST_EXISTS));
	check (nemo_lnk_read (renamed, &read));
	check (read.relative_path != NULL);
	nemo_lnk_clear (&read);

	check (nemo_link_edit_shortcut (renamed, "other.lnk", TRUE, "C:\\x\\y.txt", "", "", NULL));
	check (nemo_lnk_read (renamed, &read));
	check (g_strcmp0 (read.local_path, "C:\\x\\y.txt") == 0 && read.relative_path == NULL);
	nemo_lnk_clear (&read);

	check (!nemo_link_edit_shortcut (renamed, "clash", FALSE, NULL, NULL, NULL, &error));
	check (g_error_matches (error, G_IO_ERROR, G_IO_ERROR_EXISTS));
	g_clear_error (&error);
	check (reads_as (clash, "c"));

	/* A bad path change leaves the name as it was too. */
	check (!nemo_link_edit_shortcut (renamed, "third", TRUE, "", "", "", &error));
	g_clear_error (&error);
	check (g_file_test (renamed, G_FILE_TEST_EXISTS));

#ifndef G_OS_WIN32
	/* A save keeps the permissions the shortcut had. */
	{
		GStatBuf info;

		check (g_chmod (renamed, 0604) == 0);
		check (nemo_link_edit_shortcut (renamed, "other", TRUE, "C:\\x\\z.txt", "", "", NULL));
		check (g_stat (renamed, &info) == 0 && (info.st_mode & 0777) == 0604);
	}
#endif

	g_free (target);
	g_free (lnk);
	g_free (renamed);
	g_free (clash);
}

/* A symlink named .lnk is a symlink. It gets the target editor, and the
   shortcut it points at is never written through it. */
static void
check_symlink_named_lnk (const char *dir)
{
	char *other = g_build_filename (dir, "other-dir", NULL);
	char *real = g_build_filename (other, "real.lnk", NULL);
	char *link = g_build_filename (dir, "s.lnk", NULL);
	char *spelled = g_build_filename ("other-dir", "real.lnk", NULL);
	char *before = NULL, *after = NULL, *target;
	gsize before_length = 0, after_length = 0;
	GFile *file;
	GError *error = NULL;

	g_mkdir_with_parents (other, 0700);
	check (nemo_lnk_write (real, other, TRUE, NULL));
	check (g_file_get_contents (real, &before, &before_length, NULL));
	check (nemo_link_create (spelled, link, dir, NEMO_LINK_FILE_SYMLINK, NULL));

	file = g_file_new_for_path (link);
	check (!nemo_link_edit_is_shortcut (file));
	g_object_unref (file);
	file = g_file_new_for_path (real);
	check (nemo_link_edit_is_shortcut (file));
	g_object_unref (file);

	/* Refused, and both are left as they were. */
	check (!nemo_link_edit_shortcut (link, "s", TRUE, "C:\\x\\y.txt", "", "", &error));
	check (error != NULL);
	g_clear_error (&error);
	target = target_of (link);
	check (g_strcmp0 (target, spelled) == 0);
	g_free (target);
	check (g_file_get_contents (real, &after, &after_length, NULL));
	check (before_length == after_length && memcmp (before, after, before_length) == 0);

	check (nemo_link_edit_symlink (link, "s.lnk", "other-dir", NULL));
	target = target_of (link);
	check (g_strcmp0 (target, "other-dir") == 0);
	g_free (target);

	g_remove (link);
	g_remove (real);
	g_rmdir (other);
	g_free (before);
	g_free (after);
	g_free (spelled);
	g_free (other);
	g_free (real);
	g_free (link);
}

static char *
fold (const char *name)
{
	char *nfc = g_utf8_normalize (name, -1, G_NORMALIZE_NFC);
	char *folded = g_utf8_casefold (nfc, -1);

	g_free (nfc);
	return folded;
}

/* The entries in dir spelled like name, ignoring case and normalization, and
   the first one's name. Read from the listing, since on some file systems a
   lookup of the other spelling answers from a stale cache. */
static int
entries_like (const char *dir, const char *name, char **found)
{
	GDir *listing = g_dir_open (dir, 0, NULL);
	char *want = fold (name);
	const char *entry;
	int count = 0;

	*found = NULL;
	while (listing != NULL && (entry = g_dir_read_name (listing)) != NULL) {
		char *have = fold (entry);

		if (strcmp (have, want) == 0) {
			if (count++ == 0) {
				*found = g_build_filename (dir, entry, NULL);
			}
		}
		g_free (have);
	}
	if (listing != NULL) {
		g_dir_close (listing);
	}
	g_free (want);

	return count;
}

/* Where two spellings name one entry, a case-insensitive or a normalizing
   file system, a new target under the other spelling still leaves a link. */
static void
check_other_spelling (const char *dir)
{
	static const char *const spellings[][2] = {
		{ "Spelled", "spelled" },
		{ "caf\xc3\xa9", "cafe\xcc\x81" },
	};
	char *one = g_build_filename (dir, "sp-one.txt", NULL);
	char *two = g_build_filename (dir, "sp-two.txt", NULL);
	char *first = NULL, *second = NULL, *found = NULL, *target;
	const char *second_name = NULL;
	GError *error = NULL;
	guint i;

	/* Probed under other names, since a lookup can leave a cached entry
	   behind for the spelling that was not made. */
	for (i = 0; i < G_N_ELEMENTS (spellings) && second_name == NULL; i++) {
		char *probe_name = g_strconcat ("probe-", spellings[i][0], NULL);
		char *other_name = g_strconcat ("probe-", spellings[i][1], NULL);
		char *probe = g_build_filename (dir, probe_name, NULL);
		char *other = g_build_filename (dir, other_name, NULL);

		if (g_file_set_contents (probe, "probe", -1, NULL) &&
		    g_file_test (other, G_FILE_TEST_EXISTS)) {
			first = g_build_filename (dir, spellings[i][0], NULL);
			second = g_build_filename (dir, spellings[i][1], NULL);
			second_name = spellings[i][1];
		}
		g_remove (probe);
		g_free (probe_name);
		g_free (other_name);
		g_free (probe);
		g_free (other);
	}
	if (second_name == NULL) {
		g_printerr ("note: every spelling is its own name here, so a rename to another spelling is not checked\n");
		g_free (one);
		g_free (two);
		return;
	}

	check (g_file_set_contents (one, "1", -1, NULL));
	check (g_file_set_contents (two, "2", -1, NULL));
	check (nemo_link_create ("sp-one.txt", first, dir, NEMO_LINK_FILE_SYMLINK, NULL));

	check (nemo_link_edit_symlink (first, second_name, "sp-two.txt", &error));
	g_clear_error (&error);
	check (entries_like (dir, second_name, &found) == 1);
	target = found != NULL ? target_of (found) : NULL;
	check (g_strcmp0 (target, "sp-two.txt") == 0);
	g_free (target);
	check (reads_as (one, "1") && reads_as (two, "2"));

	if (found != NULL) {
		g_remove (found);
	}
	g_free (found);
	g_free (first);
	g_free (second);
	g_free (one);
	g_free (two);
}

/* The old link is taken away through the guard, which takes nothing else. */
static void
check_guard_takes_only_links (const char *dir)
{
	char *plain = g_build_filename (dir, "plain.txt", NULL);
	char *folder = g_build_filename (dir, "real-folder", NULL);
	GFile *file;
	GError *error = NULL;

	check (g_file_set_contents (plain, "p", -1, NULL));
	g_mkdir_with_parents (folder, 0700);

	file = g_file_new_for_path (plain);
	check (!nemo_delete_guard_remove_link (file, &error));
	check (error != NULL);
	g_clear_error (&error);
	g_object_unref (file);

	file = g_file_new_for_path (folder);
	check (!nemo_delete_guard_remove_link (file, NULL));
	g_object_unref (file);

	check (reads_as (plain, "p"));
	check (g_file_test (folder, G_FILE_TEST_IS_DIR));

	g_free (plain);
	g_free (folder);
}

int
main (int argc, char **argv)
{
	char *dir;
	guint supported;

	g_test_init (&argc, &argv, NULL);

	dir = test_scratch_dir ("nemo-link-edit-XXXXXX", NULL);
	g_assert (dir != NULL);

	check_shortcut (dir);
	check_guard_takes_only_links (dir);

	supported = nemo_link_kinds_supported (dir);
	if (supported & NEMO_LINK_FILE_SYMLINK) {
		check_file_links (dir);
		check_symlink_named_lnk (dir);
		check_other_spelling (dir);
	} else {
		g_printerr ("note: symlinks are not permitted here, so those checks are skipped\n");
	}
	if (supported & NEMO_LINK_DIR_SYMLINK) {
		check_folder_link (dir, NEMO_LINK_DIR_SYMLINK);
	}
	if (supported & NEMO_LINK_JUNCTION) {
		check_folder_link (dir, NEMO_LINK_JUNCTION);
	}

	g_free (dir);

	g_printerr ("%d failure(s)\n", failures);
	return failures == 0 ? 0 : 1;
}
