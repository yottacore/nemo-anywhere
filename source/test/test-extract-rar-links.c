/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */

/* test-extract-rar-links.c - a rar with a link libarchive can't read.

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

/* libarchive 3.8.9 and older stops at a rar link whose target is 128 bytes
 * or more: with an error when something follows it, and as if at the end
 * when nothing does. The rars here have a file on both sides of such a link,
 * the link last, and the link first, so in the first 2 the reader has put a
 * file on disk before it stops. With rar or 7-Zip there, the rest comes out
 * through the program, and nothing is asked about twice. With neither, the
 * job fails and says how far it got. On a reader that isn't affected
 * everything comes out either way. Needs rar to write them. */

#include "test.h"

#include <libnemo-private/nemo-archive.h>
#include <libnemo-private/nemo-config.h>
#include <libnemo-private/nemo-extract.h>

#include <stdlib.h>
#include <string.h>

#include "test-scratch.h"
#include "test-check.h"

#define EXTRACT_TIMEOUT_SECONDS 60
#define TARGET_BYTES 140

static gboolean job_finished;
static gboolean job_succeeded;
static char *long_target;

static void
extract_done (G_GNUC_UNUSED GFile    *destination_dir,
	      gboolean                success,
	      G_GNUC_UNUSED gpointer  data)
{
	job_succeeded = success;
	job_finished = TRUE;
	gtk_main_quit ();
}

static gboolean
give_up (G_GNUC_UNUSED gpointer data)
{
	g_printerr ("FAIL: unpacking did not finish within %d seconds\n",
		    EXTRACT_TIMEOUT_SECONDS);
	failures++;
	gtk_main_quit ();

	return G_SOURCE_REMOVE;
}

/* A question here would be about a file the job put there itself. */
static gboolean
refuse_questions (gpointer data)
{
	guint *asked = data;
	GList *windows, *l;

	windows = gtk_window_list_toplevels ();
	for (l = windows; l != NULL; l = l->next) {
		if (GTK_IS_DIALOG (l->data) && !GTK_IS_MESSAGE_DIALOG (l->data) &&
		    gtk_widget_get_mapped (l->data)) {
			(*asked)++;
			gtk_dialog_response (GTK_DIALOG (l->data), GTK_RESPONSE_DELETE_EVENT);
		}
	}
	g_list_free (windows);

	return G_SOURCE_CONTINUE;
}

static gboolean
extract (const char *archive_path,
	 const char *destination,
	 GtkWidget  *window)
{
	GList *archives = g_list_prepend (NULL, g_file_new_for_path (archive_path));
	GFile *dest = g_file_new_for_path (destination);
	guint asked = 0, timeout_id, refuse_id;

	g_mkdir_with_parents (destination, 0700);
	job_finished = FALSE;
	job_succeeded = FALSE;

	nemo_extract_files (archives, dest, NEMO_EXTRACT_HERE, GTK_WINDOW (window), extract_done, NULL);
	timeout_id = g_timeout_add_seconds (EXTRACT_TIMEOUT_SECONDS, give_up, NULL);
	refuse_id = g_timeout_add (100, refuse_questions, &asked);
	gtk_main ();
	g_source_remove (refuse_id);
	g_source_remove (timeout_id);

	check (job_finished);
	check (asked == 0);
	g_object_unref (dest);
	g_list_free_full (archives, g_object_unref);

	return job_succeeded;
}

/* What the message dialogs the job left up said, closed as they're read. */
static char *
take_messages (void)
{
	GString *said = g_string_new (NULL);
	GList *windows, *l;

	while (g_main_context_iteration (NULL, FALSE)) {
	}

	windows = gtk_window_list_toplevels ();
	for (l = windows; l != NULL; l = l->next) {
		char *text = NULL, *details = NULL;

		if (!GTK_IS_MESSAGE_DIALOG (l->data)) {
			continue;
		}
		g_object_get (l->data, "text", &text, "secondary-text", &details, NULL);
		g_string_append_printf (said, "%s\n%s\n", text != NULL ? text : "",
					details != NULL ? details : "");
		g_free (details);
		g_free (text);
		gtk_widget_destroy (GTK_WIDGET (l->data));
	}
	g_list_free (windows);

	return g_string_free (said, FALSE);
}

static gboolean
run_in (const char *dir, char **argv)
{
	char *out = NULL, *err = NULL;
	int status = -1;
	gboolean ok;

	ok = g_spawn_sync (dir, argv, NULL, G_SPAWN_DEFAULT, NULL, NULL,
			   &out, &err, &status, NULL) &&
	     g_spawn_check_wait_status (status, NULL);
	if (!ok) {
		g_printerr ("%s said: %s%s\n", argv[0], out != NULL ? out : "", err != NULL ? err : "");
	}
	g_free (out);
	g_free (err);

	return ok;
}

static void
check_contents (const char *dir,
		const char *name,
		const char *expected)
{
	char *path = g_build_filename (dir, name, NULL);
	char *contents = NULL;

	if (expected == NULL) {
		check (!g_file_test (path, G_FILE_TEST_EXISTS));
	} else if (g_file_get_contents (path, &contents, NULL, NULL)) {
		check (g_strcmp0 (contents, expected) == 0);
	} else {
		g_printerr ("FAIL: %s was not unpacked\n", path);
		failures++;
	}
	g_free (contents);
	g_free (path);
}

static void
check_link (const char *dir,
	    gboolean    expected)
{
	char *path = g_build_filename (dir, "m-link", NULL);
	char *target = g_file_read_link (path, NULL);

	if (expected) {
		check (g_strcmp0 (target, long_target) == 0);
	} else {
		check (target == NULL);
	}
	g_free (target);
	g_free (path);
}

/* before and after are the files on either side of the link, NULL for
   none. */
static void
check_archive (const char *tmp,
	       const char *archive,
	       const char *before,
	       const char *after,
	       const char *label,
	       gboolean    programs,
	       GtkWidget  *window)
{
	gboolean unreadable = arc_rar_target_unreadable (TARGET_BYTES);
	gboolean whole = programs || !unreadable;
	char *dest = g_build_filename (tmp, label, NULL);
	gboolean unpacked;
	char *said;

	unpacked = extract (archive, dest, window);
	said = take_messages ();

	if (unpacked != whole || (said[0] != '\0') == whole) {
		g_printerr ("FAIL: %s: unpacked %d, said: %s\n", label, unpacked, said);
		failures++;
	}
	if (before != NULL) {
		check_contents (dest, before, "first");
	}
	check_link (dest, whole);
	if (after != NULL) {
		check_contents (dest, after, whole ? "last" : NULL);
	}
	if (!whole) {
		check (strstr (said, "too long for the built-in reader") != NULL);
		check (strstr (said, "Install 7-Zip or RAR") != NULL);
		/* How far it got, where it got anywhere. */
		if (before != NULL) {
			check (strstr (said, "stopped at \"m-link\", after 1 file.") != NULL);
		} else {
			check (strstr (said, "stopped at") == NULL);
		}
	}
	if (!unreadable) {
		g_print ("note: this libarchive reads long rar link targets, so no program was needed\n");
	}

	g_free (said);
	g_free (dest);
}

int
main (int argc, char *argv[])
{
	char *rar = g_find_program_in_path ("rar");
	char *seven = NULL;
	GtkWidget *window;
	char *tmp, *work, *link, *config_home, *long_rar, *tail_rar, *head_rar;

	if (rar == NULL) {
		g_print ("SKIP: no rar here to write the archives\n");
		return 77;
	}

	/* Unpacking through a program clears its staging folder afterwards, and
	   the delete test guard would stop to ask about that. */
	g_setenv ("NEMO_TESTGUARD_ALL_DELETES", "0", TRUE);

	test_init (&argc, &argv);
	config_home = test_scratch_config_home ("nemo-rar-links-home-XXXXXX");
	nemo_config_init ();

	tmp = test_scratch_dir ("nemo-rar-links-XXXXXX", NULL);
	work = g_build_filename (tmp, "work", NULL);
	g_mkdir_with_parents (work, 0700);

	long_target = g_strnfill (TARGET_BYTES, 'x');
	link = g_build_filename (work, "m-link", NULL);
	{
		GFile *made = g_file_new_for_path (link);

		check (g_file_make_symbolic_link (made, long_target, NULL, NULL));
		g_object_unref (made);
	}
	{
		char *a = g_build_filename (work, "a.txt", NULL);
		char *z = g_build_filename (work, "z.txt", NULL);
		char *with_after[] = { rar, (char *) "a", (char *) "-ol", (char *) "long.rar",
				       (char *) "a.txt", (char *) "m-link", (char *) "z.txt", NULL };
		char *link_last[] = { rar, (char *) "a", (char *) "-ol", (char *) "tail.rar",
				      (char *) "a.txt", (char *) "m-link", NULL };
		char *link_first[] = { rar, (char *) "a", (char *) "-ol", (char *) "head.rar",
				       (char *) "m-link", (char *) "z.txt", NULL };

		check (g_file_set_contents (a, "first", -1, NULL));
		check (g_file_set_contents (z, "last", -1, NULL));
		check (run_in (work, with_after));
		check (run_in (work, link_last));
		check (run_in (work, link_first));
		g_free (z);
		g_free (a);
	}
	long_rar = g_build_filename (work, "long.rar", NULL);
	tail_rar = g_build_filename (work, "tail.rar", NULL);
	head_rar = g_build_filename (work, "head.rar", NULL);

	window = test_window_new ("rar links test", 5);
	gtk_widget_show (window);

	check_archive (tmp, long_rar, "a.txt", "z.txt", "long-rar", TRUE, window);
	check_archive (tmp, tail_rar, "a.txt", NULL, "tail-rar", TRUE, window);
	check_archive (tmp, head_rar, NULL, "z.txt", "head-rar", TRUE, window);

	/* 7-Zip reads rar too. */
	nemo_archive_hide_backend (NEMO_ARCHIVE_BACKEND_RAR, TRUE);
	seven = nemo_archive_find_command ((const char * const[]) { "7z", "7zz", "7za", NULL }, "7-Zip");
	if (seven != NULL) {
		check_archive (tmp, long_rar, "a.txt", "z.txt", "long-7z", TRUE, window);
	} else {
		g_print ("note: no 7z here, 7-Zip taking over not checked\n");
	}

	nemo_archive_hide_backend (NEMO_ARCHIVE_BACKEND_7Z, TRUE);
	check_archive (tmp, long_rar, "a.txt", "z.txt", "long-none", FALSE, window);
	check_archive (tmp, tail_rar, "a.txt", NULL, "tail-none", FALSE, window);
	check_archive (tmp, head_rar, NULL, "z.txt", "head-none", FALSE, window);
	nemo_archive_hide_backend (NEMO_ARCHIVE_BACKEND_7Z, FALSE);
	nemo_archive_hide_backend (NEMO_ARCHIVE_BACKEND_RAR, FALSE);

	gtk_widget_destroy (window);
	nemo_config_shutdown ();
	g_free (head_rar);
	g_free (tail_rar);
	g_free (long_rar);
	g_free (link);
	g_free (long_target);
	g_free (seven);
	g_free (work);
	g_free (tmp);
	g_free (config_home);
	g_free (rar);

	if (failures == 0) {
		g_print ("rar links: all checks passed\n");
	}

	return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
