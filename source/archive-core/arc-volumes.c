/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */

/* arc-volumes.c - volume sizes, and the names a split is written under.

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

#include "arc-volumes.h"

#include <string.h>

#include <glib/gi18n.h>

/* Binary units, because that is what the sizes mean and what the parser has
   always computed. The label is translated whole, since the note is. */
static const struct {
	const char *size;
	const char *label;
} volume_presets[] = {
	{ "25 MiB",   N_("25 MiB (email)") },
	{ "100 MiB",  N_("100 MiB (upload limits)") },
	{ "700 MiB",  N_("700 MiB (CD)") },
	{ "2 GiB",    N_("2 GiB (old 32-bit tools)") },
	{ "4095 MiB", N_("4,095 MiB (max FAT32 size)") },
	{ "4480 MiB", N_("4,480 MiB (DVD)") },
	{ "8140 MiB", N_("8,140 MiB (dual-layer DVD)") },
	{ "23 GiB",   N_("23 GiB (Blu-ray)") },
};

/* Returns: (transfer none) */
const char *
arc_volume_preset (guint        index,
		   const char **size)
{
	if (index >= G_N_ELEMENTS (volume_presets)) {
		return NULL;
	}

	if (size != NULL) {
		*size = volume_presets[index].size;
	}

	return _(volume_presets[index].label);
}

/* The untranslated label too, so one remembered under another language still
   reads. */
int
arc_volume_preset_find (const char *text)
{
	char *trimmed;
	int found = -1;
	guint i;

	if (text == NULL) {
		return -1;
	}

	trimmed = g_strstrip (g_strdup (text));

	for (i = 0; i < G_N_ELEMENTS (volume_presets) && found < 0; i++) {
		if (strcmp (trimmed, volume_presets[i].size) == 0 ||
		    strcmp (trimmed, volume_presets[i].label) == 0 ||
		    strcmp (trimmed, _(volume_presets[i].label)) == 0) {
			found = (int) i;
		}
	}

	g_free (trimmed);

	return found;
}

/* Digits, grouped by commas the way the presets show them or not at all, then
   a point and more digits. "1,5" isn't a grouping, and could mean 1.5, so it
   isn't a number here. No sign, exponent, hex, inf or nan, which strtod would
   take. */
static gboolean
read_plain_number (const char  *text,
		   double      *value,
		   const char **end)
{
	GString *digits = g_string_new (NULL);
	const char *p = text;
	guint run = 0;
	gboolean grouped = FALSE;
	gboolean ok = TRUE;

	for (; g_ascii_isdigit (*p) || *p == ','; p++) {
		if (*p != ',') {
			g_string_append_c (digits, *p);
			run++;
			continue;
		}
		/* The first group is 1 to 3 digits, every one after it 3. */
		if (run == 0 || run > 3 || (grouped && run != 3)) {
			ok = FALSE;
		}
		grouped = TRUE;
		run = 0;
	}
	if (grouped && run != 3) {
		ok = FALSE;
	}

	if (*p == '.') {
		g_string_append_c (digits, *p++);
		while (g_ascii_isdigit (*p)) {
			g_string_append_c (digits, *p++);
		}
	}

	if (!ok || strpbrk (digits->str, "0123456789") == NULL) {
		g_string_free (digits, TRUE);
		return FALSE;
	}

	*value = g_ascii_strtod (digits->str, NULL);
	*end = p;
	g_string_free (digits, TRUE);

	return TRUE;
}

/* Sizes are read the way archivers write them: k, m, g and t are 1024-based,
   and a bare number is bytes. Anything that comes out under a byte, or past
   what a size can hold, is refused, since a size of 0 means no volumes. */
gboolean
arc_parse_size (const char *text,
		guint64    *bytes)
{
	const char *p;
	const char *end = NULL;
	double value;
	double total;
	guint64 multiplier;
	int preset;

	if (text == NULL) {
		return FALSE;
	}

	preset = arc_volume_preset_find (text);
	if (preset >= 0) {
		text = volume_presets[preset].size;
	}

	p = text;
	while (g_ascii_isspace (*p)) {
		p++;
	}
	if (*p == '\0') {
		return FALSE;
	}

	if (!read_plain_number (p, &value, &end) || value <= 0.0) {
		return FALSE;
	}

	while (g_ascii_isspace (*end)) {
		end++;
	}

	switch (g_ascii_tolower (*end)) {
	case '\0':
		multiplier = 1;
		break;
	case 'b':
		multiplier = 1;
		end++;
		break;
	case 'k':
		multiplier = G_GUINT64_CONSTANT (1024);
		end++;
		break;
	case 'm':
		multiplier = G_GUINT64_CONSTANT (1024) * 1024;
		end++;
		break;
	case 'g':
		multiplier = G_GUINT64_CONSTANT (1024) * 1024 * 1024;
		end++;
		break;
	case 't':
		multiplier = G_GUINT64_CONSTANT (1024) * 1024 * 1024 * 1024;
		end++;
		break;
	default:
		return FALSE;
	}

	/* A trailing "b" as in "MB", or "ib" as in "MiB", names the same unit
	   rather than another factor. Both spellings mean the binary one here. */
	if (multiplier > 1 && g_ascii_tolower (*end) == 'i') {
		end++;
	}
	if (multiplier > 1 && g_ascii_tolower (*end) == 'b') {
		end++;
	}
	while (g_ascii_isspace (*end)) {
		end++;
	}
	if (*end != '\0') {
		return FALSE;
	}

	/* 2^63, comfortably past any disk, and exact as a double. */
	total = value * (double) multiplier;
	if (total < 1.0 || total >= 9223372036854775808.0) {
		return FALSE;
	}

	if (bytes != NULL) {
		*bytes = (guint64) total;
	}

	return TRUE;
}

/* Returns: (transfer full): free with g_free */
char *
arc_format_size (guint64 bytes)
{
	const guint64 kb = G_GUINT64_CONSTANT (1024);
	const guint64 mb = kb * 1024;
	const guint64 gb = mb * 1024;

	if (bytes == 0) {
		return g_strdup ("");
	}
	if (bytes >= gb && bytes % gb == 0) {
		return g_strdup_printf ("%llu GiB", (unsigned long long) (bytes / gb));
	}
	if (bytes >= mb && bytes % mb == 0) {
		return g_strdup_printf ("%llu MiB", (unsigned long long) (bytes / mb));
	}
	if (bytes >= kb && bytes % kb == 0) {
		return g_strdup_printf ("%llu KiB", (unsigned long long) (bytes / kb));
	}

	return g_strdup_printf ("%llu", (unsigned long long) bytes);
}

/* Neither tool writes the name it was given once splitting is on. 7z appends
   ".001" to it; rar drops the extension and puts "partN" in front of it, with
   as many digits as it feels like. Both number the only volume too, which is
   what arc_collapse_volume undoes below.
   Returns: (transfer full): free with g_free */
char *
arc_volume_name (const char *archive_name,
		 ArcBackend  backend,
		 guint       volume,
		 guint       digits)
{
	const char *dot;
	char *stem;
	char *name;

	g_return_val_if_fail (archive_name != NULL, NULL);

	if (backend != ARC_BACKEND_RAR) {
		return g_strdup_printf ("%s.%0*u", archive_name, (int) digits, volume);
	}

	dot = strrchr (archive_name, '.');
	if (dot == NULL || dot == archive_name) {
		return g_strdup_printf ("%s.part%0*u", archive_name, (int) digits, volume);
	}

	stem = g_strndup (archive_name, (gsize) (dot - archive_name));
	name = g_strdup_printf ("%s.part%0*u%s", stem, (int) digits, volume, dot);
	g_free (stem);

	return name;
}

/* Returns: (transfer full): NULL for a root, which has no folder to hold
   volumes */
GFile *
arc_volume_file (GFile     *destination,
		 ArcBackend backend,
		 guint      volume,
		 guint      digits)
{
	GFile *parent = g_file_get_parent (destination);
	char *base;
	char *name;
	GFile *file;

	if (parent == NULL) {
		return NULL;
	}

	base = g_file_get_basename (destination);
	name = arc_volume_name (base, backend, volume, digits);
	file = g_file_get_child (parent, name);

	g_free (base);
	g_free (name);
	g_object_unref (parent);

	return file;
}

/* Renames a lone volume back to the name that was asked for. The digit count
   is whatever the first volume turned out to be written with, since rar picks
   it and 7z always uses three. Answers the file the result is in either way.
   Returns: (transfer full): unref with g_object_unref */
GFile *
arc_collapse_volume (GFile     *destination,
		     ArcBackend backend)
{
	guint digits;

	for (digits = 1; digits <= 3; digits++) {
		GFile *first = arc_volume_file (destination, backend, 1, digits);
		GFile *second;

		if (first == NULL) {
			return g_object_ref (destination);
		}

		if (!g_file_query_exists (first, NULL)) {
			g_object_unref (first);
			continue;
		}

		second = arc_volume_file (destination, backend, 2, digits);
		if (second != NULL && g_file_query_exists (second, NULL)) {
			/* A real split. The first volume is where unpacking starts. */
			g_object_unref (second);
			return first;
		}
		g_clear_object (&second);

		/* No overwrite flag: run_command clears the name before it starts,
		   and the tool wrote the volume instead of it. */
		if (g_file_move (first, destination, G_FILE_COPY_NONE,
				 NULL, NULL, NULL, NULL)) {
			g_object_unref (first);
			return g_object_ref (destination);
		}

		/* The rename is a tidy-up, not the job. Leaving the volume named
		   as the tool wrote it beats failing an archive that is fine. */
		return first;
	}

	return g_object_ref (destination);
}

/* 7z keeps the first volume, and the one it is writing, as "<volume>.tmp" until
   the whole run is done, so that is the name a stopped run leaves them under. */
static GFile *
unfinished_volume (GFile *file)
{
	GFile *parent = g_file_get_parent (file);
	char *name = g_file_get_basename (file);
	char *tmp_name = g_strconcat (name, ".tmp", NULL);
	GFile *tmp = g_file_get_child (parent, tmp_name);

	g_free (tmp_name);
	g_free (name);
	g_object_unref (parent);

	return tmp;
}

/* Walks the volumes a split actually left on disk, stopping at the first gap in
   each digit width, since only one width was ever used. */
void
arc_for_each_volume (GFile       *destination,
		     ArcBackend   backend,
		     void       (*action) (GFile *file, gpointer data),
		     gpointer     data)
{
	guint digits;

	for (digits = 1; digits <= 3; digits++) {
		guint volume;

		for (volume = 1; volume < 1000; volume++) {
			GFile *file = arc_volume_file (destination, backend, volume, digits);
			GFile *unfinished;
			gboolean there;

			if (file == NULL) {
				return;
			}

			there = g_file_query_exists (file, NULL);
			if (there) {
				action (file, data);
			}

			unfinished = unfinished_volume (file);
			if (g_file_query_exists (unfinished, NULL)) {
				action (unfinished, data);
				there = TRUE;
			}
			g_object_unref (unfinished);
			g_object_unref (file);

			if (!there) {
				break;
			}
		}
	}
}
