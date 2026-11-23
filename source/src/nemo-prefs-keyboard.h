/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */

/* nemo-prefs-keyboard.h - the Keyboard tab in Settings.

   Copyright © 2026 t00mietum (CryptogID: ปʬϝღถɔ4რఠΔթะ9ƾǝu).

   This program is free software; you can redistribute it and/or
   modify it under the terms of the GNU General Public License as
   published by the Free Software Foundation; version 2 of the
   License.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, see <http://www.gnu.org/licenses/>.
*/

#ifndef NEMO_PREFS_KEYBOARD_H
#define NEMO_PREFS_KEYBOARD_H

#include <gtk/gtk.h>

enum {
	NEMO_PREFS_KEYBOARD_COL_PATH,
	NEMO_PREFS_KEYBOARD_COL_LABEL,
	NEMO_PREFS_KEYBOARD_COL_TOOLTIP,
	NEMO_PREFS_KEYBOARD_COL_KEY,
	NEMO_PREFS_KEYBOARD_COL_MODS,
	NEMO_PREFS_KEYBOARD_COL_RESET,
	NEMO_PREFS_KEYBOARD_N_COLS
};

/* Fills keyboard_page_box with the list of shortcuts. */
void nemo_prefs_keyboard_setup (GtkBuilder *builder);

#endif /* NEMO_PREFS_KEYBOARD_H */
