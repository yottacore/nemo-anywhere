/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */

/* arc-settings.h - the settings the archive core is handed.

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

#ifndef ARC_SETTINGS_H
#define ARC_SETTINGS_H

#include <glib.h>

G_BEGIN_DECLS

/* The core never reads a settings file. The app reads its own and hands the
   values in, and a job keeps a copy, so a change made while it runs doesn't
   reach it halfway. */

typedef enum {
	ARC_LINE_CREATE_7Z,
	ARC_LINE_CREATE_RAR,
	ARC_LINE_EXTRACT_7Z,
	ARC_LINE_EXTRACT_RAR,
	ARC_LINE_COUNT
} ArcLine;

/* What each line is called in a message about it. The app keeps each one in
   its settings under the same name. */
#define ARC_LINE_NAME_CREATE_7Z   "create-with-7z"
#define ARC_LINE_NAME_CREATE_RAR  "create-with-rar"
#define ARC_LINE_NAME_EXTRACT_7Z  "extract-with-7z"
#define ARC_LINE_NAME_EXTRACT_RAR "extract-with-rar"

/* Returns: (transfer none) */
const char *arc_line_name (ArcLine line);

typedef struct {
	/* The command lines as set, with the built-in line already in place of
	   one that was emptied. NULL where the app has none to give. */
	char  *lines[ARC_LINE_COUNT];
	/* The share of the cores a compression may use, as a thread count. */
	guint  threads;
} ArcSettings;

/* dest gets its own copy of every line. Clear it when done. */
void arc_settings_copy  (ArcSettings       *dest,
			 const ArcSettings *src);
void arc_settings_clear (ArcSettings       *settings);

G_END_DECLS

#endif /* ARC_SETTINGS_H */
