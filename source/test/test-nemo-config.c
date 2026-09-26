/* Exercises the settings store: typed reads and writes, defaults, the
 * detailed changed signal, property binding, and the on-disk round trip.
 * Runs against a throwaway config root. */

#include <config.h>

#include <locale.h>
#include <stdlib.h>
#include <string.h>
#include <gio/gio.h>
#include <glib/gstdio.h>
#include <gtk/gtk.h>

#include <libnemo-private/nemo-config.h>
#include <libnemo-private/nemo-config-keys.h>
#include <libnemo-private/nemo-global-preferences.h>

#include "test-scratch.h"
#include "test-check.h"

#ifdef __GLIBC__
#include <malloc.h>
#endif

static char *
read_file (void)
{
	char *path = nemo_config_get_path ();
	char *text = NULL;

	/* A read that fails leaves text NULL, which every caller already reads
	 * as "the file said nothing" - the point is not to pretend otherwise. */
	if (!g_file_get_contents (path, &text, NULL, NULL)) {
		text = NULL;
	}
	g_free (path);

	return text ? text : g_strdup ("");
}

/* The file also carries a commented list of everything not set, which names
   every key there is - so a check for what was stored has to read the settings
   alone. */
static char *
read_settings (void)
{
	char *text = read_file ();
	char *rule = strstr (text, "\n# ------");

	if (rule != NULL) {
		*rule = '\0';
	}

	return text;
}

/* change signal */

static int   changed_count;
static char *changed_key;

static void
on_changed (NemoConfigGroup *group, const char *key, gpointer data)
{
	changed_count++;
	g_free (changed_key);
	changed_key = g_strdup (key);
}

/* bind target */

/* Iterate the main context until @count moves past @from, or about two
   seconds pass. The file monitor is async. */
static void
wait_for_change (int *count, int from)
{
	int spins = 0;

	while (*count == from && spins++ < 1000) {
		g_main_context_iteration (NULL, FALSE);
		g_usleep (2000);
	}
}

/* Replace the settings file as an editor would, and wait for @group to say
   @key changed. */
static void
rewrite_and_wait (NemoConfigGroup *group, const char *key, const char *text)
{
	char  *path = nemo_config_get_path ();
	char  *detailed = g_strconcat ("changed::", key, NULL);
	gulong id;

	changed_count = 0;
	id = g_signal_connect (group, detailed, G_CALLBACK (on_changed), NULL);
	check (g_file_set_contents (path, text, -1, NULL));
	wait_for_change (&changed_count, 0);
	check (changed_count >= 1);
	g_signal_handler_disconnect (group, id);

	g_free (detailed);
	g_free (path);
}

static void
test_defaults (NemoConfigGroup *prefs, NemoConfigGroup *list_view)
{
	char **cols;

	/* Straight from the table, with nothing on disk yet. */
	check (nemo_config_get_boolean (prefs, "show-hidden-files") == FALSE);
	check (nemo_config_get_boolean (prefs, "always-use-browser") == TRUE);
	check (nemo_config_get_enum (prefs, "click-policy") == NEMO_CLICK_POLICY_DOUBLE);

	cols = nemo_config_get_strv (list_view, "default-visible-columns");
	check (g_strv_length (cols) > 0);
	check (g_strcmp0 (cols[0], "name") == 0);
	g_strfreev (cols);
}

/* Defaults chosen on purpose rather than inherited, so a change to the key
 * table that quietly puts one back is caught. Media handling all starts off;
 * a new install opens in list view with ISO dates, asks before a move to the
 * trash, and shows owner, group and permissions where the platform has them.
 * Reads the effective value on a root with nothing stored. */
static void
test_product_defaults (void)
{
	NemoConfigGroup *media = nemo_config_get_group ("media-handling");
	NemoConfigGroup *prefs = nemo_config_get_group ("preferences");
	NemoConfigGroup *list_view = nemo_config_get_group ("list-view");
	char           **cols;

	check (nemo_config_get_boolean (media, GNOME_DESKTOP_MEDIA_HANDLING_AUTOMOUNT) == FALSE);
	check (nemo_config_get_boolean (media, GNOME_DESKTOP_MEDIA_HANDLING_AUTOMOUNT_OPEN) == FALSE);
	check (nemo_config_get_boolean (prefs, NEMO_PREFERENCES_MEDIA_HANDLING_DETECT_CONTENT) == FALSE);

	check (nemo_config_get_enum (prefs, NEMO_PREFERENCES_DEFAULT_FOLDER_VIEWER) ==
	       NEMO_DEFAULT_FOLDER_VIEWER_LIST_VIEW);
	check (nemo_config_get_enum (prefs, NEMO_PREFERENCES_DATE_FORMAT) == NEMO_DATE_FORMAT_ISO);
	check (nemo_config_get_boolean (prefs, NEMO_PREFERENCES_CONFIRM_MOVE_TO_TRASH) == TRUE);

	cols = nemo_config_get_strv (list_view, NEMO_PREFERENCES_LIST_VIEW_DEFAULT_VISIBLE_COLUMNS);
	check (g_strv_contains ((const char *const *) cols, "owner"));
#ifndef G_OS_WIN32
	/* Windows has no group or mode bits to show. */
	check (g_strv_contains ((const char *const *) cols, "group"));
	check (g_strv_contains ((const char *const *) cols, "permissions"));
#endif
	g_strfreev (cols);
}

static void
test_scalars (NemoConfigGroup *prefs, NemoConfigGroup *window_state)
{
	char *s;

	nemo_config_set_boolean (prefs, "show-hidden-files", TRUE);
	check (nemo_config_get_boolean (prefs, "show-hidden-files") == TRUE);

	nemo_config_set_int (window_state, "sidebar-width", 321);
	check (nemo_config_get_int (window_state, "sidebar-width") == 321);

	nemo_config_set_string (window_state, "geometry", "800x600+10+20");
	s = nemo_config_get_string (window_state, "geometry");
	check (g_strcmp0 (s, "800x600+10+20") == 0);
	g_free (s);

	/* Enums round-trip through their nick, not their number. */
	nemo_config_set_enum (prefs, "click-policy", NEMO_CLICK_POLICY_SINGLE);
	check (nemo_config_get_enum (prefs, "click-policy") == NEMO_CLICK_POLICY_SINGLE);
}

static void
test_strv (NemoConfigGroup *list_view)
{
	const char *set[] = { "name", "size", NULL };
	const char *none[] = { NULL };
	char      **got;

	nemo_config_set_strv (list_view, "default-visible-columns", set);
	got = nemo_config_get_strv (list_view, "default-visible-columns");
	check (g_strv_length (got) == 2);
	check (g_strcmp0 (got[1], "size") == 0);
	g_strfreev (got);

	/* An emptied list must stay empty, not spring back to the default. */
	nemo_config_set_strv (list_view, "default-visible-columns", none);
	got = nemo_config_get_strv (list_view, "default-visible-columns");
	check (g_strv_length (got) == 0);
	g_strfreev (got);
}

/* A value equal to the default is dropped, so a later change to that
 * default still reaches the user (and the file stays small). */
static void
test_default_not_stored (NemoConfigGroup *prefs)
{
	char *text;

	nemo_config_set_boolean (prefs, "always-use-browser", TRUE);  /* == default */
	nemo_config_flush ();

	text = read_settings ();
	check (strstr (text, "always-use-browser") == NULL);
	g_free (text);

	nemo_config_set_boolean (prefs, "always-use-browser", FALSE); /* != default */
	nemo_config_flush ();

	text = read_settings ();
	check (strstr (text, "always-use-browser") != NULL);
	g_free (text);

	/* reset drops it again */
	nemo_config_reset (prefs, "always-use-browser");
	check (nemo_config_get_boolean (prefs, "always-use-browser") == TRUE);
}

static void
test_changed_signal (NemoConfigGroup *prefs)
{
	gulong id;

	changed_count = 0;
	id = g_signal_connect (prefs, "changed::show-hidden-files",
	                       G_CALLBACK (on_changed), NULL);

	nemo_config_set_boolean (prefs, "show-hidden-files", FALSE);
	check (changed_count == 1);
	check (g_strcmp0 (changed_key, "show-hidden-files") == 0);

	/* A different key must not reach a detailed handler. */
	nemo_config_set_boolean (prefs, "sort-directories-first", FALSE);
	check (changed_count == 1);

	g_signal_handler_disconnect (prefs, id);
	g_clear_pointer (&changed_key, g_free);
}

static void
test_bind (NemoConfigGroup *prefs)
{
	GtkWidget *toggle = gtk_check_button_new ();

	g_object_ref_sink (toggle);

	nemo_config_set_boolean (prefs, "show-hidden-files", TRUE);
	nemo_config_bind (prefs, "show-hidden-files", toggle, "active",
	                  NEMO_CONFIG_BIND_DEFAULT);

	/* config -> widget, applied at bind time */
	check (gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (toggle)) == TRUE);

	/* config -> widget, on change */
	nemo_config_set_boolean (prefs, "show-hidden-files", FALSE);
	check (gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (toggle)) == FALSE);

	/* widget -> config */
	gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (toggle), TRUE);
	check (nemo_config_get_boolean (prefs, "show-hidden-files") == TRUE);

	/* the binding must not outlive the widget */
	g_object_unref (toggle);
	nemo_config_set_boolean (prefs, "show-hidden-files", FALSE);
}

/* The preferences dialog binds every combo and radio through a mapping that
 * fills in the nick and leaves the number alone. Taking the number wrote the
 * zero-valued nick whatever was picked - for the executable-text setting that
 * meant "run it" no matter what the dialog showed. */
static gboolean
viewer_get_mapping (GValue *value, const NemoConfigValue *config_value, gpointer data)
{
	g_value_set_boolean (value, g_strcmp0 (config_value->s, "compact-view") == 0);
	return TRUE;
}

static gboolean
viewer_set_mapping (const GValue *value, NemoConfigValue *config_value, gpointer data)
{
	config_value->s = g_strdup (g_value_get_boolean (value) ? "compact-view"
	                                                        : "list-view");
	return TRUE;
}

static void
test_enum_bind_by_nick (NemoConfigGroup *prefs)
{
	GtkWidget *toggle = gtk_check_button_new ();
	char      *text;

	g_object_ref_sink (toggle);
	nemo_config_bind_with_mapping (prefs, "default-folder-viewer",
	                               toggle, "active", NEMO_CONFIG_BIND_DEFAULT,
	                               viewer_get_mapping, viewer_set_mapping,
	                               NULL, NULL);

	gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (toggle), TRUE);
	nemo_config_flush ();

	text = read_settings ();
	check (strstr (text, "default-folder-viewer: compact-view") != NULL);
	/* icon-view is the zero-valued nick, i.e. what the bug stored */
	check (strstr (text, "icon-view") == NULL);
	g_free (text);

	g_object_unref (toggle);
	nemo_config_reset (prefs, "default-folder-viewer");
}

/* Setting a comment appends a line rather than replacing one, so re-applying
 * it on every write grew the same comment without bound. */
static void
test_comment_written_once (NemoConfigGroup *prefs)
{
	const char *summary = "# Widest a tab may get, as a percentage of the tab strip";
	char       *text, *at;
	int         seen = 0;

	nemo_config_set_int (prefs, "tab-width-max-percent", 21);
	nemo_config_set_int (prefs, "tab-width-max-percent", 22);
	nemo_config_set_int (prefs, "tab-width-max-percent", 23);
	nemo_config_flush ();

	text = read_file ();
	for (at = text; (at = strstr (at, summary)) != NULL; at++)
		seen++;
	check (seen == 1);
	g_free (text);
}

static void
test_persistence (void)
{
	char *text;

	nemo_config_set_boolean (nemo_config_get_group ("preferences"),
	                         "show-hidden-files", TRUE);
	nemo_config_set_boolean (nemo_config_get_group ("preferences"),
	                         "confirm-drag-move", FALSE);
	nemo_config_flush ();

	text = read_settings ();
	/* Written as SHCL, grouped, with the summary carried across as a comment. */
	check (strstr (text, "preferences:") != NULL);
	check (strstr (text, "show-hidden-files: true") != NULL);
	check (strstr (text, "# Ask before a drop moves files") != NULL);
	g_free (text);
}

/* An external edit is picked up and reported as a per-key change. */
static void
test_external_edit (NemoConfigGroup *prefs)
{
	char  *path = nemo_config_get_path ();
	int    spins = 0;

	nemo_config_set_boolean (prefs, "show-hidden-files", FALSE);
	nemo_config_flush ();

	changed_count = 0;
	g_signal_connect (prefs, "changed::show-hidden-files",
	                  G_CALLBACK (on_changed), NULL);

	check (g_file_set_contents (path,
	                           "preferences:\n\tshow-hidden-files: true\n", -1, NULL));

	/* the monitor is async; give it a bounded chance to fire */
	while (changed_count == 0 && spins++ < 200) {
		g_main_context_iteration (NULL, FALSE);
		g_usleep (10000);
	}

	check (changed_count >= 1);
	check (nemo_config_get_boolean (prefs, "show-hidden-files") == TRUE);

	/* A key that was already in the file, edited to a different value. The
	 * diff has to compare the values, not just notice a key appear or go. */
	changed_count = 0;
	spins = 0;
	check (g_file_set_contents (path,
	                           "preferences:\n\tshow-hidden-files: false\n", -1, NULL));

	while (changed_count == 0 && spins++ < 200) {
		g_main_context_iteration (NULL, FALSE);
		g_usleep (10000);
	}

	check (changed_count >= 1);
	check (nemo_config_get_boolean (prefs, "show-hidden-files") == FALSE);

	g_free (path);
}

/* A file that exists but cannot be read (share lock, or the delete half of a
 * non-atomic external save) must not swap defaults into memory - a queued
 * save would then wipe the real file. Simulated by putting a directory at
 * the config path, which fails the read on any platform and any uid. */
static void
test_unreadable_file_kept (NemoConfigGroup *prefs)
{
	char *path = nemo_config_get_path ();
	int   spins = 0;

	nemo_config_set_boolean (prefs, "show-hidden-files", TRUE);
	nemo_config_flush ();

	g_remove (path);
	g_mkdir (path, 0700);

	/* let the monitor's DELETED/CREATED events come through */
	while (spins++ < 100) {
		g_main_context_iteration (NULL, FALSE);
		g_usleep (10000);
	}

	check (nemo_config_get_boolean (prefs, "show-hidden-files") == TRUE);

	g_rmdir (path);
	nemo_config_set_boolean (prefs, "show-hidden-files", FALSE);
	nemo_config_flush ();
	g_free (path);
}

/* strstr and even g_strstr_len stop at an embedded NUL, which is the very
 * byte under test - so search the raw buffer by hand. */
static gboolean
buf_contains (const char *buf, gsize len, const char *needle)
{
	gsize nlen = strlen (needle);
	gsize i;

	for (i = 0; nlen > 0 && i + nlen <= len; i++)
		if (memcmp (buf + i, needle, nlen) == 0)
			return TRUE;
	return FALSE;
}

/* SHCL is NUL-transparent, so a NUL that came in from the file must survive
 * the next save instead of truncating everything after it. */
static void
test_nul_survives_save (NemoConfigGroup *window_state)
{
	char        *path = nemo_config_get_path ();
	const char   before[] = "window-state:\n\tgeometry: \"a\0b\"\n\tsidebar-width: 444\n";
	char        *text = NULL;
	gsize        len = 0;
	int          spins = 0;

	changed_count = 0;
	g_signal_connect (window_state, "changed::sidebar-width",
	                  G_CALLBACK (on_changed), NULL);

	check (g_file_set_contents (path, before, sizeof (before) - 1, NULL));
	while (changed_count == 0 && spins++ < 200) {
		g_main_context_iteration (NULL, FALSE);
		g_usleep (10000);
	}
	check (nemo_config_get_int (window_state, "sidebar-width") == 444);

	nemo_config_set_int (window_state, "sidebar-width", 445);
	nemo_config_flush ();

	check (g_file_get_contents (path, &text, &len, NULL));
	check (text != NULL && memchr (text, '\0', len) != NULL);
	check (text != NULL && buf_contains (text, len, "sidebar-width"));
	g_free (text);
	g_free (path);
}

/* An implausibly large file must be refused, not parsed. The in-memory
 * settings must survive the refusal. */
static void
test_oversized_file_refused (NemoConfigGroup *prefs)
{
	char  *path = nemo_config_get_path ();
	char  *blob;
	gsize  n = 9 * 1024 * 1024;   /* over the 8 MiB cap */
	int    spins = 0;

	nemo_config_set_boolean (prefs, "show-hidden-files", TRUE);
	nemo_config_flush ();

	blob = g_malloc (n);
	memset (blob, 'x', n);
	check (g_file_set_contents (path, blob, n, NULL));
	g_free (blob);

	/* let the monitor fire and hit load_locked's cap */
	while (spins++ < 100) {
		g_main_context_iteration (NULL, FALSE);
		g_usleep (10000);
	}

	/* value preserved, process still alive */
	check (nemo_config_get_boolean (prefs, "show-hidden-files") == TRUE);

	g_remove (path);
	nemo_config_set_boolean (prefs, "show-hidden-files", FALSE);
	nemo_config_flush ();
	g_free (path);
}

/* A key written twice by hand used to read as an empty list, which opened the
 * list view with no columns at all. It falls back to the default instead, and
 * so does a scalar. The warning it logs is expected. */
static void
test_duplicate_key_falls_back (NemoConfigGroup *prefs, NemoConfigGroup *list_view)
{
	char **cols;

	rewrite_and_wait (list_view, "default-visible-columns",
	                  "list-view:\n"
	                  "\tdefault-visible-columns: name, size\n"
	                  "\tdefault-visible-columns: type\n"
	                  "preferences:\n"
	                  "\tshow-hidden-files: true\n"
	                  "\tshow-hidden-files: on\n");

	cols = nemo_config_get_strv (list_view, "default-visible-columns");
	check (g_strv_length (cols) > 1);
	check (cols[0] != NULL && g_strcmp0 (cols[0], "name") == 0);
	check (g_strv_contains ((const char *const *) cols, "owner"));
	g_strfreev (cols);

	check (nemo_config_get_boolean (prefs, "show-hidden-files") == FALSE);

	nemo_config_reset (list_view, "default-visible-columns");
	nemo_config_reset (prefs, "show-hidden-files");
	nemo_config_flush ();
}

/* A change is saved a couple of seconds after it is made. An external edit
 * arriving inside that window used to reload the file over it, so the change
 * was lost. It is carried across the reload and saved with the rest. */
static void
test_pending_change_survives_reload (NemoConfigGroup *prefs, NemoConfigGroup *window_state)
{
	char *text;

	nemo_config_flush ();
	nemo_config_set_int (window_state, "sidebar-width", 555);

	/* No flush: the change is still only in memory. */
	rewrite_and_wait (prefs, "show-hidden-files",
	                  "preferences:\n\tshow-hidden-files: true\n");

	check (nemo_config_get_boolean (prefs, "show-hidden-files") == TRUE);
	check (nemo_config_get_int (window_state, "sidebar-width") == 555);

	nemo_config_flush ();
	text = read_settings ();
	check (strstr (text, "sidebar-width: 555") != NULL);
	check (strstr (text, "show-hidden-files: true") != NULL);
	g_free (text);

	nemo_config_reset (window_state, "sidebar-width");
	nemo_config_reset (prefs, "show-hidden-files");
	nemo_config_flush ();
}

/* A count of its own: an earlier test leaves on_changed connected to the
   same key. */
static int      thread_changes;
static GThread *changed_on;

static void
on_changed_note_thread (NemoConfigGroup *group, const char *key, gpointer data)
{
	thread_changes++;
	changed_on = g_thread_self ();
}

static gpointer
set_from_worker (gpointer data)
{
	nemo_config_set_boolean (data, "show-hidden-files", TRUE);
	return NULL;
}

/* File operations set favorites from worker threads, and every handler touches
 * widgets, so a change is always announced on the main thread. */
static void
test_changed_on_main_thread (NemoConfigGroup *prefs)
{
	GThread *worker;
	gulong   id;

	thread_changes = 0;
	changed_on = NULL;
	id = g_signal_connect (prefs, "changed::show-hidden-files",
	                       G_CALLBACK (on_changed_note_thread), NULL);

	worker = g_thread_new ("config-set", set_from_worker, prefs);
	g_thread_join (worker);

	/* Not delivered on the worker, and delivered once the main loop runs. */
	check (thread_changes == 0);
	wait_for_change (&thread_changes, 0);
	check (thread_changes == 1);
	check (changed_on == g_thread_self ());
	check (nemo_config_get_boolean (prefs, "show-hidden-files") == TRUE);

	g_signal_handler_disconnect (prefs, id);
	nemo_config_reset (prefs, "show-hidden-files");
	nemo_config_flush ();
}

/* Windows paths hold backslashes and spaces. Before SHCL 3 a backslash in bare
 * text was an escape, so a UNC path read back doubled; each has to come back
 * byte for byte after a save and a reload from the file. */
static void
test_backslash_paths_round_trip (NemoConfigGroup *prefs, NemoConfigGroup *window_state)
{
	const char *paths[] = { "C:\\Users\\x\\a b", "\\\\srv\\share\\d", "C:\\", NULL };
	int         i;

	for (i = 0; paths[i] != NULL; i++) {
		char *text, *with_edit, *got, *want_width;

		nemo_config_set_string (prefs, "bulk-rename-tool", paths[i]);
		nemo_config_flush ();

		/* The same file with one more line, so it is reloaded from disk. */
		text = read_file ();
		want_width = g_strdup_printf ("%d", 600 + i);
		with_edit = g_strconcat (text, "\nwindow-state.sidebar-width: ", want_width, "\n", NULL);
		rewrite_and_wait (window_state, "sidebar-width", with_edit);
		check (nemo_config_get_int (window_state, "sidebar-width") == 600 + i);

		got = nemo_config_get_string (prefs, "bulk-rename-tool");
		if (g_strcmp0 (got, paths[i]) != 0) {
			g_printerr ("FAIL %s:%d: %s came back as %s\n",
			            __FILE__, __LINE__, paths[i], got);
			failures++;
		}

		g_free (got);
		g_free (with_edit);
		g_free (want_width);
		g_free (text);
		nemo_config_reset (window_state, "sidebar-width");
		nemo_config_flush ();
	}

	nemo_config_reset (prefs, "bulk-rename-tool");
	nemo_config_flush ();
}

/* About 3 KB of text, different for each n. */
static char *
long_value (int n)
{
	GString *text = g_string_new (NULL);

	while (text->len < 3000)
		g_string_append_printf (text, "part-%d-%zu/", n, text->len);

	return g_string_free (text, FALSE);
}

/* SHCL 2 handed every read out of an arena only a full reparse gave back, so
 * the store rebuilt itself past 256 KB. SHCL 3 lets each read go once it is
 * copied out. So reloads well past that size still read and still announce,
 * and a long run of reads does not grow the heap by what it read. A hundred
 * rewrites of 3 KB, since each one waits on the file monitor. */
static void
test_many_reloads (NemoConfigGroup *window_state)
{
	int i;
	int wrong = 0;

	for (i = 0; i < 100; i++) {
		char *value = long_value (i);
		char *text = g_strconcat ("window-state:\n\tgeometry: ", value, "\n", NULL);
		char *got;

		rewrite_and_wait (window_state, "geometry", text);
		got = nemo_config_get_string (window_state, "geometry");
		if (g_strcmp0 (got, value) != 0)
			wrong++;

		g_free (got);
		g_free (text);
		g_free (value);
	}
	check (wrong == 0);

#ifdef __GLIBC__
	{
		NemoConfigGroup  *list_view = nemo_config_get_group ("list-view");
		const char       *names[101];
		char            **owned = g_new0 (char *, 101);
		struct mallinfo2  before, after;
		size_t            used_before, used_after;

		/* A list read is where SHCL 3 still hands out arena memory. */
		for (i = 0; i < 100; i++) {
			owned[i] = g_strdup_printf ("column-%d", i);
			names[i] = owned[i];
		}
		names[100] = NULL;
		nemo_config_set_strv (list_view, "default-visible-columns", names);

		before = mallinfo2 ();
		for (i = 0; i < 20000; i++)
			g_strfreev (nemo_config_get_strv (list_view, "default-visible-columns"));
		after = mallinfo2 ();

		/* 20000 reads of 100 entries is about 32 MB if nothing is given back. */
		used_before = before.uordblks + before.hblkhd;
		used_after = after.uordblks + after.hblkhd;
		if (used_after > used_before && used_after - used_before > 4 * 1024 * 1024) {
			g_printerr ("FAIL %s:%d: 20000 list reads grew the heap by %zu bytes\n",
			            __FILE__, __LINE__, used_after - used_before);
			failures++;
		}

		g_strfreev (owned);
		nemo_config_reset (list_view, "default-visible-columns");
	}
#endif

	nemo_config_reset (window_state, "geometry");
	nemo_config_flush ();
}

/* SHCL spells a float with a dot whatever the locale. Under a comma-decimal
 * locale every float read used to fail back to its default, and the file got
 * the comma. Needs such a locale installed; without one this case is skipped
 * and the rest of the test still runs. */
static void
test_float_under_comma_locale (void)
{
	static const char *const candidates[] = {
		"de_DE.UTF-8", "de_DE.utf8", "de_DE", "fr_FR.UTF-8", "fr_FR.utf8",
		"nl_NL.UTF-8", "ru_RU.UTF-8", "German_Germany.1252", "de-DE", NULL
	};
	NemoConfigGroup *cache = nemo_config_get_group ("file-cache");
	char            *old = g_strdup (setlocale (LC_NUMERIC, NULL));
	char            *text;
	const char      *used = NULL;
	int              i;

	for (i = 0; candidates[i] != NULL && used == NULL; i++) {
		if (setlocale (LC_NUMERIC, candidates[i]) != NULL &&
		    strcmp (localeconv ()->decimal_point, ",") == 0)
			used = candidates[i];
	}

	if (used == NULL) {
		g_print ("SKIP: float under a comma-decimal locale: none installed\n");
		setlocale (LC_NUMERIC, old);
		g_free (old);
		return;
	}

	nemo_config_set_double (cache, "max-size-gib", 1.5);
	check (nemo_config_get_double (cache, "max-size-gib") == 1.5);
	nemo_config_flush ();

	text = read_settings ();
	check (strstr (text, "max-size-gib: 1.5") != NULL);
	g_free (text);

	rewrite_and_wait (cache, "max-size-gib", "file-cache:\n\tmax-size-gib: 2.25\n");
	check (nemo_config_get_double (cache, "max-size-gib") == 2.25);

	setlocale (LC_NUMERIC, old);
	g_free (old);
	nemo_config_reset (cache, "max-size-gib");
	nemo_config_flush ();
}

/* The accessors take an interned "group.key" held in key-table order rather
   than building the string per read, so a key added or moved out of step with
   that table would quietly read and write a neighbor's value. Give every key
   a value of its own, then read the lot back. Runs last: it leaves the whole
   table set. */
static void
test_keys_keep_their_own_values (void)
{
	guint i;

	for (i = 0; nemo_config_keys[i].key != NULL; i++) {
		const NemoConfigKey *k = &nemo_config_keys[i];
		NemoConfigGroup     *g = nemo_config_get_group (k->group);
		char                *marker;

		switch (k->type) {
		case NEMO_CONFIG_BOOL:
			nemo_config_set_boolean (g, k->key, g_strcmp0 (k->def, "true") != 0);
			break;
		case NEMO_CONFIG_INT:
			nemo_config_set_int64 (g, k->key, 100000 + i);
			break;
		case NEMO_CONFIG_FLOAT:
			nemo_config_set_double (g, k->key, 100000.0 + i);
			break;
		case NEMO_CONFIG_STRING:
			marker = g_strdup_printf ("marker-%u", i);
			nemo_config_set_string (g, k->key, marker);
			g_free (marker);
			break;
		case NEMO_CONFIG_STRING_LIST: {
			const char *one[2];

			marker = g_strdup_printf ("marker-%u", i);
			one[0] = marker;
			one[1] = NULL;
			nemo_config_set_strv (g, k->key, one);
			g_free (marker);
			break;
		}
		case NEMO_CONFIG_ENUM:
			/* The last nick, so a key whose default is the first one moves. */
			if (k->enum_values != NULL) {
				const NemoConfigEnumValue *v;

				for (v = k->enum_values; v[1].nick != NULL; v++)
					;
				nemo_config_set_enum (g, k->key, v->value);
			}
			break;
		}
	}

	nemo_config_flush ();

	for (i = 0; nemo_config_keys[i].key != NULL; i++) {
		const NemoConfigKey *k = &nemo_config_keys[i];
		NemoConfigGroup     *g = nemo_config_get_group (k->group);
		char                *want, *got;
		char               **list;

		switch (k->type) {
		case NEMO_CONFIG_BOOL:
			check (nemo_config_get_boolean (g, k->key) ==
			       (g_strcmp0 (k->def, "true") != 0));
			break;
		case NEMO_CONFIG_INT:
			check (nemo_config_get_int64 (g, k->key) == (gint64) (100000 + i));
			break;
		case NEMO_CONFIG_FLOAT:
			check (nemo_config_get_double (g, k->key) == 100000.0 + i);
			break;
		case NEMO_CONFIG_STRING:
			want = g_strdup_printf ("marker-%u", i);
			got  = nemo_config_get_string (g, k->key);
			check (g_strcmp0 (got, want) == 0);
			g_free (got);
			g_free (want);
			break;
		case NEMO_CONFIG_STRING_LIST:
			want = g_strdup_printf ("marker-%u", i);
			list = nemo_config_get_strv (g, k->key);
			check (g_strv_length (list) == 1 && g_strcmp0 (list[0], want) == 0);
			g_strfreev (list);
			g_free (want);
			break;
		case NEMO_CONFIG_ENUM:
			if (k->enum_values != NULL) {
				const NemoConfigEnumValue *v;

				for (v = k->enum_values; v[1].nick != NULL; v++)
					;
				check (nemo_config_get_enum (g, k->key) == v->value);
			}
			break;
		}
	}
}

int
main (int argc, char *argv[])
{
	char            *tmp;
	NemoConfigGroup *prefs, *list_view, *window_state;

	tmp = test_scratch_config_home ("nemo-config-test-XXXXXX");

	gtk_init (&argc, &argv);

	/* GDK itself logs criticals with no monitor, and those would be fatal. */
	if (gdk_display_get_n_monitors (gdk_display_get_default ()) == 0) {
		g_print ("SKIP: no monitor\n");
		return 77;
	}

	/* Several checks below assert that a misuse is refused. Without this a
	   critical is just printed and the test carries on reporting a pass. */
	g_log_set_always_fatal (G_LOG_LEVEL_CRITICAL);

	nemo_config_init ();

	prefs        = nemo_config_get_group ("preferences");
	list_view    = nemo_config_get_group ("list-view");
	window_state = nemo_config_get_group ("window-state");

	test_defaults (prefs, list_view);
	test_product_defaults ();
	test_scalars (prefs, window_state);
	test_strv (list_view);
	test_default_not_stored (prefs);
	test_changed_signal (prefs);
	test_bind (prefs);
	test_enum_bind_by_nick (prefs);
	test_comment_written_once (prefs);
	test_persistence ();
	test_external_edit (prefs);
	test_unreadable_file_kept (prefs);
	test_nul_survives_save (window_state);
	test_oversized_file_refused (prefs);
	test_duplicate_key_falls_back (prefs, list_view);
	test_pending_change_survives_reload (prefs, window_state);
	test_changed_on_main_thread (prefs);
	test_backslash_paths_round_trip (prefs, window_state);
	test_many_reloads (window_state);
	test_float_under_comma_locale ();
	test_keys_keep_their_own_values ();

	nemo_config_shutdown ();
	g_free (tmp);

	if (failures == 0)
		g_print ("nemo-config: all checks passed\n");

	return failures == 0 ? 0 : 1;
}
