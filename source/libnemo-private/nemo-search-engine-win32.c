/* nemo-search-engine-win32.c - search through the Windows Search index
 *
 * The index answers a name or content search over anything it covers in a
 * fraction of the time a directory walk takes. This engine asks it first and
 * hands the walking engine everything the index cannot do: a folder it does
 * not cover, a network location, a regular expression or a case-sensitive
 * match on file contents.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published
 * by the Free Software Foundation.
 */

#include <config.h>

#define COBJMACROS
#include <windows.h>
#include <oledb.h>
#include <msdasc.h>
#include <string.h>

#include <gio/gio.h>

#include "nemo-search-engine-win32.h"
#include "nemo-search-engine-advanced.h"
#include "nemo-global-preferences.h"
#include "nemo-file.h"

/* Spelled out here so nothing rides on which GUIDs the toolchain's libraries carry. */
static const GUID clsid_msdainitialize = { 0x2206CDB0, 0x19C1, 0x11D1, { 0x89, 0xE0, 0x00, 0xC0, 0x4F, 0xD7, 0xA8, 0x29 } };
static const GUID iid_idatainitialize  = { 0x2206CCB1, 0x19C1, 0x11D1, { 0x89, 0xE0, 0x00, 0xC0, 0x4F, 0xD7, 0xA8, 0x29 } };
static const GUID iid_idbinitialize    = { 0x0C733A8B, 0x2A1C, 0x11CE, { 0xAD, 0xE5, 0x00, 0xAA, 0x00, 0x44, 0x77, 0x3D } };
static const GUID iid_idbcreatesession = { 0x0C733A5D, 0x2A1C, 0x11CE, { 0xAD, 0xE5, 0x00, 0xAA, 0x00, 0x44, 0x77, 0x3D } };
static const GUID iid_idbcreatecommand = { 0x0C733A1D, 0x2A1C, 0x11CE, { 0xAD, 0xE5, 0x00, 0xAA, 0x00, 0x44, 0x77, 0x3D } };
static const GUID iid_icommandtext     = { 0x0C733A27, 0x2A1C, 0x11CE, { 0xAD, 0xE5, 0x00, 0xAA, 0x00, 0x44, 0x77, 0x3D } };
static const GUID iid_irowset          = { 0x0C733A7C, 0x2A1C, 0x11CE, { 0xAD, 0xE5, 0x00, 0xAA, 0x00, 0x44, 0x77, 0x3D } };
static const GUID iid_iaccessor        = { 0x0C733A8C, 0x2A1C, 0x11CE, { 0xAD, 0xE5, 0x00, 0xAA, 0x00, 0x44, 0x77, 0x3D } };
static const GUID dbguid_default       = { 0xC8B521FB, 0x5CF3, 0x11CE, { 0xAD, 0xE5, 0x00, 0xAA, 0x00, 0x44, 0x77, 0x3D } };

#define HIT_BATCH_SIZE 200
#define ROWS_PER_FETCH 64

typedef struct {
	IDBInitialize *source;
	IDBCreateCommand *commands;
} IndexConnection;

typedef struct {
	DBSTATUS path_status;
	DBLENGTH path_length;
	wchar_t *path;
	DBSTATUS attrs_status;
	DBLENGTH attrs_length;
	guint32 attrs;
} IndexRow;

typedef gboolean (*IndexRowFunc) (const wchar_t *path,
				  guint32        attrs,
				  gpointer       user_data);

typedef struct {
	NemoSearchEngineWin32 *engine;
	GCancellable *cancellable;
	gchar *folder;
	gsize folder_len;            /* fixed for the search, and hit_is_wanted runs per row */
	gchar *sql;
	NemoSearchNameMatcher *matcher;
	GHashTable *skip_names;
	GList *skip_paths;
	gboolean show_hidden;

	GList *hits;
	guint n_pending;
	gboolean sent_any;
	gboolean indexed;
} IndexSearch;

typedef struct {
	IndexSearch *search;
	GList *hits;
} HitBatch;

struct NemoSearchEngineWin32Details {
	NemoQuery *query;
	NemoSearchEngine *fallback;
	IndexSearch *active;
};

G_DEFINE_TYPE (NemoSearchEngineWin32, nemo_search_engine_win32, NEMO_TYPE_SEARCH_ENGINE);

static void
index_close (IndexConnection *conn)
{
	if (conn->commands != NULL) {
		IDBCreateCommand_Release (conn->commands);
	}
	if (conn->source != NULL) {
		IDBInitialize_Uninitialize (conn->source);
		IDBInitialize_Release (conn->source);
	}
	memset (conn, 0, sizeof *conn);
}

static gboolean
index_open (IndexConnection *conn)
{
	IDataInitialize *loader = NULL;
	IDBCreateSession *sessions = NULL;
	HRESULT hr;

	memset (conn, 0, sizeof *conn);

	hr = CoCreateInstance (&clsid_msdainitialize, NULL, CLSCTX_INPROC_SERVER,
			       &iid_idatainitialize, (void **) &loader);
	if (FAILED (hr)) {
		return FALSE;
	}

	hr = IDataInitialize_GetDataSource (loader, NULL, CLSCTX_INPROC_SERVER,
					    L"provider=Search.CollatorDSO.1;EXTENDED PROPERTIES=\"Application=Windows\"",
					    &iid_idbinitialize, (IUnknown **) &conn->source);
	IDataInitialize_Release (loader);
	if (FAILED (hr)) {
		return FALSE;
	}

	hr = IDBInitialize_Initialize (conn->source);
	if (SUCCEEDED (hr)) {
		hr = IDBInitialize_QueryInterface (conn->source, &iid_idbcreatesession, (void **) &sessions);
	}
	if (SUCCEEDED (hr)) {
		hr = IDBCreateSession_CreateSession (sessions, NULL, &iid_idbcreatecommand,
						     (IUnknown **) &conn->commands);
		IDBCreateSession_Release (sessions);
	}

	if (FAILED (hr)) {
		index_close (conn);
		return FALSE;
	}

	return TRUE;
}

/* Runs one statement and hands each row to func until it answers FALSE.
 * FALSE back means the index refused the statement outright. */
static gboolean
index_query (IndexConnection *conn,
	     const gchar     *sql,
	     IndexRowFunc     func,
	     gpointer         user_data)
{
	ICommandText *command = NULL;
	IRowset *rowset = NULL;
	IAccessor *accessor = NULL;
	HACCESSOR handle = 0;
	DBBINDING bindings[2];
	wchar_t *wide;
	HRESULT hr;
	gboolean keep_going = TRUE;

	wide = (wchar_t *) g_utf8_to_utf16 (sql, -1, NULL, NULL, NULL);
	if (wide == NULL) {
		return FALSE;
	}

	hr = IDBCreateCommand_CreateCommand (conn->commands, NULL, &iid_icommandtext, (IUnknown **) &command);
	if (SUCCEEDED (hr)) {
		hr = ICommandText_SetCommandText (command, &dbguid_default, wide);
	}
	if (SUCCEEDED (hr)) {
		hr = ICommandText_Execute (command, NULL, &iid_irowset, NULL, NULL, (IUnknown **) &rowset);
	}
	g_free (wide);

	if (FAILED (hr) || rowset == NULL) {
		g_debug ("Windows Search refused a statement (0x%08lx): %s", (unsigned long) hr, sql);
		if (command != NULL) {
			ICommandText_Release (command);
		}
		return FALSE;
	}

	memset (bindings, 0, sizeof bindings);
	bindings[0].iOrdinal = 1;
	bindings[0].obStatus = G_STRUCT_OFFSET (IndexRow, path_status);
	bindings[0].obLength = G_STRUCT_OFFSET (IndexRow, path_length);
	bindings[0].obValue = G_STRUCT_OFFSET (IndexRow, path);
	bindings[0].dwPart = DBPART_VALUE | DBPART_LENGTH | DBPART_STATUS;
	bindings[0].dwMemOwner = DBMEMOWNER_CLIENTOWNED;
	bindings[0].wType = DBTYPE_WSTR | DBTYPE_BYREF;
	bindings[0].cbMaxLen = sizeof (wchar_t *);

	bindings[1] = bindings[0];
	bindings[1].iOrdinal = 2;
	bindings[1].obStatus = G_STRUCT_OFFSET (IndexRow, attrs_status);
	bindings[1].obLength = G_STRUCT_OFFSET (IndexRow, attrs_length);
	bindings[1].obValue = G_STRUCT_OFFSET (IndexRow, attrs);
	bindings[1].wType = DBTYPE_UI4;
	bindings[1].cbMaxLen = sizeof (guint32);

	hr = IRowset_QueryInterface (rowset, &iid_iaccessor, (void **) &accessor);
	if (SUCCEEDED (hr)) {
		hr = IAccessor_CreateAccessor (accessor, DBACCESSOR_ROWDATA, 2, bindings, 0, &handle, NULL);
	}

	while (SUCCEEDED (hr) && keep_going) {
		HROW *rows = NULL;
		DBCOUNTITEM got = 0, i;

		hr = IRowset_GetNextRows (rowset, DB_NULL_HCHAPTER, 0, ROWS_PER_FETCH, &got, &rows);
		if (FAILED (hr) || got == 0) {
			break;
		}

		for (i = 0; i < got && keep_going; i++) {
			IndexRow row;

			memset (&row, 0, sizeof row);

			if (SUCCEEDED (IRowset_GetData (rowset, rows[i], handle, &row)) &&
			    row.path_status == DBSTATUS_S_OK && row.path != NULL) {
				keep_going = func (row.path, row.attrs_status == DBSTATUS_S_OK ? row.attrs : 0, user_data);
			}

			CoTaskMemFree (row.path);
		}

		IRowset_ReleaseRows (rowset, got, rows, NULL, NULL, NULL);
		CoTaskMemFree (rows);
	}

	if (handle != 0) {
		IAccessor_ReleaseAccessor (accessor, handle, NULL);
	}
	if (accessor != NULL) {
		IAccessor_Release (accessor);
	}
	IRowset_Release (rowset);
	ICommandText_Release (command);

	return TRUE;
}

static void
append_sql_string (GString     *sql,
		   const gchar *text)
{
	for (; *text != '\0'; text++) {
		if (*text == '\'') {
			g_string_append (sql, "''");
		} else {
			g_string_append_c (sql, *text);
		}
	}
}

static gchar *
scope_url (const gchar *folder)
{
	gchar *slashed = g_strdup (folder);
	gchar *url;

	g_strdelimit (slashed, "\\", '/');
	url = g_strconcat ("file:", slashed, NULL);
	g_free (slashed);

	return url;
}

static gchar *
probe_sql (const gchar *folder)
{
	GString *sql = g_string_new ("SELECT TOP 1 System.ItemPathDisplay, System.FileAttributes FROM SystemIndex WHERE SCOPE='");
	gchar *url = scope_url (folder);

	append_sql_string (sql, url);
	g_string_append_c (sql, '\'');
	g_free (url);

	return g_string_free (sql, FALSE);
}

/* Returns: (transfer full): free with g_free */
gchar *
nemo_search_win32_build_sql (const gchar *folder,
			     gboolean     recurse,
			     const gchar *file_pattern,
			     gboolean     file_pattern_is_regex,
			     const gchar *content_text)
{
	GString *sql = g_string_new ("SELECT System.ItemPathDisplay, System.FileAttributes FROM SystemIndex WHERE ");
	gchar *url = scope_url (folder);

	g_string_append (sql, recurse ? "SCOPE='" : "DIRECTORY='");
	append_sql_string (sql, url);
	g_string_append_c (sql, '\'');
	g_free (url);

	/* The name test proper is applied to every row afterwards. This only trims
	 * what the index hands over, so a word LIKE cannot express is left out. */
	if (file_pattern != NULL && !file_pattern_is_regex) {
		gchar **words = g_strsplit_set (file_pattern, " \t\r\n", -1);
		gint i;

		for (i = 0; words[i] != NULL; i++) {
			gboolean has_wildcard;
			gchar *like;

			if (words[i][0] == '\0' || strpbrk (words[i], "%_[") != NULL) {
				continue;
			}

			has_wildcard = strpbrk (words[i], "*?") != NULL;
			like = g_strdup (words[i]);
			g_strdelimit (like, "*", '%');
			g_strdelimit (like, "?", '_');

			g_string_append (sql, " AND System.FileName LIKE '");
			if (!has_wildcard) {
				g_string_append_c (sql, '%');
			}
			append_sql_string (sql, like);
			if (!has_wildcard) {
				g_string_append_c (sql, '%');
			}
			g_string_append_c (sql, '\'');

			g_free (like);
		}

		g_strfreev (words);
	}

	/* Double quotes are the phrase marks of the index's own syntax, so the text
	 * goes in as one phrase, its last word matched as a prefix. */
	if (content_text != NULL) {
		gchar *phrase = g_strdup (content_text);

		g_strdelimit (phrase, "\"*", ' ');
		g_strstrip (phrase);

		if (phrase[0] != '\0') {
			g_string_append (sql, " AND CONTAINS(System.Search.Contents, '\"");
			append_sql_string (sql, phrase);
			g_string_append (sql, "*\"')");
		}

		g_free (phrase);
	}

	return g_string_free (sql, FALSE);
}

static gboolean
note_any_row (G_GNUC_UNUSED const wchar_t *path,
	      G_GNUC_UNUSED guint32        attrs,
	      gpointer       user_data)
{
	*(gboolean *) user_data = TRUE;
	return FALSE;
}

static gboolean
probe_folder (IndexConnection *conn,
	      const gchar     *folder)
{
	gchar *sql = probe_sql (folder);
	gboolean any = FALSE;

	index_query (conn, sql, note_any_row, &any);
	g_free (sql);

	return any;
}

gboolean
nemo_search_win32_folder_is_indexed (const gchar *folder)
{
	IndexConnection conn;
	gboolean com, indexed = FALSE;

	com = SUCCEEDED (CoInitializeEx (NULL, COINIT_MULTITHREADED));

	if (index_open (&conn)) {
		indexed = probe_folder (&conn, folder);
		index_close (&conn);
	}

	if (com) {
		CoUninitialize ();
	}

	return indexed;
}

static void
index_search_free (IndexSearch *search)
{
	g_list_free_full (search->hits, (GDestroyNotify) file_search_result_free);
	g_list_free_full (search->skip_paths, g_free);
	g_hash_table_destroy (search->skip_names);
	nemo_search_name_matcher_free (search->matcher);
	g_object_unref (search->cancellable);
	g_object_unref (search->engine);
	g_free (search->folder);
	g_free (search->sql);
	g_free (search);
}

static gboolean
deliver_hits_idle (gpointer data)
{
	HitBatch *batch = data;

	if (!g_cancellable_is_cancelled (batch->search->cancellable)) {
		nemo_search_engine_hits_added (NEMO_SEARCH_ENGINE (batch->search->engine), batch->hits);
		g_list_free (batch->hits);
	} else {
		g_list_free_full (batch->hits, (GDestroyNotify) file_search_result_free);
	}

	g_free (batch);
	return FALSE;
}

static void
send_hits (IndexSearch *search)
{
	HitBatch *batch;

	if (search->hits == NULL) {
		return;
	}

	batch = g_new0 (HitBatch, 1);
	batch->search = search;
	batch->hits = search->hits;
	search->hits = NULL;
	search->n_pending = 0;
	search->sent_any = TRUE;

	g_idle_add (deliver_hits_idle, batch);
	/* cppcheck-suppress memleak ; batch is owned by the idle, which frees it */
}

/* The rules the walking engine applies while it walks, applied to a path the
 * index handed back: hidden files, skipped folders, then the name pattern. */
static gboolean
hit_is_wanted (IndexSearch *search,
	       const gchar *path,
	       guint32      attrs)
{
	const gchar *rest;
	gchar **parts;
	gchar *basename;
	gint i;
	gboolean wanted = TRUE;

	if (g_ascii_strcasecmp (path, search->folder) == 0) {
		return FALSE;
	}

	if (!search->show_hidden && (attrs & FILE_ATTRIBUTE_HIDDEN)) {
		return FALSE;
	}

	for (GList *l = search->skip_paths; l != NULL; l = l->next) {
		if (g_ascii_strncasecmp (path, l->data, strlen (l->data)) == 0) {
			return FALSE;
		}
	}

	rest = g_ascii_strncasecmp (path, search->folder, search->folder_len) == 0
		? path + search->folder_len : path;
	while (*rest == '\\' || *rest == '/') {
		rest++;
	}

	parts = g_strsplit_set (rest, "\\/", -1);

	for (i = 0; parts[i] != NULL && wanted; i++) {
		if (g_hash_table_contains (search->skip_names, parts[i])) {
			wanted = FALSE;
		} else if (!search->show_hidden && nemo_file_name_is_hidden_dot_file (parts[i])) {
			wanted = FALSE;
		}
	}

	g_strfreev (parts);

	if (!wanted) {
		return FALSE;
	}

	basename = g_path_get_basename (path);
	wanted = nemo_search_name_matcher_matches (search->matcher, basename);
	g_free (basename);

	return wanted;
}

static gboolean
collect_hit (const wchar_t *wide_path,
	     guint32        attrs,
	     gpointer       user_data)
{
	IndexSearch *search = user_data;
	gchar *path;

	if (g_cancellable_is_cancelled (search->cancellable)) {
		return FALSE;
	}

	path = g_utf16_to_utf8 (wide_path, -1, NULL, NULL, NULL);
	if (path == NULL) {
		return TRUE;
	}

	if (hit_is_wanted (search, path, attrs)) {
		gchar *uri = g_filename_to_uri (path, NULL, NULL);

		if (uri != NULL) {
			search->hits = g_list_prepend (search->hits, file_search_result_new (uri, NULL));

			if (++search->n_pending >= HIT_BATCH_SIZE) {
				send_hits (search);
			}
		}
	}

	g_free (path);
	return TRUE;
}

static void start_fallback (NemoSearchEngineWin32 *engine);

static gboolean
index_search_done_idle (gpointer data)
{
	IndexSearch *search = data;
	NemoSearchEngineWin32 *engine = search->engine;

	if (!g_cancellable_is_cancelled (search->cancellable)) {
		if (engine->details->active == search) {
			engine->details->active = NULL;
		}

		if (search->indexed) {
			nemo_search_engine_finished (NEMO_SEARCH_ENGINE (engine));
		} else {
			start_fallback (engine);
		}
	}

	index_search_free (search);
	return FALSE;
}

static gpointer
index_search_thread (gpointer data)
{
	IndexSearch *search = data;
	IndexConnection conn;
	gboolean com;

	com = SUCCEEDED (CoInitializeEx (NULL, COINIT_MULTITHREADED));

	if (index_open (&conn)) {
		search->indexed = probe_folder (&conn, search->folder);

		if (search->indexed) {
			g_debug ("Windows Search: %s", search->sql);

			if (!index_query (&conn, search->sql, collect_hit, search) && !search->sent_any) {
				/* refused the statement: the walk answers instead */
				search->indexed = FALSE;
			}

			send_hits (search);
		}

		index_close (&conn);
	}

	if (com) {
		CoUninitialize ();
	}

	g_idle_add (index_search_done_idle, search);
	return NULL;
}

static void
start_fallback (NemoSearchEngineWin32 *engine)
{
	nemo_search_engine_set_query (engine->details->fallback, engine->details->query);
	nemo_search_engine_start (engine->details->fallback);
}

static gboolean
is_drive_path (const gchar *path)
{
	return path != NULL && g_ascii_isalpha (path[0]) && path[1] == ':';
}

static void
nemo_search_engine_win32_start (NemoSearchEngine *engine)
{
	NemoSearchEngineWin32 *self = NEMO_SEARCH_ENGINE_WIN32 (engine);
	NemoQuery *query = self->details->query;
	IndexSearch *search;
	GThread *thread;
	GFile *location = NULL;
	gchar *uri, *folder = NULL, *file_pattern, *content = NULL;
	gchar **skip;
	gboolean use_index;
	gint i;

	if (query == NULL || self->details->active != NULL) {
		return;
	}

	uri = nemo_query_get_location (query);
	if (uri != NULL) {
		location = g_file_new_for_uri (uri);
		folder = g_file_get_path (location);
	}

	if (nemo_query_has_content_pattern (query)) {
		content = nemo_query_get_content_pattern (query);
	}

	use_index = nemo_config_get_boolean (nemo_windows_preferences, NEMO_PREFERENCES_USE_SEARCH_INDEX) &&
		    is_drive_path (folder) &&
		    (content == NULL ||
		     (!nemo_query_get_use_content_regex (query) && !nemo_query_get_content_case_sensitive (query)));

	if (!use_index) {
		g_free (content);
		g_free (folder);
		g_free (uri);
		g_clear_object (&location);
		start_fallback (self);
		return;
	}

	file_pattern = nemo_query_get_file_pattern (query);

	search = g_new0 (IndexSearch, 1);
	search->engine = g_object_ref (self);
	search->cancellable = g_cancellable_new ();
	search->folder = folder;
	search->folder_len = strlen (folder);
	search->sql = nemo_search_win32_build_sql (folder, nemo_query_get_recurse (query),
						  file_pattern, nemo_query_get_use_file_regex (query),
						  content);
	search->matcher = nemo_search_name_matcher_new (query);
	search->show_hidden = nemo_query_get_show_hidden (query);
	search->skip_names = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);

	skip = nemo_config_get_strv (nemo_search_preferences, NEMO_PREFERENCES_SEARCH_SKIP_FOLDERS);
	for (i = 0; skip != NULL && skip[i] != NULL; i++) {
		if (g_path_is_absolute (skip[i])) {
			search->skip_paths = g_list_prepend (search->skip_paths, g_strdup (skip[i]));
		} else {
			/* cppcheck-suppress leakNoVarFunctionCall ; skip_names owns its keys and frees them */
			g_hash_table_add (search->skip_names, g_strdup (skip[i]));
		}
	}
	g_strfreev (skip);

	g_free (file_pattern);
	g_free (content);
	g_free (uri);
	g_clear_object (&location);

	self->details->active = search;

	thread = g_thread_new ("nemo-search-index", index_search_thread, search);
	g_thread_unref (thread);
}

static void
nemo_search_engine_win32_stop (NemoSearchEngine *engine)
{
	NemoSearchEngineWin32 *self = NEMO_SEARCH_ENGINE_WIN32 (engine);

	if (self->details->active != NULL) {
		g_cancellable_cancel (self->details->active->cancellable);
		self->details->active = NULL;
	}

	nemo_search_engine_stop (self->details->fallback);
}

static void
nemo_search_engine_win32_set_query (NemoSearchEngine *engine,
				    NemoQuery        *query)
{
	NemoSearchEngineWin32 *self = NEMO_SEARCH_ENGINE_WIN32 (engine);

	if (query != NULL) {
		g_object_ref (query);
	}
	g_clear_object (&self->details->query);
	self->details->query = query;
}

static void
finalize (GObject *object)
{
	NemoSearchEngineWin32 *self = NEMO_SEARCH_ENGINE_WIN32 (object);

	nemo_search_engine_win32_stop (NEMO_SEARCH_ENGINE (self));

	g_signal_handlers_disconnect_by_data (self->details->fallback, self);
	g_clear_object (&self->details->fallback);
	g_clear_object (&self->details->query);
	g_free (self->details);

	G_OBJECT_CLASS (nemo_search_engine_win32_parent_class)->finalize (object);
}

static void
nemo_search_engine_win32_class_init (NemoSearchEngineWin32Class *class)
{
	GObjectClass *gobject_class = G_OBJECT_CLASS (class);
	NemoSearchEngineClass *engine_class = NEMO_SEARCH_ENGINE_CLASS (class);

	gobject_class->finalize = finalize;

	engine_class->set_query = nemo_search_engine_win32_set_query;
	engine_class->start = nemo_search_engine_win32_start;
	engine_class->stop = nemo_search_engine_win32_stop;
}

static void
nemo_search_engine_win32_init (NemoSearchEngineWin32 *engine)
{
	NemoSearchEngine *fallback;

	engine->details = g_new0 (NemoSearchEngineWin32Details, 1);

	/* Whatever the walking engine reports is reported as this engine's own. */
	fallback = nemo_search_engine_advanced_new ();
	g_signal_connect_swapped (fallback, "hits-added", G_CALLBACK (nemo_search_engine_hits_added), engine);
	g_signal_connect_swapped (fallback, "hits-subtracted", G_CALLBACK (nemo_search_engine_hits_subtracted), engine);
	g_signal_connect_swapped (fallback, "finished", G_CALLBACK (nemo_search_engine_finished), engine);
	g_signal_connect_swapped (fallback, "error", G_CALLBACK (nemo_search_engine_error), engine);
	engine->details->fallback = fallback;
}

/* Returns: (transfer full): unref with g_object_unref */
NemoSearchEngine *
nemo_search_engine_win32_new (void)
{
	return g_object_new (NEMO_TYPE_SEARCH_ENGINE_WIN32, NULL);
}
