/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */

/* fuzz-command-template.c - a command line from the config, expanded.

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

/* The template comes out of the settings file, so it is whatever somebody
 * typed. What matters is that no input turns one argument into 2: the split
 * happens before any token is replaced, and the expander must not undo that.
 *
 * The tokens below cover the 3 cases the code treats differently - one
 * value, several, and none - because which branch runs depends on the token,
 * not on the text. */

#include <config.h>

#include <stdint.h>

#include <glib.h>

#include <libnemo-private/nemo-command-template.h>

int LLVMFuzzerTestOneInput (const uint8_t *data, size_t size);

static const char *const one_value[]   = { "/tmp/one.zip", NULL };
static const char *const many_values[] = { "/tmp/a", "/tmp/b b", "/tmp/c\"d", NULL };
static const char *const no_values[]   = { NULL };

int
LLVMFuzzerTestOneInput (const uint8_t *data, size_t size)
{
	static const NemoCommandToken tokens[] = {
		{ "TARGET_ARCHIVE", one_value,   FALSE },
		{ "FILES",          many_values, TRUE  },
		{ "SPLIT",          no_values,   TRUE  },
		{ NULL,             NULL,        FALSE }
	};

	GError *error = NULL;
	char **argv;
	char **missing;
	char *text;

	/* The real caller passes a C string, so an embedded NUL ends it here too. */
	text = g_strndup ((const char *) data, size);

	argv = nemo_command_template_expand (text, tokens, &error);
	g_clear_error (&error);
	g_strfreev (argv);

	missing = nemo_command_template_unused (text, tokens);
	g_strfreev (missing);

	g_free (text);

	return 0;
}
