/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */

/* nemo-command-template.c - a command line a user can edit.

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

#include <config.h>
#include "nemo-command-template.h"

#include <string.h>

#include "nemo-config.h"

#define TOKEN_OPEN  "{{"
#define TOKEN_CLOSE "}}"
#define TOKEN_MARK_LEN 2

GQuark
nemo_command_template_error_quark (void)
{
	return g_quark_from_static_string ("nemo-command-template-error-quark");
}

static const NemoCommandToken *
find_token (const NemoCommandToken *tokens,
	    const char             *name,
	    gsize                   length)
{
	int i;

	for (i = 0; tokens != NULL && tokens[i].name != NULL; i++) {
		if (strlen (tokens[i].name) == length &&
		    strncmp (tokens[i].name, name, length) == 0) {
			return &tokens[i];
		}
	}

	return NULL;
}

static guint
token_count (const NemoCommandToken *token)
{
	guint count = 0;

	while (token->values != NULL && token->values[count] != NULL) {
		count++;
	}

	return count;
}

/* The next complete {{...}} at or after from. An opening brace with no closing
   one is not a placeholder, it is text. */
static gboolean
next_token_span (const char  *from,
		 const char **out_open,
		 const char **out_close)
{
	const char *open = strstr (from, TOKEN_OPEN);
	const char *close;

	if (open == NULL) {
		return FALSE;
	}

	close = strstr (open + TOKEN_MARK_LEN, TOKEN_CLOSE);

	if (close == NULL) {
		return FALSE;
	}

	*out_open = open;
	*out_close = close;

	return TRUE;
}

/* One template argument becomes none, one, or several real ones. */
static gboolean
expand_one (const char             *arg,
	    const NemoCommandToken *tokens,
	    GPtrArray              *out,
	    GError                **error)
{
	const NemoCommandToken *token;
	const char *open, *close, *cursor;
	GString *built;

	/* An argument that is nothing but one placeholder takes that token's
	   whole list - none of them, one, or a file apiece. */
	if (next_token_span (arg, &open, &close) &&
	    open == arg && close[TOKEN_MARK_LEN] == '\0') {
		token = find_token (tokens, arg + TOKEN_MARK_LEN,
				    close - (arg + TOKEN_MARK_LEN));

		if (token != NULL) {
			guint i, count = token_count (token);

			for (i = 0; i < count; i++) {
				g_ptr_array_add (out, g_strdup (token->values[i]));
			}

			return TRUE;
		}
		/* Nobody declares it, so it is not a placeholder at all. */
	}

	built = g_string_new (NULL);
	cursor = arg;

	while (next_token_span (cursor, &open, &close)) {
		guint count;

		token = find_token (tokens, open + TOKEN_MARK_LEN,
				    close - (open + TOKEN_MARK_LEN));

		if (token == NULL) {
			g_string_append_len (built, cursor,
					     close + TOKEN_MARK_LEN - cursor);
			cursor = close + TOKEN_MARK_LEN;
			continue;
		}

		count = token_count (token);

		if (count == 0) {
			/* A "-o" with nothing to point at is not an argument. */
			g_string_free (built, TRUE);
			return TRUE;
		}

		if (count > 1) {
			g_set_error (error, NEMO_COMMAND_TEMPLATE_ERROR,
				     NEMO_COMMAND_TEMPLATE_ERROR_LIST_INSIDE,
				     "{{%s}} stands for several values, so it has to be an "
				     "argument of its own rather than part of \"%s\"",
				     token->name, arg);
			g_string_free (built, TRUE);
			return FALSE;
		}

		g_string_append_len (built, cursor, open - cursor);
		g_string_append (built, token->values[0]);
		cursor = close + TOKEN_MARK_LEN;
	}

	g_string_append (built, cursor);
	g_ptr_array_add (out, g_string_free (built, FALSE));

	return TRUE;
}

/* Returns: (transfer full): free with g_strfreev */
char **
nemo_command_template_expand (const char             *template_text,
			      const NemoCommandToken *tokens,
			      GError                **error)
{
	GPtrArray *out;
	GError *parse_error = NULL;
	char **parts = NULL;
	int i;

	g_return_val_if_fail (template_text != NULL, NULL);

	if (!g_shell_parse_argv (template_text, NULL, &parts, &parse_error)) {
		g_set_error (error, NEMO_COMMAND_TEMPLATE_ERROR,
			     NEMO_COMMAND_TEMPLATE_ERROR_PARSE,
			     "%s", parse_error->message);
		g_clear_error (&parse_error);
		return NULL;
	}

	out = g_ptr_array_new_with_free_func (g_free);

	for (i = 0; parts[i] != NULL; i++) {
		if (!expand_one (parts[i], tokens, out, error)) {
			g_strfreev (parts);
			g_ptr_array_free (out, TRUE);
			return NULL;
		}
	}

	g_strfreev (parts);

	/* Every argument can legitimately drop out, and then there is nothing
	   left to run - worth saying so rather than spawning nothing. */
	if (out->len == 0) {
		g_set_error_literal (error, NEMO_COMMAND_TEMPLATE_ERROR,
				     NEMO_COMMAND_TEMPLATE_ERROR_EMPTY,
				     "the command line came out empty");
		g_ptr_array_free (out, TRUE);
		return NULL;
	}

	g_ptr_array_add (out, NULL);

	return (char **) g_ptr_array_free (out, FALSE);
}

/* Returns: (transfer full): free with g_strfreev */
char **
nemo_command_template_unused (const char             *template_text,
			      const NemoCommandToken *tokens)
{
	GPtrArray *missing;
	int i;

	g_return_val_if_fail (template_text != NULL, NULL);

	missing = g_ptr_array_new ();

	for (i = 0; tokens != NULL && tokens[i].name != NULL; i++) {
		char *marker;

		/* Only a choice the UI made, and only while it stands for
		   something - a structural token written out by hand, or an
		   option nobody turned on, is not worth a word. */
		if (!tokens[i].warn_if_dropped || token_count (&tokens[i]) == 0) {
			continue;
		}

		marker = g_strconcat (TOKEN_OPEN, tokens[i].name, TOKEN_CLOSE, NULL);

		if (strstr (template_text, marker) == NULL) {
			g_ptr_array_add (missing, g_strdup (tokens[i].name));
		}

		g_free (marker);
	}

	if (missing->len == 0) {
		g_ptr_array_free (missing, TRUE);
		return NULL;
	}

	g_ptr_array_add (missing, NULL);

	return (char **) g_ptr_array_free (missing, FALSE);
}

/* Returns: (transfer full): free with g_free */
char *
nemo_command_template_from_config (const char *group,
				   const char *key,
				   const char *fallback)
{
	char *text;

	g_return_val_if_fail (fallback != NULL, NULL);

	if (!nemo_config_is_ready ()) {
		return g_strdup (fallback);
	}

	text = nemo_config_get_string (nemo_config_get_group (group), key);

	/* An emptied key means "give me the one you shipped with" rather than
	   "run nothing", which is what a person clearing the line expects. */
	if (text == NULL || g_strstrip (text)[0] == '\0') {
		g_free (text);
		return g_strdup (fallback);
	}

	return text;
}

void
nemo_command_template_warn_unused (const char             *what,
				   const char             *template_text,
				   const NemoCommandToken *tokens)
{
	char **missing = nemo_command_template_unused (template_text, tokens);
	char *joined;

	if (missing == NULL) {
		return;
	}

	/* Not an error - the program simply never hears about that option - so
	   name the one that got dropped rather than leaving the dialog looking
	   broken to somebody who edited the line months ago. */
	joined = g_strjoinv (", ", missing);
	g_warning ("The %s command line has nowhere to put %s, so that option is not being passed on",
		   what, joined);
	g_free (joined);
	g_strfreev (missing);
}
