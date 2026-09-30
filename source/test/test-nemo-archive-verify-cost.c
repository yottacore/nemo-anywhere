/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */

/* test-nemo-archive-verify-cost.c - how long reading an archive back takes.

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

/* The check that runs before originals are deleted has to find every folder
 * in the archive. A writer may store only the files, and then a folder is
 * there when anything under it is. Answering that by walking every entry once
 * per folder made the check grow with folders times entries.
 *
 * So the same tree is checked against two archives of it: one with its folder
 * entries and one without. Both hold the same files plus a pile of others, as
 * a real archive of more than the selection would. The one without may take
 * at most twice as long as the one with. Today the two take about the same;
 * with the walk put back, the one without takes several times as long. Both
 * are timed in turn, three times each, and the best of each is compared, so a
 * busy box slows both sides alike. */

#include <config.h>

#include <archive.h>
#include <archive_entry.h>
#include <glib/gstdio.h>
#include <gio/gio.h>

#include <libnemo-private/nemo-archive.h>

#include "test-scratch.h"
#include "test-check.h"

#define FOLDERS 4000
#define OTHERS  40000
#define ROUNDS  3

static gboolean
add_entry (struct archive *out,
	   const char     *name,
	   gboolean        is_dir)
{
	struct archive_entry *entry = archive_entry_new ();
	gboolean ok;

	archive_entry_set_pathname (entry, name);
	archive_entry_set_filetype (entry, is_dir ? AE_IFDIR : AE_IFREG);
	archive_entry_set_perm (entry, is_dir ? 0755 : 0644);
	archive_entry_set_size (entry, is_dir ? 0 : 1);

	ok = archive_write_header (out, entry) == ARCHIVE_OK &&
	     (is_dir || archive_write_data (out, "x", 1) == 1);

	archive_entry_free (entry);
	return ok;
}

/* top/, then top/dNNNN/f for every folder, the folder entries only when asked,
   and other/NNNNN entries that are nowhere in the selection. */
static gboolean
write_archive (const char *path,
	       gboolean    with_folders)
{
	struct archive *out = archive_write_new ();
	char name[64];
	gboolean ok;
	int i;

	archive_write_set_format_zip (out);
	archive_write_set_options (out, "zip:compression=store");
	ok = archive_write_open_filename (out, path) == ARCHIVE_OK;

	if (ok && with_folders) {
		ok = add_entry (out, "top/", TRUE);
	}
	for (i = 0; ok && i < FOLDERS; i++) {
		if (with_folders) {
			g_snprintf (name, sizeof name, "top/d%04d/", i);
			ok = add_entry (out, name, TRUE);
		}
		g_snprintf (name, sizeof name, "top/d%04d/f", i);
		ok = ok && add_entry (out, name, FALSE);
	}
	for (i = 0; ok && i < OTHERS; i++) {
		g_snprintf (name, sizeof name, "other/%05d", i);
		ok = add_entry (out, name, FALSE);
	}

	ok = archive_write_close (out) == ARCHIVE_OK && ok;
	archive_write_free (out);
	return ok;
}

static void
make_tree (const char *top)
{
	int i;

	for (i = 0; i < FOLDERS; i++) {
		char *dir = g_strdup_printf ("%s/d%04d", top, i);
		char *file = g_build_filename (dir, "f", NULL);

		g_mkdir_with_parents (dir, 0700);
		check (g_file_set_contents (file, "x", 1, NULL));
		g_free (file);
		g_free (dir);
	}
}

/* Microseconds for one check, or -1 when it does not pass. */
static gint64
time_verify (GFile                    *archive,
	     GList                    *sources,
	     const NemoArchiveOptions *options)
{
	char *reason = NULL;
	gint64 start = g_get_monotonic_time ();
	gboolean ok = nemo_archive_verify (archive, sources, options,
					   NEMO_ARCHIVE_BACKEND_LIBARCHIVE, NULL, &reason);
	gint64 spent = g_get_monotonic_time () - start;

	if (!ok) {
		g_printerr ("verify refused: %s\n", reason != NULL ? reason : "(no reason)");
	}
	g_free (reason);

	return ok ? spent : -1;
}

int
main (int argc, char *argv[])
{
	char *tmp = test_scratch_dir ("nemo-verify-cost-XXXXXX", NULL);
	char *top = g_build_filename (tmp, "top", NULL);
	char *with_path = g_build_filename (tmp, "with.zip", NULL);
	char *flat_path = g_build_filename (tmp, "flat.zip", NULL);
	GFile *with_folders, *flat;
	GList *sources;
	NemoArchiveOptions options;
	gint64 best_with = G_MAXINT64, best_flat = G_MAXINT64;
	int round;

	make_tree (top);
	check (write_archive (with_path, TRUE));
	check (write_archive (flat_path, FALSE));

	with_folders = g_file_new_for_path (with_path);
	flat = g_file_new_for_path (flat_path);
	sources = g_list_append (NULL, g_file_new_for_path (top));

	nemo_archive_options_init (&options);
	options.format = NEMO_ARCHIVE_FORMAT_ZIP;

	for (round = 0; round < ROUNDS && failures == 0; round++) {
		gint64 spent;

		spent = time_verify (with_folders, sources, &options);
		check (spent >= 0);
		best_with = MIN (best_with, spent);

		spent = time_verify (flat, sources, &options);
		check (spent >= 0);
		best_flat = MIN (best_flat, spent);
	}

	if (failures == 0) {
		g_print ("%d folders, %d other entries: %.3f s with folder entries, %.3f s without\n",
			 FOLDERS, OTHERS, best_with / 1e6, best_flat / 1e6);
		check (best_flat <= 2 * best_with);
	}

	g_list_free_full (sources, g_object_unref);
	g_object_unref (flat);
	g_object_unref (with_folders);
	nemo_archive_options_clear (&options);
	g_free (flat_path);
	g_free (with_path);
	g_free (top);
	g_free (tmp);

	if (failures == 0) {
		g_print ("nemo-archive-verify-cost: all checks passed\n");
	}

	return failures == 0 ? 0 : 1;
}
