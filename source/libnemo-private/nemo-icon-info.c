/* -*- Mode: C; indent-tabs-mode: f; c-basic-offset: 4; tab-width: 4 -*- */
/* nemo-icon-info.c
 * Copyright (C) 2007  Red Hat, Inc.,  Alexander Larsson <alexl@redhat.com>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 51 Franklin Street - Suite 500,
 * Boston, MA 02110-1335, USA.
 */

#include <config.h>
#include <string.h>
#include "nemo-icon-info.h"
#include "nemo-icon-names.h"
#include "nemo-default-file-icon.h"
#include "nemo-ui-scale.h"
#include <gtk/gtk.h>
#include <gio/gio.h>

static void schedule_reap_cache (void);

static void
pixbuf_toggle_notify (gpointer      info,
              GObject      *object,
              gboolean      is_last_ref)
{
    NemoIconInfo  *icon = info;

    if (is_last_ref) {
        icon->sole_owner = TRUE;
        g_object_remove_toggle_ref (object,
                        pixbuf_toggle_notify,
                        info);
        icon->last_use_time = g_get_monotonic_time ();
        schedule_reap_cache ();
    }
}

static void
nemo_icon_info_free (NemoIconInfo *icon)
{
    g_return_if_fail (icon != NULL);

    if (!icon->sole_owner && icon->pixbuf) {
        g_object_remove_toggle_ref (G_OBJECT (icon->pixbuf),
                                    pixbuf_toggle_notify,
                                    icon);
    }

    if (icon->pixbuf) {
        g_object_unref (icon->pixbuf);
    }

    g_free (icon->icon_name);
    g_free (icon);
}

/* Returns: (transfer full): @icon with one more ref; drop it with nemo_icon_info_unref */
NemoIconInfo *
nemo_icon_info_ref (NemoIconInfo *icon)
{
    g_return_val_if_fail (icon != NULL, NULL);

    icon->ref_count++;

    return icon;
}

void
nemo_icon_info_unref (NemoIconInfo *icon)
{
    g_return_if_fail (icon != NULL);
    g_return_if_fail (icon->ref_count > 0);

    icon->ref_count--;

    if (icon->ref_count == 0) {
        nemo_icon_info_free (icon);
    }
}

void
nemo_icon_info_clear (NemoIconInfo **info)
{
    gpointer _info;

    _info = *info;

    if (_info) {
        *info = NULL;
        nemo_icon_info_unref (_info);
    }
}

static NemoIconInfo *
nemo_icon_info_create (void)
{
    NemoIconInfo *icon;

    icon = g_new0 (NemoIconInfo, 1);

	icon->last_use_time = g_get_monotonic_time ();
	icon->sole_owner = TRUE;
    icon->ref_count = 1;
    
    return icon;
}

gboolean
nemo_icon_info_is_fallback (NemoIconInfo  *icon)
{
  return icon->pixbuf == NULL;
}

/* Returns: (transfer full): unref with nemo_icon_info_unref */
NemoIconInfo *
nemo_icon_info_new_for_pixbuf (GdkPixbuf *pixbuf,
                                gint      scale)
{
	NemoIconInfo *icon;

	icon = nemo_icon_info_create ();

	if (pixbuf) {
		icon->pixbuf = g_object_ref (pixbuf);
	}

    icon->orig_scale = scale;

	return icon;
}

static NemoIconInfo *
nemo_icon_info_new_for_icon_info (GtkIconInfo *icon_info,
                                  gint         scale)
{
	NemoIconInfo *icon;
	const char *filename;
	char *basename, *p;

	icon = nemo_icon_info_create ();
	icon->orig_scale = scale;

	/* A theme with neither the icon nor text-x-generic. The default file
	 * picture is drawn instead. */
	if (icon_info == NULL) {
		return icon;
	}

	icon->pixbuf = gtk_icon_info_load_icon (icon_info, NULL);

	filename = gtk_icon_info_get_filename (icon_info);
	if (filename != NULL) {
		basename = g_path_get_basename (filename);
		p = strrchr (basename, '.');
		if (p) {
			*p = 0;
		}
		icon->icon_name = basename;
	}

	return icon;
}


typedef struct  {
	GIcon *icon;
	int size;
} IconKey;

static GHashTable *loadable_icon_cache = NULL;
static GHashTable *themed_icon_cache = NULL;
static guint reap_cache_timeout = 0;

#define MICROSEC_PER_SEC ((guint64)1000000L)

static guint time_now;

static gboolean
reap_old_icon (G_GNUC_UNUSED gpointer  key,
	       gpointer  value,
	       gpointer  user_info)
{
	NemoIconInfo *icon = value;
	gboolean *reapable_icons_left = user_info;

	if (icon->sole_owner) {
		if (time_now - icon->last_use_time > (gint64)(30 * MICROSEC_PER_SEC)) {
			/* This went unused 30 secs ago. reap */
			return TRUE;
		} else {
			/* We can reap this soon */
			*reapable_icons_left = TRUE;
		}
	}

	return FALSE;
}

static gboolean
reap_cache (G_GNUC_UNUSED gpointer data)
{
	gboolean reapable_icons_left;

	reapable_icons_left = TRUE;

	time_now = g_get_monotonic_time ();

	if (loadable_icon_cache) {
		g_hash_table_foreach_remove (loadable_icon_cache,
					     reap_old_icon,
					     &reapable_icons_left);
	}

	if (themed_icon_cache) {
		g_hash_table_foreach_remove (themed_icon_cache,
					     reap_old_icon,
					     &reapable_icons_left);
	}

	if (reapable_icons_left) {
		return TRUE;
	} else {
		reap_cache_timeout = 0;
		return FALSE;
	}
}

static void
schedule_reap_cache (void)
{
	if (reap_cache_timeout == 0) {
		reap_cache_timeout = g_timeout_add_seconds_full (0, 5,
								 reap_cache,
								 NULL, NULL);
	}
}

void
nemo_icon_info_clear_caches (void)
{
	if (loadable_icon_cache) {
		g_hash_table_remove_all (loadable_icon_cache);
	}

	if (themed_icon_cache) {
		g_hash_table_remove_all (themed_icon_cache);
	}
}

static guint
icon_key_hash (IconKey *key)
{
	return g_icon_hash (key->icon) ^ key->size;
}

static gboolean
icon_key_equal (const IconKey *a,
                const IconKey *b)
{
	return a->size == b->size &&
		g_icon_equal (a->icon, b->icon);
}

static IconKey *
icon_key_new (GIcon *icon, int size)
{
	IconKey *key;

	key = g_new0 (IconKey, 1);
	key->icon = g_object_ref (icon);
	key->size = size;

	return key;
}

static void
icon_key_free (IconKey *key)
{
	g_object_unref (key->icon);
	g_free (key);
}

/* Returns: (transfer full): unref with nemo_icon_info_unref */
NemoIconInfo *
nemo_icon_info_lookup (GIcon *icon,
               int size,
               int scale)
{
    GtkIconTheme *icon_theme;
    GtkIconInfo *gtkicon_info;

    NemoIconInfo *icon_info;

    icon_theme = gtk_icon_theme_get_default ();

    if (G_IS_LOADABLE_ICON (icon)) {
        GdkPixbuf *pixbuf;

        IconKey lookup_key;
        IconKey *key;
        GInputStream *stream;

        if (loadable_icon_cache == NULL) {
            loadable_icon_cache = g_hash_table_new_full ((GHashFunc) icon_key_hash,
                                                         (GEqualFunc) icon_key_equal,
                                                         (GDestroyNotify) icon_key_free,
                                                         (GDestroyNotify) nemo_icon_info_free);
        }

        lookup_key.icon = icon;
        lookup_key.size = size;

        icon_info = g_hash_table_lookup (loadable_icon_cache, &lookup_key);
        if (icon_info) {
            return nemo_icon_info_ref (icon_info);
        }

        pixbuf = NULL;
        stream = g_loadable_icon_load (G_LOADABLE_ICON (icon),
                           size * scale,
                           NULL, NULL, NULL);

        if (stream) {
            pixbuf = gdk_pixbuf_new_from_stream_at_scale (stream,
                                      size * scale, size * scale,
                                      TRUE,
                                      NULL, NULL);
            g_input_stream_close (stream, NULL, NULL);
            g_object_unref (stream);
        }

        if (!pixbuf) {
            gtkicon_info = gtk_icon_theme_lookup_icon_for_scale (icon_theme,
                                                                 "text-x-generic",
                                                                 size,
                                                                 scale,
                                                                 GTK_ICON_LOOKUP_FORCE_SIZE);

            if (gtkicon_info != NULL) {
                pixbuf = gtk_icon_info_load_icon (gtkicon_info, NULL);
                g_object_unref (gtkicon_info);
            }
        }

        icon_info = nemo_icon_info_new_for_pixbuf (pixbuf, scale);

        key = icon_key_new (icon, size);
        g_hash_table_insert (loadable_icon_cache, key, icon_info);

        g_clear_object (&pixbuf);

        return nemo_icon_info_ref (icon_info);
    } else  {
        IconKey lookup_key;
        IconKey *key;

        if (themed_icon_cache == NULL) {
            themed_icon_cache = g_hash_table_new_full ((GHashFunc) icon_key_hash,
                                                       (GEqualFunc) icon_key_equal,
                                                       (GDestroyNotify) icon_key_free,
                                                       (GDestroyNotify) nemo_icon_info_free);
        }

        lookup_key.icon = icon;
        lookup_key.size = size;

        icon_info = g_hash_table_lookup (themed_icon_cache, &lookup_key);
        if (icon_info) {
            return nemo_icon_info_ref (icon_info);
        }

        gtkicon_info = NULL;

        gtkicon_info = gtk_icon_theme_lookup_by_gicon_for_scale (icon_theme,
                                                                 icon,
                                                                 size,
                                                                 scale,
                                                                 GTK_ICON_LOOKUP_FORCE_SIZE);

        if (!gtkicon_info) {
            gtkicon_info = gtk_icon_theme_lookup_icon_for_scale (icon_theme,
                                                                 "text-x-generic",
                                                                 size,
                                                                 scale,
                                                                 GTK_ICON_LOOKUP_FORCE_SIZE);
        }

        icon_info = nemo_icon_info_new_for_icon_info (gtkicon_info, scale);
        g_clear_object (&gtkicon_info);

        key = icon_key_new (icon, size);
        g_hash_table_insert (themed_icon_cache, key, icon_info);

        return nemo_icon_info_ref (icon_info);
    }
}

/* Returns: (transfer full): unref with nemo_icon_info_unref */
NemoIconInfo *
nemo_icon_info_lookup_from_name (const char *name,
                                 int size,
                                 int scale)
{
	GIcon *icon;
	NemoIconInfo *info;

	icon = g_themed_icon_new (name);
	info = nemo_icon_info_lookup (icon, size, scale);
	g_object_unref (icon);
	return info;
}

/* Returns: (transfer full): unref with nemo_icon_info_unref */
NemoIconInfo *
nemo_icon_info_lookup_from_path (const char *path,
                                 int size,
                                 int scale)
{
	GFile *icon_file;
	GIcon *icon;
	NemoIconInfo *info;

	icon_file = g_file_new_for_path (path);
	icon = g_file_icon_new (icon_file);
	info = nemo_icon_info_lookup (icon, size, scale);
	g_object_unref (icon);
	g_object_unref (icon_file);
	return info;
}

/* Returns: (transfer full): unref with g_object_unref */
GdkPixbuf *
nemo_icon_info_get_pixbuf_nodefault (NemoIconInfo  *icon)
{
	GdkPixbuf *res;

	if (icon->pixbuf == NULL) {
		res = NULL;
	} else {
		res = g_object_ref (icon->pixbuf);

		if (icon->sole_owner) {
			icon->sole_owner = FALSE;
			g_object_add_toggle_ref (G_OBJECT (res),
						 pixbuf_toggle_notify,
						 icon);
		}
	}

	return res;
}


/* Returns: (transfer full): unref with g_object_unref */
GdkPixbuf *
nemo_icon_info_get_pixbuf (NemoIconInfo *icon)
{
	GdkPixbuf *res;

	res = nemo_icon_info_get_pixbuf_nodefault (icon);
	if (res == NULL) {
		res = gdk_pixbuf_new_from_data (nemo_default_file_icon,
						GDK_COLORSPACE_RGB,
						TRUE,
						8,
						nemo_default_file_icon_width,
						nemo_default_file_icon_height,
						nemo_default_file_icon_width * 4, /* stride */
						NULL, /* don't destroy info */
						NULL);
	}

	return res;
}

/* Returns: (transfer full): unref with g_object_unref */
GdkPixbuf *
nemo_icon_info_get_pixbuf_nodefault_at_size (NemoIconInfo  *icon,
						 gsize              forced_size)
{
	GdkPixbuf *pixbuf, *scaled_pixbuf;
	guint w, h, s;
	double scale;

	pixbuf = nemo_icon_info_get_pixbuf_nodefault (icon);

	if (pixbuf == NULL)
	  return NULL;

	w = gdk_pixbuf_get_width (pixbuf) / icon->orig_scale;
	h = gdk_pixbuf_get_height (pixbuf) / icon->orig_scale;
	s = MAX (w, h);
	if (s == forced_size) {
		return pixbuf;
	}

	scale = (double)forced_size / s;
	scaled_pixbuf = gdk_pixbuf_scale_simple (pixbuf,
						 MAX (w * scale, 1), MAX (h * scale, 1),
						 GDK_INTERP_BILINEAR);
	g_object_unref (pixbuf);
	return scaled_pixbuf;
}


/* Returns: (transfer full): unref with g_object_unref */
GdkPixbuf *
nemo_icon_info_get_pixbuf_at_size (NemoIconInfo  *icon,
				       gsize              forced_size)
{
	GdkPixbuf *pixbuf, *scaled_pixbuf;
	guint w, h, s;
	double scale;

	pixbuf = nemo_icon_info_get_pixbuf (icon);

	w = gdk_pixbuf_get_width (pixbuf) / icon->orig_scale;
	h = gdk_pixbuf_get_height (pixbuf) / icon->orig_scale;
	s = MAX (w, h);
	if (s == forced_size) {
		return pixbuf;
	}

	scale = (double)forced_size / s;
	scaled_pixbuf = gdk_pixbuf_scale_simple (pixbuf,
						 MAX (w * scale, 1), MAX (h * scale, 1),
						 GDK_INTERP_BILINEAR);
	g_object_unref (pixbuf);
	return scaled_pixbuf;
}

/* Returns: (transfer full): unref with g_object_unref */
GdkPixbuf *
nemo_icon_info_get_desktop_pixbuf_at_size (NemoIconInfo  *icon,
                                           gsize          max_height,
                                           gsize          max_width)
{
    GdkPixbuf *pixbuf, *scaled_pixbuf;
    guint w, h;
    double scale;

    pixbuf = nemo_icon_info_get_pixbuf (icon);

    w = gdk_pixbuf_get_width (pixbuf) / icon->orig_scale;
    h = gdk_pixbuf_get_height (pixbuf) / icon->orig_scale;

    if (w == max_width || h == max_height) {
        return pixbuf;
    }

    scale = (gdouble) max_height / h;

    if (w * scale > max_width) {
        scale = (gdouble) max_width / w;
    }

    scaled_pixbuf = gdk_pixbuf_scale_simple (pixbuf,
                         MAX (w * scale, 1), MAX (h * scale, 1),
                         GDK_INTERP_BILINEAR);
    g_object_unref (pixbuf);
    return scaled_pixbuf;
}

/* Returns: (transfer none): owned by @icon */
const char *
nemo_icon_info_get_used_name (NemoIconInfo  *icon)
{
	return icon->icon_name;
}

/* The stops. Everything between two of them is a size too; these are only what
 * the slider marks and what Zoom In and Zoom Out move between. Adding one is a
 * row here and nothing else. */
static const gint icon_size_steps[] = {
	NEMO_ICON_SIZE_SMALLEST,
	NEMO_ICON_SIZE_SMALLER,
	NEMO_ICON_SIZE_SMALL,
	NEMO_ICON_SIZE_STANDARD,
	NEMO_ICON_SIZE_LARGE,
	NEMO_ICON_SIZE_LARGER,
	NEMO_ICON_SIZE_LARGEST,
	NEMO_ICON_SIZE_HUGE,
	NEMO_ICON_SIZE_HUGEST,
	NEMO_ICON_SIZE_MAXIMUM
};

/* The seven levels a size was until 2026-09, in order, so an older settings
 * file or a folder's saved size still reads as what it meant. */
static const gint legacy_icon_sizes[] = {
	NEMO_ICON_SIZE_SMALLEST,
	NEMO_ICON_SIZE_SMALLER,
	NEMO_ICON_SIZE_SMALL,
	NEMO_ICON_SIZE_STANDARD,
	NEMO_ICON_SIZE_LARGE,
	NEMO_ICON_SIZE_LARGER,
	NEMO_ICON_SIZE_LARGEST
};

static const gint legacy_list_icon_sizes[] = {
	NEMO_LIST_ICON_SIZE_SMALLEST,
	NEMO_LIST_ICON_SIZE_SMALLER,
	NEMO_LIST_ICON_SIZE_SMALL,
	NEMO_LIST_ICON_SIZE_STANDARD,
	NEMO_LIST_ICON_SIZE_LARGE,
	NEMO_LIST_ICON_SIZE_LARGER,
	NEMO_LIST_ICON_SIZE_LARGEST
};

#define N_LEGACY_LEVELS ((gint) G_N_ELEMENTS (legacy_icon_sizes))

/* Returns: (transfer none): a static table */
const gint *
nemo_icon_size_steps (guint *n_steps)
{
	if (n_steps != NULL) {
		*n_steps = G_N_ELEMENTS (icon_size_steps);
	}

	return icon_size_steps;
}

gint
nemo_icon_size_clamp (gint size)
{
	return CLAMP (size, NEMO_ICON_SIZE_MIN, NEMO_ICON_SIZE_MAX);
}

gdouble
nemo_icon_size_position (gint size)
{
	gint i, n;

	size = nemo_icon_size_clamp (size);
	n = G_N_ELEMENTS (icon_size_steps);

	for (i = 0; i < n - 1; i++) {
		if (size <= icon_size_steps[i + 1]) {
			return i + (gdouble) (size - icon_size_steps[i])
				   / (icon_size_steps[i + 1] - icon_size_steps[i]);
		}
	}

	return n - 1;
}

gint
nemo_icon_size_at_position (gdouble position)
{
	gint i, n;
	gdouble frac;

	n = G_N_ELEMENTS (icon_size_steps);
	position = CLAMP (position, 0.0, (gdouble) (n - 1));

	i = (gint) position;
	if (i >= n - 1) {
		return icon_size_steps[n - 1];
	}

	frac = position - i;

	return nemo_icon_size_clamp ((gint) (icon_size_steps[i]
			+ frac * (icon_size_steps[i + 1] - icon_size_steps[i]) + 0.5));
}

gint
nemo_icon_size_step (gint size, gint direction)
{
	gint i;

	size = nemo_icon_size_clamp (size);

	if (direction > 0) {
		for (i = 0; i < (gint) G_N_ELEMENTS (icon_size_steps); i++) {
			if (icon_size_steps[i] > size) {
				return icon_size_steps[i];
			}
		}
	} else if (direction < 0) {
		for (i = G_N_ELEMENTS (icon_size_steps) - 1; i >= 0; i--) {
			if (icon_size_steps[i] < size) {
				return icon_size_steps[i];
			}
		}
	}

	/* Already at the end it was asked to move toward. */
	return size;
}

gint
nemo_icon_size_from_percent (gint percent)
{
	return nemo_icon_size_clamp ((percent * NEMO_ICON_SIZE_STANDARD + 50) / 100);
}

gint
nemo_icon_size_percent (gint size)
{
	return (nemo_icon_size_clamp (size) * 100 + NEMO_ICON_SIZE_STANDARD / 2)
	       / NEMO_ICON_SIZE_STANDARD;
}

gboolean
nemo_icon_size_is_legacy_level (gint saved)
{
	/* No real size is this small, so the two cannot be confused. */
	return saved >= 0 && saved < N_LEGACY_LEVELS;
}

gint
nemo_icon_size_from_legacy_level (gint level)
{
	if (!nemo_icon_size_is_legacy_level (level)) {
		return NEMO_ICON_SIZE_STANDARD;
	}

	return legacy_icon_sizes[level];
}

gint
nemo_list_icon_size_from_legacy_level (gint level)
{
	if (!nemo_icon_size_is_legacy_level (level)) {
		return NEMO_LIST_ICON_SIZE_STANDARD;
	}

	return legacy_list_icon_sizes[level];
}

gint
nemo_icon_size_legacy_level (gint size)
{
	gint i;

	size = nemo_icon_size_clamp (size);

	for (i = 0; i < N_LEGACY_LEVELS; i++) {
		if (size <= legacy_icon_sizes[i]) {
			return i;
		}
	}

	return N_LEGACY_LEVELS - 1;
}

/* A list row is a good deal shorter than an icon view cell, so it keeps its own
 * scale rather than showing the icon view's size. Held to the old ladder, since
 * the list view has no slider and a 640px row is no use to anyone. */
guint
nemo_get_list_icon_size (gint size)
{
	return legacy_list_icon_sizes[nemo_icon_size_legacy_level (size)];
}

/* The desktop covers a narrower range than anything else and always did. */
guint
nemo_get_desktop_icon_size (gint size)
{
	return (guint) CLAMP (size, NEMO_DESKTOP_ICON_SIZE_SMALLER,
			      NEMO_DESKTOP_ICON_SIZE_LARGER);
}

guint
nemo_get_desktop_text_width (gint size)
{
	gint icon = (gint) nemo_get_desktop_icon_size (size);

	/* The old table ran 64, 84, 110, 150, 200 against icons of 24, 32, 48,
	 * 64, 96, which is a little over twice the icon with a floor under it. */
	return (guint) MAX (NEMO_DESKTOP_TEXT_WIDTH_SMALLER, icon * 21 / 10);
}

gint
nemo_get_icon_size_for_stock_size (GtkIconSize size)
{
  gint w, h;

  if (gtk_icon_size_lookup (size, &w, &h)) {
    return nemo_ui_scale_pixels (MAX (w, h), nemo_ui_scale_leftover ());
  }
  return NEMO_ICON_SIZE_STANDARD;
}


guint
nemo_icon_get_emblem_size_for_icon_size (guint size)
{
	if (size >= 96)
		return 48;
	if (size >= 64)
		return 32;
	if (size >= 48)
		return 24;
	if (size >= 24)
		return 16;
	if (size >= 16)
		return 12;

	return 0; /* no emblems for smaller sizes */
}

/* Returns: (transfer full): unref with g_object_unref */
GIcon *
nemo_user_special_directory_get_gicon (GUserDirectory directory)
{

	#define ICON_CASE(x) \
		case G_USER_DIRECTORY_ ## x:\
			return g_themed_icon_new (NEMO_ICON_FOLDER_ ## x);

	switch (directory) {

		ICON_CASE (DESKTOP);
		ICON_CASE (DOCUMENTS);
		ICON_CASE (DOWNLOAD);
		ICON_CASE (MUSIC);
		ICON_CASE (PICTURES);
		ICON_CASE (PUBLIC_SHARE);
		ICON_CASE (TEMPLATES);
		ICON_CASE (VIDEOS);

    case G_USER_N_DIRECTORIES:
	default:
		return g_themed_icon_new ("folder");
	}

	#undef ICON_CASE
}
