/* There is no wrapper script in front of the installed program any more, so it
 * points XDG_DATA_DIRS and PATH at its own prefix itself. Proving that needs a
 * binary sitting in a prefix, so the test copies itself into one and runs the
 * copy: the child sets the environment up and prints what it got, the parent
 * checks it. The data, translation and helper-program dirs are looked for the
 * same way, beside the binary before the built-in path, so the copy reports
 * those too. POSIX only. */

#include <config.h>

#include <libnemo-private/nemo-file-utilities.h>

#include <gio/gio.h>
#include <glib/gstdio.h>

#include <string.h>

#include "test-scratch.h"
#include "test-check.h"

/* The copy's half: set up and report, one variable per line. */
static int
report (void)
{
	nemo_setup_runtime_environment ();

	g_print ("XDG_DATA_DIRS=%s\n", g_getenv ("XDG_DATA_DIRS"));
	g_print ("PATH=%s\n", g_getenv ("PATH"));
	g_print ("DATA_DIR=%s\n", nemo_get_data_dir ());
	g_print ("LOCALE_DIR=%s\n", nemo_get_locale_dir ());
	g_print ("BIN_DIR=%s\n", nemo_get_bin_dir ());

	return 0;
}

/* The value the child printed for var, or NULL. */
static char *
value_of (const char *output, const char *var)
{
	char **lines = g_strsplit (output, "\n", -1);
	char *prefixed = g_strconcat (var, "=", NULL);
	char *found = NULL;
	guint i;

	for (i = 0; lines[i] != NULL && found == NULL; i++) {
		if (g_str_has_prefix (lines[i], prefixed)) {
			found = g_strdup (lines[i] + strlen (prefixed));
		}
	}

	g_free (prefixed);
	g_strfreev (lines);

	return found;
}

/* The dir the child printed for var is want, however it is spelled. */
static void
check_resolves_to (const char *output, const char *var, const char *want)
{
	char *got = value_of (output, var);
	char *got_canon = got != NULL ? g_canonicalize_filename (got, NULL) : NULL;
	char *want_canon = g_canonicalize_filename (want, NULL);

	if (g_strcmp0 (got_canon, want_canon) != 0) {
		g_printerr ("FAIL %s=%s (wanted %s)\n", var, got != NULL ? got : "(none)", want);
		failures++;
	}

	g_free (want_canon);
	g_free (got_canon);
	g_free (got);
}

/* Run the copy at exe with XDG_DATA_DIRS set to preset (NULL to unset it). */
static char *
run_copy (const char *exe, const char *preset)
{
	char *child_argv[] = { (char *) exe, (char *) "--report", NULL };
	char *out = NULL;
	GError *error = NULL;

	/* The child inherits ours, so setting it here is enough. */
	if (preset != NULL) {
		g_setenv ("XDG_DATA_DIRS", preset, TRUE);
	} else {
		g_unsetenv ("XDG_DATA_DIRS");
	}

	if (!g_spawn_sync (NULL, child_argv, NULL, G_SPAWN_DEFAULT, NULL, NULL,
			   &out, NULL, NULL, &error)) {
		g_printerr ("spawn: %s\n", error->message);
		g_error_free (error);
		g_free (out);
		out = NULL;
	}

	return out;
}

int
main (int argc, char *argv[])
{
	char *tmp;
	char *bin;
	char *share;
	char *exe;
	char *self;
	char *out;
	char *dirs;
	char *path;
	char *data_dir;
	char *locale_dir;

	if (argc > 1 && strcmp (argv[1], "--report") == 0) {
		return report ();
	}

	tmp = test_scratch_dir ("nemo-runtime-env-XXXXXX", NULL);
	g_assert_nonnull (tmp);

	bin = g_build_filename (tmp, "bin", NULL);
	share = g_build_filename (tmp, "share", NULL);
	g_mkdir_with_parents (bin, 0755);
	g_mkdir_with_parents (share, 0755);

	/* What an install relocated to tmp carries. */
	data_dir = g_build_filename (share, NEMO_APP_SLUG, NULL);
	locale_dir = g_build_filename (share, "locale", NULL);
	g_mkdir_with_parents (data_dir, 0755);
	g_mkdir_with_parents (locale_dir, 0755);

	self = nemo_get_exe_path ();
	g_assert_nonnull (self);
	exe = g_build_filename (bin, "probe", NULL);
	{
		char *bytes = NULL;
		gsize len = 0;

		g_assert_true (g_file_get_contents (self, &bytes, &len, NULL));
		g_assert_true (g_file_set_contents (exe, bytes, len, NULL));
		g_free (bytes);
	}
	g_chmod (exe, 0755);

#ifdef TEST_EXTENSION_LIB_DIR
	/* The build rpath is $ORIGIN relative, so from the copy it points at
	 * nothing. Newer meson test runs set this anyway; 0.61 does not. */
	{
		const char *old = g_getenv ("LD_LIBRARY_PATH");
		char *lib_path = old != NULL && *old != '\0'
			? g_strconcat (TEST_EXTENSION_LIB_DIR, ":", old, NULL)
			: g_strdup (TEST_EXTENSION_LIB_DIR);

		g_setenv ("LD_LIBRARY_PATH", lib_path, TRUE);
		g_free (lib_path);
	}
#endif

	/* Nothing set: our share dir goes in front of the system default. */
	out = run_copy (exe, NULL);
	g_assert_nonnull (out);
	dirs = value_of (out, "XDG_DATA_DIRS");
	path = value_of (out, "PATH");
	check (dirs != NULL && g_str_has_prefix (dirs, share));
	check (dirs != NULL && strstr (dirs, "/usr/share") != NULL);
	check (path != NULL && g_str_has_prefix (path, bin));
	check_resolves_to (out, "DATA_DIR", data_dir);
	check_resolves_to (out, "LOCALE_DIR", locale_dir);
	check_resolves_to (out, "BIN_DIR", bin);
	g_free (dirs);
	g_free (path);
	g_free (out);

	/* Something already set: kept, with ours in front. */
	out = run_copy (exe, "/opt/somewhere/share");
	g_assert_nonnull (out);
	dirs = value_of (out, "XDG_DATA_DIRS");
	check (dirs != NULL && g_str_has_prefix (dirs, share));
	check (dirs != NULL && strstr (dirs, "/opt/somewhere/share") != NULL);
	g_free (dirs);
	g_free (out);

	/* Already in front: left exactly as it was, not doubled up. */
	{
		char *preset = g_strconcat (share, ":/opt/somewhere/share", NULL);

		out = run_copy (exe, preset);
		g_assert_nonnull (out);
		dirs = value_of (out, "XDG_DATA_DIRS");
		check (g_strcmp0 (dirs, preset) == 0);
		g_free (dirs);
		g_free (out);
		g_free (preset);
	}

	g_free (locale_dir);
	g_free (data_dir);
	g_free (exe);
	g_free (self);
	g_free (share);
	g_free (bin);

	{
		char *cmd[] = { (char *) "rm", (char *) "-rf", tmp, NULL };

		g_spawn_sync (NULL, cmd, NULL, G_SPAWN_SEARCH_PATH, NULL, NULL,
			      NULL, NULL, NULL, NULL);
	}
	g_free (tmp);

	return failures == 0 ? 0 : 1;
}
