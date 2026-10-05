/* Icon sizes are pixels rather than one of seven named levels. The check that
 * matters most is the last one: a folder's size was saved as a level number
 * until 2026-09, and reading one of those as a pixel count would show every
 * remembered folder at a three pixel icon. */

#include <config.h>

#include <gtk/gtk.h>

#include <libnemo-private/nemo-icon-info.h>

#include "test-check.h"

int
main (void)
{
	const gint *steps;
	guint n_steps, i;
	gint size;

	steps = nemo_icon_size_steps (&n_steps);

	check (n_steps > 1);
	check (steps[0] == NEMO_ICON_SIZE_MIN);
	check (steps[n_steps - 1] == NEMO_ICON_SIZE_MAX);

	for (i = 1; i < n_steps; i++) {
		check (steps[i] > steps[i - 1]);
	}

	/* Stepping walks the stops and stays put at either end. */
	check (nemo_icon_size_step (NEMO_ICON_SIZE_STANDARD, 1) == NEMO_ICON_SIZE_LARGE);
	check (nemo_icon_size_step (NEMO_ICON_SIZE_STANDARD, -1) == NEMO_ICON_SIZE_SMALL);
	check (nemo_icon_size_step (NEMO_ICON_SIZE_MAX, 1) == NEMO_ICON_SIZE_MAX);
	check (nemo_icon_size_step (NEMO_ICON_SIZE_MIN, -1) == NEMO_ICON_SIZE_MIN);

	/* A size between two stops steps to the stop either side of it, not to
	   the one it is nearest. */
	check (nemo_icon_size_step (56, 1) == NEMO_ICON_SIZE_STANDARD);
	check (nemo_icon_size_step (56, -1) == NEMO_ICON_SIZE_SMALL);

	check (nemo_icon_size_clamp (2) == NEMO_ICON_SIZE_MIN);
	check (nemo_icon_size_clamp (99999) == NEMO_ICON_SIZE_MAX);

	/* The slider's scale. A stop sits on a whole number, and half way
	   between two of them is half way between their sizes. */
	for (i = 0; i < n_steps; i++) {
		check (nemo_icon_size_position (steps[i]) == (gdouble) i);
		check (nemo_icon_size_at_position ((gdouble) i) == steps[i]);
	}
	check (nemo_icon_size_at_position (3.5) ==
	       (NEMO_ICON_SIZE_STANDARD + NEMO_ICON_SIZE_LARGE) / 2);
	check (nemo_icon_size_at_position (-5.0) == NEMO_ICON_SIZE_MIN);
	check (nemo_icon_size_at_position (999.0) == NEMO_ICON_SIZE_MAX);

	/* Per cent, which is what both settings and the preferences window use. */
	check (nemo_icon_size_from_percent (100) == NEMO_ICON_SIZE_STANDARD);
	check (nemo_icon_size_from_percent (500) == NEMO_ICON_SIZE_IMAGES);
	check (nemo_icon_size_from_percent (1000) == NEMO_ICON_SIZE_MAX);
	check (nemo_icon_size_percent (NEMO_ICON_SIZE_STANDARD) == 100);
	check (nemo_icon_size_percent (NEMO_ICON_SIZE_IMAGES) == 500);

	/* A saved value of 0 to 6 is one of the old levels. Anything a real size
	   could be must not be read as one. */
	check (nemo_icon_size_is_legacy_level (0));
	check (nemo_icon_size_is_legacy_level (6));
	check (!nemo_icon_size_is_legacy_level (7));
	check (!nemo_icon_size_is_legacy_level (-1));
	for (i = 0; i < n_steps; i++) {
		check (!nemo_icon_size_is_legacy_level (steps[i]));
	}
	check (!nemo_icon_size_is_legacy_level (NEMO_ICON_SIZE_MIN));

	check (nemo_icon_size_from_legacy_level (0) == NEMO_ICON_SIZE_SMALLEST);
	check (nemo_icon_size_from_legacy_level (3) == NEMO_ICON_SIZE_STANDARD);
	check (nemo_icon_size_from_legacy_level (6) == NEMO_ICON_SIZE_LARGEST);
	check (nemo_list_icon_size_from_legacy_level (2) == NEMO_LIST_ICON_SIZE_SMALL);

	/* The list view is held to those same steps, and the size it shows for
	   each one has to be what it always was. */
	for (i = 0; i < 7; i++) {
		size = nemo_icon_size_from_legacy_level (i);
		check (nemo_icon_size_legacy_level (size) == (gint) i);
		check ((gint) nemo_get_list_icon_size (size) == nemo_list_icon_size_from_legacy_level (i));
	}

	/* A size between two steps belongs to the step at or above it. */
	check (nemo_icon_size_legacy_level (56) == 3);
	check (nemo_get_list_icon_size (56) == NEMO_LIST_ICON_SIZE_STANDARD);

	/* Past the old top level, everything answers as the top one. */
	check (nemo_icon_size_legacy_level (NEMO_ICON_SIZE_MAX) == 6);
	check (nemo_get_list_icon_size (NEMO_ICON_SIZE_MAX) == NEMO_LIST_ICON_SIZE_LARGEST);

	/* The desktop covers a narrower range than the rest and always did. */
	check (nemo_get_desktop_icon_size (NEMO_ICON_SIZE_MIN) == NEMO_DESKTOP_ICON_SIZE_SMALLER);
	check (nemo_get_desktop_icon_size (NEMO_ICON_SIZE_MAX) == NEMO_DESKTOP_ICON_SIZE_LARGER);
	check (nemo_get_desktop_text_width (NEMO_ICON_SIZE_MIN) >= NEMO_DESKTOP_TEXT_WIDTH_SMALLER);

	if (failures == 0)
		g_print ("nemo-icon-size: all checks passed\n");

	return failures == 0 ? 0 : 1;
}
