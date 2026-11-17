/* nemo-favorite-vfs-file.h - favorites:/// vfs.
 *
 * Adapted from libxapp 2.8.8 (favorite-vfs-file.h, LGPL-2.1-or-later,
 * © Linux Mint team), relicensed under GPL-2.0 per LGPL-2.1 section 3.
 */

#ifndef NEMO_FAVORITE_VFS_FILE_H
#define NEMO_FAVORITE_VFS_FILE_H

#include "nemo-favorites.h"
#include "nemo-metadata.h"
#include <glib-object.h>
#include <gio/gio.h>

G_BEGIN_DECLS

#define NEMO_TYPE_NEMO_FAVORITE_VFS_FILE (nemo_favorite_vfs_file_get_type ())

G_DECLARE_FINAL_TYPE (NemoFavoriteVfsFile, nemo_favorite_vfs_file, \
                      NEMO_FAVORITE, VFS_FILE, GObject)

// Initializer for favorites:/// - called when the NemoFavorites singleton is created
void nemo_favorite_vfs_register (void);

GFile *nemo_favorite_vfs_file_new_for_uri (const char *uri);
GFile *_nemo_favorite_vfs_file_new_for_info (NemoFavoriteInfo *info);
gchar *nemo_favorite_vfs_file_get_real_uri (GFile *file);

#define URI_SCHEME "favorites"
#define ROOT_URI ("favorites:///")

#define FAVORITE_METADATA_KEY "metadata::" NEMO_METADATA_KEY_FAVORITE
#define FAVORITE_AVAILABLE_METADATA_KEY "metadata::" NEMO_METADATA_KEY_FAVORITE_AVAILABLE

#define META_TRUE "true"
#define META_FALSE "false"

gchar *nemo_path_to_fav_uri (const gchar *path);
gchar *nemo_fav_uri_to_display_name (const gchar *uri);

G_END_DECLS

#endif // NEMO_FAVORITE_VFS_FILE_H
