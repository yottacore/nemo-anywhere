/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */

/* nemo-extensions-list.h - list the installed extensions on stdout.

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

/* Reached by running our own exe with --extensions-list, which is what the
 * plugin page in Settings does. It is a separate process because reading an
 * extension's name means instantiating it, and a loaded module cannot be
 * unloaded again - the answer would otherwise cost the running app every
 * extension the user has switched off.
 *
 * Unix only. Nothing is loaded from disk on Windows.
 */

#ifndef NEMO_EXTENSIONS_LIST_H
#define NEMO_EXTENSIONS_LIST_H

#include <glib.h>

G_BEGIN_DECLS

void nemo_extensions_list_print (void);

G_END_DECLS

#endif /* NEMO_EXTENSIONS_LIST_H */
