/* A settings file from an older SHCL format is kept as a backup beside the
 * original and written again in the current one. A newer format is read and
 * never saved over. Conversion only happens when the store opens, so this is
 * its own program with the old file in place first. So is a dev build's file
 * from before SHCL 3.0.0-beta1, stamped format 3 with its lists bare. */

#include <config.h>

#include <string.h>
#include <gio/gio.h>
#include <glib/gstdio.h>

#include <libnemo-private/nemo-config.h>
#include <libnemo-private/nemo-file-utilities.h>

#include "test-scratch.h"
#include "test-check.h"

/* The 2.x spelling here reads as "-x\" under format 3. A key the app no longer
 * knows has to go from the new file and stay in the backup. */
static const char old_file[] =
	"preferences.show-hidden-files: true\n"
	"terminal.exec-arg: -x\\#y\n"
	"gone.removed-key: 42\n";

/* A list key's bare list gets brackets; a text key's comma stays text and is
 * refused as before; a list already in brackets is left as it is. */
static const char dev_file[] =
	"favorites: /home/a, \"/b, c\"  # pinned\r\n"
	"terminal.exec-arg: -x, -y\r\n"
	"list-view:\r\n"
	"\tdefault-visible-columns: [name, size]\r\n"
	"icon-view:\r\n"
	"\tcaptions: owner, group\r\n"
	"\r\n"
	"##    Format   3\r\n";

static int changed_count;

static void
on_changed (G_GNUC_UNUSED NemoConfigGroup *group, G_GNUC_UNUSED const char *key, G_GNUC_UNUSED gpointer data)
{
	changed_count++;
}

static void
wait_for_change (int from)
{
	int spins = 0;

	while (changed_count == from && spins++ < 1000) {
		g_main_context_iteration (NULL, FALSE);
		g_usleep (2000);
	}
}

/* Let the monitor deliver whatever it has, when no change signal is due. */
static void
settle (void)
{
	int spins;

	for (spins = 0; spins < 1000; spins++) {
		g_main_context_iteration (NULL, FALSE);
		g_usleep (2000);
	}
}

static gboolean
strv_is (char **got, const char *want0, const char *want1)
{
	gboolean same = got != NULL && g_strv_length (got) == 2 &&
	                strcmp (got[0], want0) == 0 && strcmp (got[1], want1) == 0;

	g_strfreev (got);
	return same;
}

static char *
read_path (const char *path)
{
	char *text = NULL;

	if (!g_file_get_contents (path, &text, NULL, NULL))
		return g_strdup ("");
	return text;
}

/* Backups in the config dir, by name, sorted. */
static GPtrArray *
list_backups (const char *dir)
{
	GPtrArray  *out = g_ptr_array_new_with_free_func (g_free);
	GDir       *d = g_dir_open (dir, 0, NULL);
	const char *name;

	while (d != NULL && (name = g_dir_read_name (d)) != NULL) {
		if (g_str_has_prefix (name, "settings_backup_"))
			g_ptr_array_add (out, g_strdup (name));
	}
	if (d != NULL)
		g_dir_close (d);
	g_ptr_array_sort (out, (GCompareFunc) g_strcmp0);
	return out;
}

/* settings_backup_YYYYmmDD-HHMMSS_format-v<N>.shcl */
static gboolean
backup_name_ok (const char *name, const char *format)
{
	char       *tail = g_strdup_printf ("_format-v%s.shcl", format);
	const char *stamp = name + strlen ("settings_backup_");
	gboolean    ok = g_str_has_suffix (name, tail) && strlen (stamp) >= 15 &&
	                 stamp[8] == '-';
	int         i;

	for (i = 0; ok && i < 15; i++) {
		if (i != 8 && !g_ascii_isdigit (stamp[i]))
			ok = FALSE;
	}
	g_free (tail);
	return ok;
}

int
main (void)
{
	char            *tmp, *path, *dir, *text, *first_backup;
	GPtrArray       *backups;
	NemoConfigGroup *prefs, *terminal, *root;
	char            *exec_arg;
	gulong           id;

	tmp = test_scratch_config_home ("nemo-config-format-XXXXXX");

	/* The store only knows its path once it is open. */
	dir = nemo_get_user_directory ();
	path = g_build_filename (dir, "settings.shcl", NULL);
	g_mkdir_with_parents (dir, 0700);
	check (g_file_set_contents (path, old_file, -1, NULL));

	nemo_config_init ();
	{
		char *opened = nemo_config_get_path ();

		check (g_strcmp0 (opened, path) == 0);
		g_free (opened);
	}
	prefs    = nemo_config_get_group ("preferences");
	terminal = nemo_config_get_group ("terminal");

	/* No format line at startup: converted, with the original kept as is. */
	backups = list_backups (dir);
	check (backups->len == 1);
	first_backup = backups->len > 0 ? g_strdup (backups->pdata[0]) : g_strdup ("");
	g_assert (first_backup != NULL);
	check (backup_name_ok (first_backup, "2"));
	{
		char *full = g_build_filename (dir, first_backup, NULL);

		text = read_path (full);
		check (strcmp (text, old_file) == 0);
		g_free (text);
		g_free (full);
	}
	g_ptr_array_unref (backups);

	text = read_path (path);
	check (strstr (text, "##    Format   ") != NULL);
	check (strstr (text, "removed-key") == NULL);
	check (strstr (text, "# Argument that terminal takes before a command") != NULL);
	g_free (text);

	check (nemo_config_get_boolean (prefs, "show-hidden-files") == TRUE);
	exec_arg = nemo_config_get_string (terminal, "exec-arg");
	check (g_strcmp0 (exec_arg, "-x\\#y") == 0);
	g_free (exec_arg);

	id = g_signal_connect (prefs, "changed", G_CALLBACK (on_changed), NULL);

	/* A file with no format line that turns up while running is a hand
	 * edit, read by today's rules and left alone. */
	changed_count = 0;
	check (g_file_set_contents (path, "preferences.show-hidden-files: false\n", -1, NULL));
	wait_for_change (0);
	check (nemo_config_get_boolean (prefs, "show-hidden-files") == FALSE);
	backups = list_backups (dir);
	check (backups->len == 1);
	g_ptr_array_unref (backups);

	/* A format line naming an older major is converted while running too. */
	changed_count = 0;
	check (g_file_set_contents (path,
	                            "preferences.show-hidden-files: true\n"
	                            "\n"
	                            "##    Format   2\n", -1, NULL));
	wait_for_change (0);
	settle ();
	check (nemo_config_get_boolean (prefs, "show-hidden-files") == TRUE);
	backups = list_backups (dir);
	check (backups->len == 2);
	if (backups->len == 2) {
		const char *second = g_strcmp0 (backups->pdata[0], first_backup) == 0
			? backups->pdata[1] : backups->pdata[0];

		check (backup_name_ok (second, "2"));
	}
	g_ptr_array_unref (backups);
	text = read_path (path);
	check (strstr (text, "##    Format   2") == NULL);
	check (strstr (text, "##    Format   ") != NULL);
	g_free (text);

	/* Newer than this build: read, and a change made here is not saved. */
	{
		static const char newer[] =
			"preferences.show-hidden-files: false\n"
			"\n"
			"##    Format   999\n";
		char *before;

		changed_count = 0;
		check (g_file_set_contents (path, newer, -1, NULL));
		wait_for_change (0);
		check (nemo_config_get_boolean (prefs, "show-hidden-files") == FALSE);

		nemo_config_set_boolean (prefs, "show-hidden-files", TRUE);
		nemo_config_flush ();
		before = read_path (path);
		check (strcmp (before, newer) == 0);
		g_free (before);

		backups = list_backups (dir);
		check (backups->len == 2);
		g_ptr_array_unref (backups);
	}

	/* Once the file is current again, the change made meanwhile is saved. */
	check (g_file_set_contents (path, "preferences.show-hidden-files: false\n", -1, NULL));
	settle ();
	check (nemo_config_get_boolean (prefs, "show-hidden-files") == TRUE);
	nemo_config_flush ();
	text = read_path (path);
	check (strstr (text, "show-hidden-files: true") != NULL);
	g_free (text);

	/* A bare list typed into a current file while running is a hand edit by
	 * today's rules: refused, and nothing converts. */
	root = nemo_config_get_group ("");
	changed_count = 0;
	check (g_file_set_contents (path,
	                            "preferences.show-hidden-files: false\n"
	                            "favorites: /x, /y\n"
	                            "\n"
	                            "##    Format   3\n", -1, NULL));
	wait_for_change (0);
	settle ();
	check (nemo_config_get_boolean (prefs, "show-hidden-files") == FALSE);
	check (!strv_is (nemo_config_get_strv (root, "favorites"), "/x", "/y"));
	backups = list_backups (dir);
	check (backups->len == 2);
	g_ptr_array_unref (backups);

	g_signal_handler_disconnect (prefs, id);
	nemo_config_shutdown ();

	/* The same spelling at startup is a dev build's file: backed up as format
	 * 3 and rewritten with brackets. */
	check (g_file_set_contents (path, dev_file, -1, NULL));
	nemo_config_init ();
	root     = nemo_config_get_group ("");
	terminal = nemo_config_get_group ("terminal");
	check (strv_is (nemo_config_get_strv (root, "favorites"), "/home/a", "/b, c"));
	check (strv_is (nemo_config_get_strv (nemo_config_get_group ("icon-view"), "captions"),
	                "owner", "group"));
	check (strv_is (nemo_config_get_strv (nemo_config_get_group ("list-view"),
	                                      "default-visible-columns"), "name", "size"));
	exec_arg = nemo_config_get_string (terminal, "exec-arg");
	check (g_strcmp0 (exec_arg, "-e") == 0);
	g_free (exec_arg);

	backups = list_backups (dir);
	check (backups->len == 3);
	{
		guint    i;
		gboolean found = FALSE;

		for (i = 0; i < backups->len; i++) {
			char *full = g_build_filename (dir, backups->pdata[i], NULL);

			text = read_path (full);
			if (strcmp (text, dev_file) == 0) {
				found = TRUE;
				check (backup_name_ok (backups->pdata[i], "3"));
			}
			g_free (text);
			g_free (full);
		}
		check (found);
	}
	g_ptr_array_unref (backups);
	text = read_path (path);
	check (strstr (text, "captions: [owner, group]") != NULL);
	check (strstr (text, "-x, -y") == NULL);
	g_free (text);
	nemo_config_shutdown ();

	/* What it wrote is current and reads cleanly, so nothing converts again. */
	nemo_config_init ();
	check (strv_is (nemo_config_get_strv (nemo_config_get_group (""), "favorites"),
	                "/home/a", "/b, c"));
	backups = list_backups (dir);
	check (backups->len == 3);
	g_ptr_array_unref (backups);
	nemo_config_shutdown ();

	g_free (first_backup);
	g_free (dir);
	g_free (path);
	g_free (tmp);

	if (failures == 0)
		g_print ("nemo-config-format: all checks passed\n");

	return failures == 0 ? 0 : 1;
}
