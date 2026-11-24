/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */

/* eel-vfs-extensions.c - gnome-vfs extensions.  Its likely some of these will
                          be part of gnome-vfs in the future.

   Copyright (C) 1999, 2000 Eazel, Inc.

   The Gnome Library is free software; you can redistribute it and/or
   modify it under the terms of the GNU Library General Public License as
   published by the Free Software Foundation; either version 2 of the
   License, or (at your option) any later version.

   The Gnome Library is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   Library General Public License for more details.

   You should have received a copy of the GNU Library General Public
   License along with the Gnome Library; see the file COPYING.LIB.  If not,
   write to the Free Software Foundation, Inc., 51 Franklin Street - Suite 500,
   Boston, MA 02110-1335, USA.

   Authors: Darin Adler <darin@eazel.com>
	    Pavel Cisler <pavel@eazel.com>
	    Mike Fleming  <mfleming@eazel.com>
            John Sullivan <sullivan@eazel.com>
*/

#include <config.h>
#include "eel-vfs-extensions.h"
#include "eel-glib-extensions.h"
#include "eel-lib-self-check-functions.h"

#include <glib.h>
#include <glib/gi18n-lib.h>
#include <gio/gio.h>

#include "eel-string.h"

#include <string.h>
#include <stdlib.h>

gboolean
eel_uri_is_trash (const char *uri)
{
	return g_str_has_prefix (uri, "trash:");
}

gboolean
eel_uri_is_recent (const char *uri)
{
	return g_str_has_prefix (uri, "recent:");
}

gboolean
eel_uri_is_favorite (const char *uri)
{
    return g_str_has_prefix (uri, "favorites:");
}

gboolean
eel_uri_is_search (const char *uri)
{
	return g_str_has_prefix (uri, EEL_SEARCH_URI);
}

gboolean
eel_uri_is_desktop (const char *uri)
{
	return g_str_has_prefix (uri, EEL_DESKTOP_URI);
}

gboolean
eel_uri_is_network (const char *uri)
{
    return g_str_has_prefix (uri, "smb:") || g_str_has_prefix (uri, "network:");
}

gboolean
eel_vfs_supports_uri_scheme (const gchar *scheme)
{
   const gchar * const *supported;
   gint i;

   supported = g_vfs_get_supported_uri_schemes (g_vfs_get_default ());

   for (i = 0; supported[i] != NULL; i++) {
      if (g_strcmp0 (scheme, supported[i]) == 0) {
          return TRUE;
      }
   }

   return FALSE;
}

/* Find the variable reference starting at p, if there is one. Returns the name
 * bounds and where to go on from; name_start stays NULL when there isn't
 * one, including for a lone % or a $ with nothing usable after it. */
static void
find_variable (const char  *p,
               const char **name_start,
               const char **name_end,
               const char **next)
{
	const char *close;

	*name_start = NULL;

	if (*p == '%') {
		close = strchr (p + 1, '%');
		if (close != NULL && close > p + 1) {
			*name_start = p + 1;
			*name_end = close;
			*next = close + 1;
		}
		return;
	}

	if (*p != '$') {
		return;
	}

	if (p[1] == '{') {
		close = strchr (p + 2, '}');
		if (close != NULL && close > p + 2) {
			*name_start = p + 2;
			*name_end = close;
			*next = close + 1;
		}
		return;
	}

	close = p + 1;
	while (g_ascii_isalnum (*close) || *close == '_') {
		close++;
	}
	if (close > p + 1) {
		*name_start = p + 1;
		*name_end = close;
		*next = close;
	}
}

/* Names one platform sets and another doesn't, so a path typed for one box
 * still goes somewhere sensible on the other. The environment always wins.
 * The XDG folders and APPDATA and LOCALAPPDATA aren't here: APPDATA could be
 * the config or the data folder, LOCALAPPDATA the data folder or the cache. */
static const struct {
	const char   *name;
	const char *(*fallback) (void);
} stand_ins[] = {
	{ "HOME",            g_get_home_dir },
	{ "USERPROFILE",     g_get_home_dir },
	{ "TMP",             g_get_tmp_dir },
	{ "TEMP",            g_get_tmp_dir },
	{ "TMPDIR",          g_get_tmp_dir },
	{ "USER",            g_get_user_name },
	{ "USERNAME",        g_get_user_name },
};

/* %NAME% is case-blind as on Windows, so %UserProfile% matches; $home doesn't. */
static const char *
lookup_variable (const char *name, gboolean percent_form)
{
	const char *value = g_getenv (name);
	guint       i;

	for (i = 0; value == NULL && i < G_N_ELEMENTS (stand_ins); i++) {
		if (percent_form ? g_ascii_strcasecmp (name, stand_ins[i].name) == 0
		                 : strcmp (name, stand_ins[i].name) == 0) {
			value = stand_ins[i].fallback ();
		}
	}

	return value;
}

/* Returns: (transfer full): free with g_free */
char *
eel_expand_user_input (const char *text)
{
	GString    *out;
	const char *p;
	gboolean    changed = FALSE;

	if (text == NULL) {
		return NULL;
	}

	out = g_string_new (NULL);
	p = text;

#ifdef G_OS_WIN32
	/* Only here: on POSIX glib's own parse expands a leading ~, and knows the
	 * ~user form as well, which this doesn't. */
	if (p[0] == '~' && (p[1] == '\0' || p[1] == '/' || p[1] == '\\')) {
		g_string_append (out, g_get_home_dir ());
		p++;
		changed = TRUE;
	}
#endif

	while (*p != '\0') {
		const char *name_start, *name_end = NULL, *next = NULL;
		const char *value = NULL;
		char       *name;

		find_variable (p, &name_start, &name_end, &next);

		if (name_start != NULL) {
			name = g_strndup (name_start, name_end - name_start);
			value = lookup_variable (name, *p == '%');
			g_free (name);
		}

		/* A name nobody set stays exactly as typed - which is what keeps a
		 * folder with a % or a $ in its name reachable. */
		if (value == NULL) {
			g_string_append_c (out, *p);
			p++;
			continue;
		}

		g_string_append (out, value);
		changed = TRUE;
		p = next;
	}

	if (!changed) {
		g_string_free (out, TRUE);
		return NULL;
	}

	return g_string_free (out, FALSE);
}

/* The literal text wins, so a file with a backslash in its name stays
 * reachable. Only a missing local path with backslashes is tried again with
 * slashes. */
static GFile *
parse_with_slash_retry (const char *text)
{
	GFile *location;

	location = g_file_parse_name (text);

	/* A UNC path (\\host\share) is never converted, and probing it here would
	 * block the UI for the whole SMB timeout on a host that resolves but
	 * doesn't answer. The async folder load deals with that. */
	if (g_str_has_prefix (text, "\\\\")) {
		return location;
	}

	if (strchr (text, '\\') != NULL &&
	    g_file_is_native (location) &&
	    !g_file_query_exists (location, NULL)) {
		char *converted;
		GFile *retry;

		converted = g_strdelimit (g_strdup (text), "\\", '/');
		retry = g_file_parse_name (converted);
		g_free (converted);

		if (g_file_is_native (retry) &&
		    g_file_query_exists (retry, NULL)) {
			g_object_unref (location);
			location = retry;
		} else {
			g_object_unref (retry);
		}
	}

	return location;
}

/* Typed-location parsing that takes backslash separators everywhere.
 * Returns: (transfer full): unref with g_object_unref */
GFile *
eel_g_file_new_for_user_input (const char *text)
{
	GFile *location;
	char  *expanded;

	location = parse_with_slash_retry (text);

	/* No probing a share, as above. */
	if (g_str_has_prefix (text, "\\\\")) {
		return location;
	}

	/* A ~ or a variable only stands for a local path, so it's tried last, and
	 * only when the literal can't be opened. Expanding first is free and keeps
	 * the existence check off every other typed path. The expanded text gets
	 * the same slash retry, for %USERPROFILE%\Pictures off Windows. */
	expanded = eel_expand_user_input (text);

	if (expanded != NULL) {
		if (!g_file_is_native (location) ||
		    !g_file_query_exists (location, NULL)) {
			GFile *retry = parse_with_slash_retry (expanded);

			if (g_file_is_native (retry)) {
				g_object_unref (location);
				location = retry;
			} else {
				g_object_unref (retry);
			}
		}
		g_free (expanded);
	}

	return location;
}

/* Returns: (transfer full): free with g_free */
char *
eel_make_valid_utf8 (const char *name)
{
	GString *string;
	const char *remainder, *invalid;
	int remaining_bytes, valid_bytes;

	string = NULL;
	remainder = name;
	remaining_bytes = strlen (name);

	while (remaining_bytes != 0) {
		if (g_utf8_validate (remainder, remaining_bytes, &invalid)) {
			break;
		}
		valid_bytes = invalid - remainder;

		if (string == NULL) {
			string = g_string_sized_new (remaining_bytes);
		}
		g_string_append_len (string, remainder, valid_bytes);
		g_string_append_c (string, '?');

		remaining_bytes -= valid_bytes + 1;
		remainder = invalid + 1;
	}

	if (string == NULL) {
		return g_strdup (name);
	}

	g_string_append (string, remainder);
	g_string_append (string, _(" (invalid Unicode)"));
	g_assert (g_utf8_validate (string->str, -1, NULL));

	return g_string_free (string, FALSE);
}

/* Returns: (transfer none): points into @filename */
char *
eel_filename_get_extension_offset (const char *filename)
{
	char *end, *end2;
	const char *start;

	if (filename == NULL || filename[0] == '\0') {
		return NULL;
	}

	/* basename must have at least one char */
	start = filename + 1;

	end = strrchr (start, '.');
	if (end == NULL || end[1] == '\0') {
		return NULL;
	}

	if (end != start) {
		if (strcmp (end, ".gz") == 0 ||
		    strcmp (end, ".bz2") == 0 ||
		    strcmp (end, ".sit") == 0 ||
                    strcmp (end, ".bz") == 0 ||
                    strcmp (end, ".xz") == 0 ||
		    strcmp (end, ".Z") == 0) {
			end2 = end - 1;
			while (end2 > start &&
			       *end2 != '.') {
				end2--;
			}
			if (end2 != start) {
				end = end2;
			}
		}
	}

	return end;
}

/* Returns: (transfer full): free with g_free */
char *
eel_filename_strip_extension (const char * filename_with_extension)
{
	char *filename, *end;

	if (filename_with_extension == NULL) {
		return NULL;
	}

	filename = g_strdup (filename_with_extension);
	end = eel_filename_get_extension_offset (filename);

	if (end && end != filename) {
		*end = '\0';
	}

	return filename;
}

void
eel_filename_get_rename_region (const char           *filename,
				int                  *start_offset,
				int                  *end_offset)
{
	char *filename_without_extension;

	g_return_if_fail (start_offset != NULL);
	g_return_if_fail (end_offset != NULL);

	*start_offset = 0;
	*end_offset = 0;

	g_return_if_fail (filename != NULL);

	filename_without_extension = eel_filename_strip_extension (filename);
	*end_offset = g_utf8_strlen (filename_without_extension, -1);

	g_free (filename_without_extension);
}
