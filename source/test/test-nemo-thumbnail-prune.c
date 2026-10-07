/* Keeping the thumbnail cache from growing forever. The rules that decide what
 * goes are pure arithmetic, and the PNG header read is pure parsing, so both
 * are checked here directly. The sweep itself runs over a cache built in a
 * temporary folder. */

#include <config.h>

#include <stdlib.h>
#include <string.h>
#include <glib.h>
#include <glib/gstdio.h>
#include <utime.h>

#include <libnemo-private/nemo-thumbnail-prune.h>

#include "test-scratch.h"
#include "test-check.h"

#define DAY (24 * 60 * 60)
#define NOW ((gint64) 1000000000)

static NemoThumbnailPruneEntry
entry (gint64 size, gint64 age_days, gboolean orphan)
{
	NemoThumbnailPruneEntry item = { 0 };

	item.size = size;
	item.used = NOW - age_days * DAY;
	item.orphan = orphan;

	return item;
}

/* Nothing goes while the cache is inside both limits. */
static void
check_nothing_to_do (void)
{
	NemoThumbnailPruneEntry items[3];
	gint64 freed;

	items[0] = entry (100, 1, FALSE);
	items[1] = entry (100, 2, FALSE);
	items[2] = entry (100, 3, FALSE);

	freed = nemo_thumbnail_prune_plan (items, 3, 1000, 180 * DAY, NOW);

	check (freed == 0);
	check (!items[0].drop && !items[1].drop && !items[2].drop);
}

/* A thumbnail whose file is gone goes however new it is. */
static void
check_orphans_go_first (void)
{
	NemoThumbnailPruneEntry items[2];
	gint64 freed;

	items[0] = entry (100, 0, TRUE);
	items[1] = entry (100, 0, FALSE);

	freed = nemo_thumbnail_prune_plan (items, 2, 1000, 180 * DAY, NOW);

	check (freed == 100);
	check (items[0].drop);
	check (!items[1].drop);
}

static void
check_age_limit (void)
{
	NemoThumbnailPruneEntry items[2];

	items[0] = entry (100, 200, FALSE);
	items[1] = entry (100, 10, FALSE);

	nemo_thumbnail_prune_plan (items, 2, 0, 180 * DAY, NOW);

	check (items[0].drop);
	check (!items[1].drop);
}

/* Over the size allowed, the least recently used go until the rest fit. */
static void
check_budget_evicts_oldest (void)
{
	NemoThumbnailPruneEntry items[4];
	gint64 freed;

	items[0] = entry (100, 40, FALSE);
	items[1] = entry (100, 30, FALSE);
	items[2] = entry (100, 20, FALSE);
	items[3] = entry (100, 10, FALSE);

	freed = nemo_thumbnail_prune_plan (items, 4, 250, 0, NOW);

	check (freed == 200);
	check (items[0].drop && items[1].drop);
	check (!items[2].drop && !items[3].drop);
}

/* The two limits are separate: a cache well inside its size still loses what
   nobody has looked at in months. */
static void
check_age_applies_under_budget (void)
{
	NemoThumbnailPruneEntry items[2];

	items[0] = entry (10, 400, FALSE);
	items[1] = entry (10, 1, FALSE);

	nemo_thumbnail_prune_plan (items, 2, 1000000, 180 * DAY, NOW);

	check (items[0].drop);
	check (!items[1].drop);
}

/* Zero turns a limit off rather than meaning zero. */
static void
check_zero_means_no_limit (void)
{
	NemoThumbnailPruneEntry items[2];
	gint64 freed;

	items[0] = entry (100, 4000, FALSE);
	items[1] = entry (100, 4000, FALSE);

	freed = nemo_thumbnail_prune_plan (items, 2, 0, 0, NOW);

	check (freed == 0);
	check (!items[0].drop && !items[1].drop);
}

static const guchar png_signature[8] = { 0x89, 'P', 'N', 'G', '\r', '\n', 0x1a, '\n' };

static void
put_be32 (GByteArray *out, guint32 value)
{
	guchar bytes[4] = { value >> 24, value >> 16, value >> 8, value };

	g_byte_array_append (out, bytes, 4);
}

/* A PNG header with the text chunks a thumbnailer writes. The chunk CRCs are
   left as zeroes - the reader does not check them and never decodes. */
static GByteArray *
fake_png_header (const char *key, const char *value, gboolean before_idat)
{
	GByteArray *out = g_byte_array_new ();
	gsize body_len = strlen (key) + 1 + strlen (value);

	g_byte_array_append (out, png_signature, sizeof (png_signature));

	if (!before_idat) {
		put_be32 (out, 0);
		g_byte_array_append (out, (const guchar *) "IDAT", 4);
		put_be32 (out, 0);
	}

	put_be32 (out, body_len);
	g_byte_array_append (out, (const guchar *) "tEXt", 4);
	g_byte_array_append (out, (const guchar *) key, strlen (key) + 1);
	g_byte_array_append (out, (const guchar *) value, strlen (value));
	put_be32 (out, 0);

	return out;
}

static void
check_png_text_read (void)
{
	GByteArray *png = fake_png_header ("Thumb::URI", "file:///tmp/a.png", TRUE);
	char *value;

	value = nemo_thumbnail_png_text (png->data, png->len, "Thumb::URI");
	check (g_strcmp0 (value, "file:///tmp/a.png") == 0);
	g_free (value);

	value = nemo_thumbnail_png_text (png->data, png->len, "Thumb::MTime");
	check (value == NULL);
	g_free (value);

	g_byte_array_free (png, TRUE);
}

/* The read stops at the image data and it stops at the end of what was read,
   rather than running off either. */
static void
check_png_text_stops (void)
{
	GByteArray *png = fake_png_header ("Thumb::URI", "file:///tmp/a.png", FALSE);
	char *value;

	value = nemo_thumbnail_png_text (png->data, png->len, "Thumb::URI");
	check (value == NULL);
	g_free (value);

	g_byte_array_free (png, TRUE);

	/* Only the front of a thumbnail is ever read, so a chunk running past the
	   end must read as absent rather than come back cut in half. */
	png = fake_png_header ("Thumb::URI", "file:///tmp/a.png", TRUE);
	value = nemo_thumbnail_png_text (png->data, 20, "Thumb::URI");
	check (value == NULL);
	g_free (value);

	g_byte_array_free (png, TRUE);

	value = nemo_thumbnail_png_text ((const guchar *) "not a png", 9, "Thumb::URI");
	check (value == NULL);
	g_free (value);
}

/* A thumbnail as far as the sweep is concerned: the header it reads the source
   file out of, dated to order. No image data, since nothing here decodes one. */
static gboolean
write_thumbnail (const char *path, const char *source_uri, gint64 used)
{
	GByteArray *png = fake_png_header ("Thumb::URI", source_uri, TRUE);
	struct utimbuf when;
	gboolean ok;

	ok = g_file_set_contents (path, (const char *) png->data, png->len, NULL);
	g_byte_array_free (png, TRUE);

	if (!ok) {
		return FALSE;
	}

	when.actime = used;
	when.modtime = used;

	return g_utime (path, &when) == 0;
}

static void
check_sweep_over_a_real_cache (void)
{
	char *root, *normal, *fail_dir;
	char *kept, *stale, *orphan, *source;
	gint64 now = g_get_real_time () / G_USEC_PER_SEC;
	gint64 freed;

	root = test_scratch_dir ("nemo-prune-XXXXXX", NULL);
	if (root == NULL) {
		g_printerr ("FAIL: no temporary folder\n");
		failures++;
		return;
	}

	normal = g_build_filename (root, "normal", NULL);
	fail_dir = g_build_filename (root, "fail", "nemo-anywhere", NULL);
	g_mkdir_with_parents (normal, 0700);
	g_mkdir_with_parents (fail_dir, 0700);

	/* A file two of the thumbnails claim to be of. */
	source = g_build_filename (root, "picture.png", NULL);
	check (g_file_set_contents (source, "x", 1, NULL));

	kept = g_build_filename (normal, "kept.png", NULL);
	stale = g_build_filename (normal, "stale.png", NULL);
	orphan = g_build_filename (fail_dir, "orphan.png", NULL);

	{
		char *uri = g_filename_to_uri (source, NULL, NULL);

		check (write_thumbnail (kept, uri, now - DAY));
		check (write_thumbnail (stale, uri, now - 400 * DAY));
		g_free (uri);
	}

	check (write_thumbnail (orphan, "file:///nowhere/at/all.png", now - DAY));

	freed = nemo_thumbnail_prune_sweep (root, "nemo-anywhere", 0, 180 * DAY, now);

	check (freed > 0);
	check (g_file_test (kept, G_FILE_TEST_EXISTS));
	check (!g_file_test (stale, G_FILE_TEST_EXISTS));
	check (!g_file_test (orphan, G_FILE_TEST_EXISTS));

	/* Noting a use puts the date forward, so the next sweep keeps it. */
	{
		char *old = g_build_filename (normal, "revived.png", NULL);
		char *uri = g_filename_to_uri (source, NULL, NULL);

		check (write_thumbnail (old, uri, now - 400 * DAY));
		nemo_thumbnail_prune_note_use (old);
		nemo_thumbnail_prune_sweep (root, "nemo-anywhere", 0, 180 * DAY, now);
		check (g_file_test (old, G_FILE_TEST_EXISTS));

		g_unlink (old);
		g_free (old);
		g_free (uri);
	}

	g_unlink (kept);
	g_unlink (source);
	g_rmdir (normal);
	g_rmdir (fail_dir);

	g_free (kept);
	g_free (stale);
	g_free (orphan);
	g_free (source);
	g_free (normal);
	g_free (fail_dir);
	g_free (root);
}

int
main (int argc, char *argv[])
{
	check_nothing_to_do ();
	check_orphans_go_first ();
	check_age_limit ();
	check_budget_evicts_oldest ();
	check_age_applies_under_budget ();
	check_zero_means_no_limit ();
	check_png_text_read ();
	check_png_text_stops ();
	check_sweep_over_a_real_cache ();

	if (failures > 0) {
		g_printerr ("%d check(s) failed\n", failures);
		return EXIT_FAILURE;
	}

	g_print ("OK\n");
	return EXIT_SUCCESS;
}
