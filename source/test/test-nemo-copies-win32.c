/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */

/* test-nemo-copies-win32.c - copies of the app reach each other with no bus.

   Copyright © 2026 t00mietum (CryptogID: ปʬϝღถɔ4რఠΔթะ9ƾǝu).

   This program is free software; you can redistribute it and/or
   modify it under the terms of the GNU General Public License as
   published by the Free Software Foundation; version 2 of the
   License.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   General Public License for more details.

   You should have received a copy of the GNU General Public
   License along with this program; if not, write to the
   Free Software Foundation, Inc., 51 Franklin Street, Suite 500,
   Boston, MA 02110-1335, USA.
*/

/* 2 copies of the app, each on its own folder. This test finds both, and a
 * third copy run with --reset has to find them too and refuse. Each lists its
 * window, a tab goes into one of them, and a settings change made here shows
 * in both titles. This test keeps its settings in another folder, so the
 * change can only have come across directly, not through the file. --quit
 * from a fourth copy ends both. A copy that is killed leaves nothing behind
 * that a later --reset would trip on.
 *
 * Meanwhile nothing may start a session bus: no gdbus.exe under any of the
 * copies or this test, no bus address published where there was none, and
 * no nonce file in the copies' temp folder. The copies get no
 * DBUS_SESSION_BUS_ADDRESS, so the app has to keep itself off the bus. This
 * test then drops it again and tries what used to ask for the bus: an
 * action's dbus condition, which must never pass, and the power inhibit. */

#include <config.h>

#include <string.h>

#include <gtk/gtk.h>
#include <glib/gstdio.h>
#include <windows.h>
#include <tlhelp32.h>

#include <libnemo-private/nemo-action.h>
#include <libnemo-private/nemo-config.h>
#include <libnemo-private/nemo-file.h>
#include <libnemo-private/nemo-file-utilities.h>
#include <libnemo-private/nemo-global-preferences.h>
#include <libnemo-private/nemo-instances.h>

#include "test-scratch.h"
#include "test-check.h"

#define TABS_INTERFACE "org.NemoAnywhere.Tabs"
/* GLib's own name for it, in gdbusprivate.c. */
#define BUS_ADDRESS_MAPPING L"DBusDaemonAddressInfo"

/* A copy under wine takes a while to get a window up. */
#define START_SECONDS 45
#define WAIT_SECONDS  20

static const char *app_exe;
static char *root, *app_config, *copies_temp;
static gboolean bus_was_up;
static GArray *watched;	/* DWORD pids: the copies and this test */
static gboolean bus_seen;

static gint64
deadline (int seconds)
{
	return g_get_monotonic_time () + (gint64) (seconds * test_slowness () * G_USEC_PER_SEC);
}

static void
spin (int ms)
{
	gint64 end = g_get_monotonic_time () + ms * G_TIME_SPAN_MILLISECOND;

	while (g_get_monotonic_time () < end) {
		if (!g_main_context_iteration (NULL, FALSE)) {
			g_usleep (10000);
		}
	}
}

static gboolean
bus_address_published (void)
{
	HANDLE mapping = OpenFileMappingW (FILE_MAP_READ, FALSE, BUS_ADDRESS_MAPPING);

	if (mapping == NULL) {
		return FALSE;
	}
	CloseHandle (mapping);
	return TRUE;
}

/* A bus GLib starts is a child of whoever asked. */
static void
look_for_a_bus (const char *when)
{
	PROCESSENTRY32W entry = { 0 };
	HANDLE snapshot = CreateToolhelp32Snapshot (TH32CS_SNAPPROCESS, 0);

	entry.dwSize = sizeof entry;
	if (snapshot != INVALID_HANDLE_VALUE && Process32FirstW (snapshot, &entry)) {
		do {
			guint i;

			if (_wcsicmp (entry.szExeFile, L"gdbus.exe") != 0) {
				continue;
			}
			for (i = 0; i < watched->len; i++) {
				if (entry.th32ParentProcessID == g_array_index (watched, DWORD, i) && !bus_seen) {
					g_printerr ("FAIL %s: gdbus.exe %lu started by %lu\n", when,
					            (unsigned long) entry.th32ProcessID,
					            (unsigned long) entry.th32ParentProcessID);
					bus_seen = TRUE;
					failures++;
				}
			}
		} while (Process32NextW (snapshot, &entry));
	}
	if (snapshot != INVALID_HANDLE_VALUE) {
		CloseHandle (snapshot);
	}
	if (!bus_was_up && bus_address_published () && !bus_seen) {
		g_printerr ("FAIL %s: a session bus came up\n", when);
		bus_seen = TRUE;
		failures++;
	}
}

static void
wait_with_a_look (int ms, const char *when)
{
	spin (ms);
	look_for_a_bus (when);
}

typedef struct {
	GSubprocess *process;
	DWORD pid;
	gboolean exited;
	int status;
} Copy;

static void
copy_exited (GObject *source, GAsyncResult *result, gpointer data)
{
	Copy *copy = data;

	g_subprocess_wait_finish (G_SUBPROCESS (source), result, NULL);
	copy->status = g_subprocess_get_exit_status (G_SUBPROCESS (source));
	copy->exited = TRUE;
}

static Copy *
start_copy (const char *log_name, const char *first, const char *second)
{
	GSubprocessLauncher *launcher = g_subprocess_launcher_new (G_SUBPROCESS_FLAGS_STDOUT_PIPE |
	                                                           G_SUBPROCESS_FLAGS_STDERR_MERGE);
	g_autofree char *log = g_build_filename (root, log_name, NULL);
	g_autoptr (GFile) log_file = g_file_new_for_path (log);
	GFileOutputStream *log_out;
	GError *error = NULL;
	Copy *copy = g_new0 (Copy, 1);
	char **env = g_get_environ ();

	/* The app has to keep itself off the bus; the suite's own setting would
	 * hide that. */
	env = g_environ_unsetenv (env, "DBUS_SESSION_BUS_ADDRESS");
	env = g_environ_setenv (env, "APPDATA", app_config, TRUE);
	env = g_environ_setenv (env, "LOCALAPPDATA", app_config, TRUE);
	env = g_environ_setenv (env, "HOME", app_config, TRUE);
	env = g_environ_setenv (env, "TEMP", copies_temp, TRUE);
	env = g_environ_setenv (env, "TMP", copies_temp, TRUE);
	env = g_environ_setenv (env, "NEMO_NO_CRASH_DIALOG", "1", TRUE);
	g_subprocess_launcher_set_environ (launcher, env);
	g_strfreev (env);

	copy->process = g_subprocess_launcher_spawn (launcher, &error, app_exe, first, second, NULL);
	g_object_unref (launcher);
	if (copy->process == NULL) {
		g_printerr ("FAIL: could not start %s: %s\n", app_exe, error->message);
		g_clear_error (&error);
		failures++;
		g_free (copy);
		return NULL;
	}
	log_out = g_file_replace (log_file, NULL, FALSE, G_FILE_CREATE_NONE, NULL, NULL);
	if (log_out != NULL) {
		g_output_stream_splice_async (G_OUTPUT_STREAM (log_out), g_subprocess_get_stdout_pipe (copy->process),
		                              G_OUTPUT_STREAM_SPLICE_CLOSE_SOURCE | G_OUTPUT_STREAM_SPLICE_CLOSE_TARGET,
		                              G_PRIORITY_DEFAULT, NULL, NULL, NULL);
		g_object_unref (log_out);
	}
	copy->pid = (DWORD) g_ascii_strtoull (g_subprocess_get_identifier (copy->process), NULL, 10);
	g_array_append_val (watched, copy->pid);
	g_subprocess_wait_async (copy->process, NULL, copy_exited, copy);

	return copy;
}

static gboolean
wait_for_exit (Copy *copy, int seconds)
{
	gint64 end = deadline (seconds);

	while (!copy->exited && g_get_monotonic_time () < end) {
		wait_with_a_look (100, "waiting for a copy to end");
	}
	return copy->exited;
}

static char *
listed_as (const Copy *copy)
{
	g_auto (GStrv) others = nemo_instances_list_others ();
	g_autofree char *tail = g_strdup_printf (".%lu", (unsigned long) copy->pid);
	int i;

	for (i = 0; others != NULL && others[i] != NULL; i++) {
		if (g_str_has_suffix (others[i], tail)) {
			return g_strdup (others[i]);
		}
	}
	return NULL;
}

/* The first window a copy lists: its id, and its title in *title. */
static guint32
first_window (const char *copy, char **title)
{
	GVariant *reply, *windows;
	guint32 id = 0;

	*title = NULL;
	reply = nemo_instances_call (copy, TABS_INTERFACE, "ListWindows", NULL,
	                             G_VARIANT_TYPE ("(a(uts))"), 2000, NULL);
	if (reply == NULL) {
		return 0;
	}
	windows = g_variant_get_child_value (reply, 0);
	if (g_variant_n_children (windows) > 0) {
		guint64 handle;

		g_variant_get_child (windows, 0, "(uts)", &id, &handle, title);
	}
	g_variant_unref (windows);
	g_variant_unref (reply);

	return id;
}

static gboolean
wait_for_title (const char *copy, const char *wanted, gboolean whole_path, int seconds)
{
	gint64 end = deadline (seconds);
	char *title = NULL;

	for (;;) {
		gboolean match;

		g_free (title);
		first_window (copy, &title);
		match = title != NULL && strstr (title, wanted) != NULL &&
		        (strstr (title, "nemo-copies-") != NULL) == whole_path;
		if (match) {
			g_free (title);
			return TRUE;
		}
		if (g_get_monotonic_time () >= end) {
			break;
		}
		wait_with_a_look (200, "waiting for a title");
	}
	g_printerr ("  %s: title %s, wanted %s%s\n", copy, title != NULL ? title : "(none)",
	            wanted, whole_path ? " as a whole path" : "");
	g_free (title);
	return FALSE;
}

static int
run_once (const char *option)
{
	Copy *copy = start_copy ("once.log", option, NULL);

	if (copy == NULL || !wait_for_exit (copy, WAIT_SECONDS * 3)) {
		return -1;
	}
	return copy->status;
}

/* What used to start a bus on its own, with nothing in the environment to
 * stop it. */
static void
check_no_bus_asked_for (void)
{
	g_autofree char *dir = g_build_filename (root, "actions", NULL);
	g_autofree char *path = g_build_filename (dir, "on-bus.nemo_action", NULL);
	g_autofree char *plain = g_build_filename (dir, "plain.nemo_action", NULL);
	g_autofree char *uri = g_filename_to_uri (root, NULL, NULL);
	NemoAction *on_bus, *always;
	NemoFile *parent;
	int cookie;

	g_mkdir_with_parents (dir, 0700);
	check (g_file_set_contents (path, "[Nemo Action]\nName=On bus\nExec=cmd.exe\n"
	                                  "Selection=any\nExtensions=any;\n"
	                                  "Conditions=dbus org.NemoAnywhere;\n", -1, NULL));
	check (g_file_set_contents (plain, "[Nemo Action]\nName=Plain\nExec=cmd.exe\n"
	                                   "Selection=any\nExtensions=any;\n", -1, NULL));

	g_unsetenv ("DBUS_SESSION_BUS_ADDRESS");
	on_bus = nemo_action_new ("on-bus", path);
	always = nemo_action_new ("plain", plain);
	cookie = nemo_inhibit_power_manager ("test");
	check (cookie == -1);
	if (cookie > 0) {
		nemo_uninhibit_power_manager (cookie);
	}

	/* The condition is worked out on an idle. */
	wait_with_a_look (500, "an action's dbus condition");
	parent = nemo_file_get_by_uri (uri);
	check (on_bus != NULL && always != NULL);
	if (on_bus != NULL && always != NULL) {
		nemo_action_update_display_state (on_bus, NULL, parent, FALSE, NULL);
		nemo_action_update_display_state (always, NULL, parent, FALSE, NULL);
		check (!gtk_action_get_visible (GTK_ACTION (on_bus)));
		check (gtk_action_get_visible (GTK_ACTION (always)));
	}
	g_clear_object (&on_bus);
	g_clear_object (&always);
	nemo_file_unref (parent);
	g_setenv ("DBUS_SESSION_BUS_ADDRESS", "disabled:", TRUE);
	wait_with_a_look (300, "after the dbus condition");
}

static void
show_logs (void)
{
	const char *logs[] = { "alpha.log", "beta.log", "once.log", "killed.log" };
	guint i;

	for (i = 0; i < G_N_ELEMENTS (logs); i++) {
		g_autofree char *path = g_build_filename (root, logs[i], NULL);
		g_autofree char *text = NULL;
		gsize length = 0;

		if (g_file_get_contents (path, &text, &length, NULL) && length > 0) {
			g_printerr ("--- %s, the end of it:\n%s\n", logs[i],
			            text + (length > 3000 ? length - 3000 : 0));
		}
	}
}

static void
check_nonce_files (void)
{
	GDir *dir = g_dir_open (copies_temp, 0, NULL);
	const char *name;

	while (dir != NULL && (name = g_dir_read_name (dir)) != NULL) {
		if (g_str_has_prefix (name, "gdbus-nonce-file")) {
			g_printerr ("FAIL: %s left in the copies' temp folder\n", name);
			failures++;
		}
	}
	if (dir != NULL) {
		g_dir_close (dir);
	}
}

int
main (int argc, char *argv[])
{
	g_autofree char *alpha = NULL, *beta = NULL, *gamma = NULL, *test_config = NULL;
	g_autofree char *alpha_name = NULL, *beta_name = NULL, *gamma_uri = NULL;
	g_autofree char *title = NULL;
	GApplication *app;
	Copy *a, *b, *killed;
	DWORD self = GetCurrentProcessId ();
	GError *error = NULL;
	GVariant *reply;
	guint32 window;
	int status;

	if (argc < 2) {
		g_printerr ("usage: %s <nemo-anywhere.exe>\n", argv[0]);
		return 1;
	}
	app_exe = argv[1];

	root = test_scratch_dir ("nemo-copies-XXXXXX", NULL);
	if (root == NULL) {
		g_printerr ("FAIL: no scratch folder\n");
		return 1;
	}
	app_config = g_build_filename (root, "app-config", NULL);
	test_config = g_build_filename (root, "test-config", NULL);
	copies_temp = g_build_filename (root, "temp", NULL);
	alpha = g_build_filename (root, "alpha-folder", NULL);
	beta = g_build_filename (root, "beta-folder", NULL);
	gamma = g_build_filename (root, "gamma-folder", NULL);
	g_mkdir_with_parents (app_config, 0700);
	g_mkdir_with_parents (test_config, 0700);
	g_mkdir_with_parents (copies_temp, 0700);
	g_mkdir_with_parents (alpha, 0700);
	g_mkdir_with_parents (beta, 0700);
	g_mkdir_with_parents (gamma, 0700);
	gamma_uri = g_filename_to_uri (gamma, NULL, NULL);
	test_scratch_point_config_at (test_config);

	/* As the app does, so this process stays off the bus too. */
	nemo_setup_runtime_environment ();
	if (!gtk_init_check (&argc, &argv)) {
		g_print ("SKIP: no display\n");
		return 77;
	}
	bus_was_up = bus_address_published ();
	if (bus_was_up) {
		g_print ("NOTE: another program's session bus is up, so only a bus started here counts\n");
	}
	watched = g_array_new (FALSE, FALSE, sizeof (DWORD));
	g_array_append_val (watched, self);

	nemo_global_preferences_init ();
	app = g_application_new ("org.NemoAnywhere.CopiesTest", G_APPLICATION_NON_UNIQUE);
	check (g_application_register (app, NULL, NULL));
	check_no_bus_asked_for ();

	a = start_copy ("alpha.log", alpha, NULL);
	b = start_copy ("beta.log", beta, NULL);
	if (a == NULL || b == NULL) {
		return 1;
	}

	/* Both listed, and each shows its folder. */
	for (gint64 end = deadline (START_SECONDS); g_get_monotonic_time () < end;) {
		g_free (alpha_name);
		g_free (beta_name);
		alpha_name = listed_as (a);
		beta_name = listed_as (b);
		if (alpha_name != NULL && beta_name != NULL) {
			break;
		}
		wait_with_a_look (250, "starting");
	}
	check (alpha_name != NULL);
	check (beta_name != NULL);
	if (alpha_name == NULL || beta_name == NULL) {
		goto end;
	}
	check (wait_for_title (alpha_name, "alpha-folder", FALSE, START_SECONDS));
	check (wait_for_title (beta_name, "beta-folder", FALSE, START_SECONDS));

	/* Another copy finds them as well. */
	status = run_once ("--reset");
	check (status == 1);

	/* A tab moves into beta's window, and is the one it shows. */
	window = first_window (beta_name, &title);
	check (window != 0);
	reply = nemo_instances_call (beta_name, TABS_INTERFACE, "TakeTab",
	                             g_variant_new ("(uss^asu)", window, gamma_uri, "",
	                                            (const char *const[]) { NULL }, 0),
	                             NULL, 5000, &error);
	if (reply == NULL) {
		g_printerr ("FAIL: TakeTab: %s\n", error->message);
		g_clear_error (&error);
		failures++;
	} else {
		g_variant_unref (reply);
	}
	check (wait_for_title (beta_name, "gamma-folder", FALSE, WAIT_SECONDS));

	/* A bad call is turned away, not run. */
	reply = nemo_instances_call (beta_name, TABS_INTERFACE, "TakeTab", g_variant_new ("(s)", "x"),
	                             NULL, 5000, &error);
	check (reply == NULL && error != NULL);
	g_clear_error (&error);
	g_clear_pointer (&reply, g_variant_unref);

	/* A settings change made here reaches both. */
	check (nemo_instances_start (app));
	nemo_config_set_boolean (nemo_preferences, NEMO_PREFERENCES_SHOW_FULL_PATH_TITLES, TRUE);
	check (wait_for_title (alpha_name, "alpha-folder", TRUE, WAIT_SECONDS));
	check (wait_for_title (beta_name, "gamma-folder", TRUE, WAIT_SECONDS));
	nemo_instances_stop ();

	/* Asked to quit, both go. */
	status = run_once ("--quit");
	check (status == 0);
	check (wait_for_exit (a, WAIT_SECONDS));
	check (wait_for_exit (b, WAIT_SECONDS));

	/* A killed copy leaves nothing that makes the next one think it is still
	 * running. */
	killed = start_copy ("killed.log", alpha, NULL);
	if (killed != NULL) {
		g_autofree char *name = NULL;

		for (gint64 end = deadline (START_SECONDS); name == NULL && g_get_monotonic_time () < end;) {
			name = listed_as (killed);
			if (name == NULL) {
				wait_with_a_look (250, "starting the copy to kill");
			}
		}
		check (name != NULL);
		g_subprocess_force_exit (killed->process);
		check (wait_for_exit (killed, WAIT_SECONDS));
		g_clear_pointer (&name, g_free);
		name = listed_as (killed);
		check (name == NULL);
		status = run_once ("--reset");
		check (status == 0);
	}

end:
	look_for_a_bus ("at the end");
	check_nonce_files ();
	if (a != NULL && !a->exited) {
		g_subprocess_force_exit (a->process);
	}
	if (b != NULL && !b->exited) {
		g_subprocess_force_exit (b->process);
	}
	/* Its folder watch would keep the scratch folder from going. */
	nemo_config_flush ();
	nemo_config_shutdown ();

	if (failures > 0) {
		show_logs ();
		g_printerr ("%d check(s) failed\n", failures);
		return 1;
	}
	g_print ("PASS\n");
	return 0;
}
