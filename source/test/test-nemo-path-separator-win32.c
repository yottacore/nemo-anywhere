/* Windows takes either separator, so which one is shown is a setting and a
 * typed forward slash can be refused. Checks both, plus the live reaction to a
 * setting change. Runs against a throwaway config root. */

#include <config.h>

#include <stdlib.h>
#include <string.h>
#include <gio/gio.h>
#include <gtk/gtk.h>

#include <libnemo-private/nemo-config.h>
#include <libnemo-private/nemo-file-utilities.h>
#include <libnemo-private/nemo-global-preferences.h>

#include "test-scratch.h"
#include "test-check.h"

static void
check_applied (const char *before, const char *want)
{
	char *got = g_strdup (before);

	nemo_path_apply_display_separator (got);

	if (g_strcmp0 (got, want) != 0) {
		g_printerr ("FAIL %s -> %s, wanted %s\n", before, got, want);
		failures++;
	}

	g_free (got);
}

static void
test_backslash_is_the_default (void)
{
	check (nemo_path_get_display_separator () == '\\');

	check_applied ("C:/Users/someone", "C:\\Users\\someone");
	check_applied ("C:\\Users\\someone", "C:\\Users\\someone");

	/* A name with no separator at all is left alone. */
	check_applied ("readme.txt", "readme.txt");
}

static void
test_slash_can_be_chosen (void)
{
	nemo_config_set_string (nemo_windows_preferences, NEMO_PREFERENCES_PATH_SEPARATOR, "slash");

	check (nemo_path_get_display_separator () == '/');
	check_applied ("C:\\Users\\someone", "C:/Users/someone");

	/* A UNC path keeps its shape - both leading separators convert together. */
	check_applied ("\\\\server\\share\\file", "//server/share/file");

	nemo_config_set_string (nemo_windows_preferences, NEMO_PREFERENCES_PATH_SEPARATOR, "backslash");
	check (nemo_path_get_display_separator () == '\\');
}

static void
test_slash_input_can_be_refused (void)
{
	/* Allowed by default. */
	check (nemo_path_input_is_allowed ("C:/Users/someone"));

	nemo_config_set_boolean (nemo_windows_preferences, NEMO_PREFERENCES_ALLOW_SLASH_INPUT, FALSE);

	check (!nemo_path_input_is_allowed ("C:/Users/someone"));

	/* A backslash is always a separator, so this one still goes through. */
	check (nemo_path_input_is_allowed ("C:\\Users\\someone"));

	/* A uri is all slashes by definition and has nothing to do with the setting. */
	check (nemo_path_input_is_allowed ("smb://server/share"));
	check (nemo_path_input_is_allowed ("network:///"));

	nemo_config_set_boolean (nemo_windows_preferences, NEMO_PREFERENCES_ALLOW_SLASH_INPUT, TRUE);
	check (nemo_path_input_is_allowed ("C:/Users/someone"));
}

static void
late_handler (gpointer data, G_GNUC_UNUSED const char *key)
{
	char *seen = data;

	*seen = nemo_path_get_display_separator ();
}

/* Everything that spells out a path does it from its own handler on this
 * setting, and GObject runs handlers in the order they were connected. So the
 * separator has to be refreshed by the time a handler connected later runs, or
 * the window ends up a step behind - which is what the breadcrumb did. */
static void
test_separator_is_fresh_for_later_handlers (void)
{
	char seen = '\0';
	gulong id;

	nemo_config_set_string (nemo_windows_preferences, NEMO_PREFERENCES_PATH_SEPARATOR, "backslash");

	id = g_signal_connect_swapped (nemo_windows_preferences,
				       "changed::" NEMO_PREFERENCES_PATH_SEPARATOR,
				       G_CALLBACK (late_handler), &seen);

	nemo_config_set_string (nemo_windows_preferences, NEMO_PREFERENCES_PATH_SEPARATOR, "slash");
	check (seen == '/');

	nemo_config_set_string (nemo_windows_preferences, NEMO_PREFERENCES_PATH_SEPARATOR, "backslash");
	check (seen == '\\');

	g_signal_handler_disconnect (nemo_windows_preferences, id);
}

int
main (int argc, char *argv[])
{
	char *tmp;

	tmp = test_scratch_config_home ("nemo-separator-test-XXXXXX");

	gtk_init (&argc, &argv);

	nemo_global_preferences_init ();

	/* First, and it has to stay first: any other check here would prime the
	   separator cache and hide the ordering this one is about. */
	test_separator_is_fresh_for_later_handlers ();

	test_backslash_is_the_default ();
	test_slash_can_be_chosen ();
	test_slash_input_can_be_refused ();

	g_free (tmp);

	if (failures > 0) {
		g_printerr ("%d failure(s)\n", failures);
		return EXIT_FAILURE;
	}

	g_print ("path separator: all checks passed\n");
	return EXIT_SUCCESS;
}
