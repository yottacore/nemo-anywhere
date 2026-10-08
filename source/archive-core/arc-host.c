/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */

/* arc-host.c - what the archive core asks of the program it runs in.

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

#include "arc-host.h"

#define HAS(host, part, call) ((host) != NULL && (host)->part.call != NULL)

static void
not_supported (GError    **error,
	       const char *what)
{
	g_set_error (error, G_IO_ERROR, G_IO_ERROR_NOT_SUPPORTED,
		     "The program running the archive core gave it no way to %s", what);
}

void
arc_status (const ArcHost *host,
	    const char    *text)
{
	if (HAS (host, progress, status)) {
		host->progress.status (host->data, text);
	}
}

void
arc_details (const ArcHost *host,
	     const char    *text)
{
	if (HAS (host, progress, details)) {
		host->progress.details (host->data, text);
	}
}

void
arc_fraction (const ArcHost *host,
	      double         done,
	      double         total)
{
	if (HAS (host, progress, fraction)) {
		host->progress.fraction (host->data, done, total);
	}
}

void
arc_pulse (const ArcHost *host)
{
	if (HAS (host, progress, pulse)) {
		host->progress.pulse (host->data);
	}
}

void
arc_file_added (const ArcHost *host,
		GFile         *file)
{
	if (HAS (host, changes, file_added)) {
		host->changes.file_added (host->data, file);
	}
}

/* Returns: (transfer full) */
GFileEnumerator *
arc_walk_children (const ArcHost        *host,
		   GFile                *dir,
		   const char           *attributes,
		   GFileQueryInfoFlags   flags,
		   GCancellable         *cancellable,
		   GError              **error)
{
	if (!HAS (host, walk, children)) {
		not_supported (error, "list a folder");
		return NULL;
	}
	return host->walk.children (host->data, dir, attributes, flags, cancellable, error);
}

gboolean
arc_leaves_for_share (const ArcHost *host,
		      const char    *folder,
		      const char    *path)
{
	return !HAS (host, shares, leaves_for) || host->shares.leaves_for (host->data, folder, path);
}

/* Returns: (transfer full): free with arc_run_free */
ArcRun *
arc_run_start (const ArcHost       *host,
	       const char * const  *argv,
	       const char          *cwd,
	       GSubprocessFlags     flags,
	       GError             **error)
{
	if (!HAS (host, programs, start)) {
		not_supported (error, "start a program");
		return NULL;
	}
	return host->programs.start (host->data, argv, cwd, flags, error);
}

/* Returns: (transfer none) */
GInputStream *
arc_run_stdout (const ArcHost *host,
		ArcRun        *run)
{
	return run != NULL && HAS (host, programs, stdout_pipe) ? host->programs.stdout_pipe (run) : NULL;
}

/* Returns: (transfer none) */
GInputStream *
arc_run_stderr (const ArcHost *host,
		ArcRun        *run)
{
	return run != NULL && HAS (host, programs, stderr_pipe) ? host->programs.stderr_pipe (run) : NULL;
}

void
arc_run_force_exit (const ArcHost *host,
		    ArcRun        *run)
{
	if (run != NULL && HAS (host, programs, force_exit)) {
		host->programs.force_exit (run);
	}
}

gboolean
arc_run_wait_check (const ArcHost *host,
		    ArcRun        *run,
		    GError       **error)
{
	if (run == NULL || !HAS (host, programs, wait_check)) {
		not_supported (error, "wait for a program");
		return FALSE;
	}
	return host->programs.wait_check (run, error);
}

gboolean
arc_run_if_exited (const ArcHost *host,
		   ArcRun        *run)
{
	return run != NULL && HAS (host, programs, if_exited) && host->programs.if_exited (run);
}

int
arc_run_exit_status (const ArcHost *host,
		     ArcRun        *run)
{
	return run != NULL && HAS (host, programs, exit_status) ? host->programs.exit_status (run) : -1;
}

void
arc_run_free (const ArcHost *host,
	      ArcRun        *run)
{
	if (run != NULL && HAS (host, programs, free)) {
		host->programs.free (run);
	}
}

/* Returns: (transfer full): free with g_strfreev */
char **
arc_command_expand (const ArcHost         *host,
		    const char            *text,
		    const ArcCommandToken *tokens,
		    GError               **error)
{
	if (!HAS (host, commands, expand)) {
		not_supported (error, "read a command line");
		return NULL;
	}
	return host->commands.expand (host->data, text, tokens, error);
}

void
arc_command_warn_unused (const ArcHost         *host,
			 ArcLine                line,
			 const char            *text,
			 const ArcCommandToken *tokens)
{
	if (HAS (host, commands, warn_unused)) {
		host->commands.warn_unused (host->data, line, text, tokens);
	}
}

gboolean
arc_remove_own (const ArcHost *host,
		GFile         *file)
{
	return HAS (host, deletes, remove_own) && host->deletes.remove_own (host->data, file);
}

gboolean
arc_remove_tree (const ArcHost *host,
		 GFile         *dir,
		 GCancellable  *cancellable)
{
	return HAS (host, deletes, remove_tree) && host->deletes.remove_tree (host->data, dir, cancellable);
}

void
arc_trash_by_user (const ArcHost *host,
		   GList         *files)
{
	if (!HAS (host, deletes, trash_by_user)) {
		/* Asked for by a person, so not going quietly. */
		g_warning ("The originals were kept, %u in all: nothing to send them to the trash with",
			   g_list_length (files));
		return;
	}
	host->deletes.trash_by_user (host->data, files);
}

void
arc_show_error (const ArcHost *host,
		const char    *primary,
		const char    *details)
{
	if (!HAS (host, ask, error)) {
		g_warning ("%s%s%s", primary, details != NULL ? ": " : "", details != NULL ? details : "");
		return;
	}
	host->ask.error (host->data, primary, details);
}

void
arc_show_warning (const ArcHost *host,
		  const char    *primary,
		  const char    *details)
{
	if (!HAS (host, ask, warning)) {
		g_warning ("%s%s%s", primary, details != NULL ? ": " : "", details != NULL ? details : "");
		return;
	}
	host->ask.warning (host->data, primary, details);
}

void
arc_ask_conflict (const ArcHost     *host,
		  const ArcConflict *question,
		  ArcConflictAnswer *answer)
{
	answer->response = ARC_CONFLICT_CANCEL;
	answer->new_name = NULL;
	answer->apply_to_all = FALSE;

	if (HAS (host, ask, conflict)) {
		host->ask.conflict (host->data, question, answer);
	}
}

/* Returns: (transfer full): NULL when declined */
char *
arc_ask_password (const ArcHost *host,
		  const char    *archive_name)
{
	char *password;

	if (!HAS (host, ask, password)) {
		return NULL;
	}

	password = host->ask.password (host->data, archive_name);
	if (password != NULL && password[0] == '\0') {
		g_clear_pointer (&password, g_free);
	}
	return password;
}
