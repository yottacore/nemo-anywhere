/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */

/* nemo-proportional-paned.c - a divider that keeps its share of the width.

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

#include "nemo-proportional-paned.h"
#include "nemo-pane-layout.h"

/* Neither side of a divider goes below this, however narrow the window. */
#define PANED_MIN_CHILD 40

struct _NemoProportionalPaned {
	GtkPaned parent;

	/* What every resize is measured against: the width and position when
	   the divider was last placed, by hand or at setup. Zero until both
	   children are in. Scaling from the previous step instead rounds a
	   one-pixel step away every time. */
	int anchor_width;
	int anchor_position;

	/* The width and divider after the last allocation. A different divider
	   at the same width means someone dragged it. */
	int last_width;
	int seen;
};

G_DEFINE_TYPE (NemoProportionalPaned, nemo_proportional_paned, GTK_TYPE_PANED)

enum {
	PLACED,
	LAST_SIGNAL
};

static guint signals[LAST_SIGNAL];

static void
nemo_proportional_paned_size_allocate (GtkWidget *widget,
				       GtkAllocation *allocation)
{
	NemoProportionalPaned *self = NEMO_PROPORTIONAL_PANED (widget);
	GtkPaned *paned = GTK_PANED (widget);
	gboolean first = self->anchor_width <= 0;
	gboolean scaled = FALSE;
	gboolean placed;
	int position;

	if (gtk_paned_get_child1 (paned) == NULL ||
	    gtk_paned_get_child2 (paned) == NULL) {
		self->anchor_width = 0;
		GTK_WIDGET_CLASS (nemo_proportional_paned_parent_class)->size_allocate (widget, allocation);
		return;
	}

	if (allocation->width <= 1) {
		GTK_WIDGET_CLASS (nemo_proportional_paned_parent_class)->size_allocate (widget, allocation);
		return;
	}

	if (!first && allocation->width != self->last_width) {
		/* From the anchor, not from where GTK clamped it for a child's
		   minimum size, so a window widened again gets the old share back. */
		position = nemo_pane_layout_scale_position (self->anchor_position,
							    self->anchor_width,
							    allocation->width,
							    PANED_MIN_CHILD);
		scaled = TRUE;

		if (position != gtk_paned_get_position (paned)) {
			gtk_paned_set_position (paned, position);
		}
	}

	GTK_WIDGET_CLASS (nemo_proportional_paned_parent_class)->size_allocate (widget, allocation);

	/* Read after chaining up, since a notify::position handler may move the
	   divider during the allocation. The split view centers itself that way. */
	position = gtk_paned_get_position (paned);

	placed = !scaled && !first && position != self->seen;

	if (placed || first) {
		self->anchor_width = allocation->width;
		self->anchor_position = position;
	}

	self->last_width = allocation->width;
	self->seen = position;

	if (placed) {
		g_signal_emit (self, signals[PLACED], 0, position);
	}
}

static void
nemo_proportional_paned_init (G_GNUC_UNUSED NemoProportionalPaned *self)
{
}

static void
nemo_proportional_paned_class_init (NemoProportionalPanedClass *klass)
{
	GTK_WIDGET_CLASS (klass)->size_allocate = nemo_proportional_paned_size_allocate;

	/* The divider was moved at the same width, by a drag or a set_position.
	   A resize that only scales it does not count, so a width saved from here
	   is the one that was chosen, not whatever the last window size left. */
	signals[PLACED] = g_signal_new ("placed",
					G_TYPE_FROM_CLASS (klass),
					G_SIGNAL_RUN_LAST,
					0, NULL, NULL, NULL,
					G_TYPE_NONE, 1, G_TYPE_INT);
}

GtkWidget *
nemo_proportional_paned_new (void)
{
	return g_object_new (NEMO_TYPE_PROPORTIONAL_PANED,
			     "orientation", GTK_ORIENTATION_HORIZONTAL,
			     NULL);
}
