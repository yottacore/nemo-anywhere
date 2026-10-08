/* The archive core on its own: no GTK, no display, nothing of the app. Each
 * call it makes on its host reaches the host with the host's data and comes
 * back with the host's answer, and a host that leaves a call out gets a
 * fallback that removes nothing, starts nothing and answers no question. */

#include <stdlib.h>
#include <string.h>
#include <glib/gstdio.h>
#include <gio/gio.h>

#include "arc-host.h"
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

int
main (void)
{
	ArcHost empty = { 0 };
	GError *error = NULL;
	char *path = NULL;
	GFile *file;
	int fd;

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

	g_object_unref (file);
	g_remove (path);
	g_free (path);

	if (failures > 0) {
		g_printerr ("%d check(s) failed\n", failures);
		return EXIT_FAILURE;
	}
	return EXIT_SUCCESS;
}
