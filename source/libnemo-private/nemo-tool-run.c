/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */

/* nemo-tool-run.c - a command line tool whose output is read as it runs.

   Copyright © 2026 t00mietum (CryptogID: ปʬϝღถɔ4რఠΔթะ9ƾǝu).

   This program is free software; you can redistribute it and/or
   modify it under the terms of the GNU General Public License as
   published by the Free Software Foundation; version 2 of the
   License.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, see <http://www.gnu.org/licenses/>.
*/

#include <config.h>
#include "nemo-tool-run.h"

#ifdef G_OS_WIN32
#include "nemo-launch-win32.h"
#endif

struct _NemoToolRun {
#ifdef G_OS_WIN32
	NemoLaunchWin32Child *child;
#else
	GSubprocess *process;
#endif
};

#ifdef G_OS_WIN32

/* Returns: (transfer full): free with nemo_tool_run_free */
NemoToolRun *
nemo_tool_run_start (const gchar * const  *argv,
		     const gchar          *cwd,
		     GSubprocessFlags      flags,
		     GError              **error)
{
	NemoLaunchWin32Child *child = nemo_launch_win32_child_start (argv, cwd, flags, error);
	NemoToolRun *run;

	if (child == NULL) {
		return NULL;
	}

	run = g_new0 (NemoToolRun, 1);
	run->child = child;
	return run;
}

/* Returns: (transfer none) */
GInputStream *
nemo_tool_run_get_stdout (NemoToolRun *run)
{
	return nemo_launch_win32_child_get_stdout (run->child);
}

/* Returns: (transfer none) */
GInputStream *
nemo_tool_run_get_stderr (NemoToolRun *run)
{
	return nemo_launch_win32_child_get_stderr (run->child);
}

void
nemo_tool_run_force_exit (NemoToolRun *run)
{
	nemo_launch_win32_child_force_exit (run->child);
}

void
nemo_tool_run_wait (NemoToolRun *run)
{
	nemo_launch_win32_child_wait (run->child);
}

gboolean
nemo_tool_run_wait_check (NemoToolRun  *run,
			  GError      **error)
{
	/* On Windows the wait status is the exit code. */
	return g_spawn_check_wait_status (nemo_launch_win32_child_wait (run->child), error);
}

gboolean
nemo_tool_run_get_if_exited (G_GNUC_UNUSED NemoToolRun *run)
{
	return TRUE;
}

gint
nemo_tool_run_get_exit_status (NemoToolRun *run)
{
	return nemo_launch_win32_child_wait (run->child);
}

void
nemo_tool_run_free (NemoToolRun *run)
{
	if (run != NULL) {
		nemo_launch_win32_child_free (run->child);
		g_free (run);
	}
}

#else

/* Returns: (transfer full): free with nemo_tool_run_free */
NemoToolRun *
nemo_tool_run_start (const gchar * const  *argv,
		     const gchar          *cwd,
		     GSubprocessFlags      flags,
		     GError              **error)
{
	GSubprocessLauncher *launcher = g_subprocess_launcher_new (flags);
	GSubprocess *process;
	NemoToolRun *run;

	g_subprocess_launcher_set_cwd (launcher, cwd);
	process = g_subprocess_launcher_spawnv (launcher, argv, error);
	g_object_unref (launcher);

	if (process == NULL) {
		return NULL;
	}

	if ((flags & G_SUBPROCESS_FLAGS_STDIN_PIPE) != 0) {
		g_output_stream_close (g_subprocess_get_stdin_pipe (process), NULL, NULL);
	}

	run = g_new0 (NemoToolRun, 1);
	run->process = process;
	return run;
}

/* Returns: (transfer none) */
GInputStream *
nemo_tool_run_get_stdout (NemoToolRun *run)
{
	return g_subprocess_get_stdout_pipe (run->process);
}

/* Returns: (transfer none) */
GInputStream *
nemo_tool_run_get_stderr (NemoToolRun *run)
{
	return g_subprocess_get_stderr_pipe (run->process);
}

void
nemo_tool_run_force_exit (NemoToolRun *run)
{
	g_subprocess_force_exit (run->process);
}

void
nemo_tool_run_wait (NemoToolRun *run)
{
	g_subprocess_wait (run->process, NULL, NULL);
}

gboolean
nemo_tool_run_wait_check (NemoToolRun  *run,
			  GError      **error)
{
	return g_subprocess_wait_check (run->process, NULL, error);
}

gboolean
nemo_tool_run_get_if_exited (NemoToolRun *run)
{
	return g_subprocess_get_if_exited (run->process);
}

gint
nemo_tool_run_get_exit_status (NemoToolRun *run)
{
	return g_subprocess_get_exit_status (run->process);
}

void
nemo_tool_run_free (NemoToolRun *run)
{
	if (run != NULL) {
		g_object_unref (run->process);
		g_free (run);
	}
}

#endif
