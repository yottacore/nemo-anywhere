/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */

/* test-nemo-drop-action.c - move or copy for files dropped from another program.

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

/* A drop from another file manager arrives as a bare uri list, and once always
 * copied, since nothing was known about where it came from. It moves within a
 * file system, copies across, always moves to the trash, and a held key has
 * its say through what the drag offers: Control leaves only copy on offer,
 * Shift only move. The other file system is /dev/shm where that is one. */

#include <config.h>

#include <stdlib.h>
#include <glib/gstdio.h>
#include <gtk/gtk.h>

#include <libnemo-private/nemo-dnd.h>
#include <libnemo-private/nemo-file.h>
#include <libnemo-private/nemo-file-attributes.h>
#include <libnemo-private/nemo-global-preferences.h>

#include "test-scratch.h"
#include "test-check.h"

#define ANY (GDK_ACTION_MOVE | GDK_ACTION_COPY)

static NemoFile *
loaded (const char *path)
{
	g_autofree char *uri = g_filename_to_uri (path, NULL, NULL);
	NemoFile *file = nemo_file_get_by_uri (uri);
	int spins;

	nemo_file_monitor_add (file, file, NEMO_FILE_ATTRIBUTE_INFO);
	for (spins = 0; spins < 5000 && !nemo_file_check_if_ready (file, NEMO_FILE_ATTRIBUTE_INFO); spins++) {
		g_main_context_iteration (NULL, FALSE);
		g_usleep (2000);
	}
	check (nemo_file_check_if_ready (file, NEMO_FILE_ATTRIBUTE_INFO));

	return file;
}

static void
release (NemoFile *file)
{
	nemo_file_monitor_remove (file, file);
	nemo_file_unref (file);
}

/* One drop, as a fresh drag: nothing remembered about the source yet. */
static GdkDragAction
drop (GdkDragAction actions, GdkDragAction suggested, const char *target_path,
      const char *dropped_path)
{
	g_autofree char *target = g_filename_to_uri (target_path, NULL, NULL);
	g_autofree char *dropped = g_filename_to_uri (dropped_path, NULL, NULL);
	gchar *source_fs = NULL;
	gboolean can_delete = FALSE;
	GdkDragAction action;

	action = nemo_drag_drop_action_for_uri_list (actions, suggested, target, dropped,
						     &source_fs, &can_delete);
	g_free (source_fs);
	return action;
}

static char *
other_file_system (G_GNUC_UNUSED const char *here)
{
#ifndef G_OS_WIN32
	GStatBuf here_st, other_st;

	if (g_stat (here, &here_st) == 0 && g_stat ("/dev/shm", &other_st) == 0 &&
	    here_st.st_dev != other_st.st_dev) {
		return test_scratch_dir_in ("/dev/shm", "nemo-drop-action-XXXXXX", NULL);
	}
#endif
	return NULL;
}

int
main (int argc, char *argv[])
{
	g_autofree char *target = NULL, *source = NULL, *other = NULL;
	g_autofree char *doc = NULL, *far_doc = NULL;
	NemoFile *target_file;
	char *tmp;

	tmp = test_scratch_config_home ("nemo-drop-action-test-XXXXXX");
	gtk_init_check (&argc, &argv);
	nemo_global_preferences_init ();

	target = g_build_filename (tmp, "target", NULL);
	source = g_build_filename (tmp, "source", NULL);
	doc = g_build_filename (source, "doc.txt", NULL);
	check (g_mkdir (target, 0755) == 0);
	check (g_mkdir (source, 0755) == 0);
	check (g_file_set_contents (doc, "x", -1, NULL));

	/* The target is a folder on screen, which is the only kind the decision
	   can read a file system from. */
	target_file = loaded (target);

	/* Another program suggests a copy; within one file system it moves. */
	check (drop (ANY, GDK_ACTION_COPY, target, doc) == GDK_ACTION_MOVE);
	/* Control held: only copy is on offer. */
	check (drop (GDK_ACTION_COPY, GDK_ACTION_COPY, target, doc) == GDK_ACTION_COPY);

	/* To the trash it is always a move. */
	{
		gchar *source_fs = NULL;
		gboolean can_delete = FALSE;
		g_autofree char *dropped = g_filename_to_uri (doc, NULL, NULL);

		check (nemo_drag_drop_action_for_uri_list (ANY, GDK_ACTION_COPY, "trash:///", dropped,
							   &source_fs, &can_delete) == GDK_ACTION_MOVE);
		g_free (source_fs);
	}

	other = other_file_system (tmp);
	if (other != NULL) {
		far_doc = g_build_filename (other, "far.txt", NULL);
		check (g_file_set_contents (far_doc, "x", -1, NULL));

		/* Across file systems it copies, unless Shift asks for a move. */
		check (drop (ANY, GDK_ACTION_COPY, target, far_doc) == GDK_ACTION_COPY);
		check (drop (GDK_ACTION_MOVE, GDK_ACTION_MOVE, target, far_doc) == GDK_ACTION_MOVE);
	} else {
		g_print ("no second file system here; the copy across one is not checked\n");
	}

	release (target_file);
	g_free (tmp);

	if (failures > 0) {
		g_printerr ("%d check(s) failed\n", failures);
		return EXIT_FAILURE;
	}
	g_print ("PASS\n");
	return EXIT_SUCCESS;
}
