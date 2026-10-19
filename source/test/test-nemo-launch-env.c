/* A relocated install puts its own share, bin and schema dirs on XDG_DATA_DIRS,
 * PATH and GSETTINGS_SCHEMA_DIR for itself. A program of the user's that it
 * starts gets those lists as they were before, and a helper of ours keeps
 * them. The test copies itself into a prefix, as runtime-env does, and the
 * copy starts itself again by every route with --dump, which writes the
 * environment it got. POSIX only. */

#include <config.h>

#include <libnemo-private/nemo-file-utilities.h>
#include <libnemo-private/nemo-program-choosing.h>
#include <libnemo-private/nemo-tool-run.h>

#include <gio/gdesktopappinfo.h>
#include <glib/gstdio.h>
#include <gtk/gtk.h>

#include <string.h>
#include <sys/wait.h>

#include "test-scratch.h"
#include "test-check.h"

#define USER_DATA_DIRS "/opt/user-data:/usr/share"

extern char **environ;

static int
dump (const char *file)
{
	GString *out = g_string_new (NULL);
	char *part = g_strconcat (file, ".part", NULL);
	int i;

	for (i = 0; environ[i] != NULL; i++) {
		g_string_append (out, environ[i]);
		g_string_append_c (out, '\n');
	}
	if (!g_file_set_contents (part, out->str, -1, NULL) || g_rename (part, file) != 0) {
		return 1;
	}
	g_free (part);
	g_string_free (out, TRUE);

	return 0;
}

static void
write_file (const char *path, const char *text)
{
	char *dir = g_path_get_dirname (path);

	g_mkdir_with_parents (dir, 0755);
	g_assert_true (g_file_set_contents (path, text, -1, NULL));
	g_free (dir);
}

/* A desktop file that runs exe with --dump <out>/<name>, plus extra. */
static void
desktop_file (const char *exe, const char *dir, const char *name, const char *out, const char *extra)
{
	char *path = g_strdup_printf ("%s/%s.desktop", dir, name);
	char *text = g_strdup_printf ("[Desktop Entry]\nType=Application\nName=%s\n"
				      "Exec=%s --dump %s/%s %s\n%s",
				      name, exe, out, name, extra != NULL ? extra : "",
				      extra != NULL && strstr (extra, "%u") != NULL
				      ? "MimeType=x-scheme-handler/nemolaunchtest;\n" : "");

	write_file (path, text);
	g_free (text);
	g_free (path);
}

/* All written before the copy starts, since GIO reads the folders once and
 * only hears of a new file from the main loop. */
static void
write_fixtures (const char *exe, const char *prefix, const char *home)
{
	char *out = g_build_filename (home, "out", NULL);
	char *apps = g_build_filename (home, "share", "applications", NULL);
	char *own_apps = g_build_filename (prefix, "share", "applications", NULL);
	char *own = g_build_filename (own_apps, NEMO_APP_SLUG ".desktop", NULL);
	char *text = g_strdup_printf ("[Desktop Entry]\nType=Application\nName=own\n"
				      "Exec=" NEMO_APP_SLUG " --dump %s/own %%U\n", out);
	char *notes = g_build_filename (home, "notes.txt", NULL);

	g_mkdir_with_parents (out, 0755);
	desktop_file (exe, apps, "open", out, "%f");
	desktop_file (exe, apps, "uri", out, "%u");
	desktop_file (exe, home, "desktop", out, NULL);
	write_file (own, text);
	write_file (notes, "kumquat\n");

	g_free (notes);
	g_free (text);
	g_free (own);
	g_free (own_apps);
	g_free (apps);
	g_free (out);
}

static char *
read_dump (const char *out, const char *name)
{
	char *path = g_build_filename (out, name, NULL);
	char *text = NULL;
	gint64 until = g_get_monotonic_time () + 20 * G_USEC_PER_SEC;

	while (!g_file_get_contents (path, &text, NULL, NULL) && g_get_monotonic_time () < until) {
		while (g_main_context_iteration (NULL, FALSE)) {
		}
		g_usleep (20000);
	}
	g_free (path);

	return text;
}

/* var's line in a dump, or NULL when it is not set there. */
static char *
value_in (const char *text, const char *var)
{
	char **lines = g_strsplit (text, "\n", -1);
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

/* A variable the program didn't have prints as unset, since %s of NULL is
   undefined. */
static const char *
shown (const char *value)
{
	return value != NULL ? value : "(unset)";
}

static void
check_user (const char *out, const char *name, const char *user_path)
{
	char *text = read_dump (out, name);
	char *dirs, *path, *schemas;

	if (text == NULL) {
		g_printerr ("FAIL %s: never wrote what it got\n", name);
		failures++;
		return;
	}
	dirs = value_in (text, "XDG_DATA_DIRS");
	path = value_in (text, "PATH");
	schemas = value_in (text, "GSETTINGS_SCHEMA_DIR");
	if (g_strcmp0 (dirs, USER_DATA_DIRS) != 0 || g_strcmp0 (path, user_path) != 0 || schemas != NULL) {
		g_printerr ("FAIL %s: XDG_DATA_DIRS=%s PATH=%s GSETTINGS_SCHEMA_DIR=%s\n",
			    name, shown (dirs), shown (path), shown (schemas));
		failures++;
	}
	g_free (schemas);
	g_free (path);
	g_free (dirs);
	g_free (text);
}

static void
check_ours (const char *out, const char *name, const char *prefix)
{
	char *text = read_dump (out, name);
	char *dirs = text != NULL ? value_in (text, "XDG_DATA_DIRS") : NULL;
	char *path = text != NULL ? value_in (text, "PATH") : NULL;
	char *schemas = text != NULL ? value_in (text, "GSETTINGS_SCHEMA_DIR") : NULL;
	char *share = g_build_filename (prefix, "share", NULL);
	char *bin = g_build_filename (prefix, "bin", NULL);

	if (dirs == NULL || !g_str_has_prefix (dirs, share) ||
	    path == NULL || !g_str_has_prefix (path, bin) ||
	    schemas == NULL || !g_str_has_prefix (schemas, share)) {
		g_printerr ("FAIL %s: XDG_DATA_DIRS=%s PATH=%s GSETTINGS_SCHEMA_DIR=%s\n",
			    name, shown (dirs), shown (path), shown (schemas));
		failures++;
	}
	g_free (bin);
	g_free (share);
	g_free (schemas);
	g_free (path);
	g_free (dirs);
	g_free (text);
}

/* The copy's half, run from inside the prefix. */
static int
routes (const char *prefix, const char *home, int argc, char **argv)
{
	char *out = g_build_filename (home, "out", NULL);
	char *apps = g_build_filename (home, "share", "applications", NULL);
	char *exe = nemo_get_exe_path ();
	const char *user_path = g_getenv ("PATH");
	char *saved_path = g_strdup (user_path);
	char *share = g_build_filename (prefix, "share", NULL);
	GdkScreen *screen;
	GtkWidget *window;
	GError *error = NULL;
	int rc = 1;

	nemo_setup_runtime_environment ();
	/* Otherwise nothing below tests anything. */
	if (!g_str_has_prefix (g_getenv ("XDG_DATA_DIRS"), share) ||
	    g_getenv ("GSETTINGS_SCHEMA_DIR") == NULL) {
		g_printerr ("FAIL the copy did not set up its own prefix\n");
		goto out;
	}
	if (!gtk_init_check (&argc, &argv)) {
		rc = 77;
		goto out;
	}
	screen = gdk_screen_get_default ();
	window = gtk_window_new (GTK_WINDOW_TOPLEVEL);

	/* Open With, and opening a file the usual way. */
	{
		char *path = g_build_filename (apps, "open.desktop", NULL);
		GDesktopAppInfo *app = g_desktop_app_info_new_from_filename (path);
		char *target = g_build_filename (home, "notes.txt", NULL);
		char *uri = g_filename_to_uri (target, NULL, NULL);
		GList *uris = g_list_append (NULL, uri);

		g_assert_nonnull (app);
		nemo_launch_application_by_uri (G_APP_INFO (app), uris, NULL);
		g_list_free (uris);
		g_free (uri);
		g_free (target);
		g_object_unref (app);
		g_free (path);
	}

	/* Open With on the desktop file the install has, which names the program
	 * bare. The user's PATH has some other one by that name. */
	{
		char *path = g_build_filename (share, "applications", NEMO_APP_SLUG ".desktop", NULL);
		char *target = g_build_filename (home, "notes.txt", NULL);
		char *uri = g_filename_to_uri (target, NULL, NULL);
		GList *uris = g_list_append (NULL, uri);
		GDesktopAppInfo *app = g_desktop_app_info_new_from_filename (path);

		g_assert_nonnull (app);
		nemo_launch_application_by_uri (G_APP_INFO (app), uris, NULL);
		g_list_free (uris);
		g_free (uri);
		g_free (target);
		g_object_unref (app);
		g_free (path);
	}

	/* Running a program or a script, and a terminal for one. */
	{
		char *cmd = g_strdup_printf ("%s --dump %s/command", exe, out);
		char *cmd_term = g_strdup_printf ("%s --dump %s/terminal", exe, out);

		nemo_launch_application_from_command (screen, cmd, FALSE, NULL);
		nemo_launch_application_from_command (screen, cmd_term, TRUE, NULL);
		g_free (cmd_term);
		g_free (cmd);
	}

	/* A launcher in the file list. */
	{
		char *path = g_build_filename (home, "desktop.desktop", NULL);
		char *uri = g_filename_to_uri (path, NULL, NULL);

		nemo_launch_desktop_file (screen, uri, NULL, GTK_WINDOW (window));
		g_free (uri);
		g_free (path);
	}

	/* Help and the About box's links. */
	{
		if (!nemo_show_uri (GTK_WINDOW (window), "nemolaunchtest:hello", &error)) {
			g_printerr ("FAIL uri: %s\n", error->message);
			g_clear_error (&error);
			failures++;
		}
	}

	/* What an action, the terminal here and an extension's settings use. */
	{
		char *name = g_build_filename (out, "spawn", NULL);
		char *spawn_argv[] = { exe, (char *) "--dump", name, NULL };

		if (!nemo_spawn_for_user (NULL, spawn_argv, G_SPAWN_DEFAULT, NULL, &error)) {
			g_printerr ("FAIL spawn: %s\n", error->message);
			g_clear_error (&error);
			failures++;
		}
		g_free (name);
	}

	/* A helper of ours keeps our own. */
	{
		char *name = g_build_filename (out, "helper", NULL);
		const char *helper_argv[] = { exe, "--dump", name, NULL };
		NemoToolRun *run = nemo_tool_run_start (helper_argv, NULL, G_SUBPROCESS_FLAGS_NONE, &error);

		if (run == NULL) {
			g_printerr ("FAIL helper: %s\n", error->message);
			g_clear_error (&error);
			failures++;
		} else {
			nemo_tool_run_wait (run);
			nemo_tool_run_free (run);
		}
		g_free (name);
	}

	check_user (out, "open", saved_path);
	check_user (out, "command", saved_path);
	check_user (out, "terminal", saved_path);
	check_user (out, "desktop", saved_path);
	check_user (out, "uri", saved_path);
	check_user (out, "spawn", saved_path);
	check_user (out, "own", saved_path);
	{
		char *wrong = g_build_filename (out, "own.wrong", NULL);

		check (!g_file_test (wrong, G_FILE_TEST_EXISTS));
		g_free (wrong);
	}
	check_ours (out, "helper", prefix);

	gtk_widget_destroy (window);
	rc = failures == 0 ? 0 : 1;

 out:
	g_free (share);
	g_free (saved_path);
	g_free (exe);
	g_free (apps);
	g_free (out);

	return rc;
}

int
main (int argc, char *argv[])
{
	char *tmp, *bin, *schemas, *home, *userbin, *self, *exe, *user_path;
	char *child_argv[5];
	int status = 0;
	int rc = 1;
	GError *error = NULL;

	if (argc > 2 && strcmp (argv[1], "--dump") == 0) {
		return dump (argv[2]);
	}
	if (argc > 3 && strcmp (argv[1], "--routes") == 0) {
		return routes (argv[2], argv[3], argc, argv);
	}

	tmp = test_scratch_dir ("nemo-launch-env-XXXXXX", NULL);
	g_assert_nonnull (tmp);

	bin = g_build_filename (tmp, "bin", NULL);
	schemas = g_build_filename (tmp, "share", "glib-2.0", "schemas", NULL);
	home = g_build_filename (tmp, "home", NULL);
	userbin = g_build_filename (tmp, "userbin", NULL);
	g_mkdir_with_parents (bin, 0755);
	g_mkdir_with_parents (schemas, 0755);
	g_mkdir_with_parents (home, 0755);

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

	/* First of the terminals the launch looks for. It runs the command it
	 * is handed in one piece, spaces escaped. */
	{
		char *term = g_build_filename (userbin, "alacritty", NULL);

		write_file (term, "#!/bin/sh\n[ \"$1\" = -e ] && shift\nexec /bin/sh -c \"$*\"\n");
		g_chmod (term, 0755);
		g_free (term);
	}
	{
		char *other = g_build_filename (userbin, NEMO_APP_SLUG, NULL);

		write_file (other, "#!/bin/sh\n: > \"$2.wrong\"\n");
		g_chmod (other, 0755);
		g_free (other);
	}
	/* So the copy finds its data dir in the prefix too, and with it the
	 * desktop file above. */
	{
		char *data = g_build_filename (tmp, "share", NEMO_APP_SLUG, NULL);

		g_mkdir_with_parents (data, 0755);
		g_free (data);
	}

#ifdef TEST_EXTENSION_LIB_DIR
	{
		const char *old = g_getenv ("LD_LIBRARY_PATH");
		char *lib_path = old != NULL && *old != '\0'
			? g_strconcat (TEST_EXTENSION_LIB_DIR, ":", old, NULL)
			: g_strdup (TEST_EXTENSION_LIB_DIR);

		g_setenv ("LD_LIBRARY_PATH", lib_path, TRUE);
		g_free (lib_path);
	}
#endif

	/* What the user had. The copy inherits it. */
	user_path = g_strconcat (userbin, ":", g_getenv ("PATH") != NULL ? g_getenv ("PATH") : "/usr/bin:/bin", NULL);
	g_setenv ("PATH", user_path, TRUE);
	g_setenv ("XDG_DATA_DIRS", USER_DATA_DIRS, TRUE);
	g_unsetenv ("GSETTINGS_SCHEMA_DIR");
	test_scratch_point_config_at (home);
	{
		char *data_home = g_build_filename (home, "share", NULL);
		char *mimeapps = g_build_filename (home, "mimeapps.list", NULL);

		g_setenv ("XDG_DATA_HOME", data_home, TRUE);
		write_file (mimeapps, "[Default Applications]\nx-scheme-handler/nemolaunchtest=uri.desktop\n");
		g_free (mimeapps);
		g_free (data_home);
	}

	write_fixtures (exe, tmp, home);

	child_argv[0] = exe;
	child_argv[1] = (char *) "--routes";
	child_argv[2] = tmp;
	child_argv[3] = home;
	child_argv[4] = NULL;
	if (!g_spawn_sync (NULL, child_argv, NULL, G_SPAWN_CHILD_INHERITS_STDIN, NULL, NULL,
			   NULL, NULL, &status, &error)) {
		g_printerr ("spawn: %s\n", error->message);
		g_clear_error (&error);
	} else if (WIFEXITED (status)) {
		rc = WEXITSTATUS (status);
	}

	g_free (user_path);
	g_free (exe);
	g_free (self);
	g_free (userbin);
	g_free (home);
	g_free (schemas);
	g_free (bin);
	g_free (tmp);

	return rc;
}
