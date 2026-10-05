/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */

/* test-nemo-drop-cancel.c - a drop's question, answered Cancel.

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

/* Two drops that stop to ask before anything moves. With the delete test guard
 * armed, a move asks first, since the originals stop being where they were. A
 * link drop opens Make link, which is its own question. Cancel leaves both
 * sides as they were and says the drop did not happen; the go-ahead does the
 * drop, so the Cancel is known to be what stopped it. Needs a display; exits
 * 77 without. */

#include <config.h>

#include <stdlib.h>
#include <string.h>
#include <gtk/gtk.h>
#include <glib/gstdio.h>

#include <libnemo-private/nemo-delete-testguard.h>
#include <libnemo-private/nemo-dir-enum.h>
#include <libnemo-private/nemo-file-operations.h>
#include <libnemo-private/nemo-global-preferences.h>
#include <libnemo-private/nemo-progress-info-manager.h>

#include "test-scratch.h"
#include "test-check.h"

#define GUARD_TITLE "Delete/overwrite test guard"
#define LINK_TITLE "Make a link"
#define JOB_TIMEOUT_SECONDS 20

typedef struct {
	const char *title;
	int answer;
	gboolean asked;
	gboolean done;
	gboolean success;
	GMainLoop *loop;
} Drop;

static void
drop_done (G_GNUC_UNUSED GHashTable *debuting_uris, gboolean success, gpointer data)
{
	Drop *drop = data;

	drop->done = TRUE;
	drop->success = success;
	if (g_main_loop_is_running (drop->loop)) {
		g_main_loop_quit (drop->loop);
	}
}

static gboolean
answer (gpointer data)
{
	Drop *drop = data;
	GList *windows, *l;

	windows = gtk_window_list_toplevels ();
	for (l = windows; l != NULL; l = l->next) {
		if (!GTK_IS_DIALOG (l->data) || !gtk_widget_get_mapped (l->data)) {
			continue;
		}
		if (g_strcmp0 (gtk_window_get_title (l->data), drop->title) == 0) {
			drop->asked = TRUE;
			gtk_dialog_response (GTK_DIALOG (l->data), drop->answer);
		} else {
			g_printerr ("FAIL an unexpected dialog: %s\n", gtk_window_get_title (l->data));
			failures++;
			gtk_dialog_response (GTK_DIALOG (l->data), GTK_RESPONSE_CANCEL);
		}
	}
	g_list_free (windows);

	return G_SOURCE_CONTINUE;
}

static gboolean
give_up (gpointer data)
{
	Drop *drop = data;

	g_printerr ("FAIL the drop never finished\n");
	failures++;
	g_main_loop_quit (drop->loop);

	return G_SOURCE_REMOVE;
}

/* What a drop of path onto folder does, with the question answered. */
static Drop
run_drop (const char *path, const char *folder, GdkDragAction action,
	  const char *title, int reply)
{
	Drop drop = { title, reply, FALSE, FALSE, FALSE, NULL };
	GList *uris;
	char *uri, *target;
	guint answer_id, limit_id;

	drop.loop = g_main_loop_new (NULL, FALSE);
	uri = g_filename_to_uri (path, NULL, NULL);
	target = g_filename_to_uri (folder, NULL, NULL);
	uris = g_list_prepend (NULL, uri);

	answer_id = g_timeout_add (50, answer, &drop);
	limit_id = g_timeout_add_seconds (JOB_TIMEOUT_SECONDS, give_up, &drop);

	nemo_file_operations_copy_move (uris, NULL, target, action, NULL, drop_done, &drop);
	if (!drop.done) {
		g_main_loop_run (drop.loop);
	}

	g_source_remove (answer_id);
	if (drop.done) {
		g_source_remove (limit_id);
	}

	g_list_free (uris);
	g_free (uri);
	g_free (target);
	g_main_loop_unref (drop.loop);

	return drop;
}

static guint
count_entries (const char *path)
{
	GFile *dir = g_file_new_for_path (path);
	GFileEnumerator *children;
	GFileInfo *info;
	guint n = 0;

	children = nemo_enumerate_children (dir, G_FILE_ATTRIBUTE_STANDARD_NAME,
					    G_FILE_QUERY_INFO_NOFOLLOW_SYMLINKS, NULL, NULL);
	if (children != NULL) {
		while ((info = g_file_enumerator_next_file (children, NULL, NULL)) != NULL) {
			n++;
			g_object_unref (info);
		}
		g_object_unref (children);
	}
	g_object_unref (dir);

	return n;
}

static void
check_move (const char *tmp)
{
	char *from = g_build_filename (tmp, "move-from", NULL);
	char *to = g_build_filename (tmp, "move-to", NULL);
	char *path = g_build_filename (from, "moving.txt", NULL);
	char *arrived = g_build_filename (to, "moving.txt", NULL);
	Drop drop;

	g_mkdir_with_parents (from, 0700);
	g_mkdir_with_parents (to, 0700);
	check (g_file_set_contents (path, "x", 1, NULL));

	drop = run_drop (path, to, GDK_ACTION_MOVE, GUARD_TITLE, GTK_RESPONSE_CANCEL);
	check (drop.asked);
	check (drop.done);
	check (!drop.success);
	check (g_file_test (path, G_FILE_TEST_IS_REGULAR));
	check (count_entries (to) == 0);

	drop = run_drop (path, to, GDK_ACTION_MOVE, GUARD_TITLE, GTK_RESPONSE_OK);
	check (drop.asked);
	check (drop.done && drop.success);
	check (!g_file_test (path, G_FILE_TEST_EXISTS));
	check (g_file_test (arrived, G_FILE_TEST_IS_REGULAR));

	g_free (arrived);
	g_free (path);
	g_free (to);
	g_free (from);
}

static void
check_link (const char *tmp)
{
	char *from = g_build_filename (tmp, "link-from", NULL);
	char *to = g_build_filename (tmp, "link-to", NULL);
	char *path = g_build_filename (from, "target.txt", NULL);
	Drop drop;

	g_mkdir_with_parents (from, 0700);
	g_mkdir_with_parents (to, 0700);
	check (g_file_set_contents (path, "x", 1, NULL));

	drop = run_drop (path, to, GDK_ACTION_LINK, LINK_TITLE, GTK_RESPONSE_CANCEL);
	check (drop.asked);
	check (drop.done);
	check (!drop.success);
	check (count_entries (to) == 0);

	drop = run_drop (path, to, GDK_ACTION_LINK, LINK_TITLE, GTK_RESPONSE_OK);
	check (drop.asked);
	check (drop.done && drop.success);
	check (count_entries (to) == 1);
	check (g_file_test (path, G_FILE_TEST_IS_REGULAR));

	g_free (path);
	g_free (to);
	g_free (from);
}

int
main (int argc, char *argv[])
{
	NemoProgressInfoManager *manager;
	char *tmp;

	/* Read once, so it goes before anything asks whether the guard is on. */
	g_setenv (NEMO_TESTGUARD_ENV_VAR, "1", TRUE);

	tmp = test_scratch_config_home ("nemo-drop-cancel-XXXXXX");

	if (!gtk_init_check (&argc, &argv)) {
		g_print ("SKIP: no display\n");
		return 77;
	}
	nemo_global_preferences_init ();

	/* More than one job runs here; see test-nemo-link-copy-job.c. */
	manager = nemo_progress_info_manager_new ();

	check (nemo_delete_testguard_armed ());
	check_move (tmp);
	check_link (tmp);

	g_object_unref (manager);
	g_free (tmp);

	if (failures > 0) {
		g_printerr ("%d failure(s)\n", failures);
		return EXIT_FAILURE;
	}

	g_print ("OK\n");
	return EXIT_SUCCESS;
}
