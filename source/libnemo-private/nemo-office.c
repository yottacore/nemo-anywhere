/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */

/* nemo-office.c - thumbnails and text of zip-based office files.

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

#include <config.h>

#include "nemo-office.h"
#include "nemo-file-utilities.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>

#include <archive.h>
#include <archive_entry.h>

#define READ_CHUNK       (64 * 1024)
#define MAX_ENTRIES      20000
#define MAX_PATH_LEN     1024
#define MAX_SEGMENTS     64
#define MAX_TAGS         100000
#define MAX_INDEX_BYTES  (1024 * 1024)		/* .rels, container.xml, .opf */
#define MAX_IMAGE_BYTES  (16 * 1024 * 1024)
#define MAX_TEXT_READ    (128 * 1024 * 1024)	/* unpacked, all members together */
#define MAX_SIDE         16384
#define MAX_PIXELS       (40 * 1000 * 1000)
#define MAX_JPEG_MARKERS 1000

gboolean
nemo_office_type_ok (const char *content_type)
{
	g_autofree char *given = NULL;
	g_autofree char *mime = NULL;

	if (content_type == NULL) {
		return FALSE;
	}

	given = nemo_content_type_get_mime_type (content_type);
	if (given == NULL) {
		return FALSE;
	}
	mime = g_ascii_strdown (given, -1);

	/* Flat OpenDocument is plain xml, and xlsb keeps no text as xml. */
	if (g_str_has_suffix (mime, "-flat-xml") || strstr (mime, ".binary.") != NULL) {
		return FALSE;
	}

	return g_str_has_prefix (mime, "application/vnd.oasis.opendocument.") ||
	       g_str_has_prefix (mime, "application/vnd.openxmlformats-officedocument.") ||
	       (g_str_has_prefix (mime, "application/vnd.ms-") && g_str_has_suffix (mime, ".macroenabled.12")) ||
	       strcmp (mime, "application/epub+zip") == 0;
}

/* The stream behind libarchive. Each pass over the zip starts again from 0. */
typedef struct {
	GInputStream *in;
	GCancellable *cancellable;
	guint8       *buf;
} Source;

static la_ssize_t
source_read (struct archive *a, void *data, const void **out)
{
	Source *src = data;
	gssize n = g_input_stream_read (src->in, src->buf, READ_CHUNK, src->cancellable, NULL);

	if (n < 0) {
		archive_set_error (a, EIO, "read failed");
		return -1;
	}

	*out = src->buf;
	return n;
}

static la_int64_t
source_seek (struct archive *a, void *data, la_int64_t offset, int whence)
{
	Source *src = data;
	GSeekType type = whence == SEEK_SET ? G_SEEK_SET : whence == SEEK_CUR ? G_SEEK_CUR : G_SEEK_END;

	if (!g_seekable_seek (G_SEEKABLE (src->in), offset, type, src->cancellable, NULL)) {
		archive_set_error (a, EIO, "seek failed");
		return ARCHIVE_FATAL;
	}

	return g_seekable_tell (G_SEEKABLE (src->in));
}

static la_int64_t
source_skip (struct archive *a, void *data, la_int64_t request)
{
	Source *src = data;
	goffset before = g_seekable_tell (G_SEEKABLE (src->in));

	if (source_seek (a, data, request, SEEK_CUR) < 0) {
		return 0;
	}

	return g_seekable_tell (G_SEEKABLE (src->in)) - before;
}

static gboolean
source_init (Source *src, GInputStream *in, GCancellable *cancellable)
{
	if (!G_IS_SEEKABLE (in) || !g_seekable_can_seek (G_SEEKABLE (in))) {
		return FALSE;
	}

	src->in = in;
	src->cancellable = cancellable;
	src->buf = g_malloc (READ_CHUNK);

	return TRUE;
}

static struct archive *
zip_open (Source *src)
{
	struct archive *a;

	if (g_cancellable_is_cancelled (src->cancellable) ||
	    !g_seekable_seek (G_SEEKABLE (src->in), 0, G_SEEK_SET, src->cancellable, NULL)) {
		return NULL;
	}

	/* Seekable only: it reads the central directory, which is what Office
	   writes, where the streaming reader guesses from local headers. */
	a = archive_read_new ();
	archive_read_support_format_zip_seekable (a);
	archive_read_set_read_callback (a, source_read);
	archive_read_set_seek_callback (a, source_seek);
	archive_read_set_skip_callback (a, source_skip);
	archive_read_set_callback_data (a, src);

	if (archive_read_open1 (a) != ARCHIVE_OK) {
		archive_read_free (a);
		return NULL;
	}

	return a;
}

static const char *
entry_name (struct archive_entry *entry)
{
	const char *name = archive_entry_pathname_utf8 (entry);

	if (name == NULL) {
		name = archive_entry_pathname (entry);
	}
	if (name != NULL && name[0] == '/') {
		name++;
	}

	return name;
}

/* The rest of the current member, or NULL when it doesn't read or is over cap.
   Its stated size is never looked at. */
static GBytes *
read_member (struct archive *a, gsize cap, GCancellable *cancellable)
{
	GByteArray *bytes = g_byte_array_new ();
	guint8 chunk[16 * 1024];

	for (;;) {
		la_ssize_t n = archive_read_data (a, chunk, sizeof chunk);

		if (n == 0) {
			break;
		}
		if (n < 0 || (gsize) n > cap - bytes->len || g_cancellable_is_cancelled (cancellable)) {
			g_byte_array_unref (bytes);
			return NULL;
		}
		g_byte_array_append (bytes, chunk, (guint) n);
	}

	return g_byte_array_free_to_bytes (bytes);
}

typedef struct {
	const char *name;	/* NULL for an unused slot */
	gsize       cap;
	GBytes     *got;
} Want;

/* One pass, filling in each member asked for that is there. Names compare
   as OOXML part names do, ignoring ASCII case. FALSE when it isn't a zip. */
static gboolean
zip_get (Source *src, Want *wants, guint n_wants)
{
	struct archive *a = zip_open (src);
	struct archive_entry *entry;
	guint seen = 0, left = 0, i;

	if (a == NULL) {
		return FALSE;
	}

	for (i = 0; i < n_wants; i++) {
		if (wants[i].name != NULL) {
			left++;
		}
	}

	while (left > 0 && seen++ < MAX_ENTRIES && archive_read_next_header (a, &entry) == ARCHIVE_OK) {
		const char *name = entry_name (entry);

		if (name == NULL || archive_entry_filetype (entry) != AE_IFREG) {
			continue;
		}

		for (i = 0; i < n_wants; i++) {
			if (wants[i].name != NULL && wants[i].got == NULL &&
			    g_ascii_strcasecmp (name, wants[i].name) == 0) {
				wants[i].got = read_member (a, wants[i].cap, src->cancellable);
				left--;
				break;
			}
		}
	}

	archive_read_free (a);
	return TRUE;
}

static GBytes *
zip_get_one (Source *src, const char *name, gsize cap)
{
	Want want = { name, cap, NULL };

	zip_get (src, &want, 1);
	return want.got;
}

/* @href read from a member in @base_dir, as a member name. NULL when it
   names something outside the zip, climbs out of it, or runs too long. */
static char *
resolve_member (const char *base_dir, const char *href)
{
	g_autofree char *cut = NULL;
	g_autofree char *decoded = NULL;
	g_autofree char *joined = NULL;
	g_auto (GStrv) parts = NULL;
	const char *kept[MAX_SEGMENTS];
	guint n = 0, i;
	const char *colon, *slash;

	if (href == NULL || href[0] == '\0' || strlen (href) > MAX_PATH_LEN) {
		return NULL;
	}

	/* A scheme or a drive letter. */
	colon = strchr (href, ':');
	slash = strchr (href, '/');
	if (colon != NULL && (slash == NULL || colon < slash)) {
		return NULL;
	}

	cut = g_strndup (href, strcspn (href, "#?"));
	decoded = g_uri_unescape_string (cut, NULL);
	if (decoded == NULL || strchr (decoded, '\\') != NULL) {
		return NULL;
	}

	if (decoded[0] == '/') {
		joined = g_strdup (decoded);
	} else {
		joined = g_strconcat (base_dir, "/", decoded, NULL);
	}

	parts = g_strsplit (joined, "/", -1);
	for (i = 0; parts[i] != NULL; i++) {
		if (parts[i][0] == '\0' || strcmp (parts[i], ".") == 0) {
			continue;
		}
		if (strcmp (parts[i], "..") == 0) {
			if (n == 0) {
				return NULL;
			}
			n--;
			continue;
		}
		if (n == MAX_SEGMENTS) {
			return NULL;
		}
		kept[n++] = parts[i];
	}

	if (n == 0) {
		return NULL;
	}

	{
		GString *out = g_string_new (kept[0]);

		for (i = 1; i < n; i++) {
			g_string_append_c (out, '/');
			g_string_append (out, kept[i]);
		}

		if (out->len > MAX_PATH_LEN) {
			g_string_free (out, TRUE);
			return NULL;
		}

		return g_string_free (out, FALSE);
	}
}

/* Tags, read only as far as the index files need: the name and the quoted
   attributes of a start or empty tag. Namespace prefixes are ignored. */

static gboolean
is_xml_space (char c)
{
	return c == ' ' || c == '\t' || c == '\n' || c == '\r';
}

static gboolean
local_name_is (const char *name, gsize len, const char *want)
{
	const char *colon = memchr (name, ':', len);

	if (colon != NULL) {
		len -= (gsize) (colon + 1 - name);
		name = colon + 1;
	}

	return len == strlen (want) && g_ascii_strncasecmp (name, want, len) == 0;
}

static gsize
name_len (const char *tag, gsize len)
{
	gsize i = 0;

	while (i < len && !is_xml_space (tag[i]) && tag[i] != '/' && tag[i] != '=') {
		i++;
	}

	return i;
}

static gboolean
tag_is (const char *tag, gsize len, const char *want)
{
	return local_name_is (tag, name_len (tag, len), want);
}

/* The five entities xml has, which an href with an & in it uses. */
static char *
unescape_attr (const char *value, gsize len)
{
	static const struct {
		const char *text;
		char        c;
	} entities[] = {
		{ "&lt;", '<' }, { "&gt;", '>' }, { "&quot;", '"' }, { "&apos;", '\'' }, { "&amp;", '&' },
	};
	GString *out = g_string_sized_new (len);
	gsize i = 0, e;

	while (i < len) {
		if (value[i] == '&') {
			for (e = 0; e < G_N_ELEMENTS (entities); e++) {
				gsize n = strlen (entities[e].text);

				if (len - i >= n && memcmp (value + i, entities[e].text, n) == 0) {
					g_string_append_c (out, entities[e].c);
					i += n;
					break;
				}
			}
			if (e < G_N_ELEMENTS (entities)) {
				continue;
			}
		}
		g_string_append_c (out, value[i++]);
	}

	return g_string_free (out, FALSE);
}

static char *
tag_attr (const char *tag, gsize len, const char *want)
{
	gsize i = name_len (tag, len);

	while (i < len) {
		gsize start, nlen;
		char quote;

		while (i < len && is_xml_space (tag[i])) {
			i++;
		}
		start = i;
		nlen = name_len (tag + i, len - i);
		i += nlen;
		while (i < len && is_xml_space (tag[i])) {
			i++;
		}
		if (nlen == 0 || i >= len || tag[i] != '=') {
			return NULL;
		}
		i++;
		while (i < len && is_xml_space (tag[i])) {
			i++;
		}
		if (i >= len || (tag[i] != '"' && tag[i] != '\'')) {
			return NULL;
		}
		quote = tag[i++];

		{
			const char *end = memchr (tag + i, quote, len - i);

			if (end == NULL) {
				return NULL;
			}
			if (local_name_is (tag + start, nlen, want)) {
				return unescape_attr (tag + i, (gsize) (end - (tag + i)));
			}
			i = (gsize) (end - tag) + 1;
		}
	}

	return NULL;
}

typedef gboolean (*TagFunc) (const char *tag, gsize len, gpointer data);

/* Each start or empty tag, the text between < and >, until @each answers
   TRUE. */
static void
each_tag (GBytes *xml, TagFunc each, gpointer data)
{
	gsize len = 0;
	const char *text = g_bytes_get_data (xml, &len);
	gsize i = 0;
	guint count = 0;

	while (i < len && count++ < MAX_TAGS) {
		const char *open = memchr (text + i, '<', len - i);
		const char *close;
		gsize start;

		if (open == NULL) {
			return;
		}
		start = (gsize) (open - text) + 1;
		close = memchr (text + start, '>', len - start);
		if (close == NULL) {
			return;
		}

		if (text + start < close && text[start] != '/' && text[start] != '?' && text[start] != '!' &&
		    each (text + start, (gsize) (close - (text + start)), data)) {
			return;
		}

		i = (gsize) (close - text) + 1;
	}
}

/* Picture headers, read before the decoder sees anything, so a huge one is
   refused rather than allocated. */

static gboolean
png_size (const guint8 *p, gsize len, guint32 *w, guint32 *h)
{
	static const guint8 magic[] = { 0x89, 'P', 'N', 'G', '\r', '\n', 0x1a, '\n' };

	if (len < 24 || memcmp (p, magic, sizeof magic) != 0 || memcmp (p + 12, "IHDR", 4) != 0) {
		return FALSE;
	}

	*w = ((guint32) p[16] << 24) | ((guint32) p[17] << 16) | ((guint32) p[18] << 8) | p[19];
	*h = ((guint32) p[20] << 24) | ((guint32) p[21] << 16) | ((guint32) p[22] << 8) | p[23];
	return TRUE;
}

static gboolean
jpeg_size (const guint8 *p, gsize len, guint32 *w, guint32 *h)
{
	gsize i = 2;
	guint markers = 0;

	if (len < 4 || p[0] != 0xff || p[1] != 0xd8) {
		return FALSE;
	}

	while (i + 4 <= len && markers++ < MAX_JPEG_MARKERS) {
		guint8 m;
		gsize seg;

		if (p[i] != 0xff) {
			return FALSE;
		}
		while (i < len && p[i] == 0xff) {
			i++;
		}
		if (i >= len) {
			return FALSE;
		}
		m = p[i++];

		/* No length on these. */
		if (m == 0x01 || (m >= 0xd0 && m <= 0xd8)) {
			continue;
		}
		if (i + 2 > len) {
			return FALSE;
		}
		seg = ((gsize) p[i] << 8) | p[i + 1];
		if (seg < 2 || seg > len - i) {
			return FALSE;
		}

		if (m >= 0xc0 && m <= 0xcf && m != 0xc4 && m != 0xc8 && m != 0xcc) {
			if (seg < 7) {
				return FALSE;
			}
			*h = ((guint32) p[i + 3] << 8) | p[i + 4];
			*w = ((guint32) p[i + 5] << 8) | p[i + 6];
			return TRUE;
		}

		i += seg;
	}

	return FALSE;
}

typedef struct {
	int      size;
	gboolean refused;
} Fit;

static void
fit_prepared (GdkPixbufLoader *loader, int width, int height, gpointer data)
{
	Fit *fit = data;
	double scale;

	/* Stated again by the decoder, after the header check above. 0 by 0
	   makes the loader give up before it allocates. */
	if (width <= 0 || height <= 0 || width > MAX_SIDE || height > MAX_SIDE ||
	    (gint64) width * height > MAX_PIXELS) {
		fit->refused = TRUE;
		gdk_pixbuf_loader_set_size (loader, 0, 0);
		return;
	}

	scale = (double) fit->size / MAX (width, height);
	gdk_pixbuf_loader_set_size (loader,
				    MAX ((int) (width * scale + 0.5), 1),
				    MAX ((int) (height * scale + 0.5), 1));
}

static GdkPixbuf *
decode_image (GBytes *bytes, int size, GCancellable *cancellable)
{
	gsize len = 0, done = 0;
	const guint8 *p = g_bytes_get_data (bytes, &len);
	const char *type;
	guint32 w = 0, h = 0;
	GdkPixbufLoader *loader;
	GdkPixbuf *pixbuf = NULL;
	gboolean ok = TRUE;
	Fit fit = { size, FALSE };

	if (png_size (p, len, &w, &h)) {
		type = "png";
	} else if (jpeg_size (p, len, &w, &h)) {
		type = "jpeg";
	} else {
		return NULL;
	}

	if (w == 0 || h == 0 || w > MAX_SIDE || h > MAX_SIDE || (guint64) w * h > MAX_PIXELS) {
		return NULL;
	}

	loader = gdk_pixbuf_loader_new_with_type (type, NULL);
	if (loader == NULL) {
		return NULL;
	}
	g_signal_connect (loader, "size-prepared", G_CALLBACK (fit_prepared), &fit);

	while (ok && done < len && !fit.refused) {
		gsize n = MIN (len - done, (gsize) READ_CHUNK);

		ok = !g_cancellable_is_cancelled (cancellable) &&
		     gdk_pixbuf_loader_write (loader, p + done, n, NULL);
		done += n;
	}

	if (!gdk_pixbuf_loader_close (loader, NULL)) {
		ok = FALSE;
	}

	if (ok && !fit.refused && gdk_pixbuf_loader_get_pixbuf (loader) != NULL) {
		pixbuf = g_object_ref (gdk_pixbuf_loader_get_pixbuf (loader));
	}

	g_object_unref (loader);
	return pixbuf;
}

typedef struct {
	char *found;
} RelsFind;

static gboolean
rels_thumbnail (const char *tag, gsize len, gpointer data)
{
	RelsFind *find = data;
	g_autofree char *type = NULL;

	if (!tag_is (tag, len, "Relationship")) {
		return FALSE;
	}

	type = tag_attr (tag, len, "Type");
	if (type == NULL || !g_str_has_suffix (type, "/metadata/thumbnail")) {
		return FALSE;
	}

	find->found = tag_attr (tag, len, "Target");
	return TRUE;
}

static gboolean
container_rootfile (const char *tag, gsize len, gpointer data)
{
	RelsFind *find = data;

	if (!tag_is (tag, len, "rootfile")) {
		return FALSE;
	}

	find->found = tag_attr (tag, len, "full-path");
	return find->found != NULL;
}

typedef struct {
	char *by_property;	/* EPUB 3 */
	char *cover_id;		/* EPUB 2, from <meta name="cover"> */
} OpfCover;

static gboolean
opf_cover_property (const char *tag, gsize len, gpointer data)
{
	OpfCover *cover = data;

	if (tag_is (tag, len, "item")) {
		g_autofree char *props = tag_attr (tag, len, "properties");
		g_auto (GStrv) words = NULL;
		guint i;

		if (props == NULL) {
			return FALSE;
		}
		words = g_strsplit_set (props, " \t\r\n", -1);
		for (i = 0; words[i] != NULL; i++) {
			if (strcmp (words[i], "cover-image") == 0) {
				cover->by_property = tag_attr (tag, len, "href");
				return cover->by_property != NULL;
			}
		}
	} else if (tag_is (tag, len, "meta") && cover->cover_id == NULL) {
		g_autofree char *name = tag_attr (tag, len, "name");

		if (g_strcmp0 (name, "cover") == 0) {
			cover->cover_id = tag_attr (tag, len, "content");
		}
	}

	return FALSE;
}

typedef struct {
	const char *id;
	char       *href;
} OpfItem;

static gboolean
opf_item_by_id (const char *tag, gsize len, gpointer data)
{
	OpfItem *item = data;
	g_autofree char *id = NULL;

	if (!tag_is (tag, len, "item")) {
		return FALSE;
	}

	id = tag_attr (tag, len, "id");
	if (g_strcmp0 (id, item->id) != 0) {
		return FALSE;
	}

	item->href = tag_attr (tag, len, "href");
	return TRUE;
}

static char *
epub_cover_name (Source *src, GBytes *container)
{
	RelsFind root = { NULL };
	OpfCover cover = { NULL, NULL };
	g_autofree char *opf_dir = NULL;
	g_autoptr (GBytes) opf = NULL;
	char *name = NULL;

	each_tag (container, container_rootfile, &root);
	if (root.found == NULL || strlen (root.found) > MAX_PATH_LEN) {
		g_free (root.found);
		return NULL;
	}

	opf = zip_get_one (src, root.found, MAX_INDEX_BYTES);
	opf_dir = g_path_get_dirname (root.found);
	g_free (root.found);
	if (opf == NULL) {
		return NULL;
	}

	each_tag (opf, opf_cover_property, &cover);

	if (cover.by_property == NULL && cover.cover_id != NULL) {
		OpfItem item = { cover.cover_id, NULL };

		each_tag (opf, opf_item_by_id, &item);
		cover.by_property = item.href;
	}

	if (cover.by_property != NULL) {
		name = resolve_member (strcmp (opf_dir, ".") == 0 ? "" : opf_dir, cover.by_property);
	}

	g_free (cover.by_property);
	g_free (cover.cover_id);
	return name;
}

GdkPixbuf *
nemo_office_thumbnail (GInputStream *stream, int size, GCancellable *cancellable)
{
	Source src = { NULL, NULL, NULL };
	Want wants[] = {
		{ "Thumbnails/thumbnail.png", MAX_IMAGE_BYTES, NULL },
		{ "_rels/.rels", MAX_INDEX_BYTES, NULL },
		{ "META-INF/container.xml", MAX_INDEX_BYTES, NULL },
	};
	GdkPixbuf *pixbuf = NULL;
	char *name = NULL;
	guint i;

	if (size <= 0 || !source_init (&src, stream, cancellable)) {
		return NULL;
	}

	if (!zip_get (&src, wants, G_N_ELEMENTS (wants))) {
		g_free (src.buf);
		return NULL;
	}

	if (wants[0].got != NULL) {
		pixbuf = decode_image (wants[0].got, size, cancellable);
	} else if (wants[1].got != NULL) {
		RelsFind find = { NULL };

		each_tag (wants[1].got, rels_thumbnail, &find);
		name = resolve_member ("", find.found);
		g_free (find.found);
	} else if (wants[2].got != NULL) {
		name = epub_cover_name (&src, wants[2].got);
	}

	if (name != NULL) {
		g_autoptr (GBytes) image = zip_get_one (&src, name, MAX_IMAGE_BYTES);

		if (image != NULL) {
			pixbuf = decode_image (image, size, cancellable);
		}
		g_free (name);
	}

	for (i = 0; i < G_N_ELEMENTS (wants); i++) {
		g_clear_pointer (&wants[i].got, g_bytes_unref);
	}
	g_free (src.buf);

	if (g_cancellable_is_cancelled (cancellable)) {
		g_clear_object (&pixbuf);
	}

	return pixbuf;
}

GdkPixbuf *
nemo_office_thumbnail_uri (const char *uri, int size, GCancellable *cancellable)
{
	g_autoptr (GFile) file = g_file_new_for_uri (uri);
	g_autoptr (GFileInputStream) in = g_file_read (file, cancellable, NULL);

	if (in == NULL) {
		return NULL;
	}

	return nemo_office_thumbnail (G_INPUT_STREAM (in), size, cancellable);
}

/* What nemo-anywhere-mso-to-txt left out, matched against each part of a
   member's path: styles, settings, layouts and the like. */
static const char *const skip_names[] = {
	"styles.xml", "theme", "_rels", "printerSettings", "media", "drawings",
	"META-INF", "Thumbnails",
	"settings.xml", "app.xml", "theme1.xml", "[Content_Types].xml", "fontTable.xml", "webSettings.xml",
	"worksheets", "calcChain.xml",
	"slideLayouts", "slideMasters", "presProps.xml", "tableStyles.xml", "viewProps.xml", "presentation.xml",
	NULL
};

static gboolean
member_has_text (const char *name)
{
	static const char *const suffixes[] = { ".xml", ".xhtml", ".html", ".htm", ".opf", NULL };
	g_auto (GStrv) parts = NULL;
	guint i;

	if (strlen (name) > MAX_PATH_LEN) {
		return FALSE;
	}

	parts = g_strsplit (name, "/", MAX_SEGMENTS);
	for (i = 0; parts[i] != NULL; i++) {
		if (g_strv_contains (skip_names, parts[i])) {
			return FALSE;
		}
	}

	for (i = 0; suffixes[i] != NULL; i++) {
		if (g_str_has_suffix (name, suffixes[i])) {
			return TRUE;
		}
	}

	return FALSE;
}

/* The converter ran <[^>]+> to nothing and then \s+ to one space over the
   whole text. The same, a byte at a time, so a member is never held whole.
   A tag left open at the end is dropped. */
typedef struct {
	GString *out;
	gsize    max_len;
	gboolean in_tag;
	gboolean tag_empty;
	gboolean space;
} Strip;

static gboolean
is_regex_space (guint8 c)
{
	return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f' || c == '\v';
}

static void
strip_feed (Strip *s, const guint8 *p, gsize n)
{
	gsize i;

	for (i = 0; i < n && s->out->len < s->max_len; i++) {
		guint8 c = p[i];

		if (s->in_tag) {
			if (c == '>') {
				s->in_tag = FALSE;
				/* <> is no match for the pattern, so it stays. */
				if (s->tag_empty) {
					g_string_append (s->out, "<>");
					s->space = FALSE;
				}
			} else {
				s->tag_empty = FALSE;
			}
		} else if (c == '<') {
			s->in_tag = TRUE;
			s->tag_empty = TRUE;
		} else if (is_regex_space (c)) {
			if (!s->space) {
				g_string_append_c (s->out, ' ');
				s->space = TRUE;
			}
		} else {
			g_string_append_c (s->out, (char) c);
			s->space = FALSE;
		}
	}

	if (s->out->len > s->max_len) {
		g_string_truncate (s->out, s->max_len);
	}
}

/* The entities a document body uses, in one pass, so nothing decoded is
   read again. Same answer as the converter's &amp;-last replaces. */
static void
decode_entities (GString *text)
{
	static const struct {
		const char *name;
		char        c;
	} entities[] = {
		{ "&lt;", '<' }, { "&gt;", '>' }, { "&quot;", '"' }, { "&apos;", '\'' }, { "&nbsp;", ' ' }, { "&amp;", '&' },
	};
	gsize r = 0, w = 0, e;

	while (r < text->len) {
		if (text->str[r] == '&') {
			for (e = 0; e < G_N_ELEMENTS (entities); e++) {
				gsize n = strlen (entities[e].name);

				if (text->len - r >= n && memcmp (text->str + r, entities[e].name, n) == 0) {
					text->str[w++] = entities[e].c;
					r += n;
					break;
				}
			}
			if (e < G_N_ELEMENTS (entities)) {
				continue;
			}
		}
		text->str[w++] = text->str[r++];
	}

	g_string_truncate (text, w);
}

char *
nemo_office_text (GInputStream *stream, gsize max_len, GCancellable *cancellable, GError **error)
{
	Source src = { NULL, NULL, NULL };
	struct archive *a;
	struct archive_entry *entry;
	Strip strip = { NULL, max_len, FALSE, FALSE, FALSE };
	guint8 chunk[16 * 1024];
	gsize read_total = 0;
	guint seen = 0;
	int status;

	if (!source_init (&src, stream, cancellable)) {
		g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_NOT_SUPPORTED, "Can't seek in this file");
		return NULL;
	}

	a = zip_open (&src);
	if (a == NULL) {
		g_free (src.buf);
		if (!g_cancellable_set_error_if_cancelled (cancellable, error)) {
			g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA, "Not a zip file");
		}
		return NULL;
	}

	strip.out = g_string_new (NULL);

	while (strip.out->len < max_len && read_total < MAX_TEXT_READ && seen++ < MAX_ENTRIES &&
	       !g_cancellable_is_cancelled (cancellable)) {
		const char *name;

		status = archive_read_next_header (a, &entry);
		if (status == ARCHIVE_EOF || status == ARCHIVE_FATAL) {
			break;
		}
		if (status != ARCHIVE_OK && status != ARCHIVE_WARN) {
			continue;
		}

		name = entry_name (entry);
		if (name == NULL || archive_entry_filetype (entry) != AE_IFREG || !member_has_text (name)) {
			continue;
		}

		/* A damaged member keeps what read cleanly. */
		for (;;) {
			la_ssize_t n = archive_read_data (a, chunk, sizeof chunk);

			if (n <= 0 || g_cancellable_is_cancelled (cancellable)) {
				break;
			}
			read_total += (gsize) n;
			strip_feed (&strip, chunk, (gsize) n);
			if (strip.out->len >= max_len || read_total >= MAX_TEXT_READ) {
				break;
			}
		}

		strip_feed (&strip, (const guint8 *) " ", 1);
	}

	archive_read_free (a);
	g_free (src.buf);

	if (g_cancellable_set_error_if_cancelled (cancellable, error)) {
		g_string_free (strip.out, TRUE);
		return NULL;
	}

	decode_entities (strip.out);
	return g_string_free (strip.out, FALSE);
}

char *
nemo_office_text_file (GFile *file, gsize max_len, GCancellable *cancellable, GError **error)
{
	g_autoptr (GFileInputStream) in = g_file_read (file, cancellable, error);

	if (in == NULL) {
		return NULL;
	}

	return nemo_office_text (G_INPUT_STREAM (in), max_len, cancellable, error);
}
