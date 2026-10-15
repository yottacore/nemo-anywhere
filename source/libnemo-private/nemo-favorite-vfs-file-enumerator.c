/* nemo-favorite-vfs-file-enumerator.c - favorites:/// vfs.
 *
 * Adapted from libxapp 2.8.8 (favorite-vfs-file-enumerator.c, LGPL-2.1-or-later,
 * © Linux Mint team), relicensed under GPL-2.0 per LGPL-2.1 section 3.
 */

#include <eel/eel-glib-extensions.h>

#include "nemo-favorites.h"
#include "nemo-favorite-vfs-file-enumerator.h"
#include "nemo-favorite-vfs-file.h"


typedef struct
{
    GFile *file;

    GList *uris;
    gchar *attributes;
    GFileQueryInfoFlags flags;

    GList *current_pos;
} NemoFavoriteVfsFileEnumeratorPrivate;

struct _NemoFavoriteVfsFileEnumerator
{
    GObject parent_instance;
    NemoFavoriteVfsFileEnumeratorPrivate *priv;
};

G_DEFINE_TYPE_WITH_PRIVATE(NemoFavoriteVfsFileEnumerator,
                           nemo_favorite_vfs_file_enumerator,
                           G_TYPE_FILE_ENUMERATOR)

static GFileInfo *
next_file (GFileEnumerator *enumerator,
           GCancellable    *cancellable,
           GError         **error)
{
    NemoFavoriteVfsFileEnumerator *self = NEMO_FAVORITE_VFS_FILE_ENUMERATOR (enumerator);
    NemoFavoriteVfsFileEnumeratorPrivate *priv = nemo_favorite_vfs_file_enumerator_get_instance_private (self);
    GFileInfo *info;

    if (g_cancellable_set_error_if_cancelled (cancellable, error))
    {
        return NULL;
    }

    info = NULL;

    while (priv->current_pos != NULL && info == NULL)
    {
        const gchar *display_name = (const gchar *) priv->current_pos->data;
        gchar *uri;
        GFile *file;

        /* Advance before doing anything else: every way out of an iteration has
         * to make progress, or an entry that yields nothing spins here forever,
         * piling a fresh GError onto *error each pass. */
        priv->current_pos = priv->current_pos->next;

        if (!_nemo_favorites_has_display_name (nemo_favorites_get_default (), display_name))
        {
            /* Went away between the listing being taken and us reaching it.
             * Skip it - failing here would throw away the whole folder over one
             * stale row. */
            g_debug ("NemoFavorites: '%s' is gone, leaving it out of the listing", display_name);
            continue;
        }

        uri = nemo_path_to_fav_uri (display_name);
        file = g_file_new_for_uri (uri);

        info = g_file_query_info (file,
                                  priv->attributes,
                                  priv->flags,
                                  cancellable,
                                  error);

        g_object_unref (file);
        g_free (uri);

        if (info == NULL)
        {
            /* error carries the reason, if the caller asked for one. */
            break;
        }
    }

    return info;
}

static void
next_async_op_free (GList *files)
{
    g_list_free_full (files, g_object_unref);
}

static void
next_files_async_thread (GTask        *task,
                         gpointer      source_object,
                         G_GNUC_UNUSED gpointer      task_data,
                         GCancellable *cancellable)
{
    NemoFavoriteVfsFileEnumerator *self = NEMO_FAVORITE_VFS_FILE_ENUMERATOR (source_object);
    GList *ret;
    GError *error = NULL;
    gint i, n_requested;

    n_requested = GPOINTER_TO_INT (g_task_get_task_data (task));

    ret = NULL;

    for (i = 0; i < n_requested; i++)
    {
        GFileInfo *info = NULL;
        // g_clear_error (&error);

        if (g_cancellable_set_error_if_cancelled (cancellable, &error))
        {
            /* Same as the failure tail below: what was gathered goes nowhere. */
            g_list_free_full (ret, g_object_unref);
            g_task_return_error (task, error);
            return;
        }
        else
        {
            info = next_file (G_FILE_ENUMERATOR (self), cancellable, &error);
        }

        if (info != NULL)
        {
            ret = g_list_prepend (ret, info);
        }
        else
        {
            if (error)
            {
                g_critical ("ERROR: %s\n", error->message);
            }
            break;
        }
    }

    if (error)
    {
        /* The infos gathered before the failure go nowhere now. */
        g_list_free_full (ret, g_object_unref);
        g_task_return_error (task, error);
    }
    else
    {
        ret = g_list_reverse (ret);
        g_task_return_pointer (task, ret, (GDestroyNotify) next_async_op_free);
    }
}

static void
next_files_async (GFileEnumerator     *enumerator,
                  gint                 num_files,
                  gint                 io_priority,
                  GCancellable        *cancellable,
                  GAsyncReadyCallback  callback,
                  gpointer             user_data)
{
    // NemoFavoriteVfsFileEnumerator *self = NEMO_FAVORITE_VFS_FILE_ENUMERATOR (enumerator);

    GTask *task;
    task = g_task_new (enumerator, cancellable, callback, user_data);
    g_task_set_priority (task, io_priority);
    g_task_set_task_data (task, GINT_TO_POINTER (num_files), NULL);

    g_task_run_in_thread (task, next_files_async_thread);
    g_object_unref (task);
}

static GList *
next_files_finished (GFileEnumerator  *enumerator,
                     GAsyncResult     *result,
                     GError          **error)
{
    g_return_val_if_fail (G_IS_FILE_ENUMERATOR (enumerator), NULL);
    g_return_val_if_fail (G_IS_ASYNC_RESULT (result), NULL);

    return (GList *) g_task_propagate_pointer (G_TASK (result), error);
}

static gboolean
close_fn (G_GNUC_UNUSED GFileEnumerator *enumerator,
          G_GNUC_UNUSED GCancellable    *cancellable,
          G_GNUC_UNUSED GError         **error)
{
    // NemoFavoriteVfsFileEnumerator *self = NEMO_FAVORITE_VFS_FILE_ENUMERATOR (enumerator);

    return TRUE;
}

static void
nemo_favorite_vfs_file_enumerator_init (G_GNUC_UNUSED NemoFavoriteVfsFileEnumerator *self)
{
}

static void
nemo_favorite_vfs_file_enumerator_dispose (GObject *object)
{
    G_OBJECT_CLASS (nemo_favorite_vfs_file_enumerator_parent_class)->dispose (object);
}

static void
nemo_favorite_vfs_file_enumerator_finalize (GObject *object)
{
    NemoFavoriteVfsFileEnumerator *self = NEMO_FAVORITE_VFS_FILE_ENUMERATOR(object);
    NemoFavoriteVfsFileEnumeratorPrivate *priv = nemo_favorite_vfs_file_enumerator_get_instance_private(self);

    g_list_free_full (priv->uris, (GDestroyNotify) g_free);
    g_free (priv->attributes);
    g_object_unref (priv->file);

    G_OBJECT_CLASS (nemo_favorite_vfs_file_enumerator_parent_class)->finalize (object);
}

static void
nemo_favorite_vfs_file_enumerator_class_init (NemoFavoriteVfsFileEnumeratorClass *klass)
{
    GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
    GFileEnumeratorClass *enumerator_class = G_FILE_ENUMERATOR_CLASS (klass);

    gobject_class->dispose = nemo_favorite_vfs_file_enumerator_dispose;
    gobject_class->finalize = nemo_favorite_vfs_file_enumerator_finalize;

    enumerator_class->next_file = next_file;
    enumerator_class->next_files_async = next_files_async;
    enumerator_class->next_files_finish = next_files_finished;
    enumerator_class->close_fn = close_fn;
}

/* Returns: (transfer full): unref with g_object_unref */
GFileEnumerator *
nemo_favorite_vfs_file_enumerator_new (GFile               *file,
                                  const gchar         *attributes,
                                  GFileQueryInfoFlags  flags,
                                  GList               *uris)
{
    NemoFavoriteVfsFileEnumerator *enumerator = g_object_new (NEMO_TYPE_NEMO_FAVORITE_VFS_FILE_ENUMERATOR, NULL);
    NemoFavoriteVfsFileEnumeratorPrivate *priv = nemo_favorite_vfs_file_enumerator_get_instance_private(enumerator);

    priv->uris = eel_g_str_list_copy (uris);
    priv->current_pos = priv->uris;

    priv->file = g_object_ref (file);
    priv->attributes = g_strdup (attributes);
    priv->flags = flags;

    return G_FILE_ENUMERATOR (enumerator);
}

