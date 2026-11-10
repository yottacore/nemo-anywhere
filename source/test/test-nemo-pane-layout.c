/* How a divider moves when the window changes width. Pure arithmetic, so all of
 * it is checkable without a screen - which matters, because what this guards
 * against is a side pane that creeps wider every time the window is dragged, or
 * one squeezed to nothing on a narrow window, and neither shows up elsewhere. */

#include <config.h>

#include <stdlib.h>
#include <glib.h>

#include <src/nemo-pane-layout.h>
#include "test-check.h"

#define FLOOR 40

/* The whole point: the share of the width stays put. */
static void
check_keeps_its_share (void)
{
	/* A third of the width, at 3 different widths. */
	check (nemo_pane_layout_scale_position (300, 900, 1800, FLOOR) == 600);
	check (nemo_pane_layout_scale_position (300, 900, 450, FLOOR) == 150);
	check (nemo_pane_layout_scale_position (600, 1800, 900, FLOOR) == 300);

	/* Half stays half. */
	check (nemo_pane_layout_scale_position (500, 1000, 1280, FLOOR) == 640);

	/* A width that divides into nothing round still comes within a pixel. */
	check (nemo_pane_layout_scale_position (240, 1278, 1000, FLOOR) == 188);
}

/* Nothing to do when the window's width didn't change. */
static void
check_same_width_is_left_alone (void)
{
	check (nemo_pane_layout_scale_position (240, 1278, 1278, FLOOR) == 240);
	check (nemo_pane_layout_scale_position (41, 1278, 1278, FLOOR) == 41);
}

/* Neither side is allowed to disappear. */
static void
check_floor_holds (void)
{
	/* A narrow pane shrinks to the floor and stops. */
	check (nemo_pane_layout_scale_position (50, 1000, 200, FLOOR) == FLOOR);

	/* The far side gets the same protection. */
	check (nemo_pane_layout_scale_position (950, 1000, 200, FLOOR) == 200 - FLOOR);

	/* With no room for both floors, split it down the middle rather than
	   pick a side. */
	check (nemo_pane_layout_scale_position (30, 1000, 60, FLOOR) == 30);

	/* A floor of zero is allowed and means no protection at all. */
	check (nemo_pane_layout_scale_position (1, 1000, 100, 0) == 0);
}

/* A width nobody can scale against comes back untouched rather than guessed. */
static void
check_degenerate_widths (void)
{
	check (nemo_pane_layout_scale_position (240, 0, 1000, FLOOR) == 240);
	check (nemo_pane_layout_scale_position (240, -5, 1000, FLOOR) == 240);
	check (nemo_pane_layout_scale_position (240, 1000, 0, FLOOR) == 240);
	check (nemo_pane_layout_scale_position (240, 1000, -5, FLOOR) == 240);
}

/* Odd inputs give something sane instead of something wild. */
static void
check_out_of_range_position (void)
{
	/* Past the end of the old paned, so it clamps inside the new one. */
	check (nemo_pane_layout_scale_position (5000, 1000, 800, FLOOR) == 800 - FLOOR);

	/* Negative reads as zero, then the floor applies. */
	check (nemo_pane_layout_scale_position (-100, 1000, 800, FLOOR) == FLOOR);
}

/* The multiply is the one place this could quietly go wrong. */
static void
check_no_overflow (void)
{
	int wide = 1000000;
	int got = nemo_pane_layout_scale_position (wide / 2, wide, wide * 2, FLOOR);

	check (got == wide);
}

/* Widen then narrow by the same factor and the divider comes home. Rounding
   means "close enough", not "exact", so allow a pixel either way. */
static void
check_round_trip (void)
{
	int start = 317;
	int out = nemo_pane_layout_scale_position (start, 1278, 1917, FLOOR);
	int back = nemo_pane_layout_scale_position (out, 1917, 1278, FLOOR);

	check (back >= start - 1 && back <= start + 1);
}

int
main (int argc, char *argv[])
{
	(void) argc;
	(void) argv;

	check_keeps_its_share ();
	check_same_width_is_left_alone ();
	check_floor_holds ();
	check_degenerate_widths ();
	check_out_of_range_position ();
	check_no_overflow ();
	check_round_trip ();

	if (failures > 0) {
		g_printerr ("%d check(s) failed\n", failures);
		return 1;
	}

	return 0;
}
