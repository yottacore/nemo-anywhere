/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */

/* nemo-ui-scale.c - icons for the part of a display scale GTK leaves out.

   Copyright © 2026 t00mietum (CryptogID: ปʬϝღถɔ4რఠΔթะ9ƾǝu).

   This program is free software; you can redistribute it and/or
   modify it under the terms of the GNU General Public License as
   published by the Free Software Foundation; version 2 of the
   License.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, see <http://www.gnu.org/licenses/>.
*/

#include <config.h>

#include "nemo-ui-scale.h"

/* The pixel size this file set on an image, so one the code set itself can be
   told apart and left alone. */
#define SET_BY_US "nemo-ui-scale-px"
#define WATCHED   "nemo-ui-scale-watched"

gdouble
nemo_ui_scale_leftover_for_dpi (gint xft_dpi)
{
	if (xft_dpi <= 0) {
		return 1.0;
	}

	/* The whole step is floor (dpi / 96), so what is left is under 2. */
	return CLAMP (xft_dpi / 1024.0 / 96.0, 1.0, 2.0);
}

gdouble
nemo_ui_scale_leftover (void)
{
#ifdef G_OS_WIN32
	GtkSettings *settings = gtk_settings_get_default ();
	gint xft_dpi = -1;

	if (settings == NULL) {
		return 1.0;
	}

	g_object_get (settings, "gtk-xft-dpi", &xft_dpi, NULL);
	return nemo_ui_scale_leftover_for_dpi (xft_dpi);
#else
	return 1.0;
#endif
}

gint
nemo_ui_scale_pixels (gint pixels, gdouble leftover)
{
	if (leftover <= 1.0) {
		return pixels;
	}

	return (gint) (pixels * leftover + 0.5);
}

GtkIconSize
nemo_ui_scale_icon_size (GtkIconSize size, gdouble leftover)
{
	/* Asked once per row drawn, so the last answer is kept. */
	static gint last_px;
	static GtkIconSize last_size = GTK_ICON_SIZE_INVALID;
	g_autofree char *name = NULL;
	gint w, h, px;

	if (!gtk_icon_size_lookup (size, &w, &h)) {
		return size;
	}

	px = nemo_ui_scale_pixels (MAX (w, h), leftover);
	if (px == MAX (w, h)) {
		return size;
	}
	if (px == last_px && last_size != GTK_ICON_SIZE_INVALID) {
		return last_size;
	}

	/* Registered sizes live for the life of the process, so one per pixel
	   size, shared by everyone who asks. */
	name = g_strdup_printf ("nemo-ui-scale-%d", px);
	last_size = gtk_icon_size_from_name (name);
	if (last_size == GTK_ICON_SIZE_INVALID) {
		last_size = gtk_icon_size_register (name, px, px);
	}
	last_px = px;

	return last_size;
}

void
nemo_ui_scale_image (GtkImage *image, gdouble leftover)
{
	GtkImageType type;
	GtkIconSize size = GTK_ICON_SIZE_INVALID;
	gint ours, current, w, h, wanted;

	g_return_if_fail (GTK_IS_IMAGE (image));

	/* Only these two go through the pixel size; anything else is a picture
	   the code drew at the size it wanted. */
	type = gtk_image_get_storage_type (image);
	if (type != GTK_IMAGE_ICON_NAME && type != GTK_IMAGE_GICON) {
		return;
	}

	ours = GPOINTER_TO_INT (g_object_get_data (G_OBJECT (image), SET_BY_US));
	current = gtk_image_get_pixel_size (image);
	if (current != -1 && current != ours) {
		return;
	}

	g_object_get (image, "icon-size", &size, NULL);
	if (!gtk_icon_size_lookup (size, &w, &h)) {
		return;
	}

	wanted = nemo_ui_scale_pixels (MAX (w, h), leftover);
	if (wanted == MAX (w, h)) {
		wanted = -1;
	}

	g_object_set_data (G_OBJECT (image), SET_BY_US, GINT_TO_POINTER (wanted == -1 ? 0 : wanted));
	if (wanted != current) {
		gtk_image_set_pixel_size (image, wanted);
	}
}

#ifdef G_OS_WIN32

static GHashTable *watched;

static void
image_changed_cb (GtkImage *image,
		  G_GNUC_UNUSED GParamSpec *pspec,
		  G_GNUC_UNUSED gpointer    data)
{
	nemo_ui_scale_image (image, nemo_ui_scale_leftover ());
}

static void
image_gone (G_GNUC_UNUSED gpointer data, GObject *where_it_was)
{
	g_hash_table_remove (watched, where_it_was);
}

static void
watch_image (GtkImage *image)
{
	if (g_object_get_data (G_OBJECT (image), WATCHED) != NULL) {
		return;
	}
	g_object_set_data (G_OBJECT (image), WATCHED, GINT_TO_POINTER (1));

	/* An image is often packed empty and given its icon later, or moved to
	   another size, so those are when it is sized again. */
	g_signal_connect (image, "notify::storage-type", G_CALLBACK (image_changed_cb), NULL);
	g_signal_connect (image, "notify::icon-size", G_CALLBACK (image_changed_cb), NULL);
	g_object_weak_ref (G_OBJECT (image), image_gone, NULL);
	g_hash_table_add (watched, image);

	nemo_ui_scale_image (image, nemo_ui_scale_leftover ());
}

/* Every image is packed into something before it shows, builder files and
   GTK's own buttons included, so this sees them all. */
static gboolean
parent_set_hook (G_GNUC_UNUSED GSignalInvocationHint *hint,
		 guint                  n_values,
		 const GValue          *values,
		 G_GNUC_UNUSED gpointer data)
{
	GObject *object;

	if (n_values < 1) {
		return TRUE;
	}

	object = g_value_get_object (&values[0]);
	if (GTK_IS_IMAGE (object)) {
		watch_image (GTK_IMAGE (object));
	}

	return TRUE;
}

static void
dpi_changed_cb (G_GNUC_UNUSED GtkSettings *settings,
		G_GNUC_UNUSED GParamSpec  *pspec,
		G_GNUC_UNUSED gpointer     data)
{
	gdouble leftover = nemo_ui_scale_leftover ();
	GHashTableIter iter;
	gpointer image;

	g_hash_table_iter_init (&iter, watched);
	while (g_hash_table_iter_next (&iter, &image, NULL)) {
		nemo_ui_scale_image (GTK_IMAGE (image), leftover);
	}
}

void
nemo_ui_scale_install (void)
{
	GtkSettings *settings;

	if (watched != NULL) {
		return;
	}
	watched = g_hash_table_new (NULL, NULL);

	g_signal_add_emission_hook (g_signal_lookup ("parent-set", GTK_TYPE_WIDGET),
				    0, parent_set_hook, NULL, NULL);

	settings = gtk_settings_get_default ();
	if (settings != NULL) {
		g_signal_connect (settings, "notify::gtk-xft-dpi",
				  G_CALLBACK (dpi_changed_cb), NULL);
	}
}

#else

void
nemo_ui_scale_install (void)
{
}

#endif /* G_OS_WIN32 */
