/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */

/* test-nemo-action-exec.c - running an action's programs from its own folder.

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

/* An action can name a program in its own folder by writing it as <name>, for
 * its command and for an "exec" condition. The folder used to go in front
 * unquoted, and the line was then split on spaces, so an actions folder with a
 * space in it ran the wrong program: normal in a Windows profile, and in a
 * Linux home with a space. And a condition whose program cannot be started at
 * all answered from an unset value; it has to answer no. POSIX: the programs
 * are shell scripts. */

#include <config.h>

#include <stdlib.h>
#include <string.h>
#include <gtk/gtk.h>
#include <glib/gstdio.h>

#include <libnemo-private/nemo-action.h>
#include <libnemo-private/nemo-file.h>
#include <libnemo-private/nemo-global-preferences.h>

#include "test-scratch.h"
#include "test-check.h"

static void
write_script (const char *dir, const char *name, const char *body)
{
	char *path = g_build_filename (dir, name, NULL);
	char *text = g_strdup_printf ("#!/bin/sh\n%s\n", body);

	check (g_file_set_contents (path, text, -1, NULL));
	check (g_chmod (path, 0700) == 0);
	g_free (text);
	g_free (path);
}

static NemoAction *
load_action (const char *dir, const char *name, const char *condition)
{
	char *file = g_strdup_printf ("%s.nemo_action", name);
	char *path = g_build_filename (dir, file, NULL);
	char *text = g_strdup_printf ("[Nemo Action]\n"
				      "Name=%s\n"
				      "Exec=<run.sh>\n"
				      "Selection=any\n"
				      "Extensions=any;\n"
				      "Conditions=exec <%s>;\n", name, condition);
	NemoAction *action;

	check (g_file_set_contents (path, text, -1, NULL));
	action = nemo_action_new (name, path);
	check (action != NULL);

	g_free (text);
	g_free (path);
	g_free (file);

	return action;
}

static gboolean
shown (NemoAction *action, NemoFile *parent)
{
	nemo_action_update_display_state (action, NULL, parent, FALSE, NULL);
	return gtk_action_get_visible (GTK_ACTION (action));
}

int
main (int argc, char *argv[])
{
	char *tmp, *dir, *ran, *uri;
	NemoAction *yes, *no, *missing;
	NemoFile *parent;
	int i;

	tmp = test_scratch_config_home ("nemo-action-exec-XXXXXX");

	if (!gtk_init_check (&argc, &argv)) {
		g_print ("SKIP: no display\n");
		return 77;
	}
	nemo_global_preferences_init ();

	dir = g_build_filename (tmp, "my actions", NULL);
	g_mkdir_with_parents (dir, 0700);

	write_script (dir, "yes.sh", "exit 0");
	write_script (dir, "no.sh", "exit 1");
	write_script (dir, "run.sh", "touch \"$(dirname \"$0\")/ran\"");

	yes = load_action (dir, "yes", "yes.sh");
	no = load_action (dir, "no", "no.sh");
	missing = load_action (dir, "missing", "missing.sh");

	uri = g_filename_to_uri (dir, NULL, NULL);
	parent = nemo_file_get_by_uri (uri);
	g_free (uri);

	check (yes != NULL && shown (yes, parent));
	check (no != NULL && !shown (no, parent));
	check (missing != NULL && !shown (missing, parent));

	/* Asked again, in case one answer was luck. */
	for (i = 0; i < 20 && missing != NULL; i++) {
		check (!shown (missing, parent));
	}

	ran = g_build_filename (dir, "ran", NULL);
	if (yes != NULL) {
		nemo_action_activate (yes, NULL, parent, NULL);
	}
	for (i = 0; i < 100 && !g_file_test (ran, G_FILE_TEST_EXISTS); i++) {
		g_usleep (50 * 1000);
	}
	check (g_file_test (ran, G_FILE_TEST_EXISTS));

	g_clear_object (&yes);
	g_clear_object (&no);
	g_clear_object (&missing);
	nemo_file_unref (parent);
	g_free (ran);
	g_free (dir);
	g_free (tmp);

	if (failures > 0) {
		g_printerr ("%d failure(s)\n", failures);
		return EXIT_FAILURE;
	}

	g_print ("OK\n");
	return EXIT_SUCCESS;
}
