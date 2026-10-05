/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */

/* test-nemo-thumbnail-jobs.c - counting thumbnail jobs for the status bar.

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

/* A job is counted once however often a file is asked for, is done when the
 * file's flag goes off or the file goes away, and a new batch starts from
 * nothing and tells whoever watches. */

#include <config.h>

#include <gtk/gtk.h>

#include <libnemo-private/nemo-file.h>
#include <libnemo-private/nemo-file-private.h>
#include <libnemo-private/nemo-global-preferences.h>
#include <libnemo-private/nemo-thumbnails.h>

#include "test-scratch.h"
#include "test-check.h"

static int batches_seen = 0;

static void
batch_started (G_GNUC_UNUSED gpointer data)
{
	batches_seen++;
}

static NemoFile *
fake_file (const char *name)
{
	g_autofree char *uri = g_strdup_printf ("file:///nemo-thumbnail-jobs-test/%s", name);

	return nemo_file_get_by_uri (uri);
}

static void
expect_jobs (guint want_done, guint want_waiting)
{
	guint done, waiting;

	nemo_thumbnail_jobs (&done, &waiting);
	check (done == want_done);
	check (waiting == want_waiting);
}

static void
test_counting (void)
{
	NemoFile *a = fake_file ("a.png");
	NemoFile *b = fake_file ("b.png");
	NemoFile *c = fake_file ("c.png");

	nemo_thumbnail_watch_jobs (batch_started, NULL);

	nemo_file_set_is_thumbnailing (a, TRUE);
	nemo_file_set_is_thumbnailing (b, TRUE);
	nemo_file_set_is_thumbnailing (c, TRUE);
	expect_jobs (0, 3);
	check (batches_seen == 1);

	/* Asked for again while it waits. */
	nemo_file_set_is_thumbnailing (a, TRUE);
	expect_jobs (0, 3);

	nemo_file_set_is_thumbnailing (a, FALSE);
	nemo_file_set_is_thumbnailing (a, FALSE);
	expect_jobs (1, 2);

	/* Gone while queued, so nothing comes back to clear the flag. */
	nemo_file_unref (c);
	expect_jobs (2, 1);

	nemo_file_set_is_thumbnailing (b, FALSE);
	expect_jobs (3, 0);
	check (batches_seen == 1);

	nemo_file_set_is_thumbnailing (b, TRUE);
	expect_jobs (0, 1);
	check (batches_seen == 2);

	nemo_thumbnail_unwatch_jobs (batch_started, NULL);
	nemo_file_set_is_thumbnailing (b, FALSE);
	nemo_file_set_is_thumbnailing (a, TRUE);
	check (batches_seen == 2);
	nemo_file_set_is_thumbnailing (a, FALSE);
	expect_jobs (1, 0);

	nemo_file_unref (a);
	nemo_file_unref (b);
}

static void
test_count_held (void)
{
	NemoFile *waiting = fake_file ("waiting.png");
	NemoFile *held = fake_file ("held.png");
	NemoFile *none = fake_file ("none.txt");
	NemoThumbnailLoaded loaded = { 0 };
	guint shown = 0, wanted = 0;

	loaded.pixbuf = gdk_pixbuf_new (GDK_COLORSPACE_RGB, FALSE, 8, 10, 10);
	loaded.from_store = TRUE;
	nemo_file_take_thumbnail (held, &loaded);
	nemo_file_set_is_thumbnailing (waiting, TRUE);

	nemo_file_count_thumbnail (waiting, &shown, &wanted);
	nemo_file_count_thumbnail (held, &shown, &wanted);
	nemo_file_count_thumbnail (none, &shown, &wanted);
	check (shown == 1);
	check (wanted == 2);

	/* A bigger copy on its way is not shown yet. */
	nemo_file_set_is_thumbnailing (held, TRUE);
	shown = wanted = 0;
	nemo_file_count_thumbnail (held, &shown, &wanted);
	check (shown == 0);
	check (wanted == 1);

	nemo_file_set_is_thumbnailing (held, FALSE);
	nemo_file_set_is_thumbnailing (waiting, FALSE);
	g_object_unref (loaded.pixbuf);
	nemo_file_unref (waiting);
	nemo_file_unref (held);
	nemo_file_unref (none);
}

/* A picture nothing has asked for yet still counts toward the total, and one
 * in the store is ready though not held. */
static void
test_count_whole_folder (void)
{
	NemoFile *later = fake_file ("later.png");
	NemoFile *stored = fake_file ("stored.png");
	NemoFile *failed = fake_file ("failed.png");
	guint shown = 0, wanted = 0;

	later->details->thumbnail_type_ok = TRUE;
	stored->details->thumbnail_in_store = TRUE;
	failed->details->thumbnail_type_ok = TRUE;
	failed->details->thumbnailing_failed = TRUE;

	nemo_file_count_thumbnail (later, &shown, &wanted);
	nemo_file_count_thumbnail (stored, &shown, &wanted);
	nemo_file_count_thumbnail (failed, &shown, &wanted);
	check (shown == 1);
	check (wanted == 2);

	nemo_file_unref (later);
	nemo_file_unref (stored);
	nemo_file_unref (failed);
}

static void
test_rendered_never_ahead (void)
{
	guint shown, wanted;

	/* 90 of 100 read back from the store, 1 of 4 built. */
	shown = 90;
	wanted = 100;
	nemo_thumbnail_rendered_for_bar (1, 3, &shown, &wanted);
	check (shown == 25);
	check (wanted == 100);

	/* Behind the build bar stays where it is. */
	shown = 10;
	nemo_thumbnail_rendered_for_bar (1, 3, &shown, &wanted);
	check (shown == 10);

	/* Nothing to count follows the build bar. */
	shown = wanted = 0;
	nemo_thumbnail_rendered_for_bar (2, 6, &shown, &wanted);
	check (shown == 2);
	check (wanted == 8);
}

int
main (int argc, char **argv)
{
	g_autofree char *tmp = NULL;

	gtk_init_check (&argc, &argv);

	tmp = test_scratch_config_home ("nemo-thumbnail-jobs-XXXXXX");
	if (tmp == NULL) {
		g_printerr ("FAIL could not make a temp dir\n");
		return EXIT_FAILURE;
	}

	nemo_global_preferences_init ();

	test_counting ();
	test_count_held ();
	test_count_whole_folder ();
	test_rendered_never_ahead ();

	if (failures == 0)
		g_print ("nemo-thumbnail-jobs: all checks passed\n");

	return failures == 0 ? 0 : 1;
}
