/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*-

   nemo-thumbnail-cache.h: Thumbnail code for icon factory.
 
   Copyright (C) 2000, 2001 Eazel, Inc.
   Copyright (C) 2002, 2003 Red Hat, Inc.
  
   This program is free software; you can redistribute it and/or
   modify it under the terms of the GNU General Public License as
   published by the Free Software Foundation; either version 2 of the
   License, or (at your option) any later version.
  
   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   General Public License for more details.
  
   You should have received a copy of the GNU General Public
   License along with this program; if not, write to the
   Free Software Foundation, Inc., 51 Franklin Street - Suite 500,
   Boston, MA 02110-1335, USA.
  
   Author: Andy Hertzfeld <andy@eazel.com>
*/

#include <config.h>
#include "nemo-thumbnails.h"


#include "nemo-directory-notify.h"
#include "nemo-global-preferences.h"
#include "nemo-file-utilities.h"
#include "nemo-file-digest.h"
#include "nemo-thumbnail-memory.h"
#include <math.h>
#include <eel/eel-graphic-effects.h>
#include <eel/eel-string.h>
#include <eel/eel-debug.h>
#include <eel/eel-vfs-extensions.h>
#include <gtk/gtk.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#ifdef G_OS_UNIX
#include <sys/wait.h>
#endif
#include <unistd.h>
#include <signal.h>
#include <libnemo-private/nemo-desktop-thumbnail.h>

#define DEBUG_FLAG NEMO_DEBUG_THUMBNAILS
#include <libnemo-private/nemo-debug.h>

#include "nemo-file-private.h"

#define DEBUG_THREADS 0

/* Should never be a reasonable actual mtime */
#define INVALID_MTIME 0

/* Cool-off period between last file modification time and thumbnail creation */
#define RECENT_MTIME_COOLDOWN 2

#define NEMO_THUMBNAIL_FRAME_LEFT 3
#define NEMO_THUMBNAIL_FRAME_TOP 3
#define NEMO_THUMBNAIL_FRAME_RIGHT 3
#define NEMO_THUMBNAIL_FRAME_BOTTOM 3


typedef enum {
    THUMBNAIL_ADD,
    THUMBNAIL_REMOVE,
    THUMBNAIL_THREAD_EXIT
} ThumbnailCommandType;

/* multipurpose structure used for making thumbnails, associating a uri with where the thumbnail is to be stored */
typedef struct NemoThumbnailInfo {
    char *image_uri;
    char *mime_type;
    time_t original_file_mtime;
    NemoFileId id;              /* bytes and mtime, and later the checksum */
    int size;                   /* largest size asked for, in pixels */
    gint64 order;               /* lower goes first */
    ThumbnailCommandType cmd_type;
    guint cancelled : 1;
    guint read_anyway : 1;      /* making it reads the whole file, so checksum it too */
    guint had_thumbnail : 1;    /* a smaller one is already stored */
    guint save_checksum : 1;    /* write the checksum onto the file too */
    guint ahead : 1;            /* not on screen yet; only the store wants it */
    guint load_only : 1;        /* already made; read it back to hold near the view */
    guint running : 1;          /* a worker has it, so nothing above changes now */
    struct NemoThumbnailInfo *follow;   /* a bigger ask that came in meanwhile, started when this one ends */
} NemoThumbnailInfo;

/* How it works:
 *
 * When nemo_create_thumbnail(), nemo_thumbnail_render_ahead() or nemo_thumbnail_remove_from_queue are called,
 * a new NemoThumbnailInfo is made, with info->cmd_type set based on the method called.
 *
 * These are added to the feeder queue, which feeds the feeder_task thread. When a new info arrives, it gets
 * processed based on info->cmd_type.

 * - THUMBNAIL_ADD: The info is looked up by uri in thumbnails_to_make_hash. If the info is already there, the
 *   existing info's mtime and size are updated. It keeps its place, unless the add comes from a folder being
 *   queued again in view order, which gives it the new one. Otherwise, the incoming info is added to
 *   thumbnails_to_make_hash, and pushed to the threadpool queue.
 *
 *   An info a worker already has is left alone, or the picture drawn at the old size would be stored as the
 *   new one. A bigger ask takes its place in the table and waits as its follow, pushed when it ends. A smaller
 *   or equal one is answered by what is being made.
 *
 * - nemo_thumbnail_remove_from_queue (THUMBNAIL_REMOVE): The info is looked up by uri in thumbnails_to_make_hash.
 *   If the info is found, it gets removed from thumbnails_to_make_hash, and info->cancelled is set to TRUE, so when
 *   it comes up in the threadpool queue, it is ignored and freed.
 *
 * The pool runs in `order`, lowest first. A folder queued ahead gets a band, and each file its place in the
 * view within the band, so it is made top down whatever is scrolled into view meanwhile. A newer band goes
 * before what is left of an older one. A file asked for on its own goes before every band, first come first
 * served, unless a band already gave it a place.
 *
 * - No mutex locking occurs in the public methods, only in the feeder and threadpool threads.
 * - NemoThumbnailInfos are garbage-collected in the threadpool worker only.
 */

/* Workers that actually make the thumbnail. */
static volatile GThreadPool *tpool = NULL;

/* Table of uris queued to the thread pool (tpool) */
static GMutex thumbnails_mutex;
static GHashTable *thumbnails_to_make_hash = NULL;

/* Every action goes thru the feeder queue. It gets processed in the feeder_task's thread. */
static GAsyncQueue *feeder_queue = NULL;
static GTask *feeder_task = NULL;

/* Causes the feeder_task to end. Only called when Nemo is shutting down. */
GCancellable *cancellable = NULL;

static NemoDesktopThumbnailFactory *thumbnail_factory = NULL;

static gint
get_max_threads (void) {
    gint max_threads = 1;
    gint num_processors = g_get_num_processors ();

    gint pref = nemo_config_get_int (nemo_preferences, NEMO_PREFERENCES_MAX_THUMBNAIL_THREADS);

    /* Half, so a folder of pictures cannot take the whole machine. */
    if (pref == -1) {
        max_threads = num_processors / 2;
    } else {
        max_threads = pref;
    }

    max_threads = MAX (1, max_threads);

#if DEBUG_THREADS
    g_message ("Thumbnailer threads: %d (setting: %d, system count: %d)", max_threads, pref, num_processors);
#else
    DEBUG ("Thumbnailer threads: %d (setting: %d, system count: %d)", max_threads, pref, num_processors);
#endif

    return max_threads;
}

static gint
order_sorter (gconstpointer a,
              gconstpointer b,
              gpointer      data)
{
    gint64 oa = ((const NemoThumbnailInfo *) a)->order;
    gint64 ob = ((const NemoThumbnailInfo *) b)->order;

    return oa > ob ? +1 : oa == ob ? 0 : -1;
}

static gboolean
get_file_mtime (const char *file_uri, time_t* mtime, NemoFileId *id)
{
    GFile *file;
    GFileInfo *info;
    gboolean ret;

    ret = FALSE;
    *mtime = INVALID_MTIME;

    file = g_file_new_for_uri (file_uri);
    info = g_file_query_info (file,
                              G_FILE_ATTRIBUTE_TIME_MODIFIED ","
                              G_FILE_ATTRIBUTE_TIME_MODIFIED_USEC ","
                              G_FILE_ATTRIBUTE_STANDARD_SIZE,
                              0, NULL, NULL);
    if (info) {
        if (g_file_info_has_attribute (info, G_FILE_ATTRIBUTE_TIME_MODIFIED)) {
            *mtime = g_file_info_get_attribute_uint64 (info, G_FILE_ATTRIBUTE_TIME_MODIFIED);
            id->mtime = (gint64) *mtime * G_USEC_PER_SEC
                        + g_file_info_get_attribute_uint32 (info, G_FILE_ATTRIBUTE_TIME_MODIFIED_USEC);
            id->bytes = g_file_info_get_size (info);
            ret = TRUE;
        }

        g_object_unref (info);
    }
    g_object_unref (file);

    return ret;
}

static void
free_thumbnail_info (NemoThumbnailInfo *info)
{
    g_free (info->image_uri);
    g_free (info->mime_type);
    g_free (info);
}

static NemoDesktopThumbnailFactory *
get_thumbnail_factory (void)
{
    static gsize once_init = 0;

    if (g_once_init_enter (&once_init)) {
        thumbnail_factory = nemo_desktop_thumbnail_factory_new (NEMO_DESKTOP_THUMBNAIL_SIZE_LARGE);

        g_once_init_leave (&once_init, 1);
    }

    return thumbnail_factory;
}

static GdkPixbuf *
nemo_get_thumbnail_frame (void)
{
    static GdkPixbuf *thumbnail_frame = NULL;
    static gsize once_init = 0;

    if (g_once_init_enter (&once_init)) {
        GInputStream *stream = g_resources_open_stream ("/org/nemo/icons/thumbnail_frame.png", 0, NULL);
        if (stream != NULL) {
            thumbnail_frame = gdk_pixbuf_new_from_stream (stream, NULL, NULL);
            g_object_unref (stream);
        }

        g_once_init_leave (&once_init, 1);
    }

    return thumbnail_frame;
}

static GHashTable *
get_types_table (void)
{
    static GHashTable *image_mime_types = NULL;
    GSList *format_list, *l;
    char **types;
    int i;

    static gsize once_init = 0;

    if (g_once_init_enter (&once_init)) {
        image_mime_types = g_hash_table_new_full (g_str_hash, g_str_equal,
                                                  g_free, NULL);
        format_list = gdk_pixbuf_get_formats ();
        for (l = format_list; l; l = l->next) {
            types = gdk_pixbuf_format_get_mime_types (l->data);

            for (i = 0; types[i] != NULL; i++) {
                g_hash_table_add (image_mime_types, types[i]);
            }

            g_free (types);
        }

        g_slist_free (format_list);

        g_once_init_leave (&once_init, 1);
    }

    return image_mime_types;
}

static gboolean
pixbuf_can_load_type (const char *mime_type)
{
    GHashTable *image_mime_types;

    image_mime_types = get_types_table ();

    return g_hash_table_contains (image_mime_types, mime_type);
}

/* This is a one-shot idle callback called from the main loop to call
   notify_file_changed() for a thumbnail. It frees the uri afterwards.
   We do this in an idle callback as I don't think nemo_file_changed() is
   thread-safe. */
static gboolean
thumbnail_thread_notify_file_changed (gpointer image_uri)
{
    NemoFile *file;

    file = nemo_file_get_by_uri ((char *) image_uri);

    DEBUG ("(Thumbnail Thread) Notifying file changed file: %p uri: %s", file, (char*) image_uri);

    if (file != NULL) {
        nemo_file_set_is_thumbnailing (file, FALSE);
        nemo_file_invalidate_attributes (file,
                                         NEMO_FILE_ATTRIBUTE_THUMBNAIL |
                                         NEMO_FILE_ATTRIBUTE_INFO);
        nemo_file_unref (file);
    }

    g_free (image_uri);

    return G_SOURCE_REMOVE;
}

/* A render asked for ahead of time found the store already had it. Nothing
 * about the file changed, so unlike the above there is nothing to reload,
 * unless it came into view while it waited. */
static gboolean
thumbnail_ahead_already_stored (gpointer image_uri)
{
    NemoFile *file;

    file = nemo_file_get_by_uri ((char *) image_uri);

    if (file != NULL) {
        nemo_file_set_is_thumbnailing (file, FALSE);
        file->details->thumbnail_in_store = TRUE;
        if (nemo_file_get_load_deferred_attrs (file) != NEMO_FILE_LOAD_DEFERRED_ATTRS_NO) {
            nemo_file_invalidate_attributes (file, NEMO_FILE_ATTRIBUTE_THUMBNAIL);
        }
        nemo_file_unref (file);
    }

    g_free (image_uri);

    return G_SOURCE_REMOVE;
}

/* Read back to be held near the view, and it was not there after all. */
static gboolean
thumbnail_not_loaded (gpointer image_uri)
{
    NemoFile *file;

    file = nemo_file_get_by_uri ((char *) image_uri);

    if (file != NULL) {
        nemo_file_set_is_thumbnailing (file, FALSE);
        file->details->thumbnail_in_store = FALSE;
        nemo_file_unref (file);
    }

    g_free (image_uri);

    return G_SOURCE_REMOVE;
}

/* What a render made, on its way back to the main loop. */
typedef struct {
    char *uri;
    time_t mtime;
    gboolean ahead;
    gboolean near_view;     /* wanted near the view, so held whatever it costs */
    NemoThumbnailLoaded loaded;
} RenderDone;

/* The picture just made is handed straight to the file, rather than read
 * back out of the store it was just written to. */
static gboolean
thumbnail_render_done (gpointer data)
{
    RenderDone *done = data;
    NemoFile *file;

    file = nemo_file_get_by_uri (done->uri);

    if (file != NULL) {
        nemo_file_set_is_thumbnailing (file, FALSE);

        /* Edited while it was being drawn, so what was drawn is stale. */
        if (file->details->mtime != done->mtime) {
            nemo_file_invalidate_attributes (file, NEMO_FILE_ATTRIBUTE_THUMBNAIL);
        } else if (done->ahead && !done->near_view &&
                   nemo_file_get_load_deferred_attrs (file) == NEMO_FILE_LOAD_DEFERRED_ATTRS_NO &&
                   !nemo_thumbnail_memory_has_room (done->loaded.pixbuf)) {
            /* Still not in view, and the memory for pictures is used up. It is
               read back from the store once it comes near the view. */
            file->details->thumbnail_in_store = done->loaded.from_store;
        } else {
            gboolean seen = nemo_file_get_load_deferred_attrs (file) != NEMO_FILE_LOAD_DEFERRED_ATTRS_NO;

            nemo_file_take_thumbnail (file, &done->loaded);

            /* One never on screen is drawn from what it holds when it gets
               there, so there is nothing to redraw now. */
            if (seen || done->near_view) {
                nemo_file_changed (file);
            }
        }

        nemo_file_unref (file);
    }

    g_clear_object (&done->loaded.pixbuf);
    g_free (done->uri);
    g_free (done);

    return G_SOURCE_REMOVE;
}

/* A photo decoded with an alpha channel is usually opaque all the same, and
 * only real transparency is worth giving up JPEG for. */
static gboolean
has_transparency (GdkPixbuf *pixbuf)
{
    const guchar *pixels;
    int width, height, rowstride, x, y;

    if (!gdk_pixbuf_get_has_alpha (pixbuf))
        return FALSE;

    pixels = gdk_pixbuf_read_pixels (pixbuf);
    width = gdk_pixbuf_get_width (pixbuf);
    height = gdk_pixbuf_get_height (pixbuf);
    rowstride = gdk_pixbuf_get_rowstride (pixbuf);

    for (y = 0; y < height; y++) {
        const guchar *row = pixels + (gsize) y * rowstride;

        for (x = 0; x < width; x++) {
            if (row[x * 4 + 3] != 255)
                return TRUE;
        }
    }

    return FALSE;
}

static GdkPixbuf *
drop_alpha (GdkPixbuf *pixbuf)
{
    GdkPixbuf *opaque;
    const guchar *src;
    guchar *dst;
    int width, height, src_stride, dst_stride, x, y;

    width = gdk_pixbuf_get_width (pixbuf);
    height = gdk_pixbuf_get_height (pixbuf);

    opaque = gdk_pixbuf_new (GDK_COLORSPACE_RGB, FALSE, 8, width, height);
    if (opaque == NULL)
        return NULL;

    src = gdk_pixbuf_read_pixels (pixbuf);
    dst = gdk_pixbuf_get_pixels (opaque);
    src_stride = gdk_pixbuf_get_rowstride (pixbuf);
    dst_stride = gdk_pixbuf_get_rowstride (opaque);

    for (y = 0; y < height; y++) {
        const guchar *s = src + (gsize) y * src_stride;
        guchar *d = dst + (gsize) y * dst_stride;

        for (x = 0; x < width; x++) {
            d[x * 3]     = s[x * 4];
            d[x * 3 + 1] = s[x * 4 + 1];
            d[x * 3 + 2] = s[x * 4 + 2];
        }
    }

    return opaque;
}

/* The JPEG is baseline rather than progressive, which is what gdk-pixbuf
 * writes, and is also the faster of the two to decode. */
GBytes *
nemo_thumbnail_encode (GdkPixbuf *pixbuf, NemoThumbnailFormat *format)
{
    g_autoptr (GdkPixbuf) opaque = NULL;
    gchar *buffer = NULL;
    gsize len = 0;
    gboolean ok;

    if (has_transparency (pixbuf)) {
        *format = NEMO_THUMBNAIL_FORMAT_PNG;
        ok = gdk_pixbuf_save_to_buffer (pixbuf, &buffer, &len, "png", NULL, NULL);
    } else {
        if (gdk_pixbuf_get_has_alpha (pixbuf)) {
            opaque = drop_alpha (pixbuf);
            if (opaque == NULL)
                return NULL;
        }

        *format = NEMO_THUMBNAIL_FORMAT_JPEG;
        ok = gdk_pixbuf_save_to_buffer (opaque != NULL ? opaque : pixbuf,
                                        &buffer, &len, "jpeg", NULL,
                                        "quality", "90", NULL);
    }

    if (!ok || len == 0) {
        g_free (buffer);
        return NULL;
    }

    return g_bytes_new_take (buffer, len);
}

/* The size and time in `id` were read when the file was queued, or when the
 * view last looked. True if the file still has them. */
static gboolean
stat_unchanged (const char *uri, const NemoFileId *id)
{
    NemoFileId now = { 0 };
    time_t mtime;

    return get_file_mtime (uri, &mtime, &now) &&
           now.bytes == id->bytes && now.mtime == id->mtime;
}

/* Checksums the file when making the thumbnail reads all of it anyway, which
 * costs a second pass over bytes the page cache still holds. One worked out
 * on an earlier visit is read back instead: a folder queued ahead asks about
 * every file in it, and on a second visit most are stored already.
 *
 * A file edited since its size and time were read gets no checksum. Stored
 * with the old size, the new contents' checksum would claim a record no copy
 * of them could ever match. */
static void
learn_digest (NemoCacheDb *db, NemoThumbnailInfo *info)
{
    g_autoptr (GFile) file = NULL;

    if (!info->read_anyway || info->id.has_digest)
        return;

    if (nemo_cache_db_lookup_digest (db, info->image_uri, info->id.bytes, info->id.mtime, info->id.digest)) {
        info->id.has_digest = TRUE;
        return;
    }

    file = g_file_new_for_uri (info->image_uri);
    if (g_file_peek_path (file) == NULL)
        return;

    info->id.has_digest = nemo_file_digest_file (file, info->id.digest, cancellable, NULL) &&
                          stat_unchanged (info->image_uri, &info->id);
}

/* Last, once the store has it and the draw is on its way: an attribute write is
 * slow. A file already carrying this checksum is left alone, so a folder shown
 * again costs a read rather than a write. */
static void
save_digest_on_file (NemoThumbnailInfo *info)
{
    g_autoptr (GFile) file = NULL;
    guint8 there[NEMO_CACHE_DIGEST_LEN];

    if (!info->save_checksum || !info->id.has_digest || info->cancelled ||
        g_cancellable_is_cancelled (cancellable))
        return;

    file = g_file_new_for_uri (info->image_uri);
    if (g_file_peek_path (file) == NULL)
        return;

    if (nemo_file_digest_read_attr (file, info->id.bytes, info->id.mtime, there) &&
        memcmp (there, info->id.digest, sizeof (there)) == 0)
        return;

    nemo_file_digest_write_attr (file, info->id.bytes, info->id.mtime, info->id.digest);
}

/* A copy of this file may already have been drawn big enough under another
 * name. Only a checksum can say so for certain; without one it is the size
 * and time guess the store makes. */
static gboolean
already_stored (NemoCacheDb *db, NemoThumbnailInfo *info)
{
    NemoThumbnailRecord record;

    if (!nemo_cache_db_thumbnail_lookup (db, info->image_uri, &info->id, &record, NULL))
        return FALSE;

    if (record.width == 0)
        return !info->had_thumbnail;

    return record.size >= info->size || MAX (record.width, record.height) < record.size;
}

/* Always on thumbnail thread */
static void
remove_from_hash_table (NemoThumbnailInfo *info)
{
    NemoThumbnailInfo *follow;

    g_mutex_lock (&thumbnails_mutex);

    /* A newer ask for the same file may hold the entry by now. */
    if (g_hash_table_lookup (thumbnails_to_make_hash, info->image_uri) == info) {
        g_hash_table_remove (thumbnails_to_make_hash, info->image_uri);
    }

    follow = info->follow;
    if (follow != NULL && g_cancellable_is_cancelled (cancellable)) {
        /* Shutting down: the pool takes no more. */
        if (g_hash_table_lookup (thumbnails_to_make_hash, follow->image_uri) == follow) {
            g_hash_table_remove (thumbnails_to_make_hash, follow->image_uri);
        }
        free_thumbnail_info (follow);
    } else if (follow != NULL) {
        g_thread_pool_push ((GThreadPool *) tpool, follow, NULL);
    }

    g_mutex_unlock (&thumbnails_mutex);

    free_thumbnail_info (info);
}

/* Thumbnail thread. A picture already in the store, decoded here and handed
 * to the file as if it had just been made. */
static gboolean
hand_over_stored (NemoThumbnailInfo *info, gboolean near_view)
{
    RenderDone *done = g_new0 (RenderDone, 1);

    nemo_thumbnail_load (info->image_uri, &info->id, NULL, info->size, &done->loaded);

    if (done->loaded.pixbuf == NULL) {
        g_free (done);
        return FALSE;
    }

    done->uri = g_strdup (info->image_uri);
    done->mtime = info->original_file_mtime;
    done->ahead = TRUE;
    done->near_view = near_view;
    g_idle_add_full (G_PRIORITY_HIGH_IDLE, thumbnail_render_done, done, NULL);

    return TRUE;
}

/* Thumbnail thread */
static void
thumbnail_thread (gpointer data,
                  gpointer user_data)
{
    NemoThumbnailInfo *info = (NemoThumbnailInfo *) data;
    NemoCacheDb *db;
    RenderDone *done;
    GdkPixbuf *pixbuf;
    time_t current_time;
    gchar *image_uri = info->image_uri;
    gboolean free_uri = FALSE;

    g_mutex_lock (&thumbnails_mutex);
    info->running = TRUE;
    g_mutex_unlock (&thumbnails_mutex);

    if (g_cancellable_is_cancelled (cancellable) || info->cancelled) {
        DEBUG ("Skipping cancelled file: %s", info->image_uri);
        remove_from_hash_table (info);
        return;
    }

    /* Deferred from nemo_create_thumbnail, which cannot afford to block. */
    if (info->original_file_mtime == INVALID_MTIME) {
        get_file_mtime (info->image_uri, &info->original_file_mtime, &info->id);
    }

    if (info->load_only) {
        if (!hand_over_stored (info, TRUE)) {
            g_idle_add_full (G_PRIORITY_HIGH_IDLE, thumbnail_not_loaded,
                             g_strdup (info->image_uri), NULL);
        }
        remove_from_hash_table (info);
        return;
    }

    time (&current_time);

    /* Don't try to create a thumbnail if the file was modified recently.
       This prevents constant re-thumbnailing of changing files. */ 
    if (current_time < info->original_file_mtime + RECENT_MTIME_COOLDOWN) {
        DEBUG ("(Thumbnail Thread) Skipping for %d seconds: %s",
               RECENT_MTIME_COOLDOWN, info->image_uri);

        /* Reschedule thumbnailing via a change notification */
        g_timeout_add_seconds (RECENT_MTIME_COOLDOWN, thumbnail_thread_notify_file_changed,
                               g_strdup (info->image_uri));
        remove_from_hash_table (info);
        return;
    }

    db = nemo_cache_db_get ();

    learn_digest (db, info);

    if (db != NULL && already_stored (db, info)) {
        DEBUG ("(Thumbnail Thread) Already stored: %s", info->image_uri);

        /* Read back now, in its turn and on this thread, so it is drawn
           already when the view gets to it. */
        if (info->ahead && !nemo_thumbnail_memory_full () && hand_over_stored (info, FALSE)) {
            save_digest_on_file (info);
            remove_from_hash_table (info);
            return;
        }

        g_idle_add_full (G_PRIORITY_HIGH_IDLE,
                         info->ahead ? thumbnail_ahead_already_stored :
                                       thumbnail_thread_notify_file_changed,
                         g_strdup (info->image_uri), NULL);
        save_digest_on_file (info);
        remove_from_hash_table (info);
        return;
    }

    /* Create the thumbnail. */
    DEBUG ("(Thumbnail Thread) Creating thumbnail: %s at %d", info->image_uri, info->size);

    if (eel_uri_is_network (info->image_uri)) {
        GFile *file = g_file_new_for_uri (info->image_uri);
        GError *err = NULL;
        free_uri = TRUE;

        image_uri = g_filename_to_uri (g_file_peek_path (file), NULL, &err);

        if (err) {
            DEBUG ("(Thumbnail Thread) Failed to convert local_filepath to uri: %s", err->message);
            g_error_free (err);

            image_uri = g_strdup (info->image_uri); // revert back to our original image_uri
        }

        g_object_unref (file);
    }

    /**
     * the following function internally uses g_filename_to_uri whenever it finds a %i in the thumbnailer config file
     * because of that we have to convert our path from the network URI to a local file:// URI or else any
     * thumbnailers that use %i wont generate thumbnails correctly
     */
    pixbuf = nemo_desktop_thumbnail_factory_generate_thumbnail_at_size (get_thumbnail_factory (),
                                                                         image_uri,
                                                                         info->mime_type,
                                                                         info->size);
    if (free_uri) {
        g_free (image_uri);
    }

    /* Nothing goes into the shared freedesktop cache any more. It is read
       when the store has nothing, and never written. */
    done = g_new0 (RenderDone, 1);
    done->uri = g_strdup (info->image_uri);
    done->mtime = info->original_file_mtime;
    done->ahead = info->ahead;

    if (pixbuf) {
        NemoThumbnailRecord record = { 0 };
        g_autoptr (GBytes) image = NULL;
        int longest = MAX (gdk_pixbuf_get_width (pixbuf), gdk_pixbuf_get_height (pixbuf));

        record.size = info->size;
        record.width = gdk_pixbuf_get_width (pixbuf);
        record.height = gdk_pixbuf_get_height (pixbuf);

        image = nemo_thumbnail_encode (pixbuf, &record.format);
        done->loaded.from_store = image != NULL &&
                                  nemo_cache_db_thumbnail_store (db, info->image_uri, &info->id,
                                                                 &record, image);

        done->loaded.pixbuf = pixbuf;
        done->loaded.stored_size = info->size;
        done->loaded.stored_capped = longest >= info->size;
    } else {
        NemoThumbnailRecord record = { 0 };

        /* A bigger render that failed leaves the smaller one alone. */
        if (!info->had_thumbnail) {
            record.size = info->size;
            nemo_cache_db_thumbnail_store (db, info->image_uri, &info->id, &record, NULL);
            done->loaded.failed = TRUE;
        }
    }

    /* We need to call nemo_file_changed(), but I don't think that is
       thread safe. So add an idle handler and do it from the main loop. */
    if (done->loaded.pixbuf != NULL || done->loaded.failed) {
        g_idle_add_full (G_PRIORITY_HIGH_IDLE, thumbnail_render_done, done, NULL);
    } else {
        g_free (done->uri);
        g_free (done);
        g_idle_add_full (G_PRIORITY_HIGH_IDLE,
                         thumbnail_thread_notify_file_changed,
                         g_strdup (info->image_uri), NULL);
    }

    save_digest_on_file (info);

#if DEBUG_THREADS
    g_message ("%u unprocessed (Done) (%u threads free)",
               g_thread_pool_unprocessed ((GThreadPool *) tpool),
               g_thread_pool_get_num_unused_threads ());
#endif
    remove_from_hash_table (info);
}

/* Mainloop */
static  void
feeder_task_complete (GObject      *source,
                       GAsyncResult *res,
                       gpointer      user_data)
{
    g_task_propagate_boolean (G_TASK (res), NULL);
    DEBUG ("(Finalize) Feeder task done");
}

/* Feeder thread */
static void
feeder_thread (GTask        *task,
                gpointer      source,
                gpointer      task_data,
                GCancellable *cancellable)
{
    gpointer data;
    gboolean resort = FALSE;

    while (!g_cancellable_is_cancelled (cancellable) && (data = g_async_queue_pop (feeder_queue))) {

#if DEBUG_THREADS
    g_message ("Pop from feeder (Add) %i items in feeder", g_async_queue_length (feeder_queue));
#endif
        NemoThumbnailInfo *feeder_info = (NemoThumbnailInfo *) data;
        NemoThumbnailInfo *existing_info = NULL;

        switch (feeder_info->cmd_type) {
            case THUMBNAIL_ADD:
                DEBUG ("(Add thumbnail) Locking mutex");
                g_mutex_lock (&thumbnails_mutex);
                existing_info = g_hash_table_lookup (thumbnails_to_make_hash, feeder_info->image_uri);

                if (existing_info == NULL) {
                    DEBUG ("(Main Thread) Adding new file to thumbnail: %s", feeder_info->image_uri);
#if DEBUG_THREADS
                    g_message ("%u unprocessed (Add)", g_thread_pool_unprocessed ((GThreadPool *) tpool));
#endif
                    g_hash_table_insert (thumbnails_to_make_hash, feeder_info->image_uri, feeder_info);
                    g_thread_pool_push ((GThreadPool *) tpool, feeder_info, NULL);

                    // Don't free this later.
                    feeder_info = NULL;
                } else if (existing_info->running) {
                    if (!feeder_info->load_only && feeder_info->size > existing_info->size) {
                        DEBUG ("(Main Thread) Bigger than the one being made, following it: %s", feeder_info->image_uri);

                        /* Replace, not insert: the old key is freed with the running job. */
                        existing_info->follow = feeder_info;
                        g_hash_table_replace (thumbnails_to_make_hash, feeder_info->image_uri, feeder_info);
                        feeder_info = NULL;
                    }
                } else {
                    DEBUG ("(Main Thread) Updating existing file mtime and prioritizing: %s", feeder_info->image_uri);

                    /* The file in the queue might need a new original mtime */
                    existing_info->original_file_mtime = feeder_info->original_file_mtime;
                    existing_info->id = feeder_info->id;
                    existing_info->size = MAX (existing_info->size, feeder_info->size);
                    if (feeder_info->load_only) {
                        /* Already on its way, made or read. Where it is
                           stays as it is. */
                    } else if (feeder_info->ahead) {
                        existing_info->load_only = FALSE;
                        /* Queued again in view order: takes the new place,
                           which only counts once the queue is sorted. */
                        existing_info->ahead = TRUE;
                        existing_info->order = feeder_info->order;
                        resort = TRUE;
                    } else {
                        /* Scrolled into view while it waits. It keeps its
                           place, or the last file in a folder could be made
                           first just by looking at it. */
                        existing_info->ahead = FALSE;
                        existing_info->load_only = FALSE;
                    }
                }
                DEBUG ("(Add thumbnail) Unlocking mutex");
                g_mutex_unlock (&thumbnails_mutex);
                break;
            case THUMBNAIL_REMOVE:
                if (!thumbnails_to_make_hash)
                    break;

                DEBUG ("(Remove from queue) Locking mutex");
                g_mutex_lock (&thumbnails_mutex);
                existing_info = g_hash_table_lookup (thumbnails_to_make_hash, feeder_info->image_uri);

                if (existing_info) {
                    DEBUG ("(Remove from queue) Removing %s", feeder_info->image_uri);
                    g_hash_table_remove (thumbnails_to_make_hash, feeder_info->image_uri);
                    existing_info->cancelled = TRUE;
                }
                DEBUG ("(Remove from queue) Unlocking mutex");
                g_mutex_unlock (&thumbnails_mutex);
                break;
            case THUMBNAIL_THREAD_EXIT:
                DEBUG ("(Finalize) Received THUMBNAIL_THREAD_EXIT, cancelling");
                g_cancellable_cancel (cancellable);
                break;
        }

        g_clear_pointer (&feeder_info, free_thumbnail_info);

        /* Setting the sort function again is what sorts the tasks already
           waiting. Once a batch is in rather than per file, since a folder
           queues thousands at a time. */
        if (resort && g_async_queue_length (feeder_queue) <= 0) {
            g_mutex_lock (&thumbnails_mutex);
            g_thread_pool_set_sort_function ((GThreadPool *) tpool, (GCompareDataFunc) order_sorter, NULL);
            g_mutex_unlock (&thumbnails_mutex);
            resort = FALSE;
        }
    }

    g_task_return_boolean (task, TRUE);
}

static void
finalize_thumbnailer (void)
{
    NemoThumbnailInfo *info;
    gpointer data;

    if (feeder_queue == NULL)
        return;

    DEBUG ("(Finalize) Shutdown thumbnailer.");

    info = g_new0 (NemoThumbnailInfo, 1);
    info->cmd_type = THUMBNAIL_THREAD_EXIT;

    g_async_queue_push (feeder_queue, info);

    while (!g_task_get_completed (feeder_task)) {
        gtk_main_iteration ();
    }

    while ((data = g_async_queue_try_pop (feeder_queue))) {
        if (!data) {
            break;
        }
        NemoThumbnailInfo *existing_info = (NemoThumbnailInfo *) data;
        free_thumbnail_info (existing_info);
    }

    g_object_unref (feeder_task);
    g_async_queue_unref (feeder_queue);

    /* A job ending past this point sees the cancel and drops its follow,
       rather than pushing it into a pool being freed. */
    g_mutex_lock (&thumbnails_mutex);
    g_mutex_unlock (&thumbnails_mutex);

    // This will drain and free any remaining infos.
    g_thread_pool_free ((GThreadPool *) tpool, FALSE, TRUE);

    g_hash_table_destroy (thumbnails_to_make_hash);
}

/* Mainloop */
static void
queue_render (NemoFile *file, int size, gboolean ahead, gboolean load_only, gint64 order)
{
    time_t file_mtime = 0;
    static gsize once_init = 0;
    if (g_once_init_enter (&once_init)) {
        thumbnails_to_make_hash = g_hash_table_new (g_str_hash, g_str_equal);
        DEBUG ("Initialize thread pool");

        tpool = g_thread_pool_new ((GFunc) thumbnail_thread, NULL,
                                   get_max_threads (),
                                   FALSE, NULL);
        g_thread_pool_set_sort_function ((GThreadPool *) tpool, (GCompareDataFunc) order_sorter, NULL);

        feeder_queue = g_async_queue_new ();
        cancellable = g_cancellable_new ();
        feeder_task = g_task_new (NULL, cancellable, feeder_task_complete, NULL);
        g_task_run_in_thread (feeder_task, feeder_thread);

        eel_debug_call_at_shutdown ((EelFunction) finalize_thumbnailer);

        g_once_init_leave (&once_init, 1);
    }

    /* The gdk-pixbuf-thumbnailer tool has special hardcoded handling for recent: and trash: uris.
     * we need to find the activation uri here instead */
    if (nemo_file_is_in_favorites (file)) {
        NemoFile *real_file;
        gchar *uri;

        uri = nemo_file_get_symbolic_link_target_uri (file);

        real_file = nemo_file_get_by_uri (uri);
        if (real_file != NULL) {
            queue_render (real_file, size, ahead, load_only, order);
            nemo_file_unref (real_file);
        }

        g_free (uri);
        return;
    }

    gchar *file_uri = nemo_file_get_uri (file);

    /* Hopefully the NemoFile will already have the image file mtime,
       so we can just use that. Otherwise we have to get it ourselves. */
    if (file->details->got_file_info &&
        file->details->file_info_is_up_to_date &&
        file->details->mtime != 0) {
        file_mtime = file->details->mtime;
    } else {
        /* Leave it for the thumbnail thread to stat - this is the main loop, and
           a spun-down drive or dead mapping would hold the UI for the timeout. */
        file_mtime = INVALID_MTIME;
    }

    NemoThumbnailInfo *info;
    info = g_new0 (NemoThumbnailInfo, 1);
    info->image_uri = file_uri;
    info->mime_type = nemo_file_get_mime_type (file);
    info->original_file_mtime = file_mtime;
    if (file_mtime != INVALID_MTIME) {
        nemo_thumbnail_file_id (file, &info->id);
    }
    info->size = nemo_thumbnail_size_step (size);
    info->read_anyway = nemo_can_thumbnail_internally (file);
    info->had_thumbnail = file->details->thumbnail_stored_size > 0;
    info->save_checksum = nemo_config_get_boolean (nemo_config_get_group (NEMO_FILE_CACHE_GROUP),
                                                   NEMO_FILE_CACHE_SAVE_CHECKSUM);
    info->order = order;
    info->ahead = ahead;
    info->load_only = load_only;
    info->cmd_type = THUMBNAIL_ADD;

    nemo_file_set_is_thumbnailing (file, TRUE);

#if DEBUG_THREADS
    g_message ("Push to feeder (Add) %i items in feeder", g_async_queue_length (feeder_queue));
#endif

    g_async_queue_push (feeder_queue, info);
}

/* A file that a folder queued ahead has placed keeps that place, so asking
 * again because it scrolled into view, grew or changed cannot move it up.
 * Anything else is first come first served, ahead of every folder. */
static gint64 asked = 0;

void
nemo_create_thumbnail (NemoFile *file, int size)
{
    gint64 order = file->details->thumbnail_order;

    if (order == 0) {
        order = G_MININT64 + ++asked;
    }

    queue_render (file, size, FALSE, FALSE, order);
}

/* Only reads back what is made already, so nothing is made out of turn, and
 * that is cheap enough to go ahead of every folder still being made. */
void
nemo_thumbnail_load_near_view (GList *files, int size)
{
    GList *l;

    for (l = files; l != NULL; l = l->next) {
        queue_render (l->data, size, TRUE, TRUE, G_MININT64 + ++asked);
    }
}

/* Every file gets its place, in the order given, whether it needs making now
 * or not: one already drawn may be asked for again at a bigger size. Each call
 * gets a band before the last one's, so a folder queued again, or the next
 * folder opened, goes ahead of what is left of the one before rather than
 * taking turns with it. */
void
nemo_thumbnail_render_ahead (GList *files, int size)
{
    static gint64 band = 0;
    gint64 place = 0;
    GList *l;

    band++;

    for (l = files; l != NULL; l = l->next) {
        NemoFile *file = l->data;

        file->details->thumbnail_order = -(band << 32) + place++;

        if (nemo_file_is_thumbnailing (file) || nemo_file_wants_thumbnail_ahead (file)) {
            queue_render (file, size, TRUE, FALSE, file->details->thumbnail_order);
        }
    }
}

/* Counted on the main loop, from the file's thumbnailing flag, so a job
 * folded into one already queued is not counted twice. A batch runs from the
 * queue leaving empty to its going empty again. */
static guint jobs_waiting = 0;
static guint jobs_done = 0;
static GList *job_watchers = NULL;

typedef struct {
    NemoThumbnailJobsFunc func;
    gpointer    data;
} JobWatcher;

void
nemo_thumbnail_note_job (gboolean started)
{
    GList *l;

    if (!started) {
        if (jobs_waiting > 0) {
            jobs_waiting--;
            jobs_done++;
        }
        return;
    }

    if (jobs_waiting++ > 0) {
        return;
    }

    jobs_done = 0;

    for (l = job_watchers; l != NULL; l = l->next) {
        JobWatcher *watcher = l->data;

        watcher->func (watcher->data);
    }
}

void
nemo_thumbnail_jobs (guint *done, guint *waiting)
{
    *done = jobs_done;
    *waiting = jobs_waiting;
}

/* A picture read back from the store is ready before the batch reaches it,
 * but a bar that runs ahead of the one building them reads wrong. With
 * nothing to count, the list view say, it follows that one. */
void
nemo_thumbnail_rendered_for_bar (guint done, guint waiting, guint *shown, guint *wanted)
{
    if (*wanted == 0) {
        *shown = done;
        *wanted = done + waiting;
    } else if (done + waiting > 0) {
        *shown = MIN (*shown, (guint) ((guint64) *wanted * done / (done + waiting)));
    }
}

void
nemo_thumbnail_watch_jobs (NemoThumbnailJobsFunc func, gpointer data)
{
    JobWatcher *watcher = g_new0 (JobWatcher, 1);

    watcher->func = func;
    watcher->data = data;
    job_watchers = g_list_prepend (job_watchers, watcher);
}

void
nemo_thumbnail_unwatch_jobs (NemoThumbnailJobsFunc func, gpointer data)
{
    GList *l;

    for (l = job_watchers; l != NULL; l = l->next) {
        JobWatcher *watcher = l->data;

        if (watcher->func == func && watcher->data == data) {
            job_watchers = g_list_delete_link (job_watchers, l);
            g_free (watcher);
            return;
        }
    }
}

/* Mainloop */
void
nemo_thumbnail_remove_from_queue (const char *file_uri)
{
    if (feeder_queue == NULL)
        return;

    NemoThumbnailInfo *info;

    info = g_new0 (NemoThumbnailInfo, 1);
    info->image_uri = g_strdup (file_uri);
    info->cmd_type = THUMBNAIL_REMOVE;

#if DEBUG_THREADS
    g_message ("Push to feeder (Remove) %i items in feeder", g_async_queue_length (feeder_queue));
#endif

    g_async_queue_push (feeder_queue, info);
}

gboolean
nemo_can_thumbnail_internally (NemoFile *file)
{
    g_autofree gchar *mime_type = NULL;
#ifdef G_OS_WIN32
    g_autofree gchar *real_mime = NULL;
#endif

    mime_type = nemo_file_get_mime_type (file);

#ifdef G_OS_WIN32
    /* On win32 the "mime type" is the extension (".png"); everything else that
     * looks at it converts first. Without that this never matched, so the
     * full-resolution path never ran and large zoom levels stayed blurry. */
    real_mime = mime_type != NULL ? g_content_type_get_mime_type (mime_type) : NULL;
    if (real_mime != NULL) {
        return pixbuf_can_load_type (real_mime);
    }
#endif

    return pixbuf_can_load_type (mime_type);
}

gboolean
nemo_can_thumbnail (NemoFile *file)
{
    g_autofree gchar *mime_type = NULL;
    g_autofree gchar *uri = NULL;

    uri = nemo_file_get_uri (file);
    mime_type = nemo_file_get_mime_type (file);

    return nemo_desktop_thumbnail_factory_can_make (get_thumbnail_factory (), uri, mime_type);
}

int
nemo_thumbnail_size_step (int size)
{
    int step = NEMO_THUMBNAIL_SIZE_STEP;

    return MAX (step, (size + step - 1) / step * step);
}

void
nemo_thumbnail_file_id (NemoFile *file, NemoFileId *id)
{
    memset (id, 0, sizeof (*id));
    id->bytes = MAX (file->details->size, 0);
    id->mtime = (gint64) file->details->mtime * G_USEC_PER_SEC + file->details->mtime_usec;
}

typedef struct {
    int max_size;
    gboolean capped;
} DecodeSize;

/* Decodes no bigger than `max_size` on the longer side. JPEG in particular
 * decodes much faster straight to a smaller size than at full size and then
 * scaled. */
static void
decode_size_prepared (GdkPixbufLoader *loader, int width, int height, gpointer data)
{
    DecodeSize *want = data;
    int max_size = want->max_size;

    if (max_size <= 0 || MAX (width, height) <= max_size)
        return;

    want->capped = TRUE;

    if (width >= height) {
        gdk_pixbuf_loader_set_size (loader, max_size, MAX (1, (int) ((gint64) height * max_size / width)));
    } else {
        gdk_pixbuf_loader_set_size (loader, MAX (1, (int) ((gint64) width * max_size / height)), max_size);
    }
}

static GdkPixbuf *
decode_at_most (const guint8 *data, gsize len, int max_size, gboolean *capped)
{
    g_autoptr (GdkPixbufLoader) loader = gdk_pixbuf_loader_new ();
    DecodeSize want = { max_size, FALSE };
    GdkPixbuf *pixbuf;

    *capped = FALSE;

    g_signal_connect (loader, "size-prepared", G_CALLBACK (decode_size_prepared), &want);

    if (!gdk_pixbuf_loader_write (loader, data, len, NULL) ||
        !gdk_pixbuf_loader_close (loader, NULL)) {
        return NULL;
    }

    pixbuf = gdk_pixbuf_loader_get_pixbuf (loader);
    if (pixbuf == NULL)
        return NULL;

    *capped = want.capped;

    return gdk_pixbuf_apply_embedded_orientation (pixbuf);
}

/* A thumbnail someone else made. The shared cache stamps the time of the file
 * it was made from, and one that does not match is out of date. The option
 * does not survive a scaled decode, so it is read at full size, which is at
 * most 1024 there. */
static GdkPixbuf *
load_shared (const char *path, int max_size, gboolean *capped, time_t *mtime)
{
    g_autofree gchar *contents = NULL;
    g_autoptr (GdkPixbuf) full = NULL;
    const char *stamp;
    gsize len = 0;
    int width, height;

    *capped = FALSE;

    if (!g_file_get_contents (path, &contents, &len, NULL))
        return NULL;

    full = decode_at_most ((const guint8 *) contents, len, 0, capped);
    if (full == NULL)
        return NULL;

    stamp = gdk_pixbuf_get_option (full, "tEXt::Thumb::MTime");
    *mtime = stamp != NULL ? (time_t) g_ascii_strtoll (stamp, NULL, 10) : 0;

    width = gdk_pixbuf_get_width (full);
    height = gdk_pixbuf_get_height (full);

    if (max_size <= 0 || MAX (width, height) <= max_size)
        return g_steal_pointer (&full);

    *capped = TRUE;

    if (width >= height) {
        return gdk_pixbuf_scale_simple (full, max_size, MAX (1, (int) ((gint64) height * max_size / width)),
                                        GDK_INTERP_BILINEAR);
    }

    return gdk_pixbuf_scale_simple (full, MAX (1, (int) ((gint64) width * max_size / height)), max_size,
                                    GDK_INTERP_BILINEAR);
}

void
nemo_thumbnail_load (const char          *uri,
                     const NemoFileId    *id,
                     const char          *shared_path,
                     int                  max_size,
                     NemoThumbnailLoaded *out)
{
    NemoCacheDb *db = nemo_cache_db_get ();
    NemoThumbnailRecord record;
    g_autoptr (GBytes) image = NULL;

    memset (out, 0, sizeof (*out));

    if (db != NULL && nemo_cache_db_thumbnail_lookup (db, uri, id, &record, &image)) {
        out->from_store = TRUE;

        if (record.width == 0 || image == NULL) {
            out->failed = TRUE;
            return;
        }

        out->stored_size = record.size;
        out->stored_capped = MAX (record.width, record.height) >= record.size;
        out->pixbuf = decode_at_most (g_bytes_get_data (image, NULL), g_bytes_get_size (image),
                                      max_size, &out->capped);
        if (out->pixbuf != NULL)
            return;

        /* Unreadable, so as good as not there. */
        out->from_store = FALSE;
    }

    /* How big the shared copy was made is not known, so a bigger draw
       always asks for one of our own. */
    if (shared_path != NULL) {
        out->pixbuf = load_shared (shared_path, max_size, &out->capped, &out->shared_mtime);
        out->stored_capped = TRUE;
    }
}


void
nemo_thumbnail_frame_image (GdkPixbuf **pixbuf)
{
    GdkPixbuf *pixbuf_with_frame, *frame;

    /* The pixbuf isn't already framed (i.e., it was not made by
     * an old Nemo), so we must embed it in a frame.
     */

    frame = nemo_get_thumbnail_frame ();
    if (frame == NULL) {
        return;
    }

    pixbuf_with_frame = eel_embed_image_in_frame (*pixbuf, frame,
                                                  NEMO_THUMBNAIL_FRAME_LEFT,
                                                  NEMO_THUMBNAIL_FRAME_TOP,
                                                  NEMO_THUMBNAIL_FRAME_RIGHT,
                                                  NEMO_THUMBNAIL_FRAME_BOTTOM);
    g_object_unref (*pixbuf);
    *pixbuf = pixbuf_with_frame;
}

void
nemo_thumbnail_pad_top_and_bottom (GdkPixbuf **pixbuf,
                                   gint        extra_height)
{
    GdkPixbuf *pixbuf_with_padding;
    GdkRectangle rect;
    GdkRGBA transparent = { 0, 0, 0, 0.0 };
    cairo_surface_t *surface;
    cairo_t *cr;
    gint width, height;

    width = gdk_pixbuf_get_width (*pixbuf);
    height = gdk_pixbuf_get_height (*pixbuf);

    surface = gdk_window_create_similar_image_surface (NULL,
                                                       CAIRO_FORMAT_ARGB32,
                                                       width,
                                                       height + extra_height,
                                                       0);

    cr = cairo_create (surface);

    rect.x = 0;
    rect.y = 0;
    rect.width = width;
    rect.height = height + extra_height;

    gdk_cairo_rectangle (cr, &rect);
    gdk_cairo_set_source_rgba (cr, &transparent);
    cairo_fill (cr);

    gdk_cairo_set_source_pixbuf (cr,
                                 *pixbuf,
                                 0,
                                 extra_height / 2);
    cairo_paint (cr);

    pixbuf_with_padding = gdk_pixbuf_get_from_surface (surface,
                                                       0,
                                                       0,
                                                       width,
                                                       height + extra_height);

    g_object_unref (*pixbuf);
    cairo_surface_destroy (surface);
    cairo_destroy (cr);

    *pixbuf = pixbuf_with_padding;
}

gboolean
nemo_thumbnail_factory_check_status (void)
{
    return nemo_desktop_thumbnail_cache_check_permissions (get_thumbnail_factory (), TRUE);
}
