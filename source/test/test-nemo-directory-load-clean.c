/* Listing a plain folder should not log anything at critical or warning level.
 * A file's name is not filled in until late in the same update that first
 * applies its info, so anything reading the name earlier - the drive-root
 * naming did - builds a location out of NULL and gets a critical per file. */

#include <config.h>

#include <stdlib.h>
#include <glib/gstdio.h>
#include <gtk/gtk.h>

#include <libnemo-private/nemo-directory.h>
#include <libnemo-private/nemo-file.h>

#include "test-scratch.h"
#include "test-check.h"

static int logged = 0;

static GLogWriterOutput
counting_writer (GLogLevelFlags   level,
		 const GLogField *fields,
		 gsize            n_fields,
		 gpointer         user_data)
{
	gsize i;

	if (level & (G_LOG_LEVEL_CRITICAL | G_LOG_LEVEL_WARNING)) {
		logged++;
		for (i = 0; i < n_fields; i++) {
			if (g_strcmp0 (fields[i].key, "MESSAGE") == 0) {
				g_printerr ("  logged: %s\n", (const char *) fields[i].value);
			}
		}
	}

	return g_log_writer_default (level, fields, n_fields, user_data);
}

static gboolean done = FALSE;

static void
directory_ready (G_GNUC_UNUSED NemoDirectory *directory,
		 GList         *files,
		 G_GNUC_UNUSED gpointer       callback_data)
{
	check (g_list_length (files) == 8);
	done = TRUE;
}

#ifdef G_OS_WIN32
static void
root_ready (G_GNUC_UNUSED NemoDirectory *directory,
	    G_GNUC_UNUSED GList         *files,
	    G_GNUC_UNUSED gpointer       callback_data)
{
	done = TRUE;
}
#endif

static gboolean
give_up (G_GNUC_UNUSED gpointer data)
{
	g_printerr ("FAIL directory never became ready\n");
	failures++;
	done = TRUE;
	return G_SOURCE_REMOVE;
}

int
main (int argc, char **argv)
{
	char *tmp, *uri;
	NemoDirectory *directory;
	guint timeout;
	int i;

	/* With no monitor GDK logs criticals of its own, which this would count. */
	if (gtk_init_check (&argc, &argv) &&
	    gdk_display_get_n_monitors (gdk_display_get_default ()) == 0) {
		g_print ("SKIP: no monitor\n");
		return 77;
	}

	tmp = test_scratch_dir ("nemo-dirload-XXXXXX", NULL);
	if (tmp == NULL) {
		g_printerr ("FAIL could not make a temp dir\n");
		return EXIT_FAILURE;
	}
	for (i = 0; i < 8; i++) {
		char *path = g_strdup_printf ("%s/file-%d.txt", tmp, i);
		check (g_file_set_contents (path, "x\n", 2, NULL));
		g_free (path);
	}

	/* Installed after the temp files, so nothing before the listing counts.
	   Glib only allows this once, so there is no putting it back afterwards. */
	g_log_set_writer_func (counting_writer, NULL, NULL);

	uri = g_filename_to_uri (tmp, NULL, NULL);
	directory = nemo_directory_get_by_uri (uri);
	nemo_directory_call_when_ready (directory,
					NEMO_FILE_ATTRIBUTE_INFO,
					TRUE,
					directory_ready,
					NULL);

	timeout = g_timeout_add_seconds (20, give_up, NULL);
	while (!done) {
		g_main_context_iteration (NULL, TRUE);
	}
	g_source_remove (timeout);

	check (logged == 0);

	nemo_directory_unref (directory);
	g_free (uri);

#ifdef G_OS_WIN32
	/* A drive root holds entries Windows will not stat - the page and swap
	   files - and their info comes back with no type on it at all. */
	{
		char *root = g_strndup (tmp, 3);   /* "C:/" */

		done = FALSE;
		uri = g_filename_to_uri (root, NULL, NULL);
		directory = nemo_directory_get_by_uri (uri);
		nemo_directory_call_when_ready (directory,
						NEMO_FILE_ATTRIBUTE_INFO,
						TRUE,
						root_ready,
						NULL);

		timeout = g_timeout_add_seconds (30, give_up, NULL);
		while (!done) {
			g_main_context_iteration (NULL, TRUE);
		}
		g_source_remove (timeout);

		check (logged == 0);

		nemo_directory_unref (directory);
		g_free (uri);
		g_free (root);
	}
#endif

	for (i = 0; i < 8; i++) {
		char *path = g_strdup_printf ("%s/file-%d.txt", tmp, i);
		g_unlink (path);
		g_free (path);
	}
	g_rmdir (tmp);
	g_free (tmp);

	if (failures == 0)
		g_print ("nemo-directory-load-clean: all checks passed\n");

	return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
