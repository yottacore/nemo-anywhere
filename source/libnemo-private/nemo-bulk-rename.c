/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */

/* nemo-bulk-rename.c - the bulk rename programs known for each platform.

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

#include <string.h>

#include "nemo-bulk-rename.h"

/* The selected files go after the command, each one quoted. Any of these
   takes a plain list of paths. */
typedef struct {
	const char *program;
	const char *args;
	gboolean    qt;
} Renamer;

#if defined (G_OS_WIN32)
/* Under Program Files. */
static const Renamer renamers[] = {
	{ "ReNamer\\ReNamer.exe", "", FALSE },
	{ "Ant Renamer\\Renamer.exe", " -af", FALSE },
};
#elif defined (__APPLE__)
/* App names, for open -a. */
static const Renamer renamers[] = {
	{ "NameChanger", "", FALSE },
	{ "Transnomino", "", FALSE },
	{ "Renamer", "", FALSE },
	{ "A Better Finder Rename 12", "", FALSE },
};
#else
static const Renamer renamers[] = {
	{ "thunar", " --bulk-rename", FALSE },
	{ "krename", "", TRUE },
};
#endif

#if defined (G_OS_WIN32)
static char *
program_path (const Renamer *renamer)
{
	const char *root = g_getenv ("ProgramFiles");

	return g_strconcat (root != NULL && *root != '\0' ? root : "C:\\Program Files",
			    "\\", renamer->program, NULL);
}

static char *
command_of (const Renamer *renamer)
{
	char *path = program_path (renamer);
	char *command = g_strconcat ("\"", path, "\"", renamer->args, NULL);

	g_free (path);
	return command;
}

static gboolean
installed (const Renamer *renamer)
{
	char *path = program_path (renamer);
	gboolean found = g_file_test (path, G_FILE_TEST_IS_REGULAR);

	g_free (path);
	return found;
}
#elif defined (__APPLE__)
static char *
command_of (const Renamer *renamer)
{
	char *quoted = strchr (renamer->program, ' ') != NULL
		? g_strconcat ("\"", renamer->program, "\"", NULL) : g_strdup (renamer->program);
	char *command = g_strconcat ("open -a ", quoted, renamer->args, NULL);

	g_free (quoted);
	return command;
}

static gboolean
installed (const Renamer *renamer)
{
	char *app = g_strconcat (renamer->program, ".app", NULL);
	char *system = g_build_filename ("/Applications", app, NULL);
	char *own = g_build_filename (g_get_home_dir (), "Applications", app, NULL);
	gboolean found = g_file_test (system, G_FILE_TEST_IS_DIR) ||
			 g_file_test (own, G_FILE_TEST_IS_DIR);

	g_free (own);
	g_free (system);
	g_free (app);
	return found;
}
#else
static char *
command_of (const Renamer *renamer)
{
	return g_strconcat (renamer->program, renamer->args, NULL);
}

static gboolean
installed (const Renamer *renamer)
{
	char *path = g_find_program_in_path (renamer->program);
	gboolean found = path != NULL;

	g_free (path);
	return found;
}
#endif

/* KDE and friends. Anything else, or nothing set, counts as GTK. */
static gboolean
desktop_is_qt (void)
{
#if defined (G_OS_WIN32) || defined (__APPLE__)
	return FALSE;
#else
	static const char *const qt_desktops[] = { "kde", "lxqt", "deepin", "dde", "ukui", NULL };
	const char *current = g_getenv ("XDG_CURRENT_DESKTOP");
	char **names;
	gboolean qt = FALSE;
	int i;

	if (current == NULL) {
		return FALSE;
	}
	names = g_strsplit (current, ":", -1);
	for (i = 0; names[i] != NULL && !qt; i++) {
		char *name = g_ascii_strdown (names[i], -1);

		qt = g_strv_contains (qt_desktops, name);
		g_free (name);
	}
	g_strfreev (names);
	return qt;
#endif
}

/* The desktop's own toolkit first, the order above otherwise. */
static GPtrArray *
in_order (void)
{
	GPtrArray *order = g_ptr_array_new ();
	gboolean qt = desktop_is_qt ();
	int pass;
	gsize i;

	for (pass = 0; pass < 2; pass++) {
		for (i = 0; i < G_N_ELEMENTS (renamers); i++) {
			if ((renamers[i].qt == qt) == (pass == 0)) {
				g_ptr_array_add (order, (gpointer) &renamers[i]);
			}
		}
	}
	return order;
}

/* Returns: (transfer full): free with g_free */
char *
nemo_bulk_rename_find_default (void)
{
	GPtrArray *order = in_order ();
	const Renamer *pick = g_ptr_array_index (order, 0);
	guint i;

	for (i = 0; i < order->len; i++) {
		if (installed (g_ptr_array_index (order, i))) {
			pick = g_ptr_array_index (order, i);
			break;
		}
	}
	g_ptr_array_free (order, TRUE);
	return command_of (pick);
}

/* Returns: (transfer full): free with g_free */
char *
nemo_bulk_rename_known_commands (void)
{
	GPtrArray *order = in_order ();
	GString *text = g_string_new (NULL);
	guint i;

	for (i = 0; i < order->len; i++) {
		char *command = command_of (g_ptr_array_index (order, i));

		if (i > 0) {
			g_string_append_c (text, '\n');
		}
		g_string_append (text, command);
		g_free (command);
	}
	g_ptr_array_free (order, TRUE);
	return g_string_free (text, FALSE);
}
