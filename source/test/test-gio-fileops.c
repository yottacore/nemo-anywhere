/* Probes the GIO operations the file-operations layer depends on, so
 * per-platform gaps show up here instead of in the GUI. Portable ops
 * assert; platform-divergent ops (symlink, unix::mode, invalid names,
 * trash) log their observed behavior and only fail on crashes. */

#include <gio/gio.h>
#include <glib/gstdio.h>
#include <string.h>

#include "test-scratch.h"

static int failures = 0;

static void
check (gboolean ok, const char *what, GError *error)
{
	if (ok) {
		g_print ("ok       %s\n", what);
	} else {
		failures++;
		g_print ("FAIL     %s: %s\n", what,
		         error ? error->message : "(no error set)");
	}
	g_clear_error (&error);
}

static void
probe (gboolean ok, const char *what, GError *error)
{
	if (ok) {
		g_print ("ok       %s\n", what);
	} else {
		g_print ("probe    %s -> %s (%d)\n", what,
		         error ? error->message : "(no error)",
		         error ? error->code : -1);
	}
	g_clear_error (&error);
}

static int monitor_events = 0;
static GMainLoop *monitor_loop = NULL;

static void
on_monitor_changed (GFileMonitor *monitor, GFile *file, GFile *other,
                    GFileMonitorEvent event, gpointer user_data)
{
	monitor_events++;
	if (monitor_loop)
		g_main_loop_quit (monitor_loop);
}

static gboolean
quit_loop_cb (gpointer loop)
{
	g_main_loop_quit (loop);
	return G_SOURCE_REMOVE;
}

static GFile *
child (GFile *dir, const char *name)
{
	return g_file_get_child (dir, name);
}

static gboolean
write_file (GFile *f, const char *contents)
{
	return g_file_replace_contents (f, contents, strlen (contents), NULL,
	                                FALSE, G_FILE_CREATE_NONE, NULL, NULL, NULL);
}

int
main (int argc, char *argv[])
{
	GError *error = NULL;
	gchar *base_path;
	GFile *base, *src, *dst, *dir, *sub, *f;
	GFileInfo *info;
	gboolean ok;

	base_path = test_scratch_dir ("nemo-fileops-XXXXXX", &error);
	if (base_path == NULL) {
		g_printerr ("cannot create temp dir: %s\n", error->message);
		return 1;
	}
	base = g_file_new_for_path (base_path);
	g_print ("base: %s\n", base_path);

	/* plain file copy */
	src = child (base, "a.txt");
	dst = child (base, "b.txt");
	check (write_file (src, "hello"), "create file", NULL);
	ok = g_file_copy (src, dst, G_FILE_COPY_NOFOLLOW_SYMLINKS, NULL, NULL, NULL, &error);
	check (ok, "copy file", error);
	error = NULL;

	/* copy onto existing without overwrite -> EXISTS (conflict dialog path) */
	ok = g_file_copy (src, dst, G_FILE_COPY_NOFOLLOW_SYMLINKS, NULL, NULL, NULL, &error);
	check (!ok && g_error_matches (error, G_IO_ERROR, G_IO_ERROR_EXISTS),
	       "copy-no-overwrite raises EXISTS", NULL);
	g_clear_error (&error);
	error = NULL;

	/* overwrite copy */
	ok = g_file_copy (src, dst, G_FILE_COPY_OVERWRITE | G_FILE_COPY_NOFOLLOW_SYMLINKS,
	                  NULL, NULL, NULL, &error);
	check (ok, "copy overwrite", error);
	error = NULL;

	/* dir copy -> WOULD_RECURSE (file-operations recurses itself on this) */
	dir = child (base, "dir");
	sub = child (dir, "sub");
	check (g_file_make_directory (dir, NULL, NULL), "mkdir", NULL);
	check (g_file_make_directory (sub, NULL, NULL), "mkdir nested", NULL);
	f = child (sub, "deep.txt");
	check (write_file (f, "deep"), "create nested file", NULL);
	g_object_unref (f);
	f = child (base, "dircopy");
	ok = g_file_copy (dir, f, G_FILE_COPY_NOFOLLOW_SYMLINKS, NULL, NULL, NULL, &error);
	check (!ok && g_error_matches (error, G_IO_ERROR, G_IO_ERROR_WOULD_RECURSE),
	       "dir copy raises WOULD_RECURSE", NULL);
	g_clear_error (&error);
	error = NULL;
	g_object_unref (f);

	/* same-volume move */
	f = child (base, "moved.txt");
	ok = g_file_move (dst, f, G_FILE_COPY_NOFOLLOW_SYMLINKS, NULL, NULL, NULL, &error);
	check (ok, "move file", error);
	error = NULL;

	/* move dir over empty slot, same volume (plain rename) */
	g_object_unref (f);
	f = child (base, "dirmoved");
	ok = g_file_move (dir, f, G_FILE_COPY_NOFOLLOW_SYMLINKS, NULL, NULL, NULL, &error);
	check (ok, "move dir", error);
	error = NULL;
	g_object_unref (dir);
	dir = f;
	f = NULL;

	/* rename via set_display_name (the nemo-file rename path) */
	f = child (base, "moved.txt");
	{
		GFile *renamed = g_file_set_display_name (f, "renamed.txt", NULL, &error);
		check (renamed != NULL, "set_display_name", error);
		error = NULL;
		g_clear_object (&renamed);
	}
	g_object_unref (f);

	/* rename to a windows-reserved name; INVALID_FILENAME has a handler
	 * in the rename path, anything else would come back as a raw error */
	f = child (base, "renamed.txt");
	{
		GFile *renamed = g_file_set_display_name (f, "bad:name.txt", NULL, &error);
		probe (renamed != NULL, "rename with ':' in name", error);
		error = NULL;
		if (renamed != NULL) {
			/* rename it back so cleanup below finds it */
			GFile *back = g_file_set_display_name (renamed, "renamed.txt", NULL, NULL);
			g_clear_object (&back);
			g_object_unref (renamed);
		}
	}
	g_object_unref (f);

	/* symlink creation ("Make Link" / paste-link) */
	f = child (base, "a.link");
	ok = g_file_make_symbolic_link (f, "a.txt", NULL, &error);
	probe (ok, "make_symbolic_link", error);
	error = NULL;
	if (ok)
		g_file_delete (f, NULL, NULL);
	g_object_unref (f);

	/* unix::mode attributes: is it settable, and does a set round-trip */
	info = g_file_query_info (src, "unix::mode,unix::uid,access::can-write,access::can-trash",
	                          G_FILE_QUERY_INFO_NONE, NULL, &error);
	check (info != NULL, "query unix::mode", error);
	error = NULL;
	if (info) {
		if (g_file_info_has_attribute (info, G_FILE_ATTRIBUTE_UNIX_MODE))
			g_print ("info     unix::mode present, value %04o\n",
			         g_file_info_get_attribute_uint32 (info, G_FILE_ATTRIBUTE_UNIX_MODE));
		else
			g_print ("info     unix::mode ABSENT\n");
		if (g_file_info_has_attribute (info, G_FILE_ATTRIBUTE_UNIX_UID))
			g_print ("info     unix::uid present, value %u\n",
			         g_file_info_get_attribute_uint32 (info, G_FILE_ATTRIBUTE_UNIX_UID));
		else
			g_print ("info     unix::uid ABSENT\n");
		if (g_file_info_has_attribute (info, G_FILE_ATTRIBUTE_ACCESS_CAN_TRASH))
			g_print ("info     access::can-trash = %s\n",
			         g_file_info_get_attribute_boolean (info, G_FILE_ATTRIBUTE_ACCESS_CAN_TRASH) ? "true" : "false");
		else
			g_print ("info     access::can-trash ABSENT\n");
		g_object_unref (info);
	}
	{
		GFileAttributeInfoList *settable = g_file_query_settable_attributes (src, NULL, NULL);
		gboolean mode_settable = FALSE;
		if (settable) {
			int i;
			for (i = 0; i < settable->n_infos; i++)
				if (g_str_equal (settable->infos[i].name, "unix::mode"))
					mode_settable = TRUE;
			g_file_attribute_info_list_unref (settable);
		}
		g_print ("info     unix::mode settable: %s\n", mode_settable ? "yes" : "no");
	}
	ok = g_file_set_attribute_uint32 (src, G_FILE_ATTRIBUTE_UNIX_MODE, 0600,
	                                  G_FILE_QUERY_INFO_NONE, NULL, &error);
	probe (ok, "set unix::mode 0600", error);
	error = NULL;

	/* filesystem free space (status bar, copy-space preflight) */
	info = g_file_query_filesystem_info (base, G_FILE_ATTRIBUTE_FILESYSTEM_FREE, NULL, &error);
	check (info != NULL && g_file_info_has_attribute (info, G_FILE_ATTRIBUTE_FILESYSTEM_FREE),
	       "filesystem::free", error);
	error = NULL;
	g_clear_object (&info);

	/* filesystem id (nemo compares these to pick move-vs-copy on dnd) */
	info = g_file_query_info (base, G_FILE_ATTRIBUTE_ID_FILESYSTEM, G_FILE_QUERY_INFO_NONE, NULL, &error);
	if (info != NULL && g_file_info_has_attribute (info, G_FILE_ATTRIBUTE_ID_FILESYSTEM)) {
		g_print ("ok       id::filesystem = %s\n",
		         g_file_info_get_attribute_string (info, G_FILE_ATTRIBUTE_ID_FILESYSTEM));
	} else {
		g_print ("probe    id::filesystem ABSENT\n");
	}
	g_clear_error (&error);
	error = NULL;
	g_clear_object (&info);

	/* delete: non-empty dir must raise NOT_EMPTY (recursive-delete driver) */
	ok = g_file_delete (dir, NULL, &error);
	check (!ok && g_error_matches (error, G_IO_ERROR, G_IO_ERROR_NOT_EMPTY),
	       "delete non-empty dir raises NOT_EMPTY", NULL);
	g_clear_error (&error);
	error = NULL;

	/* trash a file. On real Windows the shell puts up a modal confirmation and
	 * waits for a click, which no unattended run can give it - set
	 * NEMO_PROBE_TRASH to take that path with someone at the screen. */
#ifdef G_OS_WIN32
	if (g_getenv ("NEMO_PROBE_TRASH") == NULL) {
		g_print ("SKIP g_file_trash (prompts on Windows; set NEMO_PROBE_TRASH to run it)\n");
	} else
#endif
	{
		f = child (base, "trashme.txt");
		check (write_file (f, "bye"), "create trash victim", NULL);
		ok = g_file_trash (f, NULL, &error);
		probe (ok, "g_file_trash", error);
		error = NULL;
		if (!ok)
			g_file_delete (f, NULL, NULL);
		g_object_unref (f);
	}

	/* directory monitoring (the directory-async change pipeline) */
	{
		GFileMonitor *monitor = g_file_monitor_directory (base, G_FILE_MONITOR_NONE, NULL, &error);
		check (monitor != NULL, "monitor_directory create", error);
		error = NULL;
		if (monitor != NULL) {
			GMainLoop *loop = g_main_loop_new (NULL, FALSE);
			GFile *touched = child (base, "watched.txt");
			guint tid;
			monitor_events = 0;
			monitor_loop = loop;
			g_signal_connect (monitor, "changed", G_CALLBACK (on_monitor_changed), NULL);
			write_file (touched, "ping");
			tid = g_timeout_add (3000, quit_loop_cb, loop);
			g_main_loop_run (loop);
			g_source_remove (tid);
			check (monitor_events > 0, "monitor delivers change events", NULL);
			g_print ("info     monitor backend: %s\n", G_OBJECT_TYPE_NAME (monitor));
			g_file_monitor_cancel (monitor);
			g_file_delete (touched, NULL, NULL);
			g_object_unref (touched);
			g_object_unref (monitor);
			g_main_loop_unref (loop);
		}
	}

	/* cleanup */
	f = child (base, "renamed.txt");
	g_file_delete (f, NULL, NULL);
	g_object_unref (f);
	g_file_delete (src, NULL, NULL);
	{
		GFile *deep = g_file_resolve_relative_path (dir, "sub/deep.txt");
		GFile *subd = g_file_get_child (dir, "sub");
		g_file_delete (deep, NULL, NULL);
		g_file_delete (subd, NULL, NULL);
		g_object_unref (deep);
		g_object_unref (subd);
	}
	g_file_delete (dir, NULL, NULL);
	g_file_delete (base, NULL, NULL);

	g_object_unref (src);
	g_object_unref (dst);
	g_object_unref (dir);
	g_object_unref (sub);
	g_object_unref (base);
	g_free (base_path);

	g_print (failures ? "FAILURES: %d\n" : "all checks passed\n", failures);
	return failures ? 1 : 0;
}
