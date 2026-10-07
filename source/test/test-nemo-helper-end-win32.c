/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */

/* test-nemo-helper-end-win32.c - a helper the app started ends with the app.

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

/* A packed helper stuck on the packer's error box stayed up for good once the
 * app was gone. Here a copy of this program plays the app: it starts the fake
 * tool, which hangs, says the tool's PID, and then quits or is killed. A
 * thumbnailer-style run and a tool with its output read have to end with it,
 * either way. A user's own console program, started for an action, has to
 * outlive it, as it does on Linux. */

#include <config.h>

#include <fcntl.h>
#include <io.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <glib.h>
#include <glib/gstdio.h>
#include <windows.h>
#include <tlhelp32.h>

#include <libnemo-private/nemo-launch-win32.h>

#include "test-scratch.h"
#include "test-check.h"

#define FIND_SECONDS 10
#define END_SECONDS 5

static const char *tool;

/* The newest process @parent started under the name @name, or 0. */
static DWORD
child_named (DWORD parent, const wchar_t *name)
{
	HANDLE snapshot = CreateToolhelp32Snapshot (TH32CS_SNAPPROCESS, 0);
	PROCESSENTRY32W entry;
	DWORD found = 0;

	if (snapshot == INVALID_HANDLE_VALUE) {
		return 0;
	}

	entry.dwSize = sizeof entry;
	for (BOOL more = Process32FirstW (snapshot, &entry); more; more = Process32NextW (snapshot, &entry)) {
		if (entry.th32ParentProcessID == parent && _wcsicmp (entry.szExeFile, name) == 0) {
			found = entry.th32ProcessID;
		}
	}
	CloseHandle (snapshot);
	return found;
}

static gpointer
pipe_thread (G_GNUC_UNUSED gpointer data)
{
	const gchar *argv[] = { tool, NULL };

	nemo_launch_win32_pipe (argv, NULL, 60, NULL, NULL, NULL);
	return NULL;
}

/* The app's side. Prints the tool's PID, or 0, then quits on "exit" or waits
 * to be killed on anything else. */
static int
play_app (const char *how)
{
	const gchar *argv[] = { tool, NULL };
	g_autofree char *base = g_path_get_basename (tool);
	g_autofree wchar_t *name = g_utf8_to_utf16 (base, -1, NULL, NULL, NULL);
	NemoLaunchWin32Child *child = NULL;
	GError *error = NULL;
	char line[16] = "";
	DWORD pid = 0;

	if (strcmp (how, "pipe") == 0) {
		g_thread_unref (g_thread_new ("pipe", pipe_thread, NULL));
	} else if (strcmp (how, "tool") == 0) {
		child = nemo_launch_win32_child_start (argv, NULL, G_SUBPROCESS_FLAGS_STDOUT_PIPE, &error);
	} else {
		nemo_launch_win32_spawn (argv, FALSE, &error);
	}
	if (error != NULL) {
		g_printerr ("could not start the tool: %s\n", error->message);
		g_clear_error (&error);
	}

	for (gint64 end = g_get_monotonic_time () + FIND_SECONDS * G_USEC_PER_SEC;
	     pid == 0 && g_get_monotonic_time () < end; ) {
		pid = child_named (GetCurrentProcessId (), name);
		if (pid == 0) {
			g_usleep (20000);
		}
	}

	printf ("%lu\n", (unsigned long) pid);
	fflush (stdout);

	/* The tool is left running on purpose, child and all. */
	(void) child;
	if (fgets (line, sizeof line, stdin) == NULL || strncmp (line, "exit", 4) != 0) {
		Sleep (INFINITE);
	}
	return 0;
}

typedef struct {
	PROCESS_INFORMATION process;
	HANDLE to_app;
	FILE *from_app;
} App;

static gboolean
start_app (App *app, const char *how)
{
	wchar_t exe[MAX_PATH];
	g_autofree wchar_t *whow = g_utf8_to_utf16 (how, -1, NULL, NULL, NULL);
	g_autofree wchar_t *wtool = g_utf8_to_utf16 (tool, -1, NULL, NULL, NULL);
	g_autofree wchar_t *command = NULL;
	SECURITY_ATTRIBUTES inherit = { sizeof inherit, NULL, TRUE };
	STARTUPINFOW startup = { 0 };
	HANDLE app_in, app_out, from_app;
	size_t size;

	memset (app, 0, sizeof *app);
	if (GetModuleFileNameW (NULL, exe, MAX_PATH) == 0 || whow == NULL || wtool == NULL ||
	    !CreatePipe (&app_in, &app->to_app, &inherit, 0)) {
		return FALSE;
	}
	if (!CreatePipe (&from_app, &app_out, &inherit, 0)) {
		CloseHandle (app_in);
		CloseHandle (app->to_app);
		return FALSE;
	}
	SetHandleInformation (app->to_app, HANDLE_FLAG_INHERIT, 0);
	SetHandleInformation (from_app, HANDLE_FLAG_INHERIT, 0);

	size = wcslen (exe) + wcslen (whow) + wcslen (wtool) + 32;
	command = g_new0 (wchar_t, size);
	_snwprintf (command, size, L"\"%ls\" --app %ls \"%ls\"", exe, whow, wtool);
	command[size - 1] = 0;

	startup.cb = sizeof startup;
	startup.dwFlags = STARTF_USESTDHANDLES;
	startup.hStdInput = app_in;
	startup.hStdOutput = app_out;
	startup.hStdError = GetStdHandle (STD_ERROR_HANDLE);

	if (!CreateProcessW (exe, command, NULL, NULL, TRUE, CREATE_NO_WINDOW, NULL, NULL, &startup, &app->process)) {
		CloseHandle (app_in);
		CloseHandle (app_out);
		CloseHandle (from_app);
		CloseHandle (app->to_app);
		return FALSE;
	}
	CloseHandle (app_in);
	CloseHandle (app_out);
	app->from_app = _fdopen (_open_osfhandle ((intptr_t) from_app, 0), "r");
	return app->from_app != NULL;
}

/* The tool's process as the app copy reports it, or NULL. */
static HANDLE
tool_of (App *app)
{
	char line[32];
	unsigned long pid;

	if (fgets (line, sizeof line, app->from_app) == NULL) {
		return NULL;
	}
	pid = strtoul (line, NULL, 10);
	return pid != 0 ? OpenProcess (SYNCHRONIZE | PROCESS_TERMINATE, FALSE, (DWORD) pid) : NULL;
}

static void
end_app (App *app, gboolean quit)
{
	DWORD written;

	if (quit) {
		WriteFile (app->to_app, "exit\n", 5, &written, NULL);
	} else {
		TerminateProcess (app->process.hProcess, 1);
	}
	check (WaitForSingleObject (app->process.hProcess, (DWORD) (END_SECONDS * 1000 * test_slowness ())) == WAIT_OBJECT_0);

	CloseHandle (app->to_app);
	fclose (app->from_app);
	CloseHandle (app->process.hThread);
	CloseHandle (app->process.hProcess);
}

static void
check_ends (const char *how, gboolean quit)
{
	App app;
	HANDLE helper;

	g_print ("%s, app %s\n", how, quit ? "quits" : "killed");
	check (start_app (&app, how));
	if (app.from_app == NULL) {
		return;
	}
	helper = tool_of (&app);
	check (helper != NULL);
	end_app (&app, quit);
	if (helper == NULL) {
		return;
	}

	check (WaitForSingleObject (helper, (DWORD) (END_SECONDS * 1000 * test_slowness ())) == WAIT_OBJECT_0);
	TerminateProcess (helper, 1);
	CloseHandle (helper);
}

static void
check_outlives (void)
{
	App app;
	HANDLE program;

	g_print ("user program, app quits\n");
	check (start_app (&app, "user"));
	if (app.from_app == NULL) {
		return;
	}
	program = tool_of (&app);
	check (program != NULL);
	end_app (&app, TRUE);
	if (program == NULL) {
		return;
	}

	check (WaitForSingleObject (program, 2000) == WAIT_TIMEOUT);
	TerminateProcess (program, 1);
	WaitForSingleObject (program, 5000);
	CloseHandle (program);
}

int
main (int argc, char *argv[])
{
	g_autoptr(GError) error = NULL;
	g_autofree char *scratch = NULL;

	if (argc == 4 && strcmp (argv[1], "--app") == 0) {
		tool = argv[3];
		return play_app (argv[2]);
	}

	if (argc < 2) {
		g_printerr ("usage: %s <test-fake-tool.exe>\n", argv[0]);
		return 77;
	}
	tool = argv[1];

	scratch = test_scratch_dir ("nemo-helper-end-XXXXXX", &error);
	if (scratch == NULL) {
		g_printerr ("no scratch dir: %s\n", error->message);
		return 1;
	}
	g_setenv ("NEMO_FAKE_TOOL_DIR", scratch, TRUE);
	g_setenv ("NEMO_FAKE_TOOL_SLEEP", "1", TRUE);

	check_ends ("pipe", TRUE);
	check_ends ("pipe", FALSE);
	check_ends ("tool", TRUE);
	check_ends ("tool", FALSE);
	check_outlives ();

	return failures == 0 ? 0 : 1;
}
