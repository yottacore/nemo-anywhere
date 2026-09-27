/* Compress with the options crossed, for every format this box can write.
 * Every option a format offers is paired with every other at least once.
 * The ones that decide whether the originals may be deleted - delete itself,
 * one archive or one per item, splitting, the password and hidden names - are
 * crossed in full on top of that.
 *
 * Each run is read back: what went in and in what form, that a password
 * keeps the contents or the names out, that a split made volumes, and that
 * the originals went only where the archive checked out, with a warning
 * where they were kept.
 */

#include "test.h"

#include <libnemo-private/nemo-archive.h>
#include <libnemo-private/nemo-global-preferences.h>
#include <libnemo-private/nemo-link-copy.h>
#include <libnemo-private/nemo-progress-info-manager.h>

#include <archive.h>
#include <archive_entry.h>
#include <glib/gstdio.h>
#include <stdlib.h>
#include <string.h>

#include "test-scratch.h"
#include "test-check.h"

#define JOB_TIMEOUT_SECONDS 60
#define SPLIT_BYTES         65536
#define PASSWORD            "secret"

typedef enum {
	P_LEVEL,
	P_PASSWORD,
	P_NAMES,
	P_SPLIT,
	P_SOLID,
	P_DEDUPE,
	P_LINKS,
	P_RECOVERY,
	P_LOCK,
	P_FOLLOW,
	P_DELETE,
	P_EACH,
	N_PARAMS
} Param;

#define MAX_VALUES 3

static const char * const param_names[N_PARAMS] = {
	"level", "password", "names", "split", "solid", "dedupe", "links",
	"recovery", "lock", "follow", "delete", "each"
};

static const int levels[MAX_VALUES] = {
	NEMO_ARCHIVE_LEVEL_STORE, NEMO_ARCHIVE_LEVEL_DEFAULT, NEMO_ARCHIVE_LEVEL_MAX
};

typedef struct {
	int v[N_PARAMS];
} Row;

static gboolean job_finished;
static gboolean job_succeeded;
static gboolean with_links;
static GHashTable *contents;	/* archive path -> GBytes, what each file holds */

static void
archive_done (GFile *result, gboolean success, gpointer data)
{
	job_succeeded = success;
	job_finished = TRUE;
	gtk_main_quit ();
}

static gboolean
give_up (gpointer data)
{
	gtk_main_quit ();
	return G_SOURCE_REMOVE;
}

/* How many values each option takes for this format. One means it is not
   offered, and it stays off. */
static void
value_counts (NemoArchiveFormat format, int counts[N_PARAMS])
{
	NemoArchiveCaps caps = nemo_archive_format_caps (format);

	counts[P_LEVEL] = (caps & NEMO_ARCHIVE_CAP_LEVEL) ? 3 : 1;
	counts[P_PASSWORD] = (caps & NEMO_ARCHIVE_CAP_PASSWORD) ? 2 : 1;
	counts[P_NAMES] = (caps & NEMO_ARCHIVE_CAP_ENCRYPT_NAMES) ? 2 : 1;
	counts[P_SPLIT] = (caps & NEMO_ARCHIVE_CAP_SPLIT) ? 2 : 1;
	counts[P_SOLID] = (caps & NEMO_ARCHIVE_CAP_SOLID) ? 2 : 1;
	counts[P_DEDUPE] = (caps & NEMO_ARCHIVE_CAP_DEDUPE) ? 2 : 1;
	counts[P_LINKS] = (caps & NEMO_ARCHIVE_CAP_STORE_LINKS) ? 2 : 1;
	counts[P_RECOVERY] = (caps & NEMO_ARCHIVE_CAP_RECOVERY) ? 2 : 1;
	counts[P_LOCK] = (caps & NEMO_ARCHIVE_CAP_LOCK) ? 2 : 1;
	counts[P_FOLLOW] = with_links ? 2 : 1;
	counts[P_DELETE] = 2;
	counts[P_EACH] = 2;
}

/* Greedy all-pairs: each new row starts from a pair nothing has covered yet
   and fills the rest with whatever covers the most new pairs. Small and the
   same every run. */
static GArray *
pairwise_rows (const int counts[N_PARAMS])
{
	static gboolean covered[N_PARAMS][MAX_VALUES][N_PARAMS][MAX_VALUES];
	GArray *rows = g_array_new (FALSE, TRUE, sizeof (Row));
	int i, a, j, b;

	memset (covered, 0, sizeof covered);
	for (i = 0; i < N_PARAMS; i++) {
		for (a = counts[i]; a < MAX_VALUES; a++) {
			for (j = 0; j < N_PARAMS; j++) {
				for (b = 0; b < MAX_VALUES; b++) {
					covered[i][a][j][b] = covered[j][b][i][a] = TRUE;
				}
			}
		}
	}

	for (;;) {
		Row row;
		gboolean set[N_PARAMS] = { FALSE };
		gboolean found = FALSE;

		for (i = 0; i < N_PARAMS && !found; i++) {
			for (j = i + 1; j < N_PARAMS && !found; j++) {
				for (a = 0; a < counts[i] && !found; a++) {
					for (b = 0; b < counts[j] && !found; b++) {
						if (!covered[i][a][j][b]) {
							row.v[i] = a;
							row.v[j] = b;
							set[i] = set[j] = TRUE;
							found = TRUE;
						}
					}
				}
			}
		}
		if (!found) {
			break;
		}

		for (i = 0; i < N_PARAMS; i++) {
			int best = 0, best_gain = -1;

			if (set[i]) {
				continue;
			}
			for (a = 0; a < counts[i]; a++) {
				int gain = 0;

				for (j = 0; j < N_PARAMS; j++) {
					if (set[j] && !covered[i][a][j][row.v[j]]) {
						gain++;
					}
				}
				if (gain > best_gain) {
					best = a;
					best_gain = gain;
				}
			}
			row.v[i] = best;
			set[i] = TRUE;
		}

		for (i = 0; i < N_PARAMS; i++) {
			for (j = 0; j < N_PARAMS; j++) {
				covered[i][row.v[i]][j][row.v[j]] = TRUE;
			}
		}
		g_array_append_val (rows, row);
	}

	return rows;
}

static gboolean
has_row (GArray *rows, const Row *row)
{
	guint i;

	for (i = 0; i < rows->len; i++) {
		if (memcmp (&g_array_index (rows, Row, i), row, sizeof (Row)) == 0) {
			return TRUE;
		}
	}
	return FALSE;
}

/* The pairs, then every mix of the options that decide the delete, each
   filled out from a pair row in turn. */
static GArray *
plan_rows (const int counts[N_PARAMS])
{
	static const Param full[] = { P_DELETE, P_EACH, P_SPLIT, P_PASSWORD, P_NAMES };
	GArray *pairs = pairwise_rows (counts);
	GArray *rows = g_array_new (FALSE, TRUE, sizeof (Row));
	int cells = 1;
	int cell;
	guint k;

	for (k = 0; k < pairs->len; k++) {
		g_array_append_val (rows, g_array_index (pairs, Row, k));
	}

	for (k = 0; k < G_N_ELEMENTS (full); k++) {
		cells *= counts[full[k]];
	}
	for (cell = 0; cell < cells; cell++) {
		Row row = g_array_index (pairs, Row, cell % pairs->len);
		int rest = cell;

		for (k = 0; k < G_N_ELEMENTS (full); k++) {
			row.v[full[k]] = rest % counts[full[k]];
			rest /= counts[full[k]];
		}
		if (!has_row (rows, &row)) {
			g_array_append_val (rows, row);
		}
	}

	g_array_free (pairs, TRUE);
	return rows;
}

static void
options_from_row (NemoArchiveFormat format, const Row *row, NemoArchiveOptions *options)
{
	nemo_archive_options_init (options);
	options->format = format;
	options->level = levels[row->v[P_LEVEL]];
	options->password = row->v[P_PASSWORD] ? g_strdup (PASSWORD) : NULL;
	options->encrypt_names = row->v[P_NAMES];
	options->split_size = row->v[P_SPLIT] ? SPLIT_BYTES : 0;
	options->solid = row->v[P_SOLID];
	options->dedupe = row->v[P_DEDUPE];
	options->store_links = row->v[P_LINKS];
	options->recovery_record = row->v[P_RECOVERY];
	options->lock = row->v[P_LOCK];
	options->follow_link_dirs = row->v[P_FOLLOW];
	options->delete_sources = row->v[P_DELETE];
}

static void
write_bytes (const char *dir, const char *name, const char *bytes, gsize length)
{
	char *path = g_build_filename (dir, name, NULL);

	check (g_file_set_contents (path, bytes, (gssize) length, NULL));
	g_free (path);
}

/* Random so no format can squeeze it below a volume, and the same bytes every
   run. */
static char *
noise (guint32 seed, gsize length)
{
	GRand *rand = g_rand_new_with_seed (seed);
	char *bytes = g_malloc (length);
	gsize i;

	for (i = 0; i < length; i++) {
		bytes[i] = (char) g_rand_int_range (rand, 0, 256);
	}
	g_rand_free (rand);
	return bytes;
}

static const char * const item_names[] = {
	"a.txt", "sub", "twin1.bin", "twin2.bin", "big.bin", "file-link", "dir-link", NULL
};

static void
know (const char *name, const char *bytes, gsize length)
{
	g_hash_table_insert (contents, (gpointer) name, g_bytes_new (bytes, length));
}

/* The twins are over rar's 64 KiB floor for storing a copy once. */
static GList *
make_items (const char *items, const char *outside)
{
	char *sub = g_build_filename (items, "sub", NULL);
	char *twin = noise (7, 80 * 1024);
	char *big = noise (11, 200 * 1024);

	if (contents == NULL) {
		contents = g_hash_table_new_full (g_str_hash, g_str_equal, NULL,
						  (GDestroyNotify) g_bytes_unref);
		know ("a.txt", "alpha", 5);
		know ("sub/b.txt", "bravo", 5);
		know ("twin1.bin", twin, 80 * 1024);
		know ("twin2.bin", twin, 80 * 1024);
		know ("big.bin", big, 200 * 1024);
		know ("file-link", "target", 6);
		know ("dir-link/c.txt", "charlie", 7);
	}
	GList *sources = NULL;
	int i;

	g_mkdir_with_parents (sub, 0700);
	write_bytes (items, "a.txt", "alpha", 5);
	write_bytes (sub, "b.txt", "bravo", 5);
	write_bytes (items, "twin1.bin", twin, 80 * 1024);
	write_bytes (items, "twin2.bin", twin, 80 * 1024);
	write_bytes (items, "big.bin", big, 200 * 1024);

	if (with_links) {
		char *target = g_build_filename (outside, "target.txt", NULL);
		char *linked = g_build_filename (outside, "linked", NULL);
		char *file_link = g_build_filename (items, "file-link", NULL);
		char *dir_link = g_build_filename (items, "dir-link", NULL);

		check (nemo_link_create (target, file_link, NULL, NEMO_LINK_FILE_SYMLINK, NULL));
		check (nemo_link_create (linked, dir_link, NULL, NEMO_LINK_DIR_SYMLINK, NULL));
		g_free (dir_link);
		g_free (file_link);
		g_free (linked);
		g_free (target);
	}

	for (i = 0; item_names[i] != NULL; i++) {
		char *path = g_build_filename (items, item_names[i], NULL);

		if (g_file_test (path, G_FILE_TEST_EXISTS | G_FILE_TEST_IS_SYMLINK)) {
			sources = g_list_append (sources, g_file_new_for_path (path));
		}
		g_free (path);
	}

	g_free (big);
	g_free (twin);
	g_free (sub);
	return sources;
}

/* Every file and link a reader should find, and whether each is a link.
   Folders are left out, since some writers store them and some do not. */
static GHashTable *
expected_entries (gboolean links_stored, gboolean follow, const char *only)
{
	GHashTable *want = g_hash_table_new (g_str_hash, g_str_equal);

#define WANT(item, name, is_link) \
	if (only == NULL || g_strcmp0 (only, item) == 0) \
		g_hash_table_insert (want, (gpointer) (name), GINT_TO_POINTER (is_link))

	WANT ("a.txt", "a.txt", FALSE);
	WANT ("sub", "sub/b.txt", FALSE);
	WANT ("twin1.bin", "twin1.bin", FALSE);
	WANT ("twin2.bin", "twin2.bin", FALSE);
	WANT ("big.bin", "big.bin", FALSE);
	if (with_links) {
		if (links_stored) {
			WANT ("file-link", "file-link", TRUE);
			WANT ("dir-link", "dir-link", TRUE);
		} else {
			WANT ("file-link", "file-link", FALSE);
			if (follow) {
				WANT ("dir-link", "dir-link/c.txt", FALSE);
			}
		}
	}
#undef WANT

	return want;
}

typedef enum {
	READ_OK,
	READ_NO_NAMES,		/* not even the list could be read */
	READ_NO_DATA		/* the list, but not what is in the files */
} ReadResult;

/* Reads the archive back with no password. Entries that are not folders go
   into found, as link or not. */
static ReadResult
read_back (const char *path, GHashTable *found)
{
	struct archive *a = archive_read_new ();
	struct archive_entry *entry;
	ReadResult result = READ_OK;
	gboolean any = FALSE;
	int status;

	archive_read_support_format_all (a);
	archive_read_support_filter_all (a);

	if (archive_read_open_filename (a, path, 16384) != ARCHIVE_OK) {
		archive_read_free (a);
		return READ_NO_NAMES;
	}

	/* libarchive 3.8 cannot decrypt a 7z, and answers FAILED for the header
	   after an entry whose data it could not read. The rest is still there
	   to list, so only FATAL ends the walk early. */
	while ((status = archive_read_next_header (a, &entry)) != ARCHIVE_EOF &&
	       status != ARCHIVE_FATAL) {
		char *name;
		gsize length;
		char buffer[8192];
		la_ssize_t got;

		any = TRUE;
		if (archive_entry_filetype (entry) == AE_IFDIR) {
			continue;
		}

		name = g_strdup (archive_entry_pathname (entry));
		length = strlen (name);
		if (length > 0 && name[length - 1] == '/') {
			name[length - 1] = '\0';
		}
		g_hash_table_insert (found, name,
				     GINT_TO_POINTER (archive_entry_filetype (entry) == AE_IFLNK));

		/* A stored entry under a password comes back from some readers
		   as its scrambled bytes with no error, so every file is held to
		   what it really holds. */
		if (archive_entry_filetype (entry) == AE_IFREG && archive_entry_size (entry) > 0 &&
		    archive_entry_hardlink (entry) == NULL) {
			GBytes *known = g_hash_table_lookup (contents, name);
			GByteArray *read = g_byte_array_new ();

			while ((got = archive_read_data (a, buffer, sizeof buffer)) > 0) {
				g_byte_array_append (read, (const guint8 *) buffer, (guint) got);
			}
			if (got < 0 || known == NULL ||
			    g_bytes_get_size (known) != read->len ||
			    memcmp (g_bytes_get_data (known, NULL), read->data, read->len) != 0) {
				result = READ_NO_DATA;
			}
			g_byte_array_unref (read);
		}
	}

	if (status != ARCHIVE_EOF && !any) {
		result = READ_NO_NAMES;
	}

	archive_read_free (a);
	return result;
}

/* Whether anything wanted is a file with something in it, which a password
   would lock. A link has nothing to lock. */
static gboolean
holds_data (GHashTable *want)
{
	GHashTableIter iter;
	gpointer key, value;

	g_hash_table_iter_init (&iter, want);
	while (g_hash_table_iter_next (&iter, &key, &value)) {
		if (!GPOINTER_TO_INT (value)) {
			return TRUE;
		}
	}
	return FALSE;
}

static gboolean
same_entries (GHashTable *want, GHashTable *found)
{
	GHashTableIter iter;
	gpointer key, value;

	if (g_hash_table_size (want) != g_hash_table_size (found)) {
		return FALSE;
	}
	g_hash_table_iter_init (&iter, want);
	while (g_hash_table_iter_next (&iter, &key, &value)) {
		gpointer seen;

		if (!g_hash_table_lookup_extended (found, key, NULL, &seen) || seen != value) {
			return FALSE;
		}
	}
	return TRUE;
}

static void
dump_entries (const char *label, GHashTable *entries)
{
	GHashTableIter iter;
	gpointer key, value;

	g_printerr ("  %s:", label);
	g_hash_table_iter_init (&iter, entries);
	while (g_hash_table_iter_next (&iter, &key, &value)) {
		g_printerr (" %s%s", (char *) key, GPOINTER_TO_INT (value) ? "@" : "");
	}
	g_printerr ("\n");
}

static int
count_files (const char *dir)
{
	GDir *listing = g_dir_open (dir, 0, NULL);
	int count = 0;

	if (listing == NULL) {
		return 0;
	}
	while (g_dir_read_name (listing) != NULL) {
		count++;
	}
	g_dir_close (listing);
	return count;
}

static int questions_answered;

/* A job waiting on a question has it up modal, in a loop of its own, so this
   runs from a timeout. The only one expected is the trash asking to go
   ahead, answered yes with the last button. */
static gboolean
answer_questions (gpointer data)
{
	GList *windows = gtk_window_list_toplevels ();
	GList *l;

	for (l = windows; l != NULL; l = l->next) {
		GtkWidget *area;
		GList *buttons;
		char *text = NULL;

		if (!GTK_IS_MESSAGE_DIALOG (l->data) || !gtk_widget_get_mapped (l->data) ||
		    !gtk_window_get_modal (GTK_WINDOW (l->data))) {
			continue;
		}

		g_object_get (l->data, "text", &text, NULL);
		if (text == NULL || !g_str_has_prefix (text, "Are you sure you want to move")) {
			g_printerr ("FAIL: not expecting \"%s\"\n", text != NULL ? text : "");
			failures++;
		}
		g_free (text);

		area = gtk_dialog_get_action_area (GTK_DIALOG (l->data));
		buttons = gtk_container_get_children (GTK_CONTAINER (area));
		if (buttons != NULL) {
			gtk_button_clicked (GTK_BUTTON (g_list_last (buttons)->data));
		}
		g_list_free (buttons);
		questions_answered++;
	}
	g_list_free (windows);

	return G_SOURCE_CONTINUE;
}

/* Runs the main loop until every job has said it finished, so a delete
   started from the archive job is over before anything is looked at.
   Answers how many times the trash asked. */
static int
wait_for_jobs (NemoProgressInfoManager *manager)
{
	gint64 give_up_at = g_get_monotonic_time () + JOB_TIMEOUT_SECONDS * G_USEC_PER_SEC;
	guint answer_id = g_timeout_add (20, answer_questions, NULL);

	questions_answered = 0;
	while (nemo_progress_info_manager_get_all_infos (manager) != NULL) {
		if (g_get_monotonic_time () > give_up_at) {
			g_printerr ("FAIL: a job did not finish within %d seconds\n",
				    JOB_TIMEOUT_SECONDS);
			failures++;
			break;
		}
		if (!g_main_context_iteration (NULL, FALSE)) {
			g_usleep (2000);
		}
	}
	g_source_remove (answer_id);

	return questions_answered;
}

/* The message dialogs the job left up, closed as they are read. What they
   said goes in said, for when a check fails. */
static void
take_dialogs (int *warnings, int *errors, GString *said)
{
	GList *windows = gtk_window_list_toplevels ();
	GList *l;

	*warnings = 0;
	*errors = 0;
	for (l = windows; l != NULL; l = l->next) {
		GtkMessageType type;

		if (!GTK_IS_MESSAGE_DIALOG (l->data)) {
			continue;
		}
		{
			char *text = NULL, *secondary = NULL;

			g_object_get (l->data, "message-type", &type, "text", &text,
				      "secondary-text", &secondary, NULL);
			g_string_append_printf (said, "  said: %s / %s\n", text, secondary);
			g_free (secondary);
			g_free (text);
		}
		if (type == GTK_MESSAGE_WARNING) {
			(*warnings)++;
		} else if (type == GTK_MESSAGE_ERROR) {
			(*errors)++;
		}
		gtk_widget_destroy (GTK_WIDGET (l->data));
	}
	g_list_free (windows);
}

static gboolean
exists (const char *dir, const char *name)
{
	char *path = g_build_filename (dir, name, NULL);
	gboolean there = g_file_test (path, G_FILE_TEST_EXISTS | G_FILE_TEST_IS_SYMLINK);

	g_free (path);
	return there;
}

static void
describe (NemoArchiveFormat format, const Row *row)
{
	int i;

	g_printerr ("  with %s:", nemo_archive_format_id (format));
	for (i = 0; i < N_PARAMS; i++) {
		g_printerr (" %s=%d", param_names[i], row->v[i]);
	}
	g_printerr ("\n");
}

static void
run_row (const char *tmp, const char *outside, int number,
	 NemoArchiveFormat format, const Row *row, GtkWidget *window,
	 NemoProgressInfoManager *manager)
{
	char *name = g_strdup_printf ("case-%03d", number);
	char *root = g_build_filename (tmp, name, NULL);
	char *items = g_build_filename (root, "items", NULL);
	char *out = g_build_filename (root, "out", NULL);
	NemoArchiveOptions options;
	NemoArchiveBackend backend;
	gboolean each = row->v[P_EACH];
	gboolean stored, verifiable, walk_whole, hidden_names, has_password;
	GList *sources, *l;
	int before = failures;
	int warnings, errors, asked;
	GString *said = g_string_new (NULL);
	guint timeout_id;

	g_mkdir_with_parents (items, 0700);
	g_mkdir_with_parents (out, 0700);
	sources = make_items (items, outside);
	options_from_row (format, row, &options);

	backend = nemo_archive_pick_backend (format, &options);
	check (backend != NEMO_ARCHIVE_BACKEND_NONE);

	/* What should come out, worked out the way the dialog's promise reads:
	   links kept as links where the writer can, a linked folder left out
	   unless followed, and a delete only where the archive checks out. */
	stored = options.store_links &&
		 (nemo_archive_backend_caps (format, backend) & NEMO_ARCHIVE_CAP_STORE_LINKS) != 0;
	walk_whole = !with_links || stored || options.follow_link_dirs;
	has_password = options.password != NULL;
	hidden_names = has_password && options.encrypt_names;
	verifiable = nemo_archive_can_verify (&options);

	job_finished = FALSE;
	job_succeeded = FALSE;
	if (each) {
		GFile *dest = g_file_new_for_path (out);

		nemo_archive_create_each (sources, dest, &options, GTK_WINDOW (window),
					  archive_done, NULL);
		g_object_unref (dest);
	} else {
		char *base = g_strconcat ("all", nemo_archive_format_extension (format), NULL);
		char *path = g_build_filename (out, base, NULL);
		GFile *dest = g_file_new_for_path (path);

		nemo_archive_create (sources, dest, &options, GTK_WINDOW (window),
				     archive_done, NULL);
		g_object_unref (dest);
		g_free (path);
		g_free (base);
	}
	timeout_id = g_timeout_add_seconds (JOB_TIMEOUT_SECONDS, give_up, NULL);
	gtk_main ();
	if (job_finished) {
		g_source_remove (timeout_id);
	} else {
		g_printerr ("FAIL: compressing did not finish within %d seconds\n",
			    JOB_TIMEOUT_SECONDS);
		failures++;
	}
	asked = wait_for_jobs (manager);
	take_dialogs (&warnings, &errors, said);

	check (job_succeeded);
	check (errors == 0);

	/* Read back what can be read without the tools that wrote it. */
	if (!options.split_size) {
		for (l = sources; l != NULL; l = l->next) {
			char *item = g_file_get_basename (G_FILE (l->data));
			char *archive_name;
			char *archive_path;
			GHashTable *want, *found;
			ReadResult result;

			if (each) {
				archive_name = nemo_archive_each_name (item, format);
			} else if (l == sources) {
				archive_name = g_strconcat ("all", nemo_archive_format_extension (format), NULL);
			} else {
				g_free (item);
				break;
			}
			archive_path = g_build_filename (out, archive_name, NULL);

			want = expected_entries (stored, options.follow_link_dirs, each ? item : NULL);
			found = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);
			result = read_back (archive_path, found);

			if (g_hash_table_size (want) == 0) {
				/* A linked folder alone, not followed: nothing went in, and
				   the archive may or may not have been written. */
			} else if (hidden_names) {
				check (result == READ_NO_NAMES);
			} else {
				check (same_entries (want, found));
				check (result == (has_password && holds_data (want) ? READ_NO_DATA : READ_OK));
				if (!same_entries (want, found)) {
					g_printerr ("  in %s\n", archive_name);
					dump_entries ("wanted", want);
					dump_entries ("found", found);
				}
			}

			g_hash_table_destroy (found);
			g_hash_table_destroy (want);
			g_free (archive_path);
			g_free (archive_name);
			g_free (item);
		}
		if (!each && walk_whole) {
			check (count_files (out) == 1);
		}
	} else {
		/* Nothing squeezes the noise below a volume, so its archive has
		   more than one piece, and every item has its own at least. */
		check (count_files (out) > (each ? (int) g_list_length (sources) - 1 : 1));
	}

	/* The originals go only where every one of them checked out, and the
	   warning says so where they stay. */
	if (!options.delete_sources) {
		check (exists (items, "a.txt"));
		check (warnings == 0);
		check (asked == 0);
	} else if (!verifiable) {
		check (exists (items, "a.txt") && exists (items, "sub"));
		check (warnings == 1);
		check (asked == 0);
	} else if (each) {
		check (asked == 1);
		check (!exists (items, "a.txt") && !exists (items, "sub") && !exists (items, "big.bin"));
		if (with_links) {
			check (!exists (items, "file-link"));
			check (exists (items, "dir-link") == !walk_whole);
		}
		check (warnings == (walk_whole ? 0 : 1));
	} else {
		check (exists (items, "a.txt") == !walk_whole);
		check (exists (items, "sub") == !walk_whole);
		check (warnings == (walk_whole ? 0 : 1));
		check (asked == (walk_whole ? 1 : 0));
	}

	/* A delete takes the links, never what they point at. */
	{
		char *linked = g_build_filename (outside, "linked", NULL);

		check (exists (outside, "target.txt"));
		check (exists (linked, "c.txt"));
		g_free (linked);
	}

	if (failures > before) {
		describe (format, row);
		g_printerr ("%s", said->str);
	}
	g_string_free (said, TRUE);

	nemo_archive_options_clear (&options);
	g_list_free_full (sources, g_object_unref);
	g_free (out);
	g_free (items);
	g_free (root);
	g_free (name);
}

int
main (int argc, char *argv[])
{
	NemoProgressInfoManager *manager;
	GtkWidget *window;
	char *home, *data_home, *tmp, *outside, *linked;
	int format;
	int number = 0;

	home = test_scratch_config_home ("nemo-archive-combos-home-XXXXXX");
	/* The trash the delete uses. */
	data_home = g_build_filename (home, "data", NULL);
	g_setenv ("XDG_DATA_HOME", data_home, TRUE);
	nemo_global_preferences_init ();
	test_init (&argc, &argv);

	/* See test-nemo-link-copy-job.c: without a manager the queue never
	   starts a second job. */
	manager = nemo_progress_info_manager_new ();
	g_setenv ("NEMO_TESTGUARD_ALL_DELETES", "0", TRUE);

	/* Beside home, so the trash is on the same file system. */
	tmp = test_scratch_dir_in (home, "nemo-archive-combos-XXXXXX", NULL);
	with_links = (nemo_link_kinds_supported (tmp) & NEMO_LINK_FILE_SYMLINK) &&
		     (nemo_link_kinds_supported (tmp) & NEMO_LINK_DIR_SYMLINK);
	if (!with_links) {
		g_printerr ("note: no symlinks here, so no links in the selection\n");
	}

	outside = g_build_filename (tmp, "outside", NULL);
	linked = g_build_filename (outside, "linked", NULL);
	g_mkdir_with_parents (linked, 0700);
	write_bytes (outside, "target.txt", "target", 6);
	write_bytes (linked, "c.txt", "charlie", 7);

	window = test_window_new ("archive combinations test", 5);
	gtk_widget_show (window);

	for (format = 0; format < NEMO_ARCHIVE_N_FORMATS; format++) {
		int counts[N_PARAMS];
		GArray *rows;
		guint k;

		if (!nemo_archive_format_available (format)) {
			g_printerr ("note: nothing here writes %s, skipped\n",
				    nemo_archive_format_id (format));
			continue;
		}

		value_counts (format, counts);
		rows = plan_rows (counts);
		for (k = 0; k < rows->len; k++) {
			run_row (tmp, outside, number++, format,
				 &g_array_index (rows, Row, k), window, manager);
		}
		g_print ("%s: %u combinations\n", nemo_archive_format_id (format), rows->len);
		g_array_free (rows, TRUE);
	}

	g_object_unref (manager);
	g_clear_pointer (&contents, g_hash_table_destroy);
	g_free (linked);
	g_free (outside);
	g_free (tmp);
	g_free (data_home);
	g_free (home);

	if (failures > 0) {
		return EXIT_FAILURE;
	}

	g_print ("archive combinations: %d runs, all checks passed\n", number);
	return EXIT_SUCCESS;
}
