/* One name for a drive root, shared by every surface that shows one. gio calls
 * it "\" (the basename), the volume monitor calls it "(C:) Windows" and the
 * sidebar used to build "Windows (C:)" - three names for the same place, none
 * of which is what a user would type. Windows-only. */

#include <config.h>

#include <stdlib.h>
#include <string.h>
#include <gio/gio.h>

#include <libnemo-private/nemo-file-utilities.h>

#include "test-scratch.h"
#include "test-check.h"

/* Name of the location @uri points at, or NULL if it is not a drive root. */
static char *
name_for_uri (const char *uri)
{
	GFile *file = g_file_new_for_uri (uri);
	char *name = nemo_get_drive_root_name (file);

	g_object_unref (file);
	return name;
}

static gboolean
is_root_uri (const char *uri)
{
	GFile *file = g_file_new_for_uri (uri);
	gboolean is_root = nemo_location_is_drive_root (file);

	g_object_unref (file);
	return is_root;
}

int
main (void)
{
	char *name;
	char *tmp;

	/* A throwaway config root, or the separator the real user chose decides
	 * what a drive root is called and the spellings below fail. */
	tmp = test_scratch_config_home ("nemo-drive-root-test-XXXXXX");

	/* The whole point: a drive root is named by its letter, with the
	 * trailing separator, so it reads as a path and not as a bare word. */
	name = name_for_uri ("file:///C:/");
	check (g_strcmp0 (name, "C:\\") == 0);
	g_free (name);

	/* Any letter, not just the one this box happens to boot from. */
	name = name_for_uri ("file:///K:/");
	check (g_strcmp0 (name, "K:\\") == 0);
	g_free (name);

	/* Case is normalized up, so two spellings of one drive cannot show as
	 * two differently-named places. */
	name = name_for_uri ("file:///k:/");
	check (g_strcmp0 (name, "K:\\") == 0);
	g_free (name);

	/* Anything inside the drive keeps its own name - the override must not
	 * swallow the first level down, which is what a length-blind check on
	 * "starts with a letter and a colon" would do. */
	check (!is_root_uri ("file:///C:/Windows"));
	check (name_for_uri ("file:///C:/Windows") == NULL);

	check (!is_root_uri ("file:///C:/W"));
	check (name_for_uri ("file:///C:/W") == NULL);

	/* Non-native locations have no drive letter to report. */
	check (!is_root_uri ("trash:///"));
	check (name_for_uri ("trash:///") == NULL);
	check (!is_root_uri ("network:///"));

	/* A UNC share root is not a drive root: it has no letter, and naming it
	 * after one would be wrong rather than merely unhelpful. */
	check (!is_root_uri ("file://server/share"));
	check (name_for_uri ("file://server/share") == NULL);

	/* Both separators reach the same place, so both must be recognized -
	 * nemo builds "file:///C:/" itself while a typed path arrives as C:\. */
	check (is_root_uri ("file:///C:/"));
	{
		GFile *back = g_file_new_for_path ("C:\\");
		char *from_path = nemo_get_drive_root_name (back);

		check (nemo_location_is_drive_root (back));
		check (g_strcmp0 (from_path, "C:\\") == 0);

		g_free (from_path);
		g_object_unref (back);
	}

	/* NULL must be answered, not crashed on: callers ask about a location
	 * they have not established is real yet. */
	check (!nemo_location_is_drive_root (NULL));
	check (nemo_get_drive_root_name (NULL) == NULL);

	g_free (tmp);

	if (failures == 0) {
		g_print ("drive-root-name: all checks passed\n");
	}
	return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
