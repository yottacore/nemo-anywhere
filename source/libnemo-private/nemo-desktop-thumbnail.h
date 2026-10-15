/*
 * gnome-thumbnail.h: Utilities for handling thumbnails
 * Adapted for nemo-anywhere from cinnamon-desktop 6.4.1 (libcinnamon-desktop).
 *
 * Copyright (C) 2002 Red Hat, Inc.
 *
 * This file is part of the Gnome Library.
 *
 * The Gnome Library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 *
 * The Gnome Library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public
 * License along with the Gnome Library; see the file COPYING.LIB.  If not,
 * write to the Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 *
 * Author: Alexander Larsson <alexl@redhat.com>
 */

#ifndef NEMO_DESKTOP_THUMBNAIL_H
#define NEMO_DESKTOP_THUMBNAIL_H


#include <glib.h>
#include <glib-object.h>
#include <gio/gio.h>
#include <time.h>
#include <gdk-pixbuf/gdk-pixbuf.h>

G_BEGIN_DECLS

typedef enum {
  NEMO_DESKTOP_THUMBNAIL_SIZE_NORMAL,
  NEMO_DESKTOP_THUMBNAIL_SIZE_LARGE
} NemoDesktopThumbnailSize;

/* The folder our own failure records go in, under the cache's fail dir. */
#define NEMO_DESKTOP_THUMBNAIL_FAIL_APPNAME "nemo-anywhere-thumbnail-factory"

#define NEMO_DESKTOP_TYPE_THUMBNAIL_FACTORY		(nemo_desktop_thumbnail_factory_get_type ())
#define NEMO_DESKTOP_THUMBNAIL_FACTORY(obj)	(G_TYPE_CHECK_INSTANCE_CAST ((obj), NEMO_DESKTOP_TYPE_THUMBNAIL_FACTORY, NemoDesktopThumbnailFactory))
#define NEMO_DESKTOP_THUMBNAIL_FACTORY_CLASS(klass)	(G_TYPE_CHECK_CLASS_CAST ((klass), NEMO_DESKTOP_TYPE_THUMBNAIL_FACTORY, NemoDesktopThumbnailFactoryClass))
#define NEMO_DESKTOP_IS_THUMBNAIL_FACTORY(obj)		(G_TYPE_INSTANCE_CHECK_TYPE ((obj), NEMO_DESKTOP_TYPE_THUMBNAIL_FACTORY))
#define NEMO_DESKTOP_IS_THUMBNAIL_FACTORY_CLASS(klass)	(G_TYPE_CLASS_CHECK_CLASS_TYPE ((klass), NEMO_DESKTOP_TYPE_THUMBNAIL_FACTORY))

typedef struct _NemoDesktopThumbnailFactory        NemoDesktopThumbnailFactory;
typedef struct _NemoDesktopThumbnailFactoryClass   NemoDesktopThumbnailFactoryClass;
typedef struct _NemoDesktopThumbnailFactoryPrivate NemoDesktopThumbnailFactoryPrivate;

struct _NemoDesktopThumbnailFactory {
	GObject parent;
	
	NemoDesktopThumbnailFactoryPrivate *priv;
};

struct _NemoDesktopThumbnailFactoryClass {
	GObjectClass parent;
};

GType                  nemo_desktop_thumbnail_factory_get_type (void);
NemoDesktopThumbnailFactory *nemo_desktop_thumbnail_factory_new      (NemoDesktopThumbnailSize     size);

char *                 nemo_desktop_thumbnail_factory_lookup   (NemoDesktopThumbnailFactory *factory,
								 const char            *uri,
								 time_t                 mtime);

gboolean               nemo_desktop_thumbnail_factory_has_valid_failed_thumbnail (NemoDesktopThumbnailFactory *factory,
										   const char            *uri,
										   time_t                 mtime);
gboolean               nemo_desktop_thumbnail_factory_can_thumbnail (NemoDesktopThumbnailFactory *factory,
								      const char            *uri,
								      const char            *mime_type,
								      time_t                 mtime);
GdkPixbuf *            nemo_desktop_thumbnail_factory_generate_thumbnail (NemoDesktopThumbnailFactory *factory,
									   const char            *uri,
									   const char            *mime_type);
GdkPixbuf *            nemo_desktop_thumbnail_factory_generate_thumbnail_at_size (NemoDesktopThumbnailFactory *factory,
										   const char            *uri,
										   const char            *mime_type,
										   int                    size,
										   GCancellable          *cancellable);
gboolean               nemo_desktop_thumbnail_factory_can_make (NemoDesktopThumbnailFactory *factory,
								 const char            *uri,
								 const char            *mime_type);
void                   nemo_desktop_thumbnail_factory_save_thumbnail (NemoDesktopThumbnailFactory *factory,
								       GdkPixbuf             *thumbnail,
								       const char            *uri,
								       time_t                 original_mtime);
void                   nemo_desktop_thumbnail_factory_create_failed_thumbnail (NemoDesktopThumbnailFactory *factory,
										const char            *uri,
										time_t                 mtime);


/* Thumbnailing utils: */
gboolean   nemo_desktop_thumbnail_has_uri           (GdkPixbuf          *pixbuf,
						      const char         *uri);
gboolean   nemo_desktop_thumbnail_is_valid          (GdkPixbuf          *pixbuf,
						      const char         *uri,
						      time_t              mtime);
char *     nemo_desktop_thumbnail_md5               (const char         *uri);
char *     nemo_desktop_thumbnail_path_for_uri      (const char         *uri,
						      NemoDesktopThumbnailSize  size);


/* Pixbuf utils */

GdkPixbuf *nemo_desktop_thumbnail_scale_down_pixbuf (GdkPixbuf          *pixbuf,
						      int                 dest_width,
						      int                 dest_height);

/* Thumbnail folder checking and fixing utils */
void       nemo_desktop_thumbnail_cache_fix_permissions (void);
gboolean   nemo_desktop_thumbnail_cache_check_permissions (NemoDesktopThumbnailFactory *factory, gboolean quick);


G_END_DECLS

#endif /* NEMO_DESKTOP_THUMBNAIL_H */
