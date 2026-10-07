/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */

/* test-nemo-session-bus-win32.c - the session bus leaves nothing in TEMP.

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

/* GLib's bus on Windows left a nonce file in TEMP each time it ended. The bus
 * exe given is started the way GLib starts it, with TEMP pointed at a scratch
 * folder. 2 clients queue on the app's name the way copies of the app do; the
 * first leaves and the second gets the name. Once the bus ends nothing of it
 * may be left. Then a bus is killed, which leaves its file, and the next bus
 * has to clear it. */

#include <config.h>

#include <string.h>

#include <gio/gio.h>
#include <glib.h>
#include <glib/gstdio.h>
#include <windows.h>

#include "test-scratch.h"
#include "test-check.h"

/* GLib's own names, in gdbusprivate.c. */
#define ADDRESS_MAPPING L"DBusDaemonAddressInfo"
#define RUN_SESSION_BUS L"_win32_run_session_bus"

#define APP_NAME "org.NemoAnywhere"
#define WAIT_SECONDS 15

static const char *bus_exe;

static gint64
deadline (void)
{
	return g_get_monotonic_time () + (gint64) (WAIT_SECONDS * test_slowness () * G_USEC_PER_SEC);
}

static char *
published_address (void)
{
	HANDLE mapping = OpenFileMappingW (FILE_MAP_READ, FALSE, ADDRESS_MAPPING);
	char *address = NULL;
	const char *view;

	if (mapping == NULL) {
		return NULL;
	}
	view = MapViewOfFile (mapping, FILE_MAP_READ, 0, 0, 0);
	if (view != NULL) {
		address = g_strdup (view);
		UnmapViewOfFile (view);
	}
	CloseHandle (mapping);
	return address;
}

/* With the flags GLib starts it with. */
static gboolean
start_bus (PROCESS_INFORMATION *process)
{
	g_autofree wchar_t *wexe = g_utf8_to_utf16 (bus_exe, -1, NULL, NULL, NULL);
	g_autofree wchar_t *command = NULL;
	STARTUPINFOW startup = { 0 };

	memset (process, 0, sizeof *process);
	if (wexe == NULL) {
		return FALSE;
	}
	command = g_new0 (wchar_t, wcslen (wexe) + wcslen (RUN_SESSION_BUS) + 4);
	wcscpy (command, L"\"");
	wcscat (command, wexe);
	wcscat (command, L"\" ");
	wcscat (command, RUN_SESSION_BUS);

	startup.cb = sizeof startup;
	if (!CreateProcessW (wexe, command, NULL, NULL, FALSE, CREATE_NO_WINDOW | DETACHED_PROCESS,
			     NULL, NULL, &startup, process)) {
		return FALSE;
	}
	CloseHandle (process->hThread);
	return TRUE;
}

static char *
wait_for_address (HANDLE bus)
{
	for (gint64 end = deadline (); g_get_monotonic_time () < end; g_usleep (50000)) {
		g_autofree char *address = published_address ();

		if (address != NULL && g_str_has_prefix (address, "nonce-tcp:")) {
			return g_steal_pointer (&address);
		}
		if (WaitForSingleObject (bus, 0) == WAIT_OBJECT_0) {
			break;
		}
	}
	return NULL;
}

static char *
nonce_file (const char *address)
{
	const char *start = strstr (address, "noncefile=");
	g_autofree char *escaped = NULL;
	const char *end;

	if (start == NULL) {
		return NULL;
	}
	start += strlen ("noncefile=");
	end = strchr (start, ',');
	escaped = end != NULL ? g_strndup (start, end - start) : g_strdup (start);
	return g_uri_unescape_string (escaped, NULL);
}

static GDBusConnection *
join (const char *address)
{
	GError *error = NULL;
	GDBusConnection *connection;
	GVariant *reply;

	connection = g_dbus_connection_new_for_address_sync (address,
							     G_DBUS_CONNECTION_FLAGS_AUTHENTICATION_CLIENT |
							     G_DBUS_CONNECTION_FLAGS_MESSAGE_BUS_CONNECTION,
							     NULL, NULL, &error);
	if (connection == NULL) {
		g_printerr ("no connection: %s\n", error->message);
		g_clear_error (&error);
		return NULL;
	}

	reply = g_dbus_connection_call_sync (connection, "org.freedesktop.DBus", "/org/freedesktop/DBus",
					     "org.freedesktop.DBus", "RequestName",
					     g_variant_new ("(su)", APP_NAME, 0), G_VARIANT_TYPE ("(u)"),
					     G_DBUS_CALL_FLAGS_NONE, -1, NULL, &error);
	if (reply == NULL) {
		g_printerr ("no place in the queue: %s\n", error->message);
		g_clear_error (&error);
	} else {
		g_variant_unref (reply);
	}
	return connection;
}

/* GLib's bus answers ListQueuedOwners wrong, so the owner is asked instead. */
static char *
owner (GDBusConnection *connection)
{
	g_autoptr (GVariant) reply = NULL;
	char *name = NULL;

	reply = g_dbus_connection_call_sync (connection, "org.freedesktop.DBus", "/org/freedesktop/DBus",
					     "org.freedesktop.DBus", "GetNameOwner",
					     g_variant_new ("(s)", APP_NAME), G_VARIANT_TYPE ("(s)"),
					     G_DBUS_CALL_FLAGS_NONE, -1, NULL, NULL);
	if (reply != NULL) {
		g_variant_get (reply, "(s)", &name);
	}
	return name;
}

static void
leave (GDBusConnection *connection)
{
	if (connection != NULL) {
		g_dbus_connection_close_sync (connection, NULL, NULL);
		g_object_unref (connection);
	}
}

static gboolean
ended (HANDLE bus)
{
	gint64 left = (deadline () - g_get_monotonic_time ()) / 1000;

	return WaitForSingleObject (bus, (DWORD) MAX (left, 0)) == WAIT_OBJECT_0;
}

/* What is in a folder, by name, to say what was left. */
static char *
contents (const char *folder)
{
	g_autofree wchar_t *pattern = NULL;
	g_autofree char *wild = g_build_filename (folder, "*", NULL);
	GString *names = g_string_new (NULL);
	WIN32_FIND_DATAW found;
	HANDLE walk;

	pattern = g_utf8_to_utf16 (wild, -1, NULL, NULL, NULL);
	walk = pattern != NULL ? FindFirstFileW (pattern, &found) : INVALID_HANDLE_VALUE;
	if (walk != INVALID_HANDLE_VALUE) {
		do {
			g_autofree char *name = g_utf16_to_utf8 (found.cFileName, -1, NULL, NULL, NULL);

			if (name != NULL && strcmp (name, ".") != 0 && strcmp (name, "..") != 0) {
				g_string_append_printf (names, "%s%s", names->len > 0 ? " " : "", name);
			}
		} while (FindNextFileW (walk, &found));
		FindClose (walk);
	}
	return g_string_free (names, FALSE);
}

static void
check_nothing_left (const char *folder, const char *when)
{
	g_autofree char *left = contents (folder);

	if (*left != '\0') {
		g_printerr ("%s, TEMP still has: %s\n", when, left);
	}
	check (*left == '\0');
}

static void
quit_in_turn (const char *temp)
{
	PROCESS_INFORMATION bus;
	g_autofree char *address = NULL;
	g_autofree char *nonce = NULL;
	GDBusConnection *first, *second;
	g_autofree char *first_id = NULL;
	g_autofree char *second_id = NULL;
	g_autofree char *before = NULL;
	char *after = NULL;

	check (start_bus (&bus));
	if (bus.hProcess == NULL) {
		return;
	}
	address = wait_for_address (bus.hProcess);
	check (address != NULL);
	if (address == NULL) {
		TerminateProcess (bus.hProcess, 1);
		CloseHandle (bus.hProcess);
		return;
	}
	nonce = nonce_file (address);
	check (nonce != NULL && g_file_test (nonce, G_FILE_TEST_IS_REGULAR));

	first = join (address);
	second = join (address);
	check (first != NULL && second != NULL);
	if (first == NULL || second == NULL) {
		leave (first);
		leave (second);
		TerminateProcess (bus.hProcess, 1);
		CloseHandle (bus.hProcess);
		return;
	}
	first_id = g_strdup (g_dbus_connection_get_unique_name (first));
	second_id = g_strdup (g_dbus_connection_get_unique_name (second));
	before = owner (second);
	check (g_strcmp0 (before, first_id) == 0);

	/* The first copy quits; the second, next in the queue, takes the name. */
	leave (first);
	for (gint64 end = deadline (); g_get_monotonic_time () < end; g_usleep (50000)) {
		g_free (after);
		after = owner (second);
		if (g_strcmp0 (after, second_id) == 0) {
			break;
		}
	}
	check (g_strcmp0 (after, second_id) == 0);
	g_free (after);
	leave (second);

	check (ended (bus.hProcess));
	CloseHandle (bus.hProcess);

	check (nonce == NULL || !g_file_test (nonce, G_FILE_TEST_EXISTS));
	check_nothing_left (temp, "after a bus ended");
}

static void
killed_then_next (const char *temp)
{
	PROCESS_INFORMATION bus;
	g_autofree char *address = NULL;
	g_autofree char *nonce = NULL;
	g_autofree char *next_address = NULL;

	check (start_bus (&bus));
	if (bus.hProcess == NULL) {
		return;
	}
	address = wait_for_address (bus.hProcess);
	check (address != NULL);
	TerminateProcess (bus.hProcess, 1);
	WaitForSingleObject (bus.hProcess, INFINITE);
	CloseHandle (bus.hProcess);
	if (address == NULL) {
		return;
	}
	nonce = nonce_file (address);
	check (nonce != NULL && g_file_test (nonce, G_FILE_TEST_IS_REGULAR));

	check (start_bus (&bus));
	if (bus.hProcess == NULL) {
		return;
	}
	next_address = wait_for_address (bus.hProcess);
	check (next_address != NULL);
	if (next_address != NULL) {
		leave (join (next_address));
	} else {
		TerminateProcess (bus.hProcess, 1);
	}
	check (ended (bus.hProcess));
	CloseHandle (bus.hProcess);

	check_nothing_left (temp, "after a bus was killed and the next one ended");
}

int
main (int argc, char **argv)
{
	g_autofree char *temp = NULL;
	g_autofree char *running = NULL;

	if (argc < 2) {
		g_printerr ("usage: %s <bus exe>\n", argv[0]);
		return 77;
	}
	bus_exe = argv[1];

	/* One bus per session, so a bus some other program started would answer. */
	running = published_address ();
	if (running != NULL) {
		g_printerr ("a session bus is already up here (%s)\n", running);
		return 77;
	}

	temp = test_scratch_dir ("nemo-session-bus-XXXXXX", NULL);
	if (temp == NULL) {
		return 1;
	}
	g_setenv ("TEMP", temp, TRUE);
	g_unsetenv ("G_TEST_TMPDIR");

	quit_in_turn (temp);
	killed_then_next (temp);

	if (failures == 0) {
		g_print ("OK\n");
	}
	return failures == 0 ? 0 : 1;
}
