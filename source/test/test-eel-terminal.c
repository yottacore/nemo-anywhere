/* With no terminal emulator anywhere on PATH, opening a terminal must
 * decline cleanly instead of crashing on a NULL prefix. Launching a terminal
 * app and "Open in Terminal" share one fallback scan of known terminals when
 * nothing is configured, so that scan has to find one that is on PATH. */

#include <config.h>

#include <stdlib.h>
#include <gtk/gtk.h>
#include <glib/gstdio.h>
#include <eel/eel-gnome-extensions.h>

#include "test-scratch.h"
#include "test-check.h"

static void
check_fallback_is (const char *want)
{
	char *got = eel_gnome_get_fallback_terminal_exec ();

	if (g_strcmp0 (got, want) != 0) {
		g_printerr ("FAIL: fallback terminal %s (wanted %s)\n",
			    got != NULL ? got : "(none)", want != NULL ? want : "(none)");
		failures++;
	}
	g_free (got);
}

#ifndef G_OS_WIN32
static void
fake_program (const char *dir, const char *name)
{
	char *path = g_build_filename (dir, name, NULL);

	check (g_file_set_contents (path, "#!/bin/sh\nexit 0\n", -1, NULL));
	check (g_chmod (path, 0755) == 0);
	g_free (path);
}

/* A fake terminal on an otherwise empty PATH. Windows finds programs by
   extension, so a script there would not count. */
static void
test_fallback_scan (void)
{
	char *dir = test_scratch_dir ("eel-terminal-XXXXXX", NULL);

	check (dir != NULL);
	if (dir == NULL) {
		return;
	}

	g_setenv ("PATH", dir, TRUE);
	check_fallback_is (NULL);

	fake_program (dir, "xterm");
	check_fallback_is ("xterm");

	/* Earlier in the known list wins, whatever else is there. */
	fake_program (dir, "alacritty");
	check_fallback_is ("alacritty");

	g_setenv ("PATH", "/nonexistent", TRUE);
	g_free (dir);
}
#endif

int
main (int argc, char *argv[])
{
	g_setenv ("PATH", "/nonexistent", TRUE);
	/* also hide any desktop schema that names a terminal - the box this
	 * runs on may have one installed */
	g_setenv ("XDG_DATA_DIRS", "/nonexistent", TRUE);
	g_setenv ("GSETTINGS_SCHEMA_DIR", "/nonexistent", TRUE);

	gtk_init_check (&argc, &argv);

	check_fallback_is (NULL);
	eel_gnome_open_terminal_on_screen ("true", NULL);

#ifndef G_OS_WIN32
	test_fallback_scan ();
#endif

	if (failures > 0) {
		return EXIT_FAILURE;
	}

	g_print ("eel-terminal: all checks passed\n");
	return 0;
}
