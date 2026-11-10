/* Thumbnails read from and written to the file cache. The rules checked:
 * a photo goes in as JPEG and a picture with see-through parts as PNG, a load
 * never decodes bigger than it was asked for and says when it cut one down,
 * and the store is read before the freedesktop cache. */

#include <config.h>

#include <string.h>
#include <glib.h>
#include <glib/gstdio.h>
#include <gdk-pixbuf/gdk-pixbuf.h>

#include <libnemo-private/nemo-cache-db.h>
#include <libnemo-private/nemo-thumbnails.h>

#include "test-scratch.h"
#include "test-check.h"

#define MTIME ((gint64) 1700000000 * G_USEC_PER_SEC)

static GdkPixbuf *
solid (int width, int height, gboolean alpha, guint8 opacity)
{
	GdkPixbuf *pixbuf = gdk_pixbuf_new (GDK_COLORSPACE_RGB, alpha, 8, width, height);

	gdk_pixbuf_fill (pixbuf, 0x3366aa00u | opacity);

	return pixbuf;
}

static NemoFileId
id_for (gint64 bytes)
{
	NemoFileId id = { 0 };

	id.bytes = bytes;
	id.mtime = MTIME;

	return id;
}

static void
check_format_choice (void)
{
	g_autoptr (GdkPixbuf) photo = solid (40, 30, FALSE, 0xff);
	g_autoptr (GdkPixbuf) opaque_alpha = solid (40, 30, TRUE, 0xff);
	g_autoptr (GdkPixbuf) see_through = solid (40, 30, TRUE, 0x80);
	g_autoptr (GBytes) a = NULL;
	g_autoptr (GBytes) b = NULL;
	g_autoptr (GBytes) c = NULL;
	NemoThumbnailFormat format;

	a = nemo_thumbnail_encode (photo, &format);
	check (a != NULL && format == NEMO_THUMBNAIL_FORMAT_JPEG);

	/* An alpha channel with nothing see-through in it is still a photo. */
	b = nemo_thumbnail_encode (opaque_alpha, &format);
	check (b != NULL && format == NEMO_THUMBNAIL_FORMAT_JPEG);

	c = nemo_thumbnail_encode (see_through, &format);
	check (c != NULL && format == NEMO_THUMBNAIL_FORMAT_PNG);
}

static void
check_size_step (void)
{
	check (nemo_thumbnail_size_step (1) == NEMO_THUMBNAIL_SIZE_STEP);
	check (nemo_thumbnail_size_step (NEMO_THUMBNAIL_SIZE_STEP) == NEMO_THUMBNAIL_SIZE_STEP);
	check (nemo_thumbnail_size_step (NEMO_THUMBNAIL_SIZE_STEP + 1) == 2 * NEMO_THUMBNAIL_SIZE_STEP);
}

/* Stores a 400x200 picture rendered for 512, then reads it back at 2 sizes. */
static void
check_load_caps (NemoCacheDb *db)
{
	g_autoptr (GdkPixbuf) wide = solid (400, 200, FALSE, 0xff);
	g_autoptr (GBytes) image = NULL;
	NemoThumbnailRecord record = { 0 };
	NemoThumbnailLoaded small = { 0 };
	NemoThumbnailLoaded big = { 0 };
	NemoFileId id = id_for (1000);

	record.size = 512;
	record.width = 400;
	record.height = 200;
	image = nemo_thumbnail_encode (wide, &record.format);
	check (nemo_cache_db_thumbnail_store (db, "file:///wide.jpg", &id, &record, image));

	nemo_thumbnail_load ("file:///wide.jpg", &id, NULL, 128, &small);
	check (small.pixbuf != NULL);
	check (small.from_store);
	check (small.capped);
	check (small.stored_size == 512);
	check (!small.stored_capped);
	if (small.pixbuf != NULL) {
		check (gdk_pixbuf_get_width (small.pixbuf) == 128);
		check (gdk_pixbuf_get_height (small.pixbuf) == 64);
	}

	nemo_thumbnail_load ("file:///wide.jpg", &id, NULL, 640, &big);
	check (big.pixbuf != NULL);
	check (!big.capped);
	if (big.pixbuf != NULL)
		check (gdk_pixbuf_get_width (big.pixbuf) == 400);

	g_clear_object (&small.pixbuf);
	g_clear_object (&big.pixbuf);
}

/* A recorded failure is reported as one, with nothing to draw. */
static void
check_load_failure (NemoCacheDb *db)
{
	NemoThumbnailRecord record = { 0 };
	NemoThumbnailLoaded loaded = { 0 };
	NemoFileId id = id_for (2000);

	record.size = 256;
	check (nemo_cache_db_thumbnail_store (db, "file:///bad.jpg", &id, &record, NULL));

	nemo_thumbnail_load ("file:///bad.jpg", &id, NULL, 256, &loaded);
	check (loaded.failed);
	check (loaded.pixbuf == NULL);
}

/* The freedesktop copy is used only when the store has none, and its stamp comes
 * back so a stale one can be refused. */
static void
check_shared_fallback (NemoCacheDb *db, const char *scratch)
{
	g_autofree char *shared = g_build_filename (scratch, "shared.png", NULL);
	g_autoptr (GdkPixbuf) green = solid (100, 100, FALSE, 0xff);
	g_autoptr (GdkPixbuf) red = solid (60, 60, FALSE, 0xff);
	g_autoptr (GBytes) image = NULL;
	NemoThumbnailRecord record = { 0 };
	NemoThumbnailLoaded before = { 0 };
	NemoThumbnailLoaded after = { 0 };
	NemoFileId id = id_for (3000);

	check (gdk_pixbuf_save (green, shared, "png", NULL, "tEXt::Thumb::MTime", "1700000000", NULL));

	nemo_thumbnail_load ("file:///shared.jpg", &id, shared, 256, &before);
	check (before.pixbuf != NULL);
	check (!before.from_store);
	check (before.shared_mtime == 1700000000);
	if (before.pixbuf != NULL)
		check (gdk_pixbuf_get_width (before.pixbuf) == 100);

	record.size = 256;
	record.width = 60;
	record.height = 60;
	image = nemo_thumbnail_encode (red, &record.format);
	check (nemo_cache_db_thumbnail_store (db, "file:///shared.jpg", &id, &record, image));

	nemo_thumbnail_load ("file:///shared.jpg", &id, shared, 256, &after);
	check (after.pixbuf != NULL);
	check (after.from_store);
	if (after.pixbuf != NULL)
		check (gdk_pixbuf_get_width (after.pixbuf) == 60);

	g_clear_object (&before.pixbuf);
	g_clear_object (&after.pixbuf);
}

int
main (int argc, char *argv[])
{
	g_autofree char *scratch = NULL;
	NemoCacheDb *db;

	(void) argc;
	(void) argv;

	scratch = test_scratch_config_home ("nemo-thumbstore-XXXXXX");
	if (scratch == NULL) {
		g_printerr ("could not make a scratch dir\n");
		return 77;
	}

	db = nemo_cache_db_get ();
	if (db == NULL) {
		g_printerr ("could not open a file cache in %s\n", scratch);
		return 77;
	}

	check_format_choice ();
	check_size_step ();
	check_load_caps (db);
	check_load_failure (db);
	check_shared_fallback (db, scratch);

	nemo_cache_db_close ();

	if (failures > 0) {
		g_printerr ("%d check(s) failed\n", failures);
		return 1;
	}

	g_print ("OK\n");
	return 0;
}
