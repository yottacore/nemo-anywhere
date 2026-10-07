/* The document converters behind "Containing:". Each format is written out
 * minimally with libgsf, the converter is run on it, and its output has to
 * carry the words that went in - and not the ones from parts it must skip.
 * Then the search engine itself is pointed at the same files, with helper
 * definitions naming the freshly built converters, and has to find each one
 * by a word inside it. */

#include <config.h>

#include <string.h>
#include <glib.h>
#include <gio/gio.h>
#include <gtk/gtk.h>
#include <gsf/gsf.h>
#include <libnemo-private/nemo-global-preferences.h>
#include <libnemo-private/nemo-search-engine-advanced.h>
#include <libnemo-private/nemo-query.h>

#include "test-scratch.h"

static int failures;
static char *tmpdir;
static gboolean search_done;
static GList *found;

static void
check (gboolean ok, const char *what)
{
	g_print ("%s: %s\n", ok ? "PASS" : "FAIL", what);
	if (!ok) {
		failures++;
	}
}

static char *
helper_path (const char *name)
{
	const char *dir = g_getenv ("NEMO_SEARCH_HELPER_DIR");

	if (dir == NULL) {
		g_error ("NEMO_SEARCH_HELPER_DIR is not set");
	}

#ifdef G_OS_WIN32
	return g_strdup_printf ("%s/nemo-anywhere-%s-to-txt.exe", dir, name);
#else
	return g_strdup_printf ("%s/nemo-anywhere-%s-to-txt", dir, name);
#endif
}

/* Runs a converter and hands back what it printed. */
static char *
convert (const char *helper, const char *file)
{
	char *exe = helper_path (helper);
	char *argv[] = { exe, (char *) file, NULL };
	char *out = NULL, *err = NULL;
	int status = 0;
	GError *error = NULL;

	if (!g_spawn_sync (NULL, argv, NULL, G_SPAWN_DEFAULT, NULL, NULL, &out, &err, &status, &error)) {
		g_print ("could not run %s: %s\n", exe, error->message);
		g_error_free (error);
		out = g_strdup ("");
	} else if (status != 0) {
		g_print ("%s exited %d: %s\n", exe, status, err ? err : "");
	}

	g_free (err);
	g_free (exe);
	return out;
}

/* Same, but gives up after a bound. A converter that never returns is the
 * failure some of these cases are looking for, and a test that hangs waiting
 * for one says much less than a test that reports it. */
typedef struct {
	GMainLoop  *loop;
	GSubprocess *proc;
	char       *out;
	gboolean    timed_out;
} BoundedRun;

static void
bounded_finished (GObject *source, GAsyncResult *result, gpointer data)
{
	BoundedRun *run = data;

	g_subprocess_communicate_utf8_finish (G_SUBPROCESS (source), result, &run->out, NULL, NULL);
	g_main_loop_quit (run->loop);
}

static gboolean
bounded_expired (gpointer data)
{
	BoundedRun *run = data;

	run->timed_out = TRUE;
	g_subprocess_force_exit (run->proc);
	return G_SOURCE_REMOVE;
}

/* TRUE if the converter finished inside the bound. What it printed goes to
 * *out, so a case with a bound on it never needs a second, unbounded run. */
static gboolean
converts_within (const char *helper, const char *file, guint seconds, char **out)
{
	char *exe = helper_path (helper);
	BoundedRun run = { NULL, NULL, NULL, FALSE };
	GError *error = NULL;
	guint timer;

	run.proc = g_subprocess_new (G_SUBPROCESS_FLAGS_STDOUT_PIPE | G_SUBPROCESS_FLAGS_STDERR_SILENCE,
				     &error, exe, file, NULL);
	g_free (exe);
	if (run.proc == NULL) {
		g_print ("could not run %s converter: %s\n", helper, error->message);
		g_error_free (error);
		return FALSE;
	}

	run.loop = g_main_loop_new (NULL, FALSE);
	timer = g_timeout_add_seconds (seconds, bounded_expired, &run);
	g_subprocess_communicate_utf8_async (run.proc, NULL, NULL, bounded_finished, &run);
	g_main_loop_run (run.loop);

	if (!run.timed_out) {
		g_source_remove (timer);
	}
	g_main_loop_unref (run.loop);
	g_object_unref (run.proc);

	if (out != NULL) {
		*out = run.out != NULL ? run.out : g_strdup ("");
	} else {
		g_free (run.out);
	}

	return !run.timed_out;
}

static void
expect (const char *helper, const char *file, const char *const present[], const char *const absent[])
{
	char *out = convert (helper, file);
	const char *base = strrchr (file, G_DIR_SEPARATOR) ? strrchr (file, G_DIR_SEPARATOR) + 1 : file;
	int i;

	for (i = 0; present[i] != NULL; i++) {
		char *what = g_strdup_printf ("%s carries '%s'", base, present[i]);

		check (strstr (out, present[i]) != NULL, what);
		g_free (what);
	}

	for (i = 0; absent != NULL && absent[i] != NULL; i++) {
		char *what = g_strdup_printf ("%s leaves out '%s'", base, absent[i]);

		check (strstr (out, absent[i]) == NULL, what);
		g_free (what);
	}

	g_free (out);
}

static char *
path_for (const char *name)
{
	return g_build_filename (tmpdir, name, NULL);
}

static void
write_zip_child (GsfOutfile *zip, const char *name, const char *body)
{
	GsfOutput *child;
	char **parts = g_strsplit (name, "/", 2);

	if (parts[1] != NULL) {
		GsfOutput *dir = gsf_outfile_new_child (zip, parts[0], TRUE);

		write_zip_child (GSF_OUTFILE (dir), parts[1], body);
		gsf_output_close (dir);
		g_object_unref (dir);
	} else {
		child = gsf_outfile_new_child (zip, parts[0], FALSE);
		gsf_output_write (child, strlen (body), (const guint8 *) body);
		gsf_output_close (child);
		g_object_unref (child);
	}

	g_strfreev (parts);
}

static char *
write_zip (const char *name, const char *const names[], const char *const bodies[])
{
	char *path = path_for (name);
	GsfOutput *out = gsf_output_stdio_new (path, NULL);
	GsfOutfile *zip = gsf_outfile_zip_new (out, NULL);
	int i;

	g_object_unref (out);

	for (i = 0; names[i] != NULL; i++) {
		write_zip_child (zip, names[i], bodies[i]);
	}

	gsf_output_close (GSF_OUTPUT (zip));
	g_object_unref (zip);
	return path;
}

static char *
write_ole (const char *name, const char *const names[], GByteArray *const bodies[])
{
	char *path = path_for (name);
	GsfOutput *out = gsf_output_stdio_new (path, NULL);
	GsfOutfile *ole = gsf_outfile_msole_new (out);
	int i;

	g_object_unref (out);

	for (i = 0; names[i] != NULL; i++) {
		GsfOutput *child = gsf_outfile_new_child (ole, names[i], FALSE);

		gsf_output_write (child, bodies[i]->len, bodies[i]->data);
		gsf_output_close (child);
		g_object_unref (child);
	}

	gsf_output_close (GSF_OUTPUT (ole));
	g_object_unref (ole);
	return path;
}

static void
put16 (GByteArray *b, guint16 v)
{
	guint8 raw[2] = { v & 0xFF, v >> 8 };

	g_byte_array_append (b, raw, 2);
}

static void
put32 (GByteArray *b, guint32 v)
{
	guint8 raw[4] = { v & 0xFF, (v >> 8) & 0xFF, (v >> 16) & 0xFF, v >> 24 };

	g_byte_array_append (b, raw, 4);
}

static void
put_bytes (GByteArray *b, const char *s)
{
	g_byte_array_append (b, (const guint8 *) s, strlen (s));
}

static void
put_utf16 (GByteArray *b, const char *s)
{
	glong units = 0;
	gunichar2 *w = g_utf8_to_utf16 (s, -1, NULL, &units, NULL);

	g_byte_array_append (b, (const guint8 *) w, units * 2);
	g_free (w);
}

static void
put_double (GByteArray *b, double v)
{
	g_byte_array_append (b, (const guint8 *) &v, sizeof v);
}

static void
biff_record (GByteArray *b, guint16 type, GByteArray *payload)
{
	put16 (b, type);
	put16 (b, payload->len);
	g_byte_array_append (b, payload->data, payload->len);
	g_byte_array_free (payload, TRUE);
}

static void
test_zip_formats (void)
{
	const char *docx_names[] = { "word/document.xml", "word/settings.xml", NULL };
	const char *docx_bodies[] = {
		"<w:document><w:body><w:p><w:r><w:t>Alpha bravo &amp; charlie</w:t></w:r></w:p></w:body></w:document>",
		"<w:settings><w:x>SETTINGSTEXT</w:x></w:settings>",
	};
	const char *docx_present[] = { "Alpha bravo & charlie", NULL };
	const char *docx_absent[] = { "SETTINGSTEXT", NULL };

	const char *odt_names[] = { "content.xml", "META-INF/manifest.xml", "mimetype", NULL };
	const char *odt_bodies[] = {
		"<office:document-content><office:body><office:text><text:p>Delta echo</text:p></office:text></office:body></office:document-content>",
		"<manifest:manifest><x>MANIFESTTEXT</x></manifest:manifest>",
		"application/vnd.oasis.opendocument.text",
	};
	const char *odt_present[] = { "Delta echo", NULL };
	const char *odt_absent[] = { "MANIFESTTEXT", NULL };

	const char *epub_names[] = { "OEBPS/chapter.xhtml", "OEBPS/content.opf", "OEBPS/toc.ncx", NULL };
	const char *epub_bodies[] = {
		"<html><body><p>Foxtrot golf</p></body></html>",
		"<package><metadata><dc:title>Hotel title</dc:title></metadata></package>",
		"<ncx><x>NCXTEXT</x></ncx>",
	};
	const char *epub_present[] = { "Foxtrot golf", "Hotel title", NULL };
	const char *epub_absent[] = { "NCXTEXT", NULL };

	char *path;

	path = write_zip ("t.docx", docx_names, docx_bodies);
	expect ("mso", path, docx_present, docx_absent);
	g_free (path);

	path = write_zip ("t.odt", odt_names, odt_bodies);
	expect ("mso", path, odt_present, odt_absent);
	g_free (path);

	path = write_zip ("t.epub", epub_names, epub_bodies);
	expect ("mso", path, epub_present, epub_absent);
	g_free (path);
}

static void
test_xls (void)
{
	GByteArray *wb = g_byte_array_new ();
	GByteArray *p;
	const char *names[] = { "Workbook", NULL };
	GByteArray *bodies[] = { wb };
	const char *present[] = { "Sheet1", "Hotel", "India", "Juliet", "Kilo", "42.5", "7", NULL };
	char *path;

	/* BOF: BIFF8 workbook globals */
	p = g_byte_array_new ();
	put16 (p, 0x0600);
	put16 (p, 0x0005);
	put32 (p, 0);
	put32 (p, 0);
	put32 (p, 0);
	biff_record (wb, 0x0809, p);

	/* BOUNDSHEET: one-byte name */
	p = g_byte_array_new ();
	put32 (p, 0);
	put16 (p, 0);
	g_byte_array_append (p, (const guint8 *) "\x06\x00", 2);
	put_bytes (p, "Sheet1");
	biff_record (wb, 0x0085, p);

	/* SST: a one-byte string and a UTF-16 one */
	p = g_byte_array_new ();
	put32 (p, 2);
	put32 (p, 2);
	put16 (p, 5);
	g_byte_array_append (p, (const guint8 *) "\x00", 1);
	put_bytes (p, "Hotel");
	put16 (p, 5);
	g_byte_array_append (p, (const guint8 *) "\x01", 1);
	put_utf16 (p, "India");
	biff_record (wb, 0x00FC, p);

	/* SST split by a CONTINUE in the middle of its string */
	p = g_byte_array_new ();
	put32 (p, 1);
	put32 (p, 1);
	put16 (p, 4);
	g_byte_array_append (p, (const guint8 *) "\x00", 1);
	put_bytes (p, "Ki");
	biff_record (wb, 0x00FC, p);
	p = g_byte_array_new ();
	g_byte_array_append (p, (const guint8 *) "\x00", 1);
	put_bytes (p, "lo");
	biff_record (wb, 0x003C, p);

	/* LABEL */
	p = g_byte_array_new ();
	put16 (p, 0);
	put16 (p, 0);
	put16 (p, 0);
	put16 (p, 6);
	g_byte_array_append (p, (const guint8 *) "\x00", 1);
	put_bytes (p, "Juliet");
	biff_record (wb, 0x0204, p);

	/* NUMBER */
	p = g_byte_array_new ();
	put16 (p, 1);
	put16 (p, 0);
	put16 (p, 0);
	put_double (p, 42.5);
	biff_record (wb, 0x0203, p);

	/* RK: the integer 7 */
	p = g_byte_array_new ();
	put16 (p, 2);
	put16 (p, 0);
	put16 (p, 0);
	put32 (p, (7 << 2) | 2);
	biff_record (wb, 0x027E, p);

	p = g_byte_array_new ();
	biff_record (wb, 0x000A, p);

	path = write_ole ("t.xls", names, bodies);
	expect ("xls", path, present, NULL);
	g_free (path);
	g_byte_array_free (wb, TRUE);
}

/* An SST that says it holds four billion strings and then stops. The count is
 * read straight off the file, so nothing but the converter's own progress test
 * keeps it from reading the same exhausted record once per claimed string. The
 * converter is spawned per file, so one workbook like this in a folder used to
 * tie up a core for the length of a search. */
static void
test_xls_truncated_sst (void)
{
	GByteArray *wb = g_byte_array_new ();
	GByteArray *p;
	const char *names[] = { "Workbook", NULL };
	GByteArray *bodies[] = { wb };
	const char *present[] = { "Xray", NULL };
	char *path, *out = NULL;

	/* BOF: BIFF8 workbook globals */
	p = g_byte_array_new ();
	put16 (p, 0x0600);
	put16 (p, 0x0005);
	put32 (p, 0);
	put32 (p, 0);
	put32 (p, 0);
	biff_record (wb, 0x0809, p);

	/* SST: one string, and a claim of 0xFFFFFFFF. Nothing continues it. */
	p = g_byte_array_new ();
	put32 (p, 0xFFFFFFFF);
	put32 (p, 0xFFFFFFFF);
	put16 (p, 5);
	g_byte_array_append (p, (const guint8 *) "\x00", 1);
	put_bytes (p, "Whisk\x79");
	biff_record (wb, 0x00FC, p);

	/* A record after it, so the converter has to get past the SST to read it. */
	p = g_byte_array_new ();
	put16 (p, 0);
	put16 (p, 0);
	put16 (p, 0);
	put16 (p, 4);
	g_byte_array_append (p, (const guint8 *) "\x00", 1);
	put_bytes (p, "Xray");
	biff_record (wb, 0x0204, p);

	p = g_byte_array_new ();
	biff_record (wb, 0x000A, p);

	path = write_ole ("truncated-sst.xls", names, bodies);
	check (converts_within ("xls", path, 10, &out),
	       "a truncated shared-string table does not hang the xls converter");
	check (strstr (out, present[0]) != NULL,
	       "the records after a truncated shared-string table are still read");
	g_free (out);
	g_free (path);
	g_byte_array_free (wb, TRUE);
}

static void
test_ppt (void)
{
	GByteArray *doc = g_byte_array_new ();
	GByteArray *inner = g_byte_array_new ();
	const char *names[] = { "PowerPoint Document", NULL };
	GByteArray *bodies[] = { doc };
	const char *present[] = { "Lima", "Mike", NULL };
	const char *absent[] = { "PICTUREBYTES", NULL };
	char *path;

	/* TextCharsAtom, TextBytesAtom, and an atom of another kind to be stepped over */
	put16 (inner, 0);
	put16 (inner, 0x0FA0);
	put32 (inner, 8);
	put_utf16 (inner, "Lima");
	put16 (inner, 0);
	put16 (inner, 0x0FA8);
	put32 (inner, 4);
	put_bytes (inner, "Mike");
	put16 (inner, 0);
	put16 (inner, 0xF01E);
	put32 (inner, 12);
	put_bytes (inner, "PICTUREBYTES");

	/* wrapped in a container */
	put16 (doc, 0x000F);
	put16 (doc, 0x03E8);
	put32 (doc, inner->len);
	g_byte_array_append (doc, inner->data, inner->len);

	path = write_ole ("t.ppt", names, bodies);
	expect ("ppt", path, present, absent);
	g_free (path);
	g_byte_array_free (inner, TRUE);
	g_byte_array_free (doc, TRUE);
}

static void
test_doc (void)
{
	GByteArray *word = g_byte_array_new ();
	GByteArray *table = g_byte_array_new ();
	const char *names[] = { "WordDocument", "0Table", NULL };
	GByteArray *bodies[] = { word, table };
	const char *present[] = { "November", "Oscar", NULL };
	const char *old_names[] = { "WordDocument", NULL };
	GByteArray *old_bodies[] = { word };
	const char *old_present[] = { "Papa", NULL };
	char *path;

	/* piece table: 8 one-byte characters at 0x800, then 5 UTF-16 ones at 0x900 */
	g_byte_array_append (table, (const guint8 *) "\x02", 1);
	put32 (table, 4 * 3 + 8 * 2);
	put32 (table, 0);
	put32 (table, 8);
	put32 (table, 13);
	put16 (table, 0);
	put32 (table, 0x40000000 | (0x800 * 2));
	put16 (table, 0);
	put16 (table, 0);
	put32 (table, 0x900);
	put16 (table, 0);

	g_byte_array_set_size (word, 0x1000);
	memset (word->data, 0, word->len);
	word->data[0] = 0xEC;
	word->data[1] = 0xA5;
	word->data[2] = 0xC1;
	word->data[3] = 0x00;
	word->data[0x1A6] = table->len;
	memcpy (word->data + 0x800, "November", 8);
	{
		glong units = 0;
		gunichar2 *w = g_utf8_to_utf16 ("Oscar", -1, NULL, &units, NULL);

		memcpy (word->data + 0x900, w, units * 2);
		g_free (w);
	}

	path = write_ole ("t.doc", names, bodies);
	expect ("doc", path, present, NULL);
	g_free (path);

	/* Word 95: no piece table, text between fcMin and fcMac */
	word->data[2] = 0x65;
	word->data[0x18] = 0x00;
	word->data[0x19] = 0x02;
	word->data[0x1C] = 0x04;
	word->data[0x1D] = 0x02;
	memcpy (word->data + 0x200, "Papa", 4);

	path = write_ole ("old.doc", old_names, old_bodies);
	expect ("doc", path, old_present, NULL);
	g_free (path);

	g_byte_array_free (word, TRUE);
	g_byte_array_free (table, TRUE);
}

static void
hits_added_cb (G_GNUC_UNUSED NemoSearchEngine *engine, GList *hits, G_GNUC_UNUSED gpointer data)
{
	for (GList *l = hits; l != NULL; l = l->next) {
		FileSearchResult *result = l->data;

		found = g_list_prepend (found, g_path_get_basename (result->uri));
		file_search_result_free (result);
	}
}

static void
finished_cb (G_GNUC_UNUSED NemoSearchEngine *engine, G_GNUC_UNUSED gpointer data)
{
	search_done = TRUE;
}

/* On Windows the program's path goes in as a user would write it there, with
   single backslashes, and quoted in Exec. */
static void
write_helper_definition (const char *dir, const char *name, const char *helper, const char *mimes)
{
	char *exe = helper_path (helper);
	char *path = g_build_filename (dir, name, NULL);
#ifdef G_OS_WIN32
	char *body;

	g_strdelimit (exe, "/", '\\');
	body = g_strdup_printf ("[Nemo Search Helper]\nTryExec=%s;\nExec=\"%s\" %%s\nMimeType=%s\nPriority=100\n",
				exe, exe, mimes);
#else
	char *body = g_strdup_printf ("[Nemo Search Helper]\nTryExec=%s;\nExec=%s %%s\nMimeType=%s\nPriority=100\n",
				      exe, exe, mimes);
#endif

	if (!g_file_set_contents (path, body, -1, NULL)) {
		g_error ("could not write %s", path);
	}

	g_free (body);
	g_free (path);
	g_free (exe);
}

static int glib_criticals;
static int badtry_warnings;

static void
watch_log (const gchar *domain, GLogLevelFlags level, const gchar *message, gpointer data)
{
	if (g_strcmp0 (domain, "GLib") == 0 && (level & G_LOG_LEVEL_CRITICAL)) {
		glib_criticals++;
	}
	if ((level & G_LOG_LEVEL_WARNING) && strstr (message, "badtry.nemo_search_helper") != NULL) {
		badtry_warnings++;
	}
	g_log_default_handler (domain, level, message, data);
}

/* One word, one file it should turn up. */
static void
search_for (const char *word, const char *expected_file)
{
	NemoSearchEngine *engine = nemo_search_engine_advanced_new ();
	NemoQuery *query = nemo_query_new ();
	char *uri = g_filename_to_uri (tmpdir, NULL, NULL);
	char *what;
	gboolean hit = FALSE;
	int spins = 0;

	g_signal_connect (engine, "hits-added", G_CALLBACK (hits_added_cb), NULL);
	g_signal_connect (engine, "finished", G_CALLBACK (finished_cb), NULL);

	nemo_query_set_location (query, uri);
	nemo_query_set_content_pattern (query, word);
	nemo_search_engine_set_query (engine, query);
	g_object_unref (query);
	g_free (uri);

	search_done = FALSE;
	found = NULL;
	nemo_search_engine_start (engine);

	while (!search_done && spins++ < 1000) {
		g_main_context_iteration (NULL, FALSE);
		g_usleep (10000);
	}

	for (GList *l = found; l != NULL; l = l->next) {
		if (g_strcmp0 (l->data, expected_file) == 0) {
			hit = TRUE;
		}
	}

	what = g_strdup_printf ("searching for '%s' finds %s and %d other file(s)",
				word, expected_file, g_list_length (found) - (hit ? 1 : 0));
	check (search_done && hit && g_list_length (found) == 1, what);
	g_free (what);

	g_list_free_full (found, g_free);
	found = NULL;
	g_object_unref (engine);
}

static void
test_engine (const char *data_home)
{
	char *dir = g_build_filename (data_home, NEMO_APP_SLUG, "search-helpers", NULL);
	char *path;
	GLogFunc old_handler;

	g_mkdir_with_parents (dir, 0700);
	write_helper_definition (dir, "zip.nemo_search_helper", "mso",
				 "application/vnd.openxmlformats-officedocument.wordprocessingml.document;"
				 "application/vnd.oasis.opendocument.text;application/epub+zip;");
	write_helper_definition (dir, "xls.nemo_search_helper", "xls", "application/vnd.ms-excel;");
	write_helper_definition (dir, "ppt.nemo_search_helper", "ppt", "application/vnd.ms-powerpoint;");
	write_helper_definition (dir, "doc.nemo_search_helper", "doc", "application/msword;");

	/* A Windows path with its backslashes not doubled is a bad escape, and
	   GKeyFile hands back no list at all. That has to skip the helper, not
	   take it unchecked. On Windows it is a path naming no program. */
	path = g_build_filename (dir, "badtry.nemo_search_helper", NULL);
	check (g_file_set_contents (path, "[Nemo Search Helper]\nTryExec=C:\\Tools\\nope.exe;\n"
				    "Exec=nope %s\nMimeType=application/msword;\nPriority=200\n", -1, NULL),
	       "bad TryExec helper written");
	g_free (path);
	g_free (dir);

	old_handler = g_log_set_default_handler (watch_log, NULL);

	search_for ("bravo", "t.docx");
	search_for ("echo", "t.odt");
	search_for ("golf", "t.epub");
	search_for ("juliet", "t.xls");
	search_for ("mike", "t.ppt");
	search_for ("november", "t.doc");

	g_log_set_default_handler (old_handler, NULL);
	check (glib_criticals == 0, "no GLib criticals while loading helpers");
#ifdef G_OS_WIN32
	check (badtry_warnings == 0, "a helper whose TryExec names no program is skipped quietly");
#else
	check (badtry_warnings > 0, "a helper with an unreadable TryExec is named and skipped");
#endif
}

int
main (int argc, char *argv[])
{
	GError *error = NULL;
	g_autofree char *scratch = test_scratch_config_home ("nemo-helpers-home-XXXXXX");

	/* The user's helper folder comes off these two. */
	g_setenv ("LOCALAPPDATA", scratch, TRUE);
	g_setenv ("XDG_DATA_HOME", scratch, TRUE);

	gtk_init_check (&argc, &argv);
	nemo_global_preferences_init ();
	gsf_init ();

	tmpdir = test_scratch_dir ("nemo-helpers-XXXXXX", &error);
	if (tmpdir == NULL) {
		g_error ("no temp dir: %s", error->message);
	}

	test_zip_formats ();
	test_xls ();
	test_xls_truncated_sst ();
	test_ppt ();
	test_doc ();
	test_engine (scratch);

	{
		GFile *dir = g_file_new_for_path (tmpdir);
		GFileEnumerator *e = g_file_enumerate_children (dir, "standard::name", 0, NULL, NULL);
		GFileInfo *info;

		while (e != NULL && (info = g_file_enumerator_next_file (e, NULL, NULL)) != NULL) {
			GFile *child = g_file_get_child (dir, g_file_info_get_name (info));

			g_file_delete (child, NULL, NULL);
			g_object_unref (child);
			g_object_unref (info);
		}

		g_clear_object (&e);
		g_file_delete (dir, NULL, NULL);
		g_object_unref (dir);
	}

	gsf_shutdown ();
	g_free (tmpdir);

	return failures == 0 ? 0 : 1;
}
