/* How the list view divides its width between columns. Pure arithmetic, so all
 * of it is checkable without a screen - which matters, because the failure this
 * guards against is a column crushed to nothing, or a strip of dead space after
 * the last one, and neither shows up in any other test. */

#include <config.h>

#include <stdlib.h>
#include <glib.h>

#include <src/nemo-column-layout.h>
#include "test-check.h"

enum { NAME, SIZE, TYPE, DATE, N_COLS };

/* A plausible row: Name, Size, Type, Date Modified. Size and Date are fixed at
   the width their widest value needs. Name shows most names at 300 and all of
   them at 500. Type shows most at 120, all at 200, and may go down to 60. */
static void
usual_columns (NemoColumnLayoutItem *items)
{
	items[NAME] = (NemoColumnLayoutItem) { 300, 300, 500, TRUE  };
	items[SIZE] = (NemoColumnLayoutItem) {  80,  80,  80, FALSE };
	items[TYPE] = (NemoColumnLayoutItem) {  60, 120, 200, FALSE };
	items[DATE] = (NemoColumnLayoutItem) { 160, 160, 160, FALSE };
}

#define SUM_MIN (300 + 80 + 60 + 160)
#define SUM_FIT (300 + 80 + 120 + 160)
#define SUM_MAX (500 + 80 + 200 + 160)

static int
total (const int *widths, int n)
{
	int sum = 0, i;

	for (i = 0; i < n; i++) {
		sum += widths[i];
	}

	return sum;
}

/* Wherever the minimums allow it, nothing is left over and nothing hangs off
   the end. */
static void
check_fills_the_width (void)
{
	NemoColumnLayoutItem items[N_COLS];
	int widths[N_COLS];
	int available;

	usual_columns (items);

	for (available = SUM_MIN; available <= 2000; available += 7) {
		nemo_column_layout_distribute (items, N_COLS, available, widths);
		check (total (widths, N_COLS) == available);
	}
}

/* Room to spare: every column is at its most and Name has the rest. */
static void
check_wide (void)
{
	NemoColumnLayoutItem items[N_COLS];
	int widths[N_COLS];

	usual_columns (items);
	nemo_column_layout_distribute (items, N_COLS, 1400, widths);

	check (widths[SIZE] == 80);
	check (widths[DATE] == 160);
	check (widths[TYPE] == 200);
	check (widths[NAME] == 1400 - 80 - 200 - 160);
}

/* Between showing most and showing all, Name and Type grow together, Name the
   faster for being the wider; the fixed columns do not move. */
static void
check_grows_in_proportion (void)
{
	NemoColumnLayoutItem items[N_COLS];
	int widths[N_COLS];

	usual_columns (items);
	nemo_column_layout_distribute (items, N_COLS, SUM_FIT + 140, widths);

	check (widths[SIZE] == 80);
	check (widths[DATE] == 160);
	/* 140 shared 300:120 */
	check (widths[NAME] == 400);
	check (widths[TYPE] == 160);
	check (total (widths, N_COLS) == SUM_FIT + 140);
}

/* A column that reaches the width showing everything stops, and the others
   carry on with its share. */
static void
check_stops_at_max (void)
{
	NemoColumnLayoutItem items[N_COLS];
	int widths[N_COLS];

	usual_columns (items);
	nemo_column_layout_distribute (items, N_COLS, SUM_FIT + 400, widths);

	check (widths[TYPE] == 200);
	check (widths[NAME] == SUM_FIT + 400 - 80 - 200 - 160);
	check (total (widths, N_COLS) == SUM_FIT + 400);
}

/* Short of the defaults: only a column with a smaller minimum gives, and only
   down to it. */
static void
check_type_gives_alone (void)
{
	NemoColumnLayoutItem items[N_COLS];
	int widths[N_COLS];

	usual_columns (items);
	nemo_column_layout_distribute (items, N_COLS, SUM_FIT - 40, widths);

	check (widths[NAME] == 300);
	check (widths[SIZE] == 80);
	check (widths[DATE] == 160);
	check (widths[TYPE] == 80);
	check (total (widths, N_COLS) == SUM_FIT - 40);
}

/* Two columns with room below their fit give in proportion to their size. */
static void
check_shrinks_in_proportion (void)
{
	NemoColumnLayoutItem items[N_COLS];
	int widths[N_COLS];

	usual_columns (items);
	items[DATE].min_width = 80;	/* pretend Date could give too */

	/* 70 to give back, shared 120:160 between Type and Date */
	nemo_column_layout_distribute (items, N_COLS, SUM_FIT - 70, widths);

	check (widths[NAME] == 300);
	check (widths[SIZE] == 80);
	check (widths[TYPE] == 90);
	check (widths[DATE] == 120);
	check (total (widths, N_COLS) == SUM_FIT - 70);
}

/* Narrower than the minimums add up to. Nothing goes below its minimum, nothing
   comes back negative, and the row is wider than the window. */
static void
check_overflows (void)
{
	NemoColumnLayoutItem items[N_COLS];
	int widths[N_COLS];
	int i;

	usual_columns (items);
	nemo_column_layout_distribute (items, N_COLS, 400, widths);

	for (i = 0; i < N_COLS; i++) {
		check (widths[i] == items[i].min_width);
	}

	check (total (widths, N_COLS) == SUM_MIN);
	check (total (widths, N_COLS) > 400);

	nemo_column_layout_distribute (items, N_COLS, 0, widths);
	check (total (widths, N_COLS) == SUM_MIN);
}

/* One column on its own is still the whole width, down to its minimum. */
static void
check_single_column (void)
{
	NemoColumnLayoutItem only = { 100, 200, 300, TRUE };
	int width = 0;

	nemo_column_layout_distribute (&only, 1, 900, &width);
	check (width == 900);

	nemo_column_layout_distribute (&only, 1, 150, &width);
	check (width == 150);

	nemo_column_layout_distribute (&only, 1, 50, &width);
	check (width == 100);
}

/* The three widths out of order: the larger wins, and nothing breaks. */
static void
check_widths_out_of_order (void)
{
	NemoColumnLayoutItem items[2] = {
		{ 100, 300, 250, TRUE  },	/* max under fit */
		{ 120,  20,  10, FALSE }		/* fit and max under min */
	};
	int widths[2];

	nemo_column_layout_distribute (items, 2, 1000, widths);
	check (widths[1] == 120);
	check (widths[0] == 880);

	nemo_column_layout_distribute (items, 2, 100, widths);
	check (widths[0] == 100);
	check (widths[1] == 120);
}

/* With no column taking the surplus, the row ends short. */
static void
check_no_grower (void)
{
	NemoColumnLayoutItem items[2] = {
		{ 100, 100, 100, FALSE },
		{ 100, 100, 100, FALSE }
	};
	int widths[2];

	nemo_column_layout_distribute (items, 2, 1000, widths);
	check (widths[0] == 100);
	check (widths[1] == 100);
}

/* Name and Location both on the row: past every max they share what is left
   in proportion to their size. Below that they grow like any other pair. */
enum { P_NAME, P_LOC, P_SIZE, N_PAIR };

static void
check_primaries_share_the_surplus (void)
{
	NemoColumnLayoutItem items[N_PAIR] = {
		{ 300, 300, 500, TRUE  },
		{ 200, 200, 400, TRUE  },
		{  80,  80,  80, FALSE }
	};
	int widths[N_PAIR];

	/* 420 past every max, shared 300:200 */
	nemo_column_layout_distribute (items, N_PAIR, 1400, widths);
	check (widths[P_NAME] == 500 + 252);
	check (widths[P_LOC] == 400 + 168);
	check (widths[P_SIZE] == 80);
	check (total (widths, N_PAIR) == 1400);

	nemo_column_layout_distribute (items, N_PAIR, 580 + 100, widths);
	check (widths[P_NAME] == 360);
	check (widths[P_LOC] == 240);
	check (widths[P_SIZE] == 80);

	/* Location alone takes it all when Name does not grow. */
	items[P_NAME].grows = FALSE;
	nemo_column_layout_distribute (items, N_PAIR, 1400, widths);
	check (widths[P_NAME] == 500);
	check (widths[P_LOC] == 1400 - 500 - 80);
}

/* The width that shows a share of the values. */
static void
check_fit (void)
{
	int ten[10] = { 50, 10, 20, 30, 40, 60, 70, 80, 90, 900 };
	int three[3] = { 30, 10, 20 };
	int one[1] = { 42 };

	/* The nine that are not the outlier fit at 90; the outlier needs 900. */
	check (nemo_column_layout_fit (ten, 10, 90) == 90);
	check (nemo_column_layout_fit (ten, 10, 100) == 900);
	check (nemo_column_layout_fit (ten, 10, 50) == 50);
	check (nemo_column_layout_fit (ten, 10, 1) == 10);

	/* The share is rounded down, to no fewer than one value: 90 percent of
	   three values is two, and 99 percent of ten is nine. */
	check (nemo_column_layout_fit (three, 3, 90) == 20);
	check (nemo_column_layout_fit (three, 3, 34) == 10);
	check (nemo_column_layout_fit (ten, 10, 99) == 90);
	check (nemo_column_layout_fit (one, 1, 10) == 42);

	check (nemo_column_layout_fit (NULL, 0, 90) == 0);
	check (nemo_column_layout_fit (ten, 0, 90) == 0);

	/* Out of range reads as the nearest end. */
	check (nemo_column_layout_fit (ten, 10, 500) == 900);
	check (nemo_column_layout_fit (ten, 10, -5) == 10);

	/* The caller's array is left alone. */
	check (ten[0] == 50 && ten[9] == 900);
}

/* One column's three widths from what was measured, by class, as design.md's
   "List view column widths" sets them. Air 8, ellipsis 12, and a minor column
   of 30 or less goes without an ellipsis. */
#define PAD 8
#define ELLIPSIS 12
#define NARROW 30

static NemoColumnLayoutItem
item_for (NemoColumnKind kind, int fit, int half, int widest, int heading, int dragged)
{
	NemoColumnMeasure measure = { fit, half, widest, heading };
	NemoColumnLayoutItem item;

	nemo_column_layout_item_for_kind (kind, &measure, PAD, ELLIPSIS, NARROW, dragged, &item);
	return item;
}

static gboolean
item_is (NemoColumnLayoutItem item, int min, int fit, int max, gboolean grows)
{
	if (item.min_width == min && item.fit_width == fit && item.max_width == max &&
	    item.grows == grows) {
		return TRUE;
	}
	g_printerr ("  got %d/%d/%d grows %d, wanted %d/%d/%d grows %d\n",
		    item.min_width, item.fit_width, item.max_width, item.grows,
		    min, fit, max, grows);
	return FALSE;
}

static void
check_classes (void)
{
	/* Name: shows every value when there is room and takes a share of the
	   rest; at least the fit share, with room for an ellipsis. */
	check (item_is (item_for (NEMO_COLUMN_KIND_PRIMARY, 200, 120, 400, 40, -1),
			200 + ELLIPSIS + PAD, 400 + PAD, 400 + PAD, TRUE));
	/* Every value fits in the share, so no ellipsis is needed. */
	check (item_is (item_for (NEMO_COLUMN_KIND_PRIMARY, 200, 120, 200, 40, -1),
			200 + PAD, 200 + PAD, 200 + PAD, TRUE));
	/* Short names get a short column: no fixed floor, only the heading. */
	check (item_is (item_for (NEMO_COLUMN_KIND_PRIMARY, 10, 10, 10, 20, -1),
			20, 20, 20, TRUE));

	/* A minor column such as Type: half the values at least, the fit share
	   by default, all of them at most, and never takes the surplus. */
	check (item_is (item_for (NEMO_COLUMN_KIND_MINOR, 90, 60, 150, 30, -1),
			60 + ELLIPSIS + PAD, 90 + PAD, 150 + PAD, FALSE));
	/* Ext: too narrow for an ellipsis to leave anything to read. */
	check (item_is (item_for (NEMO_COLUMN_KIND_MINOR, 24, 18, 28, 0, -1),
			18 + PAD, 24 + PAD, 28 + PAD, FALSE));

	/* A date: always whole, never more or less. */
	check (item_is (item_for (NEMO_COLUMN_KIND_FIXED, 100, 100, 140, 30, -1),
			140 + PAD, 140 + PAD, 140 + PAD, FALSE));

	/* A drag pins primary and minor columns, and a fixed one ignores it. */
	check (item_is (item_for (NEMO_COLUMN_KIND_PRIMARY, 200, 120, 400, 40, 333),
			333, 333, 333, FALSE));
	check (item_is (item_for (NEMO_COLUMN_KIND_MINOR, 90, 60, 150, 30, 77),
			77, 77, 77, FALSE));
	check (item_is (item_for (NEMO_COLUMN_KIND_FIXED, 100, 100, 140, 30, 77),
			140 + PAD, 140 + PAD, 140 + PAD, FALSE));

	/* The heading beats everything, a drag included. */
	check (item_is (item_for (NEMO_COLUMN_KIND_MINOR, 90, 60, 150, 120, 77),
			120, 120, 120, FALSE));
	check (item_is (item_for (NEMO_COLUMN_KIND_FIXED, 40, 40, 40, 120, -1),
			120, 120, 120, FALSE));
	check (item_is (item_for (NEMO_COLUMN_KIND_MINOR, 90, 60, 150, 100, -1),
			100, 100, 150 + PAD, FALSE));
}

/* Feeds a long name repeated down `repeats` rows, each a row of its own, then
   ten short names, and returns the 90 percent fit. */
static int
fit_of_repeated_name (NemoColumnTally *tally, int repeats)
{
	char text[16];
	int fit, half, widest;
	int i;

	for (i = 0; i < repeats; i++) {
		nemo_column_tally_note (tally, GINT_TO_POINTER (i + 1), "a-long-name-everywhere", 400);
	}
	for (i = 0; i < 10; i++) {
		g_snprintf (text, sizeof text, "n%d", i);
		nemo_column_tally_note (tally, GINT_TO_POINTER (repeats + i + 1), text, 100 + i * 10);
	}

	nemo_column_tally_measure (tally, 90, &fit, &half, &widest);
	check (widest == 400);

	return fit;
}

/* Which values count, and how many times. A folder can't hold one name twice,
   so there Name counts per row. Find results can, once per folder it turns up
   in, and then each distinct name counts once, like Location. */
static void
check_tally_modes (void)
{
	NemoColumnTally *tally;

	check (nemo_column_tally_mode_for (NEMO_COLUMN_KIND_PRIMARY, TRUE, FALSE) == NEMO_COLUMN_TALLY_EACH_ROW);
	check (nemo_column_tally_mode_for (NEMO_COLUMN_KIND_PRIMARY, TRUE, TRUE) == NEMO_COLUMN_TALLY_EACH_TEXT);
	check (nemo_column_tally_mode_for (NEMO_COLUMN_KIND_PRIMARY, FALSE, FALSE) == NEMO_COLUMN_TALLY_EACH_TEXT);
	check (nemo_column_tally_mode_for (NEMO_COLUMN_KIND_PRIMARY, FALSE, TRUE) == NEMO_COLUMN_TALLY_EACH_TEXT);
	check (nemo_column_tally_mode_for (NEMO_COLUMN_KIND_MINOR, FALSE, TRUE) == NEMO_COLUMN_TALLY_EACH_TEXT);
	check (nemo_column_tally_mode_for (NEMO_COLUMN_KIND_FIXED, FALSE, TRUE) == NEMO_COLUMN_TALLY_WIDEST);

	/* 100 rows of one long name and 10 short ones. Counted per row, the long
	   name is most of what was seen and Name stays at 400. Counted once, it
	   is 1 value in 11, and the share stops at the 9th, 180. */
	tally = nemo_column_tally_new (nemo_column_tally_mode_for (NEMO_COLUMN_KIND_PRIMARY, TRUE, TRUE));
	check (fit_of_repeated_name (tally, 100) == 180);
	nemo_column_tally_free (tally);

	tally = nemo_column_tally_new (NEMO_COLUMN_TALLY_EACH_ROW);
	check (fit_of_repeated_name (tally, 100) == 400);
	nemo_column_tally_free (tally);
}

static void
check_tally_rows_and_texts (void)
{
	NemoColumnTally *tally;
	int fit, half, widest;

	/* A row measured again takes its new width, wider or narrower. */
	tally = nemo_column_tally_new (NEMO_COLUMN_TALLY_EACH_ROW);
	check (nemo_column_tally_note (tally, GINT_TO_POINTER (1), NULL, 300));
	check (!nemo_column_tally_note (tally, GINT_TO_POINTER (1), NULL, 300));
	check (nemo_column_tally_note (tally, GINT_TO_POINTER (1), NULL, 200));
	nemo_column_tally_measure (tally, 90, &fit, &half, &widest);
	check (fit == 200 && widest == 200);

	/* A row that goes takes its width with it. */
	check (nemo_column_tally_forget_row (tally, GINT_TO_POINTER (1)));
	check (!nemo_column_tally_forget_row (tally, GINT_TO_POINTER (1)));
	nemo_column_tally_measure (tally, 90, &fit, &half, &widest);
	check (fit == 0 && widest == 0);
	nemo_column_tally_free (tally);

	/* A text counts at the widest it was seen at, such as one name at two
	   depths of a tree, and a row going doesn't take a shared text with it. */
	tally = nemo_column_tally_new (NEMO_COLUMN_TALLY_EACH_TEXT);
	check (nemo_column_tally_note (tally, GINT_TO_POINTER (1), "same", 120));
	check (nemo_column_tally_note (tally, GINT_TO_POINTER (2), "same", 150));
	check (!nemo_column_tally_note (tally, GINT_TO_POINTER (3), "same", 120));
	check (!nemo_column_tally_note (tally, NULL, NULL, 999));
	check (!nemo_column_tally_forget_row (tally, GINT_TO_POINTER (2)));
	nemo_column_tally_measure (tally, 90, &fit, &half, &widest);
	check (fit == 150 && half == 150 && widest == 150);

	nemo_column_tally_clear (tally);
	nemo_column_tally_measure (tally, 90, &fit, &half, &widest);
	check (fit == 0 && widest == 0);
	nemo_column_tally_free (tally);

	/* A fixed column only keeps its widest. */
	tally = nemo_column_tally_new (NEMO_COLUMN_TALLY_WIDEST);
	check (nemo_column_tally_note (tally, NULL, "x", 80));
	check (!nemo_column_tally_note (tally, NULL, "y", 60));
	nemo_column_tally_measure (tally, 90, &fit, &half, &widest);
	check (fit == 80 && half == 80 && widest == 80);
	nemo_column_tally_free (tally);
}

int
main (void)
{
	check_fills_the_width ();
	check_wide ();
	check_grows_in_proportion ();
	check_stops_at_max ();
	check_type_gives_alone ();
	check_shrinks_in_proportion ();
	check_overflows ();
	check_single_column ();
	check_widths_out_of_order ();
	check_no_grower ();
	check_primaries_share_the_surplus ();
	check_fit ();
	check_classes ();
	check_tally_modes ();
	check_tally_rows_and_texts ();

	if (failures > 0) {
		g_printerr ("%d check(s) failed\n", failures);
		return EXIT_FAILURE;
	}

	g_print ("OK\n");
	return EXIT_SUCCESS;
}
