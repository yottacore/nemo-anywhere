/* Windows keeps hidden files behind an attribute and treats a leading dot as an
 * ordinary character, so dot-files get a switch of their own there. This checks
 * the one decision point every filter goes through. Runs against a throwaway
 * config root. */

#include <config.h>

#include <stdlib.h>
#include <gio/gio.h>
#include <gtk/gtk.h>

#include <libnemo-private/nemo-config.h>
#include <libnemo-private/nemo-file.h>
#include <libnemo-private/nemo-global-preferences.h>

#include "test-scratch.h"
#include "test-check.h"

static void
test_hidden_by_default (void)
{
	check (nemo_file_name_is_hidden_dot_file (".bashrc"));
	check (nemo_file_name_is_hidden_dot_file (".config"));

	check (!nemo_file_name_is_hidden_dot_file ("readme.txt"));
	check (!nemo_file_name_is_hidden_dot_file ("archive.tar.gz"));
	check (!nemo_file_name_is_hidden_dot_file (NULL));

	/* A name that only contains a dot elsewhere is an ordinary file. */
	check (!nemo_file_name_is_hidden_dot_file ("v1.2.3"));
}

static void
test_switch_reveals_them (void)
{
	nemo_config_set_boolean (nemo_windows_preferences, NEMO_PREFERENCES_SHOW_DOT_FILES, TRUE);

	check (!nemo_file_name_is_hidden_dot_file (".bashrc"));
	check (!nemo_file_name_is_hidden_dot_file ("readme.txt"));

	nemo_config_set_boolean (nemo_windows_preferences, NEMO_PREFERENCES_SHOW_DOT_FILES, FALSE);

	check (nemo_file_name_is_hidden_dot_file (".bashrc"));
}

/* The two switches are independent: revealing attribute-hidden files must not
 * also reveal dot-files. */
static void
test_independent_of_show_hidden (void)
{
	nemo_config_set_boolean (nemo_preferences, NEMO_PREFERENCES_SHOW_HIDDEN_FILES, TRUE);

	check (nemo_file_name_is_hidden_dot_file (".bashrc"));

	nemo_config_set_boolean (nemo_preferences, NEMO_PREFERENCES_SHOW_HIDDEN_FILES, FALSE);
}

static void
set_apart (gboolean hidden, gboolean dots)
{
	nemo_config_set_boolean (nemo_preferences, NEMO_PREFERENCES_SHOW_HIDDEN_FILES, hidden);
	nemo_config_set_boolean (nemo_windows_preferences, NEMO_PREFERENCES_SHOW_DOT_FILES, dots);
}

static gboolean
hidden_shown (void)
{
	return nemo_config_get_boolean (nemo_preferences, NEMO_PREFERENCES_SHOW_HIDDEN_FILES);
}

static gboolean
dots_shown (void)
{
	return nemo_config_get_boolean (nemo_windows_preferences, NEMO_PREFERENCES_SHOW_DOT_FILES);
}

/* Ctrl+H: out of step, the attribute switch's new value wins and the dot-file
 * one comes with it, so one keystroke shows everything that was out of sight. */
static void
test_ctrl_h_brings_them_together (void)
{
	/* Attribute shown, dots hidden: Ctrl+H turns the attribute off. */
	set_apart (TRUE, FALSE);
	nemo_global_preferences_set_show_all_hidden (!hidden_shown ());
	check (!hidden_shown ());
	check (!dots_shown ());
	check (nemo_file_name_is_hidden_dot_file (".bashrc"));

	/* Attribute hidden, dots shown: Ctrl+H turns both on. */
	set_apart (FALSE, TRUE);
	nemo_global_preferences_set_show_all_hidden (!hidden_shown ());
	check (hidden_shown ());
	check (dots_shown ());
	check (!nemo_file_name_is_hidden_dot_file (".bashrc"));

	/* In step, they stay in step. */
	nemo_global_preferences_set_show_all_hidden (!hidden_shown ());
	check (!hidden_shown ());
	check (!dots_shown ());

	set_apart (FALSE, FALSE);
}

int
main (int argc, char *argv[])
{
	char *tmp;

	tmp = test_scratch_config_home ("nemo-dot-files-test-XXXXXX");

	gtk_init (&argc, &argv);

	nemo_global_preferences_init ();

	test_hidden_by_default ();
	test_switch_reveals_them ();
	test_independent_of_show_hidden ();
	test_ctrl_h_brings_them_together ();

	g_free (tmp);

	if (failures > 0) {
		g_printerr ("%d failure(s)\n", failures);
		return EXIT_FAILURE;
	}

	g_print ("dot-file switch: all checks passed\n");
	return EXIT_SUCCESS;
}
