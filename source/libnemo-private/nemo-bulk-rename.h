/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */

/* nemo-bulk-rename.h - the bulk rename programs known for each platform.

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

#ifndef NEMO_BULK_RENAME_H
#define NEMO_BULK_RENAME_H

#include <glib.h>

/* The default for the bulk rename setting: the first known renamer that's
   installed, or the first known one when none is. Caller frees. */
char *nemo_bulk_rename_find_default (void);

/* Every known renamer's command, one per line, in the order the default is
   picked from. Caller frees. */
char *nemo_bulk_rename_known_commands (void);

#endif /* NEMO_BULK_RENAME_H */
