/* Icons and the part of a display scale the toolkit leaves out. At 150% GTK
 * on Windows draws at 1, so a 16 pixel icon has to be asked for at 24 or it
 * sits a third too small beside type that is already the right size. Off
 * Windows nothing moves, since the desktop's font DPI is a text setting there.
 */

#include <config.h>

#include <stdlib.h>
#include <gtk/gtk.h>

#include <libnemo-private/nemo-icon-info.h>
#include <libnemo-private/nemo-ui-scale.h>
#include "test-check.h"

#define DPI(n) ((n) * 1024)

static void
check_leftover (void)
{
	check (nemo_ui_scale_leftover_for_dpi (DPI (96)) == 1.0);
	check (nemo_ui_scale_leftover_for_dpi (DPI (120)) == 1.25);
	check (nemo_ui_scale_leftover_for_dpi (DPI (144)) == 1.5);
	check (nemo_ui_scale_leftover_for_dpi (DPI (168)) == 1.75);

	/* Unset, below the standard, or past what a whole step can leave. */
	check (nemo_ui_scale_leftover_for_dpi (-1) == 1.0);
	check (nemo_ui_scale_leftover_for_dpi (0) == 1.0);
	check (nemo_ui_scale_leftover_for_dpi (DPI (72)) == 1.0);
	check (nemo_ui_scale_leftover_for_dpi (DPI (960)) == 2.0);
}

static void
check_pixels (void)
{
	check (nemo_ui_scale_pixels (16, 1.0) == 16);
	check (nemo_ui_scale_pixels (16, 1.25) == 20);
	check (nemo_ui_scale_pixels (16, 1.5) == 24);
	check (nemo_ui_scale_pixels (16, 1.75) == 28);
	check (nemo_ui_scale_pixels (24, 1.5) == 36);
	check (nemo_ui_scale_pixels (16, 0.5) == 16);
}

static void
check_icon_size (void)
{
	GtkIconSize scaled, again;
	gint w = 0, h = 0;

	check (nemo_ui_scale_icon_size (GTK_ICON_SIZE_MENU, 1.0) == GTK_ICON_SIZE_MENU);

	scaled = nemo_ui_scale_icon_size (GTK_ICON_SIZE_MENU, 1.5);
	check (scaled != GTK_ICON_SIZE_MENU);
	check (gtk_icon_size_lookup (scaled, &w, &h) && w == 24 && h == 24);

	/* One registered size per pixel size, whoever asks for it. */
	again = nemo_ui_scale_icon_size (GTK_ICON_SIZE_BUTTON, 1.5);
	check (again == scaled);
	check (nemo_ui_scale_icon_size (GTK_ICON_SIZE_MENU, 1.25) != scaled);
}

static void
check_image (void)
{
	GtkWidget *named = gtk_image_new_from_icon_name ("folder", GTK_ICON_SIZE_MENU);
	GtkWidget *owned = gtk_image_new_from_icon_name ("folder", GTK_ICON_SIZE_MENU);
	GtkWidget *empty = gtk_image_new ();
	GdkPixbuf *pixbuf = gdk_pixbuf_new (GDK_COLORSPACE_RGB, TRUE, 8, 16, 16);
	GtkWidget *picture = gtk_image_new_from_pixbuf (pixbuf);

	g_object_ref_sink (named);
	g_object_ref_sink (owned);
	g_object_ref_sink (empty);
	g_object_ref_sink (picture);

	nemo_ui_scale_image (GTK_IMAGE (named), 1.5);
	check (gtk_image_get_pixel_size (GTK_IMAGE (named)) == 24);

	/* Asking again at the same scale changes nothing. */
	nemo_ui_scale_image (GTK_IMAGE (named), 1.5);
	check (gtk_image_get_pixel_size (GTK_IMAGE (named)) == 24);

	/* Moved to 125%, then back to a whole step, where the theme's own
	   size is put back. */
	nemo_ui_scale_image (GTK_IMAGE (named), 1.25);
	check (gtk_image_get_pixel_size (GTK_IMAGE (named)) == 20);
	nemo_ui_scale_image (GTK_IMAGE (named), 1.0);
	check (gtk_image_get_pixel_size (GTK_IMAGE (named)) == -1);

	/* A bigger named size scales from its own size. */
	gtk_image_set_from_icon_name (GTK_IMAGE (named), "folder", GTK_ICON_SIZE_LARGE_TOOLBAR);
	nemo_ui_scale_image (GTK_IMAGE (named), 1.5);
	check (gtk_image_get_pixel_size (GTK_IMAGE (named)) == 36);

	/* A size the code chose is the code's business. */
	gtk_image_set_pixel_size (GTK_IMAGE (owned), 40);
	nemo_ui_scale_image (GTK_IMAGE (owned), 1.5);
	check (gtk_image_get_pixel_size (GTK_IMAGE (owned)) == 40);

	/* Nothing to size yet, or a picture drawn at the size wanted. */
	nemo_ui_scale_image (GTK_IMAGE (empty), 1.5);
	check (gtk_image_get_pixel_size (GTK_IMAGE (empty)) == -1);
	nemo_ui_scale_image (GTK_IMAGE (picture), 1.5);
	check (gtk_image_get_pixel_size (GTK_IMAGE (picture)) == -1);

	g_object_unref (named);
	g_object_unref (owned);
	g_object_unref (empty);
	g_object_unref (picture);
	g_object_unref (pixbuf);
}

/* What the app does with a font DPI of 144 on each platform: on Windows that
   is 150% and icons follow it, packed or given their icon later; anywhere
   else it is the desktop's text size and icons stay as GTK draws them. */
static void
check_installed (void)
{
	GtkSettings *settings = gtk_settings_get_default ();
	GtkWidget *box = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 0);
	GtkWidget *packed = gtk_image_new_from_icon_name ("folder", GTK_ICON_SIZE_SMALL_TOOLBAR);
	GtkWidget *later = gtk_image_new ();
#ifdef G_OS_WIN32
	const gint at_150 = 24;
	const gint menu_150 = 24;
#else
	const gint at_150 = -1;
	const gint menu_150 = 16;
#endif

	g_object_ref_sink (box);
	nemo_ui_scale_install ();

	g_object_set (settings, "gtk-xft-dpi", DPI (144), NULL);
	gtk_container_add (GTK_CONTAINER (box), packed);
	gtk_container_add (GTK_CONTAINER (box), later);
	gtk_image_set_from_icon_name (GTK_IMAGE (later), "folder", GTK_ICON_SIZE_MENU);

	check (gtk_image_get_pixel_size (GTK_IMAGE (packed)) == at_150);
	check (gtk_image_get_pixel_size (GTK_IMAGE (later)) == at_150);
	check (nemo_get_icon_size_for_stock_size (GTK_ICON_SIZE_MENU) == menu_150);

	/* And back when the window moves to a monitor at 100%. */
	g_object_set (settings, "gtk-xft-dpi", DPI (96), NULL);
	check (gtk_image_get_pixel_size (GTK_IMAGE (packed)) == -1);
	check (gtk_image_get_pixel_size (GTK_IMAGE (later)) == -1);
	check (nemo_get_icon_size_for_stock_size (GTK_ICON_SIZE_MENU) == 16);

	g_object_unref (box);
}

int
main (int argc, char **argv)
{
	check_leftover ();
	check_pixels ();

	if (!gtk_init_check (&argc, &argv)) {
		g_print ("SKIP: no display\n");
		return 77;
	}

	check_icon_size ();
	check_image ();
	check_installed ();

	if (failures > 0) {
		g_printerr ("%d check(s) failed\n", failures);
		return EXIT_FAILURE;
	}

	g_print ("OK\n");
	return EXIT_SUCCESS;
}
