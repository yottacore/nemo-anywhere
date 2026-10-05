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
