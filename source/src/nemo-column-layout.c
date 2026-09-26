/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */

/* nemo-column-layout.c - how the list view divides its width between columns.

   Copyright © 2026 t00mietum (CryptogID: ปʬϝღถɔ4რఠΔթะ9ƾǝu).

   This program is free software; you can redistribute it and/or
   modify it under the terms of the GNU General Public License as
   published by the Free Software Foundation; version 2 of the
   License.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   General Public License for more details.

   You should have received a copy of the GNU General Public
   License along with this program; if not, write to the
   Free Software Foundation, Inc., 51 Franklin Street, Suite 500,
   Boston, MA 02110-1335, USA.
*/

#include <config.h>

#include "nemo-column-layout.h"

#include <stdlib.h>
#include <string.h>

/* Move `amount` pixels from the columns that have room to move, each in
   proportion to its fit width - a wide column moves the most, since it has the
   most to give or the most to show. `limits` is where each stops (max going
   up, min going down). Repeats, because a column that reaches its limit part
   way through leaves its share for the others. Returns what could not be
   moved. */
static int
move_proportionally (const NemoColumnLayoutItem *items,
		     int                        *widths,
		     const int                  *limits,
		     int                         n_items,
		     int                         direction,
		     int                         amount)
{
	while (amount > 0) {
		gint64 weight = 0;
		int moved = 0;
		int biggest = -1;
		int i;

		for (i = 0; i < n_items; i++) {
			if (direction * (limits[i] - widths[i]) > 0) {
				weight += MAX (1, items[i].fit_width);
			}
		}

		if (weight <= 0) {
			return amount;
		}

		for (i = 0; i < n_items; i++) {
			int room = direction * (limits[i] - widths[i]);
			int share;

			if (room <= 0) {
				continue;
			}

			/* Rounded down, so a pass never moves more than asked; what
			   rounding leaves goes to the biggest column below. */
			share = (int) (((gint64) amount * MAX (1, items[i].fit_width)) / weight);
			share = MIN (share, room);

			widths[i] += direction * share;
			moved += share;

			if (biggest < 0 || items[i].fit_width > items[biggest].fit_width) {
				biggest = i;
			}
		}

		amount -= moved;

		if (moved == 0) {
			if (biggest < 0 || direction * (limits[biggest] - widths[biggest]) <= 0) {
				return amount;
			}

			widths[biggest] += direction;
			amount -= 1;
		}
	}

	return 0;
}

void
nemo_column_layout_distribute (const NemoColumnLayoutItem *items,
			       int                         n_items,
			       int                         available,
			       int                        *widths)
{
	int *limits;
	gboolean any_grower = FALSE;
	int sum_fit = 0;
	int i;

	g_return_if_fail (items != NULL);
	g_return_if_fail (widths != NULL);

	if (n_items <= 0) {
		return;
	}

	limits = g_new0 (int, n_items);

	for (i = 0; i < n_items; i++) {
		int min = MAX (1, items[i].min_width);

		/* The three have to be in order; where they are not, the larger
		   wins, since a column cannot be asked to show less than its
		   minimum. */
		widths[i] = MAX (min, items[i].fit_width);
		sum_fit += widths[i];

		if (items[i].grows) {
			any_grower = TRUE;
		}
	}

	if (available >= sum_fit) {
		int surplus;

		for (i = 0; i < n_items; i++) {
			limits[i] = MAX (widths[i], items[i].max_width);
		}

		surplus = move_proportionally (items, widths, limits, n_items, 1,
					       available - sum_fit);

		/* Past every max, the growers share the rest. With none on the
		   row, the row ends short. */
		if (surplus > 0 && any_grower) {
			for (i = 0; i < n_items; i++) {
				limits[i] = items[i].grows ? widths[i] + surplus : widths[i];
			}
			move_proportionally (items, widths, limits, n_items, 1, surplus);
		}
	} else {
		for (i = 0; i < n_items; i++) {
			limits[i] = MIN (widths[i], MAX (1, items[i].min_width));
		}

		/* Whatever cannot be given back is the overflow, and the view
		   scrolls sideways for it. */
		move_proportionally (items, widths, limits, n_items, -1,
				     sum_fit - available);
	}

	g_free (limits);
}

static int
compare_ints (const void *a,
	      const void *b)
{
	int x = *(const int *) a;
	int y = *(const int *) b;

	return (x > y) - (x < y);
}

int
nemo_column_layout_fit (const int *values,
			int        n_values,
			int        percent)
{
	int *sorted;
	int rank;
	int fit;

	if (values == NULL || n_values <= 0) {
		return 0;
	}

	percent = CLAMP (percent, 1, 100);

	sorted = g_new (int, n_values);
	memcpy (sorted, values, n_values * sizeof (int));
	qsort (sorted, n_values, sizeof (int), compare_ints);

	/* Rounded down, but never to no values at all: 90 percent of three
	   values is two. */
	rank = MAX (1, (n_values * percent) / 100);

	fit = sorted[rank - 1];
	g_free (sorted);

	return fit;
}

void
nemo_column_layout_item_for_kind (NemoColumnKind           kind,
				  const NemoColumnMeasure *measure,
				  int                      pad,
				  int                      ellipsis,
				  int                      narrow,
				  int                      dragged,
				  NemoColumnLayoutItem    *item)
{
	int fit = measure->fit;
	int half = measure->half;
	int widest = measure->widest;

	item->grows = FALSE;

	switch (kind) {
	case NEMO_COLUMN_KIND_PRIMARY:
		/* Every value when there is room, and past that a share of
		   what is left. Short of room, the narrowest share of values,
		   with space for the ellipsis the rest are cut to. */
		item->max_width = widest + pad;
		item->fit_width = item->max_width;
		item->min_width = fit + (fit < widest ? ellipsis : 0) + pad;
		item->grows = TRUE;
		break;
	case NEMO_COLUMN_KIND_MINOR:
		item->max_width = widest + pad;
		item->fit_width = fit + pad;
		item->min_width = half + (half < widest && widest > narrow ? ellipsis : 0) + pad;
		break;
	case NEMO_COLUMN_KIND_FIXED:
	default:
		item->max_width = widest + pad;
		item->fit_width = item->max_width;
		item->min_width = item->max_width;
		break;
	}

	/* A width dragged into place stands until the folder changes, or
	   for good where a minor column's width is saved with the folder. */
	if (kind != NEMO_COLUMN_KIND_FIXED && dragged >= 0) {
		item->max_width = dragged;
		item->fit_width = item->max_width;
		item->min_width = item->max_width;
		item->grows = FALSE;
	}

	item->min_width = MAX (measure->heading, item->min_width);
	item->fit_width = MAX (item->min_width, item->fit_width);
	item->max_width = MAX (item->fit_width, item->max_width);
}
