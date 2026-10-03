/* The settings file carries a commented list of everything not set, so that
 * opening it shows what there is to change. Checks that the list is written,
 * that it leaves out both the keys the app writes back itself and the keys
 * already set, that uncommenting a line takes effect, and that a save is a
 * fixed point rather than growing a second copy each time. The same goes for
 * SHCL's info block, which names the format and goes last.
 *
 * Runs against a throwaway config root. */

#include <config.h>

#include <stdlib.h>
#include <string.h>
#include <gio/gio.h>
#include <glib/gstdio.h>

#include <libnemo-private/nemo-config.h>
#include <libnemo-private/nemo-global-preferences.h>

#include "test-scratch.h"
#include "test-check.h"

static char *
read_file (void)
{
	char *path = nemo_config_get_path ();
	char *text = NULL;

	if (!g_file_get_contents (path, &text, NULL, NULL)) {
		text = NULL;
	}
	g_free (path);

	return text != NULL ? text : g_strdup ("");
}

static int
count_of (const char *text, const char *needle)
{
	const char *at = text;
	int         seen = 0;

	while ((at = strstr (at, needle)) != NULL) {
		seen++;
		at++;
	}

	return seen;
}

/* Everything up to the list, which names every key there is. */
static char *
read_settings (void)
{
	char *text = read_file ();
	char *rule = strstr (text, "\n# ------");

	if (rule != NULL) {
		*rule = '\0';
	}

	return text;
}

static void
test_list_written (NemoConfigGroup *prefs)
{
	char *text;

	/* Any change at all, so there is something to save. */
	nemo_config_set_boolean (prefs, "show-hidden-files", TRUE);
	nemo_config_flush ();

	text = read_file ();
	check (strstr (text, "# Uncomment a line to change one.") != NULL);
	check (strstr (text, "\n#preferences.click-policy: double\n") != NULL);
	check (strstr (text, "\n# One click or two to open a file\n"
	                     "#preferences.click-policy: double\n") != NULL);

	/* A list default, spelled the way the writer spells one. */
	check (strstr (text, "\n#windows.terminal-candidates: wt.exe, pwsh.exe,"
	                     " powershell.exe, cmd.exe\n") != NULL);
	/* An empty list is a bare key. */
	check (strstr (text, "\n#plugins.disabled-extensions:\n") != NULL);
	/* A group nested two deep keeps its whole path. */
	check (strstr (text, "\n#preferences.menu-config.selection-menu-copy: true\n") != NULL);
	g_free (text);
}

/* A window size or the last state of a toggle is written back by the app, so
 * listing it would only invite setting something that gets overwritten. */
static void
test_remembered_keys_left_out (void)
{
	char *text = read_file ();

	check (strstr (text, "#window-state.geometry") == NULL);
	check (strstr (text, "#window-state.sidebar-width") == NULL);
	check (strstr (text, "#search.search-sort-column") == NULL);
	check (strstr (text, "#state.first-run-done") == NULL);
	/* But a real preference in the same group is there. */
	check (strstr (text, "\n#window-state.start-with-menu-bar: true\n") != NULL);
	g_free (text);
}

/* Two bindings of one key read as ambiguous and fall back to the default, so
 * a key that is set must not also be offered in the list. */
static void
test_set_key_drops_out (NemoConfigGroup *prefs)
{
	char *text;

	nemo_config_set_enum (prefs, "click-policy", NEMO_CLICK_POLICY_SINGLE);
	nemo_config_flush ();

	text = read_file ();
	check (strstr (text, "#preferences.click-policy: ") == NULL);
	check (strstr (text, "click-policy: single") != NULL);
	g_free (text);

	nemo_config_reset (prefs, "click-policy");
	nemo_config_flush ();

	text = read_file ();
	check (strstr (text, "\n#preferences.click-policy: double\n") != NULL);
	g_free (text);
}

/* Saving twice over the same settings must write the same bytes. The list
 * comes back through the parser as ordinary comments, so a save that did not
 * take the old copy off would stack another one on every write. */
static void
test_save_is_a_fixed_point (NemoConfigGroup *prefs)
{
	char *first, *second;

	nemo_config_set_int (prefs, "tab-width-max-percent", 30);
	nemo_config_flush ();
	first = read_file ();

	nemo_config_set_int (prefs, "tab-width-max-percent", 31);
	nemo_config_set_int (prefs, "tab-width-max-percent", 30);
	nemo_config_flush ();
	second = read_file ();

	check (g_strcmp0 (first, second) == 0);
	check (count_of (second, "# Uncomment a line to change one.") == 1);
	check (count_of (second, "\n##    Format   3\n") == 1);
	check (g_str_has_suffix (second, "No warranty.\n##\n"));
	check (strstr (second, "# Uncomment") < strstr (second, "##    Format"));
	g_free (first);
	g_free (second);

	nemo_config_reset (prefs, "tab-width-max-percent");
	nemo_config_flush ();
}

static int changed_count;

static void
on_changed (NemoConfigGroup *group, const char *key, gpointer data)
{
	changed_count++;
}

/* Put text on disk the way an editor would and wait for the store to notice.
 * The monitor is async, so the wait is on the change actually arriving. */
static void
write_and_wait (const char *text, NemoConfigGroup *group, const char *key)
{
	char   *path   = nemo_config_get_path ();
	char   *detail = g_strdup_printf ("changed::%s", key);
	gulong  id     = g_signal_connect (group, detail, G_CALLBACK (on_changed), NULL);
	int     spins  = 0;

	changed_count = 0;
	check (g_file_set_contents (path, text, -1, NULL));

	while (changed_count == 0 && spins++ < 200) {
		g_main_context_iteration (NULL, FALSE);
		g_usleep (10000);
	}
	check (changed_count >= 1);

	g_signal_handler_disconnect (group, id);
	g_free (detail);
	g_free (path);
}

/* Uncomment a line and it is set - the whole point of the list. The path is
 * dotted and the group may be nested, which is the part worth proving. */
static void
test_uncommenting_takes_effect (NemoConfigGroup *prefs, NemoConfigGroup *menus,
                                NemoConfigGroup *appearance)
{
	char  *text = read_file ();
	char **lines = g_strsplit (text, "\n", -1);
	char  *edited;
	int    i;

	g_free (text);
	for (i = 0; lines[i] != NULL; i++) {
		if (g_strcmp0 (lines[i], "#preferences.click-policy: double") == 0) {
			g_free (lines[i]);
			lines[i] = g_strdup ("preferences.click-policy: single");
		} else if (g_strcmp0 (lines[i],
		                      "#preferences.menu-config.selection-menu-copy: true") == 0) {
			g_free (lines[i]);
			lines[i] = g_strdup ("preferences.menu-config.selection-menu-copy: false");
		} else if (g_strcmp0 (lines[i], "#appearance.mode: system") == 0) {
			/* Nothing in this group is set, so the binding opens a group of
			   its own at the end and SHCL hands back the rest of the list
			   indented underneath it. */
			g_free (lines[i]);
			lines[i] = g_strdup ("appearance.mode: dark");
		}
	}
	edited = g_strjoinv ("\n", lines);
	g_strfreev (lines);

	write_and_wait (edited, prefs, "click-policy");
	g_free (edited);

	check (nemo_config_get_enum (prefs, "click-policy") == NEMO_CLICK_POLICY_SINGLE);
	check (nemo_config_get_boolean (menus, "selection-menu-copy") == FALSE);
	{
		char *mode = nemo_config_get_string (appearance, "mode");

		check (g_strcmp0 (mode, "dark") == 0);
		g_free (mode);
	}

	/* Both are settings now, so the next save keeps them where the settings
	   are and stops offering them - with one list, not a second stacked on by
	   writing back what the parser handed over. */
	nemo_config_set_boolean (prefs, "show-hidden-files", FALSE);
	nemo_config_set_boolean (prefs, "show-hidden-files", TRUE);
	nemo_config_flush ();

	text = read_file ();
	check (count_of (text, "# Uncomment a line to change one.") == 1);
	check (count_of (text, "#preferences.click-policy: ") == 0);
	check (count_of (text, "click-policy: single") == 1);
	check (count_of (text, "#preferences.menu-config.selection-menu-copy: ") == 0);
	check (count_of (text, "selection-menu-copy: false") == 1);
	check (count_of (text, "#appearance.mode: ") == 0);
	check (count_of (text, "# Widget theme name, or empty for the platform's own") == 1);
	g_free (text);
}

/* SHCL decides which group a run of comments belongs to, and a binding written
 * into the middle of the list has been seen to bring the rest of it back indented
 * under that group. Whatever the indent, it is still the old copy and still has
 * to come off. */
static void
test_indented_copy_removed (NemoConfigGroup *prefs)
{
	char  *text = read_file ();
	char  *list = strstr (text, "# ------");
	GString *edited;
	char **lines;
	int    i;

	check (list != NULL);
	if (list == NULL) {
		g_free (text);
		return;
	}

	lines = g_strsplit (list, "\n", -1);
	*list = '\0';
	edited = g_string_new (text);
	g_string_append (edited, "preferences:\n\tstart-with-dual-pane: true\n");
	for (i = 0; lines[i] != NULL; i++) {
		if (lines[i][0] != '\0') {
			g_string_append_printf (edited, "\t%s\n", lines[i]);
		}
	}
	g_strfreev (lines);
	g_free (text);

	write_and_wait (edited->str, prefs, "start-with-dual-pane");
	g_string_free (edited, TRUE);

	nemo_config_set_int (prefs, "tab-width-max-percent", 26);
	nemo_config_flush ();

	text = read_file ();
	check (count_of (text, "# Uncomment a line to change one.") == 1);
	check (count_of (text, "#windows.allow-slash-input: true") == 1);
	check (count_of (text, "##    Format   3") == 1);
	check (count_of (text, "This config file format is SHCL.") == 1);
	g_free (text);

	nemo_config_reset (prefs, "tab-width-max-percent");
	nemo_config_reset (prefs, "start-with-dual-pane");
	nemo_config_flush ();
}

/* A block from another SHCL release has other text in it. It still has to be
 * replaced, not left sitting above the current one. */
static void
test_older_banner_replaced (NemoConfigGroup *prefs)
{
	char *settings = read_settings ();
	char *edited = g_strconcat (settings,
	                            "\npreferences:\n\tstart-with-dual-pane: true\n\n"
	                            "##\n"
	                            "## This config file format is SHCL.\n"
	                            "##    Format   3\n"
	                            "##    Syntax   https://example.invalid/old/spec.md\n"
	                            "##    Legal    An older legal line.\n"
	                            "##\n", NULL);
	char *text;

	g_free (settings);
	write_and_wait (edited, prefs, "start-with-dual-pane");
	g_free (edited);

	nemo_config_set_int (prefs, "tab-width-min-percent", 12);
	nemo_config_flush ();

	text = read_file ();
	check (count_of (text, "##    Format   3") == 1);
	check (count_of (text, "##    Syntax") == 1);
	check (strstr (text, "example.invalid") == NULL);
	check (strstr (text, "An older legal line") == NULL);
	check (count_of (text, "This config file format is SHCL.") == 1);
	g_free (text);

	nemo_config_reset (prefs, "tab-width-min-percent");
	nemo_config_reset (prefs, "start-with-dual-pane");
	nemo_config_flush ();
}

/* A line added at the end of the file, below the info block, makes the block
 * that key's comment, so it is no longer at the end. It still comes off. */
static void
test_line_after_banner (NemoConfigGroup *prefs)
{
	char *settings = read_file ();
	char *edited = g_strconcat (settings, "preferences.start-with-dual-pane: true\n", NULL);
	char *text;

	g_free (settings);
	write_and_wait (edited, prefs, "start-with-dual-pane");
	g_free (edited);

	nemo_config_set_int (prefs, "tab-width-min-percent", 13);
	nemo_config_flush ();

	text = read_file ();
	check (count_of (text, "This config file format is SHCL.") == 1);
	check (count_of (text, "##    Format   3") == 1);
	check (g_str_has_suffix (text, "No warranty.\n##\n"));
	g_free (text);

	nemo_config_reset (prefs, "tab-width-min-percent");
	nemo_config_reset (prefs, "start-with-dual-pane");
	nemo_config_flush ();
}

/* A comment somebody wrote themselves is not ours to remove, a bare "##" of
 * their own included. */
static void
test_own_comment_kept (NemoConfigGroup *prefs)
{
	char *settings = read_settings ();
	char *edited = g_strconcat ("# notes of my own\n##\n", settings,
	                            "\npreferences:\n\tstart-with-dual-pane: true\n", NULL);
	char *text;

	g_free (settings);
	write_and_wait (edited, prefs, "start-with-dual-pane");
	g_free (edited);

	nemo_config_set_int (prefs, "tab-width-min-percent", 11);
	nemo_config_flush ();

	text = read_file ();
	check (strstr (text, "# notes of my own\n##\n") != NULL);
	g_free (text);
}

/* Only a run of "##" lines that names the format is the info block. Lines
 * spelled like its fields elsewhere are somebody's notes, and so is a "##"
 * written against the block that is not one of its own lines. */
static void
test_banner_like_comment_kept (NemoConfigGroup *prefs)
{
	char *settings = read_settings ();
	char *edited = g_strconcat ("##\n##    Aligned   like the info block\n##\n",
	                            settings,
	                            "\npreferences.always-show-tabs: true\n", NULL);
	char *text;

	g_free (settings);
	write_and_wait (edited, prefs, "always-show-tabs");
	g_free (edited);

	nemo_config_set_int (prefs, "tab-width-min-percent", 14);
	nemo_config_flush ();

	text = read_file ();
	check (strstr (text, "##\n##    Aligned   like the info block\n##\n") != NULL);
	check (count_of (text, "This config file format is SHCL.") == 1);
	g_free (text);

	/* A note added below the block, then a key under it. */
	text = read_file ();
	edited = g_strconcat (text, "## my note\npreferences.show-full-path-titles: true\n", NULL);
	g_free (text);
	write_and_wait (edited, prefs, "show-full-path-titles");
	g_free (edited);

	nemo_config_set_int (prefs, "tab-width-min-percent", 15);
	nemo_config_flush ();

	text = read_file ();
	check (strstr (text, "## my note\n") != NULL);
	check (count_of (text, "This config file format is SHCL.") == 1);
	check (count_of (text, "##    Format   3") == 1);
	check (g_str_has_suffix (text, "No warranty.\n##\n"));
	g_free (text);

	nemo_config_reset (prefs, "tab-width-min-percent");
	nemo_config_reset (prefs, "always-show-tabs");
	nemo_config_reset (prefs, "show-full-path-titles");
	nemo_config_flush ();
}

int
main (int argc, char *argv[])
{
	char            *tmp;
	NemoConfigGroup *prefs, *menus, *appearance;

	tmp = test_scratch_config_home ("nemo-config-catalog-XXXXXX");

	g_log_set_always_fatal (G_LOG_LEVEL_CRITICAL);
	nemo_config_init ();

	prefs = nemo_config_get_group ("preferences");
	menus = nemo_config_get_group ("preferences.menu-config");
	appearance = nemo_config_get_group ("appearance");

	test_list_written (prefs);
	test_remembered_keys_left_out ();
	test_set_key_drops_out (prefs);
	test_save_is_a_fixed_point (prefs);
	test_uncommenting_takes_effect (prefs, menus, appearance);
	test_indented_copy_removed (prefs);
	test_older_banner_replaced (prefs);
	test_line_after_banner (prefs);
	test_own_comment_kept (prefs);
	test_banner_like_comment_kept (prefs);

	nemo_config_shutdown ();
	g_free (tmp);

	if (failures == 0) {
		g_print ("nemo-config catalog: all checks passed\n");
	}

	return failures == 0 ? 0 : 1;
}
