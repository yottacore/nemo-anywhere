/* A move or copy onto a name that's already there, through the real job, with
 * each answer the conflict dialog can give. The dialog is answered from here
 * by a script per case, and any question the script didn't expect fails the
 * case and is canceled.
 *
 * A move to another file system is a rename that fails and is then redone as
 * a copy and delete. That copy has to go to the name the dialog settled on: a
 * typed name, a Keep both name, or the folder a merge goes into. Replace was
 * answered for that one file, so a copy that works the name out again from the
 * source overwrites a file nobody was asked about.
 *
 * Every case runs across 2 file systems, where there are 2, and on one, as a
 * move and as a copy. A merge falls back to the copy even on one file system.
 *
 * With "guard" as its argument, the delete test guard is on and only Replace
 * runs, as a move. The rename and the copy it falls back to each asked the
 * guard about the overwrite (2026101512522482), and once is right.
 */

#include "test.h"

#include <libnemo-private/nemo-file-conflict-dialog.h>
#include <libnemo-private/nemo-file-operations.h>
#include <libnemo-private/nemo-global-preferences.h>
#include <libnemo-private/nemo-link-copy.h>
#include <libnemo-private/nemo-progress-info-manager.h>
#ifdef G_OS_WIN32
#include <windows.h>
#endif

#include <glib/gstdio.h>
#include <stdlib.h>
#include <string.h>

#include "test-scratch.h"
#include "test-check.h"

#define JOB_TIMEOUT_SECONDS 30

typedef struct {
	const char *asked;	/* the name the dialog is about */
	int response;
	const char *new_name;	/* typed, for Rename */
	gboolean all;
} Answer;

#define END { NULL, 0, NULL, FALSE }

static const Answer *script;
static int answered;
static const char *case_name;
static gboolean job_finished;
static gboolean guard_mode;
static int overwrite_asks;

static void
job_done (G_GNUC_UNUSED GHashTable *debuting_uris,
	  G_GNUC_UNUSED gboolean success,
	  G_GNUC_UNUSED gpointer data)
{
	job_finished = TRUE;
	gtk_main_quit ();
}

static gboolean
give_up (G_GNUC_UNUSED gpointer data)
{
	g_printerr ("FAIL %s: the job did not finish within %d seconds\n",
		    case_name, JOB_TIMEOUT_SECONDS);
	failures++;
	gtk_main_quit ();
	return G_SOURCE_REMOVE;
}

static void
find_widgets (GtkWidget *widget, GList **found)
{
	*found = g_list_prepend (*found, widget);
	if (GTK_IS_CONTAINER (widget)) {
		GList *children = gtk_container_get_children (GTK_CONTAINER (widget));

		for (GList *l = children; l != NULL; l = l->next) {
			find_widgets (l->data, found);
		}
		g_list_free (children);
	}
}

static void
set_entry_and_toggle (GtkWidget *dialog, const char *new_name, gboolean all)
{
	GList *found = NULL;

	find_widgets (dialog, &found);
	for (GList *l = found; l != NULL; l = l->next) {
		if (new_name != NULL && GTK_IS_ENTRY (l->data)) {
			gtk_entry_set_text (GTK_ENTRY (l->data), new_name);
		}
		if (all && GTK_IS_CHECK_BUTTON (l->data)) {
			gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (l->data), TRUE);
		}
	}
	g_list_free (found);
}

static void
answer_conflict (GtkWidget *dialog)
{
	g_autofree char *asked = nemo_file_conflict_dialog_get_new_name (NEMO_FILE_CONFLICT_DIALOG (dialog));
	const Answer *next;

	/* Empty until the dialog has read both files. */
	if (asked == NULL || *asked == '\0') {
		return;
	}
	g_object_set_data (G_OBJECT (dialog), "answered", GINT_TO_POINTER (1));

	next = &script[answered];
	if (next->asked == NULL) {
		g_printerr ("FAIL %s: asked about \"%s\" again, after the script ran out\n",
			    case_name, asked);
		failures++;
		gtk_dialog_response (GTK_DIALOG (dialog), GTK_RESPONSE_CANCEL);
		return;
	}
	answered++;

	if (strcmp (asked, next->asked) != 0) {
		g_printerr ("FAIL %s: asked about \"%s\", expected \"%s\"\n",
			    case_name, asked, next->asked);
		failures++;
		gtk_dialog_response (GTK_DIALOG (dialog), GTK_RESPONSE_CANCEL);
		return;
	}

	set_entry_and_toggle (dialog, next->new_name, next->all);
	gtk_dialog_response (GTK_DIALOG (dialog), next->response);
}

static gboolean
answer_dialogs (G_GNUC_UNUSED gpointer data)
{
	GList *windows = gtk_window_list_toplevels ();

	for (GList *l = windows; l != NULL; l = l->next) {
		GtkWidget *dialog = l->data;

		if (!GTK_IS_DIALOG (dialog) || !gtk_widget_get_mapped (dialog) ||
		    g_object_get_data (G_OBJECT (dialog), "answered") != NULL) {
			continue;
		}

		if (NEMO_IS_FILE_CONFLICT_DIALOG (dialog)) {
			answer_conflict (dialog);
		} else if (guard_mode &&
			   g_strcmp0 (gtk_window_get_title (GTK_WINDOW (dialog)), "Delete/overwrite test guard") == 0) {
			GList *found = NULL;
			gboolean overwrite = FALSE;

			find_widgets (dialog, &found);
			for (GList *w = found; w != NULL; w = w->next) {
				overwrite = overwrite || (GTK_IS_LABEL (w->data) &&
							  strstr (gtk_label_get_text (w->data), "Overwrite") != NULL);
			}
			g_list_free (found);
			overwrite_asks += overwrite;
			g_object_set_data (G_OBJECT (dialog), "answered", GINT_TO_POINTER (1));
			gtk_dialog_response (GTK_DIALOG (dialog), GTK_RESPONSE_OK);
		} else {
			char *text = NULL;

			if (GTK_IS_MESSAGE_DIALOG (dialog)) {
				g_object_get (dialog, "text", &text, NULL);
			}
			g_printerr ("FAIL %s: unexpected dialog: %s\n", case_name,
				    text != NULL ? text : "(no text)");
			g_free (text);
			failures++;
			g_object_set_data (G_OBJECT (dialog), "answered", GINT_TO_POINTER (1));
			gtk_dialog_response (GTK_DIALOG (dialog), GTK_RESPONSE_DELETE_EVENT);
		}
	}
	g_list_free (windows);

	return G_SOURCE_CONTINUE;
}

static void
put (const char *dir, const char *name, const char *contents)
{
	g_autofree char *path = g_build_filename (dir, name, NULL);
	g_autofree char *parent = g_path_get_dirname (path);

	check (g_mkdir_with_parents (parent, 0700) == 0);
	check (g_file_set_contents (path, contents, -1, NULL));
}

static gboolean
holds (const char *dir, const char *name, const char *want)
{
	g_autofree char *path = g_build_filename (dir, name, NULL);
	g_autofree char *contents = NULL;

	return g_file_get_contents (path, &contents, NULL, NULL) &&
	       g_strcmp0 (contents, want) == 0;
}

static gboolean
there (const char *dir, const char *name)
{
	g_autofree char *path = g_build_filename (dir, name, NULL);
	GStatBuf st;

	return g_lstat (path, &st) == 0;
}

static int
count_entries (const char *dir)
{
	GDir *listing = g_dir_open (dir, 0, NULL);
	int n = 0;

	if (listing == NULL) {
		return -1;
	}
	while (g_dir_read_name (listing) != NULL) {
		n++;
	}
	g_dir_close (listing);
	return n;
}

/* The one entry in dir other than skip, which holds want. */
static gboolean
other_holds (const char *dir, const char *skip, const char *want)
{
	GDir *listing = g_dir_open (dir, 0, NULL);
	const char *name;
	gboolean found = FALSE;

	if (listing == NULL) {
		return FALSE;
	}
	while ((name = g_dir_read_name (listing)) != NULL) {
		if (strcmp (name, skip) != 0 && holds (dir, name, want)) {
			found = TRUE;
		}
	}
	g_dir_close (listing);
	return found;
}

static void
run_job (GtkWidget *window, gboolean is_move, const char *from,
	 const char * const *names, const char *to, const Answer *answers)
{
	GList *sources = NULL;
	GFile *dest = g_file_new_for_path (to);
	guint answer_id, timeout_id;

	for (int i = 0; names[i] != NULL; i++) {
		g_autofree char *path = g_build_filename (from, names[i], NULL);

		sources = g_list_append (sources, g_file_new_for_path (path));
	}

	script = answers;
	answered = 0;
	job_finished = FALSE;

	answer_id = g_timeout_add (50, answer_dialogs, NULL);
	timeout_id = g_timeout_add_seconds (JOB_TIMEOUT_SECONDS, give_up, NULL);

	if (is_move) {
		nemo_file_operations_move (sources, NULL, dest, GTK_WINDOW (window), job_done, NULL);
	} else {
		nemo_file_operations_copy (sources, NULL, dest, GTK_WINDOW (window), job_done, NULL);
	}

	gtk_main ();
	if (job_finished) {
		g_source_remove (timeout_id);
	}
	g_source_remove (answer_id);

	if (script[answered].asked != NULL) {
		g_printerr ("FAIL %s: never asked about \"%s\"\n", case_name, script[answered].asked);
		failures++;
	}

	g_list_free_full (sources, g_object_unref);
	g_object_unref (dest);
}

typedef struct {
	const char *from_base;
	const char *to_base;
	const char *where;
	gboolean is_move;
	GtkWidget *window;
} Run;

/* Fresh folders for one case. */
static void
case_dirs (const Run *run, const char *name, char **from, char **to)
{
	g_autofree char *dir = g_strdup_printf ("%s-%s-%s", name, run->where,
						run->is_move ? "move" : "copy");

	*from = g_build_filename (run->from_base, dir, "from", NULL);
	*to = g_build_filename (run->to_base, dir, "to", NULL);
	check (g_mkdir_with_parents (*from, 0700) == 0);
	check (g_mkdir_with_parents (*to, 0700) == 0);
}

static void
begin (const Run *run, const char *name)
{
	static char label[128];

	g_snprintf (label, sizeof label, "%s, %s, %s", name, run->where,
		    run->is_move ? "move" : "copy");
	case_name = label;
	g_print ("case: %s\n", label);
}

/* The source is gone after a move and still there after a copy. */
static void
check_source (const Run *run, const char *from, const char *name, const char *contents)
{
	if (run->is_move) {
		check (!there (from, name));
	} else {
		check (holds (from, name, contents));
	}
}

/* The case from the report: Rename to a name that's taken too, then Replace
   that one. */
static void
case_rename_then_replace (const Run *run)
{
	static const char * const names[] = { "a.txt", NULL };
	static const Answer answers[] = {
		{ "a.txt", CONFLICT_RESPONSE_RENAME, "b", FALSE },
		{ "b", CONFLICT_RESPONSE_REPLACE, NULL, FALSE },
		END
	};
	g_autofree char *from = NULL, *to = NULL;

	begin (run, "rename then replace");
	case_dirs (run, "rr", &from, &to);
	put (from, "a.txt", "new");
	put (to, "a.txt", "old a");
	put (to, "b", "old b");

	run_job (run->window, run->is_move, from, names, to, answers);

	check (holds (to, "a.txt", "old a"));
	check (holds (to, "b", "new"));
	check (count_entries (to) == 2);
	check_source (run, from, "a.txt", "new");
}

/* Rename to a name nothing has: one question, and the file goes there. */
static void
case_rename_free (const Run *run)
{
	static const char * const names[] = { "a.txt", NULL };
	static const Answer answers[] = {
		{ "a.txt", CONFLICT_RESPONSE_RENAME, "c.txt", FALSE },
		END
	};
	g_autofree char *from = NULL, *to = NULL;

	begin (run, "rename to a free name");
	case_dirs (run, "rf", &from, &to);
	put (from, "a.txt", "new");
	put (to, "a.txt", "old a");

	run_job (run->window, run->is_move, from, names, to, answers);

	check (holds (to, "a.txt", "old a"));
	check (holds (to, "c.txt", "new"));
	check (count_entries (to) == 2);
	check_source (run, from, "a.txt", "new");
}

static void
case_skip (const Run *run)
{
	static const char * const names[] = { "a.txt", NULL };
	static const Answer answers[] = {
		{ "a.txt", CONFLICT_RESPONSE_SKIP, NULL, FALSE },
		END
	};
	g_autofree char *from = NULL, *to = NULL;

	begin (run, "skip");
	case_dirs (run, "sk", &from, &to);
	put (from, "a.txt", "new");
	put (to, "a.txt", "old a");

	run_job (run->window, run->is_move, from, names, to, answers);

	check (holds (to, "a.txt", "old a"));
	check (count_entries (to) == 1);
	check (holds (from, "a.txt", "new"));
}

static void
case_replace (const Run *run)
{
	static const char * const names[] = { "a.txt", NULL };
	static const Answer answers[] = {
		{ "a.txt", CONFLICT_RESPONSE_REPLACE, NULL, FALSE },
		END
	};
	g_autofree char *from = NULL, *to = NULL;

	begin (run, "replace");
	case_dirs (run, "re", &from, &to);
	put (from, "a.txt", "new");
	put (to, "a.txt", "old a");

	run_job (run->window, run->is_move, from, names, to, answers);

	check (holds (to, "a.txt", "new"));
	check (count_entries (to) == 1);
	check_source (run, from, "a.txt", "new");
}

/* Asked once, for the first file. */
static void
case_replace_all (const Run *run)
{
	static const char * const names[] = { "a.txt", "x.txt", NULL };
	static const Answer answers[] = {
		{ "a.txt", CONFLICT_RESPONSE_REPLACE, NULL, TRUE },
		END
	};
	g_autofree char *from = NULL, *to = NULL;

	begin (run, "replace all");
	case_dirs (run, "ra", &from, &to);
	put (from, "a.txt", "new a");
	put (from, "x.txt", "new x");
	put (to, "a.txt", "old a");
	put (to, "x.txt", "old x");

	run_job (run->window, run->is_move, from, names, to, answers);

	check (holds (to, "a.txt", "new a"));
	check (holds (to, "x.txt", "new x"));
	check (count_entries (to) == 2);
	check_source (run, from, "a.txt", "new a");
	check_source (run, from, "x.txt", "new x");
}

/* Keep both, for this one file only. */
static void
case_keep_both (const Run *run)
{
	static const char * const names[] = { "a.txt", NULL };
	static const Answer answers[] = {
		{ "a.txt", CONFLICT_RESPONSE_AUTO_RENAME, NULL, FALSE },
		END
	};
	g_autofree char *from = NULL, *to = NULL;

	begin (run, "keep both");
	case_dirs (run, "kb", &from, &to);
	put (from, "a.txt", "new");
	put (to, "a.txt", "old a");

	run_job (run->window, run->is_move, from, names, to, answers);

	check (holds (to, "a.txt", "old a"));
	check (other_holds (to, "a.txt", "new"));
	check (count_entries (to) == 2);
	check_source (run, from, "a.txt", "new");
}

/* A folder renamed onto another folder that's there, then merged into it. */
static void
case_rename_then_merge (const Run *run)
{
	static const char * const names[] = { "d", NULL };
	static const Answer answers[] = {
		{ "d", CONFLICT_RESPONSE_RENAME, "e", FALSE },
		{ "e", CONFLICT_RESPONSE_REPLACE, NULL, FALSE },
		END
	};
	g_autofree char *from = NULL, *to = NULL;
	g_autofree char *to_d = NULL, *to_e = NULL;

	begin (run, "rename then merge");
	case_dirs (run, "rm", &from, &to);
	put (from, "d/inner.txt", "new");
	put (to, "d/other.txt", "old d");
	put (to, "e/keep.txt", "old e");

	run_job (run->window, run->is_move, from, names, to, answers);

	to_d = g_build_filename (to, "d", NULL);
	to_e = g_build_filename (to, "e", NULL);
	check (holds (to_e, "inner.txt", "new"));
	check (holds (to_e, "keep.txt", "old e"));
	check (holds (to_d, "other.txt", "old d"));
	check (!there (to_d, "inner.txt"));
	check (count_entries (to) == 2);
	if (run->is_move) {
		check (!there (from, "d"));
	} else {
		g_autofree char *from_d = g_build_filename (from, "d", NULL);

		check (holds (from_d, "inner.txt", "new"));
	}
}

/* A link renamed to a free name. It's taken as a link either way. */
static void
case_link_renamed (const Run *run)
{
	static const char * const names[] = { "l", NULL };
	static const Answer answers[] = {
		{ "l", CONFLICT_RESPONSE_RENAME, "m", FALSE },
		END
	};
	g_autofree char *from = NULL, *to = NULL;
	g_autofree char *target = NULL, *link = NULL, *moved_to = NULL;
	g_autoptr (GFile) moved_link = NULL;

	if (!(nemo_link_kinds_supported (run->from_base) & NEMO_LINK_FILE_SYMLINK) ||
	    !(nemo_link_kinds_supported (run->to_base) & NEMO_LINK_FILE_SYMLINK)) {
		g_print ("note: no file symlinks here, link case not run\n");
		return;
	}

	begin (run, "link renamed");
	case_dirs (run, "lr", &from, &to);
	put (run->from_base, "away/payload.txt", "payload");
	target = g_build_filename (run->from_base, "away", "payload.txt", NULL);
	link = g_build_filename (from, "l", NULL);
	check (nemo_link_create (target, link, NULL, NEMO_LINK_FILE_SYMLINK, NULL));
	put (to, "l", "old l");

	run_job (run->window, run->is_move, from, names, to, answers);

	moved_to = g_build_filename (to, "m", NULL);
	moved_link = g_file_new_for_path (moved_to);
	check (nemo_link_kind (moved_link, NULL) == NEMO_LINK_FILE_SYMLINK);
	check (holds (to, "m", "payload"));
	check (holds (to, "l", "old l"));
	check (count_entries (to) == 2);
	check (there (from, "l") == !run->is_move);
}

static void
run_all (Run *run)
{
	for (int move = 0; move <= 1; move++) {
		run->is_move = move;
		case_rename_then_replace (run);
		case_rename_free (run);
		case_skip (run);
		case_replace (run);
		case_replace_all (run);
		case_keep_both (run);
		case_rename_then_merge (run);
		case_link_renamed (run);
	}
}

/* Somewhere on another file system than root, or NULL. Same search as the
   link move test. */
static char *
other_volume (const char *root)
{
#ifdef G_OS_WIN32
	wchar_t *wide_root;
	DWORD drives, root_serial = 0;
	char letter;

	wide_root = g_utf8_to_utf16 (root, 3, NULL, NULL, NULL);
	if (wide_root == NULL ||
	    !GetVolumeInformationW (wide_root, NULL, 0, &root_serial, NULL, NULL, NULL, 0)) {
		g_free (wide_root);
		return NULL;
	}
	g_free (wide_root);

	drives = GetLogicalDrives ();
	for (letter = 'C'; letter <= 'Z'; letter++) {
		wchar_t drive[] = { (wchar_t) letter, L':', L'\\', 0 };
		DWORD serial = 0;

		if (!(drives & (1u << (letter - 'A'))) || GetDriveTypeW (drive) != DRIVE_FIXED) {
			continue;
		}
		if (GetVolumeInformationW (drive, NULL, 0, &serial, NULL, NULL, NULL, 0) &&
		    serial != root_serial) {
			return g_strdup_printf ("%c:\\", letter);
		}
	}
	return NULL;
#else
	GStatBuf root_st, other_st;

	if (g_stat (root, &root_st) == 0 && g_stat ("/dev/shm", &other_st) == 0 &&
	    root_st.st_dev != other_st.st_dev) {
		return g_strdup ("/dev/shm");
	}
	return NULL;
#endif
}

int
main (int argc, char *argv[])
{
	NemoProgressInfoManager *manager;
	g_autofree char *config = NULL, *tmp = NULL, *base = NULL, *other = NULL;
	Run run = { 0 };

	guard_mode = argc > 1 && strcmp (argv[argc - 1], "guard") == 0;

	/* The guard would ask before each move and overwrite. Read once, so
	   before anything asks. */
	g_setenv ("NEMO_TESTGUARD_ALL_DELETES", guard_mode ? "1" : "0", TRUE);
	g_setenv ("NEMO_LINK_COPY", "file=keep", TRUE);

	config = test_scratch_config_home ("nemo-move-conflict-home-XXXXXX");
	test_init (&argc, &argv);
	nemo_global_preferences_init ();

	/* Holds each job's progress until the queue has started the next one.
	   test-nemo-link-copy-job.c says why. */
	manager = nemo_progress_info_manager_new ();

	tmp = test_scratch_dir ("nemo-move-conflict-XXXXXX", NULL);
	/* A share, say, in place of the second drive. */
	base = g_strdup (g_getenv ("NEMO_TEST_OTHER_VOLUME"));
	if (base == NULL) {
		base = other_volume (tmp);
	}
	if (base != NULL) {
		other = test_scratch_dir_in (base, "nemo-move-conflict-XXXXXX", NULL);
	}

	run.window = test_window_new ("move conflict test", 5);
	gtk_widget_show (run.window);

	if (guard_mode) {
		run.is_move = TRUE;
		for (int i = 0; i < 2; i++) {
			if (i == 0 && other == NULL) {
				continue;
			}
			run.from_base = tmp;
			run.to_base = i == 0 ? other : tmp;
			run.where = i == 0 ? "2 drives" : "1 drive";
			overwrite_asks = 0;
			case_replace (&run);
			if (overwrite_asks != 1) {
				g_printerr ("FAIL replace, %s: the guard was asked about the overwrite %d times\n",
					    run.where, overwrite_asks);
				failures++;
			}
		}
		goto done;
	}

	if (other != NULL) {
		run.from_base = tmp;
		run.to_base = other;
		run.where = "2 drives";
		run_all (&run);
	}

	run.from_base = tmp;
	run.to_base = tmp;
	run.where = "1 drive";
	run_all (&run);

 done:
	g_object_unref (manager);

	if (failures > 0) {
		g_printerr ("%d failure(s)\n", failures);
		return EXIT_FAILURE;
	}
	if (other == NULL) {
		g_printerr ("SKIP: no second file system here, so only the moves on one were checked\n");
		return 77;
	}

	g_print ("OK\n");
	return EXIT_SUCCESS;
}
