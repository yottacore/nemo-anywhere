/* The archive core on its own: no GTK, no display, nothing of the app. Each
 * call it makes on its host reaches the host with the host's data and comes
 * back with the host's answer, and a host that leaves a call out gets a
 * fallback that removes nothing, starts nothing and answers no question.
 * Then the size totals on made-up paths, nested or other filesystems from
 * made-up mount tables, and the link choices against what a writer stores. */

#include <stdlib.h>
#include <string.h>
#include <glib/gstdio.h>
#include <gio/gio.h>

#include "arc-host.h"
#include "arc-link-options.h"
#include "arc-mounts.h"
#include "arc-path-list.h"
#include "arc-settings.h"
#include "test-check.h"

static guint core_warnings = 0;

static void
count_warning (const char     *domain,
	       GLogLevelFlags  level,
	       const char     *message,
	       gpointer        user_data)
{
	(void) domain;
	(void) level;
	(void) message;
	(void) user_data;
	core_warnings++;
}

/* What the fake host saw. One per process, like the app's own. */
typedef struct {
	char    *calls;		/* call names, space separated */
	gpointer last_data;
	char    *last_text;
	double   done;
	double   total;
	GFile   *last_file;
	ArcLine  last_line;
	char    *password;	/* what the password call answers */
} Seen;

static Seen seen;

static void
saw (gpointer    data,
     const char *call,
     const char *text)
{
	char *calls = g_strconcat (seen.calls != NULL ? seen.calls : "", call, " ", NULL);

	g_free (seen.calls);
	seen.calls = calls;
	seen.last_data = data;
	g_free (seen.last_text);
	seen.last_text = g_strdup (text);
}

static void
saw_file (GFile *file)
{
	g_set_object (&seen.last_file, file);
}

static void
seen_reset (void)
{
	g_clear_pointer (&seen.calls, g_free);
	g_clear_pointer (&seen.last_text, g_free);
	g_clear_pointer (&seen.password, g_free);
	g_clear_object (&seen.last_file);
	memset (&seen, 0, sizeof seen);
}

static void fake_status (gpointer d, const char *t) { saw (d, "status", t); }
static void fake_details (gpointer d, const char *t) { saw (d, "details", t); }
static void fake_pulse (gpointer d) { saw (d, "pulse", NULL); }

static void
fake_fraction (gpointer d,
	       double   done,
	       double   total)
{
	saw (d, "fraction", NULL);
	seen.done = done;
	seen.total = total;
}

static void
fake_file_added (gpointer d,
		 GFile   *file)
{
	saw (d, "file_added", NULL);
	saw_file (file);
}

static GFileEnumerator *
fake_children (gpointer             d,
	       GFile               *dir,
	       const char          *attributes,
	       GFileQueryInfoFlags  flags,
	       GCancellable        *cancellable,
	       GError             **error)
{
	(void) flags;
	(void) cancellable;
	saw (d, "children", attributes);
	saw_file (dir);
	g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_PERMISSION_DENIED, "fake");
	return NULL;
}

/* A run is whatever the host says it is. This one is a counter. */
struct _ArcRun {
	int exit_status;
	int freed;
};

static ArcRun fake_run_object = { 3, 0 };
static GInputStream *fake_out = NULL;
static GInputStream *fake_err = NULL;

static ArcRun *
fake_start (gpointer            d,
	    const char * const *argv,
	    const char         *cwd,
	    GSubprocessFlags    flags,
	    GError            **error)
{
	(void) flags;
	(void) error;
	saw (d, "start", argv[0]);
	check (g_strcmp0 (cwd, "/nowhere") == 0);
	return &fake_run_object;
}

static GInputStream *fake_stdout (ArcRun *run) { (void) run; saw (NULL, "stdout", NULL); return fake_out; }
static GInputStream *fake_stderr (ArcRun *run) { (void) run; saw (NULL, "stderr", NULL); return fake_err; }
static void fake_force_exit (ArcRun *run) { (void) run; saw (NULL, "force_exit", NULL); }
static gboolean fake_if_exited (ArcRun *run) { (void) run; saw (NULL, "if_exited", NULL); return TRUE; }
static int fake_exit_status (ArcRun *run) { saw (NULL, "exit_status", NULL); return run->exit_status; }
static void fake_run_free (ArcRun *run) { saw (NULL, "free", NULL); run->freed++; }

static gboolean
fake_wait_check (ArcRun  *run,
		 GError **error)
{
	saw (NULL, "wait_check", NULL);
	g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_FAILED, "exit 3");
	return run->exit_status == 0;
}

static char **
fake_expand (gpointer               d,
	     const char            *text,
	     const ArcCommandToken *tokens,
	     GError               **error)
{
	(void) error;
	saw (d, "expand", text);
	return g_strdupv ((char **) tokens[0].values);
}

static void
fake_warn_unused (gpointer               d,
		  ArcLine                line,
		  const char            *text,
		  const ArcCommandToken *tokens)
{
	(void) tokens;
	saw (d, "warn_unused", text);
	seen.last_line = line;
}

static gboolean
fake_remove_own (gpointer d,
		 GFile   *file)
{
	saw (d, "remove_own", NULL);
	saw_file (file);
	return TRUE;
}

static gboolean
fake_remove_tree (gpointer      d,
		  GFile        *dir,
		  GCancellable *cancellable)
{
	(void) cancellable;
	saw (d, "remove_tree", NULL);
	saw_file (dir);
	return TRUE;
}

static void
fake_trash_by_user (gpointer d,
		    GList   *files)
{
	saw (d, "trash_by_user", NULL);
	saw_file (files->data);
}

static void
fake_error (gpointer    d,
	    const char *primary,
	    const char *details)
{
	(void) details;
	saw (d, "error", primary);
}

static void
fake_warning (gpointer    d,
	      const char *primary,
	      const char *details)
{
	(void) details;
	saw (d, "warning", primary);
}

static void
fake_conflict (gpointer           d,
	       const ArcConflict *question,
	       ArcConflictAnswer *answer)
{
	saw (d, "conflict", question->entry_name);
	answer->response = ARC_CONFLICT_RENAME;
	answer->new_name = g_strdup ("b.txt");
	answer->apply_to_all = TRUE;
}

static char *
fake_password (gpointer    d,
	       const char *archive_name)
{
	saw (d, "password", archive_name);
	return g_strdup (seen.password);
}

static const ArcHost fake_host = {
	.progress = { fake_status, fake_details, fake_fraction, fake_pulse },
	.changes = { fake_file_added },
	.walk = { fake_children },
	.programs = { fake_start, fake_stdout, fake_stderr, fake_force_exit,
		      fake_wait_check, fake_if_exited, fake_exit_status, fake_run_free },
	.commands = { fake_expand, fake_warn_unused },
	.deletes = { fake_remove_own, fake_remove_tree, fake_trash_by_user },
	.ask = { fake_error, fake_warning, fake_conflict, fake_password },
	.data = (gpointer) &seen,
};

static gboolean
error_is_not_supported (GError *error)
{
	return error != NULL && g_error_matches (error, G_IO_ERROR, G_IO_ERROR_NOT_SUPPORTED);
}

/* With nothing to ask, nothing happens: no file goes, no program starts, no
   question is answered for the person. */
static void
check_fallbacks (const ArcHost *host,
		 GFile         *file)
{
	const char *argv[] = { "true", NULL };
	const char *values[] = { "x", NULL };
	ArcCommandToken tokens[] = { { "TOKEN", values, TRUE }, { NULL, NULL, FALSE } };
	ArcConflict question = { "a.zip", "a.txt", FALSE, 1, 0, file, NULL };
	ArcConflictAnswer answer = { ARC_CONFLICT_REPLACE, NULL, TRUE };
	GList *files = g_list_append (NULL, file);
	GError *error = NULL;
	char **expanded;

	arc_status (host, "status");
	arc_details (host, "details");
	arc_fraction (host, 1, 2);
	arc_pulse (host);
	arc_file_added (host, file);

	check (arc_walk_children (host, file, "standard::*", G_FILE_QUERY_INFO_NONE, NULL, &error) == NULL);
	check (error_is_not_supported (error));
	g_clear_error (&error);

	check (arc_run_start (host, argv, NULL, G_SUBPROCESS_FLAGS_NONE, &error) == NULL);
	check (error_is_not_supported (error));
	g_clear_error (&error);
	check (arc_run_stdout (host, NULL) == NULL);
	check (arc_run_stderr (host, NULL) == NULL);
	arc_run_force_exit (host, NULL);
	check (!arc_run_wait_check (host, NULL, &error));
	check (error_is_not_supported (error));
	g_clear_error (&error);
	check (!arc_run_if_exited (host, NULL));
	check (arc_run_exit_status (host, NULL) == -1);
	arc_run_free (host, NULL);

	expanded = arc_command_expand (host, "{{TOKEN}}", tokens, &error);
	check (expanded == NULL);
	check (error_is_not_supported (error));
	g_clear_error (&error);
	g_strfreev (expanded);
	arc_command_warn_unused (host, ARC_LINE_CREATE_7Z, "x", tokens);

	core_warnings = 0;
	check (!arc_remove_own (host, file));
	check (!arc_remove_tree (host, file, NULL));
	arc_trash_by_user (host, files);
	check (g_file_query_exists (file, NULL));
	check (core_warnings == 1);	/* the trash was asked for, so it says so */

	core_warnings = 0;
	arc_show_error (host, "Could not", "why");
	arc_show_warning (host, "Kept", NULL);
	check (core_warnings == 2);

	arc_ask_conflict (host, &question, &answer);
	check (answer.response == ARC_CONFLICT_CANCEL);
	check (answer.new_name == NULL);
	check (!answer.apply_to_all);

	check (arc_ask_password (host, "a.zip") == NULL);

	g_list_free (files);
}

static void
check_forwarding (GFile *file)
{
	const ArcHost *host = &fake_host;
	const char *argv[] = { "7z", NULL };
	const char *values[] = { "-mx9", NULL };
	ArcCommandToken tokens[] = { { "LEVEL", values, TRUE }, { NULL, NULL, FALSE } };
	ArcConflict question = { "a.zip", "a.txt", FALSE, 1, 0, file, NULL };
	ArcConflictAnswer answer = { ARC_CONFLICT_CANCEL, NULL, FALSE };
	GList *files = g_list_append (NULL, file);
	GError *error = NULL;
	ArcRun *run;
	char **expanded;
	char *password;

	seen_reset ();
	fake_out = g_memory_input_stream_new ();
	fake_err = g_memory_input_stream_new ();

	arc_status (host, "Compressing");
	check (seen.last_data == &seen);
	check (g_strcmp0 (seen.last_text, "Compressing") == 0);
	arc_details (host, "a.txt");
	check (g_strcmp0 (seen.last_text, "a.txt") == 0);
	arc_fraction (host, 3, 4);
	check (seen.done == 3 && seen.total == 4);
	arc_pulse (host);
	arc_file_added (host, file);
	check (seen.last_file == file);

	check (arc_walk_children (host, file, "standard::name", G_FILE_QUERY_INFO_NONE, NULL, &error) == NULL);
	check (g_error_matches (error, G_IO_ERROR, G_IO_ERROR_PERMISSION_DENIED));
	g_clear_error (&error);
	check (g_strcmp0 (seen.last_text, "standard::name") == 0);

	run = arc_run_start (host, argv, "/nowhere", G_SUBPROCESS_FLAGS_STDOUT_PIPE, &error);
	check (run == &fake_run_object);
	check (g_strcmp0 (seen.last_text, "7z") == 0);
	check (arc_run_stdout (host, run) == fake_out);
	check (arc_run_stderr (host, run) == fake_err);
	arc_run_force_exit (host, run);
	check (!arc_run_wait_check (host, run, &error));
	check (g_error_matches (error, G_IO_ERROR, G_IO_ERROR_FAILED));
	g_clear_error (&error);
	check (arc_run_if_exited (host, run));
	check (arc_run_exit_status (host, run) == 3);
	arc_run_free (host, run);
	check (fake_run_object.freed == 1);

	expanded = arc_command_expand (host, "{{LEVEL}}", tokens, &error);
	check (expanded != NULL && g_strcmp0 (expanded[0], "-mx9") == 0);
	check (g_strcmp0 (seen.last_text, "{{LEVEL}}") == 0);
	g_strfreev (expanded);
	arc_command_warn_unused (host, ARC_LINE_EXTRACT_RAR, "rar x", tokens);
	check (seen.last_line == ARC_LINE_EXTRACT_RAR);

	g_clear_object (&seen.last_file);
	check (arc_remove_own (host, file));
	check (seen.last_file == file);
	g_clear_object (&seen.last_file);
	check (arc_remove_tree (host, file, NULL));
	check (seen.last_file == file);
	g_clear_object (&seen.last_file);
	arc_trash_by_user (host, files);
	check (seen.last_file == file);

	arc_show_error (host, "Could not", NULL);
	check (g_strcmp0 (seen.last_text, "Could not") == 0);
	arc_show_warning (host, "Kept", NULL);
	check (g_strcmp0 (seen.last_text, "Kept") == 0);

	arc_ask_conflict (host, &question, &answer);
	check (answer.response == ARC_CONFLICT_RENAME);
	check (g_strcmp0 (answer.new_name, "b.txt") == 0);
	check (answer.apply_to_all);
	check (g_strcmp0 (seen.last_text, "a.txt") == 0);
	g_free (answer.new_name);

	seen.password = g_strdup ("hunter2");
	password = arc_ask_password (host, "a.zip");
	check (g_strcmp0 (password, "hunter2") == 0);
	check (g_strcmp0 (seen.last_text, "a.zip") == 0);
	g_free (password);

	/* An empty answer is a no, as the extract prompt has always taken it. */
	g_free (seen.password);
	seen.password = g_strdup ("");
	check (arc_ask_password (host, "a.zip") == NULL);

	/* Every call reached the host, in order, and each only once. */
	check (g_strcmp0 (seen.calls,
			  "status details fraction pulse file_added children "
			  "start stdout stderr force_exit wait_check if_exited exit_status free "
			  "expand warn_unused remove_own remove_tree trash_by_user "
			  "error warning conflict password password ") == 0);

	g_list_free (files);
	g_clear_object (&fake_out);
	g_clear_object (&fake_err);
	seen_reset ();
}

static void
check_settings (void)
{
	ArcSettings src = { { NULL }, 0 };
	ArcSettings copy = { { NULL }, 0 };

	src.lines[ARC_LINE_CREATE_7Z] = g_strdup ("7z a {{TARGET_ARCHIVE}}");
	src.lines[ARC_LINE_EXTRACT_RAR] = g_strdup ("rar x {{SOURCE_ARCHIVE}}");
	src.threads = 6;

	arc_settings_copy (&copy, &src);
	check (copy.threads == 6);
	check (g_strcmp0 (copy.lines[ARC_LINE_CREATE_7Z], "7z a {{TARGET_ARCHIVE}}") == 0);
	check (copy.lines[ARC_LINE_CREATE_7Z] != src.lines[ARC_LINE_CREATE_7Z]);
	check (copy.lines[ARC_LINE_CREATE_RAR] == NULL);

	/* The job's copy is its own: a settings change after it started is not
	   seen halfway through. */
	arc_settings_clear (&src);
	check (src.lines[ARC_LINE_CREATE_7Z] == NULL && src.threads == 0);
	check (g_strcmp0 (copy.lines[ARC_LINE_EXTRACT_RAR], "rar x {{SOURCE_ARCHIVE}}") == 0);

	arc_settings_clear (&copy);
	check (copy.lines[ARC_LINE_EXTRACT_RAR] == NULL);
}

#define SYM ARC_FOLLOW_SYMLINKS
#define JUNC ARC_FOLLOW_JUNCTIONS
#define NEST ARC_FOLLOW_NESTED_FS
#define OTHER ARC_FOLLOW_OTHER_FS

#define J2J ARC_STORES_JUNCTIONS
#define J2S ARC_STORES_JUNCTIONS_AS_SYMLINKS
#define S2S ARC_STORES_SYMLINKS

/* "Junction defaults" in the compression design doc, row by row. */
static void
check_junction_table (void)
{
	static const struct {
		guint         stores;
		ArcLinkChoice want;
	} rows[] = {
		{ J2J,       ARC_LINK_STORE_JUNCTION },
		{ J2J | J2S, ARC_LINK_STORE_JUNCTION },
		{ J2S,       ARC_LINK_STORE_SYMLINK },
		{ 0,         ARC_LINK_IGNORE },
	};
	guint i;

	for (i = 0; i < G_N_ELEMENTS (rows); i++) {
		guint stores = rows[i].stores | S2S;
		ArcLinkOptions options;

		check (arc_junctions_default (ARC_LINK_STORE_SYMLINK, stores) == rows[i].want);
		check (arc_junctions_default (ARC_LINK_STORE_SYMLINK, rows[i].stores) == rows[i].want);
		check (arc_junctions_default (ARC_LINK_IGNORE, stores) == ARC_LINK_IGNORE);
		check (arc_junctions_default (ARC_LINK_FOLLOW, stores) == ARC_LINK_FOLLOW);

		/* The same through the options, with Junctions never touched. */
		arc_link_options_init (&options);
		arc_link_options_set_symlinks (&options, ARC_LINK_STORE_SYMLINK);
		check (arc_link_options_junctions (&options, stores) == rows[i].want);
		arc_link_options_set_symlinks (&options, ARC_LINK_FOLLOW);
		check (arc_link_options_junctions (&options, stores) == ARC_LINK_FOLLOW);
		arc_link_options_set_symlinks (&options, ARC_LINK_IGNORE);
		check (arc_link_options_junctions (&options, stores) == ARC_LINK_IGNORE);
	}
}

static void
check_link_options (void)
{
	ArcLinkOptions options;
	guint all = S2S | J2J | J2S;

	arc_link_options_init (&options);
	check (options.symlinks == ARC_LINK_IGNORE);
	check (!options.junctions_set);
	check (options.follow_nested);
	check (!options.follow_other);
	check (arc_link_options_follow (&options, all) == NEST);

	/* A hand change to Junctions sticks through later Symlinks changes. */
	arc_link_options_set_symlinks (&options, ARC_LINK_FOLLOW);
	arc_link_options_set_junctions (&options, ARC_LINK_IGNORE);
	check (options.junctions_set);
	check (arc_link_options_junctions (&options, all) == ARC_LINK_IGNORE);
	arc_link_options_set_symlinks (&options, ARC_LINK_STORE_SYMLINK);
	check (arc_link_options_junctions (&options, all) == ARC_LINK_IGNORE);
	arc_link_options_set_symlinks (&options, ARC_LINK_FOLLOW);
	check (arc_link_options_junctions (&options, all) == ARC_LINK_IGNORE);
	check (arc_link_options_follow (&options, all) == (SYM | NEST));

	/* Any store choice the writer has, whatever Symlinks is. */
	arc_link_options_set_symlinks (&options, ARC_LINK_IGNORE);
	arc_link_options_set_junctions (&options, ARC_LINK_STORE_SYMLINK);
	check (arc_link_options_junctions (&options, all) == ARC_LINK_STORE_SYMLINK);
	arc_link_options_set_junctions (&options, ARC_LINK_FOLLOW);
	check (arc_link_options_follow (&options, all) == (JUNC | NEST));

	/* A store the writer can't do is Ignore, and only for that writer: the
	   choice itself stays for one that can. */
	arc_link_options_init (&options);
	arc_link_options_set_symlinks (&options, ARC_LINK_STORE_SYMLINK);
	check (arc_link_options_symlinks (&options, 0) == ARC_LINK_IGNORE);
	check (arc_link_options_symlinks (&options, J2J | J2S) == ARC_LINK_IGNORE);
	check (options.symlinks == ARC_LINK_STORE_SYMLINK);
	check (arc_link_options_symlinks (&options, S2S) == ARC_LINK_STORE_SYMLINK);

	arc_link_options_set_junctions (&options, ARC_LINK_STORE_JUNCTION);
	check (arc_link_options_junctions (&options, S2S | J2S) == ARC_LINK_IGNORE);
	check (arc_link_options_junctions (&options, J2J) == ARC_LINK_STORE_JUNCTION);
	arc_link_options_set_junctions (&options, ARC_LINK_STORE_SYMLINK);
	check (arc_link_options_junctions (&options, S2S | J2J) == ARC_LINK_IGNORE);
	check (arc_link_options_junctions (&options, J2S) == ARC_LINK_STORE_SYMLINK);

	/* Follow and Ignore need nothing of the writer. Store counts as Ignore
	   for the follow bits. */
	arc_link_options_init (&options);
	arc_link_options_set_symlinks (&options, ARC_LINK_FOLLOW);
	check (arc_link_options_symlinks (&options, 0) == ARC_LINK_FOLLOW);
	check (arc_link_options_junctions (&options, 0) == ARC_LINK_FOLLOW);
	check (arc_link_options_follow (&options, 0) == (SYM | JUNC | NEST));
	arc_link_options_set_symlinks (&options, ARC_LINK_STORE_SYMLINK);
	check (arc_link_options_follow (&options, all) == NEST);
	options.follow_nested = FALSE;
	options.follow_other = TRUE;
	check (arc_link_options_follow (&options, all) == OTHER);

	/* The 2 old boxes. Store plus follow was store, since a stored link
	   never got followed. */
	check (arc_link_choice_from_boxes (FALSE, FALSE) == ARC_LINK_IGNORE);
	check (arc_link_choice_from_boxes (FALSE, TRUE) == ARC_LINK_FOLLOW);
	check (arc_link_choice_from_boxes (TRUE, FALSE) == ARC_LINK_STORE_SYMLINK);
	check (arc_link_choice_from_boxes (TRUE, TRUE) == ARC_LINK_STORE_SYMLINK);

	check_junction_table ();
}

static guint
mixes_counting (const ArcPathList *list,
		guint64            bytes)
{
	guint64 totals[ARC_MIX_COUNT];
	guint mixes = 0;
	guint mix;

	arc_path_list_totals (list, totals);
	for (mix = 0; mix < ARC_MIX_COUNT; mix++) {
		if (totals[mix] == bytes) {
			mixes |= 1u << mix;
		} else {
			check (totals[mix] == 0);
		}
	}
	return mixes;
}

/* The 2 worked examples in "Size totals", each with its paths in both
   orders, since which path the scan finds first mustn't matter. */
static void
check_worked_examples (void)
{
	guint order;

	for (order = 0; order < 2; order++) {
		ArcPathList *list = arc_path_list_new ();
		guint counted = 0;
		guint64 bytes = 0;

		/* A file in v2/, also reached through current -> v2. The
		   direct path needs nothing, so it counts in all 16. */
		if (order == 0) {
			check (arc_path_list_add (list, "/sel/v2/a.txt", 100, 0));
			check (!arc_path_list_add (list, "/sel/v2/a.txt", 100, SYM));
		} else {
			check (arc_path_list_add (list, "/sel/v2/a.txt", 100, SYM));
			check (!arc_path_list_add (list, "/sel/v2/a.txt", 100, 0));
		}
		check (mixes_counting (list, 100) == 0xffff);
		check (arc_path_list_find (list, "/sel/v2/a.txt", &bytes, &counted));
		check (bytes == 100 && counted == 0xffff);
		check (arc_path_list_change (list, ARC_FOLLOW_ALL, SYM) == 0);
		arc_path_list_free (list);

		/* Reached only through a symlink one way and only through a
		   junction the other: counts wherever either is followed. */
		list = arc_path_list_new ();
		if (order == 0) {
			arc_path_list_add (list, "C:\\data\\b.bin", 7, SYM);
			arc_path_list_add (list, "C:\\data\\b.bin", 7, JUNC);
		} else {
			arc_path_list_add (list, "C:\\data\\b.bin", 7, JUNC);
			arc_path_list_add (list, "C:\\data\\b.bin", 7, SYM);
		}
		check (arc_path_list_count (list) == 1);
		check (arc_path_list_total (list, 0) == 0);
		check (arc_path_list_total (list, NEST | OTHER) == 0);
		check (arc_path_list_total (list, SYM) == 7);
		check (arc_path_list_total (list, JUNC | OTHER) == 7);
		/* every mix but the 4 with neither */
		check (mixes_counting (list, 7) == 0xeeee);
		/* Turning off either one alone leaves the other way in. */
		check (arc_path_list_change (list, SYM | JUNC, SYM) == 0);
		check (arc_path_list_change (list, SYM | JUNC, JUNC) == 0);
		check (arc_path_list_change (list, SYM, SYM) == 7);
		check (arc_path_list_change (list, SYM, JUNC) == 0);
		arc_path_list_free (list);
	}
}

/* One path that needs 2 options counts in both changes. A file's bytes come
   from the first path to it. */
static void
check_changes (void)
{
	ArcPathList *list = arc_path_list_new ();
	guint64 bytes = 0;

	arc_path_list_add (list, "/sel/both", 50, SYM | JUNC);
	arc_path_list_add (list, "/sel/plain", 1000, 0);
	arc_path_list_add (list, "/sel/far/x", 3, OTHER);
	arc_path_list_add (list, "/sel/plain", 999999, SYM);

	check (arc_path_list_total (list, 0) == 1000);
	check (arc_path_list_total (list, SYM | JUNC) == 1050);
	check (arc_path_list_total (list, ARC_FOLLOW_ALL) == 1053);
	check (arc_path_list_change (list, SYM | JUNC, SYM) == 50);
	check (arc_path_list_change (list, SYM | JUNC, JUNC) == 50);
	check (arc_path_list_change (list, SYM | JUNC, OTHER) == 0);
	check (arc_path_list_change (list, ARC_FOLLOW_ALL, OTHER) == 3);
	check (arc_path_list_change (list, ARC_FOLLOW_ALL, NEST) == 0);
	check (arc_path_list_find (list, "/sel/plain", &bytes, NULL) && bytes == 1000);
	check (!arc_path_list_find (list, "/sel/plai", NULL, NULL));
	check (!arc_path_list_find (list, "/sel/plain/", NULL, NULL));
	check (!arc_path_list_find (list, "", NULL, NULL));

	arc_path_list_free (list);
}

/* Made-up paths with made-up needs against a plain count of the rule: a mix
   counts a file when it follows everything one of its paths needs. */
static void
check_against_rule (void)
{
	ArcPathList *list = arc_path_list_new ();
	GRand *rand = g_rand_new_with_seed (20261007);
	enum { FILES = 3000, PATHS = 12000 };
	guint64 *bytes = g_new0 (guint64, FILES);
	guint *ways = g_new0 (guint, FILES);	/* one bit per needs value seen */
	guint64 want[ARC_MIX_COUNT] = { 0 };
	guint64 got[ARC_MIX_COUNT];
	guint i, mix, option;
	int wrong = 0;

	for (i = 0; i < FILES; i++) {
		bytes[i] = (guint64) g_rand_int_range (rand, 0, 1 << 30) * 7;
	}
	for (i = 0; i < PATHS; i++) {
		guint file = (guint) g_rand_int_range (rand, 0, FILES);
		guint needs = (guint) g_rand_int_range (rand, 0, ARC_MIX_COUNT);
		char *path = g_strdup_printf ("/made/up/%u/%u/file-%u", file % 17, file % 5, file);
		gboolean is_new = arc_path_list_add (list, path, bytes[file], needs);

		if (is_new != (ways[file] == 0)) {
			wrong++;
		}
		ways[file] |= 1u << needs;
		g_free (path);
	}
	for (mix = 0; mix < ARC_MIX_COUNT; mix++) {
		for (i = 0; i < FILES; i++) {
			guint needs;

			for (needs = 0; needs < ARC_MIX_COUNT; needs++) {
				if ((ways[i] & (1u << needs)) && (needs & ~mix) == 0) {
					want[mix] += bytes[i];
					break;
				}
			}
		}
	}
	arc_path_list_totals (list, got);
	for (mix = 0; mix < ARC_MIX_COUNT; mix++) {
		if (got[mix] != want[mix]) {
			wrong++;
		}
		for (option = 1; option < ARC_MIX_COUNT; option <<= 1) {
			guint64 change = (mix & option) ? want[mix] - want[mix & ~option] : 0;

			if (arc_path_list_change (list, mix, option) != change) {
				wrong++;
			}
		}
	}
	check (wrong == 0);
	check (want[ARC_FOLLOW_ALL] > want[0] && want[0] > 0);

	g_rand_free (rand);
	g_free (bytes);
	g_free (ways);
	arc_path_list_free (list);
}

/* Enough paths to grow everything many times over, each found again, and
   each found once only. */
static void
check_many_paths (guint n)
{
	ArcPathList *list = arc_path_list_new ();
	char path[200];
	guint i;
	int wrong = 0;
	guint64 bytes;

	for (i = 0; i < n; i++) {
		g_snprintf (path, sizeof path, "/home/someone/photos/%u/%u/IMG_%08u.jpg", i % 97, i % 13, i);
		if (!arc_path_list_add (list, path, i, 0)) {
			wrong++;
		}
	}
	for (i = 0; i < n; i++) {
		g_snprintf (path, sizeof path, "/home/someone/photos/%u/%u/IMG_%08u.jpg", i % 97, i % 13, i);
		if (!arc_path_list_find (list, path, &bytes, NULL) || bytes != i) {
			wrong++;
		}
		if (arc_path_list_add (list, path, 0, SYM)) {
			wrong++;
		}
	}
	g_snprintf (path, sizeof path, "/home/someone/photos/0/0/IMG_%08u.jpg", n);
	check (!arc_path_list_find (list, path, NULL, NULL));
	check (wrong == 0);
	check (arc_path_list_count (list) == n);
	check (arc_path_list_total (list, 0) == (guint64) n * (n - 1) / 2);

	arc_path_list_free (list);
}

/* Resident KB, where there is a /proc to ask. 0 elsewhere. */
static guint64
resident_kb (void)
{
	char *text = NULL;
	const char *line;
	guint64 kb = 0;

	if (g_file_get_contents ("/proc/self/status", &text, NULL, NULL) &&
	    (line = strstr (text, "VmRSS:")) != NULL) {
		kb = g_ascii_strtoull (line + 6, NULL, 10);
	}
	g_free (text);
	return kb;
}

/* test-arc-core bench N: N made-up paths in, then each found in another
   order. */
static int
bench (guint n)
{
	ArcPathList *list;
	GPtrArray *paths = g_ptr_array_new_with_free_func (g_free);
	gint64 start, added, found;
	guint64 kb;
	guint i, hits = 0;

	for (i = 0; i < n; i++) {
		g_ptr_array_add (paths, g_strdup_printf ("/mnt/data/projects/p%04u/src/module_%03u/file_%08u.c",
							 i % 5000, i % 300, i));
	}
	kb = resident_kb ();
	start = g_get_monotonic_time ();
	list = arc_path_list_new ();
	for (i = 0; i < n; i++) {
		arc_path_list_add (list, paths->pdata[i], i, i & ARC_FOLLOW_ALL);
	}
	added = g_get_monotonic_time ();
	kb = resident_kb () - kb;
	for (i = 0; i < n; i++) {
		hits += arc_path_list_find (list, paths->pdata[(i * 7919u) % n], NULL, NULL);
	}
	found = g_get_monotonic_time ();

	g_print ("%u paths: add %.0f ms (%.0f ns each), find %.0f ms (%.0f ns each), %u found, "
		 "%" G_GUINT64_FORMAT " MB (%.0f bytes each)\n",
		 n, (added - start) / 1000.0, (added - start) * 1000.0 / n,
		 (found - added) / 1000.0, (found - added) * 1000.0 / n, hits,
		 kb / 1024, kb * 1024.0 / n);

	g_ptr_array_unref (paths);
	arc_path_list_free (list);
	return hits == n ? EXIT_SUCCESS : EXIT_FAILURE;
}

static void
check_mounts_posix (void)
{
	const ArcMountEntry entries[] = {
		{ "/", "/dev/sda1", "ext4" },
		{ "/tank", "tank", "zfs" },
		{ "/tank/data", "tank/data", "zfs" },
		{ "/tank/data/.zfs/snapshot/s1", "tank/data@s1", "zfs" },
		{ "/backup", "backup/main", "zfs" },
		{ "/mnt/b", "/dev/sdb1", "btrfs" },
		{ "/mnt/b/home", "/dev/sdb1", "btrfs" },
		{ "/mnt/c", "/dev/sdc1", "btrfs" },
		{ "/srv/www", "/dev/sda1", "ext4" },
		{ "/tmp", "tmpfs", "tmpfs" },
		{ "/run", "tmpfs", "tmpfs" },
		{ "/nas", "server:/export", "nfs4" },
		{ "/nas/again", "server:/export", "nfs4" },
		{ "/mnt/over", "/dev/sdd1", "ext4" },
		{ "/mnt/over", "tank/over", "zfs" },
		{ "/Volumes/Data", "/dev/disk3s5", "apfs" },
		{ "/Volumes/Sys", "/dev/disk3s1s1", "apfs" },
		{ "/Volumes/Stick", "/dev/disk4s1", "apfs" },
		{ "/odd dir", "/dev/sde1", "xfs" },
	};
	ArcMountTable *table = arc_mount_table_new (entries, G_N_ELEMENTS (entries), FALSE);
	ArcMountTable *none = arc_mount_table_new (NULL, 0, FALSE);

	check (arc_mount_table_kind (table, "/home/me", "/home/me/docs") == ARC_FS_SAME);
	check (arc_mount_table_kind (table, "/home/me", "/") == ARC_FS_SAME);
	check (arc_mount_table_kind (table, "/", "/tankard/x") == ARC_FS_SAME);
	check (arc_mount_table_kind (table, "/home/me", "/tank") == ARC_FS_OTHER);
	check (arc_mount_table_kind (table, "/tank", "/tank/x/y") == ARC_FS_SAME);
	check (arc_mount_table_kind (table, "/tank/x", "/tank/data/y") == ARC_FS_NESTED);
	check (arc_mount_table_kind (table, "/tank/data", "/tank/x") == ARC_FS_NESTED);
	check (arc_mount_table_kind (table, "/tank/data/", "/tank/data/.zfs/snapshot/s1/f") == ARC_FS_NESTED);
	check (arc_mount_table_kind (table, "/tank", "/backup/x") == ARC_FS_OTHER);
	check (arc_mount_table_kind (table, "/mnt/b", "/mnt/b/home/me") == ARC_FS_NESTED);
	check (arc_mount_table_kind (table, "/mnt/b", "/mnt/c") == ARC_FS_OTHER);
	check (arc_mount_table_kind (table, "/mnt/b", "/mnt/b/homework") == ARC_FS_SAME);
	check (arc_mount_table_kind (table, "/home", "/srv/www/index.html") == ARC_FS_NESTED);
	check (arc_mount_table_kind (table, "/home", "/tmp/x") == ARC_FS_OTHER);
	check (arc_mount_table_kind (table, "/tmp", "/run/x") == ARC_FS_OTHER);
	check (arc_mount_table_kind (table, "/nas/a", "/nas/again/b") == ARC_FS_OTHER);
	check (arc_mount_table_kind (table, "/nas/a", "/nas/b") == ARC_FS_SAME);
	check (arc_mount_table_kind (table, "/home", "/nas") == ARC_FS_OTHER);
	/* The later mount at one place is the one on top. */
	check (arc_mount_table_kind (table, "/tank", "/mnt/over/x") == ARC_FS_NESTED);
	check (arc_mount_table_kind (table, "/Volumes/Data", "/Volumes/Sys/x") == ARC_FS_NESTED);
	check (arc_mount_table_kind (table, "/Volumes/Data", "/Volumes/Stick") == ARC_FS_OTHER);
	check (arc_mount_table_kind (table, "/", "/odd dir/x") == ARC_FS_OTHER);
	check (arc_mount_table_is_mount_point (table, "/tank/data/"));
	check (arc_mount_table_is_mount_point (table, "/"));
	check (!arc_mount_table_is_mount_point (table, "/tank/data/x"));
	/* Paths are case sensitive here. */
	check (arc_mount_table_kind (table, "/", "/TANK/x") == ARC_FS_SAME);
	check (arc_mount_table_kind (none, "/a", "/b") == ARC_FS_SAME);

	arc_mount_table_free (table);
	arc_mount_table_free (none);
}

static void
check_mounts_windows (void)
{
	const ArcMountEntry entries[] = {
		{ "\\\\?\\Volume{aaaa}\\", "\\\\?\\Volume{aaaa}\\", NULL },
		{ "C:\\", "\\\\?\\Volume{aaaa}\\", NULL },
		{ "\\\\?\\Volume{bbbb}\\", "\\\\?\\Volume{bbbb}\\", NULL },
		{ "D:\\", "\\\\?\\Volume{bbbb}\\", NULL },
		{ "C:\\mnt\\d\\", "\\\\?\\Volume{bbbb}\\", NULL },
		/* Nothing is nested on Windows, whatever it is. */
		{ "E:\\", "\\\\?\\Volume{cccc}\\", "zfs" },
		{ "C:\\mnt\\e\\", "\\\\?\\Volume{cccc}\\", "zfs" },
	};
	ArcMountTable *table = arc_mount_table_new (entries, G_N_ELEMENTS (entries), TRUE);

	check (arc_mount_table_kind (table, "C:\\Users\\me", "c:/users/ME/x") == ARC_FS_SAME);
	check (arc_mount_table_kind (table, "C:\\Users", "D:\\x") == ARC_FS_OTHER);
	check (arc_mount_table_kind (table, "C:\\Users", "C:\\mnt\\d\\x") == ARC_FS_OTHER);
	check (arc_mount_table_kind (table, "C:\\Users", "C:\\mnt\\dx") == ARC_FS_SAME);
	check (arc_mount_table_kind (table, "D:\\", "C:\\mnt\\d\\x") == ARC_FS_SAME);
	check (arc_mount_table_kind (table, "E:\\", "C:\\mnt\\e") == ARC_FS_SAME);
	check (arc_mount_table_kind (table, "D:\\", "C:\\mnt\\e") == ARC_FS_OTHER);
	check (arc_mount_table_kind (table, "C:\\x", "\\\\?\\Volume{AAAA}\\y") == ARC_FS_SAME);
	check (arc_mount_table_kind (table, "C:\\x", "\\??\\Volume{bbbb}\\y") == ARC_FS_OTHER);
	check (arc_mount_table_kind (table, "C:\\x", "\\\\?\\C:\\y") == ARC_FS_SAME);
	check (arc_mount_table_kind (table, "\\\\?\\D:\\x", "C:\\mnt\\d") == ARC_FS_SAME);
	/* A share or a mapped drive has no volume. Told apart by root alone. */
	check (arc_mount_table_kind (table, "C:\\x", "\\\\nas\\share\\y") == ARC_FS_OTHER);
	check (arc_mount_table_kind (table, "\\\\nas\\share\\x", "\\\\NAS\\Share\\y\\z") == ARC_FS_SAME);
	check (arc_mount_table_kind (table, "\\\\nas\\share\\x", "\\\\?\\UNC\\nas\\share") == ARC_FS_SAME);
	check (arc_mount_table_kind (table, "\\\\nas\\share", "\\\\nas\\other") == ARC_FS_OTHER);
	check (arc_mount_table_kind (table, "Z:\\x", "z:/y") == ARC_FS_SAME);
	check (arc_mount_table_kind (table, "Z:\\x", "Y:\\x") == ARC_FS_OTHER);
	check (arc_mount_table_kind (table, "Z:\\x", "C:\\x") == ARC_FS_OTHER);
	/* Where a volume is mounted, by any of its names. */
	check (arc_mount_table_is_mount_point (table, "\\??\\Volume{BBBB}\\"));
	check (arc_mount_table_is_mount_point (table, "d:"));
	check (arc_mount_table_is_mount_point (table, "C:/mnt/d"));
	check (!arc_mount_table_is_mount_point (table, "C:\\mnt"));
	check (!arc_mount_table_is_mount_point (table, "C:\\mnt\\d\\x"));
	/* Only a target naming a volume makes a reparse point a mount point. */
	check (arc_target_is_volume ("\\??\\Volume{0d1c2b3a-0000-0000-0000-100000000000}\\"));
	check (arc_target_is_volume ("\\\\?\\volume{x}"));
	check (!arc_target_is_volume ("\\??\\Volume{x}\\folder"));
	check (!arc_target_is_volume ("\\??\\C:\\"));
	check (!arc_target_is_volume ("D:\\"));
	check (!arc_target_is_volume ("\\??\\Volume{"));
	check (!arc_target_is_volume ("\\\\?\\Volume}"));
	check (!arc_target_is_volume (""));

	arc_mount_table_free (table);
}

/* The real table. All that can be said of every box is that a folder is on
   its own filesystem. */
static void
check_mounts_read (void)
{
	ArcMountTable *table = arc_mount_table_read ();
	char *here = g_get_current_dir ();
	char *below = g_build_filename (here, "x", NULL);

	check (table != NULL);
	check (arc_mount_table_kind (table, here, here) == ARC_FS_SAME);
	check (arc_mount_table_kind (table, here, below) == ARC_FS_SAME);

	g_free (here);
	g_free (below);
	arc_mount_table_free (table);
}

int
main (int    argc,
      char **argv)
{
	ArcHost empty = { 0 };
	GError *error = NULL;
	char *path = NULL;
	GFile *file;
	int fd;

	if (argc == 3 && strcmp (argv[1], "bench") == 0) {
		return bench ((guint) g_ascii_strtoull (argv[2], NULL, 10));
	}

	g_log_set_handler ("Archive core", G_LOG_LEVEL_WARNING, count_warning, NULL);

	/* A file for the deletes to leave alone. Removed here, not by the core. */
	fd = g_file_open_tmp ("arc-core-XXXXXX", &path, &error);
	if (fd < 0) {
		g_printerr ("no temp file: %s\n", error->message);
		return 77;
	}
	g_close (fd, NULL);
	file = g_file_new_for_path (path);

	check_fallbacks (NULL, file);
	check_fallbacks (&empty, file);
	check_forwarding (file);
	check_settings ();
	check_worked_examples ();
	check_changes ();
	check_against_rule ();
	check_many_paths (200000);
	check_mounts_posix ();
	check_mounts_windows ();
	check_mounts_read ();
	check_link_options ();

	g_object_unref (file);
	g_remove (path);
	g_free (path);

	if (failures > 0) {
		g_printerr ("%d check(s) failed\n", failures);
		return EXIT_FAILURE;
	}
	return EXIT_SUCCESS;
}
