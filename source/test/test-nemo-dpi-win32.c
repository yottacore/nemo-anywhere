/* The arithmetic behind following a monitor's DPI. The toolkit scales what it
 * draws by a whole number, so the font DPI has to carry the fraction that whole
 * number could not - and that sum is worth pinning down, because getting it
 * wrong is a window of double-sized type that no build error would catch.
 *
 * Only the sum is here. Which monitor a window is on, and when to ask again,
 * need a screen. */

#include <config.h>

#include <stdlib.h>
#include <glib.h>

#include <src/nemo-dpi-win32.h>
#include "test-check.h"

/* gtk-xft-dpi counts in 1024ths of a dot per inch. */
#define DPI(n) ((n) * 1024)

static void
check_whole_steps (void)
{
	/* Where the scale factor already covers the whole of it, the font DPI
	   does not move from the 96 everything is drawn against. 100% and 200%
	   are the two the toolkit can do on its own. */
	check (nemo_dpi_win32_font_dpi (96, 1) == DPI (96));
	check (nemo_dpi_win32_font_dpi (192, 2) == DPI (96));
	check (nemo_dpi_win32_font_dpi (288, 3) == DPI (96));
}

static void
check_fractions (void)
{
	/* The cases the whole steps cannot reach. At 125% and 150% the toolkit
	   scales by 1, so the whole of it goes to the type. */
	check (nemo_dpi_win32_font_dpi (120, 1) == DPI (120));
	check (nemo_dpi_win32_font_dpi (144, 1) == DPI (144));

	/* And at 175% or 250%, where it scales by 1 or 2, whatever is left over
	   goes to the type. */
	check (nemo_dpi_win32_font_dpi (168, 1) == DPI (168));
	check (nemo_dpi_win32_font_dpi (240, 2) == DPI (120));
}

static void
check_nonsense (void)
{
	/* Nothing on the way in is trusted: a DPI of nothing reads as the
	   standard one, and a scale factor below one would multiply rather than
	   divide. Both come from other people's APIs. */
	check (nemo_dpi_win32_font_dpi (0, 1) == DPI (96));
	check (nemo_dpi_win32_font_dpi (96, 0) == DPI (96));
	check (nemo_dpi_win32_font_dpi (96, -4) == DPI (96));
	check (nemo_dpi_win32_font_dpi (0, 0) == DPI (96));

	/* The answer is always usable as a font DPI - never zero, never
	   negative, whatever it was handed. */
	check (nemo_dpi_win32_font_dpi (1, 64) > 0);
}

int
main (void)
{
	check_whole_steps ();
	check_fractions ();
	check_nonsense ();

	if (failures > 0) {
		g_printerr ("%d check(s) failed\n", failures);
		return EXIT_FAILURE;
	}

	g_print ("OK\n");
	return EXIT_SUCCESS;
}
