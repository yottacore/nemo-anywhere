/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */

/* nemo-desktop-settings.h - read-only interop with the desktop's own settings.

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

/* A few settings are the desktop's to decide, not ours: which terminal to
 * open, whether the session records recent files, and 12h or 24h clocks. Where
 * a desktop publishes them we read its answer; everywhere else - Windows,
 * a bare WM, any non-Cinnamon session - our own value in settings.shcl
 * stands in. Read-only by design: this reports what the desktop wants, it
 * never writes back into somebody else's schema.
 *
 * This is the only remaining GSettings user, and it never touches a schema
 * of ours.
 */

#ifndef NEMO_DESKTOP_SETTINGS_H
#define NEMO_DESKTOP_SETTINGS_H

#include <glib-object.h>

G_BEGIN_DECLS

void      nemo_desktop_settings_init      (void);
void      nemo_desktop_settings_finalize  (void);

/* A couple of view preferences are shared with the GTK file chooser by
 * long-standing convention, so keep pushing our answer there where GTK's
 * schema exists. No-op everywhere else. */
void      nemo_desktop_settings_set_filechooser_bool (const char *key, gboolean value);

char     *nemo_desktop_settings_get_terminal_exec     (void);
char     *nemo_desktop_settings_get_terminal_exec_arg (void);
gboolean  nemo_desktop_settings_get_recent_enabled    (void);
gboolean  nemo_desktop_settings_get_clock_use_24h     (void);

/* Watch one of the above. The key is the desktop's own spelling
 * ("remember-recent-files", "clock-use-24h"); the callback runs whichever
 * side is backing it. */
void      nemo_desktop_settings_watch (const char *key,
                                       GCallback   callback,
                                       gpointer    user_data);
void      nemo_desktop_settings_unwatch (gpointer user_data);

G_END_DECLS

#endif /* NEMO_DESKTOP_SETTINGS_H */
