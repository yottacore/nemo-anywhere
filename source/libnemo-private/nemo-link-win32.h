/* nemo-link-win32.h - real file-system links on Windows: symlinks and
 * junctions. A different thing from a .lnk shell shortcut, which lives in
 * nemo-shortcut-win32.h. Empty on non-Windows.
 *
 * Copyright © 2026 t00mietum (CryptogID: ปʬϝღถɔ4რఠΔթะ9ƾǝu)
 *
 * This program is free software; you can redistribute it and/or modify it under
 * the terms of the GNU General Public License version 2, as published by the
 * Free Software Foundation.
 */

#ifndef NEMO_LINK_WIN32_H
#define NEMO_LINK_WIN32_H

#include <glib.h>

#include "nemo-link-copy.h"

G_BEGIN_DECLS

/* What kind of link this path is, or NONE. Only symlinks and junctions count -
   the file system uses reparse points for other things too (cloud placeholders,
   deduplication, store app aliases) and none of those are links to copy. */
NemoLinkKind nemo_win32_link_kind (const char *path);

/* Whether opening path through the link reaches anything. GLib cannot say:
   for a link that leads nowhere it answers with the link's own type, a file
   or a folder, as if it had been followed. */
gboolean nemo_win32_link_leads_somewhere (const char *path);

/* What a link points at, spelled the way the link itself spells it - so a
   relative symlink answers with its relative text. Caller frees. */
gboolean nemo_win32_link_read_target (const char  *link_path,
                                      char       **target,
                                      GError     **error);

/* Make a link of the kind asked for. A junction wants an absolute local
   directory, so relative targets are resolved against base_dir first (pass the
   directory the original link sat in; NULL to skip). */
gboolean nemo_win32_link_create (const char         *target,
                                 const char         *link_path,
                                 const char         *base_dir,
                                 NemoLinkKind        kind,
                                 GError            **error);

/* A second name for existing_path. NTFS only, and on the same drive. */
gboolean nemo_win32_link_create_hard (const char  *existing_path,
                                      const char  *link_path,
                                      GError     **error);

/* The old "just make me a link" entry point, still what the Create Link menu
   item wants: a junction for a folder wherever one will do, a symlink
   otherwise. */
gboolean nemo_win32_link_create_default (const char  *target_path,
                                         const char  *link_path,
                                         GError     **error);

/* Whether this process is allowed to make symlinks at all. Windows wants either
   Developer Mode or an elevated run, so the answer is found by trying once. */
gboolean nemo_win32_link_symlinks_allowed (void);

/* Which kinds can be created inside dir_path. Zero when the volume keeps no
   reparse points at all, which is what FAT and exFAT answer. */
guint nemo_win32_link_kinds_supported (const char *dir_path);

G_END_DECLS

#endif /* NEMO_LINK_WIN32_H */
