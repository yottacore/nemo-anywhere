/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */

/* test-nemo-drop-keys-win32.c - what the held keys make of a drop on Windows.

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

/* The toolkit on Windows offers copy and move for every drop, whatever is
 * pressed, so Control and Shift together moved the files where they should make
 * a link, and Alt or the right button never brought up the drop menu. And an
 * ask handed to the toolkit there becomes no drop at all, so it has to reach
 * Windows as a real effect and come back out as an ask. */

#include <config.h>

#include <gtk/gtk.h>
#include <gdk/gdkwin32.h>

#include <windows.h>
#include <ole2.h>

#include <libnemo-private/nemo-dnd.h>
#include <libnemo-private/nemo-dnd-win32.h>

#include "test-check.h"

int
main (int argc, char *argv[])
{
	GdkDragContext *context;

	nemo_dnd_win32_prepare ();
	if (!gtk_init_check (&argc, &argv)) {
		g_print ("no display\n");
		return 77;
	}

	check (nemo_dnd_win32_action_for_keys (0) == 0);
	check (nemo_dnd_win32_action_for_keys (MK_LBUTTON) == 0);
	check (nemo_dnd_win32_action_for_keys (MK_LBUTTON | MK_CONTROL) == GDK_ACTION_COPY);
	check (nemo_dnd_win32_action_for_keys (MK_LBUTTON | MK_SHIFT) == GDK_ACTION_MOVE);
	check (nemo_dnd_win32_action_for_keys (MK_LBUTTON | MK_CONTROL | MK_SHIFT) == GDK_ACTION_LINK);
	check (nemo_dnd_win32_action_for_keys (MK_LBUTTON | MK_ALT) == GDK_ACTION_ASK);
	check (nemo_dnd_win32_action_for_keys (MK_RBUTTON) == GDK_ACTION_ASK);
	check (nemo_dnd_win32_action_for_keys (MK_RBUTTON | MK_CONTROL | MK_SHIFT) == GDK_ACTION_ASK);
	check (nemo_dnd_win32_action_for_keys (MK_MBUTTON) == GDK_ACTION_ASK);

	/* A drop target's own context, as the toolkit makes one for a drag
	   coming in. */
	context = g_object_new (GDK_TYPE_WIN32_DRAG_CONTEXT, NULL);
	check (context != NULL);

	nemo_drag_status (context, GDK_ACTION_ASK, GDK_CURRENT_TIME);
	check (gdk_drag_context_get_selected_action (context) == GDK_ACTION_COPY);
	check (nemo_drag_selected_action (context) == GDK_ACTION_ASK);

	nemo_drag_status (context, GDK_ACTION_LINK, GDK_CURRENT_TIME);
	check (gdk_drag_context_get_selected_action (context) == GDK_ACTION_LINK);
	check (nemo_drag_selected_action (context) == GDK_ACTION_LINK);

	nemo_drag_status (context, 0, GDK_CURRENT_TIME);
	check (nemo_drag_selected_action (context) == 0);

	check ((nemo_drag_offered_actions (context) & GDK_ACTION_LINK) != 0);

	/* Not unreffed: its finalize wants what the toolkit's own constructor
	   sets up, and the process ends here anyway. */

	return failures == 0 ? 0 : 1;
}
