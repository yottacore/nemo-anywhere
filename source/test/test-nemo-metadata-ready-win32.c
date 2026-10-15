/* Swapping the view on a folder that is already listed waits for INFO, MOUNT
 * and FILESYSTEM_INFO across every file in it. On Windows a link to a share
 * that is not answering costs about twenty seconds for each of those, one at a
 * time, so a folder of links takes minutes to change view - while the same
 * folder swaps instantly the first time, when the listing is still empty and
 * there is nothing to ask about.
 *
 * Needs a share that is not there but is on the local subnet, which is the only
 * address that reliably times out rather than failing at once. Set
 * NEMO_PROBE_DEAD_SHARE to one, with a last octet no run has used in the last
 * ten minutes - Windows remembers a failed lookup that long. Skipped without it. */

#include <config.h>

#include <stdlib.h>
#include <glib/gstdio.h>
#include <gtk/gtk.h>

#include <libnemo-private/nemo-directory.h>
#include <libnemo-private/nemo-file.h>

#include <windows.h>

#include "test-scratch.h"
#include "test-check.h"

static gboolean done = FALSE;
static void *client;

static void
ready (G_GNUC_UNUSED NemoDirectory *directory, G_GNUC_UNUSED GList *files, G_GNUC_UNUSED gpointer data)
{
	done = TRUE;
}

static gboolean
give_up (G_GNUC_UNUSED gpointer data)
{
	done = TRUE;
	return G_SOURCE_REMOVE;
}

static void
run_until_done (int seconds)
{
	guint timeout = g_timeout_add_seconds (seconds, give_up, NULL);

	done = FALSE;
	while (!done) {
		g_main_context_iteration (NULL, TRUE);
	}
	g_source_remove (timeout);
}

static gboolean
make_link (const char *link, const char *target)
{
	wchar_t *wl = g_utf8_to_utf16 (link, -1, NULL, NULL, NULL);
	wchar_t *wt = g_utf8_to_utf16 (target, -1, NULL, NULL, NULL);
	gboolean ok = CreateSymbolicLinkW (wl, wt,
					   SYMBOLIC_LINK_FLAG_DIRECTORY |
					   SYMBOLIC_LINK_FLAG_ALLOW_UNPRIVILEGED_CREATE);

	g_free (wl);
	g_free (wt);
	return ok;
}

int
main (int argc, char **argv)
{
	const char *dead = g_getenv ("NEMO_PROBE_DEAD_SHARE");
	char *tmp, *uri, *link, *sub;
	NemoDirectory *directory;
	gint64 started;
	double seconds;

	gtk_init_check (&argc, &argv);

	if (dead == NULL) {
		g_print ("nemo-metadata-ready-win32: skipped, NEMO_PROBE_DEAD_SHARE not set\n");
		return 77;
	}

	tmp = test_scratch_dir ("nemo-mdready-XXXXXX", NULL);
	if (tmp == NULL) {
		g_printerr ("FAIL could not make a temp dir\n");
		return EXIT_FAILURE;
	}

	sub = g_build_filename (tmp, "plain", NULL);
	g_mkdir (sub, 0755);

	link = g_build_filename (tmp, "share-link", NULL);
	if (!make_link (link, dead)) {
		g_print ("nemo-metadata-ready-win32: skipped, cannot create a link "
			 "(needs Developer Mode or elevation)\n");
		g_rmdir (sub);
		g_rmdir (tmp);
		g_free (link);
		g_free (sub);
		g_free (tmp);
		return 77;
	}

	/* List it the way a view does, and wait for the listing to finish. */
	uri = g_filename_to_uri (tmp, NULL, NULL);
	directory = nemo_directory_get_by_uri (uri);
	nemo_directory_file_monitor_add (directory, &client, TRUE,
					 NEMO_FILE_ATTRIBUTES_FOR_ICON |
					 NEMO_FILE_ATTRIBUTE_DIRECTORY_ITEM_COUNT |
					 NEMO_FILE_ATTRIBUTE_INFO |
					 NEMO_FILE_ATTRIBUTE_LINK_INFO |
					 NEMO_FILE_ATTRIBUTE_MOUNT,
					 NULL, NULL);
	nemo_directory_call_when_ready (directory, NEMO_FILE_ATTRIBUTE_INFO,
					TRUE, ready, NULL);
	run_until_done (60);
	check (nemo_directory_are_all_files_seen (directory));

	/* Now the swap: the same question a second view asks, on a full listing. */
	started = g_get_monotonic_time ();
	nemo_directory_call_when_ready (directory,
					NEMO_FILE_ATTRIBUTE_INFO |
					NEMO_FILE_ATTRIBUTE_MOUNT |
					NEMO_FILE_ATTRIBUTE_FILESYSTEM_INFO,
					FALSE, ready, NULL);
	run_until_done (120);
	seconds = (g_get_monotonic_time () - started) / 1000000.0;

	g_printerr ("view swap waited %.1fs\n", seconds);
	check (seconds < 5.0);

	nemo_directory_file_monitor_remove (directory, &client);
	nemo_directory_unref (directory);

	g_free (uri);
	g_rmdir (link);
	g_free (link);
	g_rmdir (sub);
	g_free (sub);
	g_rmdir (tmp);
	g_free (tmp);

	if (failures == 0)
		g_print ("nemo-metadata-ready-win32: all checks passed\n");

	return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
