/* A settings change in one running copy shows in the others before the copy
 * that made it saves it, so not through the file. 2 copies of this test run
 * as the app does: one settings file, one bus, a slot each, and settings
 * shared through nemo-instances. Checked:
 *   - a set and a reset reach the other copy while the file still has the old
 *     value, and its handler runs once, not again when the file catches up
 *   - the copy that only took the change never writes the file, even when
 *     the copy that made it dies before its save
 *   - both copies setting one key at the same moment end with the same value,
 *     in memory and on disk, also when the one that lost saved last, and when
 *     its message came after its save
 *   - a hand edit read before the save keeps the change that came over
 *   - a change made just before its copy quits still arrives
 * Starts a bus under dbus-run-session where the environment has none. */

#include <config.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <gio/gio.h>

#ifndef G_OS_WIN32
#include <unistd.h>
#endif

#include <libnemo-private/nemo-config.h>
#include <libnemo-private/nemo-file-utilities.h>
#include <libnemo-private/nemo-instances.h>

/* The implementation is in the store. */
#include "shcl.h"

#include "test-scratch.h"
#include "test-check.h"

#define BOOL_KEY   "always-show-tabs"
#define STRING_KEY "bulk-rename-tool"
#define LIST_KEY   "image-viewers-with-external-sort"
#define EDIT_KEY   "confirm-drag-copy"
/* The save waits 2 s after a change. Anything slower than that came through
   the file. */
#define SAVE_DELAY_MS 2000
#define WAIT_SECONDS  15

/* The copy */

static GHashTable *change_counts;

static void
say (const char *format, ...)
{
	va_list args;

	va_start (args, format);
	vprintf (format, args);
	va_end (args);
	putchar ('\n');
	fflush (stdout);
}

static char *
value_text (NemoConfigGroup *group, const char *key)
{
	if (strcmp (key, BOOL_KEY) == 0 || strcmp (key, EDIT_KEY) == 0) {
		return g_strdup (nemo_config_get_boolean (group, key) ? "true" : "false");
	}
	if (strcmp (key, LIST_KEY) == 0) {
		char **values = nemo_config_get_strv (group, key);
		char *text = g_strjoinv (",", values);

		g_strfreev (values);
		return text;
	}
	return nemo_config_get_string (group, key);
}

static void
on_changed (NemoConfigGroup *group, const char *key, G_GNUC_UNUSED gpointer data)
{
	char *value = value_text (group, key);
	guint n = GPOINTER_TO_UINT (g_hash_table_lookup (change_counts, key)) + 1;

	g_hash_table_insert (change_counts, g_strdup (key), GUINT_TO_POINTER (n));
	say ("changed %s %s", key, value);
	g_free (value);
}

typedef struct {
	GMainLoop *loop;
	GDBusConnection *bus;
	guint shared_id, slots, settings_id;
	char *settings;
	/* What it would have sent while muted, or NULL when it isn't. */
	GPtrArray *muted;
} Copy;

static Copy copy;

static char *file_value (const char *path, const char *key);

static void
send_unless_muted (GVariant *changes, G_GNUC_UNUSED gpointer data)
{
	if (copy.muted != NULL) {
		g_ptr_array_add (copy.muted, g_variant_ref (changes));
		return;
	}
	nemo_instance_send_to_others (copy.bus, "org.NemoAnywhere.Settings", "TakeSettings", changes);
}

/* Blocks, so nothing from the bus or the file watch is taken meanwhile. */
static void
wait_until_file_has (const char *key, const char *value)
{
	char *line_key = g_strdup_printf ("preferences.%s", key);
	gint64 end = g_get_monotonic_time () + 60 * G_USEC_PER_SEC;

	for (;;) {
		char *now = file_value (copy.settings, line_key);
		gboolean has = g_strcmp0 (now, value) == 0;

		g_free (now);
		if (has || g_get_monotonic_time () > end) {
			break;
		}
		g_usleep (20 * 1000);
	}
	g_free (line_key);
}

static gboolean
run_command (gpointer data)
{
	char *line = data;
	char **words;
	NemoConfigGroup *prefs = nemo_config_get_group ("preferences");
	guint n;

	/* Commands joined by ';' run in one go, with nothing from the bus or the
	   file watch in between. */
	if (strchr (line, ';') != NULL) {
		char **parts = g_strsplit (line, ";", -1);

		for (int i = 0; parts[i] != NULL; i++) {
			run_command (g_strdup (parts[i]));
		}
		g_strfreev (parts);
		g_free (line);
		return G_SOURCE_REMOVE;
	}

	words = g_strsplit (line, " ", 3);
	n = g_strv_length (words);

	/* "at <monotonic usec>", so 2 copies can act at one moment. */
	if (n == 2 && strcmp (words[0], "at") == 0) {
		gint64 when = g_ascii_strtoll (words[1], NULL, 10);

		while (g_get_monotonic_time () < when) {
			g_usleep (200);
		}
	}

	if (n >= 2 && strcmp (words[0], "set") == 0) {
		const char *value = n == 3 ? words[2] : "";

		if (strcmp (words[1], BOOL_KEY) == 0) {
			nemo_config_set_boolean (prefs, BOOL_KEY, strcmp (value, "true") == 0);
		} else if (strcmp (words[1], LIST_KEY) == 0) {
			char **values = g_strsplit (value, ",", -1);

			nemo_config_set_strv (prefs, LIST_KEY, (const char *const *) values);
			g_strfreev (values);
		} else {
			nemo_config_set_string (prefs, words[1], value);
		}
	} else if (n == 2 && strcmp (words[0], "reset") == 0) {
		nemo_config_reset (prefs, words[1]);
	} else if (n == 2 && strcmp (words[0], "value") == 0) {
		char *value = value_text (prefs, words[1]);

		say ("value %s %s", words[1], value);
		g_free (value);
	} else if (n == 2 && strcmp (words[0], "count") == 0) {
		say ("count %s %u", words[1],
		     GPOINTER_TO_UINT (g_hash_table_lookup (change_counts, words[1])));
	} else if (n == 2 && strcmp (words[0], "hold") == 0) {
		/* Busy, so what the other copy sends waits behind it. */
		g_usleep (g_ascii_strtoull (words[1], NULL, 10) * 1000);
	} else if (n == 1 && strcmp (words[0], "flush") == 0) {
		nemo_config_flush ();
	} else if (n == 3 && strcmp (words[0], "until") == 0) {
		wait_until_file_has (words[1], words[2]);
	} else if (n == 1 && strcmp (words[0], "mute") == 0) {
		/* From here on through the test's own sender, which can hold a
		   message back. */
		copy.muted = g_ptr_array_new_with_free_func ((GDestroyNotify) g_variant_unref);
		nemo_config_set_share_func (send_unless_muted, NULL, NULL);
	} else if (n == 1 && strcmp (words[0], "unmute") == 0 && copy.muted != NULL) {
		GPtrArray *held = copy.muted;

		copy.muted = NULL;
		for (guint i = 0; i < held->len; i++) {
			send_unless_muted (g_ptr_array_index (held, i), NULL);
		}
		g_ptr_array_free (held, TRUE);
	} else if (n == 1 && strcmp (words[0], "quit") == 0) {
		g_main_loop_quit (copy.loop);
	}

	g_strfreev (words);
	g_free (line);
	return G_SOURCE_REMOVE;
}

static gpointer
read_commands (G_GNUC_UNUSED gpointer data)
{
	char line[4096];

	while (fgets (line, sizeof line, stdin) != NULL) {
		line[strcspn (line, "\r\n")] = '\0';
		/* cppcheck-suppress leakNoVarFunctionCall ; run_command frees it */
		g_idle_add (run_command, g_strdup (line));
	}
	/* cppcheck-suppress leakNoVarFunctionCall ; run_command frees it */
	g_idle_add (run_command, g_strdup ("quit"));
	return NULL;
}

static int
run_copy (const char *address)
{
	GError *error = NULL;
	char *path;

	change_counts = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);
	nemo_config_init ();
	g_signal_connect (nemo_config_get_group ("preferences"), "changed",
	                  G_CALLBACK (on_changed), NULL);

	copy.bus = g_dbus_connection_new_for_address_sync (address,
	                                                   G_DBUS_CONNECTION_FLAGS_AUTHENTICATION_CLIENT |
	                                                   G_DBUS_CONNECTION_FLAGS_MESSAGE_BUS_CONNECTION,
	                                                   NULL, NULL, &error);
	if (copy.bus == NULL) {
		say ("error %s", error->message);
		return 1;
	}
	/* The order the app takes them in. */
	copy.settings_id = nemo_instance_share_settings (copy.bus);
	copy.slots = nemo_instance_publish (copy.bus, &copy.shared_id);

	copy.loop = g_main_loop_new (NULL, FALSE);
	g_thread_unref (g_thread_new ("commands", read_commands, NULL));
	path = nemo_config_get_path ();
	say ("ready %s", path);
	copy.settings = path;

	g_main_loop_run (copy.loop);

	/* As the app quits: save, then leave the bus. */
	nemo_config_flush ();
	nemo_instance_unshare_settings (copy.bus, copy.settings_id);
	nemo_instance_unpublish (copy.bus, copy.shared_id, copy.slots);
	g_dbus_connection_flush_sync (copy.bus, NULL, NULL);
	nemo_config_shutdown ();
	g_free (copy.settings);
	say ("bye");
	return 0;
}

/* The test */

typedef struct {
	const char *name;
	GSubprocess *process;
	GOutputStream *in;
	GAsyncQueue *lines;
} Peer;

static char *settings_path;
static char *real_home;

static gpointer
read_lines (gpointer data)
{
	Peer *peer = data;
	GDataInputStream *out;
	char *line;

	out = g_data_input_stream_new (g_subprocess_get_stdout_pipe (peer->process));
	while ((line = g_data_input_stream_read_line_utf8 (out, NULL, NULL, NULL)) != NULL) {
		g_strchomp (line);
		g_async_queue_push (peer->lines, line);
	}
	g_async_queue_push (peer->lines, g_strdup ("eof"));
	g_object_unref (out);
	return NULL;
}

static void
tell (Peer *peer, const char *format, ...)
{
	va_list args;
	char *text;

	va_start (args, format);
	text = g_strdup_vprintf (format, args);
	va_end (args);
	g_output_stream_write_all (peer->in, text, strlen (text), NULL, NULL, NULL);
	g_output_stream_write_all (peer->in, "\n", 1, NULL, NULL, NULL);
	g_output_stream_flush (peer->in, NULL, NULL);
	g_free (text);
}

static gint64
wait_limit (void)
{
	return g_get_monotonic_time () +
	       (gint64) (WAIT_SECONDS * test_slowness () * G_USEC_PER_SEC);
}

/* The next line from @peer that starts with @prefix, skipping others.
   Returns: (transfer full): NULL on a timeout or an exit */
static char *
wait_for (Peer *peer, const char *prefix)
{
	gint64 end = wait_limit ();

	while (g_get_monotonic_time () < end) {
		char *line = g_async_queue_timeout_pop (peer->lines, 100 * 1000);

		if (line == NULL) {
			continue;
		}
		/* Every line the copies say, for when a run goes wrong. */
		if (g_getenv ("NEMO_TEST_SHOW_COPIES") != NULL) {
			g_printerr ("[%s] %s\n", peer->name, line);
		}
		if (g_str_has_prefix (line, prefix)) {
			return line;
		}
		if (strcmp (line, "eof") == 0) {
			g_printerr ("%s exited waiting for '%s'\n", peer->name, prefix);
			g_free (line);
			return NULL;
		}
		g_free (line);
	}
	g_printerr ("%s never said '%s'\n", peer->name, prefix);
	return NULL;
}

static char *
ask (Peer *peer, const char *what, const char *key)
{
	char *prefix = g_strdup_printf ("%s %s ", what, key);
	char *line, *answer = NULL;

	tell (peer, "%s %s", what, key);
	line = wait_for (peer, prefix);
	if (line != NULL) {
		answer = g_strdup (line + strlen (prefix));
	}
	g_free (line);
	g_free (prefix);
	return answer;
}

static gboolean
start (Peer *peer, const char *self, const char *address)
{
	GSubprocessLauncher *launcher;
	GError *error = NULL;
	char *ready;

	launcher = g_subprocess_launcher_new (G_SUBPROCESS_FLAGS_STDIN_PIPE |
	                                      G_SUBPROCESS_FLAGS_STDOUT_PIPE);
#ifdef G_OS_WIN32
	/* GLib's bus on Windows checks a cookie kept under the home folder, so a
	   copy has to see the real one, set or not. Its settings follow APPDATA. */
	if (real_home != NULL) {
		g_subprocess_launcher_setenv (launcher, "HOME", real_home, TRUE);
	} else {
		g_subprocess_launcher_unsetenv (launcher, "HOME");
	}
#endif
	peer->process = g_subprocess_launcher_spawn (launcher, &error, self, "--copy", address, NULL);
	g_object_unref (launcher);
	if (peer->process == NULL) {
		g_printerr ("could not start %s: %s\n", peer->name, error->message);
		g_clear_error (&error);
		return FALSE;
	}
	peer->in = g_subprocess_get_stdin_pipe (peer->process);
	peer->lines = g_async_queue_new_full (g_free);
	g_thread_unref (g_thread_new (peer->name, read_lines, peer));

	ready = wait_for (peer, "ready ");
	if (ready == NULL) {
		return FALSE;
	}
	if (settings_path == NULL) {
		settings_path = g_strdup (ready + strlen ("ready "));
	}
	g_free (ready);
	return TRUE;
}

static gboolean
stop (Peer *peer)
{
	gboolean clean;
	char *bye;

	tell (peer, "quit");
	bye = wait_for (peer, "bye");
	g_free (bye);
	/* Its reader holds stdin locked, and exit waits on that lock. */
	g_output_stream_close (peer->in, NULL, NULL);
	clean = g_subprocess_wait_check (peer->process, NULL, NULL);
	g_clear_object (&peer->process);
	return bye != NULL && clean;
}

/* The key's value in the file at @path, list items joined by commas, or NULL
   when it has none. Read the way the store reads it, through the store's own
   SHCL. */
static char *
file_value (const char *path, const char *key)
{
	char *text = NULL;
	gsize len = 0;
	char *value = NULL;

	if (g_file_get_contents (path, &text, &len, NULL)) {
		shcl_doc *doc = shcl_parse (text, len);
		shcl_read_str_arr read = shcl_read_string_array (doc, key, strlen (key));

		if (read.status == SHCL_GOOD || read.status == SHCL_EMPTY) {
			GPtrArray *items = g_ptr_array_new_with_free_func (g_free);

			for (size_t i = 0; i < read.n; i++) {
				g_ptr_array_add (items, g_strndup (read.values[i].p, read.values[i].n));
			}
			g_ptr_array_add (items, NULL);
			value = g_strjoinv (",", (char **) items->pdata);
			g_ptr_array_free (items, TRUE);
		}
		shcl_free (doc);
	}
	g_free (text);
	return value;
}

static gboolean
file_has (const char *key, const char *value)
{
	char *now = file_value (settings_path, key);
	gboolean has = g_strcmp0 (now, value) == 0;

	g_free (now);
	return has;
}

static gboolean
wait_file (const char *key, const char *value, gboolean present)
{
	gint64 end = wait_limit ();

	while (file_has (key, value) != present) {
		if (g_get_monotonic_time () > end) {
			g_printerr ("the file never %s %s: %s\n", present ? "got" : "lost", key, value);
			return FALSE;
		}
		g_usleep (50 * 1000);
	}
	return TRUE;
}

/* When the file was last written, to the microsecond. Every save replaces it. */
static char *
file_stamp (void)
{
	GFile *file = g_file_new_for_path (settings_path);
	GFileInfo *info;
	char *stamp = NULL;

	info = g_file_query_info (file, G_FILE_ATTRIBUTE_TIME_MODIFIED ","
	                          G_FILE_ATTRIBUTE_TIME_MODIFIED_USEC "," G_FILE_ATTRIBUTE_UNIX_INODE,
	                          G_FILE_QUERY_INFO_NONE, NULL, NULL);
	if (info != NULL) {
		stamp = g_strdup_printf ("%" G_GUINT64_FORMAT ".%u inode %" G_GUINT64_FORMAT,
		                         g_file_info_get_attribute_uint64 (info, G_FILE_ATTRIBUTE_TIME_MODIFIED),
		                         g_file_info_get_attribute_uint32 (info, G_FILE_ATTRIBUTE_TIME_MODIFIED_USEC),
		                         g_file_info_get_attribute_uint64 (info, G_FILE_ATTRIBUTE_UNIX_INODE));
		g_object_unref (info);
	}
	g_object_unref (file);
	return stamp;
}

/* @what is said by @from, and @to must report the key changed to @after
   inside the save delay, while the file still has its line as @old, or still
   has no line for @after. */
static void
check_reaches (Peer *from, Peer *to, const char *what, const char *key,
               const char *after, const char *old)
{
	char *expect = g_strdup_printf ("changed %s %s", key, after);
	char *prefix = g_strdup_printf ("changed %s ", key);
	char *line_key = g_strdup_printf ("preferences.%s", key);
	gint64 sent = g_get_monotonic_time ();
	char *seen;
	gboolean still_old;

	tell (from, "%s", what);
	seen = wait_for (to, prefix);
	still_old = old != NULL ? file_has (line_key, old) : !file_has (line_key, after);
	check (seen != NULL && strcmp (seen, expect) == 0);
	if (seen != NULL && strcmp (seen, expect) != 0) {
		g_printerr ("%s said '%s', not '%s'\n", to->name, seen, expect);
	}
	if (!still_old) {
		g_printerr ("%s saw '%s' %" G_GINT64_FORMAT " ms after the set, by when the file "
		            "had it\n", to->name, what, (g_get_monotonic_time () - sent) / 1000);
	}
	check (still_old);
	check (g_get_monotonic_time () - sent < SAVE_DELAY_MS * 1000);
	g_free (seen);
	g_free (line_key);
	g_free (prefix);
	g_free (expect);
}

static void
check_count (Peer *peer, const char *key, const char *want)
{
	char *count = ask (peer, "count", key);

	check (g_strcmp0 (count, want) == 0);
	if (g_strcmp0 (count, want) != 0) {
		g_printerr ("%s ran the handler for %s %s times, not %s\n", peer->name, key,
		            count != NULL ? count : "?", want);
	}
	g_free (count);
}

static void
test_set_and_reset (Peer *a, Peer *b)
{
	char *stamp, *later;

	check_reaches (a, b, "set " BOOL_KEY " true", BOOL_KEY, "true", NULL);

	/* Once the file catches up, the reload must not count it again, and the
	   copy that only took it must not write it back. */
	check (wait_file ("preferences." BOOL_KEY, "true", TRUE));
	stamp = file_stamp ();
	g_usleep ((SAVE_DELAY_MS + 1500) * 1000 * test_slowness ());
	later = file_stamp ();
	check (g_strcmp0 (stamp, later) == 0);
	if (g_strcmp0 (stamp, later) != 0) {
		g_printerr ("the file was written again after the save: %s, then %s\n", stamp, later);
	}
	g_free (stamp);
	g_free (later);
	check_count (b, BOOL_KEY, "1");
	check_count (a, BOOL_KEY, "1");

	/* Back to the default is a line taken out, so it has to travel too. */
	check_reaches (a, b, "reset " BOOL_KEY, BOOL_KEY, "false", "true");
	check (wait_file ("preferences." BOOL_KEY, "true", FALSE));

	check_reaches (b, a, "set " LIST_KEY " one,two", LIST_KEY, "one,two", NULL);
	check (wait_file ("preferences." LIST_KEY, "one,two", TRUE));
}

/* Each sets the key at the same moment, so each message passes the other on
   the way. */
static void
test_same_key_at_once (Peer *a, Peer *b)
{
	char *va = NULL, *vb = NULL;

	for (int round = 0; round < 6; round++) {
		gint64 when = g_get_monotonic_time () + 300 * 1000;
		gint64 end;

		/* Each holds on after its set, so both are set before either takes
		   the other's. */
		tell (a, "at %" G_GINT64_FORMAT ";set " STRING_KEY " a%d;hold 200", when, round);
		tell (b, "at %" G_GINT64_FORMAT ";set " STRING_KEY " b%d;hold 200", when, round);
		g_usleep (700 * 1000);

		/* Before either saves: through the file they would agree in the end
		   anyway, after flipping. */
		end = when + (SAVE_DELAY_MS - 500) * 1000;
		for (;;) {
			g_free (va);
			g_free (vb);
			va = ask (a, "value", STRING_KEY);
			vb = ask (b, "value", STRING_KEY);
			if ((va != NULL && g_strcmp0 (va, vb) == 0) || g_get_monotonic_time () > end) {
				break;
			}
			g_usleep (100 * 1000);
		}
		check (va != NULL && g_strcmp0 (va, vb) == 0);
		if (g_strcmp0 (va, vb) != 0) {
			g_printerr ("round %d: a has '%s', b has '%s'\n", round, va, vb);
			break;
		}
	}

	/* And the file ends the same, with neither copy flipping when it reads it. */
	if (va != NULL) {
		char *va2, *vb2;

		check (wait_file ("preferences." STRING_KEY, va, TRUE));
		g_usleep ((SAVE_DELAY_MS + 1500) * 1000 * test_slowness ());
		check (file_has ("preferences." STRING_KEY, va));
		va2 = ask (a, "value", STRING_KEY);
		vb2 = ask (b, "value", STRING_KEY);
		check (g_strcmp0 (va2, va) == 0 && g_strcmp0 (vb2, va) == 0);
		g_free (va2);
		g_free (vb2);
	}
	g_free (va);
	g_free (vb);
}

/* A hand edit read while a change from the other copy is held, before that copy
   has saved it, must not drop it back to what the file had. */
static void
test_held_through_hand_edit (Peer *a, Peer *b)
{
	char *count = ask (b, "count", STRING_KEY);
	char *want = g_strdup_printf ("%u", count != NULL ? (guint) atoi (count) + 1 : 0);
	char *text = NULL, *edited, *seen, *value;

	check_reaches (a, b, "set " STRING_KEY " held", STRING_KEY, "held", NULL);
	check (g_file_get_contents (settings_path, &text, NULL, NULL));
	edited = g_strdup_printf ("%s\npreferences." EDIT_KEY ": true\n", text != NULL ? text : "");
	check (g_file_set_contents (settings_path, edited, -1, NULL));

	seen = wait_for (b, "changed " EDIT_KEY " true");
	check (seen != NULL);
	value = ask (b, "value", STRING_KEY);
	check (g_strcmp0 (value, "held") == 0);
	if (g_strcmp0 (value, "held") != 0) {
		g_printerr ("copy b went back to '%s' on a hand edit before the save\n", value);
	}
	check (wait_file ("preferences." STRING_KEY, "held", TRUE));
	check_count (b, STRING_KEY, want);

	g_free (value);
	g_free (seen);
	g_free (edited);
	g_free (text);
	g_free (want);
	g_free (count);
}

/* A copy that dies before it saves takes its change to disk with it. The copy
   that only took it keeps it, and writes it only with a change of its own. */
static void
test_sender_killed (Peer *b, const char *self, const char *address)
{
	Peer c = { "copy c", NULL, NULL, NULL };

	if (!start (&c, self, address)) {
		check (FALSE);
		return;
	}
	check_reaches (&c, b, "set " BOOL_KEY " true", BOOL_KEY, "true", NULL);
	g_subprocess_force_exit (c.process);
	g_subprocess_wait (c.process, NULL, NULL);
	g_clear_object (&c.process);

	g_usleep ((SAVE_DELAY_MS + 1500) * 1000 * test_slowness ());
	check (!file_has ("preferences." BOOL_KEY, "true"));
	if (file_has ("preferences." BOOL_KEY, "true")) {
		g_printerr ("copy b saved a change it only took\n");
	}
	{
		char *value = ask (b, "value", BOOL_KEY);

		check (g_strcmp0 (value, "true") == 0);
		g_free (value);
	}

	tell (b, "set " LIST_KEY " x,y");
	check (wait_file ("preferences." LIST_KEY, "x,y", TRUE));
	check (file_has ("preferences." BOOL_KEY, "true"));
}

/* Both copies and the file have @value within the wait. */
static void
check_agree (Peer *a, Peer *b, const char *value, const char *after)
{
	gint64 end = wait_limit ();
	char *va = NULL, *vb = NULL;
	gboolean same = FALSE;

	while (!same && g_get_monotonic_time () < end) {
		g_usleep (200 * 1000);
		g_free (va);
		g_free (vb);
		va = ask (a, "value", STRING_KEY);
		vb = ask (b, "value", STRING_KEY);
		same = g_strcmp0 (va, value) == 0 && g_strcmp0 (vb, value) == 0 &&
		       file_has ("preferences." STRING_KEY, value);
	}
	check (same);
	if (!same) {
		char *in_file = file_value (settings_path, "preferences." STRING_KEY);

		g_printerr ("after %s: a has '%s', b has '%s', the file '%s'\n",
		            after, va, vb, in_file);
		g_free (in_file);
	}
	g_free (va);
	g_free (vb);
}

/* The copy that lost saves after the one that won, before it hears of the
   winner, so the file ends with the value that lost. The winner knows that one
   and writes its own over it. */
static void
test_stale_save_repaired (Peer *a, Peer *b)
{
	char *seen;

	/* a's set comes first, so b's is the newer change. Then a saves only once
	   b's is in the file, and before it takes b's message. */
	tell (a, "set " STRING_KEY " lost;until " STRING_KEY " won;flush");
	seen = wait_for (a, "changed " STRING_KEY " lost");
	check (seen != NULL);
	g_free (seen);
	tell (b, "set " STRING_KEY " won;flush");

	check_agree (a, b, "won", "a stale save");
}

/* As above, but a's message reaches b only after b has read a's save back from
   the file, so b can't know yet that the value in it lost. */
static void
test_late_message_repaired (Peer *a, Peer *b)
{
	char *seen;

	tell (a, "mute;set " STRING_KEY " late-lost;until " STRING_KEY " late-won;flush");
	seen = wait_for (a, "changed " STRING_KEY " late-lost");
	check (seen != NULL);
	g_free (seen);
	tell (b, "set " STRING_KEY " late-won;flush");

	seen = wait_for (b, "changed " STRING_KEY " late-lost");
	check (seen != NULL);
	g_free (seen);
	seen = wait_for (a, "changed " STRING_KEY " late-won");
	check (seen != NULL);
	g_free (seen);
	tell (a, "unmute");

	check_agree (a, b, "late-won", "a message that came after the save");
}

/* Kept open for the whole run: GLib's bus on Windows ends a few seconds after
   its last client leaves, and starting a copy can take longer than that. */
static GDBusConnection *probe;

static char *
session_address (int argc, char **argv)
{
	char *address = g_dbus_address_get_for_bus_sync (G_BUS_TYPE_SESSION, NULL, NULL);

	if (address != NULL) {
		probe = g_dbus_connection_new_for_address_sync (address,
		                                                G_DBUS_CONNECTION_FLAGS_AUTHENTICATION_CLIENT |
		                                                G_DBUS_CONNECTION_FLAGS_MESSAGE_BUS_CONNECTION,
		                                                NULL, NULL, NULL);
	}
	if (probe != NULL) {
		return address;
	}
	g_free (address);

#ifndef G_OS_WIN32
	/* meson hands every test a bus address that goes nowhere. */
	if (g_getenv ("NEMO_TEST_BUS_RELAUNCHED") == NULL) {
		char *dbus_run = g_find_program_in_path ("dbus-run-session");
		char **relaunch;

		if (dbus_run != NULL) {
			g_setenv ("NEMO_TEST_BUS_RELAUNCHED", "1", TRUE);
			relaunch = g_new0 (char *, argc + 3);
			relaunch[0] = dbus_run;
			relaunch[1] = (char *) "--";
			for (int i = 0; i < argc; i++) {
				relaunch[i + 2] = argv[i];
			}
			execv (dbus_run, relaunch);
			g_free (relaunch);
			g_free (dbus_run);
		}
	}
#else
	(void) argc;
	(void) argv;
#endif
	return NULL;
}

int
main (int argc, char **argv)
{
	Peer a = { "copy a", NULL, NULL, NULL };
	Peer b = { "copy b", NULL, NULL, NULL };
	char *address, *self, *dir;

	if (argc == 3 && strcmp (argv[1], "--copy") == 0) {
		return run_copy (argv[2]);
	}

	address = session_address (argc, argv);
	if (address == NULL) {
		g_print ("SKIP: no session bus\n");
		return 77;
	}

	/* The copies take the scratch config from here. */
	real_home = g_strdup (g_getenv ("HOME"));
	dir = test_scratch_config_home ("nemo-config-share-XXXXXX");
	self = nemo_get_exe_path ();

	if (!start (&a, self, address) || !start (&b, self, address)) {
		check (FALSE);
		return 1;
	}

	test_set_and_reset (&a, &b);
	test_same_key_at_once (&a, &b);
	test_held_through_hand_edit (&a, &b);
	test_stale_save_repaired (&a, &b);
	test_late_message_repaired (&a, &b);
	test_sender_killed (&b, self, address);

	/* Said on the way out: a's flush tells b before it saves. */
	tell (&a, "reset " BOOL_KEY);
	check (stop (&a));
	{
		char *seen = wait_for (&b, "changed " BOOL_KEY " false");

		check (seen != NULL);
		g_free (seen);
	}
	check (wait_file ("preferences." BOOL_KEY, "true", FALSE));
	check (stop (&b));

	g_dbus_connection_close_sync (probe, NULL, NULL);
	g_object_unref (probe);
	g_free (self);
	g_free (dir);
	g_free (address);
	g_free (settings_path);
	g_free (real_home);

	if (failures == 0) {
		g_print ("OK\n");
	}
	return failures == 0 ? 0 : 1;
}
