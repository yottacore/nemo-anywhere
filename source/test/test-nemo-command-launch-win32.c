/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */

/* test-nemo-command-launch-win32.c - how a program run from a command line,
   such as a script from the Scripts menu, is started on Windows.

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

/* Scripts went through GLib's app launch, which starts a program through
 * GLib's spawn helper, so a console script got a console window. Run the way
 * the Scripts menu runs one: the script's path quoted, the selected names as
 * parameters, from the folder in view. A console program or batch file runs
 * from the app with no window, and in a console of its own when a terminal is
 * asked for. A batch file at a path with spaces never started, and cmd would
 * have run what follows a & in a file name. */

#include <config.h>

#include <string.h>

#include <glib.h>
#include <glib/gstdio.h>
#include <gtk/gtk.h>
#include <windows.h>

#include <libnemo-private/nemo-program-choosing.h>
#include <libnemo-private/nemo-user-text.h>

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
started_by (const char *tool, const char *parent)
{
	g_autofree char *text = g_ascii_strdown (report_of (tool), -1);
	g_autofree char *line = g_ascii_strdown (parent, -1);
	g_autofree char *wanted = g_strdup_printf ("\nparent=%s\n", line);

	return strstr (text, wanted) != NULL;
}

static gboolean
started_by_broker (const char *tool)
{
	g_autofree char *text = g_ascii_strdown (report_of (tool), -1);

	return strstr (text, "\nparent=explorer.exe\n") != NULL ||
	       strstr (text, "\nparent=wmiprvse.exe\n") != NULL;
}

static char *
place (const char *from_path, const char *name)
{
	char *path = g_build_filename (dir, name, NULL);
	g_autoptr (GFile) from = g_file_new_for_path (from_path);
	g_autoptr (GFile) to = g_file_new_for_path (path);

	check (g_file_copy (from, to, G_FILE_COPY_NONE, NULL, NULL, NULL, NULL));
	return path;
}

int
main (int argc, char *argv[])
{
	g_autofree char *scratch = NULL, *work = NULL, *cwd_line = NULL;
	g_autofree char *quiet = NULL, *console = NULL, *viacmd = NULL, *batch = NULL, *batch2 = NULL;
	g_autofree char *quoted = NULL;
	const char *names[] = { "two words", "C:\\plain\\path", "R&D %PATH% 100%", "it's", NULL };
	GdkScreen *screen;

	if (argc < 2) {
		g_printerr ("usage: %s <console stand-in.exe>\n", argv[0]);
		return 1;
	}

	scratch = test_scratch_config_home ("nemo-command-launch-XXXXXX");
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

	dir = g_build_filename (scratch, "my scripts", NULL);
	work = g_build_filename (scratch, "the folder", NULL);
	check (g_mkdir (dir, 0755) == 0);
	check (g_mkdir (work, 0755) == 0);
	/* A broker start has the desktop's environment, and then the stand-in
	   writes beside itself, which is the same place. */
	g_setenv ("NEMO_FAKE_TOOL_DIR", dir, TRUE);

	quiet = place (argv[1], "quiet.exe");
	console = place (argv[1], "console.exe");
	viacmd = place (argv[1], "viacmd.exe");
	batch = g_build_filename (dir, "run it.cmd", NULL);
	check (g_file_set_contents (batch, "@\"%~dp0viacmd.exe\" %*\r\n", -1, NULL));
	g_free (place (argv[1], "viacmd2.exe"));
	batch2 = g_build_filename (dir, "run it too.cmd", NULL);
	check (g_file_set_contents (batch2, "@\"%~dp0viacmd2.exe\" %*\r\n", -1, NULL));

	if (!gtk_init_check (&argc, &argv)) {
		g_print ("SKIP: no display\n");
		return 77;
	}
	screen = gdk_screen_get_default ();

	/* The app has no console of its own, which is when a program got one. */
	FreeConsole ();

	/* The Scripts menu runs a script from the folder in view, with the
	   selected names as they are there. */
	check (g_chdir (work) == 0);
	cwd_line = g_strdup_printf ("cwd=%s", work);

	g_print ("console program\n");
	quoted = nemo_user_text_quote (quiet);
	nemo_launch_application_from_command_array (screen, quoted, FALSE, names);
	check (wait_for_report ("quiet", "arg=it's"));
	check (reported ("quiet", "arg=two words"));
	check (reported ("quiet", "arg=C:\\plain\\path"));
	check (reported ("quiet", "arg=R&D %PATH% 100%"));
	check (reported ("quiet", "window=0"));
	check (reported ("quiet", cwd_line));
	check (started_by ("quiet", self_name));
	g_clear_pointer (&quoted, g_free);

	g_print ("batch file\n");
	quoted = nemo_user_text_quote (batch);
	nemo_launch_application_from_command_array (screen, quoted, FALSE, names);
	check (wait_for_report ("viacmd", "arg=it's"));
	check (reported ("viacmd", "arg=two words"));
	check (reported ("viacmd", "arg=C:\\plain\\path"));
	check (reported ("viacmd", "arg=R&D %PATH% 100%"));
	check (reported ("viacmd", "window=0"));
	check (reported ("viacmd", cwd_line));
	check (started_by ("viacmd", "cmd.exe"));
	g_clear_pointer (&quoted, g_free);

	g_print ("in a terminal\n");
	quoted = nemo_user_text_quote (console);
	nemo_launch_application_from_command (screen, quoted, TRUE, "two words", NULL);
	check (wait_for_report ("console", "arg=two words"));
	check (reported ("console", "window=1"));
	check (started_by_broker ("console"));
	g_clear_pointer (&quoted, g_free);

	g_print ("batch file in a terminal\n");
	quoted = nemo_user_text_quote (batch2);
	nemo_launch_application_from_command_array (screen, quoted, TRUE, names);
	check (wait_for_report ("viacmd2", "arg=it's"));
	check (reported ("viacmd2", "arg=two words"));
	check (reported ("viacmd2", "arg=R&D %PATH% 100%"));
	check (reported ("viacmd2", "window=1"));
	check (started_by ("viacmd2", "cmd.exe"));
	g_clear_pointer (&quoted, g_free);

	g_chdir (scratch);
	g_free (self_name);
	g_free (dir);

	if (failures > 0) {
		g_printerr ("%d failure(s)\n", failures);
		return 1;
	}

	g_print ("OK\n");
	return 0;
}
