/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */

/* nemo-user-text.c - text a user writes by hand.

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

#include <config.h>
#include "nemo-user-text.h"

#include <string.h>

/* GKeyFile writes every backslash doubled, so its values have only even runs.
   One lone backslash anywhere means the value was written as is. */
static gboolean
written_doubled (const gchar *raw)
{
	const gchar *p = raw;
	gboolean any = FALSE;

	while ((p = strchr (p, '\\')) != NULL) {
		gsize run = strspn (p, "\\");

		if (run % 2 != 0) {
			return FALSE;
		}
		any = TRUE;
		p += run;
	}

	return any;
}

static gchar *
halve_backslashes (const gchar *text)
{
	GString *out = g_string_sized_new (strlen (text));
	const gchar *p;

	for (p = text; *p != '\0'; p++) {
		g_string_append_c (out, *p);
		if (*p == '\\' && p[1] == '\\') {
			p++;
		}
	}

	return g_string_free (out, FALSE);
}

/* Returns: (transfer full): free with g_free */
gchar *
nemo_user_text_windows_value (const gchar *raw)
{
	g_return_val_if_fail (raw != NULL, NULL);

	return written_doubled (raw) ? halve_backslashes (raw) : g_strdup (raw);
}

/* Returns: (transfer full): free with g_strfreev */
gchar **
nemo_user_text_windows_list (const gchar *raw,
			     gsize       *length)
{
	gboolean doubled;
	gchar **parts;
	guint n, i;

	g_return_val_if_fail (raw != NULL, NULL);

	/* Decided over the whole line, as one writer wrote it all. A path may end
	   in a backslash, so "\;" is that and then the next item. */
	doubled = written_doubled (raw);
	parts = g_strsplit (raw, ";", -1);
	n = g_strv_length (parts);

	/* Like GKeyFile, a closing semicolon makes no empty last item. */
	if (n > 0 && parts[n - 1][0] == '\0') {
		g_free (parts[n - 1]);
		parts[--n] = NULL;
	}

	for (i = 0; doubled && i < n; i++) {
		gchar *one = halve_backslashes (parts[i]);

		g_free (parts[i]);
		parts[i] = one;
	}

	if (length != NULL) {
		*length = n;
	}

	return parts;
}

gboolean
nemo_user_text_windows_split (const gchar   *command_line,
			      gint          *argc,
			      gchar       ***argv,
			      GError       **error)
{
	GPtrArray *words;
	GString *word = NULL;
	gboolean quoted = FALSE;
	const gchar *p;

	g_return_val_if_fail (command_line != NULL, FALSE);

	words = g_ptr_array_new_with_free_func (g_free);

	for (p = command_line; *p != '\0'; p++) {
		if (!quoted && g_ascii_isspace (*p)) {
			if (word != NULL) {
				g_ptr_array_add (words, g_string_free (word, FALSE));
				word = NULL;
			}
			continue;
		}

		/* A quote starts a word too, so "" on its own is an empty one. */
		if (word == NULL) {
			word = g_string_new (NULL);
		}

		if (*p == '"') {
			if (quoted && p[1] == '"') {
				g_string_append_c (word, '"');
				p++;
			} else {
				quoted = !quoted;
			}
			continue;
		}

		g_string_append_c (word, *p);
	}

	if (word != NULL) {
		g_ptr_array_add (words, g_string_free (word, FALSE));
	}

	if (quoted) {
		g_set_error (error, G_SHELL_ERROR, G_SHELL_ERROR_BAD_QUOTING,
			     "Text ended before the closing quote (the text was %s)", command_line);
		g_ptr_array_free (words, TRUE);
		return FALSE;
	}

	if (words->len == 0) {
		g_set_error_literal (error, G_SHELL_ERROR, G_SHELL_ERROR_EMPTY_STRING,
				     "Text was empty (or held only blanks)");
		g_ptr_array_free (words, TRUE);
		return FALSE;
	}

	if (argc != NULL) {
		*argc = (gint) words->len;
	}
	g_ptr_array_add (words, NULL);

	if (argv != NULL) {
		*argv = (gchar **) g_ptr_array_free (words, FALSE);
	} else {
		g_ptr_array_free (words, TRUE);
	}

	return TRUE;
}

/* Returns: (transfer full): free with g_free */
gchar *
nemo_user_text_windows_quote (const gchar *text)
{
	GString *out;
	const gchar *p;

	g_return_val_if_fail (text != NULL, NULL);

	out = g_string_sized_new (strlen (text) + 2);
	g_string_append_c (out, '"');
	for (p = text; *p != '\0'; p++) {
		if (*p == '"') {
			g_string_append_c (out, '"');
		}
		g_string_append_c (out, *p);
	}
	g_string_append_c (out, '"');

	return g_string_free (out, FALSE);
}

#ifdef G_OS_WIN32
static gchar *
checked_value (const gchar  *raw,
	       const gchar  *key,
	       GError      **error)
{
	if (!g_utf8_validate (raw, -1, NULL)) {
		g_set_error (error, G_KEY_FILE_ERROR, G_KEY_FILE_ERROR_UNKNOWN_ENCODING,
			     "Key file key \"%s\" is not UTF-8", key);
		return NULL;
	}

	return nemo_user_text_windows_value (raw);
}
#endif

/* Returns: (transfer full): free with g_free */
gchar *
nemo_user_text_get_string (GKeyFile     *key_file,
			   const gchar  *group,
			   const gchar  *key,
			   GError      **error)
{
#ifdef G_OS_WIN32
	gchar *raw = g_key_file_get_value (key_file, group, key, error);
	gchar *value;

	if (raw == NULL) {
		return NULL;
	}
	value = checked_value (raw, key, error);
	g_free (raw);

	return value;
#else
	GError *local = NULL;
	gchar *value = g_key_file_get_string (key_file, group, key, &local);

	/* GLib 2.72 hands back a value read past a bad escape, with the error
	   set too. 2.84 gives nothing; take that on every version. */
	if (local != NULL) {
		g_free (value);
		g_propagate_error (error, local);
		return NULL;
	}

	return value;
#endif
}

/* Returns: (transfer full): free with g_free */
gchar *
nemo_user_text_get_locale_string (GKeyFile     *key_file,
				  const gchar  *group,
				  const gchar  *key,
				  GError      **error)
{
	const gchar * const *languages = g_get_language_names ();
	guint i;

	/* C is the plain value, as GLib reads it since 2.84. Older GLib takes a
	   key[C] line, so the walk is ours on every version and platform. */
	for (i = 0; languages[i] != NULL && strcmp (languages[i], "C") != 0; i++) {
		gchar *full = g_strdup_printf ("%s[%s]", key, languages[i]);
		gchar *value = nemo_user_text_get_string (key_file, group, full, NULL);

		g_free (full);
		if (value != NULL) {
			return value;
		}
	}

	return nemo_user_text_get_string (key_file, group, key, error);
}

/* Returns: (transfer full): free with g_strfreev */
gchar **
nemo_user_text_get_string_list (GKeyFile     *key_file,
				const gchar  *group,
				const gchar  *key,
				gsize        *length,
				GError      **error)
{
#ifdef G_OS_WIN32
	gchar *raw = g_key_file_get_value (key_file, group, key, error);
	gchar **list;

	if (length != NULL) {
		*length = 0;
	}
	if (raw == NULL) {
		return NULL;
	}
	if (!g_utf8_validate (raw, -1, NULL)) {
		g_set_error (error, G_KEY_FILE_ERROR, G_KEY_FILE_ERROR_UNKNOWN_ENCODING,
			     "Key file key \"%s\" is not UTF-8", key);
		g_free (raw);
		return NULL;
	}
	list = nemo_user_text_windows_list (raw, length);
	g_free (raw);

	return list;
#else
	return g_key_file_get_string_list (key_file, group, key, length, error);
#endif
}

void
nemo_user_text_set_string (GKeyFile    *key_file,
			   const gchar *group,
			   const gchar *key,
			   const gchar *value)
{
#ifdef G_OS_WIN32
	gchar *back;

	/* A line break or a leading blank cannot go in as written. Rare enough
	   in a path or a name to leave to GKeyFile's escapes. */
	if (strpbrk (value, "\r\n") != NULL || value[0] == ' ' || value[0] == '\t') {
		g_key_file_set_string (key_file, group, key, value);
		return;
	}

	back = nemo_user_text_windows_value (value);
	if (strcmp (back, value) == 0) {
		g_key_file_set_value (key_file, group, key, value);
	} else {
		/* Every backslash doubled reads back as one. */
		gchar **parts = g_strsplit (value, "\\", -1);
		gchar *doubled = g_strjoinv ("\\\\", parts);

		g_key_file_set_value (key_file, group, key, doubled);
		g_free (doubled);
		g_strfreev (parts);
	}
	g_free (back);
#else
	g_key_file_set_string (key_file, group, key, value);
#endif
}

void
nemo_user_text_set_locale_string (GKeyFile    *key_file,
				  const gchar *group,
				  const gchar *key,
				  const gchar *locale,
				  const gchar *value)
{
	/* C goes in as the plain value, where the reader looks for it. */
	gchar *full = strcmp (locale, "C") == 0 ? g_strdup (key) : g_strdup_printf ("%s[%s]", key, locale);

	nemo_user_text_set_string (key_file, group, full, value);
	g_free (full);
}

gboolean
nemo_user_text_split_command (const gchar   *command_line,
			      gint          *argc,
			      gchar       ***argv,
			      GError       **error)
{
#ifdef G_OS_WIN32
	return nemo_user_text_windows_split (command_line, argc, argv, error);
#else
	return g_shell_parse_argv (command_line, argc, argv, error);
#endif
}

/* Returns: (transfer full): free with g_free */
gchar *
nemo_user_text_quote (const gchar *text)
{
#ifdef G_OS_WIN32
	return nemo_user_text_windows_quote (text);
#else
	return g_shell_quote (text);
#endif
}
