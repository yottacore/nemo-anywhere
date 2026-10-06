/* nemo-launch-win32.c - starting a program that is not ours
 *
 * The single-exe build hooks whatever it starts, so the child sees our packed
 * files instead of the real disk. No process flag avoids it: the hooks go in at
 * creation, before the child runs an instruction. The only way out is to let a
 * different process do the creating.
 *
 * Two of them can. The desktop's shell keeps the arguments and puts the new
 * window in front, but it drops the child to the ordinary integrity level and
 * refuses the call outright while we are elevated. The management service keeps
 * our token and always answers, but the window it starts opens behind and
 * cannot be raised afterwards. So the shell is asked first, the service catches
 * what the shell would not do, and a plain CreateProcess sits behind both.
 *
 * Copyright © 2026 t00mietum (CryptogID: ปʬϝღถɔ4რఠΔթะ9ƾǝu)
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published
 * by the Free Software Foundation.
 */

#include <config.h>

#define COBJMACROS

#include <string.h>
#include <windows.h>
#include <objbase.h>
#include <shlobj.h>
#include <shlguid.h>
#include <shldisp.h>
#include <exdisp.h>
#include <servprov.h>
#include <shellapi.h>
#include <wbemcli.h>

#include <gio/gio.h>
#include <gio/gwin32inputstream.h>
#include <glib/gi18n.h>

#include "nemo-launch-win32.h"

static void
release (gpointer com_object)
{
	if (com_object != NULL) {
		IUnknown_Release ((IUnknown *) com_object);
	}
}

/* S_FALSE means somebody got here first, which still needs balancing.
 * RPC_E_CHANGED_MODE means the thread is in the other apartment already; the
 * calls below work from there too, but must not be unbalanced. */
static gboolean
com_enter (gboolean *needs_leave)
{
	HRESULT hr = CoInitializeEx (NULL, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);

	*needs_leave = SUCCEEDED (hr);
	return SUCCEEDED (hr) || hr == RPC_E_CHANGED_MODE;
}

static VARIANT
bstr_variant (const gchar *text)
{
	VARIANT v;

	VariantInit (&v);

	if (text != NULL && text[0] != '\0') {
		wchar_t *wide = g_utf8_to_utf16 (text, -1, NULL, NULL, NULL);

		if (wide != NULL) {
			v.vt = VT_BSTR;
			v.bstrVal = SysAllocString (wide);
			g_free (wide);
		}
	}

	return v;
}

/* The Shell.Application object of the running desktop, which is what makes the
 * call happen in Explorer rather than here. Walking to it through the desktop's
 * own shell view is the only way to reach that instance; CoCreateInstance would
 * hand back one inside this process, hooks and all. */
static IShellDispatch2 *
desktop_shell (void)
{
	IShellWindows *windows = NULL;
	IDispatch *desktop = NULL;
	IServiceProvider *provider = NULL;
	IShellBrowser *browser = NULL;
	IShellView *view = NULL;
	IDispatch *background = NULL;
	IShellFolderViewDual *folder = NULL;
	IDispatch *application = NULL;
	IShellDispatch2 *shell = NULL;
	VARIANT nowhere;
	LONG hwnd = 0;

	if (FAILED (CoCreateInstance (&CLSID_ShellWindows, NULL, CLSCTX_ALL,
				      &IID_IShellWindows, (void **) &windows))) {
		return NULL;
	}

	VariantInit (&nowhere);

	if (SUCCEEDED (IShellWindows_FindWindowSW (windows, &nowhere, &nowhere, SWC_DESKTOP,
						   &hwnd, SWFO_NEEDDISPATCH, &desktop)) &&
	    desktop != NULL &&
	    SUCCEEDED (IDispatch_QueryInterface (desktop, &IID_IServiceProvider, (void **) &provider)) &&
	    SUCCEEDED (IServiceProvider_QueryService (provider, &SID_STopLevelBrowser,
						      &IID_IShellBrowser, (void **) &browser)) &&
	    SUCCEEDED (IShellBrowser_QueryActiveShellView (browser, &view)) &&
	    SUCCEEDED (IShellView_GetItemObject (view, SVGIO_BACKGROUND, &IID_IDispatch, (void **) &background)) &&
	    SUCCEEDED (IDispatch_QueryInterface (background, &IID_IShellFolderViewDual, (void **) &folder)) &&
	    SUCCEEDED (IShellFolderViewDual_get_Application (folder, &application))) {
		IDispatch_QueryInterface (application, &IID_IShellDispatch2, (void **) &shell);
	}

	release (application);
	release (folder);
	release (background);
	release (view);
	release (browser);
	release (provider);
	release (desktop);
	release (windows);

	return shell;
}

/* Explorer answers a file that is not there with a message box of its own, and
 * the call does not return until somebody dismisses it - which would hold the
 * thread that asked. Anything spelled as a path is checked here first; a bare
 * program name is still left for the shell to find. */
static gboolean
worth_asking_the_shell (const gchar *exe)
{
	if (strpbrk (exe, "\\/:") == NULL) {
		return TRUE;
	}

	return g_file_test (exe, G_FILE_TEST_EXISTS);
}

gboolean
nemo_launch_win32_via_shell (const gchar *exe,
			     const gchar *args,
			     const gchar *workdir)
{
	IShellDispatch2 *shell;
	VARIANT vargs, vdir, vverb, vshow;
	wchar_t *wexe;
	BSTR file;
	HRESULT hr;
	gboolean needs_leave;

	if (!worth_asking_the_shell (exe) || !com_enter (&needs_leave)) {
		return FALSE;
	}

	shell = desktop_shell ();
	if (shell == NULL) {
		if (needs_leave) {
			CoUninitialize ();
		}
		return FALSE;
	}

	wexe = g_utf8_to_utf16 (exe, -1, NULL, NULL, NULL);
	file = wexe != NULL ? SysAllocString (wexe) : NULL;

	vargs = bstr_variant (args);
	vdir = bstr_variant (workdir);
	VariantInit (&vverb);		/* whatever the type calls its default */
	VariantInit (&vshow);
	vshow.vt = VT_I4;
	vshow.lVal = SW_SHOWNORMAL;

	hr = file != NULL ? IShellDispatch2_ShellExecute (shell, file, vargs, vdir, vverb, vshow) : E_FAIL;

	VariantClear (&vargs);
	VariantClear (&vdir);
	SysFreeString (file);
	g_free (wexe);
	release (shell);

	if (needs_leave) {
		CoUninitialize ();
	}

	return SUCCEEDED (hr);
}

/* Win32_Process::Create. The provider host does the creating, so the child is
 * not ours, but it keeps our token and the window opens wherever it likes. */
gboolean
nemo_launch_win32_via_service (const gchar *command_line,
			       const gchar *workdir)
{
	IWbemLocator *locator = NULL;
	IWbemServices *services = NULL;
	IWbemClassObject *process = NULL;
	IWbemClassObject *signature = NULL;
	IWbemClassObject *arguments = NULL;
	IWbemClassObject *result = NULL;
	BSTR ns = NULL, class_name = NULL, method = NULL;
	VARIANT vline, vdir, vcode;
	gboolean started = FALSE;
	gboolean needs_leave;

	if (!com_enter (&needs_leave)) {
		return FALSE;
	}

	if (FAILED (CoCreateInstance (&CLSID_WbemLocator, NULL, CLSCTX_INPROC_SERVER,
				      &IID_IWbemLocator, (void **) &locator))) {
		goto out;
	}

	ns = SysAllocString (L"ROOT\\CIMV2");
	if (FAILED (IWbemLocator_ConnectServer (locator, ns, NULL, NULL, NULL, 0, NULL, NULL, &services))) {
		goto out;
	}

	/* Without impersonation the provider refuses to act on our behalf. */
	CoSetProxyBlanket ((IUnknown *) services, RPC_C_AUTHN_WINNT, RPC_C_AUTHZ_NONE, NULL,
			   RPC_C_AUTHN_LEVEL_CALL, RPC_C_IMP_LEVEL_IMPERSONATE, NULL, EOAC_NONE);

	class_name = SysAllocString (L"Win32_Process");
	method = SysAllocString (L"Create");

	if (FAILED (IWbemServices_GetObject (services, class_name, 0, NULL, &process, NULL)) ||
	    FAILED (IWbemClassObject_GetMethod (process, L"Create", 0, &signature, NULL)) ||
	    FAILED (IWbemClassObject_SpawnInstance (signature, 0, &arguments))) {
		goto out;
	}

	vline = bstr_variant (command_line);
	vdir = bstr_variant (workdir);
	IWbemClassObject_Put (arguments, L"CommandLine", 0, &vline, 0);
	if (vdir.vt == VT_BSTR) {
		IWbemClassObject_Put (arguments, L"CurrentDirectory", 0, &vdir, 0);
	}
	VariantClear (&vline);
	VariantClear (&vdir);

	if (SUCCEEDED (IWbemServices_ExecMethod (services, class_name, method, 0, NULL,
						 arguments, &result, NULL))) {
		VariantInit (&vcode);

		if (SUCCEEDED (IWbemClassObject_Get (result, L"ReturnValue", 0, &vcode, NULL, NULL))) {
			started = vcode.vt == VT_I4 && vcode.lVal == 0;
			if (!started) {
				g_warning ("Win32_Process::Create refused '%s' (%ld)",
					   command_line, (long) vcode.lVal);
			}
		}
		VariantClear (&vcode);
	}

out:
	SysFreeString (method);
	SysFreeString (class_name);
	SysFreeString (ns);
	release (result);
	release (arguments);
	release (signature);
	release (process);
	release (services);
	release (locator);

	if (needs_leave) {
		CoUninitialize ();
	}

	return started;
}

/* CREATE_DEFAULT_ERROR_MODE so the program does not inherit ours - a packed
 * build turns the crash dialog off for itself, and that has no business
 * reaching what it opens. */
static gboolean
direct_start (const gchar *command_line,
	      const gchar *workdir)
{
	wchar_t *wline = g_utf8_to_utf16 (command_line, -1, NULL, NULL, NULL);
	wchar_t *wdir = workdir != NULL ? g_utf8_to_utf16 (workdir, -1, NULL, NULL, NULL) : NULL;
	STARTUPINFOW startup;
	PROCESS_INFORMATION process;
	BOOL started;

	if (wline == NULL) {
		g_free (wdir);
		return FALSE;
	}

	memset (&startup, 0, sizeof startup);
	startup.cb = sizeof startup;
	startup.dwFlags = STARTF_USESHOWWINDOW;
	startup.wShowWindow = SW_SHOWNORMAL;

	started = CreateProcessW (NULL, wline, NULL, NULL, FALSE,
				  CREATE_DEFAULT_ERROR_MODE | CREATE_UNICODE_ENVIRONMENT,
				  NULL, wdir, &startup, &process);

	if (started) {
		CloseHandle (process.hThread);
		CloseHandle (process.hProcess);
	}

	g_free (wdir);
	g_free (wline);
	return started;
}

static void
set_failed (GError      **error,
	    const gchar  *what)
{
	gchar *reason = g_win32_error_message (GetLastError ());

	g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED,
		     _("Could not start \"%s\": %s"), what, reason);
	g_free (reason);
}

/* Returns: (transfer full): free with g_free, and @args too */
gchar *
nemo_launch_win32_split_command (const gchar  *command_line,
				 gchar       **args)
{
	const gchar *rest = NULL;
	const gchar *p;
	gchar *exe = NULL;

	*args = NULL;

	while (*command_line == ' ') {
		command_line++;
	}

	if (command_line[0] == '"') {
		const gchar *end = strchr (command_line + 1, '"');

		if (end != NULL) {
			exe = g_strndup (command_line + 1, end - command_line - 1);
			rest = end + 1;
		} else {
			exe = g_strdup (command_line + 1);
		}
	}

	/* An unquoted path may hold spaces, and the registry has plenty of those.
	 * The longest prefix that exists on disk wins, the way the shell reads it. */
	for (p = command_line; exe == NULL && (p = strchr (p, '.')) != NULL; p++) {
		if (g_ascii_strncasecmp (p, ".exe", 4) == 0 && (p[4] == '\0' || p[4] == ' ')) {
			gchar *candidate = g_strndup (command_line, p + 4 - command_line);

			if (g_file_test (candidate, G_FILE_TEST_EXISTS)) {
				exe = candidate;
				rest = p + 4;
				break;
			}
			g_free (candidate);
		}
	}

	if (exe == NULL) {
		p = strchr (command_line, ' ');

		if (p != NULL) {
			exe = g_strndup (command_line, p - command_line);
			rest = p;
		} else {
			exe = g_strdup (command_line);
		}
	}

	while (rest != NULL && *rest == ' ') {
		rest++;
	}

	if (rest != NULL && *rest != '\0') {
		*args = g_strdup (rest);
	}

	return exe;
}

gboolean
nemo_launch_win32_open_path (const gchar  *path,
			     const gchar  *workdir,
			     GError      **error)
{
	wchar_t *wpath, *wdir;
	gboolean started;

	g_return_val_if_fail (path != NULL, FALSE);

	if (nemo_launch_win32_via_shell (path, NULL, workdir)) {
		return TRUE;
	}

	/* Nothing else can resolve a type's default handler for us, so this one
	 * falls back to doing it in-process even though the child stays hooked. */
	wpath = g_utf8_to_utf16 (path, -1, NULL, NULL, NULL);
	wdir = workdir != NULL ? g_utf8_to_utf16 (workdir, -1, NULL, NULL, NULL) : NULL;
	started = wpath != NULL &&
		  (INT_PTR) ShellExecuteW (NULL, NULL, wpath, NULL, wdir, SW_SHOWNORMAL) > 32;
	g_free (wdir);
	g_free (wpath);

	if (!started) {
		set_failed (error, path);
	}

	return started;
}

gboolean
nemo_launch_win32_run (const gchar  *exe,
		       const gchar  *args,
		       const gchar  *workdir,
		       GError      **error)
{
	gchar *line;
	gboolean started;

	g_return_val_if_fail (exe != NULL, FALSE);

	if (nemo_launch_win32_via_shell (exe, args, workdir)) {
		return TRUE;
	}

	line = args != NULL && args[0] != '\0'
		? g_strdup_printf ("\"%s\" %s", exe, args)
		: g_strdup_printf ("\"%s\"", exe);

	started = nemo_launch_win32_via_service (line, workdir) || direct_start (line, workdir);

	if (!started) {
		set_failed (error, line);
	}

	g_free (line);
	return started;
}

gboolean
nemo_launch_win32_run_command (const gchar  *command_line,
			       const gchar  *workdir,
			       GError      **error)
{
	gchar *args = NULL;
	gchar *exe;
	gboolean started;

	g_return_val_if_fail (command_line != NULL, FALSE);

	exe = nemo_launch_win32_split_command (command_line, &args);
	started = nemo_launch_win32_via_shell (exe, args, workdir);
	g_free (args);
	g_free (exe);

	if (started) {
		return TRUE;
	}

	/* The line as it was built, not as it was split, so nothing about the
	 * quoting has to survive a round trip. */
	started = nemo_launch_win32_via_service (command_line, workdir) || direct_start (command_line, workdir);

	if (!started) {
		set_failed (error, command_line);
	}

	return started;
}

/* One argument the way the C runtime splits a command line back up. */
static void
append_argument (GString *line, const gchar *arg)
{
	const gchar *p;

	if (line->len > 0) {
		g_string_append_c (line, ' ');
	}

	if (arg[0] != '\0' && strpbrk (arg, " \t\n\v\"") == NULL) {
		g_string_append (line, arg);
		return;
	}

	g_string_append_c (line, '"');
	for (p = arg; ; p++) {
		gsize slashes = 0;

		while (*p == '\\') {
			slashes++;
			p++;
		}

		if (*p == '\0') {
			for (gsize i = 0; i < slashes * 2; i++) {
				g_string_append_c (line, '\\');
			}
			break;
		}

		if (*p == '"') {
			slashes = slashes * 2 + 1;
		}
		for (gsize i = 0; i < slashes; i++) {
			g_string_append_c (line, '\\');
		}
		g_string_append_c (line, *p);
	}
	g_string_append_c (line, '"');
}

/* Opened both ways, so one handle stands in for a missing stdin and for output
 * nobody reads. There is no console to pass on anyway. */
static HANDLE
open_nowhere (void)
{
	SECURITY_ATTRIBUTES inherit = { sizeof inherit, NULL, TRUE };

	return CreateFileW (L"NUL", GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE,
			    &inherit, OPEN_EXISTING, 0, NULL);
}

/* A pipe the program writes to. Ours is the overlapped end, so a read on it can
 * be cancelled; CreatePipe only makes ones that block until something comes. */
static gboolean
make_out_pipe (HANDLE *ours, HANDLE *theirs)
{
	static volatile LONG serial;
	SECURITY_ATTRIBUTES inherit = { sizeof inherit, NULL, TRUE };
	wchar_t name[80];

	_snwprintf (name, G_N_ELEMENTS (name), L"\\\\.\\pipe\\nemo-anywhere-%lu-%ld",
		    (unsigned long) GetCurrentProcessId (), (long) InterlockedIncrement (&serial));
	name[G_N_ELEMENTS (name) - 1] = L'\0';

	*theirs = INVALID_HANDLE_VALUE;
	*ours = CreateNamedPipeW (name,
				  PIPE_ACCESS_INBOUND | FILE_FLAG_OVERLAPPED | FILE_FLAG_FIRST_PIPE_INSTANCE,
				  PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT | PIPE_REJECT_REMOTE_CLIENTS,
				  1, 65536, 65536, 0, NULL);
	if (*ours == INVALID_HANDLE_VALUE) {
		return FALSE;
	}

	*theirs = CreateFileW (name, GENERIC_WRITE, 0, &inherit, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
	if (*theirs == INVALID_HANDLE_VALUE) {
		CloseHandle (*ours);
		*ours = INVALID_HANDLE_VALUE;
		return FALSE;
	}

	return TRUE;
}

static void
close_valid (HANDLE handle)
{
	if (handle != NULL && handle != INVALID_HANDLE_VALUE) {
		CloseHandle (handle);
	}
}

/* Started directly, so in the single-exe build it is hooked like anything else
 * started that way. Neither broker can hand over a pipe, and a tool that only
 * reads files and writes to us does not mind the hooks. GLib's own spawn goes
 * through a helper program, which gives a console tool a console window of its
 * own, and which never starts the tool at all from inside the single exe.
 *
 * Only the three handles given are inherited, so tools started at the same time
 * never hold each other's pipes open. */
static gboolean
spawn_hidden (const gchar * const  *argv,
	      const gchar          *workdir,
	      HANDLE                in,
	      HANDLE                out,
	      HANDLE                err,
	      HANDLE               *process_out,
	      GError              **error)
{
	LPPROC_THREAD_ATTRIBUTE_LIST attributes = NULL;
	SIZE_T attributes_size = 0;
	STARTUPINFOEXW startup;
	PROCESS_INFORMATION process;
	HANDLE given[3] = { in, out, err };
	HANDLE handed[3];
	guint n_handed = 0, i, j;
	GString *line;
	gchar *program;
	wchar_t *wexe, *wline, *wdir = NULL;
	gboolean started = FALSE;

	/* A bare name is found the way the rest of the app finds one, the exe's
	 * own folder before PATH. */
	program = g_path_is_absolute (argv[0]) ? g_strdup (argv[0]) : g_find_program_in_path (argv[0]);
	if (program == NULL) {
		SetLastError (ERROR_FILE_NOT_FOUND);
		set_failed (error, argv[0]);
		return FALSE;
	}

	line = g_string_new (NULL);
	append_argument (line, program);
	for (i = 1; argv[i] != NULL; i++) {
		append_argument (line, argv[i]);
	}
	wexe = g_utf8_to_utf16 (program, -1, NULL, NULL, NULL);
	wline = g_utf8_to_utf16 (line->str, -1, NULL, NULL, NULL);
	if (workdir != NULL) {
		wdir = g_utf8_to_utf16 (workdir, -1, NULL, NULL, NULL);
	}
	g_string_free (line, TRUE);

	/* The list refuses the same handle twice. */
	for (i = 0; i < G_N_ELEMENTS (given); i++) {
		for (j = 0; j < n_handed && handed[j] != given[i]; j++) {
		}
		if (j == n_handed) {
			handed[n_handed++] = given[i];
		}
	}

	InitializeProcThreadAttributeList (NULL, 1, 0, &attributes_size);
	attributes = g_malloc (attributes_size);

	if (wexe != NULL && wline != NULL && (workdir == NULL || wdir != NULL) &&
	    InitializeProcThreadAttributeList (attributes, 1, 0, &attributes_size)) {
		if (UpdateProcThreadAttribute (attributes, 0, PROC_THREAD_ATTRIBUTE_HANDLE_LIST,
					       handed, n_handed * sizeof (HANDLE), NULL, NULL)) {
			memset (&startup, 0, sizeof startup);
			startup.StartupInfo.cb = sizeof startup;
			startup.StartupInfo.dwFlags = STARTF_USESTDHANDLES;
			startup.StartupInfo.hStdInput = in;
			startup.StartupInfo.hStdOutput = out;
			startup.StartupInfo.hStdError = err;
			startup.lpAttributeList = attributes;

			started = CreateProcessW (wexe, wline, NULL, NULL, TRUE,
						  CREATE_NO_WINDOW | CREATE_DEFAULT_ERROR_MODE |
						  CREATE_UNICODE_ENVIRONMENT | EXTENDED_STARTUPINFO_PRESENT,
						  NULL, wdir, &startup.StartupInfo, &process);
		}
		DeleteProcThreadAttributeList (attributes);
	}

	if (started) {
		CloseHandle (process.hThread);
		*process_out = process.hProcess;
	} else {
		set_failed (error, program);
	}

	g_free (attributes);
	g_free (wdir);
	g_free (wline);
	g_free (wexe);
	g_free (program);
	return started;
}

/* Everything the program has written so far, without waiting for more. */
static void
take_output (HANDLE pipe, GByteArray *got)
{
	guint8 buffer[16384];
	DWORD waiting = 0, count = 0;

	while (PeekNamedPipe (pipe, NULL, 0, NULL, &waiting, NULL) && waiting > 0) {
		if (!ReadFile (pipe, buffer, MIN (waiting, (DWORD) sizeof buffer), &count, NULL) ||
		    count == 0) {
			break;
		}
		g_byte_array_append (got, buffer, count);
	}
}

gboolean
nemo_launch_win32_pipe (const gchar * const  *argv,
			const gchar          *input_path,
			guint                 timeout_seconds,
			GCancellable         *cancellable,
			GBytes              **output,
			gboolean             *timed_out)
{
	SECURITY_ATTRIBUTES inherit = { sizeof inherit, NULL, TRUE };
	HANDLE input = INVALID_HANDLE_VALUE, nowhere;
	HANDLE out_read = NULL, out_write = NULL;
	HANDLE process = NULL;
	GByteArray *got = NULL;
	wchar_t *winput;
	gint64 deadline;
	gboolean started = FALSE, ended = FALSE, stopped = FALSE, late = FALSE;
	DWORD code = 1;

	if (output != NULL) {
		*output = NULL;
	}
	if (timed_out != NULL) {
		*timed_out = FALSE;
	}
	g_return_val_if_fail (argv != NULL && argv[0] != NULL, FALSE);

	nowhere = open_nowhere ();

	/* The file itself is the program's stdin, so nothing is copied through a
	 * pipe on the way, and it still never sees a name. */
	if (input_path != NULL) {
		winput = g_utf8_to_utf16 (input_path, -1, NULL, NULL, NULL);
		if (winput != NULL) {
			input = CreateFileW (winput, GENERIC_READ,
					     FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
					     &inherit, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
		}
		g_free (winput);
	} else {
		input = nowhere;
	}

	/* A big buffer, so a whole thumbnail fits and the program is never left
	 * waiting on us to read. */
	if (input != INVALID_HANDLE_VALUE && nowhere != INVALID_HANDLE_VALUE &&
	    (output == NULL || CreatePipe (&out_read, &out_write, &inherit, 1024 * 1024))) {
		if (out_read != NULL) {
			SetHandleInformation (out_read, HANDLE_FLAG_INHERIT, 0);
		}
		started = spawn_hidden (argv, NULL, input, out_write != NULL ? out_write : nowhere,
					nowhere, &process, NULL);
		close_valid (out_write);
	}

	if (input != nowhere) {
		close_valid (input);
	}
	close_valid (nowhere);

	if (!started) {
		close_valid (out_read);
		return FALSE;
	}

	got = g_byte_array_new ();
	deadline = g_get_monotonic_time () + (gint64) timeout_seconds * G_USEC_PER_SEC;

	while (TRUE) {
		DWORD state;

		if (out_read != NULL) {
			take_output (out_read, got);
		}

		state = WaitForSingleObject (process, 10);
		if (state != WAIT_TIMEOUT) {
			break;
		}

		if (!ended && g_cancellable_is_cancelled (cancellable)) {
			stopped = ended = TRUE;
			TerminateProcess (process, 1);
		} else if (!ended && g_get_monotonic_time () > deadline) {
			late = ended = TRUE;
			TerminateProcess (process, 1);
		}
	}

	/* What it wrote last. Something it started itself may still have the pipe open,
	 * so this stops at what is there rather than waiting for the end. */
	if (out_read != NULL) {
		take_output (out_read, got);
		CloseHandle (out_read);
	}

	GetExitCodeProcess (process, &code);
	CloseHandle (process);

	if (timed_out != NULL) {
		*timed_out = late;
	}

	if (stopped || late || code != 0) {
		g_byte_array_unref (got);
		return FALSE;
	}

	if (output != NULL) {
		*output = g_byte_array_free_to_bytes (got);
	} else {
		g_byte_array_unref (got);
	}
	return TRUE;
}

struct _NemoLaunchWin32Child {
	HANDLE        process;
	GInputStream *out;
	GInputStream *err;
	gboolean      waited;
	DWORD         code;
};

/* Returns: (transfer full): free with nemo_launch_win32_child_free */
NemoLaunchWin32Child *
nemo_launch_win32_child_start (const gchar * const  *argv,
			       const gchar          *workdir,
			       GSubprocessFlags      flags,
			       GError              **error)
{
	NemoLaunchWin32Child *child;
	HANDLE nowhere;
	HANDLE out_ours = INVALID_HANDLE_VALUE, out_theirs = INVALID_HANDLE_VALUE;
	HANDLE err_ours = INVALID_HANDLE_VALUE, err_theirs = INVALID_HANDLE_VALUE;
	HANDLE out, err, process = NULL;
	gboolean ok;

	g_return_val_if_fail (argv != NULL && argv[0] != NULL, NULL);

	nowhere = open_nowhere ();
	ok = nowhere != INVALID_HANDLE_VALUE;
	if (ok && (flags & G_SUBPROCESS_FLAGS_STDOUT_PIPE) != 0) {
		ok = make_out_pipe (&out_ours, &out_theirs);
	}
	if (ok && (flags & G_SUBPROCESS_FLAGS_STDERR_PIPE) != 0) {
		ok = make_out_pipe (&err_ours, &err_theirs);
	}

	if (!ok) {
		set_failed (error, argv[0]);
	} else {
		out = out_theirs != INVALID_HANDLE_VALUE ? out_theirs : nowhere;
		err = err_theirs != INVALID_HANDLE_VALUE ? err_theirs
		    : (flags & G_SUBPROCESS_FLAGS_STDERR_MERGE) != 0 ? out
		    : nowhere;
		ok = spawn_hidden (argv, workdir, nowhere, out, err, &process, error);
	}

	close_valid (nowhere);
	close_valid (out_theirs);
	close_valid (err_theirs);

	if (!ok) {
		close_valid (out_ours);
		close_valid (err_ours);
		return NULL;
	}

	child = g_new0 (NemoLaunchWin32Child, 1);
	child->process = process;
	if (out_ours != INVALID_HANDLE_VALUE) {
		child->out = g_win32_input_stream_new (out_ours, TRUE);
	}
	if (err_ours != INVALID_HANDLE_VALUE) {
		child->err = g_win32_input_stream_new (err_ours, TRUE);
	}

	return child;
}

/* Returns: (transfer none) */
GInputStream *
nemo_launch_win32_child_get_stdout (NemoLaunchWin32Child *child)
{
	return child->out;
}

/* Returns: (transfer none) */
GInputStream *
nemo_launch_win32_child_get_stderr (NemoLaunchWin32Child *child)
{
	return child->err;
}

void
nemo_launch_win32_child_force_exit (NemoLaunchWin32Child *child)
{
	if (!child->waited) {
		TerminateProcess (child->process, 1);
	}
}

gint
nemo_launch_win32_child_wait (NemoLaunchWin32Child *child)
{
	if (!child->waited) {
		WaitForSingleObject (child->process, INFINITE);
		if (!GetExitCodeProcess (child->process, &child->code)) {
			child->code = 1;
		}
		child->waited = TRUE;
	}

	return (gint) child->code;
}

void
nemo_launch_win32_child_free (NemoLaunchWin32Child *child)
{
	if (child == NULL) {
		return;
	}

	g_clear_object (&child->out);
	g_clear_object (&child->err);
	CloseHandle (child->process);
	g_free (child);
}
