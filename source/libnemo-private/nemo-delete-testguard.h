/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */

/* nemo-delete-testguard.h - stop on every trash and delete, for testing.

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

/* Scaffolding for finding a removal that nothing else accounts for. Armed, it
 * asks before every trash and delete, whatever started it, and names the C
 * call site plus a backtrace so an unexpected caller can be read off the
 * dialog. The normal confirmations are off while it is armed, so nothing is
 * asked about twice.
 *
 * There are 2 more ways a file stops being where it was, neither of which goes through the
 * delete path. A move takes the original away from its old location, and GIO
 * does that in one step with nothing to hook. An overwrite destroys whatever
 * was at the target, inside g_file_copy or g_file_move, with no call of ours
 * anywhere near it. Both ask here, and both name the 2 paths involved rather
 * than one list, since which side is being lost is the whole question.
 *
 * It works at 2 levels. A job asks once, up front, for everything it is about
 * to take. Below that, the 2 functions that remove a file ask for
 * anything that got there without a job having asked - that is the half which
 * catches a path nobody knew about. The two are kept from doubling up by a
 * thread-local mark, since a job and the removals it drives run on one thread.
 * The overwrite ask ignores that mark: a job asks about its sources, never
 * about a target that happened to already exist.
 *
 * There are 3 things that can arm it, and the first one that speaks wins. The define
 * below only ever arms: at 1 every run of the build asks and neither of the
 * other 2 can take that back. At 0 the NEMO_TESTGUARD_ALL_DELETES
 * environment variable decides, either way, and with nothing there the
 * debug.testguard-all-deletes setting does. That setting is on by default
 * while the removal it was built for is still unexplained, so a build asks
 * unless it has been told not to.
 *
 * It compiles in whichever way the define is set, so a build already in use
 * can be armed without replacing it. Quiet, all it costs a delete is one
 * boolean read.
 */

#ifndef NEMO_DELETE_TESTGUARD_H
#define NEMO_DELETE_TESTGUARD_H

#include <gio/gio.h>
#include <gtk/gtk.h>

/* 1 arms every run of this build, whatever the environment or the settings
   file say. Leave at 0 for a shipping build. */
#define NEMO_TESTGUARD_ALL_DELETES 0

/* 1/true/yes/on, or 0/false/no/off. Beats the setting in both directions, so
   a run can be quieted as well as armed. */
#define NEMO_TESTGUARD_ENV_VAR "NEMO_TESTGUARD_ALL_DELETES"

/* Whether this run asks. Cheap enough to call per file. */
gboolean nemo_delete_testguard_armed    (void);

/* TRUE to go ahead, FALSE if it was called off. Quiet, both say go ahead.
 * `op` is what to call the operation in the dialog, such as "Move to trash". */
gboolean nemo_delete_testguard_ask_at   (const char *op,
					 GList      *files,
					 const char *func,
					 const char *source_file,
					 int         line);

gboolean nemo_delete_testguard_ask_one_at (const char *op,
					   GFile      *file,
					   const char *func,
					   const char *source_file,
					   int         line);

/* A whole move job: the listed files all leave where they are now. */
gboolean nemo_delete_testguard_ask_move_at (const char *op,
					    GList      *files,
					    GFile      *destination,
					    const char *func,
					    const char *source_file,
					    int         line);

/* One file about to be written over. `target` is what is lost. */
gboolean nemo_delete_testguard_ask_overwrite_at (const char *op,
						 GFile      *source,
						 GFile      *target,
						 const char *func,
						 const char *source_file,
						 int         line);

/* Around the part of a job that removes what it already asked about. */
void     nemo_delete_testguard_begin    (void);
void     nemo_delete_testguard_end      (void);

#define nemo_delete_testguard_ask(op, files) \
	nemo_delete_testguard_ask_at ((op), (files), G_STRFUNC, __FILE__, __LINE__)

#define nemo_delete_testguard_ask_one(op, file) \
	nemo_delete_testguard_ask_one_at ((op), (file), G_STRFUNC, __FILE__, __LINE__)

#define nemo_delete_testguard_ask_move(op, files, destination) \
	nemo_delete_testguard_ask_move_at ((op), (files), (destination), G_STRFUNC, __FILE__, __LINE__)

#define nemo_delete_testguard_ask_overwrite(op, source, target) \
	nemo_delete_testguard_ask_overwrite_at ((op), (source), (target), G_STRFUNC, __FILE__, __LINE__)

/* What the dialog would say, minus the backtrace. Exposed because the dialog
   itself needs a display, and the wording of these 2 is the point of them. */
char *nemo_delete_testguard_describe_move      (GList *files,
						GFile *destination);
char *nemo_delete_testguard_describe_overwrite (GFile *source,
						GFile *target);

/* Largest the dialog may get on a monitor with this work area: a quarter of
   the long side and half of the short one. */
void  nemo_delete_testguard_dialog_caps (int  area_width,
					 int  area_height,
					 int *max_width,
					 int *max_height);

#endif /* NEMO_DELETE_TESTGUARD_H */
