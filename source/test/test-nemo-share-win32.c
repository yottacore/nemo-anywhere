/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */

/* test-nemo-share-win32.c - what may cost a trip over the network, and a link that leads nowhere.

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

/* Two ways a folder of links used to hold a listing up for minutes on Windows.
 *
 * share: the share check has to answer from the name alone. It is asked for
 * every file on every pass of the async loop, so a lookup that went to the
 * host would cost the very timeout it exists to avoid. Set
 * NEMO_PROBE_DEAD_SHARE to an unused address on the local subnet to time it
 * against one that really does not answer.
 *
 * dangling: the listing does not follow links, so a junction whose folder is
 * gone is still listed, as a link, and the load finishes. With
 * NEMO_PROBE_DEAD_SHARE set and symlinks allowed, a link to that share sits in
 * the folder too, and the listing must not wait on it: following it costs
 * about twenty seconds. */

#include <config.h>

#include <stdlib.h>
#include <string.h>
#include <gio/gio.h>
#include <glib/gstdio.h>
#include <gtk/gtk.h>

#include <libnemo-private/nemo-directory.h>
#include <libnemo-private/nemo-file.h>
#include <libnemo-private/nemo-file-private.h>
#include <libnemo-private/nemo-link-win32.h>

#include "test-scratch.h"
#include "test-check.h"

static gboolean
share_by_uri (const char *uri)
{
	NemoFile *file = nemo_file_get_by_uri (uri);
	gboolean on_share;

	if (file == NULL) {
		g_printerr ("  no NemoFile for %s\n", uri);
		failures++;
		return FALSE;
	}

	on_share = nemo_file_is_on_a_share (file);
	nemo_file_unref (file);
	return on_share;
}

/* The info a NOFOLLOW listing hands over for a link, with no trip to where it
   points. */
static NemoFile *
link_with_target (const char *dir, const char *name, const char *target)
{
	char *path = g_build_filename (dir, name, NULL);
	GFile *location = g_file_new_for_path (path);
	NemoFile *file = nemo_file_get (location);
	GFileInfo *info = g_file_info_new ();
	GIcon *icon = g_themed_icon_new ("folder");

	g_file_info_set_name (info, name);
	g_file_info_set_display_name (info, name);
	g_file_info_set_icon (info, icon);
	g_file_info_set_file_type (info, G_FILE_TYPE_DIRECTORY);
	g_file_info_set_is_symlink (info, TRUE);
	g_file_info_set_symlink_target (info, target);
	nemo_file_update_info (file, info);

	g_object_unref (icon);
	g_object_unref (info);
	g_object_unref (location);
	g_free (path);
	return file;
}

static int
run_share (void)
{
	const char *dead = g_getenv ("NEMO_PROBE_DEAD_SHARE");
	char *host_uri, *far, *dir;
	NemoFile *link;
	gint64 started;
	double seconds;

	dir = test_scratch_dir ("nemo-share-XXXXXX", NULL);
	if (dir == NULL) {
		g_printerr ("FAIL could not make a temp dir\n");
		return EXIT_FAILURE;
	}

	/* An address in TEST-NET-1 when no dead one is given. That fails fast
	   on its own, so without the variable the timing proves only that the
	   answer is right. */
	far = g_build_filename (dead != NULL ? dead : "\\\\192.0.2.1\\share", "deep", "file.txt", NULL);
	{
		GFile *location = g_file_new_for_path (far);

		host_uri = g_file_get_uri (location);
		g_object_unref (location);
	}
	g_print ("  far file: %s\n", host_uri);

	started = g_get_monotonic_time ();
	check (share_by_uri (host_uri));
	check (share_by_uri ("file:////192.0.2.1/share/x"));
	seconds = (g_get_monotonic_time () - started) / 1000000.0;
	g_print ("  two share answers took %.3fs\n", seconds);
	check (seconds < 0.5);

	/* Local, and a drive letter the shell has mapped to a share: the check
	   reads the spelling only, and a drive letter is not a share spelling. */
	check (!share_by_uri ("file:///C:/Windows/notepad.exe"));
	check (!share_by_uri ("file:///Z:/mapped/file.txt"));

	link = link_with_target (dir, "far-link", dead != NULL ? dead : "\\\\192.0.2.1\\share");
	started = g_get_monotonic_time ();
	check (nemo_file_is_on_a_share (link));
	seconds = (g_get_monotonic_time () - started) / 1000000.0;
	check (seconds < 0.5);
	nemo_file_unref (link);

	/* Reparse targets come back spelled the NT way. */
	link = link_with_target (dir, "nt-link", "\\??\\UNC\\192.0.2.1\\share");
	check (nemo_file_is_on_a_share (link));
	nemo_file_unref (link);

	link = link_with_target (dir, "local-link", "C:\\Windows");
	check (!nemo_file_is_on_a_share (link));
	nemo_file_unref (link);

	g_free (host_uri);
	g_free (far);
	g_free (dir);

	return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}

static gboolean done;

static void
ready (NemoDirectory *directory, GList *files, gpointer data)
{
	done = TRUE;
}

static gboolean
give_up (gpointer data)
{
	done = TRUE;
	return G_SOURCE_CONTINUE;
}

static NemoFile *
find_named (GList *files, const char *name)
{
	GList *l;

	for (l = files; l != NULL; l = l->next) {
		if (g_strcmp0 (nemo_file_peek_name (l->data), name) == 0) {
			return l->data;
		}
	}
	return NULL;
}

static int
run_dangling (void)
{
	char *dir, *gone, *kept, *link, *uri;
	NemoDirectory *directory;
	NemoFile *file;
	GList *files;
	GError *error = NULL;
	gint64 started;
	double seconds;
	guint timeout;
	static int client;
	const char *dead = g_getenv ("NEMO_PROBE_DEAD_SHARE");
	gboolean with_share = FALSE;

	dir = test_scratch_dir ("nemo-dangling-XXXXXX", NULL);
	if (dir == NULL) {
		g_printerr ("FAIL could not make a temp dir\n");
		return EXIT_FAILURE;
	}

	gone = g_build_filename (dir, "gone", NULL);
	kept = g_build_filename (dir, "kept", NULL);
	link = g_build_filename (dir, "to-gone", NULL);
	g_mkdir (gone, 0755);
	g_mkdir (kept, 0755);

	if (!nemo_win32_link_create (gone, link, NULL, NEMO_LINK_JUNCTION, &error)) {
		g_print ("SKIP: no junction here: %s\n", error->message);
		g_clear_error (&error);
		g_free (link);
		g_free (kept);
		g_free (gone);
		g_free (dir);
		return 77;
	}
	check (g_rmdir (gone) == 0);

	if (dead != NULL) {
		char *share_link = g_build_filename (dir, "to-share", NULL);

		if (nemo_win32_link_create (dead, share_link, NULL, NEMO_LINK_DIR_SYMLINK, &error)) {
			with_share = TRUE;
		} else {
			g_print ("  note: no link to the dead share: %s\n", error->message);
			g_clear_error (&error);
		}
		g_free (share_link);
	}

	uri = g_filename_to_uri (dir, NULL, NULL);
	directory = nemo_directory_get_by_uri (uri);

	started = g_get_monotonic_time ();
	nemo_directory_file_monitor_add (directory, &client, TRUE,
					 NEMO_FILE_ATTRIBUTES_FOR_ICON |
					 NEMO_FILE_ATTRIBUTE_INFO |
					 NEMO_FILE_ATTRIBUTE_LINK_INFO,
					 NULL, NULL);
	done = FALSE;
	timeout = g_timeout_add_seconds (30, give_up, NULL);
	nemo_directory_call_when_ready (directory, NEMO_FILE_ATTRIBUTE_INFO,
					TRUE, ready, NULL);
	while (!done) {
		g_main_context_iteration (NULL, TRUE);
	}
	g_source_remove (timeout);
	seconds = (g_get_monotonic_time () - started) / 1000000.0;
	g_print ("  listing took %.2fs\n", seconds);

	check (nemo_directory_are_all_files_seen (directory));
	check (seconds < 5.0);

	files = nemo_directory_get_file_list (directory);
	check (find_named (files, "kept") != NULL);
	if (with_share) {
		check (find_named (files, "to-share") != NULL);
	}

	file = find_named (files, "to-gone");
	check (file != NULL);
	if (file != NULL) {
		char *type = nemo_file_get_type_as_string (file);

		check (nemo_file_is_symbolic_link (file));
		check (!nemo_file_is_gone (file));
		g_print ("  to-gone reads as '%s'\n", type ? type : "(null)");
		g_free (type);
	}
	nemo_file_list_free (files);

	nemo_directory_file_monitor_remove (directory, &client);
	nemo_directory_unref (directory);

	g_free (uri);
	g_free (link);
	g_free (kept);
	g_free (gone);
	g_free (dir);

	return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}

int
main (int argc, char *argv[])
{
	const char *mode = argc > 1 ? argv[1] : "";
	int rc;

	gtk_init_check (&argc, &argv);

	if (strcmp (mode, "share") == 0) {
		rc = run_share ();
	} else if (strcmp (mode, "dangling") == 0) {
		rc = run_dangling ();
	} else {
		g_printerr ("usage: %s share|dangling\n", argv[0]);
		return EXIT_FAILURE;
	}

	if (rc == EXIT_SUCCESS) {
		g_print ("%s: all checks passed\n", mode);
	}
	return rc;
}
