/* An extension column must survive its refcount reaching zero. The private
 * block is type-system-owned; finalize freeing it aborts glibc, which is
 * exactly what this exercises - the process dying IS the failure. */

#include <config.h>

#include <glib.h>
#include <libnemo-extension/nemo-column.h>

int
main (void)
{
	NemoColumn *column;
	gpointer    alive;

	column = g_object_new (NEMO_TYPE_COLUMN,
	                       "name", "test",
	                       "attribute", "test::attr",
	                       "label", "Test",
	                       "description", "A column that gets dropped",
	                       NULL);
	alive = column;
	g_object_add_weak_pointer (G_OBJECT (column), &alive);
	g_object_unref (column);

	if (alive != NULL) {
		g_printerr ("FAIL: column not finalized\n");
		return 1;
	}

	g_print ("nemo-column: all checks passed\n");
	return 0;
}
