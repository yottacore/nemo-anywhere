/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */

/* nemo-clipboard-win32.h - file cut and copy on the Windows clipboard.

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

#ifndef NEMO_CLIPBOARD_WIN32_H
#define NEMO_CLIPBOARD_WIN32_H

#include <gtk/gtk.h>

/* Put @text on the clipboard, written out rather than advertised. */
gboolean nemo_clipboard_win32_set_text (GtkWidget  *widget,
					const char *text);

/* Put a cut or copy of @uris on the clipboard in the formats Windows expects,
 * written out up front. FALSE means it couldn't be done and the toolkit should
 * be used instead - a place only nemo understands has no path to offer. */
gboolean nemo_clipboard_win32_set_files (GtkWidget *widget,
					 GList     *uris,
					 gboolean   cut);

/* Files another program put on the clipboard, as a uri list, with @cut set when
 * it was a cut rather than a copy. NULL when there are none. */
GList *nemo_clipboard_win32_get_files (gboolean *cut);

/* Whether the clipboard has files at all. */
gboolean nemo_clipboard_win32_has_files (void);

/* Make every entry and text view write its cut or copied text out the same
 * way, instead of only advertising it. Once, at startup. */
void nemo_clipboard_win32_watch_editables (void);

/* Empty it, which is what follows a cut once the paste has happened. */
void nemo_clipboard_win32_clear (void);

#endif /* NEMO_CLIPBOARD_WIN32_H */
