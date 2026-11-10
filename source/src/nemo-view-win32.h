/* Win32-only helpers for nemo-view, isolated so <windows.h>'s macro pollution
   (DELETE, ERROR, ...) stays out of the main view sources.

   Copyright © 2026 t00mietum (CryptogID: ปʬϝღถɔ4რఠΔթะ9ƾǝu) */

#ifndef NEMO_VIEW_WIN32_H
#define NEMO_VIEW_WIN32_H

#include <glib.h>

/* Quote @arg as one command-line argument under MSVCRT rules. Exposed because
 * both callers below hand the result to a process that re-splits it, and the
 * trailing-backslash case (a drive root) has no unattended end-to-end path. */
gchar *nemo_view_win32_quote_arg (const gchar *arg);

/* Relaunch the running executable elevated ("runas"/UAC) at @path. */
void nemo_view_win32_open_elevated (const gchar *path);

/* Whether this process is already running elevated. geteuid() can't answer it
 * on Windows - the compat shim fakes a non-root uid. */
gboolean nemo_view_win32_is_elevated (void);

/* Open the native console (Windows Terminal, PowerShell, or cmd) at @path. */
void nemo_view_win32_open_terminal (const gchar *path);

/* Split the one-field terminal preference into the program and what follows it,
 * or NULL in @args when nothing does. Exposed for the sake of a test - the real
 * call opens a window. */
gchar *nemo_view_win32_split_terminal_command (const gchar *command, gchar **args);

/* Hand @path to Explorer: a folder opens, anything else is revealed and picked
 * out inside its own folder. @is_directory says which. */
void nemo_view_win32_open_in_explorer (const gchar *path, gboolean is_directory);

#endif /* NEMO_VIEW_WIN32_H */
