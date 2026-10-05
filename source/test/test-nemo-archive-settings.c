/* The Compress dialog starts from what it was last used with. This covers the
 * two halves of that: the settings table really carries the keys the dialog
 * writes (a typo in either would just be a setting that never takes, with
 * nothing to see), and they survive a restart.
 *
 * Also covers when the password has to be typed a second time, which is its
 * own decision in nemo-archive.c.
 *
 * Runs against a throwaway config root.
 */

#include <config.h>

#include <stdlib.h>
#include <string.h>
#include <glib.h>

#include <libnemo-private/nemo-archive.h>
#include <libnemo-private/nemo-archive-commands.h>
#include <libnemo-private/nemo-config.h>

#include "test-scratch.h"
#include "test-check.h"

/* Every key the dialog writes, with the default it should fall back to. */
static const struct {
	const char *key;
	gboolean    fallback;
} bool_keys[] = {
	{ NEMO_ARCHIVE_STATE_KEY_EACH,          FALSE },
	{ NEMO_ARCHIVE_STATE_KEY_ENCRYPT_NAMES, FALSE },
	{ NEMO_ARCHIVE_STATE_KEY_SPLIT,         FALSE },
	{ NEMO_ARCHIVE_STATE_KEY_SOLID,         FALSE },
	{ NEMO_ARCHIVE_STATE_KEY_DEDUPE,        FALSE },
	{ NEMO_ARCHIVE_STATE_KEY_STORE_LINKS,   TRUE  },
	{ NEMO_ARCHIVE_STATE_KEY_FOLLOW_LINKS,  FALSE },
	{ NEMO_ARCHIVE_STATE_KEY_RECOVERY,      TRUE  },
	{ NEMO_ARCHIVE_STATE_KEY_LOCK,          FALSE },
};

static void
test_defaults (void)
{
	NemoConfigGroup *group = nemo_config_get_group (NEMO_ARCHIVE_COMMANDS_GROUP);
	char *text;
	guint i;

	check (group != NULL);
	if (group == NULL) {
		return;
	}

	for (i = 0; i < G_N_ELEMENTS (bool_keys); i++) {
		gboolean got = nemo_config_get_boolean (group, bool_keys[i].key);

		if (got != bool_keys[i].fallback) {
			g_printerr ("FAIL: %s defaults to %s\n", bool_keys[i].key,
				    got ? "true" : "false");
			failures++;
		}
	}

	check (nemo_config_get_int (group, NEMO_ARCHIVE_STATE_KEY_LEVEL) ==
	       NEMO_ARCHIVE_LEVEL_DEFAULT);

	text = nemo_config_get_string (group, NEMO_ARCHIVE_STATE_KEY_FORMAT);
	check (g_strcmp0 (text, NEMO_ARCHIVE_STATE_DEFAULT_FORMAT) == 0);
	g_free (text);

	/* The default has to be a size the dialog's own parser accepts, or the
	   remembered volume size would silently do nothing. */
	text = nemo_config_get_string (group, NEMO_ARCHIVE_STATE_KEY_SPLIT_SIZE);
	check (g_strcmp0 (text, NEMO_ARCHIVE_STATE_DEFAULT_SPLIT_SIZE) == 0);
	if (text != NULL) {
		guint64 bytes = 0;

		check (nemo_archive_parse_size (text, &bytes));
		check (bytes > 0);
	}
	g_free (text);
}

/* Written, then read back through a fresh load of the file. */
static void
test_survives_a_restart (void)
{
	NemoConfigGroup *group = nemo_config_get_group (NEMO_ARCHIVE_COMMANDS_GROUP);
	char *text;
	guint i;

	if (group == NULL) {
		return;
	}

	for (i = 0; i < G_N_ELEMENTS (bool_keys); i++) {
		nemo_config_set_boolean (group, bool_keys[i].key, !bool_keys[i].fallback);
	}
	nemo_config_set_int (group, NEMO_ARCHIVE_STATE_KEY_LEVEL, NEMO_ARCHIVE_LEVEL_MAX);
	nemo_config_set_string (group, NEMO_ARCHIVE_STATE_KEY_FORMAT, "7z");
	nemo_config_set_string (group, NEMO_ARCHIVE_STATE_KEY_SPLIT_SIZE, "700 MiB");

	nemo_config_shutdown ();
	nemo_config_init ();

	group = nemo_config_get_group (NEMO_ARCHIVE_COMMANDS_GROUP);
	check (group != NULL);
	if (group == NULL) {
		return;
	}

	for (i = 0; i < G_N_ELEMENTS (bool_keys); i++) {
		gboolean got = nemo_config_get_boolean (group, bool_keys[i].key);

		if (got == bool_keys[i].fallback) {
			g_printerr ("FAIL: %s did not survive a restart\n", bool_keys[i].key);
			failures++;
		}
	}

	check (nemo_config_get_int (group, NEMO_ARCHIVE_STATE_KEY_LEVEL) == NEMO_ARCHIVE_LEVEL_MAX);

	text = nemo_config_get_string (group, NEMO_ARCHIVE_STATE_KEY_FORMAT);
	check (g_strcmp0 (text, "7z") == 0);
	g_free (text);

	text = nemo_config_get_string (group, NEMO_ARCHIVE_STATE_KEY_SPLIT_SIZE);
	check (g_strcmp0 (text, "700 MiB") == 0);
	g_free (text);
}

static void
check_confirm (gboolean delete_sources,
	       const char *password,
	       gboolean want)
{
	NemoArchiveOptions options;
	gboolean got;

	nemo_archive_options_init (&options);
	options.delete_sources = delete_sources;
	options.password = password != NULL ? g_strdup (password) : NULL;

	got = nemo_archive_should_confirm_password (&options);
	if (got != want) {
		g_printerr ("FAIL: delete=%s password=%s asked=%s, expected %s\n",
			    delete_sources ? "yes" : "no",
			    password != NULL ? password : "(none)",
			    got ? "yes" : "no", want ? "yes" : "no");
		failures++;
	}

	nemo_archive_options_clear (&options);
}

/* Only the two together. Either on its own leaves a way back: an archive with
   a mistyped password is still openable-by-nobody but the files are still
   there, and a delete with no password is a delete of files that went into an
   archive anyone can open. */
static void
test_when_the_password_is_confirmed (void)
{
	check_confirm (FALSE, NULL,     FALSE);
	check_confirm (FALSE, "hunter", FALSE);
	check_confirm (TRUE,  NULL,     FALSE);
	check_confirm (TRUE,  "",       FALSE);
	check_confirm (TRUE,  "hunter", TRUE);
}

int
main (void)
{
	char *tmp;

	tmp = test_scratch_config_home ("nemo-archive-settings-XXXXXX");

	g_log_set_always_fatal (G_LOG_LEVEL_CRITICAL);
	nemo_config_init ();

	test_defaults ();
	test_survives_a_restart ();
	test_when_the_password_is_confirmed ();

	nemo_config_shutdown ();
	g_free (tmp);

	if (failures == 0) {
		g_print ("archive settings: all checks passed\n");
	}

	return failures == 0 ? 0 : 1;
}
