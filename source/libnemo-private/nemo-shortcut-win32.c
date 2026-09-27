/* nemo-shortcut-win32.c - create Windows .lnk shell shortcuts via COM.
 *
 * Copyright © 2026 t00mietum (CryptogID: ปʬϝღถɔ4რఠΔթะ9ƾǝu)
 *
 * This program is free software; you can redistribute it and/or modify it under
 * the terms of the GNU General Public License version 2, as published by the
 * Free Software Foundation.
 */

#include <config.h>
#include "nemo-shortcut-win32.h"

#ifdef G_OS_WIN32

#include "nemo-launch-win32.h"
#include "nemo-lnk.h"

#include <string.h>
#include <gio/gio.h>
#include <glib/gi18n.h>
#include <glib/gstdio.h>

#define COBJMACROS
#include <windows.h>
#include <shlobj.h>
#include <shellapi.h>
#include <objidl.h>

/* CoInitialize on this thread; returns TRUE if the caller must CoUninitialize. */
static gboolean
com_init (void)
{
	HRESULT hr = CoInitializeEx (NULL, COINIT_APARTMENTTHREADED);
	if (hr == RPC_E_CHANGED_MODE) {
		/* thread already initialized in another mode - shell link still
		 * works, just don't unbalance the ref count */
		return FALSE;
	}
	return TRUE;
}

static gunichar2 *
to_utf16 (const char *s)
{
	if (s == NULL) {
		return NULL;
	}
	return g_utf8_to_utf16 (s, -1, NULL, NULL, NULL);
}

static gboolean
create_shortcut (const char  *target_path,
                 const char  *lnk_path,
                 const char  *working_dir,
                 const char  *arguments,
                 const char  *description,
                 gboolean     relative,
                 GError     **error)
{
	IShellLinkW *link = NULL;
	IPersistFile *pf = NULL;
	gunichar2 *w_target = NULL, *w_lnk = NULL, *w_dir = NULL, *w_args = NULL, *w_desc = NULL;
	gboolean did_init = FALSE;
	gboolean ok = FALSE;
	HRESULT hr;

	g_return_val_if_fail (target_path != NULL, FALSE);
	g_return_val_if_fail (lnk_path != NULL, FALSE);

	/* IPersistFile::Save has no create-new mode, so without this it silently
	 * writes over whatever is already there. Report the clash and let the caller
	 * uniquify ("another link to ...") the way the symlink path does. The check
	 * is racy - the shell offers nothing atomic - but the race is a rare loss
	 * against an unconditional one. */
	if (g_file_test (lnk_path, G_FILE_TEST_EXISTS)) {
		g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_EXISTS,
				     _("A file with that name already exists."));
		goto out;
	}

	w_target = to_utf16 (target_path);
	w_lnk    = to_utf16 (lnk_path);
	if (w_target == NULL || w_lnk == NULL) {
		g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_FAILED,
				     _("Could not encode the shortcut path."));
		goto out;
	}

	did_init = com_init ();

	hr = CoCreateInstance (&CLSID_ShellLink, NULL, CLSCTX_INPROC_SERVER,
			       &IID_IShellLinkW, (void **) &link);
	if (FAILED (hr) || link == NULL) {
		g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_FAILED,
				     _("Could not create the shortcut object."));
		goto uninit;
	}

	/* SetPath is MAX_PATH-bound and refuses a longer target outright. Ignoring
	 * that saved a shortcut with no target at all and called it a success, so
	 * "Make Link" produced a dead link with nothing said. */
	hr = IShellLinkW_SetPath (link, w_target);
	if (FAILED (hr)) {
		if (wcslen ((const wchar_t *) w_target) >= MAX_PATH) {
			g_set_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_FILENAME,
				     _("A Windows shortcut cannot point at \"%s\": the path is too long."),
				     target_path);
		} else {
			g_set_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_FILENAME,
				     _("A Windows shortcut cannot point at \"%s\"."),
				     target_path);
		}
		goto release;
	}

	if (working_dir != NULL && (w_dir = to_utf16 (working_dir)) != NULL) {
		IShellLinkW_SetWorkingDirectory (link, w_dir);
	}
	if (arguments != NULL && (w_args = to_utf16 (arguments)) != NULL) {
		IShellLinkW_SetArguments (link, w_args);
	}
	if (description != NULL && (w_desc = to_utf16 (description)) != NULL) {
		IShellLinkW_SetDescription (link, w_desc);
	}
	/* Takes the shortcut's own path and works out the relative one from it.
	   Across drives there is none, and the absolute path is all it keeps. */
	if (relative) {
		IShellLinkW_SetRelativePath (link, w_lnk, 0);
	}

	hr = IShellLinkW_QueryInterface (link, &IID_IPersistFile, (void **) &pf);
	if (FAILED (hr) || pf == NULL) {
		g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_FAILED,
				     _("Could not access the shortcut file interface."));
		goto release;
	}

	hr = IPersistFile_Save (pf, w_lnk, TRUE);
	if (FAILED (hr)) {
		g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_FAILED,
				     _("Could not save the shortcut."));
		goto release;
	}

	ok = TRUE;

release:
	if (pf != NULL) {
		IPersistFile_Release (pf);
	}
	if (link != NULL) {
		IShellLinkW_Release (link);
	}
uninit:
	if (did_init) {
		CoUninitialize ();
	}
out:
	g_free (w_target);
	g_free (w_lnk);
	g_free (w_dir);
	g_free (w_args);
	g_free (w_desc);
	return ok;
}

gboolean
nemo_shortcut_win32_create (const char  *target_path,
                            const char  *lnk_path,
                            const char  *working_dir,
                            const char  *arguments,
                            const char  *description,
                            GError     **error)
{
	return create_shortcut (target_path, lnk_path, working_dir, arguments, description,
				FALSE, error);
}

gboolean
nemo_shortcut_win32_create_parts (const char  *target_path,
                                  const char  *lnk_path,
                                  guint        parts,
                                  GError     **error)
{
	GError *local_error = NULL;
	char *portable = NULL;
	gboolean ok;

	/* The shell has no way to leave the item id list out. Without it the
	 * shell follows only the path with variables, so that goes in plain
	 * when no variable covers the target. */
	if (!(parts & NEMO_LNK_ABSOLUTE)) {
		return nemo_lnk_write (lnk_path, target_path, parts, error);
	}

	/* Given a path with a variable in it, the shell keeps it that way as
	 * well as the absolute one, and prefers it. */
	if (parts & NEMO_LNK_PORTABLE) {
		portable = nemo_lnk_portable_path (target_path);
	}
	/* The block it goes in holds 260 characters. */
	if (portable != NULL && g_utf8_strlen (portable, -1) >= MAX_PATH) {
		g_clear_pointer (&portable, g_free);
	}
	ok = create_shortcut (portable != NULL ? portable : target_path, lnk_path,
			      NULL, NULL, NULL, (parts & NEMO_LNK_RELATIVE) != 0, error);
	g_free (portable);

	/* Only a spare way back is lost if this fails; the shortcut works. */
	if (ok && !(parts & NEMO_LNK_RELATIVE) &&
	    !nemo_lnk_drop_relative (lnk_path, &local_error)) {
		g_warning ("Could not take the relative path out of %s: %s",
			   lnk_path, local_error->message);
		g_error_free (local_error);
	}

	return ok;
}

/* The shell reads a shortcut through its item id list, which one made off
 * Windows does not have. What that one holds instead is the path relative to
 * itself, and the \\server\share path when the target was on a share. The
 * share goes last, since one that is not answering takes a long time to say
 * so. A path with variables in it is read by the shell, and comes here only
 * when the shell found nothing there. */
static char *
target_without_shell (const char *lnk_path)
{
	NemoLnk lnk;
	char *dir, *candidate;
	char *found = NULL;

	if (!nemo_lnk_read (lnk_path, &lnk)) {
		return NULL;
	}

	if (lnk.relative_path != NULL && lnk.relative_path[0] != '\0') {
		dir = g_path_get_dirname (lnk_path);
		candidate = g_canonicalize_filename (lnk.relative_path, dir);
		g_free (dir);
		if (g_file_test (candidate, G_FILE_TEST_EXISTS)) {
			found = candidate;
		} else {
			g_free (candidate);
		}
	}
	if (found == NULL && lnk.local_path != NULL &&
	    g_file_test (lnk.local_path, G_FILE_TEST_EXISTS)) {
		found = g_strdup (lnk.local_path);
	}
	if (found == NULL && lnk.net_share != NULL) {
		candidate = g_build_filename (lnk.net_share, lnk.net_path, NULL);
		if (g_file_test (candidate, G_FILE_TEST_EXISTS)) {
			found = candidate;
		} else {
			g_free (candidate);
		}
	}
	nemo_lnk_clear (&lnk);

	return found;
}

gboolean
nemo_shortcut_win32_read (const char  *lnk_path,
                          char       **target_path,
                          GError     **error)
{
	return nemo_shortcut_win32_read_target (lnk_path, target_path, NULL, error);
}

gboolean
nemo_shortcut_win32_read_target (const char  *lnk_path,
                                 char       **target_path,
                                 gboolean    *by_shell,
                                 GError     **error)
{
	IShellLinkW *link = NULL;
	IPersistFile *pf = NULL;
	gunichar2 *w_lnk = NULL;
	/* Long-path sized: at MAX_PATH a longer target was silently cut short and
	   then opened, which is a different file. */
	wchar_t buf[32768];
	gboolean did_init = FALSE;
	gboolean ok = FALSE;
	HRESULT hr;

	g_return_val_if_fail (lnk_path != NULL, FALSE);
	g_return_val_if_fail (target_path != NULL, FALSE);

	*target_path = NULL;
	if (by_shell != NULL) {
		*by_shell = TRUE;
	}

	w_lnk = to_utf16 (lnk_path);
	if (w_lnk == NULL) {
		g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_FAILED,
				     _("Could not encode the shortcut path."));
		goto out;
	}

	did_init = com_init ();

	hr = CoCreateInstance (&CLSID_ShellLink, NULL, CLSCTX_INPROC_SERVER,
			       &IID_IShellLinkW, (void **) &link);
	if (FAILED (hr) || link == NULL) {
		g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_FAILED,
				     _("Could not create the shortcut object."));
		goto uninit;
	}

	hr = IShellLinkW_QueryInterface (link, &IID_IPersistFile, (void **) &pf);
	if (FAILED (hr) || pf == NULL) {
		g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_FAILED,
				     _("Could not access the shortcut file interface."));
		goto release;
	}

	hr = IPersistFile_Load (pf, w_lnk, STGM_READ);
	if (FAILED (hr)) {
		g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_FAILED,
				     _("Could not load the shortcut."));
		goto release;
	}

	buf[0] = L'\0';
	hr = IShellLinkW_GetPath (link, buf, G_N_ELEMENTS (buf), NULL, 0);
	/* A shortcut with no id list but a path with variables in it has no
	 * path until it is resolved, which the shell does on opening it. An
	 * unknown variable resolves to a folder named after it on the desktop,
	 * with S_FALSE, so only S_OK and a target that is there count. Never
	 * searches, and gives up after a second. */
	if (FAILED (hr) || buf[0] == L'\0') {
		hr = IShellLinkW_Resolve (link, NULL,
					  SLR_NO_UI | SLR_NOUPDATE | SLR_NOSEARCH | SLR_NOTRACK |
					  (1000 << 16));
		buf[0] = L'\0';
		if (hr == S_OK) {
			hr = IShellLinkW_GetPath (link, buf, G_N_ELEMENTS (buf), NULL, 0);
		}
		if (hr != S_OK || GetFileAttributesW (buf) == INVALID_FILE_ATTRIBUTES) {
			buf[0] = L'\0';
		}
	}
	if (buf[0] == L'\0') {
		*target_path = target_without_shell (lnk_path);
		if (*target_path != NULL) {
			if (by_shell != NULL) {
				*by_shell = FALSE;
			}
			ok = TRUE;
			goto release;
		}
		/* No file-system target - e.g. a shortcut to a virtual item that
		 * stores only an ID list. Nothing to follow. */
		g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND,
				     _("The shortcut has no file target."));
		goto release;
	}

	*target_path = g_utf16_to_utf8 ((const gunichar2 *) buf, -1, NULL, NULL, NULL);
	ok = (*target_path != NULL);
	if (!ok) {
		/* Every other failure here leaves an error behind; this one did not. */
		g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA,
				     _("The shortcut's target could not be read."));
	}

release:
	if (pf != NULL) {
		IPersistFile_Release (pf);
	}
	if (link != NULL) {
		IShellLinkW_Release (link);
	}
uninit:
	if (did_init) {
		CoUninitialize ();
	}
out:
	g_free (w_lnk);
	return ok;
}

/* Opens a shortcut for reading or rewriting; the caller releases both. */
static gboolean
load_link (const char    *lnk_path,
           DWORD          mode,
           IShellLinkW  **link,
           IPersistFile **pf,
           GError       **error)
{
	gunichar2 *w_lnk = to_utf16 (lnk_path);
	HRESULT hr;

	*link = NULL;
	*pf = NULL;

	if (w_lnk == NULL) {
		g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_FAILED,
				     _("Could not encode the shortcut path."));
		return FALSE;
	}

	hr = CoCreateInstance (&CLSID_ShellLink, NULL, CLSCTX_INPROC_SERVER,
			       &IID_IShellLinkW, (void **) link);
	if (SUCCEEDED (hr) && *link != NULL) {
		hr = IShellLinkW_QueryInterface (*link, &IID_IPersistFile, (void **) pf);
	}
	if (SUCCEEDED (hr) && *pf != NULL) {
		hr = IPersistFile_Load (*pf, w_lnk, mode);
	}

	g_free (w_lnk);

	if (FAILED (hr) || *pf == NULL) {
		g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_FAILED,
				     _("Could not load the shortcut."));
		if (*pf != NULL) {
			IPersistFile_Release (*pf);
		}
		if (*link != NULL) {
			IShellLinkW_Release (*link);
		}
		*link = NULL;
		*pf = NULL;
		return FALSE;
	}

	return TRUE;
}

static char *
from_wide (const wchar_t *s)
{
	return g_utf16_to_utf8 ((const gunichar2 *) s, -1, NULL, NULL, NULL);
}

void
nemo_shortcut_info_clear (NemoShortcutInfo *info)
{
	g_clear_pointer (&info->target, g_free);
	g_clear_pointer (&info->arguments, g_free);
	g_clear_pointer (&info->working_dir, g_free);
	g_clear_pointer (&info->description, g_free);
}

/* Unlike nemo_shortcut_win32_read, a shortcut with no file target is not an
 * error here: the editor still has to open on it. Every field comes back set,
 * empty where the shortcut says nothing. */
gboolean
nemo_shortcut_win32_read_info (const char        *lnk_path,
                               NemoShortcutInfo  *info,
                               GError           **error)
{
	IShellLinkW *link;
	IPersistFile *pf;
	wchar_t buf[32768];
	gboolean did_init;

	g_return_val_if_fail (lnk_path != NULL, FALSE);
	g_return_val_if_fail (info != NULL, FALSE);

	memset (info, 0, sizeof *info);

	did_init = com_init ();

	if (!load_link (lnk_path, STGM_READ, &link, &pf, error)) {
		if (did_init) {
			CoUninitialize ();
		}
		return FALSE;
	}

	buf[0] = L'\0';
	IShellLinkW_GetPath (link, buf, G_N_ELEMENTS (buf), NULL, 0);
	info->target = from_wide (buf);

	buf[0] = L'\0';
	IShellLinkW_GetArguments (link, buf, G_N_ELEMENTS (buf));
	info->arguments = from_wide (buf);

	buf[0] = L'\0';
	IShellLinkW_GetWorkingDirectory (link, buf, G_N_ELEMENTS (buf));
	info->working_dir = from_wide (buf);

	buf[0] = L'\0';
	IShellLinkW_GetDescription (link, buf, G_N_ELEMENTS (buf));
	info->description = from_wide (buf);

	IPersistFile_Release (pf);
	IShellLinkW_Release (link);

	if (did_init) {
		CoUninitialize ();
	}

	return TRUE;
}

/* How many shortcut answers to keep before starting over. */
#define TARGET_CACHE_LIMIT 2000

/* Whether the shortcut points at a folder, read from the attributes the .lnk
 * stores rather than from the target itself - a shortcut to a share that is not
 * answering costs about twenty seconds, and this is asked from the draw path.
 * Kept for the same reason: it is a COM load and a file read each time. */
gboolean
nemo_shortcut_win32_target_is_dir (const char *lnk_path,
                                   gint64      mtime)
{
	static GHashTable *cache = NULL;
	IShellLinkW *link;
	IPersistFile *pf;
	WIN32_FIND_DATAW found;
	wchar_t buf[MAX_PATH];
	gboolean did_init;
	gboolean is_dir = FALSE;
	char *key;
	gpointer cached;

	g_return_val_if_fail (lnk_path != NULL, FALSE);

	if (cache == NULL) {
		cache = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);
	} else if (g_hash_table_size (cache) >= TARGET_CACHE_LIMIT) {
		g_hash_table_remove_all (cache);
	}

	key = g_strdup_printf ("%s|%lld", lnk_path, (long long) mtime);

	if (g_hash_table_lookup_extended (cache, key, NULL, &cached)) {
		g_free (key);
		return GPOINTER_TO_INT (cached) != 0;
	}

	did_init = com_init ();

	if (load_link (lnk_path, STGM_READ, &link, &pf, NULL)) {
		memset (&found, 0, sizeof found);
		buf[0] = L'\0';

		if (SUCCEEDED (IShellLinkW_GetPath (link, buf, G_N_ELEMENTS (buf),
						    &found, SLGP_RAWPATH)) && buf[0] != L'\0') {
			is_dir = (found.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
		} else {
			/* One made off Windows: the header says what the target was. */
			NemoLnk lnk;

			if (nemo_lnk_read (lnk_path, &lnk)) {
				is_dir = nemo_lnk_is_dir (&lnk);
				nemo_lnk_clear (&lnk);
			}
		}

		IPersistFile_Release (pf);
		IShellLinkW_Release (link);
	}

	if (did_init) {
		CoUninitialize ();
	}

	g_hash_table_insert (cache, key, GINT_TO_POINTER (is_dir ? 1 : 0));

	return is_dir;
}

/* Rewrites the shortcut in place with every field from info. */
gboolean
nemo_shortcut_win32_update (const char             *lnk_path,
                            const NemoShortcutInfo *info,
                            GError                **error)
{
	IShellLinkW *link;
	IPersistFile *pf;
	gunichar2 *w_target, *w_args, *w_dir, *w_desc, *w_lnk;
	gboolean did_init;
	gboolean ok = FALSE;
	HRESULT hr;

	g_return_val_if_fail (lnk_path != NULL, FALSE);
	g_return_val_if_fail (info != NULL, FALSE);

	did_init = com_init ();

	if (!load_link (lnk_path, STGM_READWRITE, &link, &pf, error)) {
		if (did_init) {
			CoUninitialize ();
		}
		return FALSE;
	}

	w_target = to_utf16 (info->target != NULL ? info->target : "");
	w_args   = to_utf16 (info->arguments != NULL ? info->arguments : "");
	w_dir    = to_utf16 (info->working_dir != NULL ? info->working_dir : "");
	w_desc   = to_utf16 (info->description != NULL ? info->description : "");
	w_lnk    = to_utf16 (lnk_path);

	hr = IShellLinkW_SetPath (link, w_target);
	if (FAILED (hr)) {
		g_set_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_FILENAME,
			     _("A Windows shortcut cannot point at \"%s\"."), info->target);
		goto out;
	}

	IShellLinkW_SetArguments (link, w_args);
	IShellLinkW_SetWorkingDirectory (link, w_dir);
	IShellLinkW_SetDescription (link, w_desc);

	hr = IPersistFile_Save (pf, w_lnk, TRUE);
	if (FAILED (hr)) {
		g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_FAILED,
				     _("Could not save the shortcut."));
		goto out;
	}

	ok = TRUE;

out:
	g_free (w_target);
	g_free (w_args);
	g_free (w_dir);
	g_free (w_desc);
	g_free (w_lnk);
	IPersistFile_Release (pf);
	IShellLinkW_Release (link);

	if (did_init) {
		CoUninitialize ();
	}

	return ok;
}

static gboolean
is_lnk (const char *path)
{
	gsize len = path != NULL ? strlen (path) : 0;

	return len > 4 && g_ascii_strcasecmp (path + len - 4, ".lnk") == 0;
}

NemoShortcutOpen
nemo_shortcut_win32_open_action (const char  *path,
                                 char       **target_path)
{
#define SHORTCUT_MAX_HOPS 8
	const char *current = path;
	char *final = NULL;
	GHashTable *seen;
	gboolean by_shell = TRUE;
	int hop;

	g_return_val_if_fail (target_path != NULL, NEMO_SHORTCUT_OPEN_NOT_A_SHORTCUT);
	*target_path = NULL;

	if (!is_lnk (path)) {
		return NEMO_SHORTCUT_OPEN_NOT_A_SHORTCUT;
	}

	/* Follow the whole chain here rather than one hop at a time: a
	 * shortcut can point at another shortcut, and a.lnk -> a.lnk
	 * recursed until the stack ran out. */
	seen = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);
	for (hop = 0; hop < SHORTCUT_MAX_HOPS && current != NULL; hop++) {
		char *target = NULL;
		gboolean hop_by_shell = TRUE;

		if (!is_lnk (current)) {
			break;
		}
		if (g_hash_table_contains (seen, current)) {
			break;  /* already followed this one - a loop */
		}
		g_hash_table_insert (seen, g_strdup (current), NULL);
		if (!nemo_shortcut_win32_read_target (current, &target, &hop_by_shell, NULL) ||
		    target == NULL) {
			g_free (target);
			break;
		}
		by_shell = by_shell && hop_by_shell;

		g_free (final);
		final = target;
		current = final;
	}
	g_hash_table_destroy (seen);

	/* The shell cannot open a chain it could not read all of itself, so
	 * a file at the end of one is opened here, as a folder is. Never a
	 * shortcut, though: one that leads back to itself would be opened
	 * here again, and again. */
	if (final != NULL &&
	    (g_file_test (final, G_FILE_TEST_IS_DIR) ||
	     (!by_shell && !is_lnk (final) && g_file_test (final, G_FILE_TEST_EXISTS)))) {
		*target_path = final;
		return NEMO_SHORTCUT_OPEN_HERE;
	}

	/* Also the way a shortcut to a virtual item (Recycle Bin, a control
	 * panel page) opens: it has no path to read, but the shell knows what to
	 * do with it. */
	g_free (final);
	return NEMO_SHORTCUT_OPEN_BY_SHELL;
#undef SHORTCUT_MAX_HOPS
}

gboolean
nemo_shortcut_win32_launch (const char  *lnk_path,
                            GError     **error)
{
	g_return_val_if_fail (lnk_path != NULL, FALSE);

	/* No verb: the default one, which is what a double-click uses. The shell
	   reads the .lnk itself, so the arguments, working directory, window state
	   and any run-as flag all come along. */
	return nemo_launch_win32_open_path (lnk_path, NULL, error);
}

#endif /* G_OS_WIN32 */
