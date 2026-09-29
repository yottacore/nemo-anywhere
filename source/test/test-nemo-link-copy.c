/* Copying a link should be able to leave a link rather than a copy of what it
 * points at. Windows makes that harder than POSIX: there are three kinds to
 * tell apart, the toolkit says only "this is a link", and the kind has to come
 * from the reparse tag. These checks cover reading a link, making one back
 * again, and the rule that decides what a link becomes when the destination
 * cannot hold its own kind.
 */

#include <config.h>

#include <stdlib.h>
#include <string.h>
#include <gio/gio.h>
#include <glib/gstdio.h>
#include <gtk/gtk.h>
#ifndef G_OS_WIN32
#include <unistd.h>
#endif

#include <libnemo-private/nemo-link-copy.h>
#include <libnemo-private/nemo-lnk.h>

#include "test-scratch.h"
#include "test-check.h"

/* Windows wants Developer Mode or an elevated run before it will make a
   symlink; where neither is on, those checks are skipped rather than failed. */
static gboolean symlinks;
static gboolean junctions;

static NemoLinkKind
kind_of (const char *path)
{
	GFile *file = g_file_new_for_path (path);
	NemoLinkKind kind = nemo_link_kind (file, NULL);

	g_object_unref (file);
	return kind;
}

static gboolean
target_of (const char *path, char **target)
{
	GFile *file = g_file_new_for_path (path);
	gboolean ok = nemo_link_read_target (file, target, NULL);

	g_object_unref (file);
	return ok;
}

static void
check_kinds (const char *dir)
{
	char *real_dir = g_build_filename (dir, "real", NULL);
	char *real_file = g_build_filename (real_dir, "f.txt", NULL);
	char *missing = g_build_filename (dir, "not-here", NULL);
	char *junction = g_build_filename (dir, "junc", NULL);
	char *dir_sym = g_build_filename (dir, "dsym", NULL);
	char *file_sym = g_build_filename (dir, "fsym", NULL);
	char *target = NULL;

	g_mkdir_with_parents (real_dir, 0700);
	check (g_file_set_contents (real_file, "hello", 5, NULL));

	check (kind_of (real_dir) == NEMO_LINK_NONE);
	check (kind_of (real_file) == NEMO_LINK_NONE);
	check (kind_of (missing) == NEMO_LINK_NONE);

	if (junctions) {
		check (nemo_link_create (real_dir, junction, NULL, NEMO_LINK_JUNCTION, NULL));
		check (kind_of (junction) == NEMO_LINK_JUNCTION);
		check (target_of (junction, &target));
		/* A junction always records an absolute path, whatever it was given. */
		check (target != NULL && g_path_is_absolute (target));
		g_free (target);
		target = NULL;
	}

	if (symlinks) {
		check (nemo_link_create (real_dir, dir_sym, NULL, NEMO_LINK_DIR_SYMLINK, NULL));
		check (kind_of (dir_sym) == NEMO_LINK_DIR_SYMLINK);

		check (nemo_link_create (real_file, file_sym, NULL, NEMO_LINK_FILE_SYMLINK, NULL));
		check (kind_of (file_sym) == NEMO_LINK_FILE_SYMLINK);
		check (target_of (file_sym, &target));
		check (g_strcmp0 (target, real_file) == 0);
		g_free (target);
	}

	g_free (real_dir);
	g_free (real_file);
	g_free (missing);
	g_free (junction);
	g_free (dir_sym);
	g_free (file_sym);
}

/* A relative link keeps its relative text, so a copy of it points where the
   original pointed rather than back at the original's folder. */
static void
check_relative_target (const char *dir)
{
	char *real_dir = g_build_filename (dir, "rel", NULL);
	char *real_file = g_build_filename (real_dir, "f.txt", NULL);
	char *link = g_build_filename (real_dir, "link", NULL);
	char *junction = g_build_filename (dir, "rel-as-junction", NULL);
	char *target = NULL;

	if (!symlinks) {
		goto out;
	}

	g_mkdir_with_parents (real_dir, 0700);
	check (g_file_set_contents (real_file, "hello", 5, NULL));

	check (nemo_link_create ("f.txt", link, NULL, NEMO_LINK_FILE_SYMLINK, NULL));
	check (target_of (link, &target));
	check (g_strcmp0 (target, "f.txt") == 0);
	g_free (target);
	target = NULL;

	/* Turning a relative link into a junction has to resolve it first - a
	   junction can only name a full path. */
	if (junctions) {
		check (nemo_link_create ("rel", junction, dir, NEMO_LINK_JUNCTION, NULL));
		check (kind_of (junction) == NEMO_LINK_JUNCTION);
		check (target_of (junction, &target));
		check (target != NULL && g_str_has_suffix (target, "rel"));
		g_free (target);
	}

 out:
	g_free (real_dir);
	g_free (real_file);
	g_free (link);
	g_free (junction);
}

static void
check_choice_defaults (void)
{
	NemoLinkChoice choice;

	/* Everything allowed: every kind stays what it was. */
	nemo_link_choice_init (&choice, NEMO_LINK_ANY);
	check (choice.file_symlink_as == NEMO_LINK_FILE_SYMLINK);
	check (choice.dir_symlink_as == NEMO_LINK_DIR_SYMLINK);
	check (choice.junction_as == NEMO_LINK_JUNCTION);
	check (nemo_link_choice_makes_links (&choice));

	/* Junctions only: a folder symlink falls back to one, which still points at
	   the same place. A file symlink has nowhere to go but a copy. */
	nemo_link_choice_init (&choice, NEMO_LINK_JUNCTION);
	check (choice.file_symlink_as == NEMO_LINK_NONE);
	check (choice.dir_symlink_as == NEMO_LINK_JUNCTION);
	check (choice.junction_as == NEMO_LINK_JUNCTION);

	/* Symlinks only, which is every platform but Windows. */
	nemo_link_choice_init (&choice, NEMO_LINK_FILE_SYMLINK | NEMO_LINK_DIR_SYMLINK);
	check (choice.file_symlink_as == NEMO_LINK_FILE_SYMLINK);
	check (choice.dir_symlink_as == NEMO_LINK_DIR_SYMLINK);
	check (choice.junction_as == NEMO_LINK_DIR_SYMLINK);

	/* A destination that keeps no links at all asks nothing and copies. */
	nemo_link_choice_init (&choice, 0);
	check (!nemo_link_choice_makes_links (&choice));
	check (nemo_link_choice_for (&choice, NEMO_LINK_JUNCTION) == NEMO_LINK_NONE);
	check (nemo_link_choice_for (&choice, NEMO_LINK_NONE) == NEMO_LINK_NONE);
}

static void
check_destination_support (const char *dir)
{
	guint kinds = nemo_link_kinds_supported (dir);
	char *real_file = g_build_filename (dir, "support-file", NULL);
	char *made = g_build_filename (dir, "support-link", NULL);

	check (g_file_set_contents (real_file, "hello", 5, NULL));

	/* Against the file system rather than against what the same call said a
	   moment ago. A destination that claims a kind it will not take offers the
	   user a choice the copy then cannot carry out. Only the file symlink is
	   checked this way: POSIX makes a symlink whatever kind is asked for, so a
	   junction request here would succeed on a platform that has none. */
	check (((kinds & NEMO_LINK_FILE_SYMLINK) != 0)
	       == (nemo_link_create (real_file, made, NULL, NEMO_LINK_FILE_SYMLINK, NULL) != FALSE));

	check (nemo_link_kinds_supported (NULL) == 0);

	g_remove (made);
	g_remove (real_file);
	g_free (made);
	g_free (real_file);
}

/* How the Make link dialog spells a relative symlink. */
static void
check_relative_spelling (const char *dir)
{
	char *from = g_build_filename (dir, "sp", "a", NULL);
	char *deep = g_build_filename (dir, "sp", "b", "c", NULL);
	char *target = g_build_filename (deep, "t.txt", NULL);
	char *beside = g_build_filename (from, "x.txt", NULL);
	char *want = g_build_filename ("..", "b", "c", "t.txt", NULL);
	char *text;

	g_mkdir_with_parents (from, 0700);
	g_mkdir_with_parents (deep, 0700);
	check (g_file_set_contents (target, "t", 1, NULL));

	text = nemo_link_relative_target (target, from);
	check (g_strcmp0 (text, want) == 0);
	g_free (text);

	text = nemo_link_relative_target (beside, from);
	check (g_strcmp0 (text, "x.txt") == 0);
	g_free (text);

	/* A link to the folder it sits in. */
	text = nemo_link_relative_target (from, from);
	check (g_strcmp0 (text, ".") == 0);
	g_free (text);

#ifdef G_OS_WIN32
	/* No way from one drive to another. */
	text = nemo_link_relative_target ("D:\\x\\t.txt", "C:\\y");
	check (text == NULL);
	g_free (text);
#else
	/* Reached through a symlinked folder, the spelling is from where the
	   link really sits, or it would point somewhere else. */
	{
		char *alias = g_build_filename (dir, "alias", NULL);
		char *made = g_build_filename (alias, "made", NULL);
		char *contents = NULL;

		check (symlink (from, alias) == 0);
		text = nemo_link_relative_target (target, alias);
		check (g_strcmp0 (text, want) == 0);
		check (text != NULL && symlink (text, made) == 0);
		check (g_file_get_contents (made, &contents, NULL, NULL) &&
		       g_strcmp0 (contents, "t") == 0);
		g_free (contents);
		g_free (text);
		g_remove (made);
		g_remove (alias);
		g_free (made);
		g_free (alias);
	}

	/* A symlinked folder below the link, going somewhere else entirely. The
	   real paths share only the scratch folder, but the way in through the
	   symlink is shorter and works. */
	{
		char *far = g_build_filename (dir, "far", "deep", NULL);
		char *far_file = g_build_filename (far, "t2.txt", NULL);
		char *far_top = g_build_filename (dir, "far", NULL);
		char *near = g_build_filename (from, "near", NULL);
		char *near_deep = g_build_filename (near, "deep", NULL);
		char *through = g_build_filename (near_deep, "t2.txt", NULL);
		char *want_near = g_build_filename ("near", "deep", "t2.txt", NULL);
		char *made = g_build_filename (from, "made2", NULL);
		char *back_link = g_build_filename (far, "back", NULL);
		char *contents = NULL;

		g_mkdir_with_parents (far, 0700);
		check (g_file_set_contents (far_file, "u", 1, NULL));
		check (symlink (far_top, near) == 0);

		text = nemo_link_relative_target (through, from);
		check (g_strcmp0 (text, want_near) == 0);
		check (text != NULL && symlink (text, made) == 0);
		check (g_file_get_contents (made, &contents, NULL, NULL) &&
		       g_strcmp0 (contents, "u") == 0);
		g_clear_pointer (&contents, g_free);
		g_free (text);

		/* Back out the same way is wrong: ".." from the real folder goes
		   under far, not back to a. So that one keeps the real path. */
		text = nemo_link_relative_target (target, near_deep);
		check (text != NULL && symlink (text, back_link) == 0);
		check (g_file_get_contents (back_link, &contents, NULL, NULL) &&
		       g_strcmp0 (contents, "t") == 0);
		g_clear_pointer (&contents, g_free);
		g_free (text);

		g_remove (back_link);
		g_remove (made);
		g_remove (near);
		g_free (far);
		g_free (far_file);
		g_free (far_top);
		g_free (near);
		g_free (near_deep);
		g_free (through);
		g_free (want_near);
		g_free (made);
		g_free (back_link);
	}
#endif

	g_free (want);
	g_free (beside);
	g_free (target);
	g_free (deep);
	g_free (from);
}

static void
check_link_options (void)
{
	NemoLinkOptions options;

	/* Every open starts the same way. */
	nemo_link_options_initial (NEMO_LINK_ANY, &options);
	check (options.folder_kind == NEMO_MAKE_JUNCTION && options.file_kind == NEMO_MAKE_SYMLINK &&
	       !options.relative && options.lnk_parts == NEMO_LNK_ALL_PARTS);

	/* No junctions here. */
	nemo_link_options_initial (NEMO_LINK_FILE_SYMLINK | NEMO_LINK_DIR_SYMLINK, &options);
	check (options.folder_kind == NEMO_MAKE_SYMLINK && options.file_kind == NEMO_MAKE_SYMLINK);

	/* Windows without the symlink privilege: folders fall back to a junction,
	   and a file to a shortcut, never a hardlink. */
	nemo_link_options_initial (NEMO_LINK_JUNCTION, &options);
	check (options.folder_kind == NEMO_MAKE_JUNCTION && options.file_kind == NEMO_MAKE_SHORTCUT);

	/* Nothing else possible: a shortcut can always be made. */
	nemo_link_options_initial (0, &options);
	check (options.folder_kind == NEMO_MAKE_SHORTCUT && options.file_kind == NEMO_MAKE_SHORTCUT);

	/* A shortcut always carries every path it can. */
	check (options.lnk_parts == NEMO_LNK_ALL_PARTS);

	/* The path choice matters only while something comes out a symlink. A
	   junction is always absolute, a hardlink has no path, and a shortcut
	   carries every kind. */
	options.folder_kind = NEMO_MAKE_JUNCTION;
	options.file_kind = NEMO_MAKE_HARDLINK;
	check (!nemo_link_options_uses_path (&options, 2, 3));
	options.file_kind = NEMO_MAKE_SYMLINK;
	check (nemo_link_options_uses_path (&options, 2, 3));
	check (!nemo_link_options_uses_path (&options, 2, 0));
	options.folder_kind = NEMO_MAKE_SYMLINK;
	check (nemo_link_options_uses_path (&options, 1, 0));
	options.folder_kind = NEMO_MAKE_SHORTCUT;
	options.file_kind = NEMO_MAKE_SHORTCUT;
	check (!nemo_link_options_uses_path (&options, 1, 1));
	options.folder_kind = NEMO_MAKE_JUNCTION;
	options.file_kind = NEMO_MAKE_HARDLINK;
	check (!nemo_link_options_uses_path (&options, 1, 1));
}

static void
check_hardlink (const char *dir)
{
	char *first = g_build_filename (dir, "hard-1", NULL);
	char *second = g_build_filename (dir, "hard-2", NULL);
	char *missing = g_build_filename (dir, "hard-missing", NULL);
	char *third = g_build_filename (dir, "hard-3", NULL);
	char *contents = NULL;
	GError *error = NULL;
	FILE *fp;

	check (g_file_set_contents (first, "one", -1, NULL));
	check (nemo_link_create_hard (first, second, &error));
	g_clear_error (&error);

	/* Written in place through one name, seen through the other. */
	fp = g_fopen (second, "ab");
	check (fp != NULL);
	if (fp != NULL) {
		fputs ("two", fp);
		fclose (fp);
	}
	check (g_file_get_contents (first, &contents, NULL, NULL) &&
	       g_strcmp0 (contents, "onetwo") == 0);
	g_free (contents);

	/* A taken name comes back as EXISTS, which the job retries under
	   another name. */
	check (!nemo_link_create_hard (first, second, &error));
	check (g_error_matches (error, G_IO_ERROR, G_IO_ERROR_EXISTS));
	g_clear_error (&error);

	check (!nemo_link_create_hard (missing, third, &error));
	check (error != NULL);
	g_clear_error (&error);

	g_remove (second);
	g_remove (first);
	g_free (third);
	g_free (missing);
	g_free (second);
	g_free (first);
}

/* A hardlink of a selected symlink is a second name for the file it leads
   to. The symlink's own relative path would lead nowhere from elsewhere. */
static void
check_hardlink_of_symlink (const char *dir)
{
	char *file = g_build_filename (dir, "hs-file.txt", NULL);
	char *near = g_build_filename (dir, "hs-a", NULL);
	char *far = g_build_filename (dir, "hs-b", "c", NULL);
	char *link = g_build_filename (near, "rel", NULL);
	char *spelled = g_build_filename ("..", "hs-file.txt", NULL);
	char *made = g_build_filename (far, "rel", NULL);
	char *contents = NULL;
	GError *error = NULL;
	FILE *fp;

	g_mkdir_with_parents (near, 0700);
	g_mkdir_with_parents (far, 0700);
	check (g_file_set_contents (file, "one", -1, NULL));
	check (nemo_link_create (spelled, link, near, NEMO_LINK_FILE_SYMLINK, NULL));

	check (nemo_link_create_hard (link, made, &error));
	g_clear_error (&error);
	check (kind_of (made) == NEMO_LINK_NONE);
	fp = g_fopen (made, "ab");
	check (fp != NULL);
	if (fp != NULL) {
		fputs ("two", fp);
		fclose (fp);
	}
	check (g_file_get_contents (file, &contents, NULL, NULL) &&
	       g_strcmp0 (contents, "onetwo") == 0);
	g_free (contents);

	g_remove (made);
	g_remove (link);
	g_remove (file);
	g_free (file);
	g_free (near);
	g_free (far);
	g_free (link);
	g_free (spelled);
	g_free (made);
}

#ifdef G_OS_WIN32
/* Windows cannot follow ".." above a share, so a link from one share to
   another of the same server has only the full path. */
static void
check_relative_across_shares (void)
{
	char *text;

	text = nemo_link_relative_target ("\\\\srv\\b\\y.txt", "\\\\srv\\a\\x");
	check (text == NULL);
	g_free (text);

	text = nemo_link_relative_target ("\\\\srv\\b\\y.txt", "\\\\srv\\a");
	check (text == NULL);
	g_free (text);

	text = nemo_link_relative_target ("\\\\srv\\a\\y.txt", "\\\\other\\a\\x");
	check (text == NULL);
	g_free (text);

	text = nemo_link_relative_target ("\\\\srv\\a\\y.txt", "\\\\srv\\a\\x");
	check (g_strcmp0 (text, "..\\y.txt") == 0);
	g_free (text);

	text = nemo_link_relative_target ("\\\\?\\UNC\\srv\\b\\y.txt", "\\\\?\\UNC\\srv\\a\\x");
	check (text == NULL);
	g_free (text);

	text = nemo_link_relative_target ("C:\\y.txt", "C:\\a");
	check (g_strcmp0 (text, "..\\y.txt") == 0);
	g_free (text);
}
#endif

/* Every row and every choice, for one link and for several: the wording
   follows the count, and no two choices in a row read the same. */
static void
check_labels (void)
{
	static const NemoLinkKind kinds[] = {
		NEMO_LINK_FILE_SYMLINK, NEMO_LINK_DIR_SYMLINK, NEMO_LINK_JUNCTION
	};
	static const NemoLinkKind offers[] = {
		NEMO_LINK_NONE, NEMO_LINK_FILE_SYMLINK, NEMO_LINK_DIR_SYMLINK, NEMO_LINK_JUNCTION
	};
	guint count;
	int k, o, move;

	for (k = 0; k < (int) G_N_ELEMENTS (kinds); k++) {
		const char *one = nemo_link_choice_row_label (kinds[k], 1);
		const char *many = nemo_link_choice_row_label (kinds[k], 2);

		check (g_str_has_suffix (many, "s:"));
		check (!g_str_has_suffix (one, "s:"));
		check (strncmp (one, many, strlen (one) - 1) == 0);
	}

	for (move = 0; move <= 1; move++) {
		for (k = 0; k < (int) G_N_ELEMENTS (kinds); k++) {
			for (o = 0; o < (int) G_N_ELEMENTS (offers); o++) {
				NemoLinkKind offer = offers[o];
				const char *one, *many;

				/* A folder link never becomes a file symlink, nor the
				   reverse. */
				if ((kinds[k] == NEMO_LINK_FILE_SYMLINK) !=
				    (offer == NEMO_LINK_FILE_SYMLINK) && offer != NEMO_LINK_NONE) {
					continue;
				}

				one = nemo_link_choice_label (kinds[k], offer, 1, move);
				many = nemo_link_choice_label (kinds[k], offer, 3, move);

				check (g_str_has_prefix (one, move && offer != NEMO_LINK_NONE ? "Move" : "Copy"));
				if (offer == NEMO_LINK_NONE && kinds[k] != NEMO_LINK_FILE_SYMLINK) {
					/* A folder has contents however many. */
					check (g_strcmp0 (one, "Copy contents") == 0);
					check (g_strcmp0 (many, "Copy contents") == 0);
				} else if (offer == NEMO_LINK_NONE) {
					check (g_strcmp0 (one, "Copy content") == 0);
					check (g_strcmp0 (many, "Copy contents") == 0);
				} else if (offer == kinds[k]) {
					check (strstr (one, " link as-is") != NULL);
					check (strstr (many, " links as-is") != NULL);
				} else {
					check (strstr (one, " as a ") != NULL);
					check (strstr (many, " as a ") == NULL);
					check (g_str_has_suffix (many, "s"));
				}
			}
		}
	}

	/* The tooltip only where a folder's contents are copied. */
	for (count = 0; count < G_N_ELEMENTS (offers); count++) {
		check (nemo_link_choice_tooltip (NEMO_LINK_FILE_SYMLINK, offers[count]) == NULL);
		check ((nemo_link_choice_tooltip (NEMO_LINK_DIR_SYMLINK, offers[count]) != NULL) ==
		       (offers[count] == NEMO_LINK_NONE));
		check ((nemo_link_choice_tooltip (NEMO_LINK_JUNCTION, offers[count]) != NULL) ==
		       (offers[count] == NEMO_LINK_NONE));
	}

	{
		NemoLinkCounts counts = { 0, 2, 0 };

		check (nemo_link_counts_kinds (&counts) == NEMO_LINK_DIR_SYMLINK);
		counts.file_symlinks = 1;
		counts.junctions = 5;
		check (nemo_link_counts_kinds (&counts) == NEMO_LINK_ANY);
	}
}

int
main (int argc, char **argv)
{
	char *dir;
	guint supported;

	g_test_init (&argc, &argv, NULL);

	dir = test_scratch_dir ("nemo-link-copy-XXXXXX", NULL);
	g_assert (dir != NULL);

	supported = nemo_link_kinds_supported (dir);
	symlinks = (supported & NEMO_LINK_FILE_SYMLINK) != 0;
	junctions = (supported & NEMO_LINK_JUNCTION) != 0;
	if (!symlinks) {
		g_printerr ("note: symlinks are not permitted here, so those checks are skipped\n");
	}

	check_kinds (dir);
	check_relative_target (dir);
	check_choice_defaults ();
	check_labels ();
	check_link_options ();
	check_hardlink (dir);
	if (symlinks) {
		check_hardlink_of_symlink (dir);
		check_relative_spelling (dir);
	}
#ifdef G_OS_WIN32
	check_relative_across_shares ();
#endif
	check_destination_support (dir);

	g_free (dir);

	g_printerr ("%d failure(s)\n", failures);
	return failures == 0 ? 0 : 1;
}
