/* Unpacking for real: a zip is written with libarchive, handed to the job, and
 * the folder it unpacks into is inspected. Covers the two layouts, the paths an
 * archive can name, and the guard that keeps a hostile one from writing outside
 * the folder that was picked. Collisions are not covered here - every answer to
 * one comes from a dialog, and there is nobody to click it.
 *
 * Also a 7z split three ways, where the later volumes are what is selected: it
 * unpacks once, from the first volume. Needs a 7z command to write it. */

#include "test.h"

#include <libnemo-private/nemo-dir-enum.h>
#include <libnemo-private/nemo-extract.h>

#include <archive.h>
#include <archive_entry.h>
#include <glib/gstdio.h>
#include <stdlib.h>
#include <string.h>

#include "test-scratch.h"
#include "test-check.h"

#define EXTRACT_TIMEOUT_SECONDS 30

static gboolean job_finished;
static gboolean job_succeeded;

static void
extract_done (GFile    *destination_dir,
	      gboolean  success,
	      gpointer  data)
{
	job_succeeded = success;
	job_finished = TRUE;
	gtk_main_quit ();
}

static gboolean
give_up (gpointer data)
{
	g_printerr ("FAIL: unpacking did not finish within %d seconds\n",
		    EXTRACT_TIMEOUT_SECONDS);
	failures++;
	gtk_main_quit ();

	return G_SOURCE_REMOVE;
}

static void
add_file_entry (struct archive *a,
		const char     *path,
		const char     *contents)
{
	struct archive_entry *entry = archive_entry_new ();

	archive_entry_set_pathname (entry, path);
	archive_entry_set_size (entry, (la_int64_t) strlen (contents));
	archive_entry_set_filetype (entry, AE_IFREG);
	archive_entry_set_perm (entry, 0644);
	archive_entry_set_mtime (entry, 1000000000, 0);

	check (archive_write_header (a, entry) == ARCHIVE_OK);
	check (archive_write_data (a, contents, strlen (contents)) == (la_ssize_t) strlen (contents));

	archive_entry_free (entry);
}

static void
add_dir_entry (struct archive *a,
	       const char     *path)
{
	struct archive_entry *entry = archive_entry_new ();

	archive_entry_set_pathname (entry, path);
	archive_entry_set_size (entry, 0);
	archive_entry_set_filetype (entry, AE_IFDIR);
	archive_entry_set_perm (entry, 0755);

	check (archive_write_header (a, entry) == ARCHIVE_OK);

	archive_entry_free (entry);
}

/* A folder with a file at its root and one a level down, plus an entry whose
   stored path climbs out of wherever it is unpacked. */
static void
write_test_zip (const char *path)
{
	struct archive *a = archive_write_new ();

	check (archive_write_set_format_zip (a) == ARCHIVE_OK);
	check (archive_write_open_filename (a, path) == ARCHIVE_OK);

	add_dir_entry (a, "photos/");
	add_file_entry (a, "photos/one.txt", "first");
	add_dir_entry (a, "photos/sub/");
	add_file_entry (a, "photos/sub/two.txt", "second");
	add_file_entry (a, "../escape.txt", "should not escape");

	check (archive_write_close (a) == ARCHIVE_OK);
	archive_write_free (a);
}

static void
run_job_on (GList             *archives,
	    const char        *destination,
	    NemoExtractLayout  layout,
	    GtkWidget         *window)
{
	GFile *dest;
	guint timeout_id;

	job_finished = FALSE;
	job_succeeded = FALSE;

	dest = g_file_new_for_path (destination);

	nemo_extract_files (archives, dest, layout, GTK_WINDOW (window), extract_done, NULL);

	/* The job runs on a worker; without a deadline a hang would sit here
	   until meson's own timeout killed the run. */
	timeout_id = g_timeout_add_seconds (EXTRACT_TIMEOUT_SECONDS, give_up, NULL);
	gtk_main ();
	g_source_remove (timeout_id);

	check (job_finished);
	check (job_succeeded);

	g_object_unref (dest);
}

static void
run_job (const char        *archive_path,
	 const char        *destination,
	 NemoExtractLayout  layout,
	 GtkWidget         *window)
{
	GList *archives = g_list_prepend (NULL, g_file_new_for_path (archive_path));

	run_job_on (archives, destination, layout, window);
	g_list_free_full (archives, g_object_unref);
}

static void
check_contents (const char *path,
		const char *expected)
{
	char *contents = NULL;

	if (g_file_get_contents (path, &contents, NULL, NULL)) {
		check (g_strcmp0 (contents, expected) == 0);
		g_free (contents);
	} else {
		g_printerr ("FAIL: %s could not be read\n", path);
		failures++;
	}
}

static void
check_here_layout (const char *tmp,
		   const char *archive_path,
		   GtkWidget  *window)
{
	char *dest = g_build_filename (tmp, "here", NULL);
	char *one, *two, *escaped, *outside;

	g_mkdir_with_parents (dest, 0700);
	run_job (archive_path, dest, NEMO_EXTRACT_HERE, window);

	/* The archive holds a folder, so unpacking here produces that folder
	   rather than its contents loose. */
	one = g_build_filename (dest, "photos", "one.txt", NULL);
	two = g_build_filename (dest, "photos", "sub", "two.txt", NULL);
	check (g_file_test (one, G_FILE_TEST_IS_REGULAR));
	check (g_file_test (two, G_FILE_TEST_IS_REGULAR));
	check_contents (one, "first");
	check_contents (two, "second");

	/* The climbing entry stayed inside the folder that was picked, and
	   nothing appeared beside it. */
	escaped = g_build_filename (dest, "escape.txt", NULL);
	outside = g_build_filename (tmp, "escape.txt", NULL);
	check (g_file_test (escaped, G_FILE_TEST_IS_REGULAR));
	check (!g_file_test (outside, G_FILE_TEST_EXISTS));

	g_free (one);
	g_free (two);
	g_free (escaped);
	g_free (outside);
	g_free (dest);
}

static void
check_subfolder_layout (const char *tmp,
			const char *archive_path,
			GtkWidget  *window)
{
	char *dest = g_build_filename (tmp, "each", NULL);
	char *one;

	g_mkdir_with_parents (dest, 0700);
	run_job (archive_path, dest, NEMO_EXTRACT_TO_SUBFOLDER, window);

	/* A folder named after the archive, holding what the archive holds -
	   here that is the archive's own "photos" folder, one level further in.
	   The layout is literal on purpose: what it is for is an archive that
	   would otherwise scatter, and guessing would make it unpredictable. */
	one = g_build_filename (dest, "photos", "photos", "one.txt", NULL);
	check (g_file_test (one, G_FILE_TEST_IS_REGULAR));
	check_contents (one, "first");

	g_free (one);
	g_free (dest);
}

static char *
find_seven_zip (void)
{
	static const char * const names[] = { "7z", "7zz", "7za", NULL };
	char *found = NULL;
	int i;

	for (i = 0; names[i] != NULL && found == NULL; i++) {
		found = g_find_program_in_path (names[i]);
	}

	return found;
}

/* Bytes that do not compress, so the volumes come out the size asked for. */
static char *
noise (gsize len, guint32 seed)
{
	GRand *rand = g_rand_new_with_seed (seed);
	char *bytes = g_malloc (len);
	gsize i;

	for (i = 0; i < len; i++) {
		bytes[i] = (char) g_rand_int_range (rand, 0, 256);
	}
	g_rand_free (rand);

	return bytes;
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
			n++;
			g_object_unref (info);
		}
		g_object_unref (children);
	}
	g_object_unref (dir);

	return n;
}

/* Once from the first volume asks nothing. A second pass over the same volumes
   would stop to ask about the folder the first one made, and with nobody to
   answer, the run would sit there. */
static gboolean
refuse_dialogs (gpointer data)
{
	guint *asked = data;
	GList *windows, *l;

	windows = gtk_window_list_toplevels ();
	for (l = windows; l != NULL; l = l->next) {
		if (GTK_IS_DIALOG (l->data) && gtk_widget_get_mapped (l->data)) {
			(*asked)++;
			gtk_dialog_response (GTK_DIALOG (l->data), GTK_RESPONSE_DELETE_EVENT);
		}
	}
	g_list_free (windows);

	return G_SOURCE_CONTINUE;
}

#define SPLIT_PART_BYTES (40 * 1024)
#define SPLIT_PARTS 3

static void
check_split_volumes (const char *tmp,
		     GtkWidget  *window)
{
	char *seven = find_seven_zip ();
	char *work, *source, *dest, *archive, *out = NULL, *err = NULL;
	char *argv[] = { seven, (char *) "a", (char *) "-mx0", (char *) "-v50k",
			 (char *) "split.7z", (char *) "split", NULL };
	char *contents[SPLIT_PARTS];
	GList *selected = NULL;
	guint asked = 0, refuse_id;
	int status = -1, i;

	if (seven == NULL) {
		g_printerr ("note: no 7z command here, split volumes not checked\n");
		return;
	}

	work = g_build_filename (tmp, "volumes", NULL);
	source = g_build_filename (work, "split", NULL);
	dest = g_build_filename (tmp, "unsplit", NULL);
	g_mkdir_with_parents (source, 0700);
	g_mkdir_with_parents (dest, 0700);

	for (i = 0; i < SPLIT_PARTS; i++) {
		char *name = g_strdup_printf ("part%d.bin", i);
		char *path = g_build_filename (source, name, NULL);

		contents[i] = noise (SPLIT_PART_BYTES, (guint32) i + 1);
		check (g_file_set_contents (path, contents[i], SPLIT_PART_BYTES, NULL));
		g_free (path);
		g_free (name);
	}

	check (g_spawn_sync (work, argv, NULL, G_SPAWN_DEFAULT, NULL, NULL,
			     &out, &err, &status, NULL));
	check (g_spawn_check_wait_status (status, NULL));

	/* Three volumes and no fourth, or the case is not the one meant. */
	for (i = 1; i <= SPLIT_PARTS + 1; i++) {
		char *name = g_strdup_printf ("split.7z.%03d", i);

		archive = g_build_filename (work, name, NULL);
		check (g_file_test (archive, G_FILE_TEST_IS_REGULAR) == (i <= SPLIT_PARTS));
		if (i >= 2 && i <= SPLIT_PARTS) {
			selected = g_list_append (selected, g_file_new_for_path (archive));
		}
		g_free (archive);
		g_free (name);
	}

	refuse_id = g_timeout_add (100, refuse_dialogs, &asked);
	run_job_on (selected, dest, NEMO_EXTRACT_TO_SUBFOLDER, window);
	g_source_remove (refuse_id);
	check (asked == 0);

	/* Once: one folder, named for the archive, holding every part whole. */
	check (count_entries (dest) == 1);
	for (i = 0; i < SPLIT_PARTS; i++) {
		char *name = g_strdup_printf ("part%d.bin", i);
		char *path = g_build_filename (dest, "split", "split", name, NULL);
		char *got = NULL;
		gsize len = 0;

		check (g_file_get_contents (path, &got, &len, NULL));
		check (len == SPLIT_PART_BYTES && got != NULL &&
		       memcmp (got, contents[i], SPLIT_PART_BYTES) == 0);
		g_free (got);
		g_free (path);
		g_free (name);
		g_free (contents[i]);
	}

	g_list_free_full (selected, g_object_unref);
	g_free (out);
	g_free (err);
	g_free (dest);
	g_free (source);
	g_free (work);
	g_free (seven);
}

int
main (int argc, char *argv[])
{
	GtkWidget *window;
	char *tmp;
	char *archive_path;

	/* Unpacking through a command clears its staging folder afterwards, and
	   the delete test guard would stop to ask about that. */
	g_setenv ("NEMO_TESTGUARD_ALL_DELETES", "0", TRUE);

	test_init (&argc, &argv);

	tmp = test_scratch_dir ("nemo-extract-test-XXXXXX", NULL);
	archive_path = g_build_filename (tmp, "photos.zip", NULL);

	write_test_zip (archive_path);
	check (g_file_test (archive_path, G_FILE_TEST_IS_REGULAR));

	window = test_window_new ("extract test", 5);
	gtk_widget_show (window);

	check_here_layout (tmp, archive_path, window);
	check_subfolder_layout (tmp, archive_path, window);
	check_split_volumes (tmp, window);

	g_free (archive_path);
	g_free (tmp);

	if (failures == 0) {
		g_print ("extract: all checks passed\n");
	}

	return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
