/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */

/* test-nemo-action-start-win32.c - how a custom action's programs are started
   on Windows.

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

/* An action's command and its "exec" condition went through GLib's spawn,
 * whose helper gave a console program a console window, for a condition every
 * time the menu was built. A terminal action looked only for Linux terminals.
 * Stand-ins in the action's folder say whether they got a window and what
 * started them: a console program runs from the app with no window, or in a
 * console of its own when the action asks for a terminal, and a program with
 * windows of its own is started from outside the app. */

#include <config.h>

#include <string.h>

#include <glib.h>
#include <glib/gstdio.h>
#include <gtk/gtk.h>
#include <windows.h>

#include <libnemo-private/nemo-action.h>
#include <libnemo-private/nemo-file.h>
#include <libnemo-private/nemo-global-preferences.h>

#include "test-scratch.h"
#include "test-check.h"

static char *dir;
static char *self_name;

static char *
report_of (const char *tool)
{
	g_autofree char *name = g_strconcat (tool, ".report", NULL);
	g_autofree char *path = g_build_filename (dir, name, NULL);
	char *text = NULL;

	return g_file_get_contents (path, &text, NULL, NULL) ? text : g_strdup ("");
}

static gboolean
reported (const char *tool, const char *line)
{
	g_autofree char *text = report_of (tool);
	g_autofree char *wanted = g_strdup_printf ("%s\n", line);

	return strstr (text, wanted) != NULL;
}

/* The last line it writes is its last argument. */
static gboolean
wait_for_report (const char *tool, const char *last)
{
	for (int i = 0; i < 200; i++) {
		if (reported (tool, last)) {
			return TRUE;
		}
		g_usleep (50 * 1000);
	}
	g_printerr ("no report from %s\n", tool);
	return FALSE;
}

static gboolean
started_by_us (const char *tool)
{
	g_autofree char *line = g_strdup_printf ("parent=%s", self_name);

	return reported (tool, line);
}

static gboolean
started_by_broker (const char *tool)
{
	g_autofree char *text = g_ascii_strdown (report_of (tool), -1);

	return strstr (text, "\nparent=explorer.exe\n") != NULL ||
	       strstr (text, "\nparent=wmiprvse.exe\n") != NULL;
}

static void
place (const char *from_path, const char *name)
{
	g_autofree char *path = g_build_filename (dir, name, NULL);
	g_autoptr (GFile) from = g_file_new_for_path (from_path);
	g_autoptr (GFile) to = g_file_new_for_path (path);

	check (g_file_copy (from, to, G_FILE_COPY_NONE, NULL, NULL, NULL, NULL));
}

static void
write_text (const char *name, const char *text)
{
	g_autofree char *path = g_build_filename (dir, name, NULL);

	check (g_file_set_contents (path, text, -1, NULL));
}

/* Both run as <program args> from the action's own folder, which has a space
   in it. */
static NemoAction *
load_action (const char *name, const char *exec, const char *condition, gboolean terminal)
{
	g_autofree char *file = g_strdup_printf ("%s.nemo_action", name);
	g_autofree char *path = g_build_filename (dir, file, NULL);
	g_autofree char *conditions = condition != NULL
		? g_strdup_printf ("Conditions=exec <%s>;\n", condition) : g_strdup ("");
	g_autofree char *text = g_strdup_printf ("[Nemo Action]\n"
						 "Name=%s\n"
						 "Exec=<%s>\n"
						 "Selection=any\n"
						 "Extensions=any;\n"
						 "Terminal=%s\n"
						 "%s", name, exec, terminal ? "true" : "false", conditions);
	NemoAction *action;

	check (g_file_set_contents (path, text, -1, NULL));
	action = nemo_action_new (name, path);
	check (action != NULL);

	return action;
}

static gboolean
shown (NemoAction *action, NemoFile *parent)
{
	if (action == NULL) {
		return FALSE;
	}
	nemo_action_update_display_state (action, NULL, parent, FALSE, NULL);
	return gtk_action_get_visible (GTK_ACTION (action));
}

static void
run (NemoAction *action, NemoFile *parent)
{
	if (action != NULL) {
		nemo_action_activate (action, NULL, parent, NULL);
	}
}

int
main (int argc, char *argv[])
{
	g_autofree char *scratch = NULL, *uri = NULL;
	NemoAction *yes, *no, *cmd_yes, *cmd_no, *missing, *quiet, *console, *windowed;
	NemoFile *parent;

	if (argc < 3) {
		g_printerr ("usage: %s <console stand-in.exe> <windowed stand-in.exe>\n", argv[0]);
		return 1;
	}

	scratch = test_scratch_config_home ("nemo-action-start-XXXXXX");
	if (scratch == NULL) {
		g_printerr ("FAIL: no scratch folder\n");
		return 1;
	}
	self_name = g_path_get_basename (argv[0]);
	if (!g_str_has_suffix (self_name, ".exe")) {
		char *with = g_strconcat (self_name, ".exe", NULL);

		g_free (self_name);
		self_name = with;
	}

	dir = g_build_filename (scratch, "my actions", NULL);
	check (g_mkdir (dir, 0755) == 0);
	/* Programs started from outside the app don't have this, and write
	   beside themselves, which is the same place. */
	g_setenv ("NEMO_FAKE_TOOL_DIR", dir, TRUE);

	place (argv[1], "cond.exe");
	place (argv[1], "quiet.exe");
	place (argv[1], "console.exe");
	place (argv[2], "windowed.exe");
	write_text ("yes.cmd", "@exit /b 0\r\n");
	write_text ("no.cmd", "@exit /b 3\r\n");

	if (!gtk_init_check (&argc, &argv)) {
		g_print ("SKIP: no display\n");
		return 77;
	}
	nemo_global_preferences_init ();

	/* The app has no console of its own, which is when a program got one. */
	FreeConsole ();

	uri = g_filename_to_uri (dir, NULL, NULL);
	parent = nemo_file_get_by_uri (uri);

	yes = load_action ("yes", "quiet.exe", "cond.exe \"two words\"", FALSE);
	no = load_action ("no", "quiet.exe", "cond.exe", FALSE);
	cmd_yes = load_action ("cmd-yes", "quiet.exe", "yes.cmd", FALSE);
	cmd_no = load_action ("cmd-no", "quiet.exe", "no.cmd", FALSE);
	missing = load_action ("missing", "quiet.exe", "missing.exe", FALSE);
	quiet = load_action ("quiet", "quiet.exe \"two words\"", NULL, FALSE);
	console = load_action ("console", "console.exe \"two words\"", NULL, TRUE);
	windowed = load_action ("windowed", "windowed.exe \"two words\"", NULL, FALSE);

	g_print ("conditions\n");
	check (shown (yes, parent));
	check (reported ("cond", "window=0"));
	check (reported ("cond", "arg=two words"));
	check (started_by_us ("cond"));
	g_setenv ("NEMO_FAKE_TOOL_EXIT", "4", TRUE);
	check (!shown (no, parent));
	g_unsetenv ("NEMO_FAKE_TOOL_EXIT");
	check (shown (cmd_yes, parent));
	check (!shown (cmd_no, parent));
	check (!shown (missing, parent));

	g_print ("command\n");
	run (quiet, parent);
	check (wait_for_report ("quiet", "arg=two words"));
	check (reported ("quiet", "window=0"));
	check (started_by_us ("quiet"));

	g_print ("command in a terminal\n");
	run (console, parent);
	check (wait_for_report ("console", "arg=two words"));
	check (reported ("console", "window=1"));
	check (started_by_broker ("console"));

	g_print ("command with windows of its own\n");
	run (windowed, parent);
	check (wait_for_report ("windowed", "arg=two words"));
	check (started_by_broker ("windowed"));

	g_clear_object (&yes);
	g_clear_object (&no);
	g_clear_object (&cmd_yes);
	g_clear_object (&cmd_no);
	g_clear_object (&missing);
	g_clear_object (&quiet);
	g_clear_object (&console);
	g_clear_object (&windowed);
	nemo_file_unref (parent);
	g_free (self_name);
	g_free (dir);

	if (failures > 0) {
		g_printerr ("%d failure(s)\n", failures);
		return 1;
	}

	g_print ("OK\n");
	return 0;
}
