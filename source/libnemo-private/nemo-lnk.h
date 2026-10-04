/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */

/* nemo-lnk.h - read and write Windows .lnk shortcuts without Windows.

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

#ifndef NEMO_LNK_H
#define NEMO_LNK_H

#include <gio/gio.h>

G_BEGIN_DECLS

/* What a shortcut file says, in UTF-8 and Windows spelling. Any string may be
   NULL. Windows reads these through the shell, and comes here only for one the
   shell cannot place, such as one made off Windows. */
typedef struct {
	guint32   attributes;     /* the target's, as they were when the link was made */
	char     *local_path;     /* C:\dir\file */
	gboolean  has_serial;
	guint32   drive_serial;   /* of the volume local_path is on */
	char     *net_share;      /* \\server\share */
	char     *net_path;       /* the rest, under net_share */
	char     *relative_path;  /* from the folder the .lnk sits in */
	char     *env_path;       /* %USERPROFILE%\dir\file, as written */
	char     *working_dir;
	char     *arguments;
	char     *description;
	char     *icon_location;  /* the file the icon is taken from, as written */
} NemoLnk;

gboolean nemo_lnk_parse (const guint8 *bytes, gsize length, NemoLnk *lnk);
gboolean nemo_lnk_read  (const char *lnk_path, NemoLnk *lnk);
void     nemo_lnk_clear (NemoLnk *lnk);

gboolean nemo_lnk_is_dir (const NemoLnk *lnk);

/* The target as Windows would print it, for messages. Caller frees. */
char    *nemo_lnk_display_target (const NemoLnk *lnk);

/* Whether the target, or the file the icon is taken from, is on a share, from
   what the shortcut records. Nothing is looked up, so a drive letter mapped to
   a share only counts where the shortcut says so itself. Windows records the
   share as well as the drive path when the target's drive is shared; that
   counts as the drive, and whether the drive is this machine's is for the
   caller to say. */
gboolean nemo_lnk_points_at_share (const NemoLnk *lnk);

/* A path with Windows %NAME% variables in it, as this machine spells it, or
   NULL when a variable is not set. Off Windows the backslashes around them
   become slashes, and %USERPROFILE% is the home folder unless it is set. */
char    *nemo_lnk_expand (const char *windows_path);

/* target_path with the variable that covers the most of it put in, such as
   %USERPROFILE%\Documents\x.txt. Off Windows only the home folder has one.
   NULL when none covers it. */
char    *nemo_lnk_portable_path (const char *target_path);

/* Write a shortcut at lnk_path to target_path, both paths here, with the
   absolute path and the relative one from the shortcut's folder, and the
   portable one too when with_portable and it fits. Paths inside are spelled
   the Windows way on every platform. Absolute is the \\server\share path
   when the target is on a mounted Windows share, else the path itself.
   Portable is the path with an environment variable in it, which Windows
   follows too, even from a shortcut made elsewhere; off Windows a share path
   goes there as well, for the same reason. Fails with G_IO_ERROR_EXISTS when
   lnk_path is taken. */
gboolean nemo_lnk_write (const char  *lnk_path,
                         const char  *target_path,
                         gboolean     with_portable,
                         GError     **error);

/* Put new paths in an existing shortcut, keeping the rest of it, such as its
   arguments, Start in folder and icon. The paths are taken as written, in
   Windows spelling, and an empty or NULL one is left out; at least one must be
   given. What else in the file points at the old target goes. */
gboolean nemo_lnk_set_paths (const char  *lnk_path,
                             const char  *absolute,
                             const char  *relative,
                             const char  *portable,
                             GError     **error);

/* Only the above is built on Windows. */
#ifndef G_OS_WIN32

/* Where the target is on this machine, as a URI, or NULL. A candidate counts
   only once it is found to exist; the one exception is the smb:// fallback,
   which cannot be checked without going to the network. Never guesses: a drive
   is matched by its volume serial and a share by its server and name. */
char    *nemo_lnk_resolve (const char *lnk_path, const NemoLnk *lnk);

/* The same for another Windows path the shortcut carries, such as its Start in
   folder. Local folders only. Caller frees. */
char    *nemo_lnk_resolve_dir (const NemoLnk *lnk, const char *windows_path);

/* Follow a shortcut, and any shortcut it points at, to the end. Returns a URI
   or NULL; *lnk_out is the last shortcut read, for its Start in folder and for
   messages. Stops on a loop. */
char    *nemo_lnk_follow (const char *lnk_path, NemoLnk *lnk_out);

/* The icon a shortcut shows, from what the .lnk records and never from the
   target, which may be on a share that is not answering. mtime keys a cache.
   NULL when the file is not a readable shortcut. */
GIcon   *nemo_lnk_icon_for_path (const char *lnk_path, gint64 mtime);

/* Whether a readable shortcut records a folder as its target, from the same
   cache. What sorts it with folders. */
gboolean nemo_lnk_target_is_dir_for_path (const char *lnk_path, gint64 mtime);

/* Tests point these at a fake mount table, volume id folder and gvfs folder.
   NULL puts a default back. */
void     nemo_lnk_set_system_paths (const char *mountinfo,
                                    const char *by_uuid_dir,
                                    const char *gvfs_dir);

#endif /* !G_OS_WIN32 */

G_END_DECLS

#endif /* NEMO_LNK_H */
