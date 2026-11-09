/* The owner loaded on its own, as on Windows, where each owner or group name
 * is a call to the local security service. The default info load leaves
 * owner::* out, a file's owner comes only when NEMO_FILE_ATTRIBUTE_OWNER is
 * asked for, and an info reload keeps the names it had. Run off Windows too,
 * with the switch the test setter gives, so the load is checked on every
 * platform. */

#include <config.h>

#include <stdlib.h>
#include <string.h>
#include <gtk/gtk.h>

#include <libnemo-private/nemo-file.h>
#include <libnemo-private/nemo-file-attributes.h>
#include <libnemo-private/nemo-file-private.h>
#include "test-check.h"
#include "test-scratch.h"

#define WAIT_SECONDS 20

static void
ready_callback (NemoFile *file, gpointer data)
{
	(void) file;
	*(gboolean *) data = TRUE;
}

static gboolean
wait_ready (NemoFile *file, NemoFileAttributes attributes, const char *what)
{
	gint64 give_up_at = g_get_monotonic_time () + WAIT_SECONDS * G_USEC_PER_SEC;
	gboolean ready = FALSE;

	nemo_file_call_when_ready (file, attributes, ready_callback, &ready);
	while (!ready) {
		if (g_get_monotonic_time () > give_up_at) {
			g_printerr ("FAIL: %s did not load within %d seconds\n", what, WAIT_SECONDS);
			failures++;
			nemo_file_cancel_call_when_ready (file, ready_callback, &ready);
			return FALSE;
		}
		g_main_context_iteration (NULL, TRUE);
	}

	return TRUE;
}

static gboolean
has_owner_name (NemoFile *file)
{
	g_autofree char *owner = nemo_file_get_owner_name (file);

	return owner != NULL && owner[0] != '\0';
}

int
main (int argc, char *argv[])
{
	g_autoptr(GError) error = NULL;
	g_autofree char *dir = NULL;
	g_autofree char *path = NULL;
	g_autofree char *owner = NULL;
	GFile *location;
	NemoFile *file;

	/* Before any file is loaded. */
	nemo_file_set_owner_comes_with_info_for_test (FALSE);

	/* NemoFile's class setup watches the icon theme, which wants a screen. */
	if (!gtk_init_check (&argc, &argv)) {
		g_print ("SKIP: no display\n");
		return 77;
	}

	check (strstr (nemo_file_default_attributes (), "owner::") == NULL);
	check (strstr (nemo_file_default_attributes (), "standard::*") != NULL);
	check (strstr (nemo_file_default_attributes (), "preview::icon") != NULL);

	dir = test_scratch_dir ("nemo-owner-load-XXXXXX", &error);
	if (dir == NULL) {
		g_printerr ("FAIL: no scratch dir: %s\n", error->message);
		return EXIT_FAILURE;
	}
	path = g_build_filename (dir, "mine.txt", NULL);
	if (!g_file_set_contents (path, "x", 1, &error)) {
		g_printerr ("FAIL: cannot write %s: %s\n", path, error->message);
		return EXIT_FAILURE;
	}

	location = g_file_new_for_path (path);
	file = nemo_file_get (location);

	/* The info alone names no owner. */
	if (wait_ready (file, NEMO_FILE_ATTRIBUTE_INFO, "the info")) {
		check (!nemo_file_is_gone (file));
		check (nemo_file_get_size (file) == 1);
		check (!has_owner_name (file));
		check (nemo_file_get_group_name (file) == NULL);
	}

	/* Asked for, it comes. */
	if (wait_ready (file, NEMO_FILE_ATTRIBUTE_OWNER, "the owner")) {
		check (has_owner_name (file));
		owner = nemo_file_get_owner_name (file);
#ifndef G_OS_WIN32
		g_autofree char *group = nemo_file_get_group_name (file);

		check (g_strcmp0 (owner, g_get_user_name ()) == 0);
		check (group != NULL);
#endif
	}

	/* A fresh info load keeps the names up, but they are read again when next
	   asked for, since the owner may have changed with the file. */
	nemo_file_invalidate_attributes (file, NEMO_FILE_ATTRIBUTE_INFO);
	if (wait_ready (file, NEMO_FILE_ATTRIBUTE_INFO, "the info again")) {
		g_autofree char *kept = nemo_file_get_owner_name (file);

		check (g_strcmp0 (kept, owner) == 0);
		check (!file->details->owner_is_up_to_date);
	}
	if (wait_ready (file, NEMO_FILE_ATTRIBUTE_OWNER, "the owner again")) {
		g_autofree char *again = nemo_file_get_owner_name (file);

		check (file->details->owner_is_up_to_date);
		check (g_strcmp0 (again, owner) == 0);
	}

	nemo_file_unref (file);
	g_object_unref (location);

	if (failures > 0) {
		g_printerr ("%d check(s) failed\n", failures);
		return EXIT_FAILURE;
	}

	g_print ("OK\n");
	return EXIT_SUCCESS;
}
