/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */

/* arc-settings.c - the settings the archive core is handed.

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

#include "arc-settings.h"

void
arc_settings_copy (ArcSettings       *dest,
		   const ArcSettings *src)
{
	guint i;

	for (i = 0; i < ARC_LINE_COUNT; i++) {
		dest->lines[i] = g_strdup (src->lines[i]);
	}
	dest->threads = src->threads;
}

void
arc_settings_clear (ArcSettings *settings)
{
	guint i;

	for (i = 0; i < ARC_LINE_COUNT; i++) {
		g_clear_pointer (&settings->lines[i], g_free);
	}
	settings->threads = 0;
}
