/* Compressing for real: a folder of items is handed to the job both ways -
 * everything into one archive, and one archive per item - and what reaches
 * disk is read back with libarchive. What goes in each archive is the point,
 * so the entry names are checked rather than just the file being there.
 *
 * Also a run cancelled partway, and one that fails on a source it cannot read:
 * neither may leave a half-written archive, or a volume of one, behind. */

#include "test.h"

#include <libnemo-private/nemo-archive.h>
#include <libnemo-private/nemo-archive-commands.h>
#include <libnemo-private/nemo-config.h>
#include <libnemo-private/nemo-dir-enum.h>
#include <libnemo-private/nemo-link-copy.h>
#include <libnemo-private/nemo-progress-info.h>
#include <libnemo-private/nemo-progress-info-manager.h>

#include <archive.h>
#include <archive_entry.h>
#include <glib/gstdio.h>
#include <stdlib.h>
#include <string.h>
#ifndef G_OS_WIN32
#include <unistd.h>
#endif

#include "test-scratch.h"
#include "test-check.h"
#include "nemo-progress-info-widget.h"

#define ARCHIVE_TIMEOUT_SECONDS 30

static gboolean job_finished;
static gboolean job_succeeded;

static void
archive_done (G_GNUC_UNUSED GFile    *result,
	      gboolean  success,
	      G_GNUC_UNUSED gpointer  data)
{
	job_succeeded = success;
	job_finished = TRUE;
	gtk_main_quit ();
}

static gboolean
give_up (G_GNUC_UNUSED gpointer data)
{
	g_printerr ("FAIL: compressing did not finish within %d seconds\n",
		    ARCHIVE_TIMEOUT_SECONDS);
	failures++;
	gtk_main_quit ();

	return G_SOURCE_REMOVE;
}

static void
wait_for_end (void)
{
	guint timeout_id = g_timeout_add_seconds (ARCHIVE_TIMEOUT_SECONDS, give_up, NULL);

	gtk_main ();
	g_source_remove (timeout_id);

	check (job_finished);
}

static void
wait_for_job (void)
{
	wait_for_end ();
	check (job_succeeded);
}

static void
write_file (const char *dir,
	    const char *name,
	    const char *contents)
{
	char *path = g_build_filename (dir, name, NULL);

	check (g_file_set_contents (path, contents, -1, NULL));

	g_free (path);
}

/* Every entry in the archive, '/'-separated, in the order libarchive hands
   them over. NULL when the file could not be opened as an archive at all. */
static char **
archive_entries (const char *path)
{
	struct archive *a = archive_read_new ();
	struct archive_entry *entry;
	GPtrArray *names;

	archive_read_support_format_all (a);
	archive_read_support_filter_all (a);

	if (archive_read_open_filename (a, path, 16384) != ARCHIVE_OK) {
		archive_read_free (a);
		return NULL;
	}

	names = g_ptr_array_new ();

	while (archive_read_next_header (a, &entry) == ARCHIVE_OK) {
		g_ptr_array_add (names, g_strdup (archive_entry_pathname (entry)));
	}

	g_ptr_array_add (names, NULL);
	archive_read_free (a);

	return (char **) g_ptr_array_free (names, FALSE);
}

static gboolean
holds_entry (char       **entries,
	     const char  *wanted)
{
	int i;

	for (i = 0; entries != NULL && entries[i] != NULL; i++) {
		if (g_strcmp0 (entries[i], wanted) == 0) {
			return TRUE;
		}
	}

	return FALSE;
}

static GList *
sources_in (const char *dir,
	    const char * const *names)
{
	GList *sources = NULL;
	int i;

	for (i = 0; names[i] != NULL; i++) {
		char *path = g_build_filename (dir, names[i], NULL);

		sources = g_list_append (sources, g_file_new_for_path (path));
		g_free (path);
	}

	return sources;
}

/* One archive holding the whole selection, which is what the dialog does with
   the box unticked. */
static void
check_one_archive (const char         *source_dir,
		   const char         *out_dir,
		   const char * const *names,
		   GtkWidget          *window)
{
	GList *sources = sources_in (source_dir, names);
	char *path = g_build_filename (out_dir, "everything.zip", NULL);
	GFile *destination = g_file_new_for_path (path);
	NemoArchiveOptions options;
	char **entries;

	nemo_archive_options_init (&options);
	options.format = NEMO_ARCHIVE_FORMAT_ZIP;

	job_finished = FALSE;
	job_succeeded = FALSE;
	nemo_archive_create (sources, destination, &options, GTK_WINDOW (window),
			     archive_done, NULL);
	wait_for_job ();

	entries = archive_entries (path);
	check (entries != NULL);
	check (holds_entry (entries, "one.txt"));
	check (holds_entry (entries, "two.txt"));
	check (holds_entry (entries, "sub/inner.txt"));
	g_strfreev (entries);

	nemo_archive_options_clear (&options);
	g_list_free_full (sources, g_object_unref);
	g_object_unref (destination);
	g_free (path);
}

/* The same selection with the box ticked: an archive apiece, each named after
   its item and each holding only that item. */
static void
check_each_archive (const char         *source_dir,
		    const char         *out_dir,
		    const char * const *names,
		    GtkWidget          *window)
{
	GList *sources = sources_in (source_dir, names);
	GFile *out = g_file_new_for_path (out_dir);
	NemoArchiveOptions options;
	char *path;
	char **entries;

	nemo_archive_options_init (&options);
	options.format = NEMO_ARCHIVE_FORMAT_ZIP;

	job_finished = FALSE;
	job_succeeded = FALSE;
	nemo_archive_create_each (sources, out, &options, GTK_WINDOW (window),
				  archive_done, NULL);
	wait_for_job ();

	/* The item's whole name is kept, so a file keeps its extension. */
	path = g_build_filename (out_dir, "one.txt.zip", NULL);
	entries = archive_entries (path);
	check (entries != NULL);
	check (holds_entry (entries, "one.txt"));
	check (!holds_entry (entries, "two.txt"));
	g_strfreev (entries);
	g_free (path);

	path = g_build_filename (out_dir, "two.txt.zip", NULL);
	entries = archive_entries (path);
	check (entries != NULL);
	check (holds_entry (entries, "two.txt"));
	check (!holds_entry (entries, "one.txt"));
	g_strfreev (entries);
	g_free (path);

	/* A folder brings what is inside it, and nothing from beside it. */
	path = g_build_filename (out_dir, "sub.zip", NULL);
	entries = archive_entries (path);
	check (entries != NULL);
	check (holds_entry (entries, "sub/inner.txt"));
	check (!holds_entry (entries, "one.txt"));
	g_strfreev (entries);
	g_free (path);

	/* Nothing was written under the name the whole selection would have
	   used, so the two ways cannot be confused for each other. */
	path = g_build_filename (out_dir, "everything.zip", NULL);
	check (!g_file_test (path, G_FILE_TEST_EXISTS));
	g_free (path);

	nemo_archive_options_clear (&options);
	g_list_free_full (sources, g_object_unref);
	g_object_unref (out);
}

/* An archive in the selection keeps its suffix rather than swapping it, or the
   archive being written would overwrite the file being read. */
static void
check_archive_of_archive (const char *out_dir,
			  GtkWidget  *window)
{
	static const char * const names[] = { "one.txt.zip", NULL };
	GList *sources = sources_in (out_dir, names);
	GFile *out = g_file_new_for_path (out_dir);
	NemoArchiveOptions options;
	char *path;
	char **entries;

	nemo_archive_options_init (&options);
	options.format = NEMO_ARCHIVE_FORMAT_ZIP;

	job_finished = FALSE;
	job_succeeded = FALSE;
	nemo_archive_create_each (sources, out, &options, GTK_WINDOW (window),
				  archive_done, NULL);
	wait_for_job ();

	path = g_build_filename (out_dir, "one.txt.zip.zip", NULL);
	entries = archive_entries (path);
	check (entries != NULL);
	check (holds_entry (entries, "one.txt.zip"));
	g_strfreev (entries);
	g_free (path);

	/* The file it was made from is still what it was. */
	path = g_build_filename (out_dir, "one.txt.zip", NULL);
	entries = archive_entries (path);
	check (entries != NULL);
	check (holds_entry (entries, "one.txt"));
	g_strfreev (entries);
	g_free (path);

	nemo_archive_options_clear (&options);
	g_list_free_full (sources, g_object_unref);
	g_object_unref (out);
}

/* Whether the originals may go is decided by reading the archive back, so that
   decision is checked from both sides: an archive that really does hold the
   lot, and each way one can come up short. Between them the happy path is
   checked again, or a later refusal could just be damage left behind. */
/* What the Compress dialog asks before it offers to delete the originals. */
static void
check_predicate (void)
{
	NemoArchiveOptions options;

	nemo_archive_options_init (&options);
	check (nemo_archive_can_verify (&options));

	options.split_size = 1024;
	check (!nemo_archive_can_verify (&options));
	options.split_size = 0;

	options.encrypt_names = TRUE;
	check (nemo_archive_can_verify (&options));

	options.password = g_strdup ("secret");
	check (!nemo_archive_can_verify (&options));

	options.encrypt_names = FALSE;
	check (nemo_archive_can_verify (&options));

	/* A 7z with a password that keeps links: its writer keeps a link's
	   target where the password locks it. Where 7-Zip keeps no links the
	   choice is Ignore. */
	options.format = NEMO_ARCHIVE_FORMAT_7Z;
	options.links.symlinks = ARC_LINK_STORE_SYMLINK;
	check (nemo_archive_can_verify (&options) ==
	       !(nemo_archive_options_link_stores (options.format, &options) & ARC_STORES_SYMLINKS));
	options.links.symlinks = ARC_LINK_FOLLOW;
	check (nemo_archive_can_verify (&options));
	options.links.symlinks = ARC_LINK_STORE_SYMLINK;
	g_clear_pointer (&options.password, g_free);
	check (nemo_archive_can_verify (&options));
	options.format = NEMO_ARCHIVE_FORMAT_ZIP;
	options.password = g_strdup ("secret");
	check (nemo_archive_can_verify (&options));

	nemo_archive_options_clear (&options);
}

/* The same archive rewritten with its folder entries left out, or with the
   one named left out. A writer is free to store only the files, and the
   verify has to read a folder as there when something inside it is. */
static gboolean
rewrite_without (const char *src,
		 const char *dst,
		 const char *name)
{
	struct archive *in  = archive_read_new ();
	struct archive *out = archive_write_new ();
	struct archive_entry *entry;
	gboolean ok = TRUE;

	archive_read_support_format_all (in);
	archive_read_support_filter_all (in);
	archive_write_set_format_zip (out);

	if (archive_read_open_filename (in, src, 16384) != ARCHIVE_OK ||
	    archive_write_open_filename (out, dst) != ARCHIVE_OK) {
		ok = FALSE;
	}

	while (ok && archive_read_next_header (in, &entry) == ARCHIVE_OK) {
		char buffer[8192];
		la_ssize_t got;

		if (name == NULL ? archive_entry_filetype (entry) == AE_IFDIR
		    : g_strcmp0 (archive_entry_pathname (entry), name) == 0) {
			continue;
		}

		if (archive_write_header (out, entry) != ARCHIVE_OK) {
			ok = FALSE;
			break;
		}

		while ((got = archive_read_data (in, buffer, sizeof buffer)) > 0) {
			if (archive_write_data (out, buffer, (size_t) got) < 0) {
				ok = FALSE;
				break;
			}
		}
	}

	archive_write_close (out);
	archive_write_free (out);
	archive_read_free (in);

	return ok;
}

static void
check_verify (const char *tmp,
	      GtkWidget  *window)
{
	static const char * const names[] = { "a.txt", "deep", "empty", NULL };
	char *dir = g_build_filename (tmp, "verify", NULL);
	char *deep = g_build_filename (dir, "deep", NULL);
	char *empty = g_build_filename (dir, "empty", NULL);
	char *out = g_build_filename (tmp, "verify-out", NULL);
	char *path = g_build_filename (out, "all.zip", NULL);
	char *extra = g_build_filename (deep, "c.txt", NULL);
	GFile *archive;
	GFile *plain;
	GList *sources;
	NemoArchiveOptions options;
	char *reason = NULL;

	g_mkdir_with_parents (deep, 0700);
	g_mkdir_with_parents (empty, 0700);
	g_mkdir_with_parents (out, 0700);
	write_file (dir, "a.txt", "alpha");
	write_file (deep, "b.txt", "bravo");

	sources = sources_in (dir, names);
	archive = g_file_new_for_path (path);

	nemo_archive_options_init (&options);
	options.format = NEMO_ARCHIVE_FORMAT_ZIP;

	job_finished = FALSE;
	job_succeeded = FALSE;
	nemo_archive_create (sources, archive, &options, GTK_WINDOW (window),
			     archive_done, NULL);
	wait_for_job ();

	/* Everything went in, including the folder with nothing in it. */
	check (nemo_archive_verify (archive, sources, &options,
				    NEMO_ARCHIVE_BACKEND_LIBARCHIVE, NULL, &reason));
	check (reason == NULL);

	/* With the folder entries dropped, "deep" is still in there because
	   b.txt is, and "empty" cannot be. */
	{
		static const char * const kept[] = { "a.txt", "deep", NULL };
		char  *flat_path = g_build_filename (out, "flat.zip", NULL);
		GFile *flat = g_file_new_for_path (flat_path);
		GList *flat_sources = sources_in (dir, kept);

		check (rewrite_without (path, flat_path, NULL));

		check (nemo_archive_verify (flat, flat_sources, &options,
					    NEMO_ARCHIVE_BACKEND_LIBARCHIVE, NULL, &reason));
		check (reason == NULL);

		check (!nemo_archive_verify (flat, sources, &options,
					     NEMO_ARCHIVE_BACKEND_LIBARCHIVE, NULL, &reason));
		check (reason != NULL);
		g_free (reason);
		reason = NULL;

		g_list_free_full (flat_sources, g_object_unref);
		g_object_unref (flat);
		g_free (flat_path);
	}

	/* A source that changed after the archive was written. */
	write_file (dir, "a.txt", "alpha and then some");
	check (!nemo_archive_verify (archive, sources, &options,
				     NEMO_ARCHIVE_BACKEND_LIBARCHIVE, NULL, &reason));
	check (reason != NULL);
	g_free (reason);
	reason = NULL;

	write_file (dir, "a.txt", "alpha");
	check (nemo_archive_verify (archive, sources, &options,
				    NEMO_ARCHIVE_BACKEND_LIBARCHIVE, NULL, NULL));

	/* A file that appeared inside one of the sources after it was written.
	   Beside them rather than inside would prove nothing: only what was
	   selected is checked. */
	write_file (deep, "c.txt", "charlie");
	check (!nemo_archive_verify (archive, sources, &options,
				     NEMO_ARCHIVE_BACKEND_LIBARCHIVE, NULL, &reason));
	check (reason != NULL);
	g_free (reason);
	reason = NULL;

	g_remove (extra);
	check (nemo_archive_verify (archive, sources, &options,
				    NEMO_ARCHIVE_BACKEND_LIBARCHIVE, NULL, NULL));

	/* One volume of a set is not an archive on its own, so there is nothing
	   to check and the answer is no whatever is on disk. */
	options.split_size = 1024;
	check (!nemo_archive_verify (archive, sources, &options,
				     NEMO_ARCHIVE_BACKEND_LIBARCHIVE, NULL, &reason));
	check (reason != NULL);
	g_free (reason);
	reason = NULL;
	options.split_size = 0;

	/* With the names encrypted the reader will not open the archive at all,
	   password or no password, so the answer is no before anything is read.
	   The archive here is the plain one that has just checked out, so a no
	   can only have come from the options. */
	options.encrypt_names = TRUE;
	options.password = g_strdup ("secret");
	check (!nemo_archive_verify (archive, sources, &options,
				     NEMO_ARCHIVE_BACKEND_LIBARCHIVE, NULL, &reason));
	check (reason != NULL);
	g_free (reason);
	reason = NULL;

	/* The names box with no password behind it encrypts nothing. */
	g_clear_pointer (&options.password, g_free);
	check (nemo_archive_verify (archive, sources, &options,
				    NEMO_ARCHIVE_BACKEND_LIBARCHIVE, NULL, NULL));
	options.encrypt_names = FALSE;

	/* The same four answers straight from the predicate. The Compress dialog
	   grays its delete box on this, so the rule is checked once here rather
	   than through a window. */
	check_predicate ();

	/* Something that will not open as an archive at all. */
	write_file (out, "notes.txt", "this is not an archive");
	g_free (path);
	path = g_build_filename (out, "notes.txt", NULL);
	plain = g_file_new_for_path (path);
	check (!nemo_archive_verify (plain, sources, &options,
				     NEMO_ARCHIVE_BACKEND_LIBARCHIVE, NULL, &reason));
	check (reason != NULL);
	g_free (reason);
	reason = NULL;
	g_object_unref (plain);

#ifndef G_OS_WIN32
	/* Anything the walk had to pass over means the archive was never offered
	   all of it, whether or not what did go in reads back cleanly. A link
	   that leads nowhere is one of those under Ignore, and where the writer
	   cannot keep it as a link. */
	{
		char *link_path = g_build_filename (deep, "dangling", NULL);
		GFile *link_file = g_file_new_for_path (link_path);
		char *link_archive = g_build_filename (out, "links.zip", NULL);
		GFile *link_destination = g_file_new_for_path (link_archive);

		check (g_file_make_symbolic_link (link_file, "nowhere", NULL, NULL));
		options.links.symlinks = ARC_LINK_IGNORE;

		job_finished = FALSE;
		job_succeeded = FALSE;
		nemo_archive_create (sources, link_destination, &options, GTK_WINDOW (window),
				     archive_done, NULL);
		wait_for_job ();

		/* Out since 20260929, when a link that leads nowhere started going
		   in as a link, so the walk is whole and the check passes.
		check (!nemo_archive_verify (link_destination, sources, &options,
					     NEMO_ARCHIVE_BACKEND_LIBARCHIVE, NULL, &reason));
		check (reason != NULL);
		*/
		/* Out since 2026100516274423: that rule is Follow's and Store's
		   now, and Ignore leaves the link out, so the check fails again.
		check (nemo_archive_verify (link_destination, sources, &options,
					    NEMO_ARCHIVE_BACKEND_LIBARCHIVE, NULL, &reason));
		check (reason == NULL);
		*/
		check (!nemo_archive_verify (link_destination, sources, &options,
					     NEMO_ARCHIVE_BACKEND_LIBARCHIVE, NULL, &reason));
		check (reason != NULL);
		g_free (reason);
		reason = NULL;

		/* Still a miss for a writer that could not take it as a link, as
		   7z on Windows cannot. rar writes no zip, so it keeps no links
		   here and stands in for that. */
		check (!nemo_archive_verify (link_destination, sources, &options,
					     NEMO_ARCHIVE_BACKEND_RAR, NULL, &reason));
		check (reason != NULL);
		g_free (reason);
		reason = NULL;

		/* Follow and Store put it in as a link, so the walk is whole. */
		{
			static const ArcLinkChoice keeping[] = { ARC_LINK_FOLLOW, ARC_LINK_STORE_SYMLINK };
			gsize k;

			for (k = 0; k < G_N_ELEMENTS (keeping); k++) {
				options.links.symlinks = keeping[k];
				job_finished = FALSE;
				job_succeeded = FALSE;
				nemo_archive_create (sources, link_destination, &options, GTK_WINDOW (window),
						     archive_done, NULL);
				wait_for_job ();
				check (nemo_archive_verify (link_destination, sources, &options,
							    NEMO_ARCHIVE_BACKEND_LIBARCHIVE, NULL, &reason));
				check (reason == NULL);
				g_free (reason);
				reason = NULL;
			}
		}

		g_remove (link_path);
		options.links.symlinks = ARC_LINK_IGNORE;

		g_object_unref (link_destination);
		g_object_unref (link_file);
		g_free (link_archive);
		g_free (link_path);
	}
#endif

	nemo_archive_options_clear (&options);
	g_list_free_full (sources, g_object_unref);
	g_object_unref (archive);
	g_free (extra);
	g_free (path);
	g_free (out);
	g_free (empty);
	g_free (deep);
	g_free (dir);
}

/* Where a followed link's folder is, made up as another filesystem. */
static char *other_fs;

/* Returns: (transfer full): free with arc_mount_table_free */
static ArcMountTable *
other_fs_mounts (void)
{
#ifdef G_OS_WIN32
	ArcMountEntry entries[] = { { other_fs, "\\\\?\\Volume{bbbb}\\", NULL } };
#else
	ArcMountEntry entries[] = { { other_fs, "/dev/sdz9", "ext4" } };
#endif

	return arc_mount_table_new (entries, G_N_ELEMENTS (entries),
#ifdef G_OS_WIN32
				    TRUE
#else
				    FALSE
#endif
				    );
}

static void
compress_to (GList                    *sources,
	     GFile                    *destination,
	     const NemoArchiveOptions *options,
	     GtkWidget                *window)
{
	job_finished = FALSE;
	job_succeeded = FALSE;
	nemo_archive_create (sources, destination, options, GTK_WINDOW (window),
			     archive_done, NULL);
	wait_for_job ();
}

static void
check_refused (GFile                    *archive,
	       GList                    *sources,
	       const NemoArchiveOptions *options,
	       const char               *what)
{
	char *reason = NULL;

	if (nemo_archive_verify (archive, sources, options, NEMO_ARCHIVE_BACKEND_LIBARCHIVE,
				 NULL, &reason)) {
		g_printerr ("FAIL: the check passed with %s\n", what);
		failures++;
	}
	check (reason != NULL);
	g_free (reason);
}

/* With links followed, the delete check holds the archive to what they led
   to as well, under the links' own names: a followed file that changed, one
   that's missing from the archive, and one added since all refuse. A link
   back into the selection adds nothing, and is no miss. Ignore refuses the
   same archive, since the links were never in. A followed link onto another
   filesystem with that option off was left out, so it refuses too, without
   reading what's there; with it on, it passes. */
static void
check_verify_followed (const char *tmp,
		       GtkWidget  *window)
{
	char *dir = g_build_filename (tmp, "verify-follow", NULL);
	char *sel = g_build_filename (dir, "sel", NULL);
	char *outside = g_build_filename (dir, "outside", NULL);
	char *out_link = g_build_filename (sel, "out", NULL);
	char *file_link = g_build_filename (sel, "f", NULL);
	char *target = g_build_filename (outside, "f.txt", NULL);
	char *path = g_build_filename (dir, "sel.zip", NULL);
	char *dropped_path = g_build_filename (dir, "dropped.zip", NULL);
	GFile *archive = g_file_new_for_path (path);
	GFile *dropped = g_file_new_for_path (dropped_path);
	GList *sources;
	NemoArchiveOptions options;
	char **entries;
	char *reason = NULL;

	g_mkdir_with_parents (sel, 0700);
	g_mkdir_with_parents (outside, 0700);
	if ((nemo_link_kinds_supported (dir) & NEMO_LINK_DIR_SYMLINK) == 0 ||
	    (nemo_link_kinds_supported (dir) & NEMO_LINK_FILE_SYMLINK) == 0) {
		g_printerr ("note: no symlinks here, followed content not checked\n");
		goto out;
	}
	write_file (sel, "a.txt", "alpha");
	write_file (outside, "f.txt", "foxtrot");
	write_file (outside, "g.txt", "golf");
	check (nemo_link_create (".." G_DIR_SEPARATOR_S "outside", out_link, NULL, NEMO_LINK_DIR_SYMLINK, NULL));
	check (nemo_link_create (".." G_DIR_SEPARATOR_S "outside" G_DIR_SEPARATOR_S "f.txt", file_link, NULL,
				 NEMO_LINK_FILE_SYMLINK, NULL));
	{
		char *again = g_build_filename (sel, "again", NULL);

		check (nemo_link_create (".." G_DIR_SEPARATOR_S "sel", again, NULL, NEMO_LINK_DIR_SYMLINK, NULL));
		g_free (again);
	}

	sources = g_list_append (NULL, g_file_new_for_path (sel));
	nemo_archive_options_init (&options);
	options.format = NEMO_ARCHIVE_FORMAT_ZIP;
	options.links.symlinks = ARC_LINK_FOLLOW;
	compress_to (sources, archive, &options, window);

	entries = archive_entries (path);
	check (entries != NULL && holds_entry (entries, "sel/out/f.txt") && holds_entry (entries, "sel/out/g.txt") &&
	       holds_entry (entries, "sel/f") && holds_entry (entries, "sel/a.txt"));
	check (entries != NULL && !holds_entry (entries, "sel/again/a.txt"));
	g_strfreev (entries);
	check (nemo_archive_verify (archive, sources, &options, NEMO_ARCHIVE_BACKEND_LIBARCHIVE,
				    NULL, &reason));
	check (reason == NULL);

	/* Changed after it went in, through either link. */
	write_file (outside, "f.txt", "foxtrot, longer");
	check_refused (archive, sources, &options, "a followed file changed");
	write_file (outside, "f.txt", "foxtrot");

	/* Added under the followed folder. */
	write_file (outside, "h.txt", "hotel");
	check_refused (archive, sources, &options, "a file added under a followed folder");
	{
		char *added = g_build_filename (outside, "h.txt", NULL);

		g_remove (added);
		g_free (added);
	}
	check (nemo_archive_verify (archive, sources, &options, NEMO_ARCHIVE_BACKEND_LIBARCHIVE,
				    NULL, NULL));

	/* Missing from the archive. */
	check (rewrite_without (path, dropped_path, "sel/out/g.txt"));
	check_refused (dropped, sources, &options, "a followed file missing from the archive");

	/* The same archive, as if the links had been ignored. */
	options.links.symlinks = ARC_LINK_IGNORE;
	check_refused (archive, sources, &options, "links ignored");
	options.links.symlinks = ARC_LINK_FOLLOW;

	/* The followed folder on another filesystem. */
#ifndef G_OS_WIN32
	{
		char *real = realpath (outside, NULL);

		other_fs = g_strdup (real);
		free (real);
	}
#else
	other_fs = g_strdup (outside);
#endif
	nemo_archive_set_mounts_for_test (other_fs_mounts);
	options.links.follow_other = FALSE;
	compress_to (sources, archive, &options, window);
	entries = archive_entries (path);
	check (entries != NULL && !holds_entry (entries, "sel/out/f.txt") && !holds_entry (entries, "sel/f"));
	g_strfreev (entries);
	check_refused (archive, sources, &options, "a followed folder on a filesystem not followed");

	options.links.follow_other = TRUE;
	compress_to (sources, archive, &options, window);
	check (nemo_archive_verify (archive, sources, &options, NEMO_ARCHIVE_BACKEND_LIBARCHIVE,
				    NULL, NULL));
	nemo_archive_set_mounts_for_test (NULL);
	g_clear_pointer (&other_fs, g_free);

	nemo_archive_options_clear (&options);
	g_list_free_full (sources, g_object_unref);
 out:
	g_object_unref (dropped);
	g_object_unref (archive);
	g_free (dropped_path);
	g_free (path);
	g_free (target);
	g_free (file_link);
	g_free (out_link);
	g_free (outside);
	g_free (sel);
	g_free (dir);
}

static guint
count_entries (const char *path)
{
	GFile *dir = g_file_new_for_path (path);
	GFileEnumerator *children;
	GFileInfo *info;
	guint n = 0;

	children = nemo_enumerate_children (dir, G_FILE_ATTRIBUTE_STANDARD_NAME,
					    G_FILE_QUERY_INFO_NOFOLLOW_SYMLINKS, NULL, NULL);
	if (children != NULL) {
		while ((info = g_file_enumerator_next_file (children, NULL, NULL)) != NULL) {
			g_printerr ("left behind: %s\n", g_file_info_get_name (info));
			n++;
			g_object_unref (info);
		}
		g_object_unref (children);
	}
	g_object_unref (dir);

	return n;
}

/* An error the job reports is a dialog left open, which nobody here reads. */
static void
close_dialogs (void)
{
	GList *windows, *l;

	windows = gtk_window_list_toplevels ();
	for (l = windows; l != NULL; l = l->next) {
		if (GTK_IS_DIALOG (l->data)) {
			gtk_widget_destroy (l->data);
		}
	}
	g_list_free (windows);
}

typedef struct {
	NemoProgressInfoManager *manager;
	const char *out_dir;
	guint files;
	gboolean cancelled;
} CancelWatch;

/* Cancels once that many files of the archive have something in them. */
static gboolean
cancel_once_written (gpointer data)
{
	CancelWatch *watch = data;
	GFile *dir = g_file_new_for_path (watch->out_dir);
	GFileEnumerator *children;
	GFileInfo *info;
	guint written = 0;
	GList *l;

	children = nemo_enumerate_children (dir, G_FILE_ATTRIBUTE_STANDARD_SIZE,
					    G_FILE_QUERY_INFO_NOFOLLOW_SYMLINKS, NULL, NULL);
	if (children != NULL) {
		while ((info = g_file_enumerator_next_file (children, NULL, NULL)) != NULL) {
			if (g_file_info_get_size (info) > 0) {
				written++;
			}
			g_object_unref (info);
		}
		g_object_unref (children);
	}
	g_object_unref (dir);

	if (written < watch->files) {
		return G_SOURCE_CONTINUE;
	}

	for (l = nemo_progress_info_manager_get_all_infos (watch->manager); l != NULL; l = l->next) {
		nemo_progress_info_cancel (l->data);
		watch->cancelled = TRUE;
	}

	return watch->cancelled ? G_SOURCE_REMOVE : G_SOURCE_CONTINUE;
}

/* Bytes that do not compress, so writing them takes long enough to stop. */
static void
write_noise (const char *dir,
	     const char *name,
	     gsize       len)
{
	char *path = g_build_filename (dir, name, NULL);
	GRand *rand = g_rand_new_with_seed (7);
	gsize n = len / sizeof (guint32);
	guint32 *words = g_malloc (n * sizeof (guint32));
	gsize i;

	for (i = 0; i < n; i++) {
		words[i] = g_rand_int (rand);
	}
	check (g_file_set_contents (path, (const char *) words, (gssize) len, NULL));

	g_free (words);
	g_rand_free (rand);
	g_free (path);
}

#define NOISE_BYTES (48 * 1024 * 1024)

static gint glib_warnings;

/* A stopped writer used to set one GError over another, which GLib only warns
   about. */
static void
count_glib_warning (const char     *domain,
		    GLogLevelFlags  level,
		    const char     *message,
		    gpointer        data)
{
	g_atomic_int_inc (&glib_warnings);
	g_log_default_handler (domain, level, message, data);
}

static void
check_cancel (const char        *tmp,
	      NemoArchiveFormat  format,
	      guint64            split_size,
	      GtkWidget         *window)
{
	static const char * const names[] = { "noise.bin", NULL };
	char *source_dir = g_build_filename (tmp, "big", NULL);
	char *out_dir = g_strdup_printf ("%s/cancel-%s%s", tmp, nemo_archive_format_id (format),
					 split_size > 0 ? "-split" : "");
	char *name = g_strdup_printf ("big%s", nemo_archive_format_extension (format));
	char *path = g_build_filename (out_dir, name, NULL);
	GFile *destination = g_file_new_for_path (path);
	GList *sources;
	NemoArchiveOptions options;
	CancelWatch watch = { NULL, out_dir, 1, FALSE };
	guint watch_id;

	nemo_archive_options_init (&options);
	options.format = format;
	options.split_size = split_size;

	if (nemo_archive_pick_backend (format, &options) == NEMO_ARCHIVE_BACKEND_NONE) {
		g_printerr ("note: nothing here writes %s%s, its cancel not checked\n",
			    nemo_archive_format_id (format), split_size > 0 ? " in volumes" : "");
		nemo_archive_options_clear (&options);
		goto out;
	}

	if (!g_file_test (source_dir, G_FILE_TEST_IS_DIR)) {
		g_mkdir_with_parents (source_dir, 0700);
		write_noise (source_dir, "noise.bin", NOISE_BYTES);
	}
	g_mkdir_with_parents (out_dir, 0700);
	sources = sources_in (source_dir, names);

	/* Split, it stops a few volumes in, so the cleanup has more than the
	   first volume to find. */
	if (split_size > 0) {
		watch.files = 4;
	}

	watch.manager = nemo_progress_info_manager_new ();
	watch_id = g_timeout_add (2, cancel_once_written, &watch);

	job_finished = FALSE;
	job_succeeded = TRUE;
	nemo_archive_create (sources, destination, &options, GTK_WINDOW (window),
			     archive_done, NULL);
	wait_for_end ();

	if (!watch.cancelled) {
		g_source_remove (watch_id);
	}

	/* Stopped while writing, or this proves nothing about what is left. */
	check (watch.cancelled);
	check (!job_succeeded);
	check (count_entries (out_dir) == 0);
	close_dialogs ();

	g_object_unref (watch.manager);
	nemo_archive_options_clear (&options);
	g_list_free_full (sources, g_object_unref);
 out:
	g_object_unref (destination);
	g_free (path);
	g_free (name);
	g_free (out_dir);
	g_free (source_dir);
}

static gboolean
error_says (const char *words)
{
	GList *windows, *l;
	gboolean found = FALSE;

	windows = gtk_window_list_toplevels ();
	for (l = windows; l != NULL; l = l->next) {
		char *text = NULL;

		if (!GTK_IS_MESSAGE_DIALOG (l->data)) {
			continue;
		}
		g_object_get (l->data, "secondary-text", &text, NULL);
		if (text != NULL && strstr (text, words) != NULL) {
			found = TRUE;
		}
		g_free (text);
	}
	g_list_free (windows);

	return found;
}

/* A password is a requirement, so a 7-Zip line edited past {{PASSWORD}} fails
   the job and says why, rather than writing an archive anyone can open. */
static void
check_edited_line (const char *tmp,
		   GtkWidget  *window)
{
	static const char * const names[] = { "one.txt", NULL };
	char *source_dir = g_build_filename (tmp, "items", NULL);
	char *out_dir = g_build_filename (tmp, "edited", NULL);
	char *path = g_build_filename (out_dir, "locked.7z", NULL);
	GFile *destination = g_file_new_for_path (path);
	GList *sources = sources_in (source_dir, names);
	NemoArchiveOptions options;
	char **parts;
	char *line;

	nemo_archive_options_init (&options);
	options.format = NEMO_ARCHIVE_FORMAT_7Z;
	options.password = g_strdup ("secret");

	if (nemo_archive_pick_backend (options.format, &options) != NEMO_ARCHIVE_BACKEND_7Z) {
		g_printerr ("note: no 7z command here, an edited line not checked\n");
		goto out;
	}

	nemo_config_init ();
	parts = g_strsplit (NEMO_ARCHIVE_COMMAND_7Z_DEFAULT, "{{PASSWORD}}", -1);
	line = g_strjoinv ("", parts);
	nemo_config_set_string (nemo_config_get_group (NEMO_ARCHIVE_COMMANDS_GROUP),
				NEMO_ARCHIVE_COMMAND_KEY_7Z, line);
	g_mkdir_with_parents (out_dir, 0700);

	job_finished = FALSE;
	job_succeeded = TRUE;
	nemo_archive_create (sources, destination, &options, GTK_WINDOW (window),
			     archive_done, NULL);
	wait_for_end ();

	check (!job_succeeded);
	check (count_entries (out_dir) == 0);
	check (error_says ("{{PASSWORD}}"));
	close_dialogs ();

	nemo_config_reset (nemo_config_get_group (NEMO_ARCHIVE_COMMANDS_GROUP),
			   NEMO_ARCHIVE_COMMAND_KEY_7Z);
	nemo_config_shutdown ();
	g_strfreev (parts);
	g_free (line);
 out:
	nemo_archive_options_clear (&options);
	g_list_free_full (sources, g_object_unref);
	g_object_unref (destination);
	g_free (path);
	g_free (out_dir);
	g_free (source_dir);
}

#ifndef G_OS_WIN32
/* A source that cannot be read fails the job, and the failure takes what was
   written so far with it. */
static void
check_unreadable (const char *label,
		  GList      *sources,
		  const char *tmp,
		  GtkWidget  *window)
{
	char *out_dir = g_strdup_printf ("%s/unreadable-%s", tmp, label);
	char *path = g_build_filename (out_dir, "broken.zip", NULL);
	GFile *destination = g_file_new_for_path (path);
	NemoArchiveOptions options;

	g_mkdir_with_parents (out_dir, 0700);
	nemo_archive_options_init (&options);
	options.format = NEMO_ARCHIVE_FORMAT_ZIP;

	job_finished = FALSE;
	job_succeeded = TRUE;
	nemo_archive_create (sources, destination, &options, GTK_WINDOW (window),
			     archive_done, NULL);
	wait_for_end ();

	check (!job_succeeded);
	check (count_entries (out_dir) == 0);
	close_dialogs ();

	nemo_archive_options_clear (&options);
	g_object_unref (destination);
	g_free (path);
	g_free (out_dir);
}

static void
check_unreadable_sources (const char *tmp,
			  GtkWidget  *window)
{
	static const char * const names[] = { "fine.txt", "locked.txt", NULL };
	char *dir = g_build_filename (tmp, "unreadable", NULL);
	char *locked = g_build_filename (dir, "locked.txt", NULL);
	GList *sources;

	/* Root reads a file whatever its mode says. */
	if (geteuid () != 0) {
		g_mkdir_with_parents (dir, 0700);
		write_file (dir, "fine.txt", "fine");
		write_file (dir, "locked.txt", "locked");
		check (g_chmod (locked, 0) == 0);

		sources = sources_in (dir, names);
		check_unreadable ("mode", sources, tmp, window);
		g_list_free_full (sources, g_object_unref);

		g_chmod (locked, 0600);
	} else {
		g_printerr ("note: running as root, the unreadable file not checked\n");
	}

	/* A file that opens and then fails on the first read, for anyone:
	   this process's own memory at address zero. */
	if (g_file_test ("/proc/self/mem", G_FILE_TEST_EXISTS)) {
		sources = g_list_prepend (NULL, g_file_new_for_path ("/proc/self/mem"));
		check_unreadable ("read", sources, tmp, window);
		g_list_free_full (sources, g_object_unref);
	}

	g_free (locked);
	g_free (dir);
}
#endif

/* The check refuses with reason, and says why. */
static void
check_says (GFile                    *archive,
	    GList                    *sources,
	    const NemoArchiveOptions *options,
	    const char               *words)
{
	char *reason = NULL;

	check (!nemo_archive_verify (archive, sources, options, NEMO_ARCHIVE_BACKEND_LIBARCHIVE,
				     NULL, &reason));
	if (g_strcmp0 (reason, words) != 0) {
		g_printerr ("FAIL: the check said \"%s\", not \"%s\"\n",
			    reason != NULL ? reason : "(nothing)", words);
		failures++;
	}
	g_free (reason);
}

static void
remove_file (const char *dir,
	     const char *name)
{
	char *path = g_build_filename (dir, name, NULL);

	check (g_remove (path) == 0);
	g_free (path);
}

/* A failed delete check says what didn't match: how many selected folders
   and files aren't in the archive, how many in it weren't selected, and
   when those match, the sizes. */
static void
check_reasons (const char *tmp,
	       GtkWidget  *window)
{
	static const char * const names[] = { "a.txt", "deep", "empty", NULL };
	char *dir = g_build_filename (tmp, "reasons", NULL);
	char *deep = g_build_filename (dir, "deep", NULL);
	char *empty = g_build_filename (dir, "empty", NULL);
	char *newer = g_build_filename (deep, "newer", NULL);
	char *inner = g_build_filename (deep, "inner", NULL);
	char *path = g_build_filename (tmp, "reasons.zip", NULL);
	char *flat_path = g_build_filename (tmp, "reasons-flat.zip", NULL);
	char *dropped_path = g_build_filename (tmp, "reasons-dropped.zip", NULL);
	GFile *archive = g_file_new_for_path (path);
	GFile *flat = g_file_new_for_path (flat_path);
	GFile *dropped = g_file_new_for_path (dropped_path);
	GList *sources;
	NemoArchiveOptions options;
	char *selected, *archived, *words;

	g_mkdir_with_parents (inner, 0700);
	g_mkdir_with_parents (empty, 0700);
	write_file (dir, "a.txt", "alpha");
	write_file (deep, "b.txt", "bravo");
	write_file (inner, "d.txt", "delta");

	sources = sources_in (dir, names);
	nemo_archive_options_init (&options);
	options.format = NEMO_ARCHIVE_FORMAT_ZIP;
	compress_to (sources, archive, &options, window);
	check (nemo_archive_verify (archive, sources, &options, NEMO_ARCHIVE_BACKEND_LIBARCHIVE,
				    NULL, NULL));

	/* Not in the archive: a file, a folder, and both. */
	check (rewrite_without (path, dropped_path, "a.txt"));
	check_says (dropped, sources, &options, "1 file that was selected isn't in the archive.");
	check (rewrite_without (path, flat_path, NULL));
	check_says (flat, sources, &options, "1 folder that was selected isn't in the archive.");
	g_mkdir_with_parents (newer, 0700);
	write_file (newer, "x.txt", "xray");
	write_file (deep, "c.txt", "charlie");
	check_says (archive, sources, &options,
		    "1 folder and 2 files that were selected aren't in the archive.");

	/* In the archive and not selected. */
	remove_file (newer, "x.txt");
	check (g_rmdir (newer) == 0);
	remove_file (deep, "c.txt");
	remove_file (deep, "b.txt");
	check_says (archive, sources, &options, "1 file in the archive wasn't selected.");
	write_file (deep, "b.txt", "bravo");
	remove_file (inner, "d.txt");
	check (g_rmdir (inner) == 0);
	check_says (archive, sources, &options, "1 folder and 1 file in the archive weren't selected.");
	g_mkdir_with_parents (inner, 0700);
	write_file (inner, "d.txt", "delta");

	/* Both ways at once. */
	remove_file (deep, "b.txt");
	write_file (deep, "c.txt", "charlie");
	check_says (archive, sources, &options,
		    "1 file that was selected isn't in the archive. "
		    "1 file in the archive wasn't selected.");
	remove_file (deep, "c.txt");
	write_file (deep, "b.txt", "bravo");
	check (nemo_archive_verify (archive, sources, &options, NEMO_ARCHIVE_BACKEND_LIBARCHIVE,
				    NULL, NULL));

	/* Counts that match, and sizes that don't. */
	write_file (dir, "a.txt", "alpha and then some");
	selected = g_format_size_full (29, G_FORMAT_SIZE_LONG_FORMAT);
	archived = g_format_size_full (15, G_FORMAT_SIZE_LONG_FORMAT);
	words = g_strdup_printf ("The selection is %s, but the archive holds %s.", selected, archived);
	check_says (archive, sources, &options, words);
	g_free (words);
	g_free (archived);
	g_free (selected);

	/* The size is left out when a count is off. */
	write_file (deep, "c.txt", "charlie");
	check_says (archive, sources, &options, "1 file that was selected isn't in the archive.");
	remove_file (deep, "c.txt");

	/* The same total from 2 sizes that differ. */
	write_file (dir, "a.txt", "alph");
	write_file (deep, "b.txt", "bravo!");
	check_says (archive, sources, &options,
		    "2 files in the archive aren't the size of the ones selected.");
	write_file (dir, "a.txt", "alpha");
	write_file (deep, "b.txt", "bravo");

#ifndef G_OS_WIN32
	/* A link the options leave out is a selected file not in the archive. */
	{
		char *link_path = g_build_filename (deep, "dangling", NULL);
		GFile *link_file = g_file_new_for_path (link_path);

		check (g_file_make_symbolic_link (link_file, "nowhere", NULL, NULL));
		options.links.symlinks = ARC_LINK_IGNORE;
		compress_to (sources, archive, &options, window);
		check_says (archive, sources, &options, "1 file that was selected isn't in the archive.");
		g_remove (link_path);
		g_object_unref (link_file);
		g_free (link_path);
	}
#endif

	nemo_archive_options_clear (&options);
	g_list_free_full (sources, g_object_unref);
	g_object_unref (dropped);
	g_object_unref (flat);
	g_object_unref (archive);
	g_free (dropped_path);
	g_free (flat_path);
	g_free (path);
	g_free (inner);
	g_free (newer);
	g_free (empty);
	g_free (deep);
	g_free (dir);
}

/* What the job's pre-scan has told the progress info, seen on the job's
   thread each time it does. */
static struct {
	char             *destination;
	NemoProgressInfo *info;
	guint             reports;
	guint             not_running;
	guint             written_first;
	guint             midway;	/* some found, not all yet */
	guint             all_files;
	guint             files;
	guint64           bytes;
} prescan_seen;

static void
watch_prescan (NemoProgressInfo *info)
{
	guint files = 0;
	guint64 bytes = 0;

	if (prescan_seen.info == NULL) {
		prescan_seen.info = g_object_ref (info);
	}
	prescan_seen.reports++;
	if (!nemo_progress_info_get_prescan (info, NULL, &files, &bytes)) {
		prescan_seen.not_running++;
	}
	if (g_file_test (prescan_seen.destination, G_FILE_TEST_EXISTS)) {
		prescan_seen.written_first++;
	}
	if (files > 0 && files < prescan_seen.all_files) {
		prescan_seen.midway++;
	}
	prescan_seen.files = files;
	prescan_seen.bytes = bytes;
}

/* The pre-scan reports what it has found, through the core's walk, before
   the first file is written, and is over once the job is. Enough files that
   the walk reports midway too. */
static void
check_prescan_reports (const char        *tmp,
		       NemoArchiveFormat  format,
		       const char        *name,
		       GtkWidget         *window)
{
	char *dir = g_build_filename (tmp, "prescan", NULL);
	char *many = g_build_filename (dir, "many", NULL);
	char *path = g_build_filename (tmp, name, NULL);
	GFile *destination = g_file_new_for_path (path);
	GList *sources;
	NemoArchiveOptions options;
	guint64 bytes = 0;
	guint i;

	g_mkdir_with_parents (many, 0700);
	for (i = 0; i < 150; i++) {
		char *file_name = g_strdup_printf ("f%03u.txt", i);
		char *text = g_strnfill (i % 7 + 1, 'x');

		write_file (many, file_name, text);
		bytes += i % 7 + 1;
		g_free (text);
		g_free (file_name);
	}
	sources = g_list_append (NULL, g_file_new_for_path (many));

	memset (&prescan_seen, 0, sizeof prescan_seen);
	prescan_seen.destination = path;
	prescan_seen.all_files = 150;
	nemo_archive_set_prescan_watch_for_test (watch_prescan);

	nemo_archive_options_init (&options);
	options.format = format;
	compress_to (sources, destination, &options, window);
	nemo_archive_set_prescan_watch_for_test (NULL);

	check (g_file_test (path, G_FILE_TEST_EXISTS));
	check (prescan_seen.reports >= 3);
	check (prescan_seen.not_running == 0);
	check (prescan_seen.written_first == 0);
	check (prescan_seen.midway > 0);
	check (prescan_seen.files == 150);
	check (prescan_seen.bytes == bytes);
	check (prescan_seen.info != NULL &&
	       !nemo_progress_info_get_prescan (prescan_seen.info, NULL, NULL, NULL));
	g_clear_object (&prescan_seen.info);

	nemo_archive_options_clear (&options);
	g_list_free_full (sources, g_object_unref);
	g_object_unref (destination);
	g_remove (path);
	g_free (path);
	g_free (many);
	g_free (dir);
}

static gboolean
time_up (gpointer data)
{
	*(gboolean *) data = TRUE;
	return G_SOURCE_REMOVE;
}

/* Runs the main loop for a while. With since, says whether the pre-scan
   bar ever showed sooner than half a second after it. */
static gboolean
run_for (guint       msec,
	 GtkWidget  *bar,
	 gint64      since)
{
	gboolean done = FALSE;
	gboolean early = FALSE;

	g_timeout_add (msec, time_up, &done);
	while (!done) {
		g_main_context_iteration (NULL, TRUE);
		if (since != 0 && gtk_widget_get_visible (bar) &&
		    g_get_monotonic_time () - since < G_USEC_PER_SEC / 2) {
			early = TRUE;
		}
	}
	return early;
}

static GtkWidget *
find_named (GtkWidget  *widget,
	    const char *name)
{
	GList *children, *l;
	GtkWidget *found = NULL;

	if (g_strcmp0 (gtk_widget_get_name (widget), name) == 0) {
		return widget;
	}
	if (!GTK_IS_CONTAINER (widget)) {
		return NULL;
	}
	children = gtk_container_get_children (GTK_CONTAINER (widget));
	for (l = children; l != NULL && found == NULL; l = l->next) {
		found = find_named (l->data, name);
	}
	g_list_free (children);
	return found;
}

/* The second bar shows only once the pre-scan has run half a second, so
   one that's over sooner never flashes it, and it goes when the walk ends. */
static void
check_prescan_bar (void)
{
	NemoProgressInfo *info = nemo_progress_info_new ();
	GtkWidget *window = gtk_offscreen_window_new ();
	GtkWidget *widget = nemo_progress_info_widget_new (info);
	GtkWidget *bar = find_named (widget, "prescan");
	GtkWidget *found = find_named (widget, "prescan-found");
	gint64 since = 0;
	char *size, *words;

	gtk_container_add (GTK_CONTAINER (window), widget);
	gtk_widget_show_all (window);
	check (bar != NULL && found != NULL);
	if (bar == NULL || found == NULL) {
		goto out;
	}
	nemo_progress_info_start (info);
	check (!gtk_widget_get_visible (bar));

	/* Over quickly. */
	nemo_progress_info_start_prescan (info);
	nemo_progress_info_set_prescan_found (info, 2, 10);
	nemo_progress_info_end_prescan (info);
	run_for (800, bar, 0);
	check (!gtk_widget_get_visible (bar));

	/* Still going after half a second. */
	nemo_progress_info_start_prescan (info);
	nemo_progress_info_get_prescan (info, &since, NULL, NULL);
	nemo_progress_info_set_prescan_found (info, 3, 1234);
	check (!run_for (900, bar, since));
	check (gtk_widget_get_visible (bar));
	size = g_format_size (1234);
	words = g_strdup_printf ("Found 3 files, %s", size);
	check (g_strcmp0 (gtk_label_get_text (GTK_LABEL (found)), words) == 0);
	g_free (words);
	g_free (size);

	nemo_progress_info_end_prescan (info);
	run_for (300, bar, 0);
	check (!gtk_widget_get_visible (bar));

 out:
	nemo_progress_info_finish (info);
	run_for (200, bar, 0);
	gtk_widget_destroy (window);
	g_object_unref (info);
}

int
main (int argc, char *argv[])
{
	static const char * const names[] = { "one.txt", "two.txt", "sub", NULL };
	GtkWidget *window;
	char *tmp;
	char *source_dir;
	char *sub_dir;
	char *one_dir;
	char *each_dir;
	char *config_home;
	guint handler;

	/* Before anything reads the config root, which GLib keeps from its first
	   answer. Made later, it was the real one that got written. */
	config_home = test_scratch_config_home ("nemo-archive-test-config-XXXXXX");
	test_init (&argc, &argv);

	tmp = test_scratch_dir ("nemo-archive-test-XXXXXX", NULL);
	source_dir = g_build_filename (tmp, "items", NULL);
	sub_dir = g_build_filename (source_dir, "sub", NULL);
	one_dir = g_build_filename (tmp, "one", NULL);
	each_dir = g_build_filename (tmp, "each", NULL);

	g_mkdir_with_parents (sub_dir, 0700);
	g_mkdir_with_parents (one_dir, 0700);
	g_mkdir_with_parents (each_dir, 0700);

	write_file (source_dir, "one.txt", "first");
	write_file (source_dir, "two.txt", "second");
	write_file (sub_dir, "inner.txt", "inner");

	window = test_window_new ("archive test", 5);
	gtk_widget_show (window);

	check_one_archive (source_dir, one_dir, names, window);
	check_each_archive (source_dir, each_dir, names, window);
	check_archive_of_archive (each_dir, window);
	check_verify (tmp, window);
	check_verify_followed (tmp, window);
	check_reasons (tmp, window);
	check_prescan_reports (tmp, NEMO_ARCHIVE_FORMAT_ZIP, "prescan.zip", window);
	if (nemo_archive_backend_present (NEMO_ARCHIVE_BACKEND_RAR)) {
		check_prescan_reports (tmp, NEMO_ARCHIVE_FORMAT_RAR, "prescan.rar", window);
	}
	check_prescan_bar ();
	handler = g_log_set_handler ("GLib", G_LOG_LEVEL_WARNING, count_glib_warning, NULL);
	check_cancel (tmp, NEMO_ARCHIVE_FORMAT_ZIP, 0, window);
	check_cancel (tmp, NEMO_ARCHIVE_FORMAT_7Z, 1024 * 1024, window);
	g_log_remove_handler ("GLib", handler);
	check (g_atomic_int_get (&glib_warnings) == 0);
#ifndef G_OS_WIN32
	check_unreadable_sources (tmp, window);
#endif
	check_edited_line (tmp, window);

	g_free (each_dir);
	g_free (one_dir);
	g_free (sub_dir);
	g_free (source_dir);
	g_free (tmp);
	g_free (config_home);

	if (failures == 0) {
		g_print ("archive: all checks passed\n");
	}

	return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
