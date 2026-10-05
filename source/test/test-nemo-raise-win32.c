/* A tab moved to another window hands that window's process the right to come
 * to the front. Only that process may get it: any other one stealing the
 * foreground while the user drags is what this guards against.
 *
 * Two copies of this program each show a window. This one takes the
 * foreground and holds it, so neither copy can take it on its own, and then
 * hands the right to one of them by its window handle.
 *
 * Where this one cannot take the foreground at all (no desktop, as over ssh),
 * or a copy can take it with no right handed over, it cannot tell, and exits
 * 77. */

#include <config.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <glib.h>

#include <windows.h>

#include "nemo-window-at-point.h"
#include "test-check.h"

#define TEST_SKIPPED 77
#define REPLY_TIMEOUT_MS 5000
#define RUN_TIMEOUT_MS 60000
#define WM_COMMAND_LINE (WM_APP + 1)

typedef struct {
	PROCESS_INFORMATION process;
	HANDLE to_child;
	HANDLE from_child;
	guint64 handle;
} Copy;

static HWND
make_window (const wchar_t *title, int x, int y)
{
	WNDCLASSW window_class = { 0 };
	HWND window;

	window_class.lpfnWndProc = DefWindowProcW;
	window_class.hInstance = GetModuleHandleW (NULL);
	window_class.lpszClassName = L"NemoRaiseTest";
	window_class.hbrBackground = (HBRUSH) (COLOR_WINDOW + 1);
	RegisterClassW (&window_class);

	window = CreateWindowExW (0, L"NemoRaiseTest", title, WS_OVERLAPPEDWINDOW,
	                          x, y, 320, 160, NULL, NULL, window_class.hInstance, NULL);
	if (window != NULL) {
		ShowWindow (window, SW_SHOW);
		UpdateWindow (window);
	}

	return window;
}

static gboolean
try_raise (HWND window)
{
	SetForegroundWindow (window);
	return GetForegroundWindow () == window;
}

static void
write_line (HANDLE out, const char *line)
{
	DWORD written;

	WriteFile (out, line, (DWORD) strlen (line), &written, NULL);
}

/* One letter per command from the test, read on a thread of its own so the
   window's thread is left free to pump messages. */
static DWORD WINAPI
read_commands (LPVOID data)
{
	HWND window = data;
	HANDLE in = GetStdHandle (STD_INPUT_HANDLE);
	char ch;
	DWORD got;

	while (ReadFile (in, &ch, 1, &got, NULL) && got == 1 && ch != 'q') {
		PostMessageW (window, WM_COMMAND_LINE, (WPARAM) ch, 0);
	}
	PostMessageW (window, WM_COMMAND_LINE, 'q', 0);

	return 0;
}

static int
run_copy (int x)
{
	HANDLE out = GetStdHandle (STD_OUTPUT_HANDLE);
	HWND window = make_window (L"nemo raise test copy", x, 300);
	char line[64];
	MSG msg;

	if (window == NULL) {
		write_line (out, "0\n");
		return EXIT_FAILURE;
	}

	/* A process may take the foreground once on its first window, if it was
	   started while its parent had it. Spend that here, before the test
	   starts counting. */
	try_raise (window);

	g_snprintf (line, sizeof line, "%" G_GUINT64_FORMAT "\n", (guint64) (guintptr) window);
	write_line (out, line);
	CloseHandle (CreateThread (NULL, 0, read_commands, window, 0, NULL));

	while (GetMessageW (&msg, NULL, 0, 0) > 0) {
		if (msg.hwnd != window || msg.message != WM_COMMAND_LINE) {
			TranslateMessage (&msg);
			DispatchMessageW (&msg);
		} else if (msg.wParam == 'r') {
			write_line (out, try_raise (window) ? "1\n" : "0\n");
		} else {
			break;
		}
	}
	DestroyWindow (window);

	return EXIT_SUCCESS;
}

/* A foreground window that stops taking messages counts as hung, and Windows
   then lets anyone past it. */
static void
pump (DWORD wait_ms)
{
	MSG msg;

	MsgWaitForMultipleObjects (0, NULL, FALSE, wait_ms, QS_ALLINPUT);
	while (PeekMessageW (&msg, NULL, 0, 0, PM_REMOVE)) {
		TranslateMessage (&msg);
		DispatchMessageW (&msg);
	}
}

/* One line back from a copy, or NULL when it says nothing in time. */
static char *
read_reply (Copy *copy)
{
	GString *line = g_string_new (NULL);
	ULONGLONG deadline = GetTickCount64 () + REPLY_TIMEOUT_MS;

	while (GetTickCount64 () < deadline) {
		DWORD waiting = 0, got;
		char ch;

		if (!PeekNamedPipe (copy->from_child, NULL, 0, NULL, &waiting, NULL)) {
			break;
		}
		if (waiting == 0) {
			pump (10);
			continue;
		}
		if (!ReadFile (copy->from_child, &ch, 1, &got, NULL) || got != 1) {
			break;
		}
		if (ch == '\n') {
			return g_string_free (line, FALSE);
		}
		if (ch != '\r') {
			g_string_append_c (line, ch);
		}
	}
	g_string_free (line, TRUE);

	return NULL;
}

static gboolean
start_copy (Copy *copy, int x)
{
	wchar_t exe[MAX_PATH];
	wchar_t command[MAX_PATH + 64];
	SECURITY_ATTRIBUTES inherit = { sizeof inherit, NULL, TRUE };
	STARTUPINFOW startup = { 0 };
	HANDLE child_in, child_out;
	char *reply;

	if (GetModuleFileNameW (NULL, exe, MAX_PATH) == 0 ||
	    !CreatePipe (&child_in, &copy->to_child, &inherit, 0) ||
	    !CreatePipe (&copy->from_child, &child_out, &inherit, 0)) {
		return FALSE;
	}
	SetHandleInformation (copy->to_child, HANDLE_FLAG_INHERIT, 0);
	SetHandleInformation (copy->from_child, HANDLE_FLAG_INHERIT, 0);

	_snwprintf (command, G_N_ELEMENTS (command), L"\"%ls\" --copy %d", exe, x);
	command[G_N_ELEMENTS (command) - 1] = 0;
	startup.cb = sizeof startup;
	startup.dwFlags = STARTF_USESTDHANDLES;
	startup.hStdInput = child_in;
	startup.hStdOutput = child_out;
	startup.hStdError = GetStdHandle (STD_ERROR_HANDLE);

	/* Processes on one console share its right to the foreground, so a copy
	   sharing this one's would be let in. */
	if (!CreateProcessW (exe, command, NULL, NULL, TRUE, DETACHED_PROCESS, NULL, NULL, &startup, &copy->process)) {
		return FALSE;
	}
	CloseHandle (child_in);
	CloseHandle (child_out);

	reply = read_reply (copy);
	copy->handle = reply != NULL ? g_ascii_strtoull (reply, NULL, 10) : 0;
	g_free (reply);

	return copy->handle != 0;
}

/* 1 for yes, 0 for no, -1 when the copy did not answer. */
static int
ask (Copy *copy, const char *command)
{
	DWORD written;
	char *reply;
	int yes;

	if (copy->process.hProcess == NULL || !WriteFile (copy->to_child, command, 1, &written, NULL)) {
		return -1;
	}
	reply = read_reply (copy);
	yes = reply == NULL ? -1 : (strcmp (reply, "1") == 0);
	g_free (reply);

	return yes;
}

static void
stop_copy (Copy *copy)
{
	DWORD written;

	if (copy->process.hProcess == NULL) {
		return;
	}
	WriteFile (copy->to_child, "q", 1, &written, NULL);
	if (WaitForSingleObject (copy->process.hProcess, 3000) != WAIT_OBJECT_0) {
		TerminateProcess (copy->process.hProcess, 1);
	}
	CloseHandle (copy->process.hProcess);
	CloseHandle (copy->process.hThread);
	CloseHandle (copy->to_child);
	CloseHandle (copy->from_child);
}

static DWORD WINAPI
watchdog (LPVOID data)
{
	Sleep (RUN_TIMEOUT_MS);
	fprintf (stderr, "FAIL: no result in %d ms\n", RUN_TIMEOUT_MS);
	fflush (stderr);
	ExitProcess (EXIT_FAILURE);

	return 0;
}

static void
finish (Copy *wanted, Copy *other, HWND own)
{
	stop_copy (wanted);
	stop_copy (other);
	if (own != NULL) {
		DestroyWindow (own);
	}
}

int
main (int argc, char *argv[])
{
	Copy wanted = { 0 }, other = { 0 };
	HWND own;

	if (argc == 3 && strcmp (argv[1], "--copy") == 0) {
		return run_copy (atoi (argv[2]));
	}

	CloseHandle (CreateThread (NULL, 0, watchdog, NULL, 0, NULL));

	/* The copies start before this one holds the foreground, or each would
	   be let in as started by it. */
	if (!start_copy (&wanted, 40) || !start_copy (&other, 400)) {
		fprintf (stderr, "FAIL: could not start the two copies\n");
		finish (&wanted, &other, NULL);
		return EXIT_FAILURE;
	}

	own = make_window (L"nemo raise test", 760, 300);
	if (own == NULL || !try_raise (own)) {
		printf ("SKIP: cannot take the foreground here (no desktop?)\n");
		finish (&wanted, &other, own);
		return TEST_SKIPPED;
	}

	/* A right left open to every process, as the old tab move did, outlives
	   the process that gave it. Handing it to one process ends it. */
	AllowSetForegroundWindow (GetCurrentProcessId ());

	if (ask (&other, "r") != 0 || ask (&wanted, "r") != 0 || GetForegroundWindow () != own) {
		printf ("SKIP: a copy took the foreground with no right handed to it\n");
		finish (&wanted, &other, own);
		return TEST_SKIPPED;
	}

	/* No window, no right for anybody. */
	nemo_window_allow_to_raise (0);
	check (ask (&wanted, "r") == 0);
	check (ask (&other, "r") == 0);

	/* The right goes to the owner of the handle and no one else. The other
	   copy asks first, so a right open to anyone would go to it. */
	nemo_window_allow_to_raise (wanted.handle);
	check (ask (&other, "r") == 0);
	check (ask (&wanted, "r") == 1);

	finish (&wanted, &other, own);
	if (failures > 0) {
		g_printerr ("%d check(s) failed\n", failures);
		return EXIT_FAILURE;
	}

	printf ("OK\n");
	return EXIT_SUCCESS;
}
