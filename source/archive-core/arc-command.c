/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */

/* arc-command.c - the command lines the 7z and rar programs are run with.

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

#include "arc-command.h"

#include <string.h>

#include <glib/gi18n.h>

/* 7-Zip takes 0,1,3,5,7,9; rar takes 0-5. Both are mapped from the 0-9 the
   dialog offers so the two ends of the scale mean the same thing everywhere. */
static int
seven_zip_level (int level)
{
	static const int steps[] = { 0, 1, 1, 3, 3, 5, 5, 7, 7, 9 };

	return steps[CLAMP (level, 0, ARC_LEVEL_MAX)];
}

static int
rar_level (int level)
{
	static const int steps[] = { 0, 1, 1, 2, 2, 3, 3, 4, 4, 5 };

	return steps[CLAMP (level, 0, ARC_LEVEL_MAX)];
}

static void
free_values (char **values)
{
	int i;

	for (i = 0; values[i] != NULL; i++) {
		g_free (values[i]);
	}
}

/* rar reads @name as a list file to load, both after -x and as a source when
   nothing by that name can be opened, as with a link that leads nowhere. It
   takes ./@name as the name and stores it without the ./.
   Returns: (transfer full): free with g_free */
char *
arc_rar_name (const char *prefix, const char *name)
{
	return g_strconcat (prefix, name[0] == '@' ? "./" : "", name, NULL);
}

#ifdef G_OS_WIN32
/* A copy of the app's PATHEXT test, which the core can't reach. */
static gboolean
on_pathext (const char *name,
	    const char *pathext)
{
	const char *dot;
	g_auto (GStrv) entries = NULL;
	guint i;

	dot = strrchr (name, '.');
	if (dot == NULL || dot[1] == '\0') {
		return FALSE;
	}

	entries = g_strsplit (pathext != NULL ? pathext : ".COM;.EXE;.BAT;.CMD", ";", -1);
	for (i = 0; entries[i] != NULL; i++) {
		if (g_ascii_strcasecmp (g_strstrip (entries[i]), dot) == 0) {
			return TRUE;
		}
	}

	return FALSE;
}
#endif

/* By the program's own name, so a line pointed at another build still counts,
   and one that runs something else is left alone. On Windows PATH can find a
   wrapper such as rar.cmd before the real exe, so any PATHEXT type counts. */
static gboolean
runs_one_of (char              **argv,
	     const char * const *names)
{
	char *base = g_path_get_basename (argv[0]);
	char *dot = strrchr (base, '.');
	gboolean found = FALSE;
	int i;

#ifdef G_OS_WIN32
	if (dot != NULL && dot != base && on_pathext (base, g_getenv ("PATHEXT"))) {
		*dot = '\0';
	}
#else
	if (dot != NULL && dot != base && g_ascii_strcasecmp (dot, ".exe") == 0) {
		*dot = '\0';
	}
#endif
	for (i = 0; names[i] != NULL && !found; i++) {
		found = g_ascii_strcasecmp (base, names[i]) == 0;
	}
	g_free (base);

	return found;
}

/* Returns: (transfer full): free with g_strfreev */
char **
arc_add_run_switches (char   **argv,
		      gboolean compressing)
{
	static const char * const seven_zip[] = { "7z", "7za", "7zr", "7zz", "7zg", NULL };
	static const char * const rar[] = { "rar", "winrar", NULL };
	const char *add = NULL;
	char **out;
	guint end, len, i;

	if (argv == NULL || argv[0] == NULL) {
		return argv;
	}

	/* Both programs take a switch anywhere before the "--", so the end of
	   the switches is the last place one goes, and the last -r rar sees is
	   the one it takes. */
	end = 1;
	while (argv[end] != NULL && strcmp (argv[end], "--") != 0) {
		end++;
	}

	if (runs_one_of (argv, seven_zip)) {
		add = "-spd";
		for (i = 1; i < end; i++) {
			if (g_ascii_strcasecmp (argv[i], add) == 0) {
				return argv;
			}
		}
	} else if (compressing && runs_one_of (argv, rar)) {
		add = "-r0";
	} else {
		return argv;
	}

	len = g_strv_length (argv);
	out = g_new (char *, len + 2);
	memcpy (out, argv, end * sizeof (char *));
	out[end] = g_strdup (add);
	memcpy (out + end + 1, argv + end, (len - end + 1) * sizeof (char *));
	g_free (argv);

	return out;
}

/* A password and volumes are requirements, so a line edited past either one
   is refused. Run anyway, it would write a readable archive, or one file. A
   token stands for something while it has a value, and the line names it by
   its marker. */
static gboolean
line_keeps_requirements (const char            *name,
			 const char            *text,
			 const ArcCommandToken *tokens,
			 GError               **error)
{
	gboolean ok = TRUE;
	int i;

	for (i = 0; tokens[i].name != NULL && ok; i++) {
		char *marker;
		gboolean password = strcmp (tokens[i].name, "PASSWORD") == 0;

		if ((!password && strcmp (tokens[i].name, "SPLIT") != 0) ||
		    tokens[i].values == NULL || tokens[i].values[0] == NULL) {
			continue;
		}

		marker = g_strconcat ("{{", tokens[i].name, "}}", NULL);
		if (strstr (text, marker) == NULL) {
			if (password) {
				g_set_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT,
					     _("The %s command line in the settings has no {{PASSWORD}}, "
					       "so the archive wouldn't be encrypted."), name);
			} else {
				g_set_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT,
					     _("The %s command line in the settings has no {{SPLIT}}, "
					       "so the archive wouldn't be split into volumes."), name);
			}
			ok = FALSE;
		}
		g_free (marker);
	}

	return ok;
}

void
arc_tool_names_init (ArcToolNames *names)
{
	names->sources = g_ptr_array_new_with_free_func (g_free);
	names->skips = g_ptr_array_new_with_free_func (g_free);
	names->moved = g_ptr_array_new_with_free_func (g_free);
}

void
arc_tool_names_clear (ArcToolNames *names)
{
	g_clear_pointer (&names->sources, g_ptr_array_unref);
	g_clear_pointer (&names->skips, g_ptr_array_unref);
	g_clear_pointer (&names->moved, g_ptr_array_unref);
}

/* Names on the line, as the job handed them before it wrote lists. */
static void
tool_names_from_lists (ArcToolNames *out,
		       ArcBackend    backend,
		       GList        *names,
		       GList        *leave_out)
{
	GList *l;

	arc_tool_names_init (out);
	for (l = names; l != NULL; l = l->next) {
		g_ptr_array_add (out->sources, backend == ARC_BACKEND_RAR
				 ? arc_rar_name ("", l->data) : g_strdup (l->data));
	}
	for (l = leave_out; l != NULL; l = l->next) {
		g_ptr_array_add (out->skips, backend == ARC_BACKEND_RAR
				 ? arc_rar_name ("-x", l->data) : g_strconcat ("-x!", (char *) l->data, NULL));
	}
}

/* 7-Zip reads @list after a "--" as a file by that name, and a switch there
   is a name too, so ours go ahead of it wherever the line put them. */
static void
move_before_stop (char **argv, GPtrArray *moved)
{
	guint stop = 1, i, j;

	while (argv[stop] != NULL && strcmp (argv[stop], "--") != 0) {
		stop++;
	}
	if (argv[stop] == NULL || moved == NULL) {
		return;
	}

	for (i = stop + 1; argv[i] != NULL; i++) {
		gboolean ours = FALSE;

		for (j = 0; j < moved->len && !ours; j++) {
			ours = strcmp (argv[i], g_ptr_array_index (moved, j)) == 0;
		}
		if (ours) {
			char *word = argv[i];

			memmove (argv + stop + 1, argv + stop, (i - stop) * sizeof (char *));
			argv[stop] = word;
			stop++;
		}
	}
}

/* Returns: (transfer full): free with g_strfreev, or NULL with why in error */
char **
arc_build_tool_command (const ArcSettings  *settings,
			const ArcHost      *host,
			ArcBackend          backend,
			ArcFormat           format,
			const ArcOptions   *options,
			const char         *program,
			const char         *archive_path,
			const ArcToolNames *names,
			gboolean            keep_links,
			gboolean            links_only,
			GError            **why)
{
	/* One slot per switch a token stands for, plus the terminator. Only the
	   password ever needs two, and only for 7-Zip. */
	char *program_v[2]  = { NULL, NULL };
	char *format_v[2]   = { NULL, NULL };
	char *level_v[2]    = { NULL, NULL };
	char *threads_v[2]  = { NULL, NULL };
	char *password_v[3] = { NULL, NULL, NULL };
	char *split_v[2]    = { NULL, NULL };
	char *solid_v[2]    = { NULL, NULL };
	char *dedupe_v[2]   = { NULL, NULL };
	char *recovery_v[2] = { NULL, NULL };
	char *lock_v[2]     = { NULL, NULL };
	GPtrArray *links_v;
	char *archive_v[2]  = { NULL, NULL };
	GPtrArray *sources;
	ArcLine line;
	const char *name;
	GError *error = NULL;
	char **argv;
	const char *text;
	guint i;
	gboolean has_password;
	int level;
	int threads;

	g_return_val_if_fail (settings != NULL, NULL);
	g_return_val_if_fail (options != NULL, NULL);
	g_return_val_if_fail (program != NULL, NULL);
	g_return_val_if_fail (archive_path != NULL, NULL);
	g_return_val_if_fail (names != NULL, NULL);

	has_password = options->password != NULL && options->password[0] != '\0';
	links_v = g_ptr_array_new_with_free_func (g_free);
	level = CLAMP (options->level, 0, ARC_LEVEL_MAX);
	threads = (int) settings->threads;

	if (backend == ARC_BACKEND_7Z) {
		line = ARC_LINE_CREATE_7Z;

		format_v[0] = g_strdup (format == ARC_FORMAT_7Z ? "-t7z" : "-tzip");
		level_v[0] = g_strdup_printf ("-mx=%d", seven_zip_level (level));
		threads_v[0] = g_strdup_printf ("-mmt=%d", threads);

		if (has_password) {
			/* A password on a command line is readable in the process
			   list; there is no other way to hand it to either tool.
			   It is a value rather than part of the line, so it never
			   reaches the config file. */
			password_v[0] = g_strconcat ("-p", options->password, NULL);

			if (format == ARC_FORMAT_ZIP) {
				password_v[1] = g_strdup ("-mem=AES256");
			} else if (options->encrypt_names) {
				password_v[1] = g_strdup ("-mhe=on");
			}
		}
		if (options->split_size > 0 && !links_only) {
			split_v[0] = g_strdup_printf ("-v%llub", (unsigned long long) options->split_size);
		}
		if (format == ARC_FORMAT_7Z) {
			solid_v[0] = g_strdup (options->solid ? "-ms=on" : "-ms=off");
		}
		if (keep_links &&
		    (arc_backend_caps (format, backend) & ARC_CAP_STORE_LINKS)) {
			g_ptr_array_add (links_v, g_strdup ("-snl"));
		}
	} else if (backend == ARC_BACKEND_RAR) {
		line = ARC_LINE_CREATE_RAR;

		level_v[0] = g_strdup_printf ("-m%d", rar_level (level));
		threads_v[0] = g_strdup_printf ("-mt%d", threads);

		if (has_password) {
			password_v[0] = options->encrypt_names
				? g_strconcat ("-hp", options->password, NULL)
				: g_strconcat ("-p", options->password, NULL);
		}
		if (options->split_size > 0 && !links_only) {
			split_v[0] = g_strdup_printf ("-v%llub", (unsigned long long) options->split_size);
		}
		solid_v[0] = g_strdup (options->solid ? "-s" : "-s-");

		if (options->dedupe) {
			dedupe_v[0] = g_strdup ("-oi");
		}
		/* Both for the real run. A locked archive cannot be added to. */
		if (options->recovery_record && !links_only) {
			recovery_v[0] = g_strdup ("-rr3p");
		}
		if (options->lock && !links_only) {
			lock_v[0] = g_strdup ("-k");
		}

		/* -ol keeps links as links. Without it rar follows them, which is
		   what not keeping them means; -ola would keep them too, with the
		   path made absolute, and -ol- would drop them. */
		if (keep_links) {
			g_ptr_array_add (links_v, g_strdup ("-ol"));
		}
		/* With -r, "sub/name" means every "name" under sub. Only a line
		   edited in the settings still has it, and the last one said is
		   the one rar takes. */
		if (links_only) {
			g_ptr_array_add (links_v, g_strdup ("-r-"));
		}
	} else {
		g_ptr_array_free (links_v, TRUE);
		return NULL;
	}

	/* Left alone, both follow every link, folders too. */
	for (i = 0; names->skips != NULL && i < names->skips->len; i++) {
		g_ptr_array_add (links_v, g_strdup (g_ptr_array_index (names->skips, i)));
	}
	g_ptr_array_add (links_v, NULL);
	program_v[0] = g_strdup (program);
	archive_v[0] = g_strdup (archive_path);

	sources = g_ptr_array_new_with_free_func (g_free);
	for (i = 0; names->moved != NULL && i < names->moved->len; i++) {
		g_ptr_array_add (sources, g_strdup (g_ptr_array_index (names->moved, i)));
	}
	for (i = 0; names->sources != NULL && i < names->sources->len; i++) {
		g_ptr_array_add (sources, g_strdup (g_ptr_array_index (names->sources, i)));
	}
	g_ptr_array_add (sources, NULL);

	{
		const ArcCommandToken tokens[] = {
			/* The last column marks the ones that stand for something
			   the Compress dialog was asked for, so a line edited
			   past one of them can say which control went quiet. */
			{ "PROGRAM",        (const char *const *) program_v,     FALSE },
			{ "FORMAT",         (const char *const *) format_v,      TRUE },
			{ "LEVEL",          (const char *const *) level_v,       TRUE },
			{ "THREADS",        (const char *const *) threads_v,     FALSE },
			{ "PASSWORD",       (const char *const *) password_v,    TRUE },
			{ "SPLIT",          (const char *const *) split_v,       TRUE },
			{ "SOLID",          (const char *const *) solid_v,       TRUE },
			{ "DEDUPE",         (const char *const *) dedupe_v,      TRUE },
			{ "RECOVERY",       (const char *const *) recovery_v,    TRUE },
			{ "LOCK",           (const char *const *) lock_v,        TRUE },
			{ "LINKS",          (const char *const *) links_v->pdata, TRUE },
			{ "TARGET_ARCHIVE", (const char *const *) archive_v,     FALSE },
			{ "SOURCE_ITEMS",   (const char *const *) sources->pdata, FALSE },
			{ NULL, NULL, FALSE }
		};

		name = arc_line_name (line);
		text = settings->lines[line] != NULL ? settings->lines[line] : "";
		argv = arc_command_expand (host, text, tokens, &error);

		if (argv == NULL) {
			g_warning ("The %s command line can't be run as written (%s): %s",
				   name, error->message, text);
			g_set_error (why, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT,
				     _("The %s command line in the settings can't be run as written: %s"),
				     name, error->message);
			g_clear_error (&error);
		} else if (!line_keeps_requirements (name, text, tokens, &error)) {
			g_warning ("%s", error->message);
			g_propagate_error (why, error);
			error = NULL;
			g_strfreev (argv);
			argv = NULL;
		} else {
			arc_command_warn_unused (host, line, text, tokens);
			move_before_stop (argv, names->moved);
			argv = arc_add_run_switches (argv, TRUE);
		}
	}

	g_ptr_array_free (sources, TRUE);
	free_values (program_v);
	free_values (format_v);
	free_values (level_v);
	free_values (threads_v);
	free_values (password_v);
	free_values (split_v);
	free_values (solid_v);
	free_values (dedupe_v);
	free_values (recovery_v);
	free_values (lock_v);
	g_ptr_array_free (links_v, TRUE);
	free_values (archive_v);

	return argv;
}

/* Returns: (transfer full): free with g_strfreev */
char **
arc_build_command (const ArcSettings *settings,
		   const ArcHost     *host,
		   ArcBackend         backend,
		   ArcFormat          format,
		   const ArcOptions  *options,
		   const char        *program,
		   const char        *archive_path,
		   GList             *names,
		   GList             *leave_out)
{
	ArcToolNames words;
	char **argv;

	tool_names_from_lists (&words, backend, names, leave_out);
	argv = arc_build_tool_command (settings, host, backend, format, options, program, archive_path,
				       &words, arc_stores_symlinks (options, format, backend), FALSE, NULL);
	arc_tool_names_clear (&words);
	return argv;
}

/* Returns: (transfer full): free with g_strfreev */
char **
arc_build_links_command (const ArcSettings *settings,
			 const ArcHost     *host,
			 ArcBackend         backend,
			 ArcFormat          format,
			 const ArcOptions  *options,
			 const char        *program,
			 const char        *archive_path,
			 GList             *names)
{
	ArcToolNames words;
	char **argv;

	tool_names_from_lists (&words, backend, names, NULL);
	argv = arc_build_tool_command (settings, host, backend, format, options, program, archive_path,
				       &words, TRUE, TRUE, NULL);
	arc_tool_names_clear (&words);
	return argv;
}

/* Returns: (transfer full): free with g_strfreev */
char **
arc_build_extract_command (const ArcSettings *settings,
			   const ArcHost     *host,
			   ArcBackend         backend,
			   const char        *program,
			   const char        *archive_path,
			   const char        *destination_path,
			   const char        *password)
{
	char *program_v[2]     = { NULL, NULL };
	char *password_v[2]    = { NULL, NULL };
	char *archive_v[2]     = { NULL, NULL };
	char *folder_v[2]      = { NULL, NULL };
	char *folder_sep_v[2]  = { NULL, NULL };
	ArcLine line;
	const char *key;
	GError *error = NULL;
	char **argv;
	const char *text;
	gboolean has_password;

	g_return_val_if_fail (settings != NULL, NULL);
	g_return_val_if_fail (program != NULL, NULL);
	g_return_val_if_fail (archive_path != NULL, NULL);
	g_return_val_if_fail (destination_path != NULL, NULL);

	has_password = password != NULL && password[0] != '\0';

	if (backend == ARC_BACKEND_7Z) {
		line = ARC_LINE_EXTRACT_7Z;

		/* Always answered, never asked: with no password switch at all
		   an encrypted archive stops for one on a console we do not
		   have, and the job would hang instead of failing. */
		password_v[0] = has_password ? g_strconcat ("-p", password, NULL)
					    : g_strdup ("-p");
	} else if (backend == ARC_BACKEND_RAR) {
		line = ARC_LINE_EXTRACT_RAR;

		password_v[0] = has_password ? g_strconcat ("-p", password, NULL)
					    : g_strdup ("-p-");
	} else {
		return NULL;
	}

	program_v[0] = g_strdup (program);
	archive_v[0] = g_strdup (archive_path);
	folder_v[0] = g_strdup (destination_path);
	folder_sep_v[0] = g_strconcat (destination_path, G_DIR_SEPARATOR_S, NULL);

	{
		const ArcCommandToken tokens[] = {
			{ "PROGRAM",                      (const char *const *) program_v,    FALSE },
			{ "PASSWORD",                     (const char *const *) password_v,   TRUE },
			{ "SOURCE_ARCHIVE",               (const char *const *) archive_v,    FALSE },
			{ "TARGET_FOLDER",                (const char *const *) folder_v,     FALSE },
			{ "TARGET_FOLDER_WITH_SEPARATOR", (const char *const *) folder_sep_v, FALSE },
			{ NULL, NULL, FALSE }
		};

		key = arc_line_name (line);
		text = settings->lines[line] != NULL ? settings->lines[line] : "";
		argv = arc_command_expand (host, text, tokens, &error);

		if (argv == NULL) {
			g_warning ("The %s command line can't be run as written (%s): %s",
				   key, error->message, text);
			g_clear_error (&error);
		} else {
			/* Dropping {{PASSWORD}} is the one that bites: with no
			   switch at all an encrypted archive waits for a console
			   that is not there, and the job hangs. */
			arc_command_warn_unused (host, line, text, tokens);
			argv = arc_add_run_switches (argv, FALSE);
		}
	}

	g_free (program_v[0]);
	g_free (password_v[0]);
	g_free (archive_v[0]);
	g_free (folder_v[0]);
	g_free (folder_sep_v[0]);

	return argv;
}

static gboolean
names_skipped (const char *path,
	       gsize       length,
	       GList      *skipped)
{
	GList *l;

	for (l = skipped; l != NULL; l = l->next) {
		const char *name = l->data;

		if (strlen (name) == length && strncmp (name, path, length) == 0) {
			return TRUE;
		}
	}

	return FALSE;
}

/* "WARNING: Cannot open 2 files", which both end on under Windows. It names
   nothing, so it says only how many names have to be there. */
static gboolean
open_count (const char *line,
	    guint      *count)
{
	const char *rest;
	char *end;
	guint64 value;

	if (!g_str_has_prefix (line, "WARNING: Cannot open ")) {
		return FALSE;
	}

	rest = line + strlen ("WARNING: Cannot open ");
	if (!g_ascii_isdigit (*rest)) {
		return FALSE;
	}

	value = g_ascii_strtoull (rest, &end, 10);
	if (strcmp (end, " file") != 0 && strcmp (end, " files") != 0) {
		return FALSE;
	}

	*count = (guint) MIN (value, G_MAXUINT);
	return TRUE;
}

gboolean
arc_only_skipped_links (ArcBackend  backend,
			int         exit_status,
			const char *output,
			GList      *skipped)
{
	char **lines;
	GHashTable *named;	/* the skipped links named, each once */
	gboolean in_list = FALSE;
	gboolean clean = TRUE;
	guint counted = 0;
	guint count;
	gboolean only;
	guint i;

	if (output == NULL || skipped == NULL) {
		return FALSE;
	}
	/* 7-Zip's 1 is any warning at all; rar's 6 is a file it could not open. */
	if (!(backend == ARC_BACKEND_7Z && exit_status == 1) &&
	    !(backend == ARC_BACKEND_RAR && exit_status == 6)) {
		return FALSE;
	}

	lines = g_strsplit (output, "\n", -1);
	for (i = 0; lines[i] != NULL; i++) {
		g_strstrip (lines[i]);
#ifdef G_OS_WIN32
		g_strdelimit (lines[i], "\\", '/');
#endif
	}

	named = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);

	for (i = 0; clean && lines[i] != NULL; i++) {
		const char *line = lines[i];

		if (open_count (line, &count)) {
			counted = MAX (counted, count);
			continue;
		}

		if (backend == ARC_BACKEND_RAR) {
			/* "Cannot open <name>", then the reason on a line of its own. */
			if (g_str_has_prefix (line, "Cannot open ")) {
				const char *name = line + strlen ("Cannot open ");

				/* Said as it was handed over, see arc_rar_name. */
				if (g_str_has_prefix (name, "./@")) {
					name += 2;
				}
				clean = names_skipped (name, strlen (name), skipped);
				/* cppcheck-suppress leakNoVarFunctionCall ; named owns its keys and frees them */
				g_hash_table_add (named, g_strdup (name));
			} else if (g_str_has_prefix (line, "Cannot ") ||
				   g_str_has_prefix (line, "WARNING") ||
				   g_str_has_prefix (line, "ERROR")) {
				clean = FALSE;
			}
			continue;
		}

		/* 7-Zip lists them again at the end, "<name> : <reason>", under
		   one heading for the scan and another for files it could not
		   read while writing. Every one has to be a link we know of. */
		if (g_str_has_suffix (line, "WARNINGS for files and folders:") ||
		    g_str_has_suffix (line, "WARNINGS for files:")) {
			in_list = TRUE;
		} else if (in_list) {
			if (g_str_has_prefix (line, "----")) {
				in_list = FALSE;
			} else if (line[0] != '\0') {
				/* A name can hold " : " itself, so try each one. */
				const char *separator = strstr (line, " : ");

				while (separator != NULL &&
				       !names_skipped (line, (gsize) (separator - line), skipped)) {
					separator = strstr (separator + 1, " : ");
				}
				clean = separator != NULL;
				if (clean) {
					/* cppcheck-suppress leakNoVarFunctionCall ; named owns its keys and frees them */
					g_hash_table_add (named, g_strndup (line, (gsize) (separator - line)));
				}
			}
		} else if (g_str_has_prefix (line, "WARNING")) {
			/* As it happens: "WARNING: <reason>", and the name next. */
			const char *name = lines[i + 1];

			clean = name != NULL && names_skipped (name, strlen (name), skipped);
			if (clean) {
				/* cppcheck-suppress leakNoVarFunctionCall ; named owns its keys and frees them */
				g_hash_table_add (named, g_strdup (name));
			}
			i++;
		} else if (g_str_has_prefix (line, "ERROR")) {
			clean = FALSE;
		}
	}

	only = clean && g_hash_table_size (named) > 0 && counted <= g_hash_table_size (named);

	g_hash_table_destroy (named);
	g_strfreev (lines);

	return only;
}

