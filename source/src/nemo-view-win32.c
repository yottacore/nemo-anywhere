/* Win32-only helpers for nemo-view. Kept in its own file because <windows.h>
   defines macros (DELETE, ERROR, ...) that collide with enum identifiers in the
   main nemo-view.c.

   Copyright © 2026 t00mietum (CryptogID: ปʬϝღถɔ4რఠΔթะ9ƾǝu) */

#include <config.h>

#include <glib.h>
#include <string.h>

#ifdef G_OS_WIN32

#include "nemo-view-win32.h"

#include <libnemo-private/nemo-config.h>
#include <libnemo-private/nemo-launch-win32.h>

#include <glib/gi18n.h>
#include <windows.h>
#include <shellapi.h>
#include <shlobj.h>

/* ShellExecuteW returns >32 on success; anything at or below is an error code.
   Ignoring it meant a refused UAC prompt or a missing shell just did nothing. */
static gboolean
shell_execute_ok (HINSTANCE result, const gchar *what, const gchar *target)
{
	if ((INT_PTR) result > 32) {
		return TRUE;
	}

	g_warning ("%s failed for '%s' (ShellExecute code %d)",
		   what, target, (int) (INT_PTR) result);
	return FALSE;
}

/* Command-line quoting, MSVCRT rules: a run of backslashes immediately before
   the closing quote has to be doubled, or the quote is escaped away. A drive
   root ("C:\") is the common case and used to swallow it.
   Returns: (transfer full): free with g_free */
gchar *
nemo_view_win32_quote_arg (const gchar *arg)
{
	GString *out = g_string_new ("\"");
	gsize i, backslashes = 0;

	for (i = 0; arg[i] != '\0'; i++) {
		if (arg[i] == '\\') {
			backslashes++;
			continue;
		}

		if (arg[i] == '"') {
			gsize n;

			for (n = 0; n < backslashes * 2 + 1; n++) {
				g_string_append_c (out, '\\');
			}
		} else {
			gsize n;

			for (n = 0; n < backslashes; n++) {
				g_string_append_c (out, '\\');
			}
		}
		backslashes = 0;
		g_string_append_c (out, arg[i]);
	}

	for (i = 0; i < backslashes * 2; i++) {
		g_string_append_c (out, '\\');
	}
	g_string_append_c (out, '"');

	{
		gchar *ret = g_strdup (out->str);

		g_string_free (out, TRUE);
		return ret;
	}
}

gboolean
nemo_view_win32_is_elevated (void)
{
	static gboolean elevated = FALSE;
	static gsize once = 0;

	if (g_once_init_enter (&once)) {
		HANDLE token = NULL;

		if (OpenProcessToken (GetCurrentProcess (), TOKEN_QUERY, &token)) {
			TOKEN_ELEVATION info;
			DWORD len = 0;

			if (GetTokenInformation (token, TokenElevation, &info, sizeof (info), &len)) {
				elevated = info.TokenIsElevated != 0;
			}
			CloseHandle (token);
		}

		g_once_init_leave (&once, 1);
	}

	return elevated;
}

void
nemo_view_win32_open_elevated (const gchar *path)
{
	wchar_t exe[MAX_PATH];
	gchar *quoted;
	wchar_t *wpath;
	HINSTANCE res;

	if (GetModuleFileNameW (NULL, exe, MAX_PATH) == 0) {
		g_warning ("Open as Administrator: cannot find our own executable");
		return;
	}

	quoted = nemo_view_win32_quote_arg (path);
	wpath = (wchar_t *) g_utf8_to_utf16 (quoted, -1, NULL, NULL, NULL);
	res = ShellExecuteW (NULL, L"runas", exe, wpath, NULL, SW_SHOWNORMAL);
	shell_execute_ok (res, "Open as Administrator", path);
	g_free (wpath);
	g_free (quoted);
}

/* The preference is one field, so the program and anything after it arrive
   together. A quoted first word wins; failing that, a string that names a
   program on its own is taken whole, so a path with spaces and no arguments
   still works unquoted; otherwise it splits at the first space.
   Returns: (transfer full): free with g_free, and @args too */
gchar *
nemo_view_win32_split_terminal_command (const gchar *command, gchar **args)
{
	gchar *found;
	const gchar *space;

	*args = NULL;

	if (command[0] == '"') {
		const gchar *close = strchr (command + 1, '"');

		if (close != NULL) {
			gchar *exe = g_strndup (command + 1, close - command - 1);

			close++;
			while (*close == ' ') {
				close++;
			}
			if (*close != '\0') {
				*args = g_strdup (close);
			}
			return exe;
		}
	}

	found = g_find_program_in_path (command);
	if (found != NULL) {
		return found;
	}

	space = strchr (command, ' ');
	if (space == NULL) {
		return g_strdup (command);
	}

	*args = g_strdup (space + 1);
	return g_strndup (command, space - command);
}

void
nemo_view_win32_open_terminal (const gchar *path)
{
	/* With nothing chosen in preferences the candidate list decides, in order:
	   Windows Terminal, then PowerShell, then cmd (always present). wt takes
	   the folder via -d, the classic shells via the working directory. */
	gchar *chosen;
	gchar *exe = NULL;
	gchar *args = NULL;
	GError *error = NULL;

	chosen = nemo_config_get_string (nemo_config_get_group ("terminal"), "exec");

	if (chosen != NULL && *chosen != '\0') {
		exe = nemo_view_win32_split_terminal_command (chosen, &args);
	} else {
		gchar **candidates;
		guint i;

		candidates = nemo_config_get_strv (nemo_config_get_group ("windows"), "terminal-candidates");

		for (i = 0; candidates != NULL && candidates[i] != NULL; i++) {
			exe = g_find_program_in_path (candidates[i]);
			if (exe != NULL) {
				break;
			}
		}
		g_strfreev (candidates);

		if (exe == NULL) {
			exe = g_strdup ("cmd.exe");
		}
	}
	g_free (chosen);

	/* wt ignores the working directory it is handed, so it needs the folder
	   spelled out - unless the user gave it arguments of their own. */
	if (args == NULL && g_str_has_suffix (exe, "wt.exe")) {
		gchar *quoted = nemo_view_win32_quote_arg (path);

		args = g_strconcat ("-d ", quoted, NULL);
		g_free (quoted);
	}

	if (!nemo_launch_win32_run (exe, args, path, &error)) {
		g_warning ("Open in Terminal failed for '%s': %s", path, error->message);
		g_clear_error (&error);
	}

	g_free (exe);
	g_free (args);
}

/* Reveal @path in its own folder by starting an Explorer of our own. This is
   the route that still works when nemo is running elevated, where talking to
   the desktop's Explorer over COM is refused. */
static gboolean
explorer_select_by_command_line (const gchar *path)
{
	gchar *quoted = nemo_view_win32_quote_arg (path);
	gchar *params = g_strconcat ("/select,", quoted, NULL);
	GError *error = NULL;
	gboolean ok;

	ok = nemo_launch_win32_run ("explorer.exe", params, NULL, &error);
	if (!ok) {
		g_warning ("Open with Explorer failed for '%s': %s", path, error->message);
		g_clear_error (&error);
	}

	g_free (quoted);
	g_free (params);

	return ok;
}

/* The one place that deliberately hands work to Explorer, because the item says
   Explorer on it. Nothing else in the tree depends on it. */
void
nemo_view_win32_open_in_explorer (const gchar *path,
				  gboolean     is_directory)
{
	wchar_t *wpath;
	PIDLIST_ABSOLUTE item;
	HRESULT hr;

	g_return_if_fail (path != NULL);

	wpath = (wchar_t *) g_utf8_to_utf16 (path, -1, NULL, NULL, NULL);
	if (wpath == NULL) {
		g_warning ("Open with Explorer: cannot convert '%s'", path);
		return;
	}

	if (is_directory) {
		HINSTANCE res = ShellExecuteW (NULL, L"explore", wpath, NULL, NULL, SW_SHOWNORMAL);

		shell_execute_ok (res, "Open with Explorer", path);
		g_free (wpath);
		return;
	}

	/* An item id rather than a command line, so nothing about the name has to
	   survive being re-split. Needs COM on this thread; already-initialized
	   and a different apartment are both fine to carry on from. */
	item = ILCreateFromPathW (wpath);
	g_free (wpath);

	if (item == NULL) {
		explorer_select_by_command_line (path);
		return;
	}

	hr = CoInitializeEx (NULL, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
	if (SUCCEEDED (hr) || hr == RPC_E_CHANGED_MODE) {
		hr = SHOpenFolderAndSelectItems (item, 0, NULL, 0);
	}

	ILFree (item);

	if (FAILED (hr)) {
		explorer_select_by_command_line (path);
	}
}

#endif /* G_OS_WIN32 */
