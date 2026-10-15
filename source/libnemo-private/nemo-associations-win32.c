/* nemo-associations-win32.c - which program opens what, on Windows
 *
 * The registry says what the shell would open a file with, and it is read the
 * way the shell reads it - the same query Explorer makes - but never written:
 * Windows keeps the per-user choice under a hash a program is not meant to
 * set. A choice made here goes into the settings file instead, as a command
 * line with %1 standing for the file, and is consulted first.
 *
 * Copyright © 2026 t00mietum (CryptogID: ปʬϝღถɔ4რఠΔթะ9ƾǝu)
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published
 * by the Free Software Foundation.
 */

#include <config.h>

#include <string.h>
#include <windows.h>
#include <shlwapi.h>

#include <glib/gi18n.h>

#include "nemo-associations-win32.h"
#include "nemo-config.h"
#include "nemo-launch-win32.h"

#define GROUP         "windows"
#define KEY_ASSOCIATIONS "associations"
#define DATA_COMMAND  "nemo-win32-command"

static NemoConfigGroup *
overrides_group (void)
{
	static NemoConfigGroup *group = NULL;

	if (group == NULL) {
		group = nemo_config_get_group (GROUP);
	}

	return group;
}

/* Entries are "<extension>=<command line>", one per type. */
static gboolean
entry_is_for (const gchar *entry,
	      const gchar *extension)
{
	const gchar *eq = strchr (entry, '=');

	return eq != NULL &&
	       (gsize) (eq - entry) == strlen (extension) &&
	       g_ascii_strncasecmp (entry, extension, eq - entry) == 0;
}

/* Returns: (transfer full): free with g_free */
gchar *
nemo_associations_win32_get_override (const gchar *content_type)
{
	gchar **entries = nemo_config_get_strv (overrides_group (), KEY_ASSOCIATIONS);
	gchar *command = NULL;
	gint i;

	for (i = 0; entries != NULL && entries[i] != NULL; i++) {
		if (entry_is_for (entries[i], content_type)) {
			command = g_strdup (strchr (entries[i], '=') + 1);
			break;
		}
	}

	g_strfreev (entries);
	return command;
}

void
nemo_associations_win32_set_override (const gchar *content_type,
				      const gchar *command)
{
	gchar **entries = nemo_config_get_strv (overrides_group (), KEY_ASSOCIATIONS);
	GPtrArray *kept = g_ptr_array_new_with_free_func (g_free);
	gint i;

	for (i = 0; entries != NULL && entries[i] != NULL; i++) {
		if (!entry_is_for (entries[i], content_type)) {
			g_ptr_array_add (kept, g_strdup (entries[i]));
		}
	}

	if (command != NULL && command[0] != '\0') {
		g_ptr_array_add (kept, g_strconcat (content_type, "=", command, NULL));
	}

	g_ptr_array_add (kept, NULL);
	nemo_config_set_strv (overrides_group (), KEY_ASSOCIATIONS, (const gchar *const *) kept->pdata);

	g_ptr_array_free (kept, TRUE);
	g_strfreev (entries);
}

static gchar *
assoc_string (ASSOCSTR     what,
	      const gchar *extension,
	      const wchar_t *verb)
{
	wchar_t *ext = g_utf8_to_utf16 (extension, -1, NULL, NULL, NULL);
	wchar_t *buf;
	DWORD len = 0;
	gchar *out = NULL;

	if (ext == NULL) {
		return NULL;
	}

	if (AssocQueryStringW (ASSOCF_NOTRUNCATE | ASSOCF_INIT_IGNOREUNKNOWN, what, ext, verb, NULL, &len) == S_FALSE && len > 0) {
		buf = g_new0 (wchar_t, len + 1);

		if (AssocQueryStringW (ASSOCF_NOTRUNCATE | ASSOCF_INIT_IGNOREUNKNOWN, what, ext, verb, buf, &len) == S_OK) {
			out = g_utf16_to_utf8 (buf, -1, NULL, NULL, NULL);
		}

		g_free (buf);
	}

	g_free (ext);
	return out;
}

/* The shell's answer for the type: the open verb where there is one, else
 * whatever verb the type calls its default.
 * Returns: (transfer full): free with g_free, and @friendly_name too */
gchar *
nemo_associations_win32_registry_command (const gchar  *content_type,
					  gchar       **friendly_name)
{
	gchar *command;

	command = assoc_string (ASSOCSTR_COMMAND, content_type, L"open");
	if (command == NULL) {
		command = assoc_string (ASSOCSTR_COMMAND, content_type, NULL);
	}

	if (command != NULL && friendly_name != NULL) {
		*friendly_name = assoc_string (ASSOCSTR_FRIENDLYAPPNAME, content_type, NULL);
	}

	return command;
}

static gchar *
file_description (const gchar *exe)
{
	wchar_t *wide = g_utf8_to_utf16 (exe, -1, NULL, NULL, NULL);
	DWORD handle = 0, size;
	gpointer block;
	gchar *name = NULL;
	struct {
		WORD language;
		WORD codepage;
	} *translations = NULL;
	UINT n = 0, i;

	if (wide == NULL) {
		return NULL;
	}

	size = GetFileVersionInfoSizeW (wide, &handle);
	if (size == 0) {
		g_free (wide);
		return NULL;
	}

	block = g_malloc0 (size);

	if (GetFileVersionInfoW (wide, 0, size, block) &&
	    VerQueryValueW (block, L"\\VarFileInfo\\Translation", (void **) &translations, &n)) {
		for (i = 0; i < n / sizeof *translations && name == NULL; i++) {
			wchar_t query[64];
			wchar_t *description = NULL;
			UINT length = 0;

			_snwprintf (query, G_N_ELEMENTS (query), L"\\StringFileInfo\\%04x%04x\\FileDescription",
				    translations[i].language, translations[i].codepage);

			if (VerQueryValueW (block, query, (void **) &description, &length) && length > 1) {
				name = g_utf16_to_utf8 (description, -1, NULL, NULL, NULL);
				if (name != NULL) {
					g_strstrip (name);
				}
				if (name != NULL && name[0] == '\0') {
					g_clear_pointer (&name, g_free);
				}
			}
		}
	}

	g_free (block);
	g_free (wide);
	return name;
}

/* What Explorer would call the program: its own description, or its file name.
 * Returns: (transfer full): free with g_free */
gchar *
nemo_associations_win32_friendly_name (const gchar *command)
{
	gchar *args = NULL;
	gchar *exe = nemo_launch_win32_split_command (command, &args);
	gchar *name = file_description (exe);

	g_free (args);

	if (name == NULL) {
		gchar *base = g_path_get_basename (exe);
		gchar *dot = strrchr (base, '.');

		if (dot != NULL && dot != base) {
			*dot = '\0';
		}
		name = base;
	}

	g_free (exe);
	return name;
}

/* %1 (and its spellings %l, %L, %*) become the file; %2 and up are printer
 * names and the like, which there is nothing to give.
 * Returns: (transfer full): free with g_free */
gchar *
nemo_associations_win32_command_for_file (const gchar *command,
					  const gchar *path)
{
	GString *out = g_string_new (NULL);
	const gchar *p;
	gboolean placed = FALSE;

	for (p = command; *p != '\0'; p++) {
		if (*p == '%' && p[1] != '\0') {
			gchar c = p[1];

			if (c == '1' || c == 'l' || c == 'L' || c == '*') {
				gboolean quoted = p > command && p[-1] == '"' && p[2] == '"';

				if (quoted) {
					g_string_append (out, path);
				} else {
					g_string_append_printf (out, "\"%s\"", path);
				}
				placed = TRUE;
				p++;
				continue;
			}

			if (g_ascii_isdigit (c)) {
				p++;
				continue;
			}

			if (c == '%') {
				g_string_append_c (out, '%');
				p++;
				continue;
			}
		}

		g_string_append_c (out, *p);
	}

	if (!placed) {
		g_string_append_printf (out, " \"%s\"", path);
	}

	/* a dropped "%2" leaves an empty quoted argument behind */
	while (TRUE) {
		gchar *empty = strstr (out->str, " \"\"");

		if (empty == NULL) {
			break;
		}
		g_string_erase (out, empty - out->str, 3);
	}

	return g_string_free (out, FALSE);
}

/* Explorer's own handlers take things only the shell can supply - an item id
 * list as %I, for one. Those verbs are left to the shell to run. */
static gboolean
needs_the_shell (const gchar *command)
{
	const gchar *p;

	for (p = command; (p = strchr (p, '%')) != NULL; p++) {
		if (g_ascii_isalpha (p[1]) && p[1] != 'l' && p[1] != 'L') {
			return TRUE;
		}
	}

	return FALSE;
}

/* One process per file, the command line handed over whole: Windows programs
 * parse their own, so re-quoting it through an argv would only lose backslashes. */
gboolean
nemo_associations_win32_launch (const gchar  *command,
				GList        *locations,
				GError      **error)
{
	GList *l;

	for (l = locations; l != NULL; l = l->next) {
		gchar *path = g_file_get_path (G_FILE (l->data));
		gchar *dir;
		gboolean started;

		if (path == NULL) {
			g_set_error (error, G_IO_ERROR, G_IO_ERROR_NOT_SUPPORTED,
				     _("Only a local file can be opened this way."));
			return FALSE;
		}

		dir = g_path_get_dirname (path);

		if (needs_the_shell (command)) {
			started = nemo_launch_win32_open_path (path, dir, error);
		} else {
			gchar *line = nemo_associations_win32_command_for_file (command, path);

			started = nemo_launch_win32_run_command (line, dir, error);
			g_free (line);
		}

		g_free (dir);
		g_free (path);

		if (!started) {
			return FALSE;
		}
	}

	return TRUE;
}

static GAppInfo *
app_info_for_command (const gchar *command,
		      const gchar *name)
{
	GAppInfo *app = g_app_info_create_from_commandline (command, name, G_APP_INFO_CREATE_NONE, NULL);

	if (app != NULL) {
		g_object_set_data_full (G_OBJECT (app), DATA_COMMAND, g_strdup (command), g_free);
	}

	return app;
}

/* Returns: (transfer none): owned by @app */
const gchar *
nemo_associations_win32_command_of (GAppInfo *app)
{
	return app != NULL ? g_object_get_data (G_OBJECT (app), DATA_COMMAND) : NULL;
}

/* GIO's own entries answer with the same registry command line ours carry, so
 * anything with one can be started the same way. A store app has none.
 * Returns: (transfer none): owned by @app */
const gchar *
nemo_associations_win32_command_for_app (GAppInfo *app)
{
	const gchar *command = nemo_associations_win32_command_of (app);

	if (command == NULL && app != NULL) {
		command = g_app_info_get_commandline (app);
	}

	return command;
}

/* Returns: (transfer full): free with g_free */
gchar *
nemo_associations_win32_name_for_app (GAppInfo *app)
{
	const gchar *command = nemo_associations_win32_command_for_app (app);
	gchar *name = command != NULL ? nemo_associations_win32_friendly_name (command) : NULL;

	if (name == NULL && app != NULL) {
		name = g_strdup (g_app_info_get_name (app));
	}

	return name;
}

/* Returns: (transfer full): unref with g_object_unref */
GAppInfo *
nemo_associations_win32_default_for_type (const gchar *content_type)
{
	GAppInfo *app;
	gchar *command, *name = NULL;

	/* win32 calls the type its extension; anything else is not ours to answer */
	if (content_type == NULL || content_type[0] != '.') {
		return NULL;
	}

	command = nemo_associations_win32_get_override (content_type);
	if (command == NULL) {
		command = nemo_associations_win32_registry_command (content_type, &name);
	}

	if (command == NULL) {
		return NULL;
	}

	if (name == NULL) {
		name = nemo_associations_win32_friendly_name (command);
	}

	app = app_info_for_command (command, name);

	g_free (name);
	g_free (command);
	return app;
}

/* The command a chosen app should be remembered as: what it carries, else
 * what the registry gave GIO for it, else its program with the file appended. */
gboolean
nemo_associations_win32_set_default (GAppInfo    *app,
				     const gchar *content_type)
{
	const gchar *known = nemo_associations_win32_command_of (app);
	gchar *command;

	if (content_type == NULL || content_type[0] != '.') {
		return FALSE;
	}

	if (known != NULL) {
		command = g_strdup (known);
	} else if (g_app_info_get_commandline (app) != NULL) {
		command = g_strdup (g_app_info_get_commandline (app));
	} else if (g_app_info_get_executable (app) != NULL) {
		command = g_strdup_printf ("\"%s\"", g_app_info_get_executable (app));
	} else {
		return FALSE;
	}

	if (strstr (command, "%1") == NULL && strstr (command, "%L") == NULL &&
	    strstr (command, "%l") == NULL && strstr (command, "%*") == NULL) {
		gchar *with_file = g_strconcat (command, " \"%1\"", NULL);

		g_free (command);
		command = with_file;
	}

	nemo_associations_win32_set_override (content_type, command);
	g_free (command);
	return TRUE;
}

/* A registry walk brings the print and print-to verbs along as if they were
 * programs of their own. Their templates take a printer as %2.
 * Returns: (transfer full): @apps less the print verbs, which are unreffed and dropped */
GList *
nemo_associations_win32_filter_apps (GList *apps)
{
	GList *l = apps;

	while (l != NULL) {
		GList *next = l->next;
		const gchar *command = g_app_info_get_commandline (l->data);

		if (command != NULL && strstr (command, "%2") != NULL) {
			g_object_unref (l->data);
			apps = g_list_delete_link (apps, l);
		}

		l = next;
	}

	return apps;
}
