/* nemo-link-edit.h - change where an existing link points, and its name.
 * Symlinks and junctions have one target; a Windows shortcut has up to three
 * paths to it. A hardlink has no target to change, so it is not a link here.
 *
 * Copyright © 2026 t00mietum (CryptogID: ปʬϝღถɔ4რఠΔթะ9ƾǝu)
 *
 * This program is free software; you can redistribute it and/or modify it under
 * the terms of the GNU General Public License version 2, as published by the
 * Free Software Foundation.
 */

#ifndef NEMO_LINK_EDIT_H
#define NEMO_LINK_EDIT_H

#include <gtk/gtk.h>
#include <gio/gio.h>

G_BEGIN_DECLS

/* Give the symlink or junction at link_path a new name and target. Either may
   be the same as before. The target is taken as written, so a relative one
   stays relative. The old link is only taken away once the new one is made. */
gboolean nemo_link_edit_symlink  (const char  *link_path,
                                  const char  *new_name,
                                  const char  *new_target,
                                  GError     **error);

/* Whether link gets the shortcut editor: a file named .lnk that is not itself
   a symlink or junction. Those get the target editor, whatever their name. */
gboolean nemo_link_edit_is_shortcut (GFile *link);

/* The same for a Windows shortcut, and a symlink to one is refused. ".lnk" is
   put on the name when it is not there. The paths are rewritten only when
   set_paths is TRUE, since that drops what else the shortcut knew about its
   old target. */
gboolean nemo_link_edit_shortcut (const char  *lnk_path,
                                  const char  *new_name,
                                  gboolean     set_paths,
                                  const char  *absolute,
                                  const char  *relative,
                                  const char  *portable,
                                  GError     **error);

/* The Edit link dialog, which makes the change itself and says so when it
   cannot. */
void     nemo_link_edit_ask      (GtkWindow   *parent,
                                  GFile       *link);

G_END_DECLS

#endif /* NEMO_LINK_EDIT_H */
