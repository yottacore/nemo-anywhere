/* nemo-shell-icon-win32.h - the icon the Windows shell would draw for a shortcut
 *
 * Copyright © 2026 t00mietum (CryptogID: ปʬϝღถɔ4რఠΔթะ9ƾǝu)
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published
 * by the Free Software Foundation.
 */

#ifndef NEMO_SHELL_ICON_WIN32_H
#define NEMO_SHELL_ICON_WIN32_H

#include <glib.h>
#include <gdk-pixbuf/gdk-pixbuf.h>

typedef void (*NemoShellIconReady) (gpointer data);

/* The shell's icon for the .lnk at path, exactly pixel_size square: taken
 * from the system image list of that size (16, 32, 48, 256) or scaled down
 * from the next one up. When the target or the shortcut's own icon is on a
 * share, the icon for the target's name, so the share is never visited. NULL
 * when the shell has none. Cached by path, size and the file's modification
 * time. Main thread only; this one waits for the shell, and the views use the
 * lookup below. Caller unrefs. */
GdkPixbuf *nemo_shell_icon_win32_for_path (const gchar *path,
					   gint         pixel_size,
					   gint64       mtime);

/* The same icon from the cache, or NULL at once. On a miss the shell is asked
 * on a worker thread, and ready (data) is called on the main thread when it
 * found an icon, which a second lookup then returns. A miss already under way
 * is not asked twice. data is always handed to destroy once it is no longer
 * needed. Main thread only. Caller unrefs. */
GdkPixbuf *nemo_shell_icon_win32_lookup (const gchar        *path,
					 gint                pixel_size,
					 gint64              mtime,
					 NemoShellIconReady  ready,
					 gpointer            data,
					 GDestroyNotify      destroy);

void       nemo_shell_icon_win32_clear_cache (void);

#endif /* NEMO_SHELL_ICON_WIN32_H */
