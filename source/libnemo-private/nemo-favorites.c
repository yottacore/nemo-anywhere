/* nemo-favorites.c - favorite-files store, backed by the config store.
 *
 * Adapted from libxapp 2.8.8 (xapp-favorites.c, LGPL-2.1-or-later,
 * © Linux Mint team), relicensed under GPL-2.0 per LGPL-2.1 section 3.
 * Launch/menu helpers dropped; storage moved to the org.nemo-anywhere schema.
 *
 * Upstream disabled some functions with an early return and left the body
 * compiling. Those bodies are commented out here instead, so the dead code
 * cannot drift or be read as live. They stay in the file because they are the
 * reference if the feature is ever turned back on; the reason each one is off
 * is the comment above its return.
 */

#include <config.h>

#include "nemo-config.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <glib/gstdio.h>

#include "nemo-favorites.h"
#include "nemo-favorite-vfs-file.h"

/* Favorites live at the config file root, not in a section. */
#define FAVORITES_SCHEMA ""
#define FAVORITES_KEY "favorites"
#define SETTINGS_DELIMITER "::"
#define MAX_DISPLAY_URI_LENGTH 20

G_DEFINE_BOXED_TYPE (NemoFavoriteInfo, nemo_favorite_info, nemo_favorite_info_copy, nemo_favorite_info_free);

/**
 * nemo_favorite_info_copy:
 * @info: The #NemoFavoriteInfo to duplicate.
 *
 * Makes an exact copy of an existing #NemoFavoriteInfo.
 *
 * Returns: (transfer full): a new #NemoFavoriteInfo.  Free using #nemo_favorite_info_free.
 *
 * Since 2.0
 */
NemoFavorites *global_favorites;

NemoFavoriteInfo *
nemo_favorite_info_copy (const NemoFavoriteInfo *info)
{
    // g_debug ("NemoFavoriteInfo: copy");
    g_return_val_if_fail (info != NULL, NULL);

    NemoFavoriteInfo *_info = g_slice_dup (NemoFavoriteInfo, info);
    _info->uri = g_strdup (info->uri);
    _info->display_name = g_strdup (info->display_name);
    _info->cached_mimetype = g_strdup (info->cached_mimetype);

    return _info;
}

/**
 * nemo_favorite_info_free:
 * @info: The #NemoFavoriteInfo to free.
 *
 * Destroys the #NemoFavoriteInfo.
 *
 * Since 2.0
 */
void
nemo_favorite_info_free (NemoFavoriteInfo *info)
{
    g_return_if_fail (info != NULL);

    g_debug ("NemoFavoriteInfo free (%s)", info->uri);

    g_free (info->uri);
    g_free (info->display_name);
    g_free (info->cached_mimetype);
    g_slice_free (NemoFavoriteInfo, info);
}

typedef struct
{
    GHashTable *infos;

    NemoConfigGroup *settings;

    gulong settings_listener_id;
    guint changed_timer_id;
} NemoFavoritesPrivate;

struct _NemoFavorites
{
    GObject parent_instance;
};

G_DEFINE_TYPE_WITH_PRIVATE (NemoFavorites, nemo_favorites, G_TYPE_OBJECT)

enum
{
    CHANGED,
    LAST_SIGNAL
};

static guint signals[LAST_SIGNAL] = {0, };

/* The favorites:/// vfs sets supports_thread_contexts, so GIO reads this list
 * from worker threads while the main thread tears the whole table down and
 * rebuilds it on every stored change. Recursive because the mutating paths call
 * back into the lookups. */
static GRecMutex infos_lock;

static void finish_add_favorite (NemoFavorites *favorites,
                                 const gchar   *uri,
                                 const gchar   *mimetype,
                                 gboolean       from_saved);

/* Stored entries are "mimetype::uri". The mimetype comes first because a mime
 * type can never contain a colon, so the first "::" is always the separator;
 * the other way round, a file named "notes::draft.txt" silently repointed its
 * favorite at "file:///home/u/notes". Entries written in the old order are
 * still read - their first half carries the uri scheme's colon, which tells
 * them apart - and get rewritten the next time the list is stored. */
static gboolean
parse_favorite_entry (const gchar  *entry,
                      gchar       **uri,
                      gchar       **mimetype)
{
    const gchar *sep;

    *uri = NULL;
    *mimetype = NULL;

    if (entry == NULL || *entry == '\0')
    {
        return FALSE;
    }

    sep = strstr (entry, SETTINGS_DELIMITER);

    if (sep == NULL)
    {
        /* No mimetype half was ever written for this one. */
        *uri = g_strdup (entry);
    }
    else if (memchr (entry, ':', sep - entry) != NULL)
    {
        *uri = g_strndup (entry, sep - entry);
        *mimetype = g_strdup (sep + strlen (SETTINGS_DELIMITER));
    }
    else
    {
        *mimetype = g_strndup (entry, sep - entry);
        *uri = g_strdup (sep + strlen (SETTINGS_DELIMITER));
    }

    if (*mimetype != NULL && **mimetype == '\0')
    {
        g_clear_pointer (mimetype, g_free);
    }

    if (**uri == '\0')
    {
        g_clear_pointer (uri, g_free);
        g_clear_pointer (mimetype, g_free);
        return FALSE;
    }

    return TRUE;
}

static gchar *
format_favorite_entry (const NemoFavoriteInfo *info)
{
    /* Always emit the separator, even with nothing to put in front of it -
     * g_strjoin would stop at a NULL mimetype and write a bare uri. */
    return g_strconcat (info->cached_mimetype != NULL ? info->cached_mimetype : "",
                        SETTINGS_DELIMITER,
                        info->uri,
                        NULL);
}

static gboolean
changed_callback (gpointer data)
{
    g_return_val_if_fail (NEMO_IS_FAVORITES (data), G_SOURCE_REMOVE);
    NemoFavorites *favorites = NEMO_FAVORITES (data);

    NemoFavoritesPrivate *priv = nemo_favorites_get_instance_private (favorites);
    g_debug ("NemoFavorites: list updated, emitting changed signal");

    /* changed_timer_id is touched from worker threads via queue_changed; guard
     * it with the same recursive lock the infos table uses. */
    g_rec_mutex_lock (&infos_lock);
    priv->changed_timer_id = 0;
    g_rec_mutex_unlock (&infos_lock);
    g_signal_emit (favorites, signals[CHANGED], 0);

    return G_SOURCE_REMOVE;
}

static void
queue_changed (NemoFavorites *favorites)
{
    NemoFavoritesPrivate *priv = nemo_favorites_get_instance_private (favorites);

    g_rec_mutex_lock (&infos_lock);
    if (priv->changed_timer_id > 0)
    {
        g_source_remove (priv->changed_timer_id);
    }

    priv->changed_timer_id = g_idle_add ((GSourceFunc) changed_callback, favorites);
    g_rec_mutex_unlock (&infos_lock);
}

// static void
// sync_metadata_callback (GObject      *source,
//                         GAsyncResult *res,
//                         gpointer      user_data)
// {
//     // Disabled
//     return;

//     GFile *file;
//     GError *error;

//     file = G_FILE (source);
//     error = NULL;

//     if (!g_file_set_attributes_finish (file,
//                                        res,
//                                        NULL,
//                                        &error))
//     {
//         if (error != NULL)
//         {
//             if (error->code != G_IO_ERROR_NOT_FOUND)
//             {
//                 g_warning ("Could not update file metadata for favorite file '%s': %s", g_file_get_uri (file), error->message);
//             }

//             g_error_free (error);
//         }
//     }
//     else
//     {
//         if (g_file_is_native (file))
//         {
//             // I can't think of any other way to touch a file so a file monitor might notice
//             // the attribute change. It shouldn't be too much trouble since most times add/remove
//             // will be done in the file manager (where the update can be triggered internally).

//             gchar *local_path = g_file_get_path (file);
//             g_utime (local_path, NULL);
//             g_free (local_path);
//         }
//     }
// }

static void
sync_file_metadata (G_GNUC_UNUSED NemoFavorites *favorites,
                    G_GNUC_UNUSED const gchar   *uri,
                    G_GNUC_UNUSED gboolean       is_favorite)
{
    /* Disabled - this is less than optimal, and is implemented instead in
     * nemo, currently. This could be changed later to help support other browsers.
     * Also, this only works with local files. */
    return;

//     /* borrowed from nemo-vfs-file.c */
//     GFileInfo *info;
//     GFile *file;

//     g_debug ("Sync metadata: %s - Favorite? %d", uri, is_favorite);

//     info = g_file_info_new ();

//     if (is_favorite) {
//         g_file_info_set_attribute_string (info, FAVORITE_METADATA_KEY, META_TRUE);
//     } else {
//         /* Unset the key */
//         g_file_info_set_attribute (info, FAVORITE_METADATA_KEY, G_FILE_ATTRIBUTE_TYPE_INVALID, NULL);
//     }

//     file = g_file_new_for_uri (uri);

//     g_file_set_attributes_async (file,
//                                  info,
//                                  0,
//                                  G_PRIORITY_DEFAULT,
//                                  NULL,
//                                  sync_metadata_callback,
//                                  favorites);

//     g_object_unref (file);
//     g_object_unref (info);
}

static void
store_favorites (NemoFavorites *favorites)
{
    NemoFavoritesPrivate *priv = nemo_favorites_get_instance_private (favorites);
    GList *iter, *keys;
    GPtrArray *array;
    gchar **new_settings;

    array = g_ptr_array_new ();

    g_rec_mutex_lock (&infos_lock);

    keys = g_hash_table_get_keys (priv->infos);

    for (iter = keys; iter != NULL; iter = iter->next)
    {
        NemoFavoriteInfo *info = (NemoFavoriteInfo *) g_hash_table_lookup (priv->infos, iter->data);

        g_ptr_array_add (array, format_favorite_entry (info));
    }

    g_ptr_array_add (array, NULL);

    g_list_free (keys);

    g_rec_mutex_unlock (&infos_lock);

    new_settings = (gchar **) g_ptr_array_free (array, FALSE);

    g_signal_handler_block (priv->settings, priv->settings_listener_id);
    nemo_config_set_strv (priv->settings, FAVORITES_KEY, (const gchar* const*) new_settings);
    g_signal_handler_unblock (priv->settings, priv->settings_listener_id);

    g_debug ("NemoFavorites: store_favorites: favorites saved");

    g_strfreev (new_settings);
}

static void
load_favorites (NemoFavorites *favorites,
                gboolean       signal_changed)
{
    NemoFavoritesPrivate *priv = nemo_favorites_get_instance_private (favorites);
    gchar **raw_list;
    guint i, count;

    g_rec_mutex_lock (&infos_lock);

    if (priv->infos != NULL)
    {
        g_hash_table_destroy (priv->infos);
    }

    priv->infos = g_hash_table_new_full (g_str_hash, g_str_equal,
                                         g_free, (GDestroyNotify) nemo_favorite_info_free);

    raw_list = nemo_config_get_strv (priv->settings, FAVORITES_KEY);
    count = raw_list != NULL ? g_strv_length (raw_list) : 0;

    for (i = 0; i < count; i++)
    {
        gchar *uri, *mimetype;

        if (!parse_favorite_entry (raw_list[i], &uri, &mimetype))
        {
            g_debug ("NemoFavorites: dropping unreadable favorites entry '%s'", raw_list[i]);
            continue;
        }

        finish_add_favorite (favorites, uri, mimetype, TRUE);

        g_free (uri);
        g_free (mimetype);
    }

    g_strfreev (raw_list);

    g_rec_mutex_unlock (&infos_lock);

    g_debug ("NemoFavorites: load_favorite: favorites loaded (%u)", count);

    if (signal_changed)
    {
        queue_changed (favorites);
    }
}

static void
rename_favorite (NemoFavorites *favorites,
                 const gchar   *old_uri,
                 const gchar   *new_uri)
{
    NemoFavoriteInfo *info;
    NemoFavoritesPrivate *priv = nemo_favorites_get_instance_private (favorites);
    gchar *final_new_uri = NULL;

    g_rec_mutex_lock (&infos_lock);

    if (g_str_has_prefix (old_uri, ROOT_URI))
    {
        // Renaming occurred inside of favorites:/// we need to identify by
        // display name.

        const gchar *old_display_name = old_uri + strlen (ROOT_URI);
        const gchar *new_display_name = new_uri + strlen (ROOT_URI);

        info = nemo_favorites_find_by_display_name (favorites, old_display_name);

        if (info)
        {
            GFile *real_file, *parent, *renamed_file;

            real_file = g_file_new_for_uri (info->uri);
            parent = g_file_get_parent (real_file);

            renamed_file = g_file_get_child_for_display_name (parent,
                                                              new_display_name,
                                                              NULL);

            if (renamed_file != NULL)
            {
                final_new_uri = g_file_get_uri (renamed_file);
            }

            g_object_unref (real_file);
            g_object_unref (parent);
            g_clear_object (&renamed_file);
        }
    }
    else
    {
        info = g_hash_table_lookup (priv->infos, old_uri);
        final_new_uri = g_strdup (new_uri);
    }

    if (info != NULL && final_new_uri != NULL)
    {
        gchar *mimetype = g_strdup (info->cached_mimetype);

        sync_file_metadata (favorites, info->uri, FALSE);

        g_hash_table_remove (priv->infos,
                             (gconstpointer) info->uri);

        finish_add_favorite (favorites,
                             final_new_uri,
                             mimetype,
                             FALSE);

        sync_file_metadata (favorites, final_new_uri, TRUE);

        g_free (mimetype);
    }

    g_rec_mutex_unlock (&infos_lock);

    g_free (final_new_uri);
}

static void
remove_favorite (NemoFavorites *favorites,
                 const gchar   *uri)
{
    NemoFavoritesPrivate *priv = nemo_favorites_get_instance_private (favorites);
    gchar *real_uri;

    if (g_str_has_prefix (uri, "favorites"))
    {
        GFile *file = g_file_new_for_uri (uri);
        real_uri = nemo_favorite_vfs_file_get_real_uri (file);

        g_object_unref (file);
    }
    else
    {
        real_uri = g_strdup (uri);
    }

    g_return_if_fail (real_uri != NULL);

    g_debug ("NemoFavorites: remove favorite: %s", real_uri);

    // It may be orphaned for some reason.. even if it's not in gsettings, still try
    // to remove the favorite attribute.
    sync_file_metadata (favorites, real_uri, FALSE);

    g_rec_mutex_lock (&infos_lock);

    if (!g_hash_table_remove (priv->infos, real_uri))
    {
        g_rec_mutex_unlock (&infos_lock);

        g_debug ("NemoFavorites: remove_favorite: could not find favorite for uri '%s'", real_uri);
        g_free (real_uri);
        return;
    }

    g_free (real_uri);

    store_favorites (favorites);

    g_rec_mutex_unlock (&infos_lock);

    queue_changed (favorites);
}

/* A short label for a favorite's parent dir, used to tell apart favorites that
 * share a basename. Home-relative when possible, the native path otherwise,
 * ellipsized in the middle when long. */
static gchar *
favorite_parent_label (GFile *parent_file, const gchar *fallback_uri)
{
    GFile *home_file = g_file_new_for_path (g_get_home_dir ());
    gchar *label = NULL;

    if (g_file_equal (parent_file, home_file))
    {
        label = g_strdup ("~");
    }
    else if (g_file_has_prefix (parent_file, home_file))
    {
        gchar *rel = g_file_get_relative_path (home_file, parent_file);
        label = g_strconcat ("~/", rel, NULL);
        g_free (rel);
    }
    else if (g_file_is_native (parent_file))
    {
        label = g_file_get_path (parent_file);
    }

    g_object_unref (home_file);

    if (label == NULL)
        label = g_strdup (fallback_uri);

    if (g_utf8_strlen (label, -1) > MAX_DISPLAY_URI_LENGTH)
    {
        glong len = g_utf8_strlen (label, -1);
        glong head = (MAX_DISPLAY_URI_LENGTH - 3) / 2;
        glong tail = MAX_DISPLAY_URI_LENGTH - 3 - head;
        const gchar *head_end = g_utf8_offset_to_pointer (label, head);
        const gchar *tail_start = g_utf8_offset_to_pointer (label, len - tail);
        gchar *head_str = g_strndup (label, head_end - label);
        gchar *ellipsized = g_strconcat (head_str, "...", tail_start, NULL);

        g_free (head_str);
        g_free (label);
        label = ellipsized;
    }

    return label;
}

/* Callers hold infos_lock - this walks the table and rewrites display names in
 * place. */
static void
deduplicate_display_names (G_GNUC_UNUSED NemoFavorites *favorites,
                           GHashTable    *infos)
{
    GList *fav_uris, *ptr;
    GHashTable *lists_of_keys_by_basename = g_hash_table_new_full (g_str_hash, g_str_equal,
                                                                   g_free, NULL);
    GHashTableIter iter;

    fav_uris = g_hash_table_get_keys (infos);

    for (ptr = fav_uris; ptr != NULL; ptr = ptr->next)
    {
        GList *uris;
        const gchar *uri = (gchar *) ptr->data;
        gchar *original_display_name = g_path_get_basename (uri);

        if (g_hash_table_contains (lists_of_keys_by_basename, original_display_name))
        {
            uris = g_hash_table_lookup (lists_of_keys_by_basename, original_display_name);

            // this could be prepend, but then the value in the table would have to be replaced
            uris = g_list_append ((GList *) uris, g_strdup (uri));
        }
        else
        {
            uris = g_list_prepend (NULL, g_strdup (uri));
            g_hash_table_insert (lists_of_keys_by_basename,
                                 g_strdup (original_display_name),
                                 uris);
        }

        g_free (original_display_name);
    }

    g_list_free (fav_uris);

    gpointer key, value;

    g_hash_table_iter_init (&iter, lists_of_keys_by_basename);

    while (g_hash_table_iter_next (&iter, &key, &value))
    {
        GList *same_names_list, *uri_ptr;
        gchar *common_display_name = NULL;

        if (((GList *) value)->next == NULL)
        {
            // Single member of current common name list;
            g_list_free_full ((GList *) value, g_free);
            continue;
        }
        // Now we know we have a list of uris that would have identical display names
        // Add a part of the uri after each to distinguish them.
        common_display_name = g_uri_unescape_string ((const gchar *) key, NULL);
        same_names_list = (GList *) value;

        /* The display name is the favorites:/// identity, so within a colliding
         * group it must come out unique. Borrowed set of the names already
         * handed out this pass (values live on in info->display_name). */
        GHashTable *taken = g_hash_table_new (g_str_hash, g_str_equal);

        for (uri_ptr = same_names_list; uri_ptr != NULL; uri_ptr = uri_ptr->next)
        {
            NemoFavoriteInfo *info;
            GFile *uri_file, *parent_file;
            GString *new_display_string;
            const gchar *current_uri;
            gchar *parent_label;

            current_uri = (const gchar *) uri_ptr->data;

            uri_file = g_file_new_for_uri (current_uri);
            parent_file = g_file_get_parent (uri_file);

            new_display_string = g_string_new (common_display_name);
            g_string_append (new_display_string, "  (");
            parent_label = favorite_parent_label (parent_file, current_uri);
            g_string_append (new_display_string, parent_label);
            g_string_append_c (new_display_string, ')');
            g_free (parent_label);

            /* Same basename and same parent label still collides - keep the
             * label but append a counter until it is unique. */
            if (g_hash_table_contains (taken, new_display_string->str))
            {
                guint n = 2;
                gsize base_len = new_display_string->len;

                do {
                    g_string_truncate (new_display_string, base_len);
                    g_string_append_printf (new_display_string, " %u", n++);
                } while (g_hash_table_contains (taken, new_display_string->str));
            }

            g_object_unref (uri_file);
            g_object_unref (parent_file);

            // Look up the info from our master table
            info = g_hash_table_lookup (infos, current_uri);
            g_free (info->display_name);

            info->display_name = g_string_free (new_display_string, FALSE);
            g_hash_table_add (taken, info->display_name);
        }

        g_hash_table_destroy (taken);

        g_free (common_display_name);
        g_list_free_full (same_names_list, g_free);
    }

    // We freed the individual lists just above, only the keys will need
    // freed here.
    g_hash_table_destroy (lists_of_keys_by_basename);
}

static void
on_display_name_received (GObject      *source,
                          GAsyncResult *res,
                          gpointer      user_data)
{
    GFile *file;
    GFileInfo *file_info;
    GError *error;
    gchar *display_name;
    g_autofree gchar *uri = NULL;

    file = G_FILE (source);
    error = NULL;

    uri = g_file_get_uri (file);
    file_info = g_file_query_info_finish (file, res, &error);

    if (error)
    {
        g_debug ("NemoFavorites: problem trying to get real display name for uri '%s': %s",
               uri, error->message);
        g_error_free (error);
        return;
    }

    g_return_if_fail (NEMO_IS_FAVORITES (user_data));

    NemoFavorites *favorites = NEMO_FAVORITES (user_data);
    NemoFavoritesPrivate *priv = nemo_favorites_get_instance_private (favorites);

    display_name = NULL;

    if (file_info)
    {
        NemoFavoriteInfo *info;
        const gchar *real_display_name = g_file_info_get_display_name (file_info);

        g_rec_mutex_lock (&infos_lock);

        info = g_hash_table_lookup (priv->infos, uri);

        if (info != NULL && g_strcmp0 (info->display_name, real_display_name) != 0)
        {
            gchar *old_name = info->display_name;
            info->display_name = g_strdup (real_display_name);
            g_free (old_name);

            deduplicate_display_names (favorites, priv->infos);

            g_rec_mutex_unlock (&infos_lock);
            queue_changed (favorites);
        }
        else
        {
            g_rec_mutex_unlock (&infos_lock);
        }
    }

    g_free (display_name);
    g_clear_object (&file_info);
}

static void
finish_add_favorite (NemoFavorites *favorites,
                     const gchar   *uri,
                     const gchar   *cached_mimetype,
                     gboolean       from_saved)
{
    NemoFavoritesPrivate *priv = nemo_favorites_get_instance_private (favorites);
    NemoFavoriteInfo *info;
    gchar *unescaped_uri;

    g_return_if_fail (uri != NULL);

    g_rec_mutex_lock (&infos_lock);

    // Check if it's there again, in case it was added while we were getting mimetype.
    if (g_hash_table_contains (priv->infos, uri))
    {
        g_rec_mutex_unlock (&infos_lock);

        g_debug ("NemoFavorites: favorite for '%s' exists, ignoring", uri);
        return;
    }

    info = g_slice_new0 (NemoFavoriteInfo);
    info->uri = g_strdup (uri);

    unescaped_uri = g_uri_unescape_string (uri, NULL);
    info->display_name = g_path_get_basename (unescaped_uri);
    g_free (unescaped_uri);

    info->cached_mimetype = g_strdup (cached_mimetype);

    g_hash_table_insert (priv->infos, (gpointer) g_strdup (uri), (gpointer) info);

    g_debug ("NemoFavorites: added favorite: %s", uri);

    deduplicate_display_names (favorites, priv->infos);

    g_rec_mutex_unlock (&infos_lock);

    GFile *gfile = g_file_new_for_uri (uri);
    g_file_query_info_async (gfile,
                             G_FILE_ATTRIBUTE_STANDARD_DISPLAY_NAME,
                             G_FILE_QUERY_INFO_NONE,
                             G_PRIORITY_LOW,
                             NULL,
                             on_display_name_received,
                             favorites);
    g_object_unref (gfile);

    if (from_saved)
    {
        return;
    }

    store_favorites (favorites);
    queue_changed (favorites);
}

static void
on_content_type_info_received (GObject      *source,
                               GAsyncResult *res,
                               gpointer      user_data)
{
    NemoFavorites *favorites = NEMO_FAVORITES (user_data);
    GFile *file;
    GFileInfo *file_info;
    GError *error;
    gchar *cached_mimetype, *uri;

    file = G_FILE (source);
    uri = g_file_get_uri (file);
    error = NULL;
    cached_mimetype = NULL;

    file_info = g_file_query_info_finish (file, res, &error);

    if (error)
    {
        g_debug ("NemoFavorites: problem trying to figure out content type for uri '%s': %s",
                 uri, error->message);
        g_error_free (error);
    }

    if (file_info)
    {
        cached_mimetype = g_strdup (g_file_info_get_attribute_string (file_info, G_FILE_ATTRIBUTE_STANDARD_CONTENT_TYPE));

        if (cached_mimetype == NULL)
        {
            cached_mimetype = g_strdup ("application/unknown");
        }

        finish_add_favorite (favorites,
                             uri,
                             cached_mimetype,
                             FALSE);

        sync_file_metadata (favorites, uri, TRUE);
    }

    g_free (uri);
    g_free (cached_mimetype);
    g_clear_object (&file_info);
    g_object_unref (file);
}

static void
add_favorite (NemoFavorites *favorites,
              const gchar   *uri)
{
    NemoFavoritesPrivate *priv = nemo_favorites_get_instance_private (favorites);
    GFile *file;
    gboolean known;

    g_rec_mutex_lock (&infos_lock);
    known = g_hash_table_contains (priv->infos, uri);
    g_rec_mutex_unlock (&infos_lock);

    if (known)
    {
        g_debug ("NemoFavorites: favorite for '%s' exists, ignoring", uri);
        return;
    }

    file = g_file_new_for_uri (uri);

    g_file_query_info_async (file,
                             G_FILE_ATTRIBUTE_STANDARD_CONTENT_TYPE,
                             G_FILE_QUERY_INFO_NONE,
                             G_PRIORITY_LOW,
                             NULL,
                             on_content_type_info_received,
                             favorites);
}

static void
on_settings_list_changed (G_GNUC_UNUSED NemoConfigGroup *settings,
                          G_GNUC_UNUSED gchar     *key,
                          gpointer   user_data)
{
    NemoFavorites *favorites = NEMO_FAVORITES (user_data);

    load_favorites (favorites, TRUE);
}

static void
nemo_favorites_init (NemoFavorites *favorites)
{
    NemoFavoritesPrivate *priv = nemo_favorites_get_instance_private (favorites);

    g_debug ("NemoFavorites: init:");

    priv->settings = nemo_config_get_group (FAVORITES_SCHEMA);
    priv->settings_listener_id = g_signal_connect (priv->settings,
                                                   "changed::" FAVORITES_KEY,
                                                   G_CALLBACK (on_settings_list_changed),
                                                   favorites);

    load_favorites (favorites, FALSE);
}

static void
nemo_favorites_dispose (GObject *object)
{
    NemoFavorites *favorites = NEMO_FAVORITES (object);
    NemoFavoritesPrivate *priv = nemo_favorites_get_instance_private (favorites);

    g_debug ("NemoFavorites dispose (%p)", object);

    /* The queued idle and the settings handler both call back in here, so they
     * have to go before the list does. */
    g_rec_mutex_lock (&infos_lock);
    if (priv->changed_timer_id > 0)
    {
        g_source_remove (priv->changed_timer_id);
        priv->changed_timer_id = 0;
    }
    g_rec_mutex_unlock (&infos_lock);

    /* Borrowed from the config store, which outlives us - only the handler is
     * ours to drop. */
    if (priv->settings != NULL)
    {
        g_clear_signal_handler (&priv->settings_listener_id, priv->settings);
        priv->settings = NULL;
    }

    g_rec_mutex_lock (&infos_lock);
    g_clear_pointer (&priv->infos, g_hash_table_destroy);
    g_rec_mutex_unlock (&infos_lock);

    G_OBJECT_CLASS (nemo_favorites_parent_class)->dispose (object);
}

static void
nemo_favorites_finalize (GObject *object)
{
    g_debug ("NemoFavorites finalize (%p)", object);

    G_OBJECT_CLASS (nemo_favorites_parent_class)->finalize (object);
}

static void
nemo_favorites_class_init (NemoFavoritesClass *klass)
{
    GObjectClass *gobject_class = G_OBJECT_CLASS (klass);

    gobject_class->dispose = nemo_favorites_dispose;
    gobject_class->finalize = nemo_favorites_finalize;

    /**
     * NemoFavorites::changed:

     * Notifies when the favorites list has changed.
     */
    signals [CHANGED] =
        g_signal_new ("changed",
                      NEMO_TYPE_FAVORITES,
                      G_SIGNAL_RUN_FIRST | G_SIGNAL_ACTION,
                      0,
                      NULL, NULL, NULL,
                      G_TYPE_NONE, 0);
}

/**
 * nemo_favorites_get_default:
 *
 * Returns the #NemoFavorites instance.
 *
 * Returns: (transfer none): the NemoFavorites instance for the process. Do not free.
 *
 * Since: 2.0
 */
NemoFavorites *
nemo_favorites_get_default (void)
{
    if (global_favorites == NULL)
    {
        nemo_favorite_vfs_register ();
        global_favorites = g_object_new (NEMO_TYPE_FAVORITES, NULL);
    }

    return global_favorites;
}

typedef struct {
    GList *items;
    const gchar **mimetypes;
} MatchData;

void
match_mimetypes (G_GNUC_UNUSED gpointer key,
                 gpointer value,
                 gpointer user_data)
{
    MatchData *data = (MatchData *) user_data;
    const NemoFavoriteInfo *info = (NemoFavoriteInfo *) value;

    if (data->mimetypes == NULL)
    {
        data->items = g_list_prepend (data->items, nemo_favorite_info_copy (info));
        return;
    }

    guint i;

    for (i = 0; i < g_strv_length ((gchar **) data->mimetypes); i++)
    {
        if (g_content_type_is_mime_type (info->cached_mimetype, data->mimetypes[i]))
        {
            data->items = g_list_prepend (data->items, nemo_favorite_info_copy (info));
            return;
        }
    }
}

/**
 * nemo_favorites_get_favorites:
 * @favorites: The #NemoFavorites
 * @mimetypes: (nullable) (array zero-terminated=1): The mimetypes to filter by for results
 *
 * Gets a list of all favorites.  If mimetype is not %NULL, the list will
 * contain only favorites with that mimetype.
 *
 * Returns: (element-type NemoFavoriteInfo) (transfer full): a list of #NemoFavoriteInfos.
            Free the list with #g_list_free, free elements with #nemo_favorite_info_free.
 *
 * Since: 2.0
 */
GList *
nemo_favorites_get_favorites (NemoFavorites       *favorites,
                              const gchar * const *mimetypes)
{
    g_return_val_if_fail (NEMO_IS_FAVORITES (favorites), NULL);
    NemoFavoritesPrivate *priv = nemo_favorites_get_instance_private (favorites);
    GList *ret = NULL;
    MatchData data;

    data.items = NULL;
    data.mimetypes = (const gchar **) mimetypes;

    g_rec_mutex_lock (&infos_lock);
    g_hash_table_foreach (priv->infos,
                          (GHFunc) match_mimetypes,
                          &data);
    g_rec_mutex_unlock (&infos_lock);

    ret = g_list_reverse (data.items);

    gchar *typestring = mimetypes ? g_strjoinv (", ", (gchar **) mimetypes) : NULL;
    g_debug ("NemoFavorites: get_favorites returning list for mimetype '%s' (%d items)",
             typestring != NULL ? typestring : "any", g_list_length (ret));
    g_free (typestring);

    return ret;
}

/**
 * nemo_favorites_get_n_favorites:
 * @favorites: The #NemoFavorites
 *
 * Returns: The number of favorite files

 * Since: 2.0
 */
gint
nemo_favorites_get_n_favorites (NemoFavorites *favorites)
{
    g_return_val_if_fail (NEMO_IS_FAVORITES (favorites), 0);
    NemoFavoritesPrivate *priv = nemo_favorites_get_instance_private (favorites);
    gint n;

    g_rec_mutex_lock (&infos_lock);
    n = g_hash_table_size (priv->infos);
    g_rec_mutex_unlock (&infos_lock);

    g_debug ("NemoFavorites: get_n_favorites returning number of items: %d.", n);

    return n;
}

static gboolean
lookup_display_name (G_GNUC_UNUSED gpointer key,
                     gpointer value,
                     gpointer user_data)
{
    NemoFavoriteInfo *info = (NemoFavoriteInfo *) value;

    if (g_strcmp0 (info->display_name, (const gchar *) user_data) == 0)
    {
        return TRUE;
    }

    return FALSE;
}

/**
 * nemo_favorites_find_by_display_name:
 * @favorites: The #NemoFavorites
 * @display_name: (not nullable): The display name to lookup info for.
 *
 * Looks for an NemoFavoriteInfo that corresponds to @display_name.
 *
 * Returns: (transfer none): an NemoFavoriteInfo or NULL if one was not found. This is owned
 *          by the favorites manager and should not be freed. Only safe to hold on
 *          the main thread - use _nemo_favorites_dup_by_display_name off it.
 *
 * Since: 2.0
 */
NemoFavoriteInfo *
nemo_favorites_find_by_display_name (NemoFavorites *favorites,
                                     const gchar   *display_name)
{
    g_return_val_if_fail (NEMO_IS_FAVORITES (favorites), NULL);
    g_return_val_if_fail (display_name != NULL, NULL);

    NemoFavoriteInfo *info;
    NemoFavoritesPrivate *priv = nemo_favorites_get_instance_private (favorites);

    g_rec_mutex_lock (&infos_lock);

    info = g_hash_table_find (priv->infos,
                              (GHRFunc) lookup_display_name,
                              (gpointer) display_name);

    g_rec_mutex_unlock (&infos_lock);

    return info;
}

/* The two below exist so the vfs, which runs on GIO worker threads, never holds
 * a borrowed pointer into a table the main thread may replace. */
gboolean
_nemo_favorites_has_display_name (NemoFavorites *favorites,
                                  const gchar   *display_name)
{
    g_return_val_if_fail (NEMO_IS_FAVORITES (favorites), FALSE);
    g_return_val_if_fail (display_name != NULL, FALSE);

    NemoFavoritesPrivate *priv = nemo_favorites_get_instance_private (favorites);
    gboolean found;

    g_rec_mutex_lock (&infos_lock);

    found = g_hash_table_find (priv->infos,
                               (GHRFunc) lookup_display_name,
                               (gpointer) display_name) != NULL;

    g_rec_mutex_unlock (&infos_lock);

    return found;
}

NemoFavoriteInfo *
_nemo_favorites_dup_by_display_name (NemoFavorites *favorites,
                                     const gchar   *display_name)
{
    g_return_val_if_fail (NEMO_IS_FAVORITES (favorites), NULL);
    g_return_val_if_fail (display_name != NULL, NULL);

    NemoFavoritesPrivate *priv = nemo_favorites_get_instance_private (favorites);
    NemoFavoriteInfo *info, *copy;

    g_rec_mutex_lock (&infos_lock);

    info = g_hash_table_find (priv->infos,
                              (GHRFunc) lookup_display_name,
                              (gpointer) display_name);

    copy = info != NULL ? nemo_favorite_info_copy (info) : NULL;

    g_rec_mutex_unlock (&infos_lock);

    return copy;
}

/**
 * nemo_favorites_find_by_uri:
 * @favorites: The #NemoFavorites
 * @uri: (not nullable): The uri to lookup info for.
 *
 * Looks for an NemoFavoriteInfo that corresponds to @uri.
 *
 * Returns: (transfer none): an NemoFavoriteInfo or NULL if one was not found. This is owned
 *          by the favorites manager and should not be freed.
 *
 * Since: 2.0
 */
NemoFavoriteInfo *
nemo_favorites_find_by_uri (NemoFavorites *favorites,
                            const gchar   *uri)
{
    g_return_val_if_fail (NEMO_IS_FAVORITES (favorites), NULL);
    g_return_val_if_fail (uri != NULL, NULL);

    NemoFavoriteInfo *info;
    NemoFavoritesPrivate *priv = nemo_favorites_get_instance_private (favorites);

    g_rec_mutex_lock (&infos_lock);
    info = g_hash_table_lookup (priv->infos, uri);
    g_rec_mutex_unlock (&infos_lock);

    return info;
}

/**
 * nemo_favorites_add:
 * @favorites: The #NemoFavorites
 * @uri: The uri the favorite is for
 *
 * Adds a new favorite.  If the uri already exists, this does nothing.
 *
 * Since: 2.0
 */
void
nemo_favorites_add (NemoFavorites *favorites,
                    const gchar   *uri)
{
    g_return_if_fail (NEMO_IS_FAVORITES (favorites));
    g_return_if_fail (uri != NULL);

    add_favorite (favorites, uri);
}

/**
 * nemo_favorites_remove:
 * @favorites: The #NemoFavorites
 * @uri: The uri for the favorite being removed
 *
 * Removes a favorite from the list.
 *
 * Since: 2.0
 */
void
nemo_favorites_remove (NemoFavorites *favorites,
                       const gchar   *uri)
{
    g_return_if_fail (NEMO_IS_FAVORITES (favorites));
    g_return_if_fail (uri != NULL);

    remove_favorite (favorites, uri);
}

/**
 * nemo_favorites_rename:
 * @old_uri: the old favorite's uri.
 * @new_uri: The new uri.
 *
 * Removes old_uri and adds new_uri. This is mainly for file managers to use as
 * a convenience instead of add/remove, and guarantees the result, without having to
 * worry about multiple dbus calls (gsettings).
 *
 * Since: 2.0
 */
void
nemo_favorites_rename (NemoFavorites *favorites,
                       const gchar   *old_uri,
                       const gchar   *new_uri)
{
    g_return_if_fail (NEMO_IS_FAVORITES (favorites));
    g_return_if_fail (old_uri != NULL && new_uri != NULL);

    rename_favorite (favorites, old_uri, new_uri);
}


/* Used by nemo_favorite_vfs_file. The names are copies - the caller reads them
 * on a worker thread, where the table itself is not its to hold. */
GList *
_nemo_favorites_get_display_names (NemoFavorites *favorites)
{
    g_return_val_if_fail (NEMO_IS_FAVORITES (favorites), NULL);
    NemoFavoritesPrivate *priv = nemo_favorites_get_instance_private (favorites);
    GHashTableIter iter;
    GList *ret;
    gpointer key, value;

    ret = NULL;

    g_rec_mutex_lock (&infos_lock);

    g_hash_table_iter_init (&iter, priv->infos);

    while (g_hash_table_iter_next (&iter, &key, &value))
    {
        NemoFavoriteInfo *info = (NemoFavoriteInfo *) value;
        ret = g_list_prepend (ret, g_strdup (info->display_name));
    }

    g_rec_mutex_unlock (&infos_lock);

    ret = g_list_reverse (ret);
    return ret;
}
