/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */

/* nemo-tool-run.h - a command line tool whose output is read as it runs.

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

#ifndef NEMO_TOOL_RUN_H
#define NEMO_TOOL_RUN_H

#include <gio/gio.h>

/* GSubprocess everywhere but Windows, where it goes through
   nemo-launch-win32.c with no console window. The calls are named after the
   GSubprocess ones they stand for.

   A STDIN_PIPE in @flags is handed over already ended, since nothing here
   answers a prompt. On Windows anything not piped goes to NUL.
   Returns: (transfer full): free with nemo_tool_run_free */
typedef struct _NemoToolRun NemoToolRun;

NemoToolRun  *nemo_tool_run_start           (const gchar * const  *argv,
					     const gchar          *cwd,
					     GSubprocessFlags      flags,
					     GError              **error);
/* Returns: (transfer none) */
GInputStream *nemo_tool_run_get_stdout      (NemoToolRun          *run);
/* Returns: (transfer none) */
GInputStream *nemo_tool_run_get_stderr      (NemoToolRun          *run);
void          nemo_tool_run_force_exit      (NemoToolRun          *run);
void          nemo_tool_run_wait            (NemoToolRun          *run);
gboolean      nemo_tool_run_wait_check      (NemoToolRun          *run,
					     GError              **error);
/* Only after a wait. */
gboolean      nemo_tool_run_get_if_exited   (NemoToolRun          *run);
gint          nemo_tool_run_get_exit_status (NemoToolRun          *run);
void          nemo_tool_run_free            (NemoToolRun          *run);

#endif /* NEMO_TOOL_RUN_H */
