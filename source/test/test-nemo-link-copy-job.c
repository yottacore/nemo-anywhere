/* Every answer the link copy dialog can give, through the real copy job. The
 * dialog is answered up front through NEMO_LINK_COPY, since there is nobody
 * here to click it. Each run copies or moves the same set: a file symlink, a
 * folder symlink, a junction where there are junctions, a shortcut, and a
 * plain folder with a file symlink inside it.
 *
 * Checked every time: a row's answer reaches both the link that was picked
 * and the one inside the plain folder; a folder whose contents are copied
 * keeps the links inside it as links; a shortcut is only ever the shortcut
 * file; and a shortcut on its own brings up no question at all. A move stays
 * on one drive here, so it is a rename and every link keeps its kind.
 */

#include "test.h"

#include <libnemo-private/nemo-global-preferences.h>
#include <libnemo-private/nemo-file-operations.h>
#include <libnemo-private/nemo-link-copy.h>
#include <libnemo-private/nemo-lnk.h>
#include <libnemo-private/nemo-progress-info-manager.h>

#include <glib/gstdio.h>
#include <stdlib.h>
#include <string.h>

#include "test-scratch.h"
#include "test-check.h"

#define COPY_TIMEOUT_SECONDS 20

static gboolean copy_finished;
static gboolean copy_succeeded;

static void
copy_done (GHashTable *debuting_uris,
           gboolean success,
           gpointer data)
{
	copy_succeeded = success;
	copy_finished = TRUE;
	gtk_main_quit ();
}

static gboolean
give_up (gpointer data)
{
	g_printerr ("FAIL: copy did not finish within %d seconds\n",
		    COPY_TIMEOUT_SECONDS);
	failures++;
	gtk_main_quit ();
	return G_SOURCE_REMOVE;
}

static NemoLinkKind
kind_at (const char *dir, const char *name)
{
	char *path = g_build_filename (dir, name, NULL);
	GFile *file = g_file_new_for_path (path);
	NemoLinkKind kind = nemo_link_kind (file, NULL);

	g_object_unref (file);
	g_free (path);
	return kind;
}

static gboolean
holds (const char *dir, const char *name, const char *want)
{
	char *path = g_build_filename (dir, name, NULL);
	char *contents = NULL;
	gboolean same;

	same = g_file_get_contents (path, &contents, NULL, NULL) &&
	       g_strcmp0 (contents, want) == 0;
	g_free (contents);
	g_free (path);
	return same;
}

static gboolean
is_real_dir (const char *dir, const char *name)
{
	char *path = g_build_filename (dir, name, NULL);
	gboolean real = kind_at (dir, name) == NEMO_LINK_NONE &&
			g_file_test (path, G_FILE_TEST_IS_DIR);

	g_free (path);
	return real;
}

static void
write_file (const char *dir, const char *name, const char *contents)
{
	char *path = g_build_filename (dir, name, NULL);

	check (g_file_set_contents (path, contents, -1, NULL));
	g_free (path);
}

static void
make_link (const char *target, const char *dir, const char *name, NemoLinkKind kind)
{
	char *path = g_build_filename (dir, name, NULL);

	check (nemo_link_create (target, path, NULL, kind, NULL));
	g_free (path);
}

/* The targets live beside "from", not in it, so a move takes only links and
   every link stays good wherever it ends up: all of them are absolute. */
static void
make_targets (const char *root, gboolean with_junctions)
{
	char *away = g_build_filename (root, "away", NULL);
	char *folder = g_build_filename (away, "folder", NULL);
	char *payload = g_build_filename (away, "payload.txt", NULL);
	char *inner = g_build_filename (folder, "inner.txt", NULL);

	g_mkdir_with_parents (folder, 0700);
	write_file (away, "payload.txt", "payload");
	write_file (folder, "inner.txt", "inner");
	make_link (inner, folder, "inner-link", NEMO_LINK_FILE_SYMLINK);
	if (with_junctions) {
		make_link (away, folder, "inner-junction", NEMO_LINK_JUNCTION);
	}

	g_free (inner);
	g_free (payload);
	g_free (folder);
	g_free (away);
}

static GList *
make_source (const char *root, const char *from, guint supported)
{
	static const char * const picked[] = {
		"pointer", "dir-pointer", "junction", "shortcut.lnk", "plain", NULL
	};
	char *away = g_build_filename (root, "away", NULL);
	char *folder = g_build_filename (away, "folder", NULL);
	char *payload = g_build_filename (away, "payload.txt", NULL);
	char *plain = g_build_filename (from, "plain", NULL);
	char *lnk = g_build_filename (from, "shortcut.lnk", NULL);
	GList *sources = NULL;
	int i;

	g_mkdir_with_parents (plain, 0700);
	make_link (payload, from, "pointer", NEMO_LINK_FILE_SYMLINK);
	make_link (payload, plain, "deep", NEMO_LINK_FILE_SYMLINK);
	if (supported & NEMO_LINK_DIR_SYMLINK) {
		make_link (folder, from, "dir-pointer", NEMO_LINK_DIR_SYMLINK);
	}
	if (supported & NEMO_LINK_JUNCTION) {
		make_link (folder, from, "junction", NEMO_LINK_JUNCTION);
	}
	check (nemo_lnk_write (lnk, payload, TRUE, NULL));

	for (i = 0; picked[i] != NULL; i++) {
		char *path = g_build_filename (from, picked[i], NULL);

		if (g_file_test (path, G_FILE_TEST_EXISTS | G_FILE_TEST_IS_SYMLINK)) {
			sources = g_list_append (sources, g_file_new_for_path (path));
		}
		g_free (path);
	}

	g_free (lnk);
	g_free (plain);
	g_free (payload);
	g_free (folder);
	g_free (away);
	return sources;
}

static gboolean
run_job (GList *sources, const char *to, gboolean is_move, GtkWidget *window)
{
	GFile *dest = g_file_new_for_path (to);
	guint timeout_id;

	copy_finished = FALSE;
	copy_succeeded = FALSE;
	if (is_move) {
		nemo_file_operations_move (sources, NULL, dest, GTK_WINDOW (window),
					   copy_done, NULL);
	} else {
		nemo_file_operations_copy (sources, NULL, dest, GTK_WINDOW (window),
					   copy_done, NULL);
	}

	timeout_id = g_timeout_add_seconds (COPY_TIMEOUT_SECONDS, give_up, NULL);
	gtk_main ();
	if (copy_finished) {
		g_source_remove (timeout_id);
	}
	g_object_unref (dest);

	return copy_finished && copy_succeeded;
}

static gboolean
gone (const char *dir, const char *name)
{
	char *path = g_build_filename (dir, name, NULL);
	gboolean missing = !g_file_test (path, G_FILE_TEST_EXISTS | G_FILE_TEST_IS_SYMLINK);

	g_free (path);
	return missing;
}

/* A row answered NONE copies what the link points at. Either way the payload
   reads back through it. */
static void
check_file_row (const char *dir, const char *name, NemoLinkKind as)
{
	check (kind_at (dir, name) == as);
	check (holds (dir, name, "payload"));
}

static void
check_dir_row (const char *dir, const char *name, NemoLinkKind as, gboolean with_junctions)
{
	char *inside;

	if (as != NEMO_LINK_NONE) {
		check (kind_at (dir, name) == as);
		return;
	}

	/* The contents, and the links inside kept as links, whatever the file
	   row said. */
	check (is_real_dir (dir, name));
	inside = g_build_filename (dir, name, NULL);
	check (holds (inside, "inner.txt", "inner"));
	check (kind_at (inside, "inner-link") == NEMO_LINK_FILE_SYMLINK);
	if (with_junctions) {
		check (kind_at (inside, "inner-junction") == NEMO_LINK_JUNCTION);
	}
	g_free (inside);
}

static const char *
answer_word (NemoLinkKind found, NemoLinkKind as)
{
	if (as == NEMO_LINK_NONE) {
		return "copy";
	}
	if (as == found) {
		return "keep";
	}
	return as == NEMO_LINK_JUNCTION ? "junction" : "symlink";
}

/* One combination: copy or move, and what each row was answered. */
static void
run_combination (const char *tmp, int number, guint supported, gboolean is_move,
		 NemoLinkKind file_as, NemoLinkKind dir_as, NemoLinkKind junction_as,
		 GtkWidget *window)
{
	char *name = g_strdup_printf ("case-%02d", number);
	char *root = g_build_filename (tmp, name, NULL);
	char *from = g_build_filename (root, "from", NULL);
	char *to = g_build_filename (root, "to", NULL);
	char *to_plain = g_build_filename (to, "plain", NULL);
	char *answer;
	gboolean with_junctions = (supported & NEMO_LINK_JUNCTION) != 0;
	GList *sources;
	int before = failures;

	answer = g_strdup_printf ("file=%s,dir=%s,junction=%s",
				  answer_word (NEMO_LINK_FILE_SYMLINK, file_as),
				  answer_word (NEMO_LINK_DIR_SYMLINK, dir_as),
				  answer_word (NEMO_LINK_JUNCTION, junction_as));

	g_mkdir_with_parents (from, 0700);
	g_mkdir_with_parents (to, 0700);
	make_targets (root, with_junctions);
	sources = make_source (root, from, supported);

	g_setenv ("NEMO_LINK_COPY", answer, TRUE);
	check (run_job (sources, to, is_move, window));

	/* A move on one drive is a rename, so every link arrives as it was,
	   whatever the rows said. Only a move to another drive asks. */
	if (is_move) {
		file_as = NEMO_LINK_FILE_SYMLINK;
		dir_as = NEMO_LINK_DIR_SYMLINK;
		junction_as = NEMO_LINK_JUNCTION;
	}

	check_file_row (to, "pointer", file_as);
	check_file_row (to_plain, "deep", file_as);
	if (supported & NEMO_LINK_DIR_SYMLINK) {
		check_dir_row (to, "dir-pointer", dir_as, with_junctions);
	}
	if (with_junctions) {
		check_dir_row (to, "junction", junction_as, with_junctions);
	}

	/* The shortcut is the same file on the far side, never its target. */
	check (kind_at (to, "shortcut.lnk") == NEMO_LINK_NONE);
	{
		char *copied = g_build_filename (to, "shortcut.lnk", NULL);
		NemoLnk lnk;

		check (nemo_lnk_read (copied, &lnk));
		nemo_lnk_clear (&lnk);
		g_free (copied);
	}

	if (is_move) {
		check (gone (from, "pointer"));
		check (gone (from, "dir-pointer"));
		check (gone (from, "junction"));
		check (gone (from, "shortcut.lnk"));
		check (gone (from, "plain"));
	} else {
		check (kind_at (from, "pointer") == NEMO_LINK_FILE_SYMLINK);
		if (supported & NEMO_LINK_DIR_SYMLINK) {
			check (kind_at (from, "dir-pointer") == NEMO_LINK_DIR_SYMLINK);
		}
	}

	/* What was pointed at is never touched. */
	{
		char *away = g_build_filename (root, "away", NULL);

		check (holds (away, "payload.txt", "payload"));
		g_free (away);
	}

	if (failures > before) {
		g_printerr ("  in %s: %s %s\n", name, is_move ? "move" : "copy", answer);
	}

	g_list_free_full (sources, g_object_unref);
	g_free (answer);
	g_free (to_plain);
	g_free (to);
	g_free (from);
	g_free (root);
	g_free (name);
}

/* A shortcut is an ordinary file to a copy, so it alone asks nothing. With
   no answer given, a question would sit there until the timeout. */
static void
check_shortcut_alone (const char *tmp, GtkWidget *window)
{
	char *root = g_build_filename (tmp, "shortcut-alone", NULL);
	char *from = g_build_filename (root, "from", NULL);
	char *to = g_build_filename (root, "to", NULL);
	char *target = g_build_filename (root, "target.txt", NULL);
	char *lnk = g_build_filename (from, "alone.lnk", NULL);
	char *copied = g_build_filename (to, "alone.lnk", NULL);
	char *before = NULL, *after = NULL;
	gsize before_len = 0, after_len = 0;
	GList *sources;

	g_mkdir_with_parents (from, 0700);
	g_mkdir_with_parents (to, 0700);
	write_file (root, "target.txt", "target");
	check (nemo_lnk_write (lnk, target, TRUE, NULL));

	g_unsetenv ("NEMO_LINK_COPY");
	sources = g_list_append (NULL, g_file_new_for_path (lnk));
	check (run_job (sources, to, FALSE, window));

	check (g_file_get_contents (lnk, &before, &before_len, NULL));
	check (g_file_get_contents (copied, &after, &after_len, NULL));
	check (before_len == after_len && before != NULL && after != NULL &&
	       memcmp (before, after, before_len) == 0);

	g_list_free_full (sources, g_object_unref);
	g_free (after);
	g_free (before);
	g_free (copied);
	g_free (lnk);
	g_free (target);
	g_free (to);
	g_free (from);
	g_free (root);
}

int
main (int argc, char *argv[])
{
	/* What each row can be answered. NONE is a copy of the contents, which a
	   move never offers. */
	static const NemoLinkKind file_answers[] = { NEMO_LINK_FILE_SYMLINK, NEMO_LINK_NONE };
	static const NemoLinkKind dir_answers[] = {
		NEMO_LINK_DIR_SYMLINK, NEMO_LINK_JUNCTION, NEMO_LINK_NONE
	};
	NemoProgressInfoManager *manager;
	GtkWidget *window;
	char *home, *tmp;
	guint supported;
	int move, f, d, j;
	int number = 0;

	home = test_scratch_config_home ("nemo-link-job-home-XXXXXX");
	nemo_global_preferences_init ();
	test_init (&argc, &argv);
	g_free (home);

	/* The queue starts the next job when the last one's progress says it
	   finished, and says so from an idle. The app's manager keeps each
	   progress alive until then; with nobody holding one, it is freed first
	   and the next job never starts. */
	manager = nemo_progress_info_manager_new ();

	/* A move checks with the delete test guard, which would ask. */
	g_setenv ("NEMO_TESTGUARD_ALL_DELETES", "0", TRUE);

	tmp = test_scratch_dir ("nemo-link-job-XXXXXX", NULL);
	supported = nemo_link_kinds_supported (tmp);

	if (!(supported & NEMO_LINK_FILE_SYMLINK)) {
		g_printerr ("note: file symlinks are not permitted here, nothing to check\n");
		return 77;
	}

	window = test_window_new ("link copy test", 5);
	gtk_widget_show (window);

	check_shortcut_alone (tmp, window);

	for (move = 0; move <= 1; move++) {
		for (f = 0; f < (int) G_N_ELEMENTS (file_answers); f++) {
			for (d = 0; d < (int) G_N_ELEMENTS (dir_answers); d++) {
				for (j = 0; j < (int) G_N_ELEMENTS (dir_answers); j++) {
					NemoLinkKind file_as = file_answers[f];
					NemoLinkKind dir_as = dir_answers[d];
					NemoLinkKind junction_as = dir_answers[j];

					/* A move takes a link as a link. */
					if (move && (file_as == NEMO_LINK_NONE ||
						     dir_as == NEMO_LINK_NONE ||
						     junction_as == NEMO_LINK_NONE)) {
						continue;
					}
					/* Nothing to answer with where the kind cannot
					   be made, and no junction row without one. */
					if ((dir_as != NEMO_LINK_NONE && !(supported & dir_as)) ||
					    (junction_as != NEMO_LINK_NONE && !(supported & junction_as))) {
						continue;
					}
					if (!(supported & NEMO_LINK_JUNCTION) && j > 0) {
						continue;
					}

					run_combination (tmp, number++, supported, move,
							 file_as, dir_as, junction_as, window);
				}
			}
		}
	}

	g_free (tmp);
	g_object_unref (manager);

	if (failures > 0) {
		return EXIT_FAILURE;
	}

	g_print ("link copy job: %d combinations, all checks passed\n", number);
	return EXIT_SUCCESS;
}
