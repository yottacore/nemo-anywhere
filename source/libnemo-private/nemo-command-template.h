/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */

/* nemo-command-template.h - a command line a user can edit.

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

/* Where we shell out to somebody else's program, the command line lives in the
 * config file rather than in here, so a person can point it at a different
 * build, add a switch we never thought to offer, or work around a version that
 * spells something its own way.
 *
 * Placeholders are {{UPPER_SNAKE}}, and that is the convention for anything
 * else that grows one later. Braces because no shell and no platform expands
 * them: the same line pasted into cmd, bash or pwsh to try it out comes back
 * unchanged, where %NAME% would quietly vanish in cmd and ${NAME} in bash.
 * Only the tokens a caller declares are replaced, so anything else in braces
 * is passed through as itself and no escape is needed.
 *
 * The template is split into arguments FIRST and tokens are replaced inside
 * each one after, which is what makes it safe: no value is ever re-parsed, so
 * a file named with a space, a quote or a backslash cannot become two
 * arguments or a switch. Nothing here is quoted and nothing here needs to be.
 * The template text itself is split by nemo_user_text_split_command: on
 * Windows a backslash in it is a path character and double quotes group,
 * elsewhere it is the ordinary shell kind, so there a literal backslash must
 * be quoted or doubled. Paths arriving through tokens are unaffected.
 */

#ifndef NEMO_COMMAND_TEMPLATE_H
#define NEMO_COMMAND_TEMPLATE_H

#include <glib.h>

G_BEGIN_DECLS

#define NEMO_COMMAND_TEMPLATE_ERROR (nemo_command_template_error_quark ())

typedef enum {
	NEMO_COMMAND_TEMPLATE_ERROR_PARSE,	/* unbalanced quoting in the template */
	NEMO_COMMAND_TEMPLATE_ERROR_EMPTY,	/* nothing left to run */
	NEMO_COMMAND_TEMPLATE_ERROR_LIST_INSIDE	/* a many-valued token glued to other text */
} NemoCommandTemplateError;

/* One replaceable value. values is NULL-terminated; NULL or empty means the
 * token stands for nothing, and the argument carrying it drops out entirely -
 * which is how an option the user did not ask for leaves no trace.
 *
 * Set warn_if_dropped on the tokens that stand for a choice made elsewhere in
 * the UI - an edit that leaves one out turns that control into a no-op, which
 * is worth a word. Leave it clear on the structural ones: writing the program
 * path or the file names into the line by hand is a fair thing to do. */
typedef struct {
	const char        *name;	/* "TARGET_ARCHIVE", without the braces */
	const char *const *values;
	gboolean           warn_if_dropped;
} NemoCommandToken;

GQuark   nemo_command_template_error_quark (void);

/* argv for g_subprocess_launcher_spawnv, or NULL with error set. tokens is
 * terminated by an entry whose name is NULL. */
char   **nemo_command_template_expand      (const char             *template_text,
					    const NemoCommandToken *tokens,
					    GError                **error);

/* Tokens that carry a value the template never mentions - an edit that dropped
 * {{SPLIT}} while the user is asking for volumes. NULL when nothing is
 * missing, otherwise a NULL-terminated list of names to free with g_strfreev.
 * Only warn_if_dropped tokens are considered, and only while they stand for
 * something: leaving out one that means nothing right now is normal. */
char   **nemo_command_template_unused      (const char             *template_text,
					    const NemoCommandToken *tokens);

/* Says so, once per call, when a template has left a carried value nowhere to
 * go. what names the setting, so the message points at the line to fix. */
void     nemo_command_template_warn_unused (const char             *what,
					    const char             *template_text,
					    const NemoCommandToken *tokens);

/* The line as the user has it, or the built-in one when the settings store is
 * not up yet (a test, or anything reached before nemo_config_init) or the key
 * has been emptied out. Free with g_free. */
char    *nemo_command_template_from_config (const char             *group,
					    const char             *key,
					    const char             *fallback);

G_END_DECLS

#endif /* NEMO_COMMAND_TEMPLATE_H */
