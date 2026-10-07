/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */

/* nemo-user-text.h - text a user writes by hand.

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

/* Key files a user writes (actions, search helpers, thumbnailers, links) and
 * command lines kept in the settings. On Windows a backslash there is a path
 * character, never an escape: see "Paths and desktop settings" in design.md.
 * Elsewhere the usual rules hold, the desktop entry spec's escapes and POSIX
 * shell quoting, so Linux reads these files as it always has. */

#ifndef NEMO_USER_TEXT_H
#define NEMO_USER_TEXT_H

#include <glib.h>

G_BEGIN_DECLS

/* A key file value as the user wrote it. NULL with @error set when the key is
 * not there or the text is not UTF-8.
 * Returns: (transfer full): free with g_free */
gchar   *nemo_user_text_get_string        (GKeyFile     *key_file,
					   const gchar  *group,
					   const gchar  *key,
					   GError      **error);

/* The same in the best language the file has, the way
 * g_key_file_get_locale_string picks one.
 * Returns: (transfer full): free with g_free */
gchar   *nemo_user_text_get_locale_string (GKeyFile     *key_file,
					   const gchar  *group,
					   const gchar  *key,
					   GError      **error);

/* A list split on semicolons. On Windows a semicolon always splits.
 * Returns: (transfer full): free with g_strfreev */
gchar  **nemo_user_text_get_string_list   (GKeyFile     *key_file,
					   const gchar  *group,
					   const gchar  *key,
					   gsize        *length,
					   GError      **error);

/* Stores @value so the readers above give it back. On Windows it goes in as
 * written wherever that reads back the same. */
void     nemo_user_text_set_string        (GKeyFile     *key_file,
					   const gchar  *group,
					   const gchar  *key,
					   const gchar  *value);
void     nemo_user_text_set_locale_string (GKeyFile     *key_file,
					   const gchar  *group,
					   const gchar  *key,
					   const gchar  *locale,
					   const gchar  *value);

/* A command line split into arguments. On Windows blanks split it, double
 * quotes group, "" inside quotes is one quote, and a backslash is itself.
 * Elsewhere g_shell_parse_argv. Errors are G_SHELL_ERROR, as there. */
gboolean nemo_user_text_split_command     (const gchar  *command_line,
					   gint         *argc,
					   gchar      ***argv,
					   GError      **error);

/* @text quoted so nemo_user_text_split_command gives it back as one argument.
 * Returns: (transfer full): free with g_free */
gchar   *nemo_user_text_quote             (const gchar  *text);

/* The Windows rules on their own, built everywhere so they can be tested
 * anywhere. A raw value, as g_key_file_get_value hands it over. */
gchar   *nemo_user_text_windows_value     (const gchar  *raw);
gchar  **nemo_user_text_windows_list      (const gchar  *raw,
					   gsize        *length);
gboolean nemo_user_text_windows_split     (const gchar  *command_line,
					   gint         *argc,
					   gchar      ***argv,
					   GError      **error);
gchar   *nemo_user_text_windows_quote     (const gchar  *text);

G_END_DECLS

#endif /* NEMO_USER_TEXT_H */
