/* A tab moves between windows that are separate processes by being handed
 * over on the session bus: each copy lists its windows, and one of them takes
 * the folder as a new tab and shows it. A tab moved out to a window of its own
 * starts a new copy with the view and selection on its command line.
 *
 * On Linux every copy also has test-nemo-tab-move-probe (argv[2]) preloaded,
 * which reports the view and selection each window shows, so a move that kept
 * the folder but lost either one fails. The probe also drags a tab off to
 * empty screen: a second tab goes to a new copy, and the only tab stays put,
 * with the window in its own process or with every window in one process.
 * The only tab dropped on another copy's window, or closed from the tab menu,
 * takes its window with it, and that copy has to end cleanly.
 *
 * Needs the built program (argv[1]), a display and a session bus. Runs on an
 * X server of its own, since the probe moves the pointer, and starts a bus if
 * the environment has none.
 */

#include <config.h>

#include <gio/gio.h>
#include <glib/gstdio.h>

#include <fcntl.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

#include "test-scratch.h"
#include "test-check.h"

#define COMPACT_VIEW "OAFIID:Nemo_File_Manager_Compact_View"

static GDBusConnection *bus;
static char *probe_dir;

static GPid
launch (char **argv)
{
	GPid pid = 0;
	GError *error = NULL;

	if (!g_spawn_async (NULL, argv, NULL, G_SPAWN_DO_NOT_REAP_CHILD,
	                    NULL, NULL, &pid, &error)) {
		g_printerr ("spawn: %s\n", error->message);
		g_error_free (error);
	}

	return pid;
}

static void
to_log (gpointer data)
{
	int fd = open (data, O_WRONLY | O_CREAT | O_TRUNC, 0644);

	if (fd >= 0) {
		dup2 (fd, STDERR_FILENO);
		close (fd);
	}
}

/* The same, with what the copy prints on stderr kept in a file. */
static GPid
launch_logged (char **argv, const char *log_path)
{
	GPid pid = 0;
	GError *error = NULL;

	if (!g_spawn_async (NULL, argv, NULL, G_SPAWN_DO_NOT_REAP_CHILD,
	                    to_log, (gpointer) log_path, &pid, &error)) {
		g_printerr ("spawn: %s\n", error->message);
		g_error_free (error);
	}

	return pid;
}

/* Waits for the copy to end on its own. FALSE if it is still running. */
static gboolean
wait_exit (GPid pid, int seconds, int *status)
{
	int i;

	for (i = 0; i < seconds * 10; i++) {
		if (waitpid (pid, status, WNOHANG) == pid) {
			return TRUE;
		}
		g_usleep (100 * 1000);
	}

	return FALSE;
}

static gboolean
alive (GPid pid)
{
	int status;

	return pid > 0 && waitpid (pid, &status, WNOHANG) == 0;
}

static void
stop (GPid pid)
{
	int i;

	if (pid <= 0) {
		return;
	}
	if (alive (pid)) {
		kill (pid, SIGTERM);
		for (i = 0; i < 50 && alive (pid); i++) {
			g_usleep (100 * 1000);
		}
	}
	g_spawn_close_pid (pid);
}

static GPid
owner_pid (const char *unique)
{
	GVariant *reply;
	guint32 pid = 0;

	reply = g_dbus_connection_call_sync (bus,
	                                     "org.freedesktop.DBus", "/org/freedesktop/DBus",
	                                     "org.freedesktop.DBus", "GetConnectionUnixProcessID",
	                                     g_variant_new ("(s)", unique),
	                                     G_VARIANT_TYPE ("(u)"),
	                                     G_DBUS_CALL_FLAGS_NONE, -1, NULL, NULL);
	if (reply == NULL) {
		return 0;
	}
	g_variant_get (reply, "(u)", &pid);
	g_variant_unref (reply);

	return (GPid) pid;
}

/* The unique name the process with this pid holds its place in the queue by. */
static char *
name_of (GPid pid)
{
	GVariant *reply;
	char **names = NULL, *found = NULL;
	int i;

	reply = g_dbus_connection_call_sync (bus,
	                                     "org.freedesktop.DBus", "/org/freedesktop/DBus",
	                                     "org.freedesktop.DBus", "ListQueuedOwners",
	                                     g_variant_new ("(s)", "org.NemoAnywhere"),
	                                     G_VARIANT_TYPE ("(as)"),
	                                     G_DBUS_CALL_FLAGS_NONE, -1, NULL, NULL);
	if (reply == NULL) {
		return NULL;
	}
	g_variant_get (reply, "(^as)", &names);
	g_variant_unref (reply);

	for (i = 0; names[i] != NULL && found == NULL; i++) {
		if (owner_pid (names[i]) == pid) {
			found = g_strdup (names[i]);
		}
	}
	g_strfreev (names);

	return found;
}

static char *
wait_name_of (GPid pid)
{
	char *name = NULL;
	int i;

	for (i = 0; i < 200 && name == NULL; i++) {
		name = name_of (pid);
		if (name == NULL) {
			g_usleep (100 * 1000);
		}
	}

	return name;
}

/* The copy's one window: its id and its title, which names the folder showing. */
static gboolean
first_window (const char *name, guint32 *id, char **title)
{
	GVariant *reply, *windows;
	gboolean found = FALSE;

	reply = g_dbus_connection_call_sync (bus, name, "/org/NemoAnywhere",
	                                     "org.NemoAnywhere.Tabs", "ListWindows", NULL,
	                                     G_VARIANT_TYPE ("(a(uts))"),
	                                     G_DBUS_CALL_FLAGS_NONE, 5000, NULL, NULL);
	if (reply == NULL) {
		return FALSE;
	}
	windows = g_variant_get_child_value (reply, 0);
	if (g_variant_n_children (windows) > 0) {
		guint64 handle;

		g_variant_get_child (windows, 0, "(uts)", id, &handle, title);
		found = TRUE;
	}
	g_variant_unref (windows);
	g_variant_unref (reply);

	return found;
}

/* The title starts with the folder's name; what follows is the program's. */
static gboolean
shows (const char *title, const char *folder)
{
	size_t len = strlen (folder);

	return title != NULL && strncmp (title, folder, len) == 0 &&
	       (title[len] == '\0' || title[len] == ' ');
}

static gboolean
wait_title (const char *name, const char *want, guint32 *id)
{
	char *title = NULL;
	int i;

	for (i = 0; i < 200; i++) {
		g_clear_pointer (&title, g_free);
		if (first_window (name, id, &title) && shows (title, want)) {
			g_free (title);
			return TRUE;
		}
		g_usleep (100 * 1000);
	}

	g_printerr ("wanted a window titled \"%s\", last saw %s%s%s\n", want,
	            title != NULL ? "\"" : "", title != NULL ? title : "none", title != NULL ? "\"" : "");
	g_free (title);

	return FALSE;
}

static gboolean
take_tab (const char *name, guint32 id, const char *uri, const char *view,
          const char * const *selected, GError **error)
{
	GVariant *reply;

	reply = g_dbus_connection_call_sync (bus, name, "/org/NemoAnywhere",
	                                     "org.NemoAnywhere.Tabs", "TakeTab",
	                                     g_variant_new ("(uss^asu)", id, uri, view, selected, 0),
	                                     NULL, G_DBUS_CALL_FLAGS_NONE, 5000, NULL, error);
	if (reply == NULL) {
		return FALSE;
	}
	g_variant_unref (reply);

	return TRUE;
}

/* Every copy in the queue for the name, by its unique name. */
static char **
queued (void)
{
	GVariant *reply;
	char **names = NULL;

	reply = g_dbus_connection_call_sync (bus,
	                                     "org.freedesktop.DBus", "/org/freedesktop/DBus",
	                                     "org.freedesktop.DBus", "ListQueuedOwners",
	                                     g_variant_new ("(s)", "org.NemoAnywhere"),
	                                     G_VARIANT_TYPE ("(as)"),
	                                     G_DBUS_CALL_FLAGS_NONE, -1, NULL, NULL);
	if (reply == NULL) {
		return g_new0 (char *, 1);
	}
	g_variant_get (reply, "(^as)", &names);
	g_variant_unref (reply);

	return names;
}

/* A copy that is not one of these, started by one of them. */
static char *
wait_new_copy (const char * const *known, int seconds)
{
	int i, j;

	for (i = 0; i < seconds * 10; i++) {
		char **names = queued ();

		for (j = 0; names[j] != NULL; j++) {
			if (!g_strv_contains (known, names[j])) {
				char *found = g_strdup (names[j]);

				g_strfreev (names);
				return found;
			}
		}
		g_strfreev (names);
		g_usleep (100 * 1000);
	}

	return NULL;
}

static guint
count_queued (void)
{
	char **names = queued ();
	guint n = g_strv_length (names);

	g_strfreev (names);

	return n;
}

/* What the probe in that copy last wrote, or NULL before it has. */
static char *
probe_state (GPid pid)
{
	char *path = g_strdup_printf ("%s/%d", probe_dir, (int) pid);
	char *text = NULL;

	g_file_get_contents (path, &text, NULL, NULL);
	g_free (path);

	return text;
}

static gboolean
state_says (const char *state, const char *key, const char *want)
{
	char *line = g_strdup_printf ("\n%s=%s\n", key, want);
	char *with_start = g_strconcat ("\n", state, NULL);
	gboolean says = strstr (with_start, line) != NULL;

	g_free (with_start);
	g_free (line);

	return says;
}

/* The window shows the folder, in the view, with these names selected. A
   NULL view or selection is not looked at. */
static gboolean
wait_shows (GPid pid, const char *folder, const char *view, const char *selected)
{
	char *state = NULL;
	char *title_line = g_strdup_printf ("\ntitle=%s", folder);
	int i;

	for (i = 0; i < 200; i++) {
		char *with_start;
		gboolean title_ok;

		g_clear_pointer (&state, g_free);
		state = probe_state (pid);
		if (state != NULL) {
			with_start = g_strconcat ("\n", state, NULL);
			title_ok = strstr (with_start, title_line) != NULL;
			g_free (with_start);
			if (title_ok && (view == NULL || state_says (state, "view", view)) &&
			    (selected == NULL || state_says (state, "selected", selected))) {
				g_free (title_line);
				g_free (state);
				return TRUE;
			}
		}
		g_usleep (100 * 1000);
	}

	g_printerr ("wanted %s in the %s view with \"%s\" selected, last saw:\n%s", folder,
	            view != NULL ? view : "any", selected != NULL ? selected : "any",
	            state != NULL ? state : "nothing\n");
	g_free (title_line);
	g_free (state);

	return FALSE;
}

/* Has the probe drag the current tab off to empty screen, and waits for it. */
static gboolean
tear_off (GPid pid)
{
	char *state = NULL;
	int i;

	if (kill (pid, SIGUSR1) != 0) {
		return FALSE;
	}
	for (i = 0; i < 100; i++) {
		state = probe_state (pid);
		if (state != NULL && state_says (state, "tore", "1")) {
			g_free (state);
			return TRUE;
		}
		g_free (state);
		g_usleep (100 * 1000);
	}

	return FALSE;
}

/* Has the probe run a command: "drop X Y" or "close". */
static gboolean
probe_do (GPid pid, const char *command)
{
	char *path = g_strdup_printf ("%s/%d.do", probe_dir, (int) pid);
	gboolean written = g_file_set_contents (path, command, -1, NULL);

	g_free (path);

	return written && kill (pid, SIGUSR2) == 0;
}

/* The copy, whose last tab just went, ends on its own with nothing logged
   against it and no crash report. Reaps it, so its pid is cleared. */
static void
check_closed_cleanly (GPid *pid, const char *log_path, const char *what)
{
	char *log = NULL, *crash_dir, *crash_end;
	GDir *dir;
	int status = 0;

	crash_end = g_strdup_printf ("-%d.txt", (int) *pid);
	if (!wait_exit (*pid, 20, &status)) {
		g_printerr ("FAIL the window %s did not close\n", what);
		failures++;
	} else {
		*pid = 0;
		if (!WIFEXITED (status) || WEXITSTATUS (status) != 0) {
			g_printerr ("FAIL the window %s ended badly (%s %d)\n", what,
			            WIFSIGNALED (status) ? "signal" : "exit",
			            WIFSIGNALED (status) ? WTERMSIG (status) : WEXITSTATUS (status));
			failures++;
		}
	}
	g_file_get_contents (log_path, &log, NULL, NULL);
	if (log != NULL && strstr (log, "CRITICAL") != NULL) {
		g_printerr ("FAIL the window %s logged criticals:\n%s", what, log);
		failures++;
	}
	g_free (log);
	crash_dir = g_build_filename (g_getenv ("XDG_CONFIG_HOME"), "nemo-anywhere", "crash", NULL);
	dir = g_dir_open (crash_dir, 0, NULL);
	if (dir != NULL) {
		const char *name;

		while ((name = g_dir_read_name (dir)) != NULL) {
			if (g_str_has_suffix (name, crash_end)) {
				g_printerr ("FAIL the window %s left a crash report, %s\n", what, name);
				failures++;
			}
		}
		g_dir_close (dir);
	}
	g_free (crash_dir);
	g_free (crash_end);
}

/* A copy some other copy started, so not ours to reap. */
static void
stop_other (GPid pid)
{
	int i;

	if (pid <= 0 || kill (pid, SIGTERM) != 0) {
		return;
	}
	for (i = 0; i < 50 && kill (pid, 0) == 0; i++) {
		g_usleep (100 * 1000);
	}
}

/* Same as the instances test: meson turns the bus off for every test. */
static void
ensure_session_bus (int argc, char *argv[])
{
	GDBusConnection *probe;
	char *dbus_run;
	char **relaunch;
	int i;

	if (g_getenv ("NEMO_TEST_BUS_RELAUNCHED") != NULL) {
		return;
	}

	probe = g_bus_get_sync (G_BUS_TYPE_SESSION, NULL, NULL);
	if (probe != NULL) {
		g_object_unref (probe);
		return;
	}

	dbus_run = g_find_program_in_path ("dbus-run-session");
	if (dbus_run == NULL) {
		return;
	}

	g_setenv ("NEMO_TEST_BUS_RELAUNCHED", "1", TRUE);

	relaunch = g_new0 (char *, argc + 3);
	relaunch[0] = dbus_run;
	relaunch[1] = (char *) "--";
	for (i = 0; i < argc; i++) {
		relaunch[i + 2] = argv[i];
	}

	execv (dbus_run, relaunch);

	g_free (relaunch);
	g_free (dbus_run);
}

int
main (int argc, char *argv[])
{
	char *exe, *home, *tmp, *alpha, *beta, *gamma, *delta, *epsilon, *alpha_uri, *beta_uri, *picked, *kept;
	char *alpha_name = NULL, *beta_name = NULL, *third_name = NULL, *torn_name = NULL;
	char *single = NULL, *single_settings = NULL, *single_config = NULL;
	GPid alpha_pid = 0, beta_pid = 0, third_pid = 0, torn_pid = 0, single_pid = 0;
	GPid delta_pid = 0, epsilon_pid = 0;
	char *delta_name = NULL, *delta_log = NULL, *epsilon_log = NULL;
	guint32 alpha_id = 0, beta_id = 0, third_id = 0, torn_id = 0;
	gboolean probed = FALSE;
	GError *error = NULL;

	if (argc < 2) {
		g_printerr ("usage: %s <nemo-anywhere> [probe module]\n", argv[0]);
		return 1;
	}
	exe = argv[1];

	test_own_display (argc, argv, "1280x900x24");

	if (g_getenv ("DISPLAY") == NULL && g_getenv ("WAYLAND_DISPLAY") == NULL) {
		g_print ("SKIP: no display\n");
		return 77;
	}

	ensure_session_bus (argc, argv);

	bus = g_bus_get_sync (G_BUS_TYPE_SESSION, NULL, NULL);
	if (bus == NULL) {
		g_print ("SKIP: no session bus\n");
		return 77;
	}

	home = test_scratch_config_home ("nemo-tabmove-home-XXXXXX");
	tmp = test_scratch_dir ("nemo-tabmove-XXXXXX", NULL);
	alpha = g_build_filename (tmp, "alpha", NULL);
	beta = g_build_filename (tmp, "beta", NULL);
	gamma = g_build_filename (tmp, "gamma", NULL);
	delta = g_build_filename (tmp, "delta", NULL);
	epsilon = g_build_filename (tmp, "epsilon", NULL);
	picked = g_build_filename (alpha, "picked.txt", NULL);
	kept = g_build_filename (beta, "kept.txt", NULL);
	check (g_mkdir (alpha, 0755) == 0);
	check (g_mkdir (beta, 0755) == 0);
	check (g_mkdir (gamma, 0755) == 0);
	check (g_mkdir (delta, 0755) == 0);
	check (g_mkdir (epsilon, 0755) == 0);
	check (g_file_set_contents (picked, "x\n", -1, NULL));
	check (g_file_set_contents (kept, "x\n", -1, NULL));
	alpha_uri = g_filename_to_uri (alpha, NULL, NULL);
	beta_uri = g_filename_to_uri (beta, NULL, NULL);

#ifdef __linux__
	/* Every copy from here on has the probe, and so does any copy it starts. */
	if (argc >= 3) {
		probe_dir = test_scratch_dir ("nemo-tabmove-probe-XXXXXX", NULL);
		g_setenv ("NEMO_TABMOVE_OUT", probe_dir, TRUE);
		g_setenv ("LD_PRELOAD", argv[2], TRUE);
		probed = TRUE;
	}
#endif
	if (!probed) {
		g_print ("no probe here, so view, selection and drags are not checked\n");
	}

	{
		char *args_a[] = { exe, (char *) "--geometry=600x400", alpha, NULL };
		/* Off to the side, so a tab can be dropped on it. */
		char *args_b[] = { exe, (char *) "--geometry=600x400+650+0", beta, NULL };

		alpha_pid = launch (args_a);
		beta_pid = launch (args_b);
	}
	check (alpha_pid > 0 && beta_pid > 0);

	alpha_name = wait_name_of (alpha_pid);
	beta_name = wait_name_of (beta_pid);
	check (alpha_name != NULL && beta_name != NULL);
	if (alpha_name == NULL || beta_name == NULL) {
		goto out;
	}

	/* Each lists its one window, titled for the folder it shows. */
	check (wait_title (alpha_name, "alpha", &alpha_id));
	check (wait_title (beta_name, "beta", &beta_id));

	/* The hand-over: beta's window takes alpha's folder, and shows it in the
	   view it had, with the same file selected. */
	{
		char *selected_uri = g_filename_to_uri (picked, NULL, NULL);
		const char *selected[] = { selected_uri, NULL };

		check (take_tab (beta_name, beta_id, alpha_uri, COMPACT_VIEW, selected, NULL));
		g_free (selected_uri);
	}
	if (!wait_title (beta_name, "alpha", &beta_id)) {
		g_printerr ("FAIL the window that took the tab does not show it\n");
		failures++;
	}
	if (probed && !wait_shows (beta_pid, "alpha", "compact", "picked.txt")) {
		g_printerr ("FAIL the tab taken lost its view or its selection\n");
		failures++;
	}
	check (alive (beta_pid));

	/* A window that is not there, or something that is not a URI, is refused
	   and changes nothing. */
	{
		const char *none[] = { NULL };

		check (!take_tab (beta_name, beta_id + 1000, beta_uri, "", none, &error));
		check (error != NULL && g_dbus_error_is_remote_error (error));
		g_clear_error (&error);
		check (!take_tab (beta_name, beta_id, "not a uri", "", none, &error));
		check (error != NULL);
		g_clear_error (&error);
		check (wait_title (beta_name, "alpha", &beta_id));
	}

	/* A tab moved out to a window of its own. */
	{
		char *selected_uri = g_filename_to_uri (kept, NULL, NULL);
		char *args[] = { exe, (char *) "--geometry=600x400", (char *) "--tab-view",
		                 (char *) COMPACT_VIEW, (char *) "--tab-select", selected_uri,
		                 beta_uri, NULL };

		third_pid = launch (args);
		g_free (selected_uri);
	}
	check (third_pid > 0);
	third_name = wait_name_of (third_pid);
	check (third_name != NULL);
	if (third_name != NULL) {
		check (wait_title (third_name, "beta", &third_id));
		if (probed && !wait_shows (third_pid, "beta", "compact", "kept.txt")) {
			g_printerr ("FAIL the window of its own lost the view or the selection\n");
			failures++;
		}
	}
	if (!probed || third_name == NULL) {
		goto out;
	}

	/* The only tab dragged to empty screen stays where it is, rather than the
	   window starting over in a new copy. */
	{
		guint before = count_queued ();

		check (tear_off (alpha_pid));
		g_usleep (3 * G_USEC_PER_SEC);
		if (count_queued () != before) {
			g_printerr ("FAIL the only tab dragged off started another copy\n");
			failures++;
		}
		check (alive (alpha_pid));
		check (wait_title (alpha_name, "alpha", &alpha_id));
	}

	/* One of two tabs dragged to empty screen goes to a new copy, view and
	   selection with it, and the tab it was goes. */
	{
		const char *known[] = { alpha_name, beta_name, third_name, NULL };

		check (tear_off (beta_pid));
		torn_name = wait_new_copy (known, 20);
		check (torn_name != NULL);
		if (torn_name != NULL) {
			torn_pid = owner_pid (torn_name);
			check (wait_title (torn_name, "alpha", &torn_id));
			if (!wait_shows (torn_pid, "alpha", "compact", "picked.txt")) {
				g_printerr ("FAIL the tab dragged off lost its view or its selection\n");
				failures++;
			}
		}
		check (wait_title (beta_name, "beta", &beta_id));
	}

	/* The only tab dropped on another copy's window goes there, and the window
	   it left closes cleanly. Its tab used to be freed after the window, and
	   took its menus off a window that was gone. The copies from the two
	   cases above can sit over beta's window, so they go first. */
	stop_other (torn_pid);
	torn_pid = 0;
	stop (third_pid);
	third_pid = 0;
	delta_log = g_build_filename (probe_dir, "delta.log", NULL);
	{
		char *args[] = { exe, (char *) "--geometry=600x400+0+460", delta, NULL };

		delta_pid = launch_logged (args, delta_log);
	}
	check (delta_pid > 0);
	delta_name = delta_pid > 0 ? wait_name_of (delta_pid) : NULL;
	check (delta_name != NULL);
	if (delta_name != NULL && wait_shows (delta_pid, "delta", NULL, NULL)) {
		check (wait_title (beta_name, "beta", &beta_id));
		/* The middle of beta's window. */
		check (probe_do (delta_pid, "drop 950 200"));
		if (!wait_title (beta_name, "delta", &beta_id)) {
			g_printerr ("FAIL the only tab dropped on another window did not go there\n");
			failures++;
		}
		check_closed_cleanly (&delta_pid, delta_log, "the only tab left");
	} else {
		failures++;
	}

	/* The only tab closed from the tab menu closes the window cleanly too. The
	   menu's Move tab to items hold the tab as well. */
	epsilon_log = g_build_filename (probe_dir, "epsilon.log", NULL);
	{
		char *args[] = { exe, (char *) "--geometry=600x400", epsilon, NULL };

		epsilon_pid = launch_logged (args, epsilon_log);
	}
	check (epsilon_pid > 0);
	if (epsilon_pid > 0 && wait_shows (epsilon_pid, "epsilon", NULL, NULL)) {
		check (probe_do (epsilon_pid, "close"));
		check_closed_cleanly (&epsilon_pid, epsilon_log, "whose only tab was closed");
	} else {
		failures++;
	}

	/* With every window in one process, the only tab stays put as well, and no
	   window is made for it. */
	single = test_scratch_dir ("nemo-tabmove-single-XXXXXX", NULL);
	single_config = g_build_filename (single, "nemo-anywhere", NULL);
	single_settings = g_build_filename (single_config, "settings.shcl", NULL);
	check (g_mkdir (single_config, 0755) == 0);
	check (g_file_set_contents (single_settings, "preferences.window-per-process: false\n",
	                            -1, NULL));
	{
		char *args[] = { exe, (char *) "--geometry=600x400", gamma, NULL };
		char **envp = g_environ_setenv (g_get_environ (), "XDG_CONFIG_HOME", single, TRUE);

		if (!g_spawn_async (NULL, args, envp, G_SPAWN_DO_NOT_REAP_CHILD,
		                    NULL, NULL, &single_pid, &error)) {
			g_printerr ("spawn: %s\n", error->message);
			g_clear_error (&error);
		}
		g_strfreev (envp);
	}
	check (single_pid > 0);
	if (single_pid > 0 && wait_shows (single_pid, "gamma", NULL, NULL)) {
		char *state;

		check (tear_off (single_pid));
		g_usleep (2 * G_USEC_PER_SEC);
		state = probe_state (single_pid);
		if (state == NULL || !state_says (state, "windows", "1")) {
			g_printerr ("FAIL the only tab dragged off made a window:\n%s",
			            state != NULL ? state : "nothing\n");
			failures++;
		}
		g_free (state);
		check (alive (single_pid));
	} else {
		failures++;
	}

out:
	g_free (alpha_name);
	g_free (beta_name);
	g_free (third_name);
	g_free (torn_name);
	g_free (delta_name);
	g_free (delta_log);
	g_free (epsilon_log);

	stop (single_pid);
	stop (delta_pid);
	stop (epsilon_pid);
	stop_other (torn_pid);
	stop (third_pid);
	stop (beta_pid);
	stop (alpha_pid);
	g_clear_object (&bus);

	if (single_settings != NULL) {
		g_unlink (single_settings);
		g_rmdir (single_config);
	}
	g_unlink (picked);
	g_unlink (kept);
	g_rmdir (alpha);
	g_rmdir (beta);
	g_rmdir (gamma);
	g_rmdir (delta);
	g_rmdir (epsilon);
	g_free (single_settings);
	g_free (single_config);
	g_free (single);
	g_free (picked);
	g_free (kept);
	g_free (alpha);
	g_free (beta);
	g_free (gamma);
	g_free (delta);
	g_free (epsilon);
	g_free (alpha_uri);
	g_free (beta_uri);
	g_free (tmp);
	g_free (home);
	g_free (probe_dir);

	if (failures == 0) {
		g_print ("nemo-tab-move: all checks passed\n");
	}

	return failures == 0 ? 0 : 1;
}
