/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */

/* test-nemo-icon-bounds.c - the icon bounds of a view with nothing shown.

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

/* Laying out new icons in a manual layout view asks only for the bottom edge,
 * and passes NULL for the rest. With no icon shown yet the empty box was
 * written through all 4 anyway. */

#include <config.h>

#include <stdlib.h>
#include <gtk/gtk.h>

#include <libnemo-private/nemo-global-preferences.h>
#include <libnemo-private/nemo-icon-container.h>
#include <libnemo-private/nemo-icon-private.h>

#include "test-scratch.h"
#include "test-check.h"

int
main (int argc, char *argv[])
{
	GtkWidget *container;
	double x1 = 1, y1 = 1, x2 = 1, y2 = 1;
	char *tmp;

	tmp = test_scratch_config_home ("nemo-icon-bounds-XXXXXX");

	if (!gtk_init_check (&argc, &argv)) {
		g_print ("SKIP: no display\n");
		return 77;
	}
	nemo_global_preferences_init ();

	container = g_object_ref_sink (nemo_icon_container_new ());

	nemo_icon_container_get_all_icon_bounds (NEMO_ICON_CONTAINER (container), NULL, NULL, NULL, &y2, BOUNDS_USAGE_FOR_LAYOUT);
	check (y2 == 0);

	y2 = 1;
	nemo_icon_container_get_all_icon_bounds (NEMO_ICON_CONTAINER (container), &x1, &y1, &x2, &y2, BOUNDS_USAGE_FOR_ENTIRE_ITEM);
	check (x1 == 0 && y1 == 0 && x2 == 0 && y2 == 0);

	g_object_unref (container);
	g_free (tmp);

	if (failures > 0) {
		g_printerr ("%d failure(s)\n", failures);
		return EXIT_FAILURE;
	}

	g_print ("OK\n");
	return EXIT_SUCCESS;
}
