/* Unpacking for real: a zip is written with libarchive, handed to the job, and
 * the folder it unpacks into is inspected. Covers the two layouts, the paths an
 * archive can name, and the guard that keeps a hostile one from writing outside
 * the folder that was picked. Collisions are not covered here - every answer to
 * one comes from a dialog, and there is nobody to click it.
 *
 * Also a 7z split three ways, where the later volumes are what is selected: it
 * unpacks once, from the first volume. Needs a 7z command to write it.
 *
 * Also archives whose names hold * or ?, unpacked through the commands, which
 * must not read those names as patterns, with the 7-Zip line as shipped and
 * as edited. */

#include "test.h"

#include <libnemo-private/nemo-archive-commands.h>
#include <libnemo-private/nemo-config.h>
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
extract_done (G_GNUC_UNUSED GFile    *destination_dir,
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

static gboolean
start_and_wait (GList             *archives,
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
	g_object_unref (dest);

	return job_succeeded;
}

static void
run_job_on (GList             *archives,
	    const char        *destination,
	    NemoExtractLayout  layout,
	    GtkWidget         *window)
{
	check (start_and_wait (archives, destination, layout, window));
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

#ifndef G_OS_WIN32
/* Names with * or ? cannot be made on Windows. */
static char *
find_rar (void)
{
	static const char * const names[] = { "rar", NULL };
	char *found = NULL;
	int i;

	for (i = 0; names[i] != NULL && found == NULL; i++) {
		found = g_find_program_in_path (names[i]);
	}

	return found;
}

static gboolean
run_in (const char *dir, char **argv)
{
	char *out = NULL, *err = NULL;
	int status = -1;
	gboolean ok;

	ok = g_spawn_sync (dir, argv, NULL, G_SPAWN_DEFAULT, NULL, NULL,
			   &out, &err, &status, NULL) &&
	     g_spawn_check_wait_status (status, NULL);
	if (!ok) {
		g_printerr ("%s said: %s%s\n", argv[0], out != NULL ? out : "", err != NULL ? err : "");
	}
	g_free (out);
	g_free (err);

	return ok;
}

static void
make_folder_with (const char *dir, const char *name)
{
	char *folder = g_build_filename (dir, name, NULL);
	char *file_name = g_strconcat (name, ".txt", NULL);
	char *path = g_build_filename (folder, file_name, NULL);

	g_mkdir_with_parents (folder, 0700);
	check (g_file_set_contents (path, name, -1, NULL));
	g_free (path);
	g_free (file_name);
	g_free (folder);
}

static void
rename_in (const char *dir, const char *from, const char *to)
{
	char *from_path = g_build_filename (dir, from, NULL);
	char *to_path = g_build_filename (dir, to, NULL);

	check (g_rename (from_path, to_path) == 0);
	g_free (to_path);
	g_free (from_path);
}

/* Fills in the password when it is asked for. Anything else but a message,
   which take_messages reads, is refused. */
static gboolean
answer_password (gpointer data)
{
	guint *other = data;
	GList *windows, *l;

	windows = gtk_window_list_toplevels ();
	for (l = windows; l != NULL; l = l->next) {
		GtkWidget *content;
		GList *boxes, *children, *c;
		gboolean filled = FALSE;

		if (!GTK_IS_DIALOG (l->data) || GTK_IS_MESSAGE_DIALOG (l->data) ||
		    !gtk_widget_get_mapped (l->data)) {
			continue;
		}
		if (g_strcmp0 (gtk_window_get_title (GTK_WINDOW (l->data)), "Password required") != 0) {
			(*other)++;
			gtk_dialog_response (GTK_DIALOG (l->data), GTK_RESPONSE_DELETE_EVENT);
			continue;
		}

		content = gtk_dialog_get_content_area (GTK_DIALOG (l->data));
		boxes = gtk_container_get_children (GTK_CONTAINER (content));
		children = boxes != NULL && GTK_IS_CONTAINER (boxes->data)
			? gtk_container_get_children (GTK_CONTAINER (boxes->data)) : NULL;
		for (c = children; c != NULL; c = c->next) {
			if (GTK_IS_ENTRY (c->data)) {
				gtk_entry_set_text (GTK_ENTRY (c->data), "secret");
				filled = TRUE;
			}
		}
		g_list_free (children);
		g_list_free (boxes);
		check (filled);
		gtk_dialog_response (GTK_DIALOG (l->data), GTK_RESPONSE_OK);
	}
	g_list_free (windows);

	return G_SOURCE_CONTINUE;
}

/* What the message dialogs a job left up said, closed as they are read. */
static char *
take_messages (void)
{
	GString *said = g_string_new (NULL);
	GList *windows, *l;

	while (g_main_context_iteration (NULL, FALSE)) {
	}

	windows = gtk_window_list_toplevels ();
	for (l = windows; l != NULL; l = l->next) {
		char *text = NULL;

		if (!GTK_IS_MESSAGE_DIALOG (l->data)) {
			continue;
		}
		g_object_get (l->data, "secondary-text", &text, NULL);
		g_string_append_printf (said, "%s\n", text != NULL ? text : "");
		g_free (text);
		gtk_widget_destroy (GTK_WIDGET (l->data));
	}
	g_list_free (windows);

	return g_string_free (said, FALSE);
}

/* An archive named with * or ? is one archive. Unpacked through a command, the
   name must not be read as a pattern that takes in its neighbors too. */
static void
check_pattern_names (const char *tmp,
		     GtkWidget  *window)
{
	char *seven = find_seven_zip ();
	char *rar = find_rar ();
	char *work, *dest, *path, *wanted, *stray;
	guint other = 0, answer_id;

	if (seven == NULL) {
		g_printerr ("note: no 7z command here, names with wildcards not checked\n");
		g_free (rar);
		return;
	}

	work = g_build_filename (tmp, "patterns", NULL);
	dest = g_build_filename (tmp, "unpatterned", NULL);
	g_mkdir_with_parents (work, 0700);
	g_mkdir_with_parents (dest, 0700);
	make_folder_with (work, "one");
	make_folder_with (work, "two");

	/* Encrypted names are something libarchive cannot read, so these go to
	   the commands, which then ask for the password. */
	answer_id = g_timeout_add (100, answer_password, &other);
	{
		char *first[] = { seven, (char *) "a", (char *) "-psecret", (char *) "-mhe=on",
				  (char *) "sq.7z", (char *) "one", NULL };
		char *second[] = { seven, (char *) "a", (char *) "sx.7z", (char *) "two", NULL };

		check (run_in (work, first));
		check (run_in (work, second));
		rename_in (work, "sq.7z", "s?.7z");
	}

	path = g_build_filename (work, "s?.7z", NULL);
	run_job (path, dest, NEMO_EXTRACT_TO_SUBFOLDER, window);
	wanted = g_build_filename (dest, "s?", "one", "one.txt", NULL);
	stray = g_build_filename (dest, "s?", "two", NULL);
	check (g_file_test (wanted, G_FILE_TEST_IS_REGULAR));
	check (!g_file_test (stray, G_FILE_TEST_EXISTS));
	g_free (stray);
	g_free (wanted);

	/* Again with the 7-Zip line edited without -spd, which the app puts back
	   at run time. */
	{
		NemoConfigGroup *group = nemo_config_get_group (NEMO_ARCHIVE_COMMANDS_GROUP);
		char *edited = g_build_filename (tmp, "unpatterned-edited", NULL);

		g_mkdir_with_parents (edited, 0700);
		nemo_config_set_string (group, NEMO_EXTRACT_COMMAND_KEY_7Z,
					"{{PROGRAM}} x -y -bsp1 {{PASSWORD}} -o{{TARGET_FOLDER}} -- {{SOURCE_ARCHIVE}}");
		run_job (path, edited, NEMO_EXTRACT_TO_SUBFOLDER, window);
		nemo_config_reset (group, NEMO_EXTRACT_COMMAND_KEY_7Z);
		wanted = g_build_filename (edited, "s?", "one", "one.txt", NULL);
		stray = g_build_filename (edited, "s?", "two", NULL);
		check (g_file_test (wanted, G_FILE_TEST_IS_REGULAR));
		check (!g_file_test (stray, G_FILE_TEST_EXISTS));
		g_free (stray);
		g_free (wanted);
		g_free (edited);
	}
	g_free (path);

	/* rar has no way to take a name as it is, so it is passed over. 7z reads
	   rar too, where it was built with the rar codec, and does the job; where
	   not, nothing does, and that is the answer. */
	if (rar == NULL) {
		g_printerr ("note: no rar command here, rar names with wildcards not checked\n");
	} else {
		char *first[] = { rar, (char *) "a", (char *) "-hpsecret", (char *) "rq.rar", (char *) "one", NULL };
		char *second[] = { rar, (char *) "a", (char *) "rx.rar", (char *) "two", NULL };
		GList *archives;
		gboolean unpacked;
		char *said;

		check (run_in (work, first));
		check (run_in (work, second));
		rename_in (work, "rq.rar", "r?.rar");

		path = g_build_filename (work, "r?.rar", NULL);
		archives = g_list_prepend (NULL, g_file_new_for_path (path));
		unpacked = start_and_wait (archives, dest, NEMO_EXTRACT_TO_SUBFOLDER, window);
		g_list_free_full (archives, g_object_unref);

		wanted = g_build_filename (dest, "r?", "one", "one.txt", NULL);
		stray = g_build_filename (dest, "r?", "two", NULL);
		said = take_messages ();
		check (g_file_test (wanted, G_FILE_TEST_IS_REGULAR) == unpacked);
		check (!g_file_test (stray, G_FILE_TEST_EXISTS));
		check (unpacked || strstr (said, "wildcard") != NULL);
		g_free (said);
		g_free (stray);
		g_free (wanted);
		g_free (path);
	}
	g_source_remove (answer_id);
	check (other == 0);

	g_free (dest);
	g_free (work);
	g_free (rar);
	g_free (seven);
}
#endif

int
main (int argc, char *argv[])
{
	GtkWidget *window;
	char *tmp;
	char *archive_path;
	char *config_home;

	/* Unpacking through a command clears its staging folder afterwards, and
	   the delete test guard would stop to ask about that. */
	g_setenv ("NEMO_TESTGUARD_ALL_DELETES", "0", TRUE);

	test_init (&argc, &argv);

	/* For the edited command line. */
	config_home = test_scratch_config_home ("nemo-extract-test-home-XXXXXX");
	nemo_config_init ();

	tmp = test_scratch_dir ("nemo-extract-test-XXXXXX", NULL);
	archive_path = g_build_filename (tmp, "photos.zip", NULL);

	write_test_zip (archive_path);
	check (g_file_test (archive_path, G_FILE_TEST_IS_REGULAR));

	window = test_window_new ("extract test", 5);
	gtk_widget_show (window);

	check_here_layout (tmp, archive_path, window);
	check_subfolder_layout (tmp, archive_path, window);
	check_split_volumes (tmp, window);
#ifndef G_OS_WIN32
	check_pattern_names (tmp, window);
#endif

	nemo_config_shutdown ();
	g_free (archive_path);
	g_free (tmp);
	g_free (config_home);

	if (failures == 0) {
		g_print ("extract: all checks passed\n");
	}

	return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
