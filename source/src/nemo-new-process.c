/* nemo-new-process: start another copy of this program to show a location.
 *
 * Copyright © 2026 t00mietum (CryptogID: ปʬϝღถɔ4რఠΔթะ9ƾǝu)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; version 2 of the License.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 */

#include <config.h>
#include "nemo-new-process.h"

#include <libnemo-private/nemo-file-utilities.h>

#ifndef G_OS_WIN32
#include <unistd.h>
#endif

/* The command line another copy needs to show what this one was asked for.
 * A selection is passed as --select when the location is its folder; when it
 * is not, the location alone is what can be said on a command line.
 * Returns: (transfer full): free with g_strfreev */
char **
nemo_new_process_argv (GFile *location,
                       GFile *selection)
{
	GPtrArray *argv = g_ptr_array_new ();
	char *exe = nemo_get_exe_path ();

	g_ptr_array_add (argv, exe != NULL ? exe : g_strdup (NEMO_APP_SLUG));

	if (selection != NULL &&
	    (location == NULL || g_file_has_parent (selection, location))) {
		g_ptr_array_add (argv, g_strdup ("--select"));
		g_ptr_array_add (argv, g_file_get_uri (selection));
	} else if (location != NULL) {
		g_ptr_array_add (argv, g_file_get_uri (location));
	}

	g_ptr_array_add (argv, NULL);

	return (char **) g_ptr_array_free (argv, FALSE);
}

/* A tab moved out into a window of its own keeps its view and selection. The
 * options are hidden ones, and apply only to a single location.
 * Returns: (transfer full): free with g_strfreev */
char **
nemo_new_process_argv_tab (GFile       *location,
                           const char  *view_id,
                           char       **selected)
{
	GPtrArray *argv = g_ptr_array_new ();
	char *exe = nemo_get_exe_path ();
	int i;

	g_ptr_array_add (argv, exe != NULL ? exe : g_strdup (NEMO_APP_SLUG));

	if (view_id != NULL && view_id[0] != '\0') {
		g_ptr_array_add (argv, g_strdup ("--tab-view"));
		g_ptr_array_add (argv, g_strdup (view_id));
	}
	for (i = 0; selected != NULL && selected[i] != NULL; i++) {
		g_ptr_array_add (argv, g_strdup ("--tab-select"));
		g_ptr_array_add (argv, g_strdup (selected[i]));
	}
	g_ptr_array_add (argv, g_file_get_uri (location));
	g_ptr_array_add (argv, NULL);

	return (char **) g_ptr_array_free (argv, FALSE);
}

static void
child_exited (GPid     pid,
              G_GNUC_UNUSED gint     status,
              G_GNUC_UNUSED gpointer user_data)
{
	g_spawn_close_pid (pid);
}

#ifndef G_OS_WIN32
/* Its own session, so a hangup or an interrupt aimed at the window that
 * started it does not reach it. */
static void
detach_from_terminal (G_GNUC_UNUSED gpointer user_data)
{
	setsid ();
}
#endif

static gboolean
spawn_argv (char    **argv,
            GError  **error)
{
	GSpawnChildSetupFunc setup = NULL;
	GPid pid;
	gboolean ok;

#ifndef G_OS_WIN32
	setup = detach_from_terminal;
#endif

	/* Reaping it ourselves keeps GLib off its helper process on Windows and
	 * off a zombie here. */
	ok = g_spawn_async (NULL, argv, NULL,
	                    G_SPAWN_SEARCH_PATH | G_SPAWN_DO_NOT_REAP_CHILD,
	                    setup, NULL, &pid, error);
	if (ok) {
		g_child_watch_add (pid, child_exited, NULL);
	}

	return ok;
}

gboolean
nemo_new_process_spawn (GFile   *location,
                        GFile   *selection,
                        GError **error)
{
	char **argv = nemo_new_process_argv (location, selection);
	gboolean ok = spawn_argv (argv, error);

	g_strfreev (argv);

	return ok;
}

gboolean
nemo_new_process_spawn_tab (GFile       *location,
                            const char  *view_id,
                            char       **selected,
                            GError     **error)
{
	char **argv = nemo_new_process_argv_tab (location, view_id, selected);
	gboolean ok = spawn_argv (argv, error);

	g_strfreev (argv);

	return ok;
}
