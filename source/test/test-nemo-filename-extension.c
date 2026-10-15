/* What the Ext column shows for a given name. The interesting part is what does
 * NOT count as an extension - a dot is often just part of a name - so most of
 * these are refusals. */

#include <config.h>

#include <stdlib.h>
#include <string.h>
#include <glib.h>

#include <libnemo-private/nemo-file-utilities.h>

static int failures = 0;

static void
check_is (const char *name, const char *expected)
{
	char *got = nemo_filename_get_extension (name);

	if (g_strcmp0 (got, expected) != 0) {
		g_printerr ("FAIL: %s -> %s (wanted %s)\n",
			    name != NULL ? name : "(null)",
			    got != NULL ? got : "(null)",
			    expected != NULL ? expected : "(null)");
		failures++;
	}

	g_free (got);
}

int
main (void)
{
	/* The ordinary cases, compound names included: only the last dot counts, and
	   the dot itself is not part of the answer. */
	check_is ("report.txt", "txt");
	check_is ("archive.tar.gz", "gz");
	check_is ("song.mp3", "mp3");
	check_is ("archive.7z", "7z");
	check_is ("page.html5", "html5");
	check_is ("app.properties", "properties");
	check_is ("UPPER.TXT", "TXT");

	/* No dot at all. */
	check_is ("README", NULL);
	check_is ("", NULL);
	check_is (NULL, NULL);

	/* A hidden file's leading dot is not an extension - but a hidden file
	   can still have one of its own. */
	check_is (".bashrc", NULL);
	check_is (".config.bak", "bak");

	/* A trailing dot has nothing after it. */
	check_is ("odd.", NULL);

	/* All digits reads as a version or a date, not an extension. */
	check_is ("backup.2026", NULL);
	check_is ("lib.so.1", NULL);

	/* Too long to be one. */
	check_is ("notes.presentation", NULL);

	/* Only letters and digits qualify. */
	check_is ("backup.2026-08-22", NULL);
	check_is ("file.t x t", NULL);
	check_is ("essay.r\303\251sum\303\251", NULL);

	if (failures > 0) {
		g_printerr ("%d check(s) failed\n", failures);
		return EXIT_FAILURE;
	}

	g_print ("OK\n");
	return EXIT_SUCCESS;
}
