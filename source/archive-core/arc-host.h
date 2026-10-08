/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */

/* arc-host.h - what the archive core asks of the program it runs in.

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

/* The core links GLib and GIO and none of the app around it, so it could
 * leave for a project of its own. Everything else it needs comes through an
 * ArcHost: progress, new files, the folder walk, other programs, command
 * lines, deletes and questions. The job queue stays in the app, which runs
 * the core's work on the job's thread. Modularity, in
 * project/design_docs/20260929-101432_compression.md, has the layers.
 *
 * Call through the arc_* functions at the bottom, not the tables. Each passes
 * host->data first. A NULL host, or a call it left NULL, gets the fallback
 * named beside it, and no fallback touches the disk or starts a program.
 */

#ifndef ARC_HOST_H
#define ARC_HOST_H

#include <gio/gio.h>

#include "arc-settings.h"

G_BEGIN_DECLS

/* Job's thread. Fallback: nothing shown. */
typedef struct {
	void (*status)   (gpointer data, const char *text);
	void (*details)  (gpointer data, const char *text);
	void (*fraction) (gpointer data, double done, double total);
	void (*pulse)    (gpointer data);
} ArcProgress;

/* A file the job made, for a folder view to show. Job's thread. Fallback:
   nobody told. */
typedef struct {
	void (*file_added) (gpointer data, GFile *file);
} ArcChanges;

/* Same contract as g_file_enumerate_children. GIO's own walk lists the wrong
   folder past MAX_PATH on Windows, so the app hands in one that doesn't. Any
   thread. Fallback: G_IO_ERROR_NOT_SUPPORTED. */
typedef struct {
	GFileEnumerator *(*children) (gpointer             data,
				      GFile               *dir,
				      const char          *attributes,
				      GFileQueryInfoFlags  flags,
				      GCancellable        *cancellable,
				      GError             **error);
} ArcWalk;

/* Another program, run with its output read as it goes. On Windows the app
   has one place that may start a program, so the core never starts one
   itself. The calls are named after the GSubprocess ones they stand for. Only
   start gets data; the rest get the run it made. Job's thread. Fallback: start
   fails with G_IO_ERROR_NOT_SUPPORTED. */
typedef struct _ArcRun ArcRun;	/* the host's own */

typedef struct {
	ArcRun       *(*start)       (gpointer            data,
				      const char * const *argv,
				      const char         *cwd,
				      GSubprocessFlags    flags,
				      GError            **error);
	GInputStream *(*stdout_pipe) (ArcRun *run);
	GInputStream *(*stderr_pipe) (ArcRun *run);
	void          (*force_exit)  (ArcRun *run);
	gboolean      (*wait_check)  (ArcRun *run, GError **error);
	gboolean      (*if_exited)   (ArcRun *run);
	int           (*exit_status) (ArcRun *run);
	void          (*free)        (ArcRun *run);
} ArcPrograms;

/* A command line a person can edit, from ArcSettings. Splitting one is the
   app's, since how a backslash reads there is a rule of its own. One
   replaceable value per token; see the app's command template for the rules.
   tokens ends with a NULL name. Job's thread. Fallback: expand fails with
   G_IO_ERROR_NOT_SUPPORTED, and warn_unused says nothing. */
typedef struct {
	const char        *name;
	const char *const *values;
	gboolean           warn_if_dropped;
} ArcCommandToken;

typedef struct {
	char **(*expand)      (gpointer               data,
			       const char            *text,
			       const ArcCommandToken *tokens,
			       GError               **error);
	void   (*warn_unused) (gpointer               data,
			       ArcLine                line,
			       const char            *text,
			       const ArcCommandToken *tokens);
} ArcCommands;

/* The core never deletes anything on its own. remove_own is for a file this
   job wrote, such as a half-written archive or one of its volumes, and
   remove_tree for a folder it extracted into or a file the person chose to
   replace; both on the job's thread, and the app's delete guard sees both.
   trash_by_user is for the originals once the archive checked out, on the
   main thread, and goes through the app's ordinary trash or delete, which
   asks. Fallback: nothing removed, and the two removes answer FALSE. */
typedef struct {
	gboolean (*remove_own)    (gpointer data, GFile *file);
	gboolean (*remove_tree)   (gpointer data, GFile *dir, GCancellable *cancellable);
	void     (*trash_by_user) (gpointer data, GList *files);
} ArcDeletes;

/* CANCEL is 0, so an answer left zeroed stops the job rather than writing
   over something. */
typedef enum {
	ARC_CONFLICT_CANCEL,
	ARC_CONFLICT_SKIP,
	ARC_CONFLICT_REPLACE,
	ARC_CONFLICT_RENAME,
	ARC_CONFLICT_AUTO_RENAME
} ArcConflictResponse;

/* An entry that would land on something already there. */
typedef struct {
	const char *archive_name;
	const char *entry_name;
	gboolean    entry_is_dir;
	guint64     entry_size;
	gint64      entry_mtime;	/* unix seconds, 0 when the archive has none */
	GFile      *destination;
	GFile      *destination_dir;
} ArcConflict;

typedef struct {
	ArcConflictResponse response;
	char               *new_name;	/* RENAME only; the caller frees it */
	gboolean            apply_to_all;
} ArcConflictAnswer;

/* error and warning are on the main thread, after the work is done.
   conflict and password are on the job's thread and wait for the answer; the
   host pauses the job's clock while they're up. Fallbacks: error and warning
   go to the log, conflict answers CANCEL, password answers NULL. */
typedef struct {
	void  (*error)    (gpointer data, const char *primary, const char *details);
	void  (*warning)  (gpointer data, const char *primary, const char *details);
	void  (*conflict) (gpointer data, const ArcConflict *question, ArcConflictAnswer *answer);
	char *(*password) (gpointer data, const char *archive_name);
} ArcAsk;

typedef struct {
	ArcProgress progress;
	ArcChanges  changes;
	ArcWalk     walk;
	ArcPrograms programs;
	ArcCommands commands;
	ArcDeletes  deletes;
	ArcAsk      ask;
	gpointer    data;
} ArcHost;

void             arc_status              (const ArcHost *host, const char *text);
void             arc_details             (const ArcHost *host, const char *text);
void             arc_fraction            (const ArcHost *host, double done, double total);
void             arc_pulse               (const ArcHost *host);

void             arc_file_added          (const ArcHost *host, GFile *file);

/* Returns: (transfer full) */
GFileEnumerator *arc_walk_children       (const ArcHost        *host,
					  GFile                *dir,
					  const char           *attributes,
					  GFileQueryInfoFlags   flags,
					  GCancellable         *cancellable,
					  GError              **error);

/* Returns: (transfer full): free with arc_run_free */
ArcRun          *arc_run_start           (const ArcHost       *host,
					  const char * const  *argv,
					  const char          *cwd,
					  GSubprocessFlags     flags,
					  GError             **error);
/* Returns: (transfer none) */
GInputStream    *arc_run_stdout          (const ArcHost *host, ArcRun *run);
/* Returns: (transfer none) */
GInputStream    *arc_run_stderr          (const ArcHost *host, ArcRun *run);
void             arc_run_force_exit      (const ArcHost *host, ArcRun *run);
gboolean         arc_run_wait_check      (const ArcHost *host, ArcRun *run, GError **error);
/* Only after a wait. */
gboolean         arc_run_if_exited       (const ArcHost *host, ArcRun *run);
int              arc_run_exit_status     (const ArcHost *host, ArcRun *run);
void             arc_run_free            (const ArcHost *host, ArcRun *run);

/* Returns: (transfer full): free with g_strfreev */
char           **arc_command_expand      (const ArcHost         *host,
					  const char            *text,
					  const ArcCommandToken *tokens,
					  GError               **error);
void             arc_command_warn_unused (const ArcHost         *host,
					  ArcLine                line,
					  const char            *text,
					  const ArcCommandToken *tokens);

gboolean         arc_remove_own          (const ArcHost *host, GFile *file);
gboolean         arc_remove_tree         (const ArcHost *host, GFile *dir, GCancellable *cancellable);
void             arc_trash_by_user       (const ArcHost *host, GList *files);

void             arc_show_error          (const ArcHost *host, const char *primary, const char *details);
void             arc_show_warning        (const ArcHost *host, const char *primary, const char *details);
/* answer is filled in either way. */
void             arc_ask_conflict        (const ArcHost       *host,
					  const ArcConflict   *question,
					  ArcConflictAnswer   *answer);
/* Returns: (transfer full): NULL when declined */
char            *arc_ask_password        (const ArcHost *host, const char *archive_name);

G_END_DECLS

#endif /* ARC_HOST_H */
