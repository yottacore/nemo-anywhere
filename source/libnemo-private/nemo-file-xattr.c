/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */

/* nemo-file-xattr.c - small named values kept beside a file's contents.

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

#include "nemo-file-xattr.h"

#include <string.h>

#ifdef G_OS_WIN32
#include "nemo-xattr-win32.h"
#endif

#ifndef G_OS_WIN32
/* GIO spells a `user` namespace attribute `xattr::name`, and puts the `user.`
 * back itself. */
static char *
gio_attribute (const char *name)
{
	return g_strconcat ("xattr::", name, NULL);
}
#endif

/* Returns: (transfer full): free with g_free */
char *
nemo_file_xattr_get (GFile *file, const char *name)
{
	g_return_val_if_fail (G_IS_FILE (file), NULL);
	g_return_val_if_fail (name != NULL, NULL);

#ifdef G_OS_WIN32
	{
		const char *path = g_file_peek_path (file);

		return path != NULL ? nemo_xattr_win32_get (path, name) : NULL;
	}
#else
	{
		g_autofree char *attribute = gio_attribute (name);
		g_autoptr (GFileInfo) info = NULL;
		char *value;

		/* Links are followed, because the value describes the contents,
		 * and the contents of a link are whatever it points at. */
		info = g_file_query_info (file, attribute, G_FILE_QUERY_INFO_NONE, NULL, NULL);
		if (info == NULL || !g_file_info_has_attribute (info, attribute))
			return NULL;

		value = g_file_info_get_attribute_as_string (info, attribute);
		if (value != NULL && strlen (value) >= NEMO_FILE_XATTR_MAX) {
			g_free (value);
			return NULL;
		}

		return value;
	}
#endif
}

gboolean
nemo_file_xattr_set (GFile *file, const char *name, const char *value)
{
	g_return_val_if_fail (G_IS_FILE (file), FALSE);
	g_return_val_if_fail (name != NULL, FALSE);
	g_return_val_if_fail (value != NULL, FALSE);

	if (strlen (value) >= NEMO_FILE_XATTR_MAX)
		return FALSE;

#ifdef G_OS_WIN32
	{
		const char *path = g_file_peek_path (file);

		return path != NULL && nemo_xattr_win32_set (path, name, value);
	}
#else
	{
		g_autofree char *attribute = gio_attribute (name);

		return g_file_set_attribute_string (file, attribute, value,
						    G_FILE_QUERY_INFO_NONE, NULL, NULL);
	}
#endif
}
