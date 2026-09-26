/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */

/* test-nemo-desktop-terminal.c - whose terminal Open in Terminal runs.

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

/* A terminal named in our own preferences beats the one the desktop publishes;
 * the desktop's is used only while ours is empty. The desktop's setting is a
 * GSettings schema of its own, so this compiles a stand-in for it into a
 * scratch dir and reads it through the memory backend. POSIX only: the
 * desktop schema is never there on Windows. */

#include <config.h>

#include <stdlib.h>
#include <gio/gio.h>

#include <libnemo-private/nemo-config.h>
#include <libnemo-private/nemo-desktop-settings.h>

#include "test-scratch.h"
#include "test-check.h"

#define DESKTOP_SCHEMA "org.cinnamon.desktop.default-applications.terminal"

static const char schema_xml[] =
	"<schemalist>\n"
	"  <schema id=\"" DESKTOP_SCHEMA "\"\n"
	"          path=\"/org/cinnamon/desktop/applications/terminal/\">\n"
	"    <key name=\"exec\" type=\"s\"><default>'desktop-term'</default></key>\n"
	"    <key name=\"exec-arg\" type=\"s\"><default>'-x'</default></key>\n"
	"  </schema>\n"
	"</schemalist>\n";

/* FALSE when the compiler is not there to make the stand-in. */
static gboolean
compile_schema (const char *dir)
{
	char *compiler = g_find_program_in_path ("glib-compile-schemas");
	char *xml = g_build_filename (dir, DESKTOP_SCHEMA ".gschema.xml", NULL);
	char *argv[] = { compiler, (char *) dir, NULL };
	int status = -1;
	gboolean ok = FALSE;

	if (compiler != NULL && g_file_set_contents (xml, schema_xml, -1, NULL)) {
		ok = g_spawn_sync (NULL, argv, NULL, G_SPAWN_DEFAULT, NULL, NULL,
				   NULL, NULL, &status, NULL) &&
		     g_spawn_check_wait_status (status, NULL);
	}

	g_free (xml);
	g_free (compiler);
	return ok;
}

static void
check_exec (const char *want_exec, const char *want_arg)
{
	char *exec = nemo_desktop_settings_get_terminal_exec ();
	char *arg = nemo_desktop_settings_get_terminal_exec_arg ();

	if (g_strcmp0 (exec, want_exec) != 0 || g_strcmp0 (arg, want_arg) != 0) {
		g_printerr ("FAIL: terminal %s %s (wanted %s %s)\n",
			    exec, arg, want_exec, want_arg);
		failures++;
	}

	g_free (exec);
	g_free (arg);
}

int
main (int argc, char *argv[])
{
	char *tmp, *schemas;
	NemoConfigGroup *own;

	tmp = test_scratch_config_home ("nemo-desktop-terminal-XXXXXX");
	schemas = g_build_filename (tmp, "schemas", NULL);
	check (g_mkdir_with_parents (schemas, 0755) == 0);

	if (!compile_schema (schemas)) {
		g_print ("SKIP: cannot compile a settings schema here\n");
		g_free (schemas);
		g_free (tmp);
		return 77;
	}

	/* Before anything opens GSettings: the stand-in schema only, held in
	   memory. */
	g_setenv ("GSETTINGS_SCHEMA_DIR", schemas, TRUE);
	g_setenv ("XDG_DATA_DIRS", schemas, TRUE);
	g_setenv ("GSETTINGS_BACKEND", "memory", TRUE);

	nemo_config_init ();
	nemo_desktop_settings_init ();
	own = nemo_config_get_group ("terminal");

	/* Ours empty: the desktop's choice, and its argument with it. */
	check_exec ("desktop-term", "-x");

	/* Ours set: ours, with our own argument, not a mix of the two. */
	nemo_config_set_string (own, "exec", "my-term");
	nemo_config_set_string (own, "exec-arg", "--run");
	check_exec ("my-term", "--run");

	/* Emptied again: back to the desktop's. */
	nemo_config_set_string (own, "exec", "");
	check_exec ("desktop-term", "-x");

	nemo_desktop_settings_finalize ();
	nemo_config_shutdown ();
	g_free (schemas);
	g_free (tmp);

	if (failures == 0) {
		g_print ("nemo-desktop-terminal: all checks passed\n");
	}

	return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
