/* Art and documents that used to be installed as files now ride inside the
 * binary, and nothing else looks for them on disk any more. A name dropped from
 * the manifest would just stop appearing, so check the ones with no file left to
 * fall back on. */

#include <config.h>

#include <gio/gio.h>
#include "test-check.h"

static void
carries (const char *path)
{
	GBytes *bytes = g_resources_lookup_data (path, G_RESOURCE_LOOKUP_FLAGS_NONE, NULL);

	if (bytes == NULL) {
		g_printerr ("FAIL missing resource: %s\n", path);
		failures++;
		return;
	}

	if (g_bytes_get_size (bytes) == 0) {
		g_printerr ("FAIL empty resource: %s\n", path);
		failures++;
	}

	g_bytes_unref (bytes);
}

int
main (int argc, char *argv[])
{
	/* The note emblem, which a file carrying a note is drawn with. */
	carries ("/org/nemo/appicons/16x16/emblems/emblem-note.png");
	carries ("/org/nemo/appicons/24x24/emblems/emblem-note.png");
	carries ("/org/nemo/appicons/48x48/emblems/emblem-note.png");

	/* The shortcut and symlink emblems, which no theme carries. */
	carries ("/org/nemo/appicons/scalable/emblems/nemo-emblem-shortcut.svg");
	carries ("/org/nemo/appicons/scalable/emblems/nemo-emblem-symlink.svg");

	/* What the info bar offers to open in the actions and scripts folders. */
	carries ("/org/nemo/action-info.md");
	carries ("/org/nemo/script-info.md");

	return failures == 0 ? 0 : 1;
}
