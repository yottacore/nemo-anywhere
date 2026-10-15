/* Exercises Windows .lnk shortcut creation (nemo_shortcut_win32_create): make a
 * shortcut to a real file, then load it back through the shell IShellLinkW to
 * confirm the stored target round-trips. Windows-only. */

#include <config.h>

#include <stdlib.h>
#include <string.h>
#include <gio/gio.h>
#include <glib/gstdio.h>

#include <libnemo-private/nemo-shortcut-win32.h>
#include <libnemo-private/nemo-link-win32.h>
#include <libnemo-private/nemo-lnk.h>

#define COBJMACROS
#include <windows.h>
#include <shlobj.h>
#include <objidl.h>

#include "test-scratch.h"
#include "test-check.h"

/* Which kind of reparse point a path is, or 0 if it is not one. The find data
 * carries the tag in dwReserved0 whenever the reparse attribute is set. */
static DWORD
reparse_tag_of (const char *path)
{
	gunichar2 *wide = g_utf8_to_utf16 (path, -1, NULL, NULL, NULL);
	WIN32_FIND_DATAW data;
	HANDLE find;
	DWORD tag = 0;

	if (wide == NULL) {
		return 0;
	}

	find = FindFirstFileW ((LPCWSTR) wide, &data);
	if (find != INVALID_HANDLE_VALUE) {
		if (data.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) {
			tag = data.dwReserved0;
		}
		FindClose (find);
	}

	g_free (wide);
	return tag;
}

/* The shell always reports canonical long paths, while TEMP on a stock Windows
 * box is often still the 8.3 short form (C:\Users\COLLIE~1\...) - which is what
 * g_dir_make_tmp builds on. Compare both sides on the long form or a perfectly
 * good shortcut reads as a mismatch. Falls back to the input unchanged, so a
 * path the API won't expand still gets compared rather than dropped. */
static char *
long_path (const char *path)
{
	gunichar2 *w_in;
	wchar_t *buf;
	DWORD needed;
	char *ret;

	w_in = g_utf8_to_utf16 (path, -1, NULL, NULL, NULL);
	if (w_in == NULL) {
		return g_strdup (path);
	}

	needed = GetLongPathNameW ((LPCWSTR) w_in, NULL, 0);
	if (needed == 0) {
		g_free (w_in);
		return g_strdup (path);
	}

	buf = g_new (wchar_t, needed);
	if (GetLongPathNameW ((LPCWSTR) w_in, buf, needed) == 0) {
		ret = g_strdup (path);
	} else {
		ret = g_utf16_to_utf8 ((const gunichar2 *) buf, -1, NULL, NULL, NULL);
		if (ret == NULL) {
			ret = g_strdup (path);
		}
	}

	g_free (buf);
	g_free (w_in);
	return ret;
}

/* Read the target a .lnk points at, via the shell - proves it is a genuine,
 * loadable shortcut and not just an arbitrary file we wrote. */
static char *
resolve_target (const char *lnk_path)
{
	IShellLinkW *link = NULL;
	IPersistFile *pf = NULL;
	gunichar2 *w_lnk;
	wchar_t buf[MAX_PATH];
	char *ret = NULL;

	w_lnk = g_utf8_to_utf16 (lnk_path, -1, NULL, NULL, NULL);
	if (w_lnk == NULL) {
		return NULL;
	}

	CoInitializeEx (NULL, COINIT_APARTMENTTHREADED);

	if (SUCCEEDED (CoCreateInstance (&CLSID_ShellLink, NULL, CLSCTX_INPROC_SERVER,
					 &IID_IShellLinkW, (void **) &link)) &&
	    SUCCEEDED (IShellLinkW_QueryInterface (link, &IID_IPersistFile, (void **) &pf)) &&
	    SUCCEEDED (IPersistFile_Load (pf, w_lnk, STGM_READ)) &&
	    SUCCEEDED (IShellLinkW_GetPath (link, buf, MAX_PATH, NULL, 0))) {
		ret = g_utf16_to_utf8 ((const gunichar2 *) buf, -1, NULL, NULL, NULL);
	}

	if (pf != NULL) {
		IPersistFile_Release (pf);
	}
	if (link != NULL) {
		IShellLinkW_Release (link);
	}
	CoUninitialize ();
	g_free (w_lnk);
	return ret;
}

/* The shell stores a target in its long form, so a temp folder reached by an
 * 8.3 name comes back spelled differently from what was handed in. */
static gboolean
same_path (const char *a, const char *b)
{
	char *long_a, *long_b;
	gboolean same;

	if (a == NULL || b == NULL) {
		return FALSE;
	}

	long_a = long_path (a);
	long_b = long_path (b);
	same = g_ascii_strcasecmp (long_a, long_b) == 0;

	g_free (long_a);
	g_free (long_b);
	return same;
}

/* Every field a shortcut carries reads back as written, and an edit of the
 * target and the arguments reaches the file while the rest stays. */
static void
test_info_round_trip (const char *dir, const char *target)
{
	char *lnk = g_build_filename (dir, "edited.lnk", NULL);
	char *other = g_build_filename (dir, "other.txt", NULL);
	NemoShortcutInfo info;
	GError *error = NULL;

	check (g_file_set_contents (other, "other", -1, NULL));
	check (nemo_shortcut_win32_create (target, lnk, dir, "--flag one", "a comment", &error));
	check (error == NULL);

	check (nemo_shortcut_win32_read_info (lnk, &info, &error));
	check (error == NULL);
	check (same_path (info.target, target));
	check (g_strcmp0 (info.arguments, "--flag one") == 0);
	check (same_path (info.working_dir, dir));
	check (g_strcmp0 (info.description, "a comment") == 0);

	g_free (info.target);
	g_free (info.arguments);
	info.target = g_strdup (other);
	info.arguments = g_strdup ("");
	check (nemo_shortcut_win32_update (lnk, &info, &error));
	check (error == NULL);
	nemo_shortcut_info_clear (&info);

	check (nemo_shortcut_win32_read_info (lnk, &info, &error));
	check (same_path (info.target, other));
	check (g_strcmp0 (info.arguments, "") == 0);
	check (g_strcmp0 (info.description, "a comment") == 0);
	nemo_shortcut_info_clear (&info);

	/* a shortcut with no file target still opens */
	info.target = g_strdup ("");
	info.arguments = g_strdup ("");
	info.working_dir = g_strdup ("");
	info.description = g_strdup ("bare");
	check (nemo_shortcut_win32_update (lnk, &info, &error));
	nemo_shortcut_info_clear (&info);
	check (nemo_shortcut_win32_read_info (lnk, &info, &error));
	check (g_strcmp0 (info.target, "") == 0);
	check (g_strcmp0 (info.description, "bare") == 0);
	nemo_shortcut_info_clear (&info);

	g_clear_error (&error);
	g_remove (lnk);
	g_remove (other);
	g_free (other);
	g_free (lnk);
}

/* A shortcut the way one made off Windows is written: the header, then only a
   relative path. No item id list and no LinkInfo, so the shell finds nothing. */
static void
write_foreign_lnk (const char *lnk_path, const char *relative, gboolean is_dir)
{
	GByteArray *bytes = g_byte_array_new ();
	static const guint8 clsid[16] = {
		0x01, 0x14, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00,
		0xc0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x46
	};
	guint8 header[0x4c] = { 0x4c };
	glong units = 0;
	gunichar2 *wide = g_utf8_to_utf16 (relative, -1, NULL, &units, NULL);
	guint8 count[2] = { units & 0xff, units >> 8 };

	memcpy (header + 4, clsid, sizeof clsid);
	header[20] = 0x08 | 0x80;              /* relative path, Unicode */
	header[24] = is_dir ? 0x10 : 0x20;     /* the target's attributes */
	header[60] = 1;                        /* SW_SHOWNORMAL */
	g_byte_array_append (bytes, header, sizeof header);
	g_byte_array_append (bytes, count, 2);
	g_byte_array_append (bytes, (const guint8 *) wide, units * 2);
	g_byte_array_append (bytes, (const guint8 *) "\0\0\0\0", 4);

	check (g_file_set_contents (lnk_path, (const char *) bytes->data, bytes->len, NULL));
	g_byte_array_unref (bytes);
	g_free (wide);
}

/* The shell cannot place one of those, so the target comes from the relative
   path, and callers are told the shell could not. */
static void
test_foreign (const char *dir)
{
	char *sub = g_build_filename (dir, "foreign", "target", NULL);
	char *links = g_build_filename (dir, "foreign", "links", NULL);
	char *doc = g_build_filename (sub, "doc.txt", NULL);
	char *doc_lnk = g_build_filename (links, "doc.txt.lnk", NULL);
	char *dir_lnk = g_build_filename (links, "target.lnk", NULL);
	char *dead_lnk = g_build_filename (links, "dead.lnk", NULL);
	char *target = NULL;
	gboolean by_shell = TRUE;

	g_mkdir_with_parents (sub, 0700);
	g_mkdir_with_parents (links, 0700);
	check (g_file_set_contents (doc, "doc", -1, NULL));
	write_foreign_lnk (doc_lnk, "..\\target\\doc.txt", FALSE);
	write_foreign_lnk (dir_lnk, "..\\target", TRUE);
	write_foreign_lnk (dead_lnk, "..\\target\\gone.txt", FALSE);

	check (nemo_shortcut_win32_read_target (doc_lnk, &target, &by_shell, NULL));
	check (!by_shell);
	check (target != NULL && same_path (target, doc));
	g_clear_pointer (&target, g_free);

	check (nemo_shortcut_win32_read (dir_lnk, &target, NULL));
	check (target != NULL && same_path (target, sub));
	g_clear_pointer (&target, g_free);
	check (nemo_shortcut_win32_target_is_dir (dir_lnk, 1));
	check (!nemo_shortcut_win32_target_is_dir (doc_lnk, 1));

	/* Nothing there: no target, not a guess. */
	check (!nemo_shortcut_win32_read (dead_lnk, &target, NULL));
	check (target == NULL);

	/* One the shell made says so. */
	g_unlink (dead_lnk);
	check (nemo_shortcut_win32_create (doc, dead_lnk, NULL, NULL, NULL, NULL));
	by_shell = FALSE;
	check (nemo_shortcut_win32_read_target (dead_lnk, &target, &by_shell, NULL));
	check (by_shell);
	g_free (target);

	g_unlink (doc_lnk);
	g_unlink (dir_lnk);
	g_unlink (dead_lnk);
	g_unlink (doc);
	g_rmdir (sub);
	g_rmdir (links);
	g_free (doc);
	g_free (doc_lnk);
	g_free (dir_lnk);
	g_free (dead_lnk);
	g_free (sub);
	g_free (links);
}

/* With and without the portable path, read back the way the app reads a
 * shortcut, and the shell can follow it. */
static void
check_parts (const char *doc, const char *lnk, gboolean with_portable)
{
	NemoLnk info;
	char *target = NULL;
	gboolean by_shell = FALSE;

	g_unlink (lnk);
	check (nemo_shortcut_win32_create_paths (doc, lnk, with_portable, NULL));
	check (nemo_lnk_read (lnk, &info));
	check (info.local_path != NULL);
	check (info.relative_path != NULL);
	check ((info.env_path != NULL) == with_portable);
	nemo_lnk_clear (&info);

	check (nemo_shortcut_win32_read_target (lnk, &target, &by_shell, NULL));
	check (by_shell);
	check (target != NULL && same_path (target, doc));
	g_free (target);
	g_unlink (lnk);
}

static void
test_parts (const char *dir)
{
	char *doc = g_build_filename (dir, "parts.txt", NULL);
	char *lnk = g_build_filename (dir, "parts.lnk", NULL);
	char *portable = nemo_lnk_portable_path (doc);
	char *saved = g_strdup (g_getenv ("LOCALAPPDATA"));
	NemoLnk info;

	check (g_file_set_contents (doc, "doc", -1, NULL));

	/* A shortcut with only the portable path, which was the only way a
	   plain path went in that block, can no longer be asked for since the
	   path choice went (backlog 2026092813381440).

	check (nemo_shortcut_win32_create_parts (doc, lnk, NEMO_LNK_PORTABLE, NULL));
	check (nemo_lnk_read (lnk, &info));
	check (info.env_path != NULL &&
	       g_ascii_strcasecmp (info.env_path, portable != NULL ? portable : doc) == 0);
	nemo_lnk_clear (&info);
	by_shell = FALSE;
	check (nemo_shortcut_win32_read_target (lnk, &target, &by_shell, NULL));
	check (by_shell && target != NULL && same_path (target, doc));
	g_clear_pointer (&target, g_free);
	g_unlink (lnk);
	*/

	/* From here on a variable covers it, whatever the box. */
	g_setenv ("LOCALAPPDATA", dir, TRUE);
	g_free (portable);
	portable = nemo_lnk_portable_path (doc);
	check (g_strcmp0 (portable, "%LOCALAPPDATA%\\parts.txt") == 0);

	check_parts (doc, lnk, FALSE);
	check_parts (doc, lnk, TRUE);
	/* Shortcuts without the absolute or the relative path went with the
	   path choice (backlog 2026092813381440).
	check_parts (doc, lnk, NEMO_LNK_ABSOLUTE, TRUE);
	check_parts (doc, lnk, NEMO_LNK_PORTABLE, TRUE);
	check_parts (doc, lnk, NEMO_LNK_PORTABLE | NEMO_LNK_RELATIVE, TRUE);
	check_parts (doc, lnk, NEMO_LNK_RELATIVE, FALSE);
	*/

	/* The variable is kept as written, not expanded. */
	check (nemo_shortcut_win32_create_paths (doc, lnk, TRUE, NULL));
	check (nemo_lnk_read (lnk, &info));
	check (g_strcmp0 (info.env_path, portable) == 0);
	nemo_lnk_clear (&info);
	g_unlink (lnk);

	if (saved != NULL) {
		g_setenv ("LOCALAPPDATA", saved, TRUE);
	} else {
		g_unsetenv ("LOCALAPPDATA");
	}
	g_unlink (doc);
	g_free (saved);
	g_free (portable);
	g_free (lnk);
	g_free (doc);
}

static NemoShortcutOpen
open_action (const char *path, char **target)
{
	NemoShortcutOpen action;

	g_free (*target);
	action = nemo_shortcut_win32_open_action (path, target);
	g_print ("  open %s -> %d %s\n", path, action, *target != NULL ? *target : "");
	return action;
}

/* What a double-click on a shortcut does, decided before anything is started:
 * a folder opens in the tab, anything the shell can place goes to the shell
 * with the shortcut itself, and a file only this program can place opens here. */
static void
test_open_action (const char *dir)
{
	char *base = g_build_filename (dir, "open", NULL);
	char *folder = g_build_filename (base, "folder", NULL);
	char *doc = g_build_filename (base, "doc.txt", NULL);
	char *gone = g_build_filename (base, "gone.txt", NULL);
	char *folder_lnk = g_build_filename (base, "folder.lnk", NULL);
	char *doc_lnk = g_build_filename (base, "doc.lnk", NULL);
	char *gone_lnk = g_build_filename (base, "gone.lnk", NULL);
	char *program_lnk = g_build_filename (base, "program.lnk", NULL);
	char *chain_lnk = g_build_filename (base, "chain.lnk", NULL);
	char *foreign_lnk = g_build_filename (base, "foreign.lnk", NULL);
	char *loop_lnk = g_build_filename (base, "loop.lnk", NULL);
	char *notepad = g_build_filename (g_getenv ("SystemRoot") != NULL ? g_getenv ("SystemRoot") : "C:\\Windows",
					  "notepad.exe", NULL);
	char *target = NULL;

	g_mkdir_with_parents (folder, 0700);
	check (g_file_set_contents (doc, "doc", -1, NULL));
	check (g_file_set_contents (gone, "gone", -1, NULL));

	check (nemo_shortcut_win32_create (folder, folder_lnk, NULL, NULL, NULL, NULL));
	check (nemo_shortcut_win32_create (doc, doc_lnk, NULL, NULL, NULL, NULL));
	check (nemo_shortcut_win32_create (gone, gone_lnk, NULL, NULL, NULL, NULL));
	check (nemo_shortcut_win32_create (notepad, program_lnk, base, doc, NULL, NULL));
	check (nemo_shortcut_win32_create (folder_lnk, chain_lnk, NULL, NULL, NULL, NULL));
	write_foreign_lnk (foreign_lnk, "doc.txt", FALSE);
	write_foreign_lnk (loop_lnk, "loop.lnk", FALSE);
	g_unlink (gone);

	check (open_action (doc, &target) == NEMO_SHORTCUT_OPEN_NOT_A_SHORTCUT);
	check (target == NULL);

	check (open_action (folder_lnk, &target) == NEMO_SHORTCUT_OPEN_HERE);
	check (target != NULL && same_path (target, folder));

	/* The shell reads the arguments, Start in and window state from the
	   shortcut; none of that survives being reduced to a path. */
	check (open_action (doc_lnk, &target) == NEMO_SHORTCUT_OPEN_BY_SHELL);
	check (target == NULL);
	check (open_action (program_lnk, &target) == NEMO_SHORTCUT_OPEN_BY_SHELL);
	check (target == NULL);

	/* Nothing to open here, so the shell is the one to say so. */
	check (open_action (gone_lnk, &target) == NEMO_SHORTCUT_OPEN_BY_SHELL);
	check (target == NULL);

	check (open_action (chain_lnk, &target) == NEMO_SHORTCUT_OPEN_HERE);
	check (target != NULL && same_path (target, folder));

	/* Made off Windows: the shell cannot place it, so it opens here. */
	check (open_action (foreign_lnk, &target) == NEMO_SHORTCUT_OPEN_HERE);
	check (target != NULL && same_path (target, doc));

	/* A shortcut to itself ends, and never hands a shortcut back to be
	   opened again. */
	check (open_action (loop_lnk, &target) == NEMO_SHORTCUT_OPEN_BY_SHELL);
	check (target == NULL);

	g_free (target);
	g_unlink (loop_lnk);
	g_unlink (foreign_lnk);
	g_unlink (chain_lnk);
	g_unlink (program_lnk);
	g_unlink (gone_lnk);
	g_unlink (doc_lnk);
	g_unlink (folder_lnk);
	g_unlink (doc);
	g_rmdir (folder);
	g_rmdir (base);
	g_free (notepad);
	g_free (loop_lnk);
	g_free (foreign_lnk);
	g_free (chain_lnk);
	g_free (program_lnk);
	g_free (gone_lnk);
	g_free (doc_lnk);
	g_free (folder_lnk);
	g_free (gone);
	g_free (doc);
	g_free (folder);
	g_free (base);
}

int
main (void)
{
	char *dir, *target, *lnk;
	GError *error = NULL;
	gboolean ok;

	dir = test_scratch_dir ("nemo-lnk-XXXXXX", NULL);
	g_assert (dir != NULL);

	target = g_build_filename (dir, "target.txt", NULL);
	check (g_file_set_contents (target, "hello", -1, NULL));

	test_info_round_trip (dir, target);
	test_foreign (dir);
	test_parts (dir);
	test_open_action (dir);

	lnk = g_build_filename (dir, "shortcut.lnk", NULL);

	ok = nemo_shortcut_win32_create (target, lnk, NULL, NULL, "test shortcut", &error);
	check (ok);
	check (error == NULL);
	check (g_file_test (lnk, G_FILE_TEST_EXISTS));

	if (ok) {
		char *want = long_path (target);
		char *resolved = resolve_target (lnk);
		char *got = resolved ? long_path (resolved) : NULL;

		check (resolved != NULL);
		/* Case-insensitive: the shell may normalize the drive/casing. */
		check (got != NULL && g_ascii_strcasecmp (got, want) == 0);
		g_free (resolved);
		g_free (got);

		/* Same round-trip through the public read helper (follow-on-open). */
		char *readback = NULL;
		GError *rerr = NULL;
		check (nemo_shortcut_win32_read (lnk, &readback, &rerr));
		check (rerr == NULL);
		got = readback ? long_path (readback) : NULL;
		check (got != NULL && g_ascii_strcasecmp (got, want) == 0);
		g_clear_error (&rerr);
		g_free (readback);
		g_free (got);
		g_free (want);
	}

	g_clear_error (&error);

	/* Creating over something that already exists must report the clash rather
	 * than overwrite it - the caller relies on G_IO_ERROR_EXISTS to retry under
	 * a free name, and anything already at that path must survive untouched. */
	{
		char *occupied = g_build_filename (dir, "occupied.lnk", NULL);
		char *contents = NULL;
		GError *eerr = NULL;

		check (g_file_set_contents (occupied, "do not clobber", -1, NULL));

		check (!nemo_shortcut_win32_create (target, occupied, NULL, NULL, NULL, &eerr));
		check (eerr != NULL);
		check (eerr != NULL && g_error_matches (eerr, G_IO_ERROR, G_IO_ERROR_EXISTS));

		check (g_file_get_contents (occupied, &contents, NULL, NULL));
		check (contents != NULL && strcmp (contents, "do not clobber") == 0);

		g_free (contents);
		g_clear_error (&eerr);
		g_unlink (occupied);
		g_free (occupied);
	}

	/* A target past MAX_PATH has to be refused, and refused with an error. The
	 * shell will not store one - SetPath rejects it - so a create that ignores
	 * that result writes a .lnk pointing at nothing and reports success, which
	 * is a dead shortcut the user was never told about. Long paths are enabled
	 * on plenty of boxes, so the target itself is perfectly ordinary. */
	{
		GString *deep = g_string_new (dir);
		char *long_target, *long_lnk;
		GError *lerr = NULL;
		int i;

		for (i = 0; i < 12; i++) {
			g_string_append_printf (deep, "\\level-%02d-padded-out-to-length", i);
		}
		g_string_append (deep, "\\deep-target.txt");
		long_target = g_string_free (deep, FALSE);
		check (strlen (long_target) > MAX_PATH);

		long_lnk = g_build_filename (dir, "long.lnk", NULL);
		check (!nemo_shortcut_win32_create (long_target, long_lnk, NULL, NULL, NULL, &lerr));
		check (lerr != NULL);
		check (lerr == NULL || lerr->message != NULL);

		/* Nothing left behind to double-click. */
		check (!g_file_test (long_lnk, G_FILE_TEST_EXISTS));

		g_clear_error (&lerr);
		g_unlink (long_lnk);
		g_free (long_lnk);
		g_free (long_target);
	}

	/* A refused create must always leave an error behind: the file-operations
	 * caller reads error->message straight off the failure path. */
	{
		char *nested = g_build_filename (dir, "no-such-dir", "x.lnk", NULL);
		GError *eerr = NULL;

		check (!nemo_shortcut_win32_create (target, nested, NULL, NULL, NULL, &eerr));
		check (eerr != NULL);
		check (eerr == NULL || eerr->message != NULL);

		g_clear_error (&eerr);
		g_free (nested);
	}

	/* The point of handing the shortcut itself to the shell rather than its
	 * target: the arguments and the working directory come along. Both are
	 * checked at once by writing to a relative name - it can only be written in the
	 * shortcut's working directory. */
	{
		const char *shell = g_getenv ("COMSPEC");
		char *run_lnk = g_build_filename (dir, "run.lnk", NULL);
		char *stamp = g_build_filename (dir, "stamp.txt", NULL);
		GError *rerr = NULL;
		int waited;

		if (shell == NULL) {
			shell = "C:\\windows\\system32\\cmd.exe";
		}

		check (nemo_shortcut_win32_create (shell, run_lnk, dir,
						   "/c echo carried > stamp.txt", NULL, &rerr));
		check (rerr == NULL);
		g_clear_error (&rerr);

		if (g_file_test (run_lnk, G_FILE_TEST_EXISTS)) {
			check (nemo_shortcut_win32_launch (run_lnk, &rerr));
			check (rerr == NULL);
			g_clear_error (&rerr);

			/* The shell returns as soon as it has started the process. */
			for (waited = 0; waited < 40 && !g_file_test (stamp, G_FILE_TEST_EXISTS); waited++) {
				g_usleep (250000);
			}
			check (g_file_test (stamp, G_FILE_TEST_EXISTS));
		}

		g_unlink (stamp);
		g_unlink (run_lnk);
		g_free (stamp);
		g_free (run_lnk);
	}

	/* Opening a shortcut that is not there must fail with something to show,
	 * not report success into a void. Nothing is launched: the shell has
	 * nothing to open and answers before it starts anything. */
	{
		char *missing = g_build_filename (dir, "not-here.lnk", NULL);
		GError *lerr = NULL;

		check (!nemo_shortcut_win32_launch (missing, &lerr));
		check (lerr != NULL);
		check (lerr == NULL || lerr->message != NULL);

		g_clear_error (&lerr);
		g_free (missing);
	}

	/* The other kind of link. Windows wants Developer Mode or an elevated run
	 * for this, so on a box that has neither there is nothing to check but the
	 * refusal itself. */
	{
		char *sym = g_build_filename (dir, "symlink-to-target", NULL);
		GError *serr = NULL;
		gboolean made = nemo_win32_link_create_default (target, sym, &serr);

		check (made == nemo_win32_link_symlinks_allowed ());

		if (made) {
			char *back = NULL;
			GFile *f = g_file_new_for_path (sym);
			GFileInfo *info;

			/* Not g_file_test: its symlink question always answers no on
			 * Windows. GIO reads the reparse point and does not. */
			info = g_file_query_info (f, G_FILE_ATTRIBUTE_STANDARD_IS_SYMLINK,
						  G_FILE_QUERY_INFO_NOFOLLOW_SYMLINKS, NULL, NULL);
			check (info != NULL && g_file_info_get_is_symlink (info));
			g_clear_object (&info);
			g_object_unref (f);

			check (g_file_get_contents (sym, &back, NULL, NULL));
			check (back != NULL);
			g_free (back);

			/* A name already taken has to read as a clash, or the caller
			 * cannot uniquify it and just gives up. */
			g_clear_error (&serr);
			check (!nemo_win32_link_create_default (target, sym, &serr));
			check (serr != NULL && g_error_matches (serr, G_IO_ERROR, G_IO_ERROR_EXISTS));

			g_unlink (sym);
		} else {
			check (serr != NULL);
			g_print ("SKIP symlink contents (this box does not allow symlinks)\n");
		}

		g_clear_error (&serr);
		g_free (sym);
	}

	/* A link to a folder is a junction, which Windows allows with no privilege
	 * at all - so this one has to work whatever the box is set to. */
	{
		char *folder = g_build_filename (dir, "folder", NULL);
		char *inside = g_build_filename (folder, "inside.txt", NULL);
		char *link = g_build_filename (dir, "link-to-folder", NULL);
		char *through = g_build_filename (link, "inside.txt", NULL);
		char *back = NULL;
		GError *jerr = NULL;
		GFile *f;
		GFileInfo *info;

		check (g_mkdir (folder, 0755) == 0);
		check (g_file_set_contents (inside, "junction", -1, NULL));

		check (nemo_win32_link_create_default (folder, link, &jerr));
		check (jerr == NULL);

		/* Reads through to the target, and is a reparse point rather than a
		 * second copy of the folder. */
		check (g_file_get_contents (through, &back, NULL, NULL));
		check (g_strcmp0 (back, "junction") == 0);
		g_free (back);

		f = g_file_new_for_path (link);
		info = g_file_query_info (f, G_FILE_ATTRIBUTE_STANDARD_IS_SYMLINK,
					  G_FILE_QUERY_INFO_NOFOLLOW_SYMLINKS, NULL, NULL);
		check (info != NULL && g_file_info_get_is_symlink (info));
		g_clear_object (&info);
		g_object_unref (f);

		/* And a junction specifically, not a symlink. This box runs elevated,
		 * so a symlink would have worked too and every other check here would
		 * still pass - the tag is the only thing that tells them apart. */
		check (reparse_tag_of (link) == IO_REPARSE_TAG_MOUNT_POINT);

		/* A taken name still has to read as a clash so the caller can
		 * uniquify it. */
		check (!nemo_win32_link_create_default (folder, link, &jerr));
		check (jerr != NULL && g_error_matches (jerr, G_IO_ERROR, G_IO_ERROR_EXISTS));
		g_clear_error (&jerr);

		/* Removing the link must not take the target's contents with it. */
		check (g_rmdir (link) == 0);
		check (g_file_test (inside, G_FILE_TEST_EXISTS));

		g_unlink (inside);
		g_rmdir (folder);
		g_free (through);
		g_free (link);
		g_free (inside);
		g_free (folder);
	}

	g_unlink (lnk);
	g_unlink (target);
	g_rmdir (dir);
	g_free (lnk);
	g_free (target);
	g_free (dir);

	if (failures == 0) {
		g_print ("shortcut-win32: all checks passed\n");
	}
	return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
