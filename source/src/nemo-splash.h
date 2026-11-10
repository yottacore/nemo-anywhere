/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */

/* nemo-splash.h - the window that stands in until the real one is drawn.

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

/* Windows only, and deliberately not built on GTK: it has to be on screen
 * before gtk_init has run. Everywhere else these are no-ops - a Linux or BSD
 * launch shows its window in about a second and has nothing to cover.
 *
 * It runs its own thread and its own message loop, so it keeps painting and
 * scrolling while the main thread is busy starting the application.
 */

#ifndef NEMO_SPLASH_H
#define NEMO_SPLASH_H

#include <glib.h>

G_BEGIN_DECLS

/* Put it on screen. Safe to call more than once; the second call does nothing. */
void     nemo_splash_show (void);

/* Add a line to the log it is showing. Any thread; the text is copied. */
void     nemo_splash_note (const char *message);

/* Take it down. Safe to call when it was never shown. */
void     nemo_splash_hide (void);

/* Whether a splash is currently up, so a caller can skip the work of
 * describing what it is doing when nobody is reading. */
gboolean nemo_splash_is_up (void);

G_END_DECLS

#endif /* NEMO_SPLASH_H */
