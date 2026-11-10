/* Which of the 3 ways in wins. The define only ever arms: the variable and
 * the setting can turn a build with it at 0 on, and neither can turn a build
 * with it at 1 off. Which half of that is checked depends on how this build was
 * compiled, so both halves are here.
 *
 * Each case runs as a child of this binary, since the environment variable is
 * read once per process and a second read in the same one would see the first
 * value. The child exits 0 armed, 1 quiet, so the parent only reads a status.
 *
 * Also checks the 2 dialogs that name a pair of paths, since the 2 the
 * wrong way round would read as the opposite of what is happening, and the
 * size caps that keep a long list from pushing the buttons off the screen.
 *
 * Runs against a throwaway config root. */

#include <config.h>

#include <stdlib.h>
#include <string.h>
#include <glib.h>

#include <libnemo-private/nemo-config.h>
#include <libnemo-private/nemo-delete-testguard.h>
#include <libnemo-private/nemo-global-preferences.h>

#include "test-scratch.h"
#include "test-check.h"

#define CHILD_ARMED  0
#define CHILD_QUIET  1
#define CHILD_BROKEN 2

/* "store" writes the setting and stops; "read" reports what arming decided,
   and "early" does the same before the settings file has been read. */
static int
run_as_child (const char *mode)
{
	NemoConfigGroup *debug;

	if (strcmp (mode, "early") == 0) {
		return nemo_delete_testguard_armed () ? CHILD_ARMED : CHILD_QUIET;
	}

	nemo_config_init ();

	if (strcmp (mode, "read") == 0) {
		int armed = nemo_delete_testguard_armed () ? CHILD_ARMED : CHILD_QUIET;

		nemo_config_shutdown ();
		return armed;
	}

	debug = nemo_config_get_group (NEMO_DEBUG_GROUP);
	if (debug == NULL) {
		return CHILD_BROKEN;
	}

	nemo_config_set_boolean (debug, NEMO_PREFERENCES_TESTGUARD_ALL_DELETES,
				 strcmp (mode, "store-on") == 0);
	nemo_config_flush ();
	nemo_config_shutdown ();

	return CHILD_ARMED;
}

/* `env_value` NULL leaves the variable unset. */
static int
spawn_child (const char *self, const char *mode, const char *env_value)
{
	char     *argv[] = { (char *) self, (char *) mode, NULL };
	GError   *error = NULL;
	int       status = -1;

	if (env_value != NULL) {
		g_setenv (NEMO_TESTGUARD_ENV_VAR, env_value, TRUE);
	} else {
		g_unsetenv (NEMO_TESTGUARD_ENV_VAR);
	}

	if (!g_spawn_sync (NULL, argv, NULL, G_SPAWN_DEFAULT,
			   NULL, NULL, NULL, NULL, &status, &error)) {
		g_printerr ("could not run %s %s: %s\n", self, mode, error->message);
		g_error_free (error);
		return -1;
	}

	/* A non-zero exit is the result here, not a failure, and this is the
	   portable way back to the number the child returned. */
	if (!g_spawn_check_exit_status (status, &error)) {
		int code = error->domain == G_SPAWN_EXIT_ERROR ? error->code : -1;

		g_error_free (error);
		return code;
	}

	return CHILD_ARMED;
}

/* A move and an overwrite each lose one of 2 paths, and the dialog is only
   any use if it says which. Checked here rather than through the dialog, which
   wants a display the gate doesn't have. The paths are looked for as the
   dialog writes them, which on Windows is with backslashes. */
static void
check_wording (void)
{
	GFile *a = g_file_new_for_path ("/tmp/nemo-guard-src/a");
	GFile *b = g_file_new_for_path ("/tmp/nemo-guard-src/b");
	GFile *dest_dir = g_file_new_for_path ("/tmp/nemo-guard-dest");
	GFile *target = g_file_new_for_path ("/tmp/nemo-guard-dest/a");
	char *a_name = g_file_get_parse_name (a);
	char *b_name = g_file_get_parse_name (b);
	char *dest_name = g_file_get_parse_name (dest_dir);
	char *target_name = g_file_get_parse_name (target);
	GList *files = NULL;
	char *text;

	files = g_list_append (files, a);
	files = g_list_append (files, b);

	text = nemo_delete_testguard_describe_move (files, dest_dir);
	check (strstr (text, dest_name) != NULL);
	check (strstr (text, a_name) != NULL);
	check (strstr (text, b_name) != NULL);
	/* The destination is named above the sources, not among them. */
	check (strstr (text, dest_name) < strstr (text, a_name));
	g_free (text);

	text = nemo_delete_testguard_describe_overwrite (a, target);
	check (strstr (text, "Overwritten") != NULL);
	check (strstr (text, "Replaced by") != NULL);
	/* Both names have to be there before anywhere they sit means anything: a
	   missing one is NULL, and NULL sorts below every real position, so the
	   ordering checks below would pass on exactly the text they guard against. */
	check (strstr (text, target_name) != NULL);
	check (strstr (text, a_name) != NULL);
	/* The one that is lost is named first, under its own heading. */
	check (strstr (text, target_name) < strstr (text, "Replaced by"));
	check (strstr (text, "Replaced by") < strstr (text, a_name));
	g_free (text);

	g_list_free (files);
	g_free (a_name);
	g_free (b_name);
	g_free (dest_name);
	g_free (target_name);
	g_object_unref (a);
	g_object_unref (b);
	g_object_unref (dest_dir);
	g_object_unref (target);
}

/* A quarter of the long side and half of the short one, whichever way round
   the screen is. */
static void
check_caps (void)
{
	int width, height;

	nemo_delete_testguard_dialog_caps (1920, 1080, &width, &height);
	check (width == 480 && height == 540);

	nemo_delete_testguard_dialog_caps (1080, 1920, &width, &height);
	check (width == 540 && height == 480);

	nemo_delete_testguard_dialog_caps (1000, 1000, &width, &height);
	check (width == 250 && height == 500);
}

int
main (int argc, char *argv[])
{
	char *store_on, *store_off;

	if (argc > 1) {
		return run_as_child (argv[1]);
	}

	check_wording ();
	check_caps ();

	store_on  = test_scratch_dir ("nemo-testguard-on-XXXXXX", NULL);
	store_off = test_scratch_dir ("nemo-testguard-off-XXXXXX", NULL);

	if (NEMO_TESTGUARD_ALL_DELETES) {
		/* The define only ever arms. Neither of the other 2 can take it
		   back, and off is all either of them has to say. */
		test_scratch_point_config_at (store_off);
		check (spawn_child (argv[0], "read", NULL) == CHILD_ARMED);
		check (spawn_child (argv[0], "read", "0") == CHILD_ARMED);
		check (spawn_child (argv[0], "read", "off") == CHILD_ARMED);

		test_scratch_point_config_at (store_on);
		check (spawn_child (argv[0], "store-on", NULL) == CHILD_ARMED);
		check (spawn_child (argv[0], "read", "0") == CHILD_ARMED);
	} else {
		/* Nothing set anywhere. Quiet until 20260923, when the setting
		   became on by default:
		check (spawn_child (argv[0], "read", NULL) == CHILD_QUIET);
		*/
		test_scratch_point_config_at (store_off);
		check (spawn_child (argv[0], "read", NULL) == CHILD_ARMED);

		/* Before the settings file is read, the default applies, and the
		   variable still beats it. */
		check (spawn_child (argv[0], "early", NULL) == CHILD_ARMED);
		check (spawn_child (argv[0], "early", "0") == CHILD_QUIET);

		/* The setting on its own. */
		test_scratch_point_config_at (store_on);
		check (spawn_child (argv[0], "store-on", NULL) == CHILD_ARMED);
		check (spawn_child (argv[0], "read", NULL) == CHILD_ARMED);

		/* The variable beats the setting in both directions. */
		check (spawn_child (argv[0], "read", "0") == CHILD_QUIET);
		check (spawn_child (argv[0], "read", "off") == CHILD_QUIET);

		test_scratch_point_config_at (store_off);
		check (spawn_child (argv[0], "store-off", NULL) == CHILD_ARMED);
		check (spawn_child (argv[0], "read", "1") == CHILD_ARMED);
		check (spawn_child (argv[0], "read", "YES") == CHILD_ARMED);

		/* A value that is neither is ignored, so the setting still applies. */
		check (spawn_child (argv[0], "read", "maybe") == CHILD_QUIET);
		test_scratch_point_config_at (store_on);
		check (spawn_child (argv[0], "read", "maybe") == CHILD_ARMED);
	}

	g_free (store_on);
	g_free (store_off);

	if (failures == 0)
		g_print ("nemo-delete-testguard: arming order, move/overwrite wording and size caps hold\n");

	return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
