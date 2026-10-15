/* The data dir list feeds every plugin-style scan (actions, search helpers,
 * themes), and the same share dir often arrives more than once - from the
 * wrapper, the launcher and, on win32, GLib's own exe-relative entry. One
 * copy each, in first-seen order, empty entries dropped. */

#include <config.h>

#include <string.h>
#include <glib.h>

#include <libnemo-private/nemo-file-utilities.h>
#include "test-check.h"

int
main (void)
{
	const char * const *dirs;
	guint n;
	gboolean seen_one = FALSE, seen_two = FALSE;

#ifdef G_OS_WIN32
	g_setenv ("XDG_DATA_DIRS", "C:\\one\\share;C:/one/share/;c:\\ONE\\share;;C:\\two\\share", TRUE);
#else
	g_setenv ("XDG_DATA_DIRS", "/one/share:/one/share/:/one//share::/two/share:/one/share", TRUE);
#endif

	dirs = nemo_get_system_data_dirs ();
	if (dirs == NULL) {
		g_printerr ("FAIL: no data dirs at all\n");
		return 1;
	}

	for (n = 0; dirs[n] != NULL; n++) {
		guint m;

		check (dirs[n][0] != '\0');
		for (m = 0; m < n; m++) {
			char *a = g_canonicalize_filename (dirs[m], NULL);
			char *b = g_canonicalize_filename (dirs[n], NULL);
#ifdef G_OS_WIN32
			check (g_ascii_strcasecmp (a, b) != 0);
#else
			check (strcmp (a, b) != 0);
#endif
			g_free (a);
			g_free (b);
		}
		if (strstr (dirs[n], "one") != NULL) {
			seen_one = TRUE;
			check (!seen_two);   /* first-seen order kept */
		}
		if (strstr (dirs[n], "two") != NULL) {
			seen_two = TRUE;
		}
	}
	check (seen_one && seen_two);
	/* GLib on win32 adds its own exe-relative entries, so only the env half
	 * has a known count there. */
#ifndef G_OS_WIN32
	check (n == 2);
#endif

	if (failures == 0) {
		g_print ("data-dirs: all checks passed\n");
	}
	return failures == 0 ? 0 : 1;
}
