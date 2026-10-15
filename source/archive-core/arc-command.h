/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */

/* arc-command.h - the command lines the 7z and rar programs are run with.

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

#ifndef ARC_COMMAND_H
#define ARC_COMMAND_H

#include <gio/gio.h>

#include "arc-host.h"
#include "arc-settings.h"
#include "arc-writer.h"

G_BEGIN_DECLS

/* What a program is handed, each word already in its own form: sources for
   {{SOURCE_ITEMS}}, skips with {{LINKS}}, and moved, switches among the
   sources that have to come before a "--". */
typedef struct {
	GPtrArray *sources;
	GPtrArray *skips;
	GPtrArray *moved;
} ArcToolNames;

void    arc_tool_names_init    (ArcToolNames *names);
void    arc_tool_names_clear   (ArcToolNames *names);

/* A name as rar is handed it, after prefix, such as "-x". rar reads @name as
   a list file to load. */
char   *arc_rar_name           (const char   *prefix,
				const char   *name);

/* The line from settings, expanded through the host. keep_links has the
   program keep links as links. links_only is the run ahead of the real one,
   for links alone: it leaves the switches the real run adds, or that would
   stop it adding, to the real run. */
char  **arc_build_tool_command (const ArcSettings  *settings,
				const ArcHost      *host,
				ArcBackend          backend,
				ArcFormat           format,
				const ArcOptions   *options,
				const char         *program,
				const char         *archive_path,
				const ArcToolNames *names,
				gboolean            keep_links,
				gboolean            links_only,
				GError            **why);

/* The command line a 7z or rar backend would be run with, in the base folder
   holding the selection, with the names on the line. leave_out holds paths
   relative to that folder, to be left out. The job hands its names in list
   files instead. Exposed so the switches can be checked without spawning
   anything. */
char  **arc_build_command       (const ArcSettings *settings,
				 const ArcHost     *host,
				 ArcBackend         backend,
				 ArcFormat          format,
				 const ArcOptions  *options,
				 const char        *program,
				 const char        *archive_path,
				 GList             *names,
				 GList             *leave_out);

/* The run ahead of that one, which puts in the links named in names, as
   links, while the real run follows links. */
char  **arc_build_links_command (const ArcSettings *settings,
				 const ArcHost     *host,
				 ArcBackend         backend,
				 ArcFormat          format,
				 const ArcOptions  *options,
				 const char        *program,
				 const char        *archive_path,
				 GList             *names);

/* The command a 7z or rar backend unpacks with, from the extract lines in
   settings. NULL for any other backend, or a line that can't be run as
   written. Exposed so the switches can be checked without spawning
   anything. */
char  **arc_build_extract_command (const ArcSettings *settings,
				   const ArcHost     *host,
				   ArcBackend         backend,
				   const char        *program,
				   const char        *archive_path,
				   const char        *destination_path,
				   const char        *password);

/* Switches a command line from the settings gets at run time, whatever was
   typed there: -spd when it runs 7-Zip and lacks it, and -r0 last when it
   runs rar to compress. The saved line is never changed. Takes argv and
   returns the one to use; free with g_strfreev. */
char  **arc_add_run_switches    (char     **argv,
				 gboolean   compressing);

/* Whether a 7z or rar run that ended on a warning status warned about nothing
   but names in skipped: what it was told to leave out, and links that lead
   nowhere, which have nothing to put in.
   output is what the tool printed, with its backspaces already applied;
   skipped holds paths relative to the base folder. */
gboolean arc_only_skipped_links (ArcBackend  backend,
				 int         exit_status,
				 const char *output,
				 GList      *skipped);

G_END_DECLS

#endif /* ARC_COMMAND_H */
