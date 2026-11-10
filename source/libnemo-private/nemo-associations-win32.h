/* nemo-associations-win32.h - which program opens what, on Windows
 *
 * Copyright © 2026 t00mietum (CryptogID: ปʬϝღถɔ4რఠΔթะ9ƾǝu)
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published
 * by the Free Software Foundation.
 */

#ifndef NEMO_ASSOCIATIONS_WIN32_H
#define NEMO_ASSOCIATIONS_WIN32_H

#include <gio/gio.h>

/* The program for a type: the override from the settings file when there is
 * one, else what the shell would open it with. NULL when neither knows. */
GAppInfo    *nemo_associations_win32_default_for_type (const gchar *content_type);

/* The command line an app info made here has, %1 standing for the file;
 * NULL for one that came from GIO. */
const gchar *nemo_associations_win32_command_of      (GAppInfo    *app);

/* The command line to start any app info with, ours or GIO's. NULL when the
 * app has none to give, which only a store app does. */
const gchar *nemo_associations_win32_command_for_app (GAppInfo    *app);

gboolean     nemo_associations_win32_launch          (const gchar  *command,
						      GList        *locations,
						      GError      **error);

/* Records the app as the type's default, in the settings file. */
gboolean     nemo_associations_win32_set_default     (GAppInfo    *app,
						      const gchar *content_type);

gchar       *nemo_associations_win32_get_override    (const gchar *content_type);
void         nemo_associations_win32_set_override    (const gchar *content_type,
						      const gchar *command);

/* Drops the print and print-to entries the registry walk brings along. */
GList       *nemo_associations_win32_filter_apps     (GList       *apps);

gchar       *nemo_associations_win32_command_for_file (const gchar *command,
						       const gchar *path);
gchar       *nemo_associations_win32_friendly_name   (const gchar *command);

/* GIO has no name for a program beyond the file it found, so its entries read
 * "Code.exe". Answers with what the program calls itself. */
gchar       *nemo_associations_win32_name_for_app    (GAppInfo    *app);
gchar       *nemo_associations_win32_registry_command (const gchar  *content_type,
						       gchar       **friendly_name);

#endif /* NEMO_ASSOCIATIONS_WIN32_H */
