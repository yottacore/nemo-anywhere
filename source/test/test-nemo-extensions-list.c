/* The extension lister is a flag on the program itself rather than a second
 * binary. It has to work without a display and without touching a copy that
 * is already running, so this drives it the way the plugin page does: run the
 * program (argv[1]) with the flag and read what comes back. */

#include <config.h>

#include <gio/gio.h>
#include "test-check.h"

int
main (int argc, char *argv[])
{
	char *child_argv[3];
	char *out = NULL;
	char *err = NULL;
	int status = -1;
	GError *error = NULL;
	char **lines;
	guint i;

	if (argc < 2) {
		g_printerr ("usage: %s <nemo-executable>\n", argv[0]);
		return 1;
	}

	child_argv[0] = argv[1];
	child_argv[1] = (char *) "--extensions-list";
	child_argv[2] = NULL;

	/* No DISPLAY on purpose. A run that needed one would be a regression:
	 * the flag is handled before anything starts up. */
	g_unsetenv ("DISPLAY");
	g_unsetenv ("WAYLAND_DISPLAY");

	if (!g_spawn_sync (NULL, child_argv, NULL, G_SPAWN_DEFAULT, NULL, NULL,
	                   &out, &err, &status, &error)) {
		g_printerr ("spawn: %s\n", error->message);
		g_error_free (error);
		return 1;
	}

	check (g_spawn_check_wait_status (status, NULL));

	/* Whatever is installed on the machine running this, every line the
	 * plugin page will parse has to have the prefix it looks for. */
	lines = g_strsplit (out, "\n", -1);
	for (i = 0; lines[i] != NULL; i++) {
		if (lines[i][0] == '\0') {
			continue;
		}
		check (g_str_has_prefix (lines[i], "NEMO_EXTENSION:::"));
	}
	g_strfreev (lines);

	g_free (out);
	g_free (err);

	return failures == 0 ? 0 : 1;
}
