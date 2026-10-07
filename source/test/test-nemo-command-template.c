/* The command-line templates a user can edit: what the placeholders turn into,
 * and - the reason the expansion happens after the split rather than before it
 * - that no value a user supplies can ever become an argument of its own or a
 * switch. Nothing here spawns anything. */

#include <config.h>

#include <stdlib.h>
#include <string.h>
#include <glib.h>

#include <gtk/gtk.h>

#include <libnemo-private/nemo-archive.h>
#include <libnemo-private/nemo-archive-commands.h>
#include <libnemo-private/nemo-command-template.h>
#include <libnemo-private/nemo-config.h>

#include "test-scratch.h"
#include "test-check.h"

static guint
count_args (char **argv)
{
	guint n = 0;

	while (argv != NULL && argv[n] != NULL) {
		n++;
	}

	return n;
}

static const char * const one_program[]  = { "7z", NULL };
static const char * const one_archive[]  = { "/tmp/out.7z", NULL };
static const char * const no_values[]    = { NULL };
static const char * const three_names[]  = { "one.txt", "a folder", "-weird", NULL };
static const char * const two_switches[] = { "-psecret", "-mhe=on", NULL };

static void
check_basics (void)
{
	NemoCommandToken tokens[] = {
		{ "PROGRAM",        one_program, FALSE },
		{ "TARGET_ARCHIVE", one_archive, FALSE },
		{ "SOURCE_ITEMS",   three_names, FALSE },
		{ "SPLIT",          no_values,   TRUE },
		{ NULL, NULL, FALSE }
	};
	GError *error = NULL;
	char **argv;

	argv = nemo_command_template_expand ("{{PROGRAM}} a {{SPLIT}} -- {{TARGET_ARCHIVE}} {{SOURCE_ITEMS}}",
					     tokens, &error);
	check (argv != NULL);
	check (error == NULL);

	if (argv == NULL) {
		g_clear_error (&error);
		return;
	}

	/* The list becomes one argument apiece, and the option nobody asked for
	   leaves nothing behind - not an empty argument, which several programs
	   would read as a filename. */
	check (count_args (argv) == 7);
	check (g_strcmp0 (argv[0], "7z") == 0);
	check (g_strcmp0 (argv[1], "a") == 0);
	check (g_strcmp0 (argv[2], "--") == 0);
	check (g_strcmp0 (argv[3], "/tmp/out.7z") == 0);
	check (g_strcmp0 (argv[4], "one.txt") == 0);
	check (g_strcmp0 (argv[5], "a folder") == 0);
	check (g_strcmp0 (argv[6], "-weird") == 0);
	g_strfreev (argv);
}

static void
check_embedded (void)
{
	static const char * const folder[] = { "/tmp/some place", NULL };
	NemoCommandToken tokens[] = {
		{ "TARGET_FOLDER", folder,    FALSE },
		{ "PASSWORD",      no_values, TRUE },
		{ NULL, NULL, FALSE }
	};
	GError *error = NULL;
	char **argv;

	/* 7-Zip glues its destination to the switch, so a token has to be able
	   to sit inside an argument as well as be one. */
	argv = nemo_command_template_expand ("x -o{{TARGET_FOLDER}} {{PASSWORD}}", tokens, &error);
	check (argv != NULL);

	if (argv != NULL) {
		check (count_args (argv) == 2);
		check (g_strcmp0 (argv[1], "-o/tmp/some place") == 0);
		g_strfreev (argv);
	}
	g_clear_error (&error);

	/* A switch with nothing to point at is not an argument at all. */
	tokens[0].values = no_values;
	argv = nemo_command_template_expand ("x -o{{TARGET_FOLDER}} --", tokens, &error);
	check (argv != NULL);

	if (argv != NULL) {
		check (count_args (argv) == 2);
		check (g_strcmp0 (argv[1], "--") == 0);
		g_strfreev (argv);
	}
	g_clear_error (&error);
}

static void
check_values_stay_one_argument (void)
{
	/* The whole reason the template is split first and filled in after: a
	   file named like this went through g_shell_quote before, and one
	   missed quote turned a name into arguments. */
	static const char * const nasty[] = {
		"a file \"with\" quotes.txt",
		"back\\slash and $HOME",
		"; rm -rf /",
		NULL
	};
	NemoCommandToken tokens[] = {
		{ "SOURCE_ITEMS", nasty, FALSE },
		{ NULL, NULL, FALSE }
	};
	GError *error = NULL;
	char **argv = nemo_command_template_expand ("7z a -- out.7z {{SOURCE_ITEMS}}", tokens, &error);

	check (argv != NULL);

	if (argv != NULL) {
		check (count_args (argv) == 7);
		check (g_strcmp0 (argv[4], "a file \"with\" quotes.txt") == 0);
		check (g_strcmp0 (argv[5], "back\\slash and $HOME") == 0);
		check (g_strcmp0 (argv[6], "; rm -rf /") == 0);
		g_strfreev (argv);
	}
	g_clear_error (&error);
}

static void
check_refusals (void)
{
	NemoCommandToken tokens[] = {
		{ "PASSWORD", two_switches, TRUE },
		{ NULL, NULL, FALSE }
	};
	GError *error = NULL;
	char **argv;

	/* Two switches cannot be glued to one argument without silently losing
	   the second, so it is refused rather than half-honored. */
	argv = nemo_command_template_expand ("7z a -x{{PASSWORD}}", tokens, &error);
	check (argv == NULL);
	check (g_error_matches (error, NEMO_COMMAND_TEMPLATE_ERROR,
				NEMO_COMMAND_TEMPLATE_ERROR_LIST_INSIDE));
	g_clear_error (&error);

	/* Standing on its own it is fine, and becomes both. */
	argv = nemo_command_template_expand ("7z a {{PASSWORD}}", tokens, &error);
	check (argv != NULL);

	if (argv != NULL) {
		check (count_args (argv) == 4);
		check (g_strcmp0 (argv[3], "-mhe=on") == 0);
		g_strfreev (argv);
	}
	g_clear_error (&error);

	argv = nemo_command_template_expand ("7z a \"unbalanced", tokens, &error);
	check (argv == NULL);
	check (g_error_matches (error, NEMO_COMMAND_TEMPLATE_ERROR,
				NEMO_COMMAND_TEMPLATE_ERROR_PARSE));
	g_clear_error (&error);

	/* Every argument can legitimately drop out, and then there is nothing
	   to run. */
	tokens[0].values = no_values;
	argv = nemo_command_template_expand ("{{PASSWORD}}", tokens, &error);
	check (argv == NULL);
	check (g_error_matches (error, NEMO_COMMAND_TEMPLATE_ERROR,
				NEMO_COMMAND_TEMPLATE_ERROR_EMPTY));
	g_clear_error (&error);
}

static void
check_passthrough (void)
{
	NemoCommandToken tokens[] = {
		{ "PROGRAM", one_program, FALSE },
		{ NULL, NULL, FALSE }
	};
	GError *error = NULL;
	char **argv;

	/* Only the tokens a caller declares are placeholders. Everything else
	   in braces is somebody's argument, and no escape is needed for it. */
	argv = nemo_command_template_expand ("{{PROGRAM}} a -m{{NOT_A_TOKEN}} {{unclosed", tokens, &error);
	check (argv != NULL);

	if (argv != NULL) {
		check (count_args (argv) == 4);
		check (g_strcmp0 (argv[2], "-m{{NOT_A_TOKEN}}") == 0);
		check (g_strcmp0 (argv[3], "{{unclosed") == 0);
		g_strfreev (argv);
	}
	g_clear_error (&error);
}

/* A line the user edits takes a Windows path as written there. Elsewhere it
   keeps the shell's rules, backslash escapes included. */
static void
check_windows_paths (void)
{
	NemoCommandToken tokens[] = {
		{ "SOURCE_ARCHIVE", one_archive, FALSE },
		{ NULL, NULL, FALSE }
	};
	GError *error = NULL;
	char **argv;

	argv = nemo_command_template_expand ("C:\\Tools\\7z.exe x -oC:\\out\\new {{SOURCE_ARCHIVE}}", tokens, &error);
	check (argv != NULL);

	if (argv != NULL) {
		check (count_args (argv) == 4);
#ifdef G_OS_WIN32
		check (g_strcmp0 (argv[0], "C:\\Tools\\7z.exe") == 0);
		check (g_strcmp0 (argv[2], "-oC:\\out\\new") == 0);
#else
		check (g_strcmp0 (argv[0], "C:Tools7z.exe") == 0);
		check (g_strcmp0 (argv[2], "-oC:outnew") == 0);
#endif
		check (g_strcmp0 (argv[3], one_archive[0]) == 0);
		g_strfreev (argv);
	}
	g_clear_error (&error);

	argv = nemo_command_template_expand ("\"C:\\Program Files\\7-Zip\\7z.exe\" x \\\\box\\share\\a.7z", tokens, &error);
	check (argv != NULL);

	if (argv != NULL) {
		check (count_args (argv) == 3);
		check (g_strcmp0 (argv[0], "C:\\Program Files\\7-Zip\\7z.exe") == 0);
#ifdef G_OS_WIN32
		check (g_strcmp0 (argv[2], "\\\\box\\share\\a.7z") == 0);
#else
		check (g_strcmp0 (argv[2], "\\boxsharea.7z") == 0);
#endif
		g_strfreev (argv);
	}
	g_clear_error (&error);
}

static void
check_unused (void)
{
	NemoCommandToken tokens[] = {
		{ "PROGRAM", one_program,  FALSE },
		{ "SPLIT",   two_switches, TRUE },
		{ "SOLID",   no_values,    TRUE },
		{ NULL, NULL, FALSE }
	};
	char **missing;

	/* A line that mentions everything carrying a value has nothing to say. */
	check (nemo_command_template_unused ("{{PROGRAM}} a {{SPLIT}} {{SOLID}}", tokens) == NULL);

	/* Leaving out a token that stands for nothing is the normal case. */
	check (nemo_command_template_unused ("{{PROGRAM}} a {{SPLIT}}", tokens) == NULL);

	/* So is writing a structural one out by hand - naming the program is a
	   fair reason to edit the line in the first place. */
	check (nemo_command_template_unused ("/opt/7zz a {{SPLIT}}", tokens) == NULL);

	/* Leaving out one the user asked for is what wants saying. */
	missing = nemo_command_template_unused ("{{PROGRAM}} a", tokens);
	check (missing != NULL);

	if (missing != NULL) {
		check (count_args (missing) == 1);
		check (g_strcmp0 (missing[0], "SPLIT") == 0);
		g_strfreev (missing);
	}
}

static gboolean
has_arg (char       **argv,
	 const char  *wanted)
{
	int i;

	for (i = 0; argv != NULL && argv[i] != NULL; i++) {
		if (g_strcmp0 (argv[i], wanted) == 0) {
			return TRUE;
		}
	}

	return FALSE;
}

/* The point of keeping the line in the config rather than in the source: an
   edit has to reach the program, and the dialog has to keep working around it.
   Runs against a throwaway config root. */
static void
check_from_config (void)
{
	NemoConfigGroup *archive = nemo_config_get_group (NEMO_ARCHIVE_COMMANDS_GROUP);
	NemoArchiveOptions options;
	GList *names = NULL;
	char **argv;
	char *text;

	/* Untouched, the store hands back the same line the builder falls back
	   to when there is no store at all - one default, not two. */
	text = nemo_command_template_from_config (NEMO_ARCHIVE_COMMANDS_GROUP,
						  NEMO_ARCHIVE_COMMAND_KEY_7Z,
						  NEMO_ARCHIVE_COMMAND_7Z_DEFAULT);
	check (g_strcmp0 (text, NEMO_ARCHIVE_COMMAND_7Z_DEFAULT) == 0);
	g_free (text);

	names = g_list_append (names, (gpointer) "one.txt");

	nemo_archive_options_init (&options);
	options.format = NEMO_ARCHIVE_FORMAT_7Z;
	options.level = NEMO_ARCHIVE_LEVEL_MAX;

	/* A different binary, an extra switch, and one of ours taken out. */
	nemo_config_set_string (archive, NEMO_ARCHIVE_COMMAND_KEY_7Z,
				"/opt/7zz a {{FORMAT}} {{LEVEL}} -mmt=4 -y -- {{TARGET_ARCHIVE}} {{SOURCE_ITEMS}}");

	argv = nemo_archive_build_command (NEMO_ARCHIVE_BACKEND_7Z, options.format, &options,
					   "7z", "/tmp/out.7z", names, NULL);
	check (argv != NULL);

	if (argv != NULL) {
		/* The program written into the line beats the one probed for. */
		check (g_strcmp0 (argv[0], "/opt/7zz") == 0);
		check (has_arg (argv, "-mmt=4"));
		/* What the dialog was asked for still arrives alongside it. */
		check (has_arg (argv, "-t7z"));
		check (has_arg (argv, "-mx=9"));
		check (has_arg (argv, "one.txt"));
		check (!has_arg (argv, "-bsp1"));
		g_strfreev (argv);
	}

	/* Emptying the key puts the shipped line back rather than running
	   nothing, which is what clearing a field is meant to mean. */
	nemo_config_set_string (archive, NEMO_ARCHIVE_COMMAND_KEY_7Z, "");

	argv = nemo_archive_build_command (NEMO_ARCHIVE_BACKEND_7Z, options.format, &options,
					   "7z", "/tmp/out.7z", names, NULL);
	check (argv != NULL);

	if (argv != NULL) {
		check (g_strcmp0 (argv[0], "7z") == 0);
		check (has_arg (argv, "-bsp1"));
		g_strfreev (argv);
	}

	/* A line that cannot be split at all is refused outright, rather than
	   half-run: better no archive than a wrong one. */
	nemo_config_set_string (archive, NEMO_ARCHIVE_COMMAND_KEY_7Z, "7z a \"unbalanced");

	argv = nemo_archive_build_command (NEMO_ARCHIVE_BACKEND_7Z, options.format, &options,
					   "7z", "/tmp/out.7z", names, NULL);
	check (argv == NULL);

	nemo_config_reset (archive, NEMO_ARCHIVE_COMMAND_KEY_7Z);
	nemo_archive_options_clear (&options);
	g_list_free (names);
}

int
main (int argc, char *argv[])
{
	char *tmp;

	check_basics ();
	check_embedded ();
	check_values_stay_one_argument ();
	check_refusals ();
	check_passthrough ();
	check_windows_paths ();
	check_unused ();

	tmp = test_scratch_config_home ("nemo-command-template-test-XXXXXX");

	gtk_init (&argc, &argv);
	nemo_config_init ();

	check_from_config ();

	nemo_config_shutdown ();
	g_free (tmp);

	if (failures > 0) {
		g_printerr ("%d check(s) failed\n", failures);
		return EXIT_FAILURE;
	}

	g_print ("OK\n");
	return EXIT_SUCCESS;
}
