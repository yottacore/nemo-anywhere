/* Two launches are two processes that both stay up, a crash in one leaves the
 * others running, and --quit takes every one of them down. Nothing on the bus
 * may copy, move, trash or delete, and an Open hint of any shape is taken
 * without harm. --reset is refused while a copy runs, since that copy would
 * write its settings straight back, and once none does it takes the settings
 * file, bookmarks and their side file. Needs the built program (argv[1]) and a
 * display; without a display it skips. The crash, --quit and --reset halves
 * need a session bus, and start one if the environment has none. */

#include <config.h>

#include <gio/gio.h>
#include <glib/gstdio.h>

#include <signal.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

#include "test-scratch.h"
#include "test-check.h"

static GPid
launch (const char *exe, const char *arg)
{
	char *argv[] = { (char *) exe, (char *) arg, NULL };
	GPid pid = 0;
	GError *error = NULL;

	if (!g_spawn_async (NULL, argv, NULL, G_SPAWN_DO_NOT_REAP_CHILD,
	                    NULL, NULL, &pid, &error)) {
		g_printerr ("spawn: %s\n", error->message);
		g_error_free (error);
	}

	return pid;
}

/* Runs to the end; the exit status, or -1 when it would not start. */
static int
run (const char *exe, const char *arg)
{
	char *argv[] = { (char *) exe, (char *) arg, NULL };
	int status = -1;
	GError *error = NULL;

	if (!g_spawn_sync (NULL, argv, NULL, G_SPAWN_STDOUT_TO_DEV_NULL | G_SPAWN_STDERR_TO_DEV_NULL,
			   NULL, NULL, NULL, NULL, &status, &error)) {
		g_printerr ("spawn: %s\n", error->message);
		g_error_free (error);
		return -1;
	}

	return WIFEXITED (status) ? WEXITSTATUS (status) : -1;
}

static gboolean
alive (GPid pid)
{
	int status;

	return pid > 0 && waitpid (pid, &status, WNOHANG) == 0;
}

/* TRUE when the process went away inside the time. */
static gboolean
wait_gone (GPid pid, int seconds)
{
	int i;

	for (i = 0; i < seconds * 10; i++) {
		if (!alive (pid)) {
			return TRUE;
		}
		g_usleep (100 * 1000);
	}

	return FALSE;
}

/* Pid behind a unique bus name, or 0. */
static GPid
owner_pid (GDBusConnection *bus, const char *unique)
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

/* Unique name holding org.NemoAnywhere, or NULL if nobody does. */
static char *
name_owner (GDBusConnection *bus)
{
	GVariant *reply;
	char *owner = NULL;

	reply = g_dbus_connection_call_sync (bus,
	                                     "org.freedesktop.DBus", "/org/freedesktop/DBus",
	                                     "org.freedesktop.DBus", "GetNameOwner",
	                                     g_variant_new ("(s)", "org.NemoAnywhere"),
	                                     G_VARIANT_TYPE ("(s)"),
	                                     G_DBUS_CALL_FLAGS_NONE, -1, NULL, NULL);
	if (reply == NULL) {
		return NULL;
	}

	g_variant_get (reply, "(s)", &owner);
	g_variant_unref (reply);

	return owner;
}

/* Poll for the name to move somewhere other than was, up to seconds. */
static char *
wait_owner_change (GDBusConnection *bus, const char *was, int seconds)
{
	char *owner = NULL;
	int i;

	for (i = 0; i < seconds * 10; i++) {
		g_free (owner);
		owner = name_owner (bus);
		if (owner != NULL && g_strcmp0 (owner, was) != 0) {
			return owner;
		}
		g_usleep (100 * 1000);
	}

	return owner;
}

/* meson sets DBUS_SESSION_BUS_ADDRESS=disabled: for every test, so the bus
 * halves used to skip on each run and still say OK. Start a real bus and come
 * back in, rather than report a pass on a test that mostly did not happen.
 * The variable being set proves nothing, so this asks the bus instead. */
static char *
introspect (GDBusConnection *bus, const char *unique, const char *path)
{
	GVariant *reply;
	char *xml = NULL;

	reply = g_dbus_connection_call_sync (bus, unique, path,
					     "org.freedesktop.DBus.Introspectable", "Introspect",
					     NULL, G_VARIANT_TYPE ("(s)"),
					     G_DBUS_CALL_FLAGS_NONE, 5000, NULL, NULL);
	if (reply != NULL) {
		g_variant_get (reply, "(s)", &xml);
		g_variant_unref (reply);
	}
	return xml;
}

/* The Open hint used to be split on "=" with no check, so one without took the
   running copy down. */
static void
open_with_hint (GDBusConnection *bus, const char *unique, const char *folder, const char *hint)
{
	char *uri = g_filename_to_uri (folder, NULL, NULL);
	const char *uris[] = { uri, NULL };
	GVariant *reply;
	GError *error = NULL;

	reply = g_dbus_connection_call_sync (bus, unique, "/org/NemoAnywhere",
					     "org.gtk.Application", "Open",
					     g_variant_new_parsed ("(%^as, %s, @a{sv} {})", uris, hint),
					     NULL, G_DBUS_CALL_FLAGS_NONE, 5000, NULL, &error);
	if (reply == NULL) {
		g_printerr ("FAIL Open with hint \"%s\": %s\n", hint, error->message);
		failures++;
		g_error_free (error);
	} else {
		g_variant_unref (reply);
	}
	g_free (uri);
}

static char *
config_file (const char *root, const char *dir, const char *name)
{
	return g_build_filename (root, dir, name, NULL);
}

static void
write_config_file (const char *path, const char *text)
{
	char *dir = g_path_get_dirname (path);

	g_mkdir_with_parents (dir, 0700);
	check (g_file_set_contents (path, text, -1, NULL));
	g_free (dir);
}

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

	/* Only here if the exec failed, and then the bus halves skip as before. */
	g_free (relaunch);
	g_free (dbus_run);
}

int
main (int argc, char *argv[])
{
	const char *exe;
	char *tmp;
	GPid first, second, third = 0, quitter, doomed, survivor;
	GDBusConnection *bus;
	char *held = NULL, *owner = NULL;
	char *settings, *bookmarks, *bookmark_metadata;

	if (argc < 2) {
		g_printerr ("usage: %s <nemo-anywhere>\n", argv[0]);
		return 1;
	}
	exe = argv[1];

	if (g_getenv ("DISPLAY") == NULL && g_getenv ("WAYLAND_DISPLAY") == NULL) {
		g_print ("SKIP: no display\n");
		return 77;
	}

	ensure_session_bus (argc, argv);

	tmp = test_scratch_config_home ("nemo-instances-test-XXXXXX");
	settings = config_file (tmp, "nemo-anywhere", "settings.shcl");
	bookmarks = config_file (tmp, "gtk-3.0", "bookmarks");
	bookmark_metadata = config_file (tmp, "nemo-anywhere", "bookmark-metadata");

	first = launch (exe, tmp);
	second = launch (exe, tmp);
	check (first > 0 && second > 0);

	/* A second copy that handed its arguments over would be gone well inside
	 * this; both are meant to stay. */
	g_usleep (4 * G_USEC_PER_SEC);
	check (alive (first));
	check (alive (second));

	bus = g_bus_get_sync (G_BUS_TYPE_SESSION, NULL, NULL);
	if (bus == NULL) {
		g_print ("no session bus: crash isolation and --quit not checked\n");
		goto out;
	}

	/* Crash the copy holding the bus name. It is the one the others have
	 * something to inherit from, and the crash handler runs in it either way. */
	held = name_owner (bus);
	check (held != NULL);
	if (held == NULL) {
		goto out;
	}

	/* The file operations interface left over from the Nemo desktop let any
	 * program copy, move or empty the trash. Only asked about, never called,
	 * so a build that still has it cannot empty a trash here. */
	{
		const char *paths[] = { "/org/Nemo", "/org/NemoAnywhere", "/", NULL };
		int i;

		for (i = 0; paths[i] != NULL; i++) {
			char *xml = introspect (bus, held, paths[i]);

			check (xml == NULL || strstr (xml, "FileOperations") == NULL);
			check (xml == NULL || strstr (xml, "EmptyTrash") == NULL);
			check (xml == NULL || strstr (xml, "MoveURIs") == NULL);
			g_free (xml);
		}
	}

	{
		const char *hints[] = { "no-equals-sign", "", "=", "EXISTING_WINDOW", NULL };
		GPid holder = owner_pid (bus, held);
		int i;

		for (i = 0; hints[i] != NULL; i++) {
			open_with_hint (bus, held, tmp, hints[i]);
		}
		g_usleep (2 * G_USEC_PER_SEC);
		check (holder == first || holder == second);
		check (alive (holder));
	}

	/* Refused, and says so, while a copy holds the settings in memory. */
	write_config_file (settings, "hand.written: yes\n");
	check (run (exe, "--reset") != 0);
	check (g_file_test (settings, G_FILE_TEST_EXISTS));

	/* Anything but one of ours and there is nothing safe to signal: pid 0 is
	 * the whole process group, this test included. */
	doomed = owner_pid (bus, held);
	check (doomed == first || doomed == second);
	if (doomed != first && doomed != second) {
		goto out;
	}
	survivor = (doomed == first) ? second : first;

	kill (doomed, SIGSEGV);
	check (wait_gone (doomed, 20));

	/* Long enough for a sibling to have followed it down. */
	g_usleep (5 * G_USEC_PER_SEC);
	check (alive (survivor));

	/* And the survivor picks up the name the dead one was holding. */
	owner = wait_owner_change (bus, held, 20);
	check (owner != NULL);
	check (owner_pid (bus, owner) == survivor);

	/* Back to two, so --quit still has more than one to take down. */
	third = launch (exe, tmp);
	check (third > 0);
	g_usleep (4 * G_USEC_PER_SEC);
	check (alive (third));
	check (alive (survivor));

	quitter = launch (exe, "--quit");
	check (wait_gone (quitter, 20));
	check (wait_gone (survivor, 20));
	check (wait_gone (third, 20));

	/* With nothing running it goes ahead, hand-written lines and all. */
	write_config_file (settings, "hand.written: yes\n");
	write_config_file (bookmarks, "file:///tmp\n");
	write_config_file (bookmark_metadata, "[file:///tmp]\nemblems=\n");
	check (run (exe, "--reset") == 0);
	check (!g_file_test (settings, G_FILE_TEST_EXISTS));
	check (!g_file_test (bookmarks, G_FILE_TEST_EXISTS));
	check (!g_file_test (bookmark_metadata, G_FILE_TEST_EXISTS));

out:
	g_clear_object (&bus);
	g_free (held);
	g_free (owner);

	if (alive (first)) {
		kill (first, SIGTERM);
		wait_gone (first, 5);
	}
	if (alive (second)) {
		kill (second, SIGTERM);
		wait_gone (second, 5);
	}
	if (alive (third)) {
		kill (third, SIGTERM);
		wait_gone (third, 5);
	}
	g_spawn_close_pid (first);
	g_spawn_close_pid (second);
	if (third > 0) {
		g_spawn_close_pid (third);
	}

	g_free (bookmark_metadata);
	g_free (bookmarks);
	g_free (settings);
	g_free (tmp);

	if (failures == 0) {
		g_print ("OK\n");
	}

	return failures == 0 ? 0 : 1;
}
