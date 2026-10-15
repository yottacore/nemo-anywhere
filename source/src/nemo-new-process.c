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

#ifdef G_OS_WIN32
#include <libnemo-private/nemo-launch-win32.h>
#else
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

#ifdef G_OS_WIN32
static gboolean
spawn_argv (char    **argv,
            GError  **error)
{
	return nemo_launch_win32_new_copy ((const gchar * const *) argv, error);
}
#else
static void
child_exited (GPid     pid,
              G_GNUC_UNUSED gint     status,
              G_GNUC_UNUSED gpointer user_data)
{
	g_spawn_close_pid (pid);
}

/* Its own session, so a hangup or an interrupt aimed at the window that
 * started it does not reach it. */
static void
detach_from_terminal (G_GNUC_UNUSED gpointer user_data)
{
	setsid ();
}

static gboolean
spawn_argv (char    **argv,
            GError  **error)
{
	/* It makes its own settings again, and has to see the user's to do it. */
	char **env = nemo_get_user_environ ();
	GPid pid;
	gboolean ok;

	/* Reaped here, so no zombie is left. */
	ok = g_spawn_async (NULL, argv, env,
	                    G_SPAWN_SEARCH_PATH | G_SPAWN_DO_NOT_REAP_CHILD,
	                    detach_from_terminal, NULL, &pid, error);
	g_strfreev (env);
	if (ok) {
		g_child_watch_add (pid, child_exited, NULL);
	}

	return ok;
}
#endif

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
