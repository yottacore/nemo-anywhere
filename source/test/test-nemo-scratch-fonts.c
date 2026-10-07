/* The scratch cleanup and fontconfig's user cache. Newer GTK 3 makes the
 * default pango font map in gtk_init, and pango runs FcInit for it in a
 * thread of its own. Where no system font cache is usable, that thread writes
 * one under XDG_CACHE_HOME, which a test points at its scratch dir. A short
 * test reached its exit cleanup with the thread still writing, and fontconfig
 * made the dir again for the next cache file. Seen on FreeBSD, where the
 * system cache was out of date.
 *
 * Each run goes in a child, with a font config that has no system cache in
 * it, so every font dir gets scanned and cached anew. */

#include <config.h>

#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <glib.h>
#include <glib/gstdio.h>
#include <pango/pangocairo.h>

#include "test-scratch.h"
#include "test-check.h"

static const char *font_dirs[] = {
	"/usr/share/fonts",
	"/usr/local/share/fonts",
	"/usr/X11R6/lib/X11/fonts",
};

/* Blocks until pango's FcInit is done, or runs it here on a pango without
   the thread. */
static void
fonts_settle (void)
{
	PangoFontFamily **families = NULL;
	int count = 0;

	pango_font_map_list_families (pango_cairo_font_map_get_default (), &families, &count);
	g_free (families);
}

static gboolean
has_font_cache (const char *home)
{
	g_autofree char *dir = g_build_filename (home, "fontconfig", NULL);
	GDir *listing = g_dir_open (dir, 0, NULL);
	const char *name;
	gboolean found = FALSE;

	if (listing == NULL) {
		return FALSE;
	}
	while ((name = g_dir_read_name (listing)) != NULL) {
		found = found || strstr (name, ".cache-") != NULL;
	}
	g_dir_close (listing);
	return found;
}

static int
child (const char *mode)
{
	char *home = test_scratch_config_home ("nemo-scratch-fonts-XXXXXX");
	int status;

	/* What gtk_init does first. */
	pango_cairo_font_map_get_default ();

	if (strcmp (mode, "control") == 0) {
		fonts_settle ();
		status = has_font_cache (home) ? EXIT_SUCCESS : 77;
	} else {
		test_scratch_cleanup ();
		fonts_settle ();
		status = g_file_test (home, G_FILE_TEST_EXISTS) ? EXIT_FAILURE : EXIT_SUCCESS;
		if (status != EXIT_SUCCESS) {
			g_printerr ("FAIL %s is back after the cleanup\n", home);
		}
	}

	g_free (home);
	return status;
}

static int
run_child (const char *self, const char *mode, char **envp)
{
	const char *argv[] = { self, "--child", mode, NULL };
	GError *error = NULL;
	int wait_status = 0;

	if (!g_spawn_sync (NULL, (char **) argv, envp, G_SPAWN_CHILD_INHERITS_STDIN, NULL, NULL,
			   NULL, NULL, &wait_status, &error)) {
		g_printerr ("FAIL could not start %s: %s\n", self, error->message);
		g_error_free (error);
		return -1;
	}
	return WIFEXITED (wait_status) ? WEXITSTATUS (wait_status) : -1;
}

static gboolean
is_empty_dir (const char *path)
{
	GDir *listing = g_dir_open (path, 0, NULL);
	gboolean empty;

	if (listing == NULL) {
		return FALSE;
	}
	empty = g_dir_read_name (listing) == NULL;
	g_dir_close (listing);
	return empty;
}

int
main (int argc, char *argv[])
{
	GString *conf;
	char *base, *conf_path, *tmp, **envp;
	gboolean any_dir = FALSE;
	guint i;
	int got;

	if (argc == 3 && strcmp (argv[1], "--child") == 0) {
		return child (argv[2]);
	}

	base = test_scratch_dir ("nemo-scratch-fonts-run-XXXXXX", NULL);
	conf_path = g_build_filename (base, "fonts.conf", NULL);
	tmp = g_build_filename (base, "tmp", NULL);
	check (g_mkdir (tmp, 0700) == 0);

	/* The first cachedir sits under a plain file, so nobody can write there,
	   root included, and the user cache is the only one. */
	conf = g_string_new ("<?xml version=\"1.0\"?>\n<fontconfig>\n");
	for (i = 0; i < G_N_ELEMENTS (font_dirs); i++) {
		if (g_file_test (font_dirs[i], G_FILE_TEST_IS_DIR)) {
			g_string_append_printf (conf, "\t<dir>%s</dir>\n", font_dirs[i]);
			any_dir = TRUE;
		}
	}
	g_string_append_printf (conf, "\t<cachedir>%s/none</cachedir>\n", conf_path);
	g_string_append (conf, "\t<cachedir prefix=\"xdg\">fontconfig</cachedir>\n</fontconfig>\n");
	check (g_file_set_contents (conf_path, conf->str, -1, NULL));
	g_string_free (conf, TRUE);

	if (!any_dir) {
		g_print ("SKIP: no font folder here\n");
		return 77;
	}

	envp = g_get_environ ();
	envp = g_environ_setenv (envp, "FONTCONFIG_FILE", conf_path, TRUE);
	envp = g_environ_unsetenv (envp, "FONTCONFIG_PATH");
	envp = g_environ_setenv (envp, "TMPDIR", tmp, TRUE);

	got = run_child (argv[0], "control", envp);
	if (got == 77) {
		g_print ("SKIP: pango here does not use fontconfig, or found no fonts\n");
		g_strfreev (envp);
		return 77;
	}
	check (got == EXIT_SUCCESS);
	check (is_empty_dir (tmp));

	/* A few times, since the font scan on a fast box with few fonts is short. */
	for (i = 0; i < 3; i++) {
		check (run_child (argv[0], "race", envp) == EXIT_SUCCESS);
		check (is_empty_dir (tmp));
	}

	g_strfreev (envp);
	g_free (tmp);
	g_free (conf_path);
	g_free (base);

	if (failures > 0) {
		return EXIT_FAILURE;
	}
	g_print ("scratch fonts: all checks passed\n");
	return EXIT_SUCCESS;
}
