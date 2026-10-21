/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */

/* nemo-network-win32.h - network:/// over native Windows networking.

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

/* In-process network:/// scheme for Windows: the network neighborhood
 * enumerated natively (WNet), servers as folders, shares as shortcuts
 * to their UNC path, which win32 GIO handles as an ordinary native
 * location. No-op on other platforms.
 */

#ifndef NEMO_NETWORK_WIN32_H
#define NEMO_NETWORK_WIN32_H

#include <glib.h>
#include <gio/gio.h>

void nemo_network_win32_register (void);

#ifdef G_OS_WIN32
/* What a WNet failure is shown as. @server names the machine being browsed, or
 * NULL for the neighborhood itself. Public so the failures can be checked on a
 * box that does not fail in those ways. */
void nemo_network_win32_set_error (GError      **error,
				   gulong        rc,
				   const gchar  *server);

/* Whether this machine has a network at all. The enumeration cannot say: it
 * succeeds with an empty list either way. */
gboolean nemo_network_win32_is_available (void);

/* Windows has no smb scheme of its own, so an smb:// address goes where the
 * same place goes here: a share, or a folder in one, is its UNC path, a server
 * is its list of shares under network:///, and no server is network:/// itself.
 * Registered for both uris and typed names. NULL when uri isn't an smb address. */
GFile *nemo_network_win32_file_for_smb_uri (const char *uri);

/* A UNC path naming only a server, \\host, as that server's list of shares,
 * the way Explorer opens it. Typed names are mapped as they're parsed; this is
 * for a location that arrives as a path. NULL for anything else. */
GFile *nemo_network_win32_file_for_unc_server (GFile *location);
#endif

#endif /* NEMO_NETWORK_WIN32_H */
