/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */

/* test-nemo-tool-start-win32.c - how the archive tools, the search
   converters and the thumbnailers are started on Windows.

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

/* Each of these used to open a console window, since the app has no console
 * and the tool it started was given one of its own. Stand-ins first on PATH
 * say whether they got a window. Through the real jobs: compress to rar, with
 * the tool's own words in the error when it fails and a stop that ends it
 * while it says nothing; unpack a rar that only the tool reads; a content
 * search through a converter; and a thumbnail from a thumbnailer program. */

#include <config.h>

#include <string.h>

#include <glib.h>
#include <glib/gstdio.h>
#include <gtk/gtk.h>
#include <windows.h>

#include <libnemo-private/nemo-archive.h>
#include <libnemo-private/nemo-desktop-thumbnail.h>
#include <libnemo-private/nemo-extract.h>
#include <libnemo-private/nemo-global-preferences.h>
#include <libnemo-private/nemo-progress-info.h>
#include <libnemo-private/nemo-progress-info-manager.h>
#include <libnemo-private/nemo-query.h>
#include <libnemo-private/nemo-search-engine-advanced.h>

#include "test-scratch.h"
#include "test-check.h"

#define JOB_SECONDS 20

static char *scratch;
static GtkWidget *window;
static gboolean job_done;
static gboolean job_ok;
static NemoProgressInfo *stopping;
static gint64 stopped_at;

static char *
report_of (const char *tool)
{
	g_autofree char *name = g_strconcat (tool, ".report", NULL);
	g_autofree char *path = g_build_filename (scratch, name, NULL);
	char *text = NULL;

	return g_file_get_contents (path, &text, NULL, NULL) ? text : g_strdup ("");
}

static gboolean
reported (const char *tool, const char *line)
{
	g_autofree char *text = report_of (tool);
	g_autofree char *wanted = g_strdup_printf ("%s\n", line);

	return strstr (text, wanted) != NULL;
}

static void
forget_report (const char *tool)
{
	g_autofree char *name = g_strconcat (tool, ".report", NULL);
	g_autofree char *path = g_build_filename (scratch, name, NULL);

	g_remove (path);
}

static void
archive_done (G_GNUC_UNUSED GFile *file, gboolean success, G_GNUC_UNUSED gpointer data)
{
	job_ok = success;
	job_done = TRUE;
	gtk_main_quit ();
}

static void
extract_done (G_GNUC_UNUSED GFile *dir, gboolean success, G_GNUC_UNUSED gpointer data)
{
	job_ok = success;
	job_done = TRUE;
	gtk_main_quit ();
}

static gboolean
give_up (G_GNUC_UNUSED gpointer data)
{
	gtk_main_quit ();
	return G_SOURCE_REMOVE;
}

static void
wait_for_job (void)
{
	guint timeout = g_timeout_add_seconds (JOB_SECONDS, give_up, NULL);

	gtk_main ();
	if (job_done) {
		g_source_remove (timeout);
	}
	check (job_done);
}

/* What the error dialogs a job left up said, closed as they are read. */
static char *
take_messages (void)
{
	GString *said = g_string_new (NULL);
	GList *windows, *l;

	while (g_main_context_iteration (NULL, FALSE)) {
	}

	windows = gtk_window_list_toplevels ();
	for (l = windows; l != NULL; l = l->next) {
		char *text = NULL;

		if (!GTK_IS_MESSAGE_DIALOG (l->data)) {
			continue;
		}
		g_object_get (l->data, "secondary-text", &text, NULL);
		g_string_append_printf (said, "%s\n", text != NULL ? text : "");
		g_free (text);
		gtk_widget_destroy (GTK_WIDGET (l->data));
	}
	g_list_free (windows);

	return g_string_free (said, FALSE);
}

static gboolean
compress (const char *name)
{
	g_autofree char *source = g_build_filename (scratch, "files", NULL);
	g_autofree char *target = g_build_filename (scratch, name, NULL);
	g_autoptr (GFile) from = g_file_new_for_path (source);
	g_autoptr (GFile) to = g_file_new_for_path (target);
	GList *sources = g_list_append (NULL, from);
	NemoArchiveOptions options;

	nemo_archive_options_init (&options);
	options.format = NEMO_ARCHIVE_FORMAT_RAR;
	check (nemo_archive_pick_backend (options.format, &options) == NEMO_ARCHIVE_BACKEND_RAR);

	job_done = job_ok = FALSE;
	nemo_archive_create (sources, to, &options, GTK_WINDOW (window), archive_done, NULL);
	wait_for_job ();

	nemo_archive_options_clear (&options);
	g_list_free (sources);
	return job_ok;
}

static void
check_compress (void)
{
	g_autofree char *said = NULL;
	g_autofree char *cwd = g_strdup_printf ("cwd=%s", scratch);

	forget_report ("rar");
	check (compress ("one.rar"));
	check (reported ("rar", "window=0"));
	check (reported ("rar", "bytes=0"));
	check (reported ("rar", cwd));
	check (reported ("rar", "arg=a"));
	g_free (take_messages ());

	/* Both outputs reach the error, which is all a tool has to say why. */
	g_setenv ("NEMO_FAKE_TOOL_FAIL", "1", TRUE);
	check (!compress ("two.rar"));
	g_unsetenv ("NEMO_FAKE_TOOL_FAIL");
	said = take_messages ();
	check (strstr (said, "fake out said no") != NULL);
	check (strstr (said, "fake err said no") != NULL);
}

static void
stop_once_going (NemoProgressInfo *info, G_GNUC_UNUSED gpointer data)
{
	/* Once the tool itself is going, not on progress from before it. */
	if (info == stopping && stopped_at == 0 && nemo_progress_info_get_progress (info) > 0 &&
	    reported ("rar", "arg=a")) {
		stopped_at = g_get_monotonic_time ();
		nemo_progress_info_cancel (info);
	}
}

static void
watch_new_progress (G_GNUC_UNUSED NemoProgressInfoManager *manager,
		    NemoProgressInfo *info,
		    G_GNUC_UNUSED gpointer data)
{
	if (stopping == NULL) {
		stopping = g_object_ref (info);
		g_signal_connect (info, "progress-changed", G_CALLBACK (stop_once_going), NULL);
	}
}

/* The tool says how far it got and then nothing more, so only a read that
   gives up on a stop lets the job end. */
static void
check_compress_stop (NemoProgressInfoManager *manager)
{
	gulong watch = g_signal_connect (manager, "new-progress-info",
					 G_CALLBACK (watch_new_progress), NULL);

	forget_report ("rar");
	g_setenv ("NEMO_FAKE_TOOL_SLEEP", "1", TRUE);
	check (!compress ("three.rar"));
	g_unsetenv ("NEMO_FAKE_TOOL_SLEEP");
	g_signal_handler_disconnect (manager, watch);

	check (stopped_at != 0);
	if (stopped_at != 0) {
		check (g_get_monotonic_time () - stopped_at < 10 * G_USEC_PER_SEC);
	}
	if (stopping != NULL) {
		g_signal_handlers_disconnect_by_data (stopping, NULL);
		g_clear_object (&stopping);
	}
	g_free (take_messages ());
}

static void
check_extract (void)
{
	g_autofree char *box = g_build_filename (scratch, "box.rar", NULL);
	g_autofree char *out = g_build_filename (scratch, "out", NULL);
	g_autoptr (GFile) archive = g_file_new_for_path (box);
	g_autoptr (GFile) dest = NULL;
	GList *archives;

	/* Not a rar libarchive can read, so it goes to the tool. */
	check (g_file_set_contents (box, "not an archive at all", -1, NULL));
	check (g_mkdir (out, 0755) == 0);
	dest = g_file_new_for_path (out);
	archives = g_list_append (NULL, archive);

	forget_report ("unrar");
	job_done = FALSE;
	nemo_extract_files (archives, dest, NEMO_EXTRACT_HERE, GTK_WINDOW (window), extract_done, NULL);
	wait_for_job ();
	g_list_free (archives);
	g_free (take_messages ());

	check (reported ("unrar", "window=0"));
	check (reported ("unrar", "bytes=0"));
	check (reported ("unrar", "arg=x"));
}

static gboolean search_done;
static GList *found;

static void
hits_added (G_GNUC_UNUSED NemoSearchEngine *engine, GList *hits, G_GNUC_UNUSED gpointer data)
{
	for (GList *l = hits; l != NULL; l = l->next) {
		FileSearchResult *result = l->data;

		found = g_list_prepend (found, g_path_get_basename (result->uri));
		file_search_result_free (result);
	}
}

static void
search_finished (G_GNUC_UNUSED NemoSearchEngine *engine, G_GNUC_UNUSED gpointer data)
{
	search_done = TRUE;
}

/* The converter's output is what the search reads, so finding the word
   proves its stdout came back. */
static void
check_search (void)
{
	g_autofree char *docs = g_build_filename (scratch, "docs", NULL);
	g_autofree char *doc = g_build_filename (docs, "t.nemofake", NULL);
	g_autofree char *uri = g_filename_to_uri (docs, NULL, NULL);
	NemoSearchEngine *engine = nemo_search_engine_advanced_new ();
	NemoQuery *query = nemo_query_new ();
	int spins = 0;

	check (g_mkdir (docs, 0755) == 0);
	/* Not text, or it would be read without the converter. A type nothing
	   shipped has a converter for, so only ours is tried. */
	check (g_file_set_contents (doc, "\xd0\xcf\x11\xe0\0\0\0\0", 8, NULL));
	g_setenv ("NEMO_FAKE_TOOL_SAYS", "alpha november zulu", TRUE);

	g_signal_connect (engine, "hits-added", G_CALLBACK (hits_added), NULL);
	g_signal_connect (engine, "finished", G_CALLBACK (search_finished), NULL);
	nemo_query_set_location (query, uri);
	nemo_query_set_content_pattern (query, "november");
	nemo_search_engine_set_query (engine, query);
	g_object_unref (query);

	nemo_search_engine_start (engine);
	while (!search_done && spins++ < 2000) {
		g_main_context_iteration (NULL, FALSE);
		g_usleep (10000);
	}

	check (search_done);
	check (g_list_length (found) == 1 && g_strcmp0 (found->data, "t.nemofake") == 0);
	check (reported ("nemo-fake-to-txt", "window=0"));
	check (reported ("nemo-fake-to-txt", "bytes=0"));

	g_list_free_full (found, g_free);
	found = NULL;
	g_object_unref (engine);
	g_unsetenv ("NEMO_FAKE_TOOL_SAYS");
}

static void
check_thumbnailer (void)
{
	g_autofree char *path = g_build_filename (scratch, "pic.fake", NULL);
	g_autofree char *uri = g_filename_to_uri (path, NULL, NULL);
	g_autofree char *input = g_strdup_printf ("arg=%s", path);
	NemoDesktopThumbnailFactory *factory;
	GdkPixbuf *pixbuf;

	check (g_file_set_contents (path, "picture", -1, NULL));

	factory = nemo_desktop_thumbnail_factory_new (NEMO_DESKTOP_THUMBNAIL_SIZE_LARGE);
	pixbuf = nemo_desktop_thumbnail_factory_generate_thumbnail_at_size (factory, uri,
									    "application/x-nemo-fake",
									    128, NULL);
	check (pixbuf != NULL);
	if (pixbuf != NULL) {
		check (gdk_pixbuf_get_width (pixbuf) == 3 && gdk_pixbuf_get_height (pixbuf) == 2);
		g_object_unref (pixbuf);
	}
	check (reported ("fake-thumb", "window=0"));
	check (reported ("fake-thumb", input));
	g_object_unref (factory);
}

static char *
place_tool (const char *fake, const char *bin, const char *name)
{
	g_autofree char *exe = g_strconcat (name, ".exe", NULL);
	char *path = g_build_filename (bin, exe, NULL);
	g_autoptr (GFile) from = g_file_new_for_path (fake);
	g_autoptr (GFile) to = g_file_new_for_path (path);

	check (g_file_copy (from, to, G_FILE_COPY_NONE, NULL, NULL, NULL, NULL));
	return path;
}

static void
write_text (const char *dir, const char *name, const char *text)
{
	g_autofree char *path = g_build_filename (dir, name, NULL);

	check (g_mkdir_with_parents (dir, 0755) == 0);
	check (g_file_set_contents (path, text, -1, NULL));
}

int
main (int argc, char *argv[])
{
	g_autofree char *bin = NULL, *rar = NULL, *unrar = NULL, *converter = NULL, *thumb = NULL;
	g_autofree char *path = NULL, *dir = NULL, *text = NULL, *files = NULL;
	NemoProgressInfoManager *manager;

	if (argc < 2) {
		g_printerr ("usage: %s <stand-in tool.exe>\n", argv[0]);
		return 1;
	}

	/* Before anything caches the real data folders. */
	scratch = test_scratch_config_home ("nemo-tool-start-XXXXXX");
	check (scratch != NULL);
	g_setenv ("NEMO_FAKE_TOOL_DIR", scratch, TRUE);
	/* Unpacking clears its own staging folder, which the guard would ask about. */
	g_setenv ("NEMO_TESTGUARD_ALL_DELETES", "0", TRUE);

	/* A space in the folder, so each program's path has to be quoted. */
	bin = g_build_filename (scratch, "tool bin", NULL);
	check (g_mkdir (bin, 0755) == 0);
	rar = place_tool (argv[1], bin, "rar");
	/* Unpacking looks for this one first, and a real one may be on PATH. */
	unrar = place_tool (argv[1], bin, "unrar");
	converter = place_tool (argv[1], bin, "nemo-fake-to-txt");
	thumb = place_tool (argv[1], bin, "fake-thumb");
	path = g_strconcat (bin, ";", g_getenv ("PATH"), NULL);
	g_setenv ("PATH", path, TRUE);

	files = g_build_filename (scratch, "files", NULL);
	write_text (files, "a.txt", "some text");

	dir = g_build_filename (g_get_user_data_dir (), NEMO_APP_SLUG, "search-helpers", NULL);
	text = g_strdup_printf ("[Nemo Search Helper]\nTryExec=%s;\nExec=nemo-fake-to-txt %%s\n"
				"MimeType=application/x-ext-nemofake;\nPriority=100\n", converter);
	write_text (dir, "fake.nemo_search_helper", text);
	g_free (dir);

	/* A bare name, as the shipped thumbnailers have. */
	dir = g_build_filename (g_get_user_data_dir (), "thumbnailers", NULL);
	write_text (dir, "fake.thumbnailer",
		    "[Thumbnailer Entry]\nExec=fake-thumb %i %o\nMimeType=application/x-nemo-fake;\n");

	if (!gtk_init_check (&argc, &argv)) {
		g_print ("SKIP: no display\n");
		return 77;
	}
	nemo_global_preferences_init ();
	manager = nemo_progress_info_manager_new ();
	window = gtk_window_new (GTK_WINDOW_TOPLEVEL);

	/* The app has no console of its own, which is when a tool got a window. */
	FreeConsole ();

	g_print ("compress\n");
	check_compress ();
	g_print ("compress stopped\n");
	check_compress_stop (manager);
	g_print ("unpack\n");
	check_extract ();
	g_print ("search\n");
	check_search ();
	g_print ("thumbnailer\n");
	check_thumbnailer ();

	/* Only goes once nothing runs from it, so the stopped one has ended. */
	check (g_remove (rar) == 0);

	gtk_widget_destroy (window);
	g_object_unref (manager);

	return failures == 0 ? 0 : 1;
}
