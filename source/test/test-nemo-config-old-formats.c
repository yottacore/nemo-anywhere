/* Settings files from each SHCL release before format 3, written by that
 * release's own code (shcl-old-writer, built per release), have to read in the
 * app the way that release read them. The conversion checked is the app's, at
 * startup: a file with no format line is read as 2.x, and backed up and
 * rewritten when today's rules would read it another way.
 *
 * A file with no format line is converted when the store opens, and the store
 * opens once a process, so each case is two runs of this program: one that
 * opens the old file, and one that opens what the first left behind. The
 * blocked case is one run: no backup can be written, so the file must never
 * be saved over, and a hand edit to it while running is still read as 2.x.
 *
 *   test-nemo-config-old-formats <release> <writer> [<release> <writer> ...] */

#include <config.h>

#include <stdlib.h>
#include <string.h>
#include <gio/gio.h>
#include <glib/gstdio.h>

#include <libnemo-private/nemo-config.h>
#include <libnemo-private/nemo-file-utilities.h>

#include "test-scratch.h"
#include "test-check.h"

#define FORMAT_LINE "\n##    Format   3\n"

static char *
read_path (const char *path, gsize *len)
{
	char  *text = NULL;
	gsize  n = 0;

	if (!g_file_get_contents (path, &text, &n, NULL)) {
		text = g_strdup ("");
		n = 0;
	}
	if (len != NULL)
		*len = n;
	return text;
}

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
	return out;
}

static GString *
unhex (const char *hex)
{
	GString *out = g_string_new (NULL);

	while (g_ascii_isxdigit (hex[0]) && g_ascii_isxdigit (hex[1])) {
		g_string_append_c (out, (char) (g_ascii_xdigit_value (hex[0]) * 16 +
		                                g_ascii_xdigit_value (hex[1])));
		hex += 2;
	}
	return out;
}

static gboolean
same_strv (char **got, const char *want)
{
	char    **hex;
	guint     n, i;
	gboolean  same;

	n = (guint) g_ascii_strtoull (want, NULL, 10);
	want = strchr (want, ':') + 1;
	hex = n == 0 ? g_new0 (char *, 1) : g_strsplit (want, ",", -1);
	same = got != NULL && g_strv_length (got) == n && g_strv_length (hex) == n;
	for (i = 0; same && i < n; i++) {
		GString *one = unhex (hex[i]);

		same = strcmp (got[i], one->str) == 0;
		g_string_free (one, TRUE);
	}
	g_strfreev (hex);
	return same;
}

static int changed_count;

static void
on_changed (NemoConfigGroup *group, const char *key, gpointer data)
{
	changed_count++;
}

static void
wait_for_change (int from)
{
	int spins = 0;

	while (changed_count == from && spins++ < 2500) {
		g_main_context_iteration (NULL, FALSE);
		g_usleep (2000);
	}
}

/* Every key the old release wrote, read through the app. A key marked as
 * misread is a known fault, filed in the backlog; it has to still read wrong,
 * so the check is updated when it is fixed. */
static void
check_values (const char *expect_path, const char *label)
{
	char  *text = read_path (expect_path, NULL);
	char **lines = g_strsplit (text, "\n", -1);
	int    n;

	g_free (text);
	check (g_strv_length (lines) > 15);

	for (n = 0; lines[n] != NULL; n++) {
		char           **f = g_strsplit (lines[n], "\t", 4);
		const char      *dot;
		char            *group_name, *got = NULL;
		NemoConfigGroup *group;
		gboolean         same = FALSE;

		if (g_strv_length (f) != 4) {
			check (lines[n][0] == '\0');
			g_strfreev (f);
			continue;
		}

		dot = strrchr (f[0], '.');
		group_name = g_strndup (f[0], (gsize) (dot - f[0]));
		group = nemo_config_get_group (group_name);

		switch (f[1][0]) {
		case 's': {
			GString *want = unhex (f[3]);

			got = nemo_config_get_string (group, dot + 1);
			same = g_strcmp0 (got, want->str) == 0;
			g_string_free (want, TRUE);
			break;
		}
		case 'l': {
			char **strv = nemo_config_get_strv (group, dot + 1);

			same = same_strv (strv, f[3]);
			got = strv != NULL ? g_strjoinv ("|", strv) : NULL;
			g_strfreev (strv);
			break;
		}
		case 'b':
			same = nemo_config_get_boolean (group, dot + 1) == (atoi (f[3]) != 0);
			break;
		case 'i':
			same = nemo_config_get_int (group, dot + 1) == atoi (f[3]);
			break;
		case 'f':
			same = nemo_config_get_double (group, dot + 1) == g_ascii_strtod (f[3], NULL);
			break;
		}

		if (strcmp (f[2], "1") == 0) {
			if (same)
				g_printerr ("%s: %s now reads as written; take its misread mark off\n",
				            label, f[0]);
			check (!same);
		} else {
			if (!same)
				g_printerr ("%s: %s reads <%s>, wanted hex %s\n",
				            label, f[0], got != NULL ? got : "", f[3]);
			check (same);
		}

		g_free (got);
		g_free (group_name);
		g_strfreev (f);
	}
	g_strfreev (lines);
}

/* First run: the old file goes in, then the store opens. */
static int
open_old_file (const char *home, const char *writer, const char *variant, const char *label)
{
	char       *dir = nemo_get_user_directory ();
	char       *path = g_build_filename (dir, "settings.shcl", NULL);
	char       *expect = g_build_filename (home, "expect.txt", NULL);
	const char *run[] = { writer, path, expect, variant, NULL };
	int         status = -1;
	char       *old, *now;
	gsize       old_len, now_len;
	GPtrArray  *backups;

	g_mkdir_with_parents (dir, 0700);
	check (g_spawn_sync (NULL, (char **) run, NULL, G_SPAWN_DEFAULT,
	                     NULL, NULL, NULL, NULL, &status, NULL));
	check (g_spawn_check_exit_status (status, NULL));
	old = read_path (path, &old_len);
	check (old_len > 0);
	check (strstr (old, "##    Format") == NULL);

	nemo_config_init ();

	backups = list_backups (dir);
	now = read_path (path, &now_len);
	if (strcmp (variant, "nobackslash") == 0) {
		/* Both rule sets read it the same. Left alone until the next save,
		 * which only adds the info block. */
		check (backups->len == 0);
		check (now_len == old_len && memcmp (now, old, old_len) == 0);
	} else {
		check (backups->len == 1);
		if (backups->len == 1) {
			char  *full = g_build_filename (dir, backups->pdata[0], NULL);
			char  *kept;
			gsize  kept_len;

			kept = read_path (full, &kept_len);
			check (kept_len == old_len && memcmp (kept, old, old_len) == 0);
			check (g_str_has_suffix (backups->pdata[0], "_format-v2.shcl"));
			g_free (kept);
			g_free (full);
		}
		check (strstr (now, FORMAT_LINE) != NULL);
	}
	g_free (now);
	g_ptr_array_unref (backups);

	check_values (expect, label);

	/* Any change saves, and the save is what the second run reads. */
	nemo_config_set_boolean (nemo_config_get_group ("preferences"), "always-show-tabs", TRUE);
	nemo_config_flush ();
	now = read_path (path, NULL);
	check (strstr (now, FORMAT_LINE) != NULL);
	g_free (now);

	nemo_config_shutdown ();
	g_free (old);
	g_free (expect);
	g_free (path);
	g_free (dir);
	return failures == 0 ? 0 : 1;
}

/* Second run: what the first one left has to read the same, and is current,
 * so nothing is converted again. */
static int
open_saved_file (const char *home, const char *variant, const char *label)
{
	char      *dir = nemo_get_user_directory ();
	char      *expect = g_build_filename (home, "expect.txt", NULL);
	GPtrArray *backups;

	nemo_config_init ();

	backups = list_backups (dir);
	check (backups->len == (strcmp (variant, "nobackslash") == 0 ? 0u : 1u));
	g_ptr_array_unref (backups);
	check_values (expect, label);
	check (nemo_config_get_boolean (nemo_config_get_group ("preferences"), "always-show-tabs"));

	nemo_config_shutdown ();
	g_free (expect);
	g_free (dir);
	return failures == 0 ? 0 : 1;
}

/* A directory at each name a backup could take in the next minute, so none
 * can be written. Only regular files count as backups after this. */
static void
block_backups (const char *dir)
{
	GDateTime *now = g_date_time_new_now_local ();
	int        s;

	for (s = -2; s < 60; s++) {
		GDateTime *at = g_date_time_add_seconds (now, s);
		char      *stamp = g_date_time_format (at, "%Y%m%d-%H%M%S");
		char      *name = g_strdup_printf ("settings_backup_%s_format-v2.shcl", stamp);
		char      *full = g_build_filename (dir, name, NULL);

		check (g_mkdir_with_parents (full, 0700) == 0);
		g_free (full);
		g_free (name);
		g_free (stamp);
		g_date_time_unref (at);
	}
	g_date_time_unref (now);
}

static guint
count_backup_files (const char *dir)
{
	GPtrArray *backups = list_backups (dir);
	guint      i, n = 0;

	for (i = 0; i < backups->len; i++) {
		char *full = g_build_filename (dir, backups->pdata[i], NULL);

		if (g_file_test (full, G_FILE_TEST_IS_REGULAR))
			n++;
		g_free (full);
	}
	g_ptr_array_unref (backups);
	return n;
}

/* A 2.x file that could not be backed up stays as it is, and an edit to it
 * while running is read the way the startup read it. */
static int
open_blocked (const char *home, const char *writer, const char *label)
{
	char            *dir = nemo_get_user_directory ();
	char            *path = g_build_filename (dir, "settings.shcl", NULL);
	char            *expect = g_build_filename (home, "expect.txt", NULL);
	const char      *run[] = { writer, path, expect, "plain", NULL };
	int              status = -1;
	char            *old, *edited, *now;
	gsize            old_len, now_len;
	NemoConfigGroup *prefs;
	gulong           id;

	g_mkdir_with_parents (dir, 0700);
	check (g_spawn_sync (NULL, (char **) run, NULL, G_SPAWN_DEFAULT,
	                     NULL, NULL, NULL, NULL, &status, NULL));
	check (g_spawn_check_exit_status (status, NULL));
	old = read_path (path, &old_len);
	block_backups (dir);

	nemo_config_init ();
	prefs = nemo_config_get_group ("preferences");
	check (count_backup_files (dir) == 0);
	now = read_path (path, &now_len);
	check (now_len == old_len && memcmp (now, old, old_len) == 0);
	g_free (now);
	check_values (expect, label);

	id = g_signal_connect (prefs, "changed", G_CALLBACK (on_changed), NULL);
	changed_count = 0;
	edited = g_strconcat (old, "preferences.always-show-tabs: true\n", NULL);
	check (g_file_set_contents (path, edited, -1, NULL));
	wait_for_change (0);
	check (nemo_config_get_boolean (prefs, "always-show-tabs"));
	check_values (expect, label);

	nemo_config_set_boolean (prefs, "show-hidden-files", FALSE);
	nemo_config_flush ();
	now = read_path (path, NULL);
	check (strcmp (now, edited) == 0);
	check (count_backup_files (dir) == 0);
	g_free (now);

	g_signal_handler_disconnect (prefs, id);
	nemo_config_shutdown ();
	g_free (edited);
	g_free (old);
	g_free (expect);
	g_free (path);
	g_free (dir);
	return failures == 0 ? 0 : 1;
}

static void
run_self (const char *self, const char *stage, const char *home, const char *writer,
          const char *variant, const char *label)
{
	const char *run[] = { self, stage, home, writer, variant, label, NULL };
	int         status = -1;

	check (g_spawn_sync (NULL, (char **) run, NULL, G_SPAWN_DEFAULT,
	                     NULL, NULL, NULL, NULL, &status, NULL));
	if (!g_spawn_check_exit_status (status, NULL)) {
		g_printerr ("%s: %s failed\n", label, stage);
		failures++;
	}
}

int
main (int argc, char *argv[])
{
	static const char *variants[] = { "plain", "hand", "nobackslash" };
	int                i, v;

	g_log_set_always_fatal (G_LOG_LEVEL_CRITICAL);

	if (argc == 6 && strcmp (argv[1], "--open-old") == 0) {
		test_scratch_point_config_at (argv[2]);
		return open_old_file (argv[2], argv[3], argv[4], argv[5]);
	}
	if (argc == 6 && strcmp (argv[1], "--open-saved") == 0) {
		test_scratch_point_config_at (argv[2]);
		return open_saved_file (argv[2], argv[4], argv[5]);
	}
	if (argc == 6 && strcmp (argv[1], "--blocked") == 0) {
		test_scratch_point_config_at (argv[2]);
		return open_blocked (argv[2], argv[3], argv[5]);
	}

	if (argc < 3 || (argc - 1) % 2 != 0) {
		g_printerr ("usage: %s <release> <writer> [<release> <writer> ...]\n", argv[0]);
		return 2;
	}

	for (i = 1; i + 1 < argc; i += 2) {
		for (v = 0; v < (int) G_N_ELEMENTS (variants); v++) {
			char *home = test_scratch_dir ("nemo-config-old-XXXXXX", NULL);
			char *label = g_strdup_printf ("SHCL %s, %s", argv[i], variants[v]);

			check (home != NULL);
			if (home == NULL)
				continue;
			run_self (argv[0], "--open-old", home, argv[i + 1], variants[v], label);
			run_self (argv[0], "--open-saved", home, argv[i + 1], variants[v], label);
			g_free (label);
			g_free (home);
		}
		{
			char *home = test_scratch_dir ("nemo-config-old-XXXXXX", NULL);
			char *label = g_strdup_printf ("SHCL %s, no backup", argv[i]);

			check (home != NULL);
			if (home != NULL)
				run_self (argv[0], "--blocked", home, argv[i + 1], "plain", label);
			g_free (label);
			g_free (home);
		}
	}

	if (failures == 0)
		g_print ("nemo-config-old-formats: all checks passed\n");

	return failures == 0 ? 0 : 1;
}
