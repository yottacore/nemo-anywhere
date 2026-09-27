/* File associations on Windows: the override map in the settings file, the
 * registry read the way the shell reads it, the %1 substitution, and the name
 * a program is shown under. Nothing is launched. */

#include <config.h>

#include <string.h>
#include <gtk/gtk.h>
#include <libnemo-private/nemo-associations-win32.h>
#include <libnemo-private/nemo-global-preferences.h>

#include "test-scratch.h"

static int failures;

static void
check (gboolean ok, const char *what)
{
	g_print ("%s: %s\n", ok ? "PASS" : "FAIL", what);
	if (!ok) {
		failures++;
	}
}

static void
check_str (const char *what, const char *got, const char *expected)
{
	check (g_strcmp0 (got, expected) == 0, what);
	if (g_strcmp0 (got, expected) != 0) {
		g_print ("     got: %s\n  wanted: %s\n", got ? got : "(null)", expected ? expected : "(null)");
	}
}

static void
test_substitution (void)
{
	char *line;

	line = nemo_associations_win32_command_for_file ("\"C:\\App\\app.exe\" \"%1\"", "C:\\Docs\\a b.txt");
	check_str ("quoted %1 takes the path", line, "\"C:\\App\\app.exe\" \"C:\\Docs\\a b.txt\"");
	g_free (line);

	line = nemo_associations_win32_command_for_file ("C:\\App\\app.exe %1", "C:\\Docs\\a b.txt");
	check_str ("bare %1 is quoted", line, "C:\\App\\app.exe \"C:\\Docs\\a b.txt\"");
	g_free (line);

	line = nemo_associations_win32_command_for_file ("\"C:\\App\\app.exe\" -pt \"%2\" \"%1\"", "C:\\x.doc");
	check_str ("%2 and its quotes fall away", line, "\"C:\\App\\app.exe\" -pt \"C:\\x.doc\"");
	g_free (line);

	line = nemo_associations_win32_command_for_file ("\"C:\\App\\app.exe\"", "C:\\x.doc");
	check_str ("no placeholder: the file is appended", line, "\"C:\\App\\app.exe\" \"C:\\x.doc\"");
	g_free (line);

	line = nemo_associations_win32_command_for_file ("app.exe --rate=100%% %L", "C:\\x.doc");
	check_str ("%% and %L", line, "app.exe --rate=100% \"C:\\x.doc\"");
	g_free (line);
}

static void
test_overrides (void)
{
	char *command;
	GAppInfo *app;

	nemo_associations_win32_set_override (".zzz", NULL);
	command = nemo_associations_win32_get_override (".zzz");
	check (command == NULL, "no override to begin with");

	nemo_associations_win32_set_override (".zzz", "\"C:\\Windows\\notepad.exe\" \"%1\"");
	nemo_associations_win32_set_override (".yyy", "\"C:\\Windows\\write.exe\" \"%1\"");
	command = nemo_associations_win32_get_override (".ZZZ");
	check_str ("an override is found, whatever the case", command, "\"C:\\Windows\\notepad.exe\" \"%1\"");
	g_free (command);

	app = nemo_associations_win32_default_for_type (".zzz");
	check (app != NULL, "the override answers as the default");
	if (app != NULL) {
		check_str ("and carries its command", nemo_associations_win32_command_of (app), "\"C:\\Windows\\notepad.exe\" \"%1\"");
		check_str ("under the program's own name", g_app_info_get_display_name (app), "Notepad");
		g_object_unref (app);
	}

	nemo_associations_win32_set_override (".zzz", "\"C:\\Windows\\write.exe\" \"%1\"");
	command = nemo_associations_win32_get_override (".zzz");
	check_str ("setting again replaces", command, "\"C:\\Windows\\write.exe\" \"%1\"");
	g_free (command);

	nemo_associations_win32_set_override (".zzz", NULL);
	command = nemo_associations_win32_get_override (".zzz");
	check (command == NULL, "and NULL removes");
	command = nemo_associations_win32_get_override (".yyy");
	check (command != NULL, "leaving the others alone");
	g_free (command);
	nemo_associations_win32_set_override (".yyy", NULL);

	check (nemo_associations_win32_default_for_type ("text/plain") == NULL, "a mime type is not answered");
}

static void
test_registry (void)
{
	char *name = NULL;
	char *command = nemo_associations_win32_registry_command (".txt", &name);
	GAppInfo *app;

	check (command != NULL, ".txt has a program in the registry");
	g_print ("      .txt -> %s [%s]\n", name ? name : "(no name)", command ? command : "");
	check (command == NULL || strstr (command, "%2") == NULL, "and it is the open verb, not a print one");
	g_free (command);
	g_free (name);

	command = nemo_associations_win32_registry_command (".no-such-type-here", NULL);
	check (command == NULL, "an unknown type has none");

	{
		const char *types[] = { ".docx", ".pdf", ".png", ".html", ".zip", NULL };
		int i;

		for (i = 0; types[i] != NULL; i++) {
			name = NULL;
			command = nemo_associations_win32_registry_command (types[i], &name);
			g_print ("      %s -> %s [%s]\n", types[i], name ? name : "(no name)", command ? command : "(none)");
			g_free (command);
			g_free (name);
		}
	}

	app = nemo_associations_win32_default_for_type (".txt");
	check (app != NULL && nemo_associations_win32_command_of (app) != NULL, "the default for .txt is ours to launch");
	g_clear_object (&app);
}

/* The Open With list is GIO's, and every entry on it has to be startable the
 * same way ours are, or the program comes up as a child of nemo. */
static void
test_command_for_app (void)
{
	GAppInfo *app = g_app_info_create_from_commandline ("\"C:\\Windows\\notepad.exe\" \"%1\"",
							    NULL, G_APP_INFO_CREATE_NONE, NULL);
	GList *apps, *l;
	gboolean any = FALSE;

	check (app != NULL, "an app info can be made from a command line");
	if (app != NULL) {
		check (nemo_associations_win32_command_of (app) == NULL, "it is not one of ours");
		check (nemo_associations_win32_command_for_app (app) != NULL,
		       "and it still answers with a command line");
		g_object_unref (app);
	}

	apps = g_app_info_get_all_for_type (".txt");
	for (l = apps; l != NULL; l = l->next) {
		/* A store app has no executable and no command line; it is the one
		   kind that has to stay with GIO. */
		if (g_app_info_get_executable (l->data) == NULL) {
			continue;
		}

		any = TRUE;
		check (nemo_associations_win32_command_for_app (l->data) != NULL,
		       g_app_info_get_name (l->data));
	}
	g_list_free_full (apps, g_object_unref);

	if (!any) {
		g_print ("note: nothing ordinary is registered for .txt here\n");
	}
}

/* Windows will not let a program set the per-user default, so "Set as
 * default" keeps its own in the settings file. The choice has to read back as
 * the default for that type, carrying the command it was made from. */
static void
test_set_default (void)
{
	GAppInfo *app, *back;

	app = g_app_info_create_from_commandline ("\"C:\\Windows\\notepad.exe\" \"%1\"",
						  NULL, G_APP_INFO_CREATE_NONE, NULL);
	check (app != NULL, "an app info can be made from a command line");
	if (app == NULL) {
		return;
	}
	g_print ("      GIO spells it: %s\n", g_app_info_get_commandline (app));

	check (!nemo_associations_win32_set_default (app, "text/plain"),
	       "a mime type is refused, since nothing is looked up by one");

	check (nemo_associations_win32_set_default (app, ".qqq"), "set as default for .qqq");
	back = nemo_associations_win32_default_for_type (".qqq");
	check (back != NULL, "and it reads back as the default");
	if (back != NULL) {
		const char *command = nemo_associations_win32_command_of (back);

		check (command != NULL && strstr (command, "notepad.exe") != NULL &&
		       strstr (command, "%1") != NULL,
		       "carrying the program and where the file goes");
		g_print ("      .qqq -> %s\n", command ? command : "(null)");
		g_object_unref (back);
	}
	g_object_unref (app);

	/* No placeholder in what GIO holds: the file still has to be handed over. */
	app = g_app_info_create_from_commandline ("\"C:\\Windows\\write.exe\"",
						  NULL, G_APP_INFO_CREATE_NONE, NULL);
	if (app != NULL) {
		check (nemo_associations_win32_set_default (app, ".qqq"), "set again, with no placeholder");
		back = nemo_associations_win32_default_for_type (".qqq");
		check (back != NULL && nemo_associations_win32_command_of (back) != NULL &&
		       strstr (nemo_associations_win32_command_of (back), "write.exe") != NULL &&
		       strstr (nemo_associations_win32_command_of (back), "%1") != NULL,
		       "the new choice replaces the old, with a place for the file");
		g_clear_object (&back);
		g_object_unref (app);
	}

	nemo_associations_win32_set_override (".qqq", NULL);
}

static void
test_names (void)
{
	char *name;

	name = nemo_associations_win32_friendly_name ("\"C:\\Windows\\notepad.exe\" \"%1\"");
	check_str ("a program's description", name, "Notepad");
	g_free (name);

	name = nemo_associations_win32_friendly_name ("C:\\Windows\\System32\\notepad.exe %1");
	check_str ("found behind an unquoted path too", name, "Notepad");
	g_free (name);

	name = nemo_associations_win32_friendly_name ("\"C:\\nowhere\\my tool.exe\" \"%1\"");
	check_str ("a missing program is named by its file", name, "my tool");
	g_free (name);

	/* GIO would call this one "notepad.exe", which is what the Open With
	   submenu used to read. */
	{
		GAppInfo *app = g_app_info_create_from_commandline ("\"C:\\Windows\\notepad.exe\" \"%1\"",
								    NULL, G_APP_INFO_CREATE_NONE, NULL);

		if (app != NULL) {
			name = nemo_associations_win32_name_for_app (app);
			check_str ("a GIO entry is named by the program too", name, "Notepad");
			g_free (name);
			g_object_unref (app);
		}
	}
}

int
main (int argc, char *argv[])
{
	char *scratch = test_scratch_config_home ("nemo-assoc-home-XXXXXX");


	gtk_init_check (&argc, &argv);
	nemo_global_preferences_init ();

	test_substitution ();
	test_overrides ();
	test_registry ();
	test_command_for_app ();
	test_set_default ();
	test_names ();

	g_free (scratch);
	return failures == 0 ? 0 : 1;
}
