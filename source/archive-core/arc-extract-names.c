/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */

/* arc-extract-names.c - which names unpack, and where to.

   Copyright © 2026 t00mietum (CryptogID: ปʬϝღถɔ4რఠΔթะ9ƾǝu).

   This program is free software; you can redistribute it and/or
   modify it under the terms of the GNU General Public License as
   published by the Free Software Foundation; version 2 of the
   License.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, see <http://www.gnu.org/licenses/>.
*/

#include "arc-extract-names.h"

#include <string.h>

/* Suffixes we offer to unpack, longest first so ".tar.gz" is not read as ".gz".
   Much wider than the list we can write - libarchive reads a great deal it
   cannot produce - and a name is the only thing there is to go on, since on
   win32 a file's mime type is its extension. */
static const char * const archive_extensions[] = {
	".tar.gz", ".tar.xz", ".tar.bz2", ".tar.zst", ".tar.lz", ".tar.lzma", ".tar.z",
	".tgz", ".txz", ".tbz2", ".tbz", ".tzst",
	".zip", ".jar", ".war", ".ear", ".cbz",
	".7z", ".rar", ".cbr",
	".tar", ".cab", ".iso", ".lha", ".lzh", ".cpio", ".xar", ".pax",
	".gz", ".bz2", ".xz", ".zst", ".lz", ".lzma", ".z",
	NULL
};

/* 7z numbers a split archive "name.7z.001", so the name to read the format off
   is the one underneath. Three digits is what it writes, including for the only
   volume. Returns how much of the end to ignore, or 0 for an ordinary name. */
static gsize
volume_suffix_len (const char *name, gsize len)
{
	gsize i;

	if (len < 5 || name[len - 4] != '.') {
		return 0;
	}

	for (i = len - 3; i < len; i++) {
		if (!g_ascii_isdigit (name[i])) {
			return 0;
		}
	}

	return 4;
}

static const char *
matching_extension (const char *name)
{
	gsize len;
	int i;

	if (name == NULL) {
		return NULL;
	}

	len = strlen (name);
	len -= volume_suffix_len (name, len);

	for (i = 0; archive_extensions[i] != NULL; i++) {
		gsize ext_len = strlen (archive_extensions[i]);

		/* A suffix only, and never the whole name - ".zip" on its own is
		   a file called .zip, not an archive of nothing. Length-limited,
		   since a volume number may still be sitting past `len`. */
		if (len > ext_len &&
		    g_ascii_strncasecmp (name + len - ext_len, archive_extensions[i], ext_len) == 0) {
			return archive_extensions[i];
		}
	}

	return NULL;
}

gboolean
arc_extract_is_archive_name (const char *name)
{
	return matching_extension (name) != NULL;
}

/* Where unpacking a split archive has to start. Any other volume answers with
   its ".001" sibling, and a name that is not a volume answers with itself. A
   missing sibling is left alone, so the failure reads as the archive it was. */
/* Returns: (transfer full) */
GFile *
arc_extract_first_volume (GFile *archive)
{
	char *name = g_file_get_basename (archive);
	gsize len = name != NULL ? strlen (name) : 0;
	GFile *parent;
	GFile *first = NULL;
	char *wanted;

	if (name == NULL || volume_suffix_len (name, len) == 0 ||
	    strcmp (name + len - 3, "001") == 0) {
		g_free (name);
		return g_object_ref (archive);
	}

	parent = g_file_get_parent (archive);
	if (parent == NULL) {
		g_free (name);
		return g_object_ref (archive);
	}

	wanted = g_strdup_printf ("%.*s001", (int) (len - 3), name);
	first = g_file_get_child (parent, wanted);

	if (!g_file_query_exists (first, NULL)) {
		g_clear_object (&first);
		first = g_object_ref (archive);
	}

	g_free (wanted);
	g_free (name);
	g_object_unref (parent);

	return first;
}

/* Returns: (transfer full): free with g_free */
char *
arc_extract_folder_name (const char *archive_name)
{
	const char *extension;
	char *base;
	gsize len;

	g_return_val_if_fail (archive_name != NULL, NULL);

	extension = matching_extension (archive_name);
	if (extension == NULL) {
		return NULL;
	}

	len = strlen (archive_name);
	len -= volume_suffix_len (archive_name, len);

	base = g_strndup (archive_name, len - strlen (extension));

	if (base[0] == '\0') {
		g_free (base);
		return NULL;
	}

	return base;
}

/* Returns: (transfer full): free with g_free */
char *
arc_extract_unique_name (const char *name,
			 guint       attempt)
{
	const char *dot;

	g_return_val_if_fail (name != NULL, NULL);

	/* A leading dot is the start of a hidden name, not an extension. */
	dot = name[0] != '\0' ? strrchr (name + 1, '.') : NULL;

	if (dot == NULL) {
		return g_strdup_printf ("%s (%u)", name, attempt);
	}

	return g_strdup_printf ("%.*s (%u)%s", (int) (dot - name), name, attempt, dot);
}

#ifdef G_OS_WIN32
/* "CON", "nul.txt", "com1 .log" and the rest of Windows' device names. The
   app keeps its own list, and reww9h2r holds the two together. */
static gboolean
is_device_name (const char *name)
{
	static const char * const devices[] = { "CON", "PRN", "AUX", "NUL" };
	gsize len = strcspn (name, ".");

	while (len > 0 && name[len - 1] == ' ') {
		len--;
	}
	if (len == 3) {
		for (guint i = 0; i < G_N_ELEMENTS (devices); i++) {
			if (g_ascii_strncasecmp (name, devices[i], 3) == 0) {
				return TRUE;
			}
		}
		return FALSE;
	}
	if (len < 4 || (g_ascii_strncasecmp (name, "COM", 3) != 0 && g_ascii_strncasecmp (name, "LPT", 3) != 0)) {
		return FALSE;
	}
	/* A digit, or superscript 1 to 3. */
	return (len == 4 && g_ascii_isdigit (name[3])) ||
	       (len == 5 && (guchar) name[3] == 0xc2 &&
		((guchar) name[4] == 0xb9 || (guchar) name[4] == 0xb2 || (guchar) name[4] == 0xb3));
}
#endif

/* Returns: (transfer full): free with g_free */
char *
arc_extract_sanitize_path (const char *entry_path)
{
	char *normalized;
	char *valid = NULL;
	char **parts;
	GPtrArray *kept;
	char *result;
	int i;

	g_return_val_if_fail (entry_path != NULL, NULL);

	if (!g_utf8_validate (entry_path, -1, NULL)) {
		valid = g_utf8_make_valid (entry_path, -1);
		entry_path = valid;
	}

	/* Archives written on Windows sometimes separate with backslashes, and
	   a backslash is not a name character on either platform once it is a
	   path inside an archive. */
	normalized = g_strdelimit (g_strdup (entry_path), "\\", '/');
	parts = g_strsplit (normalized, "/", -1);
	kept = g_ptr_array_new ();

	for (i = 0; parts[i] != NULL; i++) {
		const char *part = parts[i];
		gsize len = strlen (part);

		/* Empty means a leading, trailing or doubled separator; "." is
		   nothing; ".." would climb out of the folder being unpacked
		   into, and a drive letter would leave it altogether. */
		if (len == 0 || g_strcmp0 (part, ".") == 0 || g_strcmp0 (part, "..") == 0) {
			continue;
		}
		if (len == 2 && part[1] == ':' && g_ascii_isalpha (part[0])) {
			continue;
		}

#ifdef G_OS_WIN32
		/* What Windows won't take in a name becomes "_", as 7-Zip does, so
		   both backends agree. Left as it is, ":" writes into a hidden stream
		   of a file named by what comes before it, a trailing dot or space is
		   dropped, and "NUL" writes nowhere. */
		for (char *c = parts[i]; *c != '\0'; c++) {
			if ((guchar) *c < 0x20 || strchr (":*?\"<>|", *c) != NULL) {
				*c = '_';
			}
		}
		for (char *c = parts[i] + len; c > parts[i] && (c[-1] == '.' || c[-1] == ' '); c--) {
			c[-1] = '_';
		}
		if (is_device_name (parts[i])) {
			char *prefixed = g_strconcat ("_", parts[i], NULL);

			g_free (parts[i]);
			parts[i] = prefixed;
			part = prefixed;
		}
#endif

		g_ptr_array_add (kept, (gpointer) part);
	}

	g_ptr_array_add (kept, NULL);
	result = g_strjoinv ("/", (char **) kept->pdata);

	g_ptr_array_free (kept, TRUE);
	g_strfreev (parts);
	g_free (normalized);
	g_free (valid);

	if (result[0] == '\0') {
		g_free (result);
		return NULL;
	}

	return result;
}

gboolean
arc_extract_is_rar_name (const char *name)
{
	const char *extension = matching_extension (name);

	return g_strcmp0 (extension, ".rar") == 0 || g_strcmp0 (extension, ".cbr") == 0;
}
