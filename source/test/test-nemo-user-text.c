/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */

/* test-nemo-user-text.c - Windows paths in text a user writes.

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

/* On Windows a backslash in a file or setting a user writes is a path
 * character, never an escape. Elsewhere a desktop file keeps the spec's
 * escapes. The Windows rules are checked on every platform, then key files
 * through the platform's own reader, then link and action files as a user
 * wrote them. */

#include <config.h>

#include <string.h>

#include <glib.h>
#include <glib/gstdio.h>
#include <gtk/gtk.h>

#include <libnemo-private/nemo-action.h>
#include <libnemo-private/nemo-config.h>
#include <libnemo-private/nemo-global-preferences.h>
#include <libnemo-private/nemo-link.h>
#include <libnemo-private/nemo-user-text.h>

#include "test-scratch.h"
#include "test-check.h"

static char *scratch;

typedef struct {
	char *uri;
	char *name;
	GIcon *icon;
} LinkInfo;

static void
read_link (const char *text, LinkInfo *info)
{
	gboolean launcher = FALSE, foreign = FALSE;

	memset (info, 0, sizeof *info);
	nemo_link_get_link_info_given_file_contents (text, (int) strlen (text), NULL,
						     &info->uri, &info->name, &info->icon,
						     &launcher, &foreign);
}

static void
clear_link (LinkInfo *info)
{
	g_free (info->uri);
	g_free (info->name);
	g_clear_object (&info->icon);
}

static gboolean
same_list (char **have, const char *const *want)
{
	guint i;

	if (have == NULL) {
		return FALSE;
	}
	for (i = 0; want[i] != NULL; i++) {
		if (g_strcmp0 (have[i], want[i]) != 0) {
			return FALSE;
		}
	}
	return have[i] == NULL;
}

static gboolean
value_is (const char *raw, const char *want)
{
	g_autofree char *have = nemo_user_text_windows_value (raw);

	return g_strcmp0 (have, want) == 0;
}

static gboolean
list_is (const char *raw, const char *const *want)
{
	g_auto (GStrv) have = nemo_user_text_windows_list (raw, NULL);

	return same_list (have, want);
}

static gboolean
split_is (const char *line, const char *const *want)
{
	g_auto (GStrv) have = NULL;
	int argc = -1;

	if (!nemo_user_text_windows_split (line, &argc, &have, NULL)) {
		return FALSE;
	}
	return argc == (int) g_strv_length ((char **) want) && same_list (have, want);
}

static gboolean
quote_comes_back (const char *text)
{
	g_autofree char *quoted = nemo_user_text_windows_quote (text);
	const char *want[] = { text, NULL };

	return split_is (quoted, want);
}

static void
check_windows_rules (void)
{
	GError *error = NULL;
	gsize n = 99;

	/* As written, unless every backslash is doubled. */
	check (value_is ("C:\\Tools\\new.exe", "C:\\Tools\\new.exe"));
	check (value_is ("C:\\\\Tools\\\\new.exe", "C:\\Tools\\new.exe"));
	check (value_is ("\\\\box\\share\\a.txt", "\\\\box\\share\\a.txt"));
	check (value_is ("\\\\\\\\box\\\\share\\\\a.txt", "\\\\box\\share\\a.txt"));
	check (value_is ("C:\\Tools\\", "C:\\Tools\\"));
	check (value_is ("tab\\there", "tab\\there"));
	check (value_is ("plain", "plain"));
	/* The one spelling both readings share. A share needs a name after it. */
	check (value_is ("\\\\box", "\\box"));

	{
		const char *two[] = { "a", "b", NULL };
		const char *dirs[] = { "C:\\x\\", "D:\\y\\", NULL };
		const char *doubled[] = { "C:\\x\\", "D:\\y", NULL };
		const char *none[] = { NULL };
		const char *blank[] = { "", NULL };
		g_auto (GStrv) empty = nemo_user_text_windows_list ("", &n);

		check (list_is ("a;b;", two));
		check (list_is ("a;b", two));
		check (list_is ("C:\\x\\;D:\\y\\;", dirs));
		check (list_is ("C:\\\\x\\\\;D:\\\\y;", doubled));
		check (empty != NULL && n == 0);
		check (list_is ("", none));
		check (list_is (";", blank));
	}

	{
		const char *words[] = { "C:\\Tools\\x.exe", "a\\b", "C:\\Program Files\\y", "", "end", NULL };
		const char *said[] = { "say \"hi\"", NULL };
		const char *joined[] = { "C:\\dir\\x.exe", NULL };
		const char *spaced[] = { "a\\", "b", NULL };
		const char *blanks[] = { "a", "b", "c", NULL };
		const char *single[] = { "'it''s'", NULL };
		g_auto (GStrv) argv = NULL;

		check (split_is ("C:\\Tools\\x.exe a\\b \"C:\\Program Files\\y\" \"\" end", words));
		check (split_is ("\"say \"\"hi\"\"\"", said));
		check (split_is ("\"C:\\dir\\\"x.exe", joined));
		check (split_is ("a\\ b", spaced));
		check (split_is (" a\tb\r\nc ", blanks));
		check (split_is ("'it''s'", single));

		check (!nemo_user_text_windows_split ("x \"open", NULL, &argv, &error));
		check (g_error_matches (error, G_SHELL_ERROR, G_SHELL_ERROR_BAD_QUOTING));
		g_clear_error (&error);
		check (!nemo_user_text_windows_split (" \t ", NULL, &argv, &error));
		check (g_error_matches (error, G_SHELL_ERROR, G_SHELL_ERROR_EMPTY_STRING));
		g_clear_error (&error);
	}

	check (quote_comes_back ("plain"));
	check (quote_comes_back ("C:\\a b\\"));
	check (quote_comes_back ("say \"hi\""));
	check (quote_comes_back (""));
	check (quote_comes_back ("it's"));
}

/* Through the platform's own reader and writer. */
static void
check_key_files (void)
{
	/* GKeyFile drops a translation for C as it loads, so under C the plain
	   value is the answer. */
	const char *lang = g_get_language_names ()[0];
	gboolean translated = g_strcmp0 (lang, "C") != 0;
	g_autofree char *text = g_strdup_printf ("[G]\nA=C:\\Tools\\new\nB=C:\\\\Tools\\\\new\n"
						 "L=C:\\\\x;D:\\\\y;\nN=plain\nN[%s]=C:\\\\Loc\\\\x\n"
						 "P=plain\nP[C]=c\n", lang);
	GKeyFile *key_file = g_key_file_new ();
	GError *error = NULL;
	g_autofree char *a = NULL, *b = NULL, *name = NULL, *plain = NULL, *c_back = NULL;
	g_auto (GStrv) list = NULL;
	const char *dirs[] = { "C:\\x", "D:\\y", NULL };

	check (g_key_file_load_from_data (key_file, text, -1, G_KEY_FILE_NONE, NULL));

	a = nemo_user_text_get_string (key_file, "G", "A", &error);
	b = nemo_user_text_get_string (key_file, "G", "B", NULL);
	list = nemo_user_text_get_string_list (key_file, "G", "L", NULL, NULL);
	name = nemo_user_text_get_locale_string (key_file, "G", "N", NULL);
#ifdef G_OS_WIN32
	check (g_strcmp0 (a, "C:\\Tools\\new") == 0);
	check (error == NULL);
#else
	check (a == NULL && error != NULL);
#endif
	g_clear_error (&error);
	check (g_strcmp0 (b, "C:\\Tools\\new") == 0);
	check (same_list (list, dirs));
	check (g_strcmp0 (name, translated ? "C:\\Loc\\x" : "plain") == 0);

	/* C is the plain value whatever the GLib, so a key[C] line is never read
	   and never written. */
	plain = nemo_user_text_get_locale_string (key_file, "G", "P", NULL);
	check (g_strcmp0 (plain, "plain") == 0);
	nemo_user_text_set_locale_string (key_file, "G", "P", "C", "set");
	c_back = g_key_file_get_value (key_file, "G", "P", NULL);
	check (g_strcmp0 (c_back, "set") == 0);

	check (nemo_user_text_get_string (key_file, "G", "missing", &error) == NULL);
	check (g_error_matches (error, G_KEY_FILE_ERROR, G_KEY_FILE_ERROR_KEY_NOT_FOUND));
	g_clear_error (&error);

	/* Whatever goes in comes back. On Windows it goes in as written where
	   that reads back the same. */
	{
		const char *values[] = { "C:\\x y\\z", "\\\\box\\share", "\\\\box", "a\\\\b", "tab\there", NULL };
		guint i;

		for (i = 0; values[i] != NULL; i++) {
			g_autofree char *back = NULL;

			nemo_user_text_set_string (key_file, "S", "v", values[i]);
			back = nemo_user_text_get_string (key_file, "S", "v", NULL);
			check (g_strcmp0 (back, values[i]) == 0);
		}
#ifdef G_OS_WIN32
		{
			g_autofree char *raw = NULL;

			nemo_user_text_set_string (key_file, "S", "v", "C:\\x y\\z");
			raw = g_key_file_get_value (key_file, "S", "v", NULL);
			check (g_strcmp0 (raw, "C:\\x y\\z") == 0);
		}
#endif
	}

	g_key_file_free (key_file);
}

#ifdef G_OS_WIN32
static gboolean
icon_is_file (GIcon *icon, const char *path)
{
	g_autofree char *have = NULL;

	if (icon == NULL || !G_IS_FILE_ICON (icon)) {
		return FALSE;
	}
	have = g_file_get_path (g_file_icon_get_file (G_FILE_ICON (icon)));
	return g_strcmp0 (have, path) == 0;
}
#endif

static void
check_links (void)
{
	LinkInfo info;

	/* Doubled, as GKeyFile writes it. Reads the same everywhere. */
	read_link ("[Desktop Entry]\nType=Link\nName=pic\nURL=https://example.com/\n"
		   "Icon=C:\\\\icons\\\\pic.png\n", &info);
	check (g_strcmp0 (info.name, "pic") == 0);
#ifdef G_OS_WIN32
	check (icon_is_file (info.icon, "C:\\icons\\pic.png"));
#endif
	clear_link (&info);

	/* A spec escape. Only Windows reads it as written. */
	read_link ("[Desktop Entry]\nType=Link\nName=tab\\there\nURL=https://example.com/\n", &info);
#ifdef G_OS_WIN32
	check (g_strcmp0 (info.name, "tab\\there") == 0);
#else
	check (g_strcmp0 (info.name, "tab\there") == 0);
#endif
	clear_link (&info);

#ifdef G_OS_WIN32
	{
		g_autofree char *want = g_filename_to_uri ("C:\\Users\\new\\note.txt", NULL, NULL);
		g_autofree char *unc = g_filename_to_uri ("\\\\box\\share\\a.txt", NULL, NULL);

		read_link ("[Desktop Entry]\nType=Link\nName=Notes in C:\\Tools\\new\n"
			   "URL=C:\\Users\\new\\note.txt\nIcon=C:\\icons\\note.png\n", &info);
		check (g_strcmp0 (info.name, "Notes in C:\\Tools\\new") == 0);
		check (g_strcmp0 (info.uri, want) == 0);
		check (icon_is_file (info.icon, "C:\\icons\\note.png"));
		clear_link (&info);

		read_link ("[Desktop Entry]\nType=Link\nName=share\nURL=\\\\box\\share\\a.txt\n", &info);
		check (g_strcmp0 (info.uri, unc) == 0);
		clear_link (&info);
	}
#endif
}

#ifdef G_OS_WIN32
static void
check_action (void)
{
	g_autofree char *path = g_build_filename (scratch, "tools.nemo_action", NULL);
	NemoAction *action;
	GIcon *icon = NULL;

	check (g_file_set_contents (path, "[Nemo Action]\nName=Open in C:\\Tools\\new\n"
				    "Comment=Runs C:\\Tools\\new\\view.exe\n"
				    "Icon-Name=C:\\icons\\act.png\nExec=C:\\Tools\\new\\view.exe %F\n"
				    "Selection=any\nExtensions=any;\n", -1, NULL));
	action = nemo_action_new ("tools", path);
	check (action != NULL);
	if (action == NULL) {
		return;
	}

	check (g_strcmp0 (nemo_action_get_orig_label (action), "Open in C:\\Tools\\new") == 0);
	g_object_get (action, "gicon", &icon, NULL);
	check (icon_is_file (icon, "C:\\icons\\act.png"));
	g_clear_object (&icon);
	g_object_unref (action);
}
#endif

int
main (int argc, char *argv[])
{
	scratch = test_scratch_config_home ("nemo-user-text-XXXXXX");
	if (scratch == NULL) {
		g_printerr ("FAIL: no scratch folder\n");
		return 1;
	}

	check_windows_rules ();
	check_key_files ();
	check_links ();

#ifdef G_OS_WIN32
	if (gtk_init_check (&argc, &argv)) {
		nemo_global_preferences_init ();
		check_action ();
		nemo_config_shutdown ();
	} else {
		g_print ("no display, action file not checked\n");
	}
#else
	(void) argc;
	(void) argv;
#endif

	g_free (scratch);

	if (failures > 0) {
		g_printerr ("%d check(s) failed\n", failures);
		return 1;
	}

	g_print ("OK\n");
	return 0;
}
