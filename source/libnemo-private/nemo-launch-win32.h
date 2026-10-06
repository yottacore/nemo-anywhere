/* nemo-launch-win32.h - starting a program that is not ours
 *
 * Copyright © 2026 t00mietum (CryptogID: ปʬϝღถɔ4რఠΔթะ9ƾǝu)
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published
 * by the Free Software Foundation.
 */

#ifndef NEMO_LAUNCH_WIN32_H
#define NEMO_LAUNCH_WIN32_H

#include <gio/gio.h>

/* Default action on a path, the way a double-click in the shell would do it. */
gboolean nemo_launch_win32_open_path   (const gchar  *path,
					const gchar  *workdir,
					GError      **error);

/* A named program. @args is a command tail the program parses itself, or NULL. */
gboolean nemo_launch_win32_run         (const gchar  *exe,
					const gchar  *args,
					const gchar  *workdir,
					GError      **error);

/* Same, from a whole command line with the program on the front. */
gboolean nemo_launch_win32_run_command (const gchar  *command_line,
					const gchar  *workdir,
					GError      **error);

/* The program at the front of a command line; *@args gets the rest, or NULL. */
gchar   *nemo_launch_win32_split_command (const gchar  *command_line,
					  gchar       **args);

/* The two brokers on their own. Exposed so a probe can tell which of them the
 * box actually allows, rather than watching one cover for the other. */
gboolean nemo_launch_win32_via_shell   (const gchar *exe,
					const gchar *args,
					const gchar *workdir);
gboolean nemo_launch_win32_via_service (const gchar *command_line,
					const gchar *workdir);

/* Runs @argv with no console window, with the file at @input_path as its stdin,
 * or NUL when that is NULL, and hands back what it wrote to stdout, or drops it
 * when @output is NULL. A bare program name is looked up beside our exe, then on
 * PATH. Ends it after @timeout_seconds, setting *@timed_out, or once
 * @cancellable is cancelled. FALSE if it could not start, was ended or did not
 * exit 0.
 * @output: (out) (optional) (transfer full): unref with g_bytes_unref */
gboolean nemo_launch_win32_pipe        (const gchar * const  *argv,
					const gchar          *input_path,
					guint                 timeout_seconds,
					GCancellable         *cancellable,
					GBytes              **output,
					gboolean             *timed_out);

/* A tool run with no console window whose output is read as it comes, for
 * nemo-tool-run.c. Of @flags only STDOUT_PIPE, STDERR_PIPE and STDERR_MERGE
 * mean anything. stdin and every stream not piped go to NUL.
 * Returns: (transfer full): free with nemo_launch_win32_child_free */
typedef struct _NemoLaunchWin32Child NemoLaunchWin32Child;

NemoLaunchWin32Child *nemo_launch_win32_child_start (const gchar * const  *argv,
						     const gchar          *workdir,
						     GSubprocessFlags      flags,
						     GError              **error);
/* Returns: (transfer none): NULL when not piped. Reads take a cancellable. */
GInputStream *nemo_launch_win32_child_get_stdout (NemoLaunchWin32Child *child);
GInputStream *nemo_launch_win32_child_get_stderr (NemoLaunchWin32Child *child);
void          nemo_launch_win32_child_force_exit (NemoLaunchWin32Child *child);
/* Waits for it to end and gives its exit code. */
gint          nemo_launch_win32_child_wait       (NemoLaunchWin32Child *child);
void          nemo_launch_win32_child_free       (NemoLaunchWin32Child *child);

#endif /* NEMO_LAUNCH_WIN32_H */
