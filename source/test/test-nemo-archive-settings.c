/* The Compress dialog starts from what it was last used with. This covers the
 * two halves of that: the settings table really carries the keys the dialog
 * writes (a typo in either would just be a setting that never takes, with
 * nothing to see), and they survive a restart.
 *
 * Then the link choices: the old store links and follow links values mapped
 * over and dropped, what is kept and what never is, and the fall back to
 * Ignore agreeing with what each writer claims.
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
#include <libnemo-private/nemo-archive-host.h>
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
	/* Commented out: the dialog no longer writes these 2. They are read
	   once and dropped, which test_old_boxes covers. */
	/* { NEMO_ARCHIVE_STATE_KEY_STORE_LINKS,   TRUE  }, */
	/* { NEMO_ARCHIVE_STATE_KEY_FOLLOW_LINKS,  FALSE }, */
	{ NEMO_ARCHIVE_STATE_KEY_FOLLOW_NESTED, TRUE  },
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

static gboolean
file_mentions (const char *key)
{
	char *path = nemo_config_get_path ();
	char *text = NULL;
	gboolean found;

	nemo_config_flush ();
	if (!g_file_get_contents (path, &text, NULL, NULL)) {
		text = g_strdup ("");
	}
	found = strstr (text, key) != NULL;

	g_free (text);
	g_free (path);
	return found;
}

static int changes = 0;

static void
count_change (NemoConfigGroup *group,
	      const char      *key,
	      gpointer         data)
{
	(void) group;
	(void) key;
	(void) data;
	changes++;
}

static void
clear_link_keys (NemoConfigGroup *group)
{
	nemo_config_reset (group, NEMO_ARCHIVE_STATE_KEY_STORE_LINKS);
	nemo_config_reset (group, NEMO_ARCHIVE_STATE_KEY_FOLLOW_LINKS);
	nemo_config_reset (group, NEMO_ARCHIVE_STATE_KEY_SYMLINKS);
	nemo_config_reset (group, NEMO_ARCHIVE_STATE_KEY_JUNCTIONS);
	nemo_config_reset (group, NEMO_ARCHIVE_STATE_KEY_FOLLOW_NESTED);
}

static void
check_old_boxes (const char   *store,
		 const char   *follow,
		 ArcLinkChoice want)
{
	NemoConfigGroup *group = nemo_config_get_group (NEMO_ARCHIVE_COMMANDS_GROUP);
	ArcLinkOptions options;

	clear_link_keys (group);
	nemo_config_flush ();

	/* Written into the file, as an older release or a hand edit left it,
	   then read back when the app sees the file change. */
	if (store != NULL || follow != NULL) {
		char *path = nemo_config_get_path ();
		char *text = NULL;
		GString *more;
		gulong id;
		int spins = 0;

		check (g_file_get_contents (path, &text, NULL, NULL));
		more = g_string_new (text);
		if (store != NULL) {
			g_string_append_printf (more, "%s.%s: %s\n", NEMO_ARCHIVE_COMMANDS_GROUP,
						NEMO_ARCHIVE_STATE_KEY_STORE_LINKS,
						strcmp (store, "on") == 0 ? "true" : "false");
		}
		if (follow != NULL) {
			g_string_append_printf (more, "%s.%s: %s\n", NEMO_ARCHIVE_COMMANDS_GROUP,
						NEMO_ARCHIVE_STATE_KEY_FOLLOW_LINKS,
						strcmp (follow, "on") == 0 ? "true" : "false");
		}

		changes = 0;
		id = g_signal_connect (group, "changed", G_CALLBACK (count_change), NULL);
		check (g_file_set_contents (path, more->str, (gssize) more->len, NULL));
		while (changes == 0 && spins++ < 2500) {
			g_main_context_iteration (NULL, FALSE);
			g_usleep (2000);
		}
		check (changes > 0);
		g_signal_handler_disconnect (group, id);

		g_string_free (more, TRUE);
		g_free (text);
		g_free (path);
	}

	nemo_archive_link_options_load (&options);
	if (options.symlinks != want) {
		g_printerr ("FAIL: store links %s, follow links %s became %d, expected %d\n",
			    store != NULL ? store : "unset", follow != NULL ? follow : "unset",
			    options.symlinks, want);
		failures++;
	}
	check (!options.junctions_set);
	check (options.follow_nested);
	check (!options.follow_other);

	/* Dropped, and the mapped value is what's kept now. */
	check (!nemo_config_is_set (group, NEMO_ARCHIVE_STATE_KEY_STORE_LINKS));
	check (!nemo_config_is_set (group, NEMO_ARCHIVE_STATE_KEY_FOLLOW_LINKS));
	check (nemo_config_is_set (group, NEMO_ARCHIVE_STATE_KEY_SYMLINKS) ==
	       (want != ARC_LINK_IGNORE));

	check (!file_mentions (NEMO_ARCHIVE_STATE_KEY_STORE_LINKS));
	check (!file_mentions (NEMO_ARCHIVE_STATE_KEY_FOLLOW_LINKS));
	check (!nemo_config_is_set (group, NEMO_ARCHIVE_STATE_KEY_STORE_LINKS));
	check (nemo_config_get_enum (group, NEMO_ARCHIVE_STATE_KEY_SYMLINKS) == (gint) want);
}

/* Store links was on unless changed, so a file with only follow links in it
   was storing links. */
static void
test_old_boxes (void)
{
	check_old_boxes (NULL,  NULL,  ARC_LINK_IGNORE);
	check_old_boxes ("off", NULL,  ARC_LINK_IGNORE);
	check_old_boxes ("off", "on",  ARC_LINK_FOLLOW);
	check_old_boxes (NULL,  "on",  ARC_LINK_STORE_SYMLINK);
	check_old_boxes ("on",  "on",  ARC_LINK_STORE_SYMLINK);
	check_old_boxes ("on",  NULL,  ARC_LINK_STORE_SYMLINK);
	check_old_boxes ("on",  "off", ARC_LINK_STORE_SYMLINK);
	check_old_boxes ("off", "off", ARC_LINK_IGNORE);
}

static void
test_link_choices_kept (void)
{
	NemoConfigGroup *group = nemo_config_get_group (NEMO_ARCHIVE_COMMANDS_GROUP);
	ArcLinkOptions options;
	ArcLinkOptions back;

	clear_link_keys (group);
	check (!file_mentions ("last-symlinks"));

	/* Nothing set is the defaults, and saving them writes nothing. */
	nemo_archive_link_options_load (&options);
	check (options.symlinks == ARC_LINK_IGNORE);
	check (!options.junctions_set);
	check (options.follow_nested);
	check (!options.follow_other);
	nemo_archive_link_options_save (&options);
	check (!file_mentions ("last-symlinks"));
	check (!file_mentions ("last-junctions"));
	check (!file_mentions ("last-follow-nested"));

	/* A Junctions change by hand is kept as that, and other filesystems
	   never is. */
	arc_link_options_set_symlinks (&options, ARC_LINK_FOLLOW);
	arc_link_options_set_junctions (&options, ARC_LINK_STORE_JUNCTION);
	options.follow_nested = FALSE;
	options.follow_other = TRUE;
	nemo_archive_link_options_save (&options);
	check (file_mentions ("store-as-junctions"));
	check (!file_mentions ("last-follow-other"));

	nemo_archive_link_options_load (&back);
	check (back.symlinks == ARC_LINK_FOLLOW);
	check (back.junctions_set);
	check (back.junctions == ARC_LINK_STORE_JUNCTION);
	check (!back.follow_nested);
	check (!back.follow_other);

	/* Back to following Symlinks, by the table, once nothing is set by hand. */
	arc_link_options_init (&options);
	arc_link_options_set_symlinks (&options, ARC_LINK_STORE_SYMLINK);
	nemo_archive_link_options_save (&options);
	check (file_mentions ("store-as-symlinks"));
	check (!file_mentions ("last-junctions"));
	nemo_archive_link_options_load (&back);
	check (back.symlinks == ARC_LINK_STORE_SYMLINK);
	check (!back.junctions_set);

	clear_link_keys (group);
}

/* A store choice falls back to Ignore exactly where the writer doesn't claim
   to store links. The format's answer is the union over what's installed,
   the same as its caps. */
static void
test_fall_back_follows_claims (void)
{
	static const NemoArchiveBackend backends[] = {
		NEMO_ARCHIVE_BACKEND_LIBARCHIVE, NEMO_ARCHIVE_BACKEND_7Z, NEMO_ARCHIVE_BACKEND_RAR
	};
	ArcLinkOptions options;
	guint format;
	guint i;

	arc_link_options_init (&options);
	arc_link_options_set_symlinks (&options, ARC_LINK_STORE_SYMLINK);

	for (format = 0; format < NEMO_ARCHIVE_N_FORMATS; format++) {
		gboolean any = FALSE;

		for (i = 0; i < G_N_ELEMENTS (backends); i++) {
			gboolean claims = (nemo_archive_backend_caps (format, backends[i]) &
					   NEMO_ARCHIVE_CAP_STORE_LINKS) != 0;
			guint stores = nemo_archive_backend_link_stores (format, backends[i]);
			ArcLinkChoice got = arc_link_options_symlinks (&options, stores);

			if ((got == ARC_LINK_STORE_SYMLINK) != claims) {
				g_printerr ("FAIL: %s by writer %d stores links %s, claims %s\n",
					    nemo_archive_format_id (format), backends[i],
					    got == ARC_LINK_STORE_SYMLINK ? "yes" : "no",
					    claims ? "yes" : "no");
				failures++;
			}
			if (!claims) {
				check (stores == 0);
			}
			if (nemo_archive_backend_present (backends[i]) && claims) {
				any = TRUE;
			}
		}

		check (((nemo_archive_format_caps (format) & NEMO_ARCHIVE_CAP_STORE_LINKS) != 0) == any);
		check (((nemo_archive_format_link_stores (format) & ARC_STORES_SYMLINKS) != 0) == any);
	}

	/* The library's 7z writer claims it, so a 7z can always store links. */
	check ((nemo_archive_backend_link_stores (NEMO_ARCHIVE_FORMAT_7Z,
						  NEMO_ARCHIVE_BACKEND_LIBARCHIVE) &
		ARC_STORES_SYMLINKS) != 0);
	check ((nemo_archive_format_link_stores (NEMO_ARCHIVE_FORMAT_7Z) & ARC_STORES_SYMLINKS) != 0);

#ifdef G_OS_WIN32
	/* 7-Zip leaves links out there. */
	check (nemo_archive_backend_link_stores (NEMO_ARCHIVE_FORMAT_7Z, NEMO_ARCHIVE_BACKEND_7Z) == 0);
#endif
}

int
main (void)
{
	char *tmp;

	tmp = test_scratch_config_home ("nemo-archive-settings-XXXXXX");

	g_log_set_always_fatal (G_LOG_LEVEL_CRITICAL);
	nemo_config_init ();

	test_defaults ();
	/* Before the restart, which leaves no watch on the file. */
	test_old_boxes ();
	test_link_choices_kept ();
	test_fall_back_follows_claims ();
	test_survives_a_restart ();
	test_when_the_password_is_confirmed ();

	nemo_config_shutdown ();
	g_free (tmp);

	if (failures == 0) {
		g_print ("archive settings: all checks passed\n");
	}

	return failures == 0 ? 0 : 1;
}
