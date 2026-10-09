/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */

/* test-nemo-office-ole.c - .doc, .xls and .ppt read in the app.

   Copyright © 2026 t00mietum (CryptogID: ปʬϝღถɔ4რఠΔթะ9ƾǝu).

   This program is free software; you can redistribute it and/or
   modify it under the terms of the GNU General Public License as
   published by the Free Software Foundation; version 2 of the
   License.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   General Public License for more details.

   You should have received a copy of the GNU General Public
   License along with this program; if not, write to the
   Free Software Foundation, Inc., 51 Franklin Street, Suite 500,
   Boston, MA 02110-1335, USA.
*/

/* Small Word, Excel and PowerPoint files, written with the tests' own
 * compound file writer in test-ole2-writer.c, have to give their text through the reader and
 * through a real content search, and the bitmap in thier summary stream
 * through the thumbnail factory. A thumbnailer and a search helper for these
 * types are installed, both this program run with --ran, which leaves a
 * mark. Nothing may leave one. Then damaged and lying files, which must
 * answer quickly and never crash. Same layout as test-nemo-office.c. */

#include <config.h>

#include <string.h>

#include <glib.h>
#include <glib/gstdio.h>
#include <gtk/gtk.h>

#include <libnemo-private/nemo-desktop-thumbnail.h>
#include <libnemo-private/nemo-global-preferences.h>
#include <libnemo-private/nemo-office.h>
#include <libnemo-private/nemo-office-ole.h>
#include <libnemo-private/nemo-query.h>
#include <libnemo-private/nemo-search-engine-advanced.h>

#include "test-scratch.h"
#include "test-check.h"
#include "test-ole2-writer.h"

#define MARK_ENV "NEMO_OFFICE_TEST_MARK"

static char *files_dir;

static void
put16 (GByteArray *a, guint16 v)
{
	guint8 b[2] = { (guint8) (v & 0xff), (guint8) (v >> 8) };

	g_byte_array_append (a, b, 2);
}

static void
put32 (GByteArray *a, guint32 v)
{
	guint8 b[4] = { (guint8) (v & 0xff), (guint8) ((v >> 8) & 0xff), (guint8) ((v >> 16) & 0xff), (guint8) (v >> 24) };

	g_byte_array_append (a, b, 4);
}

static void
set32 (guint8 *p, guint32 v)
{
	p[0] = (guint8) (v & 0xff);
	p[1] = (guint8) ((v >> 8) & 0xff);
	p[2] = (guint8) ((v >> 16) & 0xff);
	p[3] = (guint8) (v >> 24);
}

static void
set16 (guint8 *p, guint16 v)
{
	p[0] = (guint8) (v & 0xff);
	p[1] = (guint8) (v >> 8);
}

static void
put_utf16 (GByteArray *a, const char *utf8)
{
	glong n = 0, i;
	g_autofree gunichar2 *units = g_utf8_to_utf16 (utf8, -1, NULL, &n, NULL);

	for (i = 0; i < n; i++) {
		put16 (a, units[i]);
	}
}

static void
zeros (GByteArray *a, gsize n)
{
	static const guint8 nothing[64] = { 0 };

	while (n > 0) {
		gsize take = MIN (n, sizeof nothing);

		g_byte_array_append (a, nothing, (guint) take);
		n -= take;
	}
}

/* Where FAT entry @sector sits in the written file. */
static gsize
fat_entry_at (const OleLayout *lay, guint32 sector)
{
	gsize per = ((gsize) 1 << lay->shift) / 4;

	return ((gsize) lay->fat_sector[sector / per] + 1) * ((gsize) 1 << lay->shift) + (sector % per) * 4;
}

static gsize
dir_entry_at (const OleLayout *lay, guint id)
{
	/* The directory is one run, as the writer puts it. */
	return ((gsize) lay->dir_start + 1) * ((gsize) 1 << lay->shift) + (gsize) id * 128;
}

/* The streams. */

static GBytes *
word97_stream (const char *wide_text, const char *narrow_text, gsize len)
{
	g_autoptr (GByteArray) a = g_byte_array_new ();
	g_autofree char *narrow = g_convert (narrow_text, -1, "WINDOWS-1252", "UTF-8", NULL, NULL, NULL);
	g_autoptr (GByteArray) wide = g_byte_array_new ();

	g_assert (narrow != NULL);
	put_utf16 (wide, wide_text);
	zeros (a, len);
	set16 (a->data, 0xa5ec);
	set16 (a->data + 2, 0x00c1);
	set16 (a->data + 0x0a, 0x0200);	/* the table is 1Table */
	memcpy (a->data + 0x800, wide->data, wide->len);
	memcpy (a->data + 0x1000, narrow, strlen (narrow));

	return g_byte_array_free_to_bytes (g_steal_pointer (&a));
}

/* A property blob, then 2 pieces: the UTF-16 text, then the one-byte text. */
static GBytes *
word97_table (GBytes *word, const char *wide_text, const char *narrow_text)
{
	g_autoptr (GByteArray) a = g_byte_array_new ();
	g_autofree char *narrow = g_convert (narrow_text, -1, "WINDOWS-1252", "UTF-8", NULL, NULL, NULL);
	guint32 wide_units = (guint32) g_utf8_strlen (wide_text, -1);
	guint32 narrow_len = (guint32) strlen (narrow);
	guint8 *doc = (guint8 *) g_bytes_get_data (word, NULL);

	g_byte_array_append (a, (const guint8 *) "\001\002\000\252\273", 5);
	g_byte_array_append (a, (const guint8 *) "\002", 1);
	put32 (a, 3 * 4 + 2 * 8);
	put32 (a, 0);
	put32 (a, wide_units);
	put32 (a, wide_units + narrow_len);
	put16 (a, 0);
	put32 (a, 0x800);
	put16 (a, 0);
	put16 (a, 0);
	put32 (a, (0x1000 * 2) | 0x40000000);
	put16 (a, 0);

	/* fcClx 0, lcbClx */
	set32 (doc + 0x1a2, 0);
	set32 (doc + 0x1a6, a->len);

	return g_byte_array_free_to_bytes (g_steal_pointer (&a));
}

/* Word 6 and 95: one run of one-byte text between fcMin and fcMac. */
static GBytes *
word95_stream (const char *text)
{
	g_autoptr (GByteArray) a = g_byte_array_new ();
	gsize len = strlen (text);

	zeros (a, 0x200 + len + 0x40);
	set16 (a->data, 0xa5ec);
	set16 (a->data + 2, 0x0065);
	set32 (a->data + 0x18, 0x200);
	set32 (a->data + 0x1c, (guint32) (0x200 + len));
	memcpy (a->data + 0x200, text, len);

	return g_byte_array_free_to_bytes (g_steal_pointer (&a));
}

static void
biff_record (GByteArray *a, guint16 type, const GByteArray *payload)
{
	put16 (a, type);
	put16 (a, (guint16) payload->len);
	g_byte_array_append (a, payload->data, payload->len);
}

static GBytes *
workbook_stream (void)
{
	g_autoptr (GByteArray) a = g_byte_array_new ();
	g_autoptr (GByteArray) r = g_byte_array_new ();
	guint i;

	put16 (r, 0x0600);
	put16 (r, 0x0005);
	zeros (r, 12);
	biff_record (a, 0x0809, r);

	/* Over 109 FAT sectors' worth of records nothing reads, so the text
	   after them is found through the DIFAT chain. */
	g_byte_array_set_size (r, 0);
	zeros (r, 8000);
	for (i = 0; i < 920; i++) {
		biff_record (a, 0x1234, r);
	}

	g_byte_array_set_size (r, 0);
	put32 (r, 0);
	put16 (r, 0);
	g_byte_array_append (r, (const guint8 *) "\011\000Sheet One", 11);
	biff_record (a, 0x0085, r);

	/* 2 shared strings. The second is UTF-16 and runs on into a CONTINUE,
	   which picks one-byte text for the rest. */
	g_byte_array_set_size (r, 0);
	put32 (r, 2);
	put32 (r, 2);
	put16 (r, 11);
	g_byte_array_append (r, (const guint8 *) "\000xray yankee", 12);
	put16 (r, 12);
	g_byte_array_append (r, (const guint8 *) "\001", 1);
	put_utf16 (r, "Unico");
	biff_record (a, 0x00fc, r);

	g_byte_array_set_size (r, 0);
	g_byte_array_append (r, (const guint8 *) "\000d\351 zulu", 8);
	biff_record (a, 0x003c, r);

	g_byte_array_set_size (r, 0);
	put16 (r, 0);
	put16 (r, 0);
	put16 (r, 15);
	{
		double v = 3.25;
		guint8 b[8];

		memcpy (b, &v, 8);
		g_byte_array_append (r, b, 8);
	}
	biff_record (a, 0x0203, r);

	g_byte_array_set_size (r, 0);
	put16 (r, 1);
	put16 (r, 0);
	put16 (r, 15);
	put32 (r, (4242u << 2) | 2);
	biff_record (a, 0x027e, r);

	g_byte_array_set_size (r, 0);
	biff_record (a, 0x000a, r);

	return g_byte_array_free_to_bytes (g_steal_pointer (&a));
}

/* BIFF5, no container: a LABEL in the code page. */
static GBytes *
bare_workbook (void)
{
	g_autoptr (GByteArray) a = g_byte_array_new ();
	g_autoptr (GByteArray) r = g_byte_array_new ();

	put16 (r, 0x0500);
	put16 (r, 0x0005);
	zeros (r, 4);
	biff_record (a, 0x0809, r);

	g_byte_array_set_size (r, 0);
	put16 (r, 0);
	put16 (r, 0);
	put16 (r, 15);
	put16 (r, 10);
	g_byte_array_append (r, (const guint8 *) "oscar papa", 10);
	biff_record (a, 0x0204, r);

	g_byte_array_set_size (r, 0);
	biff_record (a, 0x000a, r);

	return g_byte_array_free_to_bytes (g_steal_pointer (&a));
}

static void
ppt_record (GByteArray *a, guint16 ver_inst, guint16 type, const guint8 *payload, gsize len)
{
	put16 (a, ver_inst);
	put16 (a, type);
	put32 (a, (guint32) len);
	g_byte_array_append (a, payload, (guint) len);
}

static GBytes *
presentation_stream (void)
{
	g_autoptr (GByteArray) a = g_byte_array_new ();
	g_autoptr (GByteArray) doc = g_byte_array_new ();
	g_autoptr (GByteArray) inner = g_byte_array_new ();
	g_autoptr (GByteArray) chars = g_byte_array_new ();

	put_utf16 (chars, "quebec romeo");
	ppt_record (doc, 0, 0x0fa0, chars->data, chars->len);
	ppt_record (inner, 0, 0x0fa8, (const guint8 *) "sierra tango", 12);
	ppt_record (doc, 0x000f, 0x0ff0, inner->data, inner->len);
	ppt_record (doc, 0, 0x1234, (const guint8 *) "PICTURETEXT", 11);
	ppt_record (a, 0x000f, 0x03e8, doc->data, doc->len);

	return g_byte_array_free_to_bytes (g_steal_pointer (&a));
}

/* A DIB in one color, as the summary stream keeps it. */
static GBytes *
dib (guint32 rgb, int width, int height, int bits, gboolean top_down)
{
	g_autoptr (GByteArray) a = g_byte_array_new ();
	gsize row = (((gsize) width * (gsize) bits / 8) + 3) & ~(gsize) 3;
	int x, y;

	put32 (a, 40);
	put32 (a, (guint32) width);
	put32 (a, (guint32) (top_down ? -height : height));
	put16 (a, 1);
	put16 (a, (guint16) bits);
	put32 (a, 0);
	put32 (a, (guint32) (row * (gsize) height));
	zeros (a, 16);

	for (y = 0; y < height; y++) {
		gsize begin = a->len;

		for (x = 0; x < width; x++) {
			guint8 px[4] = { (guint8) (rgb & 0xff), (guint8) ((rgb >> 8) & 0xff), (guint8) ((rgb >> 16) & 0xff), 0xff };

			g_byte_array_append (a, px, (guint) bits / 8);
		}
		zeros (a, row - (a->len - begin));
	}

	return g_byte_array_free_to_bytes (g_steal_pointer (&a));
}

/* SummaryInformation: a title, then the thumbnail as clipboard data. */
static GBytes *
summary_stream (guint32 clip_format, GBytes *payload)
{
	static const guint8 fmtid[16] = {
		0xe0, 0x85, 0x9f, 0xf2, 0xf9, 0x4f, 0x68, 0x10, 0xab, 0x91, 0x08, 0x00, 0x2b, 0x27, 0xb3, 0xd9
	};
	g_autoptr (GByteArray) a = g_byte_array_new ();
	g_autoptr (GByteArray) props = g_byte_array_new ();
	gsize len = 0, title_at, thumb_at;
	const guint8 *p = g_bytes_get_data (payload, &len);

	title_at = 8 + 2 * 8;
	put16 (props, 0x1e);
	put16 (props, 0);
	put32 (props, 8);
	g_byte_array_append (props, (const guint8 *) "A title", 8);

	thumb_at = title_at + props->len;
	put16 (props, 0x47);
	put16 (props, 0);
	put32 (props, (guint32) (len + 8));
	put32 (props, 0xffffffffu);
	put32 (props, clip_format);
	g_byte_array_append (props, p, (guint) len);
	while (props->len % 4 != 0) {
		zeros (props, 1);
	}

	put16 (a, 0xfffe);
	put16 (a, 0);
	put32 (a, 0x00020006);
	zeros (a, 16);
	put32 (a, 1);
	g_byte_array_append (a, fmtid, 16);
	put32 (a, 0x30);

	put32 (a, (guint32) (title_at + props->len));
	put32 (a, 2);
	put32 (a, 2);
	put32 (a, (guint32) title_at);
	put32 (a, 0x11);
	put32 (a, (guint32) thumb_at);
	g_byte_array_append (a, props->data, props->len);

	return g_byte_array_free_to_bytes (g_steal_pointer (&a));
}

static GBytes *
metafile (void)
{
	/* The 8 byte header CF_METAFILEPICT has, then a bare WMF header. */
	static const guint8 wmf[] = {
		8, 0, 100, 0, 100, 0, 0, 0,
		1, 0, 9, 0, 0, 3, 12, 0, 0, 0, 0, 0, 3, 0, 0, 0, 0, 0, 3, 0, 0, 0, 0, 0
	};

	return g_bytes_new_static (wmf, sizeof wmf);
}

/* The samples. */

typedef struct {
	const char        *file;
	guint32            rgb;		/* the preview; 0 for none */
	int                width, height;
	const char        *word;	/* searched for, in no other file */
	const char *const *present;
	const char        *absent;
} Sample;

static const char *const doc_present[] = { "Alpha bravo charlie \xe2\x82\xacuro", NULL };
static const char *const doc95_present[] = { "golf hotel india", NULL };
static const char *const xls_present[] = { "Sheet One", "xray yankee", "Unicod\xc3\xa9 zulu", "3.25", "4242", NULL };
static const char *const bare_present[] = { "oscar papa", NULL };
static const char *const ppt_present[] = { "quebec romeo", "sierra tango", NULL };
static const char *const wmf_present[] = { "kilo lima", NULL };

static const Sample samples[] = {
	{ "t.doc", 0xd02020, 40, 30, "bravo", doc_present, "EMBEDDEDTEXT" },
	{ "t95.doc", 0, 0, 0, "hotel", doc95_present, NULL },
	{ "t.xls", 0x20d020, 30, 40, "yankee", xls_present, NULL },
	{ "bare.xls", 0, 0, 0, "oscar", bare_present, NULL },
	{ "t.ppt", 0x2020d0, 64, 48, "romeo", ppt_present, "PICTURETEXT" },
	{ "wmf.doc", 0, 0, 0, "lima", wmf_present, NULL },
};

/* The bytes of each sample file, and for t.doc where things are. */
static GBytes *
sample_bytes (const char *file, OleLayout *lay_out)
{
	OleLayout lay = { 9, { 0 }, { 0 }, 0, 0 };
	GBytes *result = NULL;

	if (strcmp (file, "t.doc") == 0) {
		g_autoptr (GBytes) word = word97_stream ("Alpha bravo ", "charlie \xe2\x82\xacuro", 0x2000);
		g_autoptr (GBytes) table = word97_table (word, "Alpha bravo ", "charlie \xe2\x82\xacuro");
		g_autoptr (GBytes) inner = word95_stream ("EMBEDDEDTEXT");
		g_autoptr (GBytes) picture = dib (0xd02020, 40, 30, 24, FALSE);
		g_autoptr (GBytes) summary = summary_stream (8, picture);
		/* The embedded copy comes first in its storage, and must not be
		   taken for the document's own. */
		const OleEntry entries[] = {
			{ "ObjectPool", NULL, -1 },
			{ "WordDocument", inner, 0 },
			{ "\005SummaryInformation", summary, -1 },
			{ "1Table", table, -1 },
			{ "WordDocument", word, -1 },
		};

		result = test_ole2_write (entries, G_N_ELEMENTS (entries), &lay);
	} else if (strcmp (file, "t95.doc") == 0) {
		g_autoptr (GBytes) word = word95_stream ("golf hotel india");
		const OleEntry entries[] = { { "WordDocument", word, -1 } };

		/* Version 4, 4096 byte sectors. */
		lay.shift = 12;
		result = test_ole2_write (entries, G_N_ELEMENTS (entries), &lay);
	} else if (strcmp (file, "t.xls") == 0) {
		g_autoptr (GBytes) book = workbook_stream ();
		g_autoptr (GBytes) picture = dib (0x20d020, 30, 40, 32, FALSE);
		g_autoptr (GBytes) summary = summary_stream (8, picture);
		const OleEntry entries[] = {
			{ "Workbook", book, -1 },
			{ "\005SummaryInformation", summary, -1 },
		};

		result = test_ole2_write (entries, G_N_ELEMENTS (entries), &lay);
	} else if (strcmp (file, "bare.xls") == 0) {
		result = bare_workbook ();
	} else if (strcmp (file, "t.ppt") == 0) {
		g_autoptr (GBytes) show = presentation_stream ();
		g_autoptr (GBytes) picture = dib (0x2020d0, 64, 48, 24, TRUE);
		g_autoptr (GBytes) summary = summary_stream (2, picture);
		const OleEntry entries[] = {
			{ "\005SummaryInformation", summary, -1 },
			{ "PowerPoint Document", show, -1 },
		};

		result = test_ole2_write (entries, G_N_ELEMENTS (entries), &lay);
	} else if (strcmp (file, "wmf.doc") == 0) {
		g_autoptr (GBytes) word = word95_stream ("kilo lima");
		g_autoptr (GBytes) picture = metafile ();
		g_autoptr (GBytes) summary = summary_stream (3, picture);
		const OleEntry entries[] = {
			{ "WordDocument", word, -1 },
			{ "\005SummaryInformation", summary, -1 },
		};

		result = test_ole2_write (entries, G_N_ELEMENTS (entries), &lay);
	}

	g_assert (result != NULL);
	if (lay_out != NULL) {
		*lay_out = lay;
	}
	return result;
}

static char *
write_sample (const char *file)
{
	g_autoptr (GBytes) bytes = sample_bytes (file, NULL);
	char *path = g_build_filename (files_dir, file, NULL);
	gsize len = 0;
	const char *data = g_bytes_get_data (bytes, &len);

	check (g_file_set_contents (path, data, (gssize) len, NULL));
	return path;
}

static char *
mark_path (void)
{
	return g_build_filename (files_dir, "..", "helper-ran", NULL);
}

static gboolean
helper_ran (void)
{
	g_autofree char *mark = mark_path ();

	return g_file_test (mark, G_FILE_TEST_EXISTS);
}

static void
forget_mark (void)
{
	g_autofree char *mark = mark_path ();

	g_unlink (mark);
}

static char *
content_type_of (const char *path)
{
	g_autoptr (GFile) file = g_file_new_for_path (path);
	g_autoptr (GFileInfo) info = g_file_query_info (file, G_FILE_ATTRIBUTE_STANDARD_CONTENT_TYPE,
							G_FILE_QUERY_INFO_NONE, NULL, NULL);

	return info != NULL ? g_strdup (g_file_info_get_content_type (info)) : NULL;
}

static gboolean
about (int a, int b)
{
	return ABS (a - b) <= 8;
}

static void
test_sample (NemoDesktopThumbnailFactory *factory, const Sample *s)
{
	g_autofree char *path = write_sample (s->file);
	g_autofree char *uri = g_filename_to_uri (path, NULL, NULL);
	g_autofree char *type = content_type_of (path);
	g_autofree char *text = NULL;
	g_autoptr (GFile) file = g_file_new_for_path (path);
	GError *error = NULL;
	guint i;

	g_print ("%s (%s)\n", s->file, type != NULL ? type : "no type");

	check (nemo_office_type_ok (type));
	check (nemo_desktop_thumbnail_factory_can_make (factory, uri, type));

	/* With no bitmap, or a metafile, the reader gives nothing, and then the
	   stand-in thumbnailer for these types gets its turn. It draws nothing. */
	if (s->rgb == 0) {
		GdkPixbuf *none = nemo_office_thumbnail_uri (uri, 128, NULL);

		check (none == NULL);
		g_clear_object (&none);
		none = nemo_desktop_thumbnail_factory_generate_thumbnail_at_size (factory, uri, type, 128, NULL);
		check (none == NULL);
		g_clear_object (&none);
		check (helper_ran ());
		forget_mark ();
	} else {
		GdkPixbuf *pixbuf = nemo_desktop_thumbnail_factory_generate_thumbnail_at_size (factory, uri, type, 128, NULL);

		check (pixbuf != NULL);
		if (pixbuf != NULL) {
			const guchar *px = gdk_pixbuf_read_pixels (pixbuf);
			int stride = gdk_pixbuf_get_rowstride (pixbuf);
			int w = gdk_pixbuf_get_width (pixbuf), h = gdk_pixbuf_get_height (pixbuf);
			int longer = MAX (s->width, s->height);
			const guchar *mid = px + (h / 2) * stride + (w / 2) * gdk_pixbuf_get_n_channels (pixbuf);

			/* Fitted to the size asked for, up as well as down. */
			check (w == MAX ((int) (s->width * 128.0 / longer + 0.5), 1) &&
			       h == MAX ((int) (s->height * 128.0 / longer + 0.5), 1));
			check (about (mid[0], (int) ((s->rgb >> 16) & 0xff)) && about (mid[1], (int) ((s->rgb >> 8) & 0xff)) &&
			       about (mid[2], (int) (s->rgb & 0xff)));
			g_object_unref (pixbuf);
		}
	}

	text = nemo_office_text_file (file, 1024 * 1024, NULL, &error);
	check (text != NULL && error == NULL);
	g_clear_error (&error);
	if (text != NULL) {
		for (i = 0; s->present[i] != NULL; i++) {
			if (strstr (text, s->present[i]) == NULL) {
				g_printerr ("%s: no \"%s\" in \"%s\"\n", s->file, s->present[i], text);
				failures++;
			}
		}
		check (s->absent == NULL || strstr (text, s->absent) == NULL);
	}

	check (!helper_ran ());
}

static gboolean search_done;
static GList *found;

static void
hits_added (G_GNUC_UNUSED NemoSearchEngine *engine, GList *hits, G_GNUC_UNUSED gpointer data)
{
	GList *l;

	for (l = hits; l != NULL; l = l->next) {
		FileSearchResult *result = l->data;

		found = g_list_prepend (found, g_path_get_basename (result->uri));
		file_search_result_free (result);
	}
}

static void
finished (G_GNUC_UNUSED NemoSearchEngine *engine, G_GNUC_UNUSED gpointer data)
{
	search_done = TRUE;
}

static void
search_for (const Sample *s, guint want, gboolean helper_runs)
{
	NemoSearchEngine *engine = nemo_search_engine_advanced_new ();
	NemoQuery *query = nemo_query_new ();
	g_autofree char *uri = g_filename_to_uri (files_dir, NULL, NULL);
	gint64 deadline = g_get_monotonic_time () + 30 * G_USEC_PER_SEC;

	g_signal_connect (engine, "hits-added", G_CALLBACK (hits_added), NULL);
	g_signal_connect (engine, "finished", G_CALLBACK (finished), NULL);

	nemo_query_set_location (query, uri);
	nemo_query_set_content_pattern (query, s->word);
	nemo_search_engine_set_query (engine, query);
	g_object_unref (query);

	search_done = FALSE;
	found = NULL;
	nemo_search_engine_start (engine);

	while (!search_done && g_get_monotonic_time () < deadline) {
		g_main_context_iteration (NULL, FALSE);
		g_usleep (10000);
	}

	g_print ("search for %s: %u found\n", s->word, g_list_length (found));
	check (search_done);
	check (g_list_length (found) == want && (want == 0 || g_strcmp0 (found->data, s->file) == 0));

	g_list_free_full (found, g_free);
	found = NULL;
	g_object_unref (engine);

	check (helper_ran () == helper_runs);
}

/* Not a compound file at all, with the name of one. The app can't read it,
   so the stand-in thumbnailer and search helper installed for these types get
   their turn. Neither gives anything. */
static const char junk[] = "\001\002\377\000 nothingreadsme \000\376\375";

static void
test_unreadable (NemoDesktopThumbnailFactory *factory, const char *file)
{
	g_autofree char *path = g_build_filename (files_dir, file, NULL);
	g_autofree char *uri = g_filename_to_uri (path, NULL, NULL);
	g_autofree char *type = NULL;
	const Sample none = { file, 0, 0, 0, "nothingreadsme", NULL, NULL };
	GdkPixbuf *pixbuf;

	/* Not text either, which Windows would search as text when nothing
	   there has registered the extension. */
	check (g_file_set_contents (path, junk, sizeof junk, NULL));
	type = content_type_of (path);
	g_print ("%s (%s), not a compound file\n", file, type != NULL ? type : "no type");
	check (nemo_office_type_ok (type));

	pixbuf = nemo_office_thumbnail_uri (uri, 128, NULL);
	check (pixbuf == NULL);
	g_clear_object (&pixbuf);
	pixbuf = nemo_desktop_thumbnail_factory_generate_thumbnail_at_size (factory, uri, type, 128, NULL);
	check (pixbuf == NULL);
	g_clear_object (&pixbuf);
	check (helper_ran ());
	forget_mark ();

	search_for (&none, 0, TRUE);
	forget_mark ();
	check (g_unlink (path) == 0);
}

static void
read_both (const guint8 *bytes, gsize len, gsize max_len, gsize *text_len)
{
	g_autoptr (GInputStream) in = g_memory_input_stream_new_from_data (bytes, (gssize) len, NULL);
	g_autoptr (GInputStream) again = g_memory_input_stream_new_from_data (bytes, (gssize) len, NULL);
	GdkPixbuf *pixbuf = nemo_office_thumbnail (in, 128, NULL);
	char *text = nemo_office_text (again, max_len, NULL, NULL);

	if (text_len != NULL) {
		*text_len = text != NULL ? strlen (text) : 0;
	}
	check (text == NULL || strlen (text) <= max_len);
	g_clear_object (&pixbuf);
	g_free (text);
}

/* Cut short and bit-flipped. Nothing may crash. */
static void
test_damaged (const char *file)
{
	g_autoptr (GBytes) whole = sample_bytes (file, NULL);
	gsize len = 0, at;
	const guint8 *bytes = g_bytes_get_data (whole, &len);
	g_autofree guint8 *flipped = g_memdup2 (bytes, len);

	g_print ("damaged %s\n", file);

	for (at = 0; at < len; at += 61) {
		read_both (bytes, at, 1024, NULL);
	}

	for (at = 0; at < len; at += 5) {
		flipped[at] ^= 0x5a;
		read_both (flipped, len, 1024, NULL);
		flipped[at] ^= 0x5a;
	}
}

/* Lengths and links that lie: each has to answer at once, with no more
   text than asked for. */
static void
test_lies (void)
{
	OleLayout lay;
	g_autoptr (GBytes) whole = sample_bytes ("t.doc", &lay);
	gsize len = 0, text_len = 0;
	const guint8 *bytes = g_bytes_get_data (whole, &len);
	g_autofree guint8 *copy = g_memdup2 (bytes, len);
	gint64 began = g_get_monotonic_time ();
	guint32 word_start = lay.start[4];

	/* The document stream's first sector links back to itself, and it
	   claims to be nearly 4 GB. */
	set32 (copy + fat_entry_at (&lay, word_start), word_start);
	set32 (copy + dir_entry_at (&lay, 5) + 0x78, 0xfffffff0u);
	read_both (copy, len, 4096, &text_len);
	memcpy (copy, bytes, len);

	/* A sibling link loop through the top storage's children, and the
	   storage's child pointing back at the top. */
	set32 (copy + dir_entry_at (&lay, 3) + 0x48, 2);
	set32 (copy + dir_entry_at (&lay, 2) + 0x44, 3);
	set32 (copy + dir_entry_at (&lay, 1) + 0x4c, 0);
	read_both (copy, len, 4096, NULL);
	memcpy (copy, bytes, len);

	/* Huge FAT, directory and mini FAT counts, and a sector size that
	   isn't one. */
	set32 (copy + 0x2c, 0x7fffffffu);
	set32 (copy + 0x40, 0x7fffffffu);
	read_both (copy, len, 4096, &text_len);
	check (text_len > 0);
	set16 (copy + 0x1e, 30);
	read_both (copy, len, 4096, &text_len);
	check (text_len == 0);
	memcpy (copy, bytes, len);

	check (g_get_monotonic_time () - began < 10 * G_USEC_PER_SEC);
}

/* Many pieces all pointing at the same text must stop at the cap rather than
   repeat it for every piece. Uncapped, this is over a GB and some seconds. */
static void
test_piece_flood (void)
{
	g_autoptr (GByteArray) table = g_byte_array_new ();
	g_autoptr (GBytes) word = word97_stream ("Alpha bravo ", "x", 0x2000);
	guint8 *doc = (guint8 *) g_bytes_get_data (word, NULL);
	guint32 n = 300000, i;
	gint64 began = g_get_monotonic_time ();
	char *text;

	g_byte_array_append (table, (const guint8 *) "\002", 1);
	put32 (table, (n + 1) * 4 + n * 8);
	for (i = 0; i <= n; i++) {
		put32 (table, i * 0x1000);
	}
	for (i = 0; i < n; i++) {
		put16 (table, 0);
		put32 (table, 0);
		put16 (table, 0);
	}
	set32 (doc + 0x1a6, table->len);

	text = nemo_office_word_text (doc, g_bytes_get_size (word), table->data, table->len, 64 * 1024);
	check (text != NULL && strlen (text) <= 64 * 1024);
	g_free (text);

	check (g_get_monotonic_time () - began < 2 * G_USEC_PER_SEC);
}

/* A thumbnailer and a search helper for every type here, ahead of anything
   else, that only leave a mark. */
static void
install_fake_helpers (const char *home, const char *self)
{
	g_autofree char *thumbs = g_build_filename (home, "thumbnailers", NULL);
	g_autofree char *helpers = g_build_filename (home, NEMO_APP_SLUG, "search-helpers", NULL);
	g_autofree char *absolute = g_canonicalize_filename (self, NULL);
	g_autofree char *exe = NULL;
	g_autofree char *thumb_entry = NULL;
	g_autofree char *helper_entry = NULL;
	g_autofree char *thumb_path = NULL;
	g_autofree char *helper_path = NULL;
	const char *mimes = "application/msword;application/vnd.ms-excel;application/vnd.ms-powerpoint;";

#ifdef G_OS_WIN32
	if (!g_str_has_suffix (absolute, ".exe")) {
		exe = g_strconcat (absolute, ".exe", NULL);
	} else {
		exe = g_strdup (absolute);
	}
#else
	exe = g_strdup (absolute);
#endif

	g_mkdir_with_parents (thumbs, 0755);
	g_mkdir_with_parents (helpers, 0755);

	thumb_entry = g_strdup_printf ("[Thumbnailer Entry]\nTryExec=%s\nExec=\"%s\" --ran %%i %%o\nMimeType=%s\n",
				       exe, exe, mimes);
	helper_entry = g_strdup_printf ("[Nemo Search Helper]\nTryExec=%s;\nExec=\"%s\" --ran %%s\nMimeType=%s\nPriority=500\n",
					exe, exe, mimes);
	thumb_path = g_build_filename (thumbs, "fake.thumbnailer", NULL);
	helper_path = g_build_filename (helpers, "fake.nemo_search_helper", NULL);
	check (g_file_set_contents (thumb_path, thumb_entry, -1, NULL));
	check (g_file_set_contents (helper_path, helper_entry, -1, NULL));
}

#ifndef G_OS_WIN32
/* Typing by name needs the shared mime database, hidden along with the box's
   own thumbnailers. */
static void
link_mime_database (const char *home)
{
	const char *const dirs[] = { "/usr/local/share", "/usr/share", NULL };
	g_autoptr (GFile) link = g_file_new_build_filename (home, "mime", NULL);
	guint i;

	for (i = 0; dirs[i] != NULL; i++) {
		g_autofree char *probe = g_build_filename (dirs[i], "mime", "application", "msword.xml", NULL);
		g_autofree char *from = g_build_filename (dirs[i], "mime", NULL);

		if (g_file_test (probe, G_FILE_TEST_EXISTS) && g_file_make_symbolic_link (link, from, NULL, NULL)) {
			return;
		}
	}

	g_printerr ("no shared mime database found\n");
}
#endif

static gboolean
has_bmp_loader (void)
{
	GdkPixbufLoader *loader = gdk_pixbuf_loader_new_with_type ("bmp", NULL);

	if (loader == NULL) {
		return FALSE;
	}
	gdk_pixbuf_loader_close (loader, NULL);
	g_object_unref (loader);
	return TRUE;
}

int
main (int argc, char *argv[])
{
	g_autofree char *home = NULL;
	g_autofree char *mark = NULL;
	NemoDesktopThumbnailFactory *factory;
	guint i;

	/* The stand-in thumbnailer and search helper. */
	if (argc > 1 && g_strcmp0 (argv[1], "--ran") == 0) {
		const char *at = g_getenv (MARK_ENV);

		if (at != NULL) {
			g_file_set_contents (at, "ran\n", -1, NULL);
		}
		return 0;
	}

	home = test_scratch_config_home ("nemo-office-ole-test-XXXXXX");
	g_setenv ("XDG_DATA_HOME", home, TRUE);
	g_setenv ("LOCALAPPDATA", home, TRUE);
#ifndef G_OS_WIN32
	link_mime_database (home);
#endif
	g_setenv ("XDG_DATA_DIRS", home, TRUE);
	g_setenv ("XDG_CACHE_HOME", home, TRUE);

	files_dir = g_build_filename (home, "files", NULL);
	g_mkdir_with_parents (files_dir, 0755);
	mark = mark_path ();
	g_setenv (MARK_ENV, mark, TRUE);

	if (!gtk_init_check (&argc, &argv)) {
		g_print ("SKIP: no display\n");
		return 77;
	}
	if (!has_bmp_loader ()) {
		g_print ("SKIP: no bmp loader\n");
		return 77;
	}
	nemo_global_preferences_init ();

	install_fake_helpers (home, argv[0]);
	factory = nemo_desktop_thumbnail_factory_new (NEMO_DESKTOP_THUMBNAIL_SIZE_NORMAL);

	for (i = 0; i < G_N_ELEMENTS (samples); i++) {
		test_sample (factory, &samples[i]);
	}
	for (i = 0; i < G_N_ELEMENTS (samples); i++) {
		search_for (&samples[i], 1, FALSE);
	}

	test_damaged ("t.doc");
	test_damaged ("t95.doc");
	test_damaged ("t.ppt");
	test_damaged ("bare.xls");
	test_lies ();
	test_piece_flood ();
	test_unreadable (factory, "junk.doc");
	test_unreadable (factory, "junk.xls");
	test_unreadable (factory, "junk.ppt");

	g_object_unref (factory);
	g_free (files_dir);

	if (failures > 0) {
		g_printerr ("%d failure(s)\n", failures);
		return 1;
	}

	g_print ("OK\n");
	return 0;
}
