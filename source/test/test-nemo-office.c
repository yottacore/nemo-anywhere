/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */

/* test-nemo-office.c - office thumbnails and text, read in the app.

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

/* A small file of each zip-based type, written here, has to give its own
 * thumbnail through the thumbnail factory and its text through a real
 * content search. A thumbnailer and a search helper for every one of these
 * types are installed, both this program run with --ran, which leaves a mark.
 * Nothing may leave one. Then damaged files, which must answer nothing. */

#include <config.h>

#include <string.h>

#include <archive.h>
#include <archive_entry.h>
#include <glib.h>
#include <glib/gstdio.h>
#include <gtk/gtk.h>

#include <libnemo-private/nemo-desktop-thumbnail.h>
#include <libnemo-private/nemo-global-preferences.h>
#include <libnemo-private/nemo-office.h>
#include <libnemo-private/nemo-query.h>
#include <libnemo-private/nemo-search-engine-advanced.h>

#include "test-scratch.h"
#include "test-check.h"

#define MARK_ENV "NEMO_OFFICE_TEST_MARK"

static char *files_dir;

typedef struct {
	const char *name;
	const char *body;	/* NULL: the picture */
} Member;

typedef struct {
	const char   *file;
	const char   *picture_at;	/* member that gets the picture */
	const char   *picture_type;	/* "png" or "jpeg" */
	guint32       rgb;
	int           width, height;
	const Member *members;
	const char   *word;		/* searched for, in no other file */
	const char   *present;
	const char   *absent;
} Sample;

static const char rels_thumb[] =
	"<?xml version=\"1.0\"?><Relationships xmlns=\"http://schemas.openxmlformats.org/package/2006/relationships\">"
	"<Relationship Id=\"rId1\" Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/officeDocument\" Target=\"word/document.xml\"/>"
	"<Relationship Id=\"rId2\" Type=\"http://schemas.openxmlformats.org/package/2006/relationships/metadata/thumbnail\" Target=\"docProps/thumbnail.jpeg\"/>"
	"</Relationships>";

static const Member docx[] = {
	{ "[Content_Types].xml", "<Types><Default Extension=\"xml\" ContentType=\"TYPESTEXT\"/></Types>" },
	{ "_rels/.rels", rels_thumb },
	{ "docProps/thumbnail.jpeg", NULL },
	{ "docProps/core.xml", "<cp:coreProperties><dc:title>Docx title</dc:title></cp:coreProperties>" },
	{ "word/document.xml", "<w:document><w:body><w:p><w:r><w:t>Alpha bravo &amp; charlie</w:t></w:r></w:p></w:body></w:document>" },
	{ "word/settings.xml", "<w:settings><w:x>SETTINGSTEXT</w:x></w:settings>" },
	{ NULL, NULL }
};

static const Member xlsx[] = {
	{ "_rels/.rels", rels_thumb },
	{ "docProps/thumbnail.jpeg", NULL },
	{ "xl/sharedStrings.xml", "<sst><si><t>xray yankee</t></si></sst>" },
	{ "xl/worksheets/sheet1.xml", "<worksheet><c><v>WORKSHEETTEXT</v></c></worksheet>" },
	{ NULL, NULL }
};

/* Target with a leading slash and a part name in other case, both allowed. */
static const Member pptx[] = {
	{ "_rels/.rels", "<Relationships><Relationship Id=\"x\" Type=\"http://schemas.openxmlformats.org/package/2006/relationships/metadata/thumbnail\" Target=\"/docProps/Thumbnail.JPEG\"/></Relationships>" },
	{ "docProps/thumbnail.jpeg", NULL },
	{ "ppt/slides/slide1.xml", "<p:sld><a:t>papa quebec</a:t></p:sld>" },
	{ "ppt/slideLayouts/slideLayout1.xml", "<p:sldLayout><a:t>LAYOUTTEXT</a:t></p:sldLayout>" },
	{ NULL, NULL }
};

static const Member odt[] = {
	{ "mimetype", "application/vnd.oasis.opendocument.text" },
	{ "content.xml", "<office:document-content><office:body><office:text><text:p>Delta echo</text:p></office:text></office:body></office:document-content>" },
	{ "META-INF/manifest.xml", "<manifest:manifest><x>MANIFESTTEXT</x></manifest:manifest>" },
	{ "Thumbnails/thumbnail.png", NULL },
	{ NULL, NULL }
};

static const Member ods[] = {
	{ "mimetype", "application/vnd.oasis.opendocument.spreadsheet" },
	{ "content.xml", "<office:document-content><table:table-cell><text:p>sierra tango</text:p></table:table-cell></office:document-content>" },
	{ "styles.xml", "<office:document-styles>STYLESTEXT</office:document-styles>" },
	{ "Thumbnails/thumbnail.png", NULL },
	{ NULL, NULL }
};

static const Member odp[] = {
	{ "mimetype", "application/vnd.oasis.opendocument.presentation" },
	{ "content.xml", "<office:document-content><draw:page><text:p>uniform victor</text:p></draw:page></office:document-content>" },
	{ "Thumbnails/thumbnail.png", NULL },
	{ NULL, NULL }
};

static const Member odg[] = {
	{ "mimetype", "application/vnd.oasis.opendocument.graphics" },
	{ "content.xml", "<office:document-content><draw:page><text:p>whiskey zulu</text:p></draw:page></office:document-content>" },
	{ "Thumbnails/thumbnail.png", NULL },
	{ NULL, NULL }
};

/* EPUB 3: the cover by property, in a folder, with an escaped name. */
static const Member epub3[] = {
	{ "mimetype", "application/epub+zip" },
	{ "META-INF/container.xml", "<container><rootfiles><rootfile full-path=\"OEBPS/content.opf\" media-type=\"application/oebps-package+xml\"/></rootfiles></container>" },
	{ "OEBPS/content.opf", "<package><metadata><dc:title>Hotel title</dc:title></metadata><manifest>"
	  "<item id=\"ch\" href=\"chapter.xhtml\" media-type=\"application/xhtml+xml\"/>"
	  "<item id=\"c\" href=\"images/cover%20art.png\" media-type=\"image/png\" properties=\"cover-image\"/>"
	  "</manifest></package>" },
	{ "OEBPS/chapter.xhtml", "<html><body><p>Foxtrot golf</p></body></html>" },
	{ "OEBPS/toc.ncx", "<ncx><x>NCXTEXT</x></ncx>" },
	{ "OEBPS/images/cover art.png", NULL },
	{ NULL, NULL }
};

/* EPUB 2: the cover named by a meta, one folder up from the package. */
static const Member epub2[] = {
	{ "mimetype", "application/epub+zip" },
	{ "META-INF/container.xml", "<container><rootfiles><rootfile media-type='application/oebps-package+xml' full-path='OPS/package.opf'/></rootfiles></container>" },
	{ "OPS/package.opf", "<opf:package><opf:metadata><opf:meta name=\"cover\" content=\"cov\"/></opf:metadata><opf:manifest>"
	  "<opf:item id=\"cov\" href=\"../cover.jpg\" media-type=\"image/jpeg\"/>"
	  "<opf:item id=\"t\" href=\"text.html\" media-type=\"text/html\"/>"
	  "</opf:manifest></opf:package>" },
	{ "OPS/text.html", "<html><body>kilo lima</body></html>" },
	{ "cover.jpg", NULL },
	{ NULL, NULL }
};

static const Sample samples[] = {
	{ "t.docx", "docProps/thumbnail.jpeg", "jpeg", 0xd02020, 40, 30, docx, "bravo", "Alpha bravo & charlie", "SETTINGSTEXT" },
	{ "t.xlsx", "docProps/thumbnail.jpeg", "jpeg", 0x20d020, 30, 40, xlsx, "yankee", "xray yankee", "WORKSHEETTEXT" },
	{ "t.pptx", "docProps/thumbnail.jpeg", "jpeg", 0x2020d0, 64, 48, pptx, "quebec", "papa quebec", "LAYOUTTEXT" },
	{ "t.odt", "Thumbnails/thumbnail.png", "png", 0xd02020, 5, 4, odt, "echo", "Delta echo", "MANIFESTTEXT" },
	{ "t.ods", "Thumbnails/thumbnail.png", "png", 0x20d020, 200, 256, ods, "tango", "sierra tango", "STYLESTEXT" },
	{ "t.odp", "Thumbnails/thumbnail.png", "png", 0x2020d0, 256, 192, odp, "victor", "uniform victor", NULL },
	{ "t.odg", "Thumbnails/thumbnail.png", "png", 0xd0d020, 100, 100, odg, "zulu", "whiskey zulu", NULL },
	{ "t.epub", "OEBPS/images/cover art.png", "png", 0x20d0d0, 60, 90, epub3, "golf", "Foxtrot golf", "NCXTEXT" },
	{ "t2.epub", "cover.jpg", "jpeg", 0xd020d0, 90, 60, epub2, "lima", "kilo lima", NULL },
};

static GBytes *
picture (const char *type, guint32 rgb, int width, int height)
{
	GdkPixbuf *pixbuf = gdk_pixbuf_new (GDK_COLORSPACE_RGB, FALSE, 8, width, height);
	gchar *buf = NULL;
	gsize len = 0;

	gdk_pixbuf_fill (pixbuf, (rgb << 8) | 0xff);
	check (gdk_pixbuf_save_to_buffer (pixbuf, &buf, &len, type, NULL, NULL));
	g_object_unref (pixbuf);

	return g_bytes_new_take (buf, len);
}

static void
add_member (struct archive *a, const char *name, const void *data, gsize len)
{
	struct archive_entry *entry = archive_entry_new ();

	archive_entry_set_pathname (entry, name);
	archive_entry_set_filetype (entry, AE_IFREG);
	archive_entry_set_perm (entry, 0644);
	archive_entry_set_size (entry, (la_int64_t) len);
	archive_entry_set_mtime (entry, 1700000000, 0);
	check (archive_write_header (a, entry) == ARCHIVE_OK);
	check (archive_write_data (a, data, len) == (la_ssize_t) len);
	archive_entry_free (entry);
}

/* Returns: (transfer full): the path */
static char *
write_zip (const char *file, const Member *members, GBytes *pic)
{
	gsize cap = 4 * 1024 * 1024, used = 0;
	guint8 *buf = g_malloc (cap);
	struct archive *a = archive_write_new ();
	char *path = g_build_filename (files_dir, file, NULL);
	guint i;

	archive_write_set_format_zip (a);
	check (archive_write_open_memory (a, buf, cap, &used) == ARCHIVE_OK);

	for (i = 0; members[i].name != NULL; i++) {
		if (members[i].body != NULL) {
			add_member (a, members[i].name, members[i].body, strlen (members[i].body));
		} else {
			gsize len = 0;
			const void *data = g_bytes_get_data (pic, &len);

			add_member (a, members[i].name, data, len);
		}
	}

	check (archive_write_close (a) == ARCHIVE_OK);
	archive_write_free (a);

	check (g_file_set_contents (path, (const char *) buf, (gssize) used, NULL));
	g_free (buf);

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
	return ABS (a - b) <= 48;
}

static void
test_sample (NemoDesktopThumbnailFactory *factory, const Sample *s)
{
	g_autoptr (GBytes) pic = picture (s->picture_type, s->rgb, s->width, s->height);
	g_autofree char *path = write_zip (s->file, s->members, pic);
	g_autofree char *uri = g_filename_to_uri (path, NULL, NULL);
	g_autofree char *type = content_type_of (path);
	g_autofree char *text = NULL;
	g_autoptr (GFile) file = g_file_new_for_path (path);
	GError *error = NULL;
	GdkPixbuf *pixbuf;
	int want_w, want_h, longer = MAX (s->width, s->height);

	g_print ("%s (%s)\n", s->file, type != NULL ? type : "no type");

	check (nemo_office_type_ok (type));
	check (nemo_desktop_thumbnail_factory_can_make (factory, uri, type));

	pixbuf = nemo_desktop_thumbnail_factory_generate_thumbnail_at_size (factory, uri, type, 128, NULL);
	check (pixbuf != NULL);
	if (pixbuf != NULL) {
		const guchar *px = gdk_pixbuf_read_pixels (pixbuf);
		int stride = gdk_pixbuf_get_rowstride (pixbuf);
		int w = gdk_pixbuf_get_width (pixbuf), h = gdk_pixbuf_get_height (pixbuf);
		const guchar *mid = px + (h / 2) * stride + (w / 2) * gdk_pixbuf_get_n_channels (pixbuf);

		/* Fitted to the size asked for, up as well as down, as the
		   gsf-office thumbnailer did. */
		want_w = MAX ((int) (s->width * 128.0 / longer + 0.5), 1);
		want_h = MAX ((int) (s->height * 128.0 / longer + 0.5), 1);
		check (w == want_w && h == want_h);
		check (about (mid[0], (s->rgb >> 16) & 0xff) && about (mid[1], (s->rgb >> 8) & 0xff) &&
		       about (mid[2], s->rgb & 0xff));
		g_object_unref (pixbuf);
	}

	text = nemo_office_text_file (file, 1024 * 1024, NULL, &error);
	check (text != NULL && error == NULL);
	g_clear_error (&error);
	if (text != NULL) {
		check (strstr (text, s->present) != NULL);
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

/* No picture the app can draw: none at all, a metafile, or not a zip. The
   reader is asked first and gives nothing, and then the stand-in thumbnailer
   installed for these types gets its turn. Not a zip has no text either, so
   the stand-in search helper gets its turn for it. */
static const Member odt_bare[] = {
	{ "mimetype", "application/vnd.oasis.opendocument.text" },
	{ "content.xml", "<office:document-content><office:body><office:text><text:p>bare text</text:p></office:text></office:body></office:document-content>" },
	{ NULL, NULL }
};

static const Member docx_wmf[] = {
	{ "_rels/.rels", "<Relationships><Relationship Id=\"t\" Type=\"http://schemas.openxmlformats.org/package/2006/relationships/metadata/thumbnail\" Target=\"docProps/thumbnail.wmf\"/></Relationships>" },
	{ "docProps/thumbnail.wmf", "\xd7\xcd\xc6\x9a\x00\x00 not drawn here" },
	{ "word/document.xml", "<w:document><w:body><w:p><w:r><w:t>wmf text</w:t></w:r></w:p></w:body></w:document>" },
	{ NULL, NULL }
};

static void
forget_mark (void)
{
	g_autofree char *mark = mark_path ();

	g_unlink (mark);
}

static void
no_thumbnail (NemoDesktopThumbnailFactory *factory, const char *path)
{
	g_autofree char *uri = g_filename_to_uri (path, NULL, NULL);
	g_autofree char *type = content_type_of (path);
	GdkPixbuf *pixbuf;

	g_print ("%s (%s), no picture\n", path, type != NULL ? type : "no type");
	check (nemo_office_type_ok (type));
	pixbuf = nemo_office_thumbnail_uri (uri, 128, NULL);
	check (pixbuf == NULL);
	g_clear_object (&pixbuf);

	/* The stand-in draws nothing, so there is still no thumbnail. */
	forget_mark ();
	pixbuf = nemo_desktop_thumbnail_factory_generate_thumbnail_at_size (factory, uri, type, 128, NULL);
	check (pixbuf == NULL);
	g_clear_object (&pixbuf);
	check (helper_ran ());
	forget_mark ();
}

static void
test_no_picture (NemoDesktopThumbnailFactory *factory)
{
	g_autofree char *bare = write_zip ("bare.odt", odt_bare, NULL);
	g_autofree char *wmf = write_zip ("wmf.docx", docx_wmf, NULL);
	g_autofree char *junk = g_build_filename (files_dir, "junk.docx", NULL);
	const Sample none = { "junk.docx", NULL, NULL, 0, 0, 0, NULL, "nothingreadsme", NULL, NULL };

	no_thumbnail (factory, bare);
	no_thumbnail (factory, wmf);

	/* Not text either, which Windows would search as text when nothing
	   there has registered the extension. */
	check (g_file_set_contents (junk, "\001\002\377\000 nothingreadsme \000\376\375", 23, NULL));
	no_thumbnail (factory, junk);
	search_for (&none, 0, TRUE);
	forget_mark ();

	check (g_unlink (bare) == 0 && g_unlink (wmf) == 0 && g_unlink (junk) == 0);
}

/* Cut short, bit-flipped and not a zip. Cut short gives nothing, since the
   zip's directory is at its end, and nothing may crash. */
static void
test_damaged (void)
{
	g_autoptr (GBytes) pic = picture ("png", 0xd02020, 20, 20);
	g_autofree char *path = write_zip ("whole.odt", odt, pic);
	g_autofree char *bytes = NULL;
	gsize len = 0, cut;
	GError *error = NULL;

	check (g_file_get_contents (path, &bytes, &len, NULL));

	for (cut = 0; cut < len; cut += 7) {
		g_autoptr (GInputStream) in = g_memory_input_stream_new_from_data (bytes, (gssize) cut, NULL);
		g_autoptr (GInputStream) again = g_memory_input_stream_new_from_data (bytes, (gssize) cut, NULL);
		GdkPixbuf *pixbuf = nemo_office_thumbnail (in, 128, NULL);
		char *text = nemo_office_text (again, 1024, NULL, NULL);

		/* With the directory at the end cut off, nothing is there. */
		check (pixbuf == NULL);
		g_clear_object (&pixbuf);
		g_free (text);
	}

	for (cut = 0; cut < len; cut += 3) {
		g_autofree char *flipped = g_memdup2 (bytes, len);
		g_autoptr (GInputStream) in = NULL;
		g_autoptr (GInputStream) again = NULL;
		GdkPixbuf *pixbuf;

		flipped[cut] ^= 0x5a;
		in = g_memory_input_stream_new_from_data (flipped, (gssize) len, NULL);
		again = g_memory_input_stream_new_from_data (flipped, (gssize) len, NULL);
		pixbuf = nemo_office_thumbnail (in, 128, NULL);
		g_clear_object (&pixbuf);
		g_free (nemo_office_text (again, 1024, NULL, NULL));
	}

	{
		g_autoptr (GInputStream) in = g_memory_input_stream_new_from_data ("not a zip at all", 16, NULL);
		char *text = nemo_office_text (in, 1024, NULL, &error);

		check (text == NULL && error != NULL);
		g_clear_error (&error);
	}
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
	const char *mimes = "application/vnd.openxmlformats-officedocument.wordprocessingml.document;"
			    "application/vnd.openxmlformats-officedocument.spreadsheetml.sheet;"
			    "application/vnd.openxmlformats-officedocument.presentationml.presentation;"
			    "application/vnd.oasis.opendocument.text;application/vnd.oasis.opendocument.spreadsheet;"
			    "application/vnd.oasis.opendocument.presentation;application/vnd.oasis.opendocument.graphics;"
			    "application/epub+zip;";

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
		g_autofree char *probe = g_build_filename (dirs[i], "mime", "application", "epub+zip.xml", NULL);
		g_autofree char *from = g_build_filename (dirs[i], "mime", NULL);

		if (g_file_test (probe, G_FILE_TEST_EXISTS) && g_file_make_symbolic_link (link, from, NULL, NULL)) {
			return;
		}
	}

	g_printerr ("no shared mime database found\n");
}
#endif

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

		if (at != NULL && !g_file_set_contents (at, "ran\n", -1, NULL)) {
			return 1;
		}
		return 0;
	}

	home = test_scratch_config_home ("nemo-office-test-XXXXXX");
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
	if (!test_pixbuf_writes ("png") || !test_pixbuf_writes ("jpeg")) {
		g_print ("SKIP: no png or jpeg writer\n");
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

	test_no_picture (factory);
	test_damaged ();

	g_object_unref (factory);
	g_free (files_dir);

	if (failures > 0) {
		g_printerr ("%d failure(s)\n", failures);
		return 1;
	}

	g_print ("OK\n");
	return 0;
}
