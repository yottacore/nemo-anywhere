/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */

/* nemo-prefs-current-folder.h - the Default and Current tabs on Views.

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

#ifndef NEMO_PREFS_CURRENT_FOLDER_H
#define NEMO_PREFS_CURRENT_FOLDER_H

#include <gtk/gtk.h>

/* Wires the Current tab to the folder in the last focused window, starting
 * with @parent, and the buttons that copy between the 2 tabs. The Default
 * tab's own widgets are bound to the preferences elsewhere. */
void nemo_prefs_current_folder_setup (GtkBuilder *builder,
				      GtkWidget  *dialog,
				      GtkWindow  *parent);

#endif /* NEMO_PREFS_CURRENT_FOLDER_H */
