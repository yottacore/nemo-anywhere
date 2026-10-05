/* A permanent delete out of the trash asks on the same terms as a direct delete.
 * It used to stop at the trash preference alone, so with that off, a delete over
 * a trash address that came from anything but a delete command went through with
 * no dialog. Its two siblings had always checked both. */

#include <config.h>

#include <stdlib.h>
#include <glib.h>
#include <gtk/gtk.h>

#include <libnemo-private/nemo-config.h>
#include <libnemo-private/nemo-global-preferences.h>
#include <libnemo-private/nemo-file-operations.h>

#include "test-scratch.h"
#include "test-check.h"

int
main (void)
{
	char *tmp;

	tmp = test_scratch_config_home ("nemo-delete-from-trash-XXXXXX");

	nemo_global_preferences_init ();
	nemo_config_set_int (nemo_preferences, NEMO_PREFERENCES_CONFIRM_MANY_ITEMS, 10);

	/* On, and it asks whatever else is true. */
	nemo_config_set_boolean (nemo_preferences, NEMO_PREFERENCES_CONFIRM_TRASH, TRUE);
	check (nemo_file_operations_delete_from_trash_asks (TRUE, 1));
	check (nemo_file_operations_delete_from_trash_asks (FALSE, 1));

	/* Off. The preference is the person's to turn off, and it only covers
	   what they asked for themselves, one item at a time. */
	nemo_config_set_boolean (nemo_preferences, NEMO_PREFERENCES_CONFIRM_TRASH, FALSE);
	check (!nemo_file_operations_delete_from_trash_asks (TRUE, 1));
	check (nemo_file_operations_delete_from_trash_asks (FALSE, 1));
	check (nemo_file_operations_delete_from_trash_asks (TRUE, 10));
	check (nemo_file_operations_delete_from_trash_asks (TRUE, 99));
	check (!nemo_file_operations_delete_from_trash_asks (TRUE, 9));

	/* Nothing to count against means the count cannot make it ask. */
	nemo_config_set_int (nemo_preferences, NEMO_PREFERENCES_CONFIRM_MANY_ITEMS, 0);
	check (!nemo_file_operations_delete_from_trash_asks (TRUE, 99));
	check (nemo_file_operations_delete_from_trash_asks (FALSE, 99));

	g_free (tmp);

	if (failures > 0) {
		g_printerr ("%d failure(s)\n", failures);
		return EXIT_FAILURE;
	}

	g_print ("OK\n");
	return EXIT_SUCCESS;
}
