/* Compressing for real: a folder of items is handed to the job both ways -
 * everything into one archive, and one archive per item - and what reaches
 * disk is read back with libarchive. What goes in each archive is the point,
 * so the entry names are checked rather than just the file being there.
 *
 * Also a run cancelled partway, and one that fails on a source it cannot read:
 * neither may leave a half-written archive, or a volume of one, behind. */

#include "test.h"

#include <libnemo-private/nemo-archive.h>
#include <libnemo-private/nemo-dir-enum.h>
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

	nemo_archive_options_clear (&options);
}

/* The same archive rewritten with its folder entries left out. A writer is
   free to store only the files, and the verify has to read a folder as there
   when something inside it is. */
static gboolean
rewrite_without_directories (const char *src,
			     const char *dst)
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

		if (archive_entry_filetype (entry) == AE_IFDIR) {
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

		check (rewrite_without_directories (path, flat_path));

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
	   that leads nowhere is one of those only where the writer cannot keep
	   it as a link. The library can, with the links box unticked too. */
	{
		char *link_path = g_build_filename (deep, "dangling", NULL);
		GFile *link_file = g_file_new_for_path (link_path);
		char *link_archive = g_build_filename (out, "links.zip", NULL);
		GFile *link_destination = g_file_new_for_path (link_archive);

		check (g_file_make_symbolic_link (link_file, "nowhere", NULL, NULL));
		options.store_links = FALSE;

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
		check (nemo_archive_verify (link_destination, sources, &options,
					    NEMO_ARCHIVE_BACKEND_LIBARCHIVE, NULL, &reason));
		check (reason == NULL);
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

		g_remove (link_path);
		options.store_links = TRUE;

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
	guint handler;

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
	handler = g_log_set_handler ("GLib", G_LOG_LEVEL_WARNING, count_glib_warning, NULL);
	check_cancel (tmp, NEMO_ARCHIVE_FORMAT_ZIP, 0, window);
	check_cancel (tmp, NEMO_ARCHIVE_FORMAT_7Z, 1024 * 1024, window);
	g_log_remove_handler ("GLib", handler);
	check (g_atomic_int_get (&glib_warnings) == 0);
#ifndef G_OS_WIN32
	check_unreadable_sources (tmp, window);
#endif

	g_free (each_dir);
	g_free (one_dir);
	g_free (sub_dir);
	g_free (source_dir);
	g_free (tmp);

	if (failures == 0) {
		g_print ("archive: all checks passed\n");
	}

	return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
