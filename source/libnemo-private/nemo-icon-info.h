#ifndef NEMO_ICON_INFO_H
#define NEMO_ICON_INFO_H

#include <glib-object.h>
#include <gdk-pixbuf/gdk-pixbuf.h>
#include <gdk/gdk.h>
#include <gio/gio.h>
#include <gtk/gtk.h>

G_BEGIN_DECLS

/* A view's icon size is a number of pixels at a scale factor of 1. It used to
 * be one of seven named levels, which meant eight separate tables keyed by the
 * level and no way to ask for a size that was not on the list. The names
 * survive below only as the steps the slider marks, what Zoom In and Zoom Out
 * move between, and what an older settings file has to be read as.
 *
 * Icons are assumed to fit a square, though an individual image need not be
 * square and can be stretched, so this is a nominal size rather than a bound.
 */

#define NEMO_COMPACT_FORCED_ICON_SIZE 16

#define NEMO_LIST_ICON_SIZE_SMALLEST 16
#define NEMO_LIST_ICON_SIZE_SMALLER  16
#define NEMO_LIST_ICON_SIZE_SMALL    24
#define NEMO_LIST_ICON_SIZE_STANDARD 32
#define NEMO_LIST_ICON_SIZE_LARGE    48
#define NEMO_LIST_ICON_SIZE_LARGER   72
#define NEMO_LIST_ICON_SIZE_LARGEST  96

#define NEMO_ICON_SIZE_SMALLEST 24
#define NEMO_ICON_SIZE_SMALLER  32
#define NEMO_ICON_SIZE_SMALL    48
#define NEMO_ICON_SIZE_STANDARD 64
#define NEMO_ICON_SIZE_LARGE    96
#define NEMO_ICON_SIZE_LARGER   128
#define NEMO_ICON_SIZE_LARGEST  256
#define NEMO_ICON_SIZE_HUGE     320
#define NEMO_ICON_SIZE_HUGEST   448
#define NEMO_ICON_SIZE_MAXIMUM  640

/* What a folder of images opens at, and the range the slider covers. */
#define NEMO_ICON_SIZE_IMAGES   NEMO_ICON_SIZE_HUGE
#define NEMO_ICON_SIZE_MIN      NEMO_ICON_SIZE_SMALLEST
#define NEMO_ICON_SIZE_MAX      NEMO_ICON_SIZE_MAXIMUM

#define NEMO_DESKTOP_ICON_SIZE_SMALLER 24
#define NEMO_DESKTOP_ICON_SIZE_SMALL 32
#define NEMO_DESKTOP_ICON_SIZE_STANDARD 48
#define NEMO_DESKTOP_ICON_SIZE_LARGE 64
#define NEMO_DESKTOP_ICON_SIZE_LARGER 96

#define NEMO_DESKTOP_TEXT_WIDTH_SMALLER 64
#define NEMO_DESKTOP_TEXT_WIDTH_SMALL 84
#define NEMO_DESKTOP_TEXT_WIDTH_STANDARD 110
#define NEMO_DESKTOP_TEXT_WIDTH_LARGE 150
#define NEMO_DESKTOP_TEXT_WIDTH_LARGER 200

/* A name runs as wide as the icon above it, but never narrower than this, or
 * an ordinary file name wraps into three lines at the usual size. The old
 * table of widths had 110 here and nothing wider until the icon passed it. */
#define NEMO_ICON_LABEL_WIDTH_MIN 110

/* Smallest icon that still gets a name under it. Below this the label would be
 * most of the cell, so it is left off, which is what the old smallest level
 * did with a text width of zero. */
#define NEMO_ICON_SIZE_LABEL_MIN 32

/* Maximum size of an icon that the icon factory will ever produce */
#define NEMO_ICON_MAXIMUM_SIZE     320

typedef struct {
    gint ref_count;
    gboolean sole_owner;
    gint64 last_use_time;
    GdkPixbuf *pixbuf;

    char *icon_name;
    gint orig_scale;
} NemoIconInfo;

NemoIconInfo *    nemo_icon_info_ref                          (NemoIconInfo      *icon);
void              nemo_icon_info_unref                        (NemoIconInfo      *icon);
void              nemo_icon_info_clear                        (NemoIconInfo     **info);
NemoIconInfo *    nemo_icon_info_new_for_pixbuf               (GdkPixbuf         *pixbuf,
                                                               int                scale);
NemoIconInfo *    nemo_icon_info_lookup                       (GIcon             *icon,
                                                               int                size,
                                                               int                scale);
NemoIconInfo *    nemo_icon_info_lookup_from_name             (const char        *name,
                                                               int                size,
                                                               int                scale);
NemoIconInfo *    nemo_icon_info_lookup_from_path             (const char        *path,
                                                               int                size,
                                                               int                scale);
gboolean              nemo_icon_info_is_fallback                  (NemoIconInfo  *icon);
GdkPixbuf *           nemo_icon_info_get_pixbuf                   (NemoIconInfo  *icon);
GdkPixbuf *           nemo_icon_info_get_pixbuf_nodefault         (NemoIconInfo  *icon);
GdkPixbuf *           nemo_icon_info_get_pixbuf_nodefault_at_size (NemoIconInfo  *icon,
								       gsize              forced_size);
GdkPixbuf *           nemo_icon_info_get_pixbuf_at_size           (NemoIconInfo  *icon,
								       gsize              forced_size);
GdkPixbuf *           nemo_icon_info_get_desktop_pixbuf_at_size (NemoIconInfo  *icon,
                                                                 gsize          max_height,
                                                                 gsize          max_width);
const char *          nemo_icon_info_get_used_name                (NemoIconInfo  *icon);

void                  nemo_icon_info_clear_caches                 (void);

/* The sizes the slider marks and the zoom steps move between, smallest first.
 * Everything between two of them is a size as well; these are only the stops. */
const gint * nemo_icon_size_steps                (guint *n_steps);

/* The next stop above or below `size`, or `size` itself at either end.
 * `direction` is 1 or -1. */
gint  nemo_icon_size_step                        (gint size, gint direction);

gint  nemo_icon_size_clamp                       (gint size);

/* Where a size sits along the steps, counting a size between two of them as a
 * fraction of the way from one to the next. The slider runs on this rather
 * than on pixels, so its marks come out evenly spaced over a range that is
 * nearly thirty times as wide at one end as the other. */
gdouble nemo_icon_size_position                  (gint size);
gint  nemo_icon_size_at_position                 (gdouble position);

/* A size as a per cent of the standard size, which is how both the settings
 * file and the settings window express one. */
gint  nemo_icon_size_from_percent                (gint percent);
gint  nemo_icon_size_percent                     (gint size);

/* What one icon-view size means to the views that keep their own scale. */
guint nemo_get_list_icon_size                    (gint size);
guint nemo_get_desktop_icon_size                 (gint size);
guint nemo_get_desktop_text_width                (gint size);

/* Sizes were one of seven levels until 2026-09. A saved value that small is
 * one of those rather than a number of pixels. */
gboolean nemo_icon_size_is_legacy_level          (gint saved);
gint  nemo_icon_size_from_legacy_level           (gint level);
gint  nemo_list_icon_size_from_legacy_level      (gint level);

/* The nearest of those levels to a size. Only the list view still wants this,
 * because its icon is picked by which model column a row reads. */
gint  nemo_icon_size_legacy_level                (gint size);

gint  nemo_get_icon_size_for_stock_size          (GtkIconSize        size);
guint nemo_icon_get_emblem_size_for_icon_size    (guint              size);

GIcon * nemo_user_special_directory_get_gicon (GUserDirectory directory);


G_END_DECLS

#endif /* NEMO_ICON_INFO_H */

