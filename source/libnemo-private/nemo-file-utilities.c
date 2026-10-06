/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */

/* nemo-file-utilities.c - implementation of file manipulation routines.

   Copyright (C) 1999, 2000, 2001 Eazel, Inc.

   The Gnome Library is free software; you can redistribute it and/or
   modify it under the terms of the GNU Library General Public License as
   published by the Free Software Foundation; either version 2 of the
   License, or (at your option) any later version.

   The Gnome Library is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   Library General Public License for more details.

   You should have received a copy of the GNU Library General Public
   License along with the Gnome Library; see the file COPYING.LIB.  If not,
   write to the Free Software Foundation, Inc., 51 Franklin Street - Suite 500,
   Boston, MA 02110-1335, USA.

   Authors: John Sullivan <sullivan@eazel.com>
*/

#include <config.h>
#include "nemo-file-utilities.h"
#include <libnemo-private/nemo-posix-compat.h>

#include "nemo-global-preferences.h"
#include "nemo-dir-enum.h"
#include "nemo-lib-self-check-functions.h"
#include "nemo-metadata.h"
#include "nemo-file.h"
#include "nemo-file-operations.h"
#include "nemo-search-directory.h"
#include "nemo-signaller.h"
#include <eel/eel-glib-extensions.h>
#include <eel/eel-stock-dialogs.h>
#include <eel/eel-string.h>
#include <eel/eel-vfs-extensions.h>
#include <eel/eel-debug.h>
#include <glib.h>
#include <glib/gi18n.h>
#include <glib/gstdio.h>
#include <gio/gio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#ifdef G_OS_WIN32
#include <windows.h>
#endif

#if defined(__FreeBSD__) || defined(__DragonFly__)
#include <sys/types.h>
#include <sys/sysctl.h>
#endif

#define NEMO_USER_DIRECTORY_NAME NEMO_APP_SLUG

#define DESKTOP_DIRECTORY_NAME "Desktop"
#define LEGACY_DESKTOP_DIRECTORY_NAME ".gnome-desktop"

static void update_xdg_dir_cache (void);
static void schedule_user_dirs_changed (void);
static void desktop_dir_changed (void);
static void update_xdg_user_dir (const char *type, const char *path);
static GFile *nemo_find_file_insensitive_next (GFile *parent, const gchar *name);

/* How much of the folder part of a window title survives. Only reached by a
   name or a uri; a path is shortened by measurement well before this. */
#define WINDOW_TITLE_LIMIT 180

/* Returns: (transfer full): free with g_free */
char *
nemo_compute_title_for_location (GFile *location)
{
	NemoFile *file;
	char *title;
    char *builder;
	/* TODO-gio: This doesn't really work all that great if the
	   info about the file isn't known atm... */

	/* "Home" is a name, not a path, so it has to yield when the full path was
	   asked for - otherwise turning the preference on in the home folder looks
	   like it did nothing at all. */
	if (nemo_is_home_directory (location) &&
	    !nemo_config_get_boolean (nemo_preferences, NEMO_PREFERENCES_SHOW_FULL_PATH_TITLES)) {
		return g_strdup (_("Home"));
	}

	builder = NULL;
	if (location) {
		file = nemo_file_get (location);
		builder = nemo_file_get_description (file);
		if (builder == NULL) {
			builder = nemo_file_get_display_name (file);
		}
		nemo_file_unref (file);
	}

	if (builder == NULL) {
		builder = g_file_get_basename (location);
	}

    if (nemo_config_get_boolean (nemo_preferences, NEMO_PREFERENCES_SHOW_FULL_PATH_TITLES)) {
        gchar *path;

        path = nemo_compute_title_path_for_location (location);
        if (path != NULL) {
            /* The path already ends in the folder's name, so putting the name in
               front of it just says the same thing twice. It is handed over
               whole: the title bar and the tabs each shorten it to what they
               have room for. */
            title = g_strdup (path);
        } else {
            gchar *uri = g_file_get_uri (location);

            title = g_strdup_printf("%s - %s", builder, uri);
            g_free (uri);
        }
        g_free (path);
        g_free (builder);
    } else {
        title = g_strdup(builder);
        g_free (builder);
    }
    return title;
}

/* Returns: (transfer full): free with g_free */
gchar *
nemo_compute_title_path_for_location (GFile *location)
{
	NemoFile *file;
	gchar *uri, *path;

	if (location == NULL ||
	    !nemo_config_get_boolean (nemo_preferences, NEMO_PREFERENCES_SHOW_FULL_PATH_TITLES)) {
		return NULL;
	}

	file = nemo_file_get (location);
	uri = nemo_file_get_uri (file);
	path = g_filename_from_uri (uri, NULL, NULL);
	nemo_path_apply_display_separator (path);
	nemo_file_unref (file);
	g_free (uri);

	return path;
}

static gboolean
has_space (const char *text)
{
	for (; *text != '\0'; text = g_utf8_next_char (text)) {
		if (g_unichar_isspace (g_utf8_get_char (text))) {
			return TRUE;
		}
	}
	return FALSE;
}

/* The window title names the program as well as the folder, so a taskbar button
   or a window switcher tells our window from any other file manager's. The
   folder goes first, since a narrow taskbar button cuts from the end. Quotes
   only go on when a space would otherwise blur where the name stops. The tabs
   keep the bare folder title, since the window around them already says it.
   Returns: (transfer full): free with g_free */
char *
nemo_compute_window_title (const char *location_title)
{
	char *shortened;
	char *title;

	if (location_title == NULL || *location_title == '\0') {
		return g_strdup (_("Nemo Anywhere"));
	}

	shortened = eel_str_middle_truncate (location_title, WINDOW_TITLE_LIMIT);
	if (has_space (shortened)) {
		title = g_strdup_printf (_("\"%s\" - Nemo Anywhere"), shortened);
	} else {
		title = g_strdup_printf (_("%s - Nemo Anywhere"), shortened);
	}
	g_free (shortened);

	return title;
}

static gint64 process_started = 0;

/* Called first thing in main, so the About box can say how long this copy has
   been running. By default every window is its own copy. */
void
nemo_note_process_start (void)
{
	if (process_started == 0) {
		process_started = g_get_monotonic_time ();
	}
}

gint64
nemo_get_uptime_seconds (void)
{
	if (process_started == 0) {
		return 0;
	}

	return (g_get_monotonic_time () - process_started) / G_USEC_PER_SEC;
}

/* Days, hours and minutes, leaving out any that are zero. Seconds would only
   be stale by the time anyone read them.
   Returns: (transfer full): free with g_free */
char *
nemo_format_uptime (gint64 seconds)
{
	GString *text;
	gint64 days, hours, minutes;

	if (seconds < 60) {
		return g_strdup (_("less than a minute"));
	}

	days = seconds / 86400;
	hours = (seconds % 86400) / 3600;
	minutes = (seconds % 3600) / 60;

	text = g_string_new (NULL);

	if (days > 0) {
		g_string_append_printf (text, ngettext ("%d day", "%d days", days), (int) days);
	}
	if (hours > 0) {
		if (text->len > 0) {
			g_string_append (text, ", ");
		}
		g_string_append_printf (text, ngettext ("%d hour", "%d hours", hours), (int) hours);
	}
	if (minutes > 0) {
		if (text->len > 0) {
			g_string_append (text, ", ");
		}
		g_string_append_printf (text, ngettext ("%d minute", "%d minutes", minutes), (int) minutes);
	}

	return g_string_free (text, FALSE);
}

// TODO: Maybe this can replace nemo_compute_title_for_location() all around?
/* Returns: (transfer full): free with g_free */
char *
nemo_compute_search_title_for_location (GFile *location)
{
    GFile *home_file;
    gchar *location_string;

    if (nemo_is_home_directory (location)) {
        return g_strdup (_("Home"));
    }

    home_file = g_file_new_for_path (g_get_home_dir ());

    location_string = NULL;

    location_string = g_file_get_relative_path (home_file, location);

    if (location_string == NULL) {
        if (g_file_is_native (location)) {
            location_string = g_file_get_path (location);
        } else {
            location_string = g_file_get_uri (location);
        }
    }

    g_object_unref (home_file);

    return location_string;
}

/**
 * nemo_get_user_config_root:
 *
 * The per-user directory our own config directory lives in.
 *
 * GLib answers XDG everywhere, which is right on Linux and BSD but is a poor
 * fit elsewhere: on Windows it points at the local, machine-bound AppData, and
 * on macOS at a hidden dotfile dir. Settings are small, portable and worth
 * carrying between a user's machines, so Windows gets roaming AppData and
 * macOS its Application Support dir.
 *
 * Return value: (transfer none): the directory path, owned by the callee.
 **/
const char *
nemo_get_user_config_root (void)
{
#if defined(G_OS_WIN32)
	static char *root = NULL;
	static gsize once = 0;

	if (g_once_init_enter (&once)) {
		const char *roaming = g_getenv ("APPDATA");

		root = (roaming != NULL && *roaming != '\0')
			? g_strdup (roaming)
			: g_strdup (g_get_user_config_dir ());
		g_once_init_leave (&once, 1);
	}

	return root;
#elif defined(__APPLE__)
	static char *root = NULL;
	static gsize once = 0;

	if (g_once_init_enter (&once)) {
		root = g_build_filename (g_get_home_dir (), "Library", "Application Support", NULL);
		g_once_init_leave (&once, 1);
	}

	return root;
#else
	return g_get_user_config_dir ();
#endif
}

/**
 * nemo_get_user_cache_root:
 *
 * The per-user directory our cache directory lives in.
 *
 * Deliberately not the same choice as the config root above. A cache is big,
 * rebuildable and tied to the machine that built it, so on Windows it belongs
 * in local AppData rather than the roaming dir settings use - nobody wants a
 * thumbnail database syncing between machines. macOS has a cache dir of its
 * own that the system knows it may empty.
 *
 * Return value: (transfer none): the directory path, owned by the callee.
 **/
const char *
nemo_get_user_cache_root (void)
{
#if defined(G_OS_WIN32)
	static char *root = NULL;
	static gsize once = 0;

	if (g_once_init_enter (&once)) {
		const char *local = g_getenv ("LOCALAPPDATA");

		root = (local != NULL && *local != '\0')
			? g_strdup (local)
			: g_strdup (g_get_user_cache_dir ());
		g_once_init_leave (&once, 1);
	}

	return root;
#elif defined(__APPLE__)
	static char *root = NULL;
	static gsize once = 0;

	if (g_once_init_enter (&once)) {
		root = g_build_filename (g_get_home_dir (), "Library", "Caches", NULL);
		g_once_init_leave (&once, 1);
	}

	return root;
#else
	return g_get_user_cache_dir ();
#endif
}

/* Returns: (transfer full): free with g_free */
char *
nemo_get_user_cache_directory (void)
{
	char *dir = g_build_filename (nemo_get_user_cache_root (),
				      NEMO_USER_DIRECTORY_NAME,
				      NULL);

	if (g_mkdir_with_parents (dir, DEFAULT_NEMO_DIRECTORY_MODE) != 0) {
		g_warning ("could not make the cache dir %s: %s", dir, g_strerror (errno));
		g_free (dir);
		return NULL;
	}

	return dir;
}

/* Windows and macOS used to keep the config dir wherever GLib's XDG answer
 * put it. Move a dir left there by an older build rather than silently
 * starting from defaults; a partial or failed move leaves the old one alone. */
static void
migrate_legacy_user_directory (const char *user_directory)
{
	g_autofree char *legacy = NULL;
	g_autofree char *settings = NULL;
	g_autofree char *parent = NULL;

	if (g_strcmp0 (nemo_get_user_config_root (), g_get_user_config_dir ()) == 0)
		return;

	legacy = g_build_filename (g_get_user_config_dir (),
				   NEMO_USER_DIRECTORY_NAME,
				   NULL);

	/* On Windows the XDG config dir is also the user data dir, where actions and
	 * scripts live for good. Only a settings file marks an old config. */
	settings = g_build_filename (legacy, "settings.shcl", NULL);

	if (!g_file_test (settings, G_FILE_TEST_IS_REGULAR))
		return;

	/* rename(2) wants somewhere to rename into. The real config roots always
	 * exist, but a fresh profile or a redirected one need not. */
	parent = g_path_get_dirname (user_directory);
	g_mkdir_with_parents (parent, DEFAULT_NEMO_DIRECTORY_MODE);

	if (g_rename (legacy, user_directory) != 0) {
		g_warning ("could not move settings from %s to %s: %s",
			   legacy, user_directory, g_strerror (errno));
		return;
	}

	g_message ("moved settings from %s to %s", legacy, user_directory);
}

/**
 * nemo_get_user_directory:
 *
 * Get the path for the directory containing nemo settings.
 *
 * Return value: (transfer full): the directory path. Free with g_free.
 **/
char *
nemo_get_user_directory (void)
{
	char *user_directory = NULL;

	user_directory = g_build_filename (nemo_get_user_config_root (),
					   NEMO_USER_DIRECTORY_NAME,
					   NULL);

	if (!g_file_test (user_directory, G_FILE_TEST_EXISTS)) {
		migrate_legacy_user_directory (user_directory);
		g_mkdir_with_parents (user_directory, DEFAULT_NEMO_DIRECTORY_MODE);
		/* FIXME bugzilla.gnome.org 41286:
		 * How should we handle the case where this mkdir fails?
		 * Note that nemo_application_startup will refuse to launch if this
		 * directory doesn't get created, so that case is OK. But the directory
		 * could be deleted after Nemo was launched, and perhaps
		 * there is some bad side-effect of not handling that case.
		 */
	}

	return user_directory;
}

/**
 * nemo_get_accel_map_file:
 *
 * The keyboard shortcut file, beside the settings file. The folder is made
 * here; the file need not exist.
 *
 * Return value: (transfer full): the filename path, freed by the caller.
 **/
char *
nemo_get_accel_map_file (void)
{
	g_autofree char *dir = nemo_get_user_directory ();

	return g_build_filename (dir, "accels", NULL);
}

/* Where upstream Nemo keeps its shortcuts, and where older builds of ours
 * saved them too. Read once, to carry custom shortcuts over; never written.
 * Returns: (transfer full): free with g_free */
char *
nemo_get_legacy_accel_map_file (void)
{
	const char *override = g_getenv ("GNOME22_USER_DIR");

	if (override != NULL && *override != '\0')
		return g_build_filename (override, "accels", "nemo", NULL);

	return g_build_filename (g_get_home_dir (), ".gnome2", "accels", "nemo", NULL);
}

/**
 * nemo_get_scripts_directory_path:
 *
 * Get the path for the directory containing nemo scripts.
 *
 * Return value: (transfer full): the directory path containing nemo scripts. Free with g_free.
 **/
char *
nemo_get_scripts_directory_path (void)
{
	return g_build_filename (g_get_user_data_dir (), NEMO_APP_SLUG, "scripts", NULL);
}

typedef struct {
	char *type;
	char *path;
	NemoFile *file;
} XdgDirEntry;


static XdgDirEntry *
parse_xdg_dirs (const char *config_file)
{
  GArray *array;
  char *config_file_free = NULL;
  XdgDirEntry dir;
  char *data;
  char **lines;
  char *p, *d;
  int i;
  char *type_start, *type_end;
  char *value, *unescaped;
  gboolean relative;

  array = g_array_new (TRUE, TRUE, sizeof (XdgDirEntry));

  if (config_file == NULL)
    {
      config_file_free = g_build_filename (g_get_user_config_dir (),
					   "user-dirs.dirs", NULL);
      config_file = (const char *)config_file_free;
    }

  if (g_file_get_contents (config_file, &data, NULL, NULL))
    {
      lines = g_strsplit (data, "\n", 0);
      g_free (data);
      for (i = 0; lines[i] != NULL; i++)
	{
	  p = lines[i];
	  while (g_ascii_isspace (*p))
	    p++;

	  if (*p == '#')
	    continue;

	  value = strchr (p, '=');
	  if (value == NULL)
	    continue;
	  *value++ = 0;

	  g_strchug (g_strchomp (p));
	  if (!g_str_has_prefix (p, "XDG_"))
	    continue;
	  if (!g_str_has_suffix (p, "_DIR"))
	    continue;
	  type_start = p + 4;
	  type_end = p + strlen (p) - 4;

	  while (g_ascii_isspace (*value))
	    value++;

	  if (*value != '"')
	    continue;
	  value++;

	  relative = FALSE;
	  if (g_str_has_prefix (value, "$HOME"))
	    {
	      relative = TRUE;
	      value += 5;
	      while (*value == '/')
		      value++;
	    }
	  else if (*value != '/')
	    continue;

	  d = unescaped = g_malloc (strlen (value) + 1);
	  while (*value && *value != '"')
	    {
	      if ((*value == '\\') && (*(value + 1) != 0))
		value++;
	      *d++ = *value++;
	    }
	  *d = 0;

	  *type_end = 0;
	  dir.type = g_strdup (type_start);
	  if (relative)
	    {
	      dir.path = g_build_filename (g_get_home_dir (), unescaped, NULL);
	      g_free (unescaped);
	    }
	  else
	    dir.path = unescaped;

	  g_array_append_val (array, dir);
	}

      g_strfreev (lines);
    }

  g_free (config_file_free);

  return (XdgDirEntry *)g_array_free (array, FALSE);
}

static XdgDirEntry *cached_xdg_dirs = NULL;
static GFileMonitor *cached_xdg_dirs_monitor = NULL;

static void
update_xdg_user_dir (const char *type, const char *path)
{
    char *argv[5];
    int i;

    i = 0;
    argv[i++] = (char *)"xdg-user-dirs-update";
    argv[i++] = (char *)"--set";
    argv[i++] = (char *)type;
    argv[i++] = (char *)path;
    argv[i++] = NULL;

    /* We do this sync, to avoid possible race-conditions
       if multiple dirs change at the same time. Its
       blocking the main thread, but these updates should
       be very rare and very fast. */
    g_spawn_sync (NULL,
                  argv, NULL,
                  G_SPAWN_SEARCH_PATH |
                  G_SPAWN_STDOUT_TO_DEV_NULL |
                  G_SPAWN_STDERR_TO_DEV_NULL,
                  NULL, NULL,
                  NULL, NULL, NULL, NULL);
}

static void
xdg_dir_changed (NemoFile *file,
		 XdgDirEntry *dir)
{
	GFile *location, *dir_location;
	char *path;

	location = nemo_file_get_location (file);
	dir_location = g_file_new_for_path (dir->path);
	if (!g_file_equal (location, dir_location)) {
		path = g_file_get_path (location);

		if (path) {
			g_free (dir->path);
			dir->path = path;

			update_xdg_user_dir (dir->type, dir->path);
			g_reload_user_special_dirs_cache ();
			schedule_user_dirs_changed ();
			desktop_dir_changed ();
			/* Icon might have changed */
			nemo_file_invalidate_attributes (file, NEMO_FILE_ATTRIBUTE_INFO);
		}
	}
	g_object_unref (location);
	g_object_unref (dir_location);
}

static void
xdg_dir_cache_changed_cb (G_GNUC_UNUSED GFileMonitor  *monitor,
			  G_GNUC_UNUSED GFile *file,
			  G_GNUC_UNUSED GFile *other_file,
			  GFileMonitorEvent event_type)
{
	if (event_type == G_FILE_MONITOR_EVENT_CHANGED ||
	    event_type == G_FILE_MONITOR_EVENT_CREATED) {
		update_xdg_dir_cache ();
	}
}

static int user_dirs_changed_tag = 0;

static gboolean
emit_user_dirs_changed_idle (G_GNUC_UNUSED gpointer data)
{
	g_signal_emit_by_name (nemo_signaller_get_current (),
			       "user_dirs_changed");
	user_dirs_changed_tag = 0;
	return FALSE;
}

static void
schedule_user_dirs_changed (void)
{
	if (user_dirs_changed_tag == 0) {
		user_dirs_changed_tag = g_idle_add (emit_user_dirs_changed_idle, NULL);
	}
}

static void
unschedule_user_dirs_changed (void)
{
	if (user_dirs_changed_tag != 0) {
		g_source_remove (user_dirs_changed_tag);
		user_dirs_changed_tag = 0;
	}
}

static void
free_xdg_dir_cache (void)
{
	int i;

	if (cached_xdg_dirs != NULL) {
		for (i = 0; cached_xdg_dirs[i].type != NULL; i++) {
			if (cached_xdg_dirs[i].file != NULL) {
				nemo_file_monitor_remove (cached_xdg_dirs[i].file,
							      &cached_xdg_dirs[i]);
				g_signal_handlers_disconnect_by_func (cached_xdg_dirs[i].file,
								      G_CALLBACK (xdg_dir_changed),
								      &cached_xdg_dirs[i]);
				nemo_file_unref (cached_xdg_dirs[i].file);
			}
			g_free (cached_xdg_dirs[i].type);
			g_free (cached_xdg_dirs[i].path);
		}
		g_free (cached_xdg_dirs);
	}
}

static void
destroy_xdg_dir_cache (void)
{
	free_xdg_dir_cache ();
	unschedule_user_dirs_changed ();
	desktop_dir_changed ();

	if (cached_xdg_dirs_monitor != NULL) {
		g_object_unref  (cached_xdg_dirs_monitor);
		cached_xdg_dirs_monitor = NULL;
	}
}

static void
update_xdg_dir_cache (void)
{
	GFile *file;
	char *config_file, *uri;
	int i;

	free_xdg_dir_cache ();
	g_reload_user_special_dirs_cache ();
	schedule_user_dirs_changed ();
	desktop_dir_changed ();

	cached_xdg_dirs = parse_xdg_dirs (NULL);

	for (i = 0 ; cached_xdg_dirs[i].type != NULL; i++) {
		cached_xdg_dirs[i].file = NULL;
		if (strcmp (cached_xdg_dirs[i].path, g_get_home_dir ()) != 0) {
			uri = g_filename_to_uri (cached_xdg_dirs[i].path, NULL, NULL);
			cached_xdg_dirs[i].file = nemo_file_get_by_uri (uri);
			nemo_file_monitor_add (cached_xdg_dirs[i].file,
						   &cached_xdg_dirs[i],
						   NEMO_FILE_ATTRIBUTE_INFO);
			g_signal_connect (cached_xdg_dirs[i].file,
					  "changed", G_CALLBACK (xdg_dir_changed), &cached_xdg_dirs[i]);
			g_free (uri);
		}
	}

	if (cached_xdg_dirs_monitor == NULL) {
		config_file = g_build_filename (g_get_user_config_dir (),
						     "user-dirs.dirs", NULL);
		file = g_file_new_for_path (config_file);
		cached_xdg_dirs_monitor = g_file_monitor_file (file, 0, NULL, NULL);
		g_signal_connect (cached_xdg_dirs_monitor, "changed",
				  G_CALLBACK (xdg_dir_cache_changed_cb), NULL);
		g_object_unref (file);
		g_free (config_file);

		eel_debug_call_at_shutdown (destroy_xdg_dir_cache);
	}
}

/* Returns: (transfer full): free with g_free */
char *
nemo_get_xdg_dir (const char *type)
{
	int i;

	if (cached_xdg_dirs == NULL) {
		update_xdg_dir_cache ();
	}

	for (i = 0 ; cached_xdg_dirs != NULL && cached_xdg_dirs[i].type != NULL; i++) {
		if (strcmp (cached_xdg_dirs[i].type, type) == 0) {
			return g_strdup (cached_xdg_dirs[i].path);
		}
	}
	if (strcmp ("DESKTOP", type) == 0) {
		return g_build_filename (g_get_home_dir (), DESKTOP_DIRECTORY_NAME, NULL);
	}
	if (strcmp ("TEMPLATES", type) == 0) {
		return g_build_filename (g_get_home_dir (), "Templates", NULL);
	}

	return g_strdup (g_get_home_dir ());
}

static char *
get_desktop_path (void)
{
	if (nemo_config_get_boolean (nemo_preferences, NEMO_PREFERENCES_DESKTOP_IS_HOME_DIR)) {
		return g_strdup (g_get_home_dir());
	} else {
		return nemo_get_xdg_dir ("DESKTOP");
	}
}

/**
 * nemo_get_desktop_directory:
 *
 * Get the path for the directory containing files on the desktop.
 *
 * Return value: (transfer full): the directory path. Free with g_free.
 **/
char *
nemo_get_desktop_directory (void)
{
	char *desktop_directory;

	desktop_directory = get_desktop_path ();

	/* Don't try to create a home directory */
	if (!nemo_config_get_boolean (nemo_preferences, NEMO_PREFERENCES_DESKTOP_IS_HOME_DIR)) {
		if (!g_file_test (desktop_directory, G_FILE_TEST_EXISTS)) {
			g_mkdir (desktop_directory, DEFAULT_DESKTOP_DIRECTORY_MODE);
			/* FIXME bugzilla.gnome.org 41286:
			 * How should we handle the case where this mkdir fails?
			 * Note that nemo_application_startup will refuse to launch if this
			 * directory doesn't get created, so that case is OK. But the directory
			 * could be deleted after Nemo was launched, and perhaps
			 * there is some bad side-effect of not handling that case.
			 */
		}
	}

	return desktop_directory;
}

/* Returns: (transfer full): unref with g_object_unref */
GFile *
nemo_get_desktop_location (void)
{
	char *desktop_directory;
	GFile *res;

	desktop_directory = get_desktop_path ();

	res = g_file_new_for_path (desktop_directory);
	g_free (desktop_directory);
	return res;
}


/**
 * nemo_get_desktop_directory_uri:
 *
 * Get the uri for the directory containing files on the desktop.
 *
 * Return value: (transfer full): the directory path. Free with g_free.
 **/
char *
nemo_get_desktop_directory_uri (void)
{
	char *desktop_path;
	char *desktop_uri;

	desktop_path = nemo_get_desktop_directory ();
	desktop_uri = g_filename_to_uri (desktop_path, NULL, NULL);
	g_free (desktop_path);

	return desktop_uri;
}

/* Returns: (transfer full): free with g_free */
char *
nemo_get_desktop_directory_uri_no_create (void)
{
	char *desktop_path;
	char *desktop_uri;

	desktop_path = get_desktop_path ();
	desktop_uri = g_filename_to_uri (desktop_path, NULL, NULL);
	g_free (desktop_path);

	return desktop_uri;
}

/* Returns: (transfer full): free with g_free */
char *
nemo_get_home_directory_uri (void)
{
	return  g_filename_to_uri (g_get_home_dir (), NULL, NULL);
}


gboolean
nemo_should_use_templates_directory (void)
{
	char *dir;
	gboolean res;

	dir = nemo_get_xdg_dir ("TEMPLATES");
	res = strcmp (dir, g_get_home_dir ()) != 0;
	g_free (dir);
	return res;
}

/* Returns: (transfer full): free with g_free */
char *
nemo_get_templates_directory (void)
{
	return nemo_get_xdg_dir ("TEMPLATES");
}

void
nemo_ensure_valid_templates_directory (void)
{
    /* This is called only at points where the user would expect the Templates
     * directory to exist:
     * - Template prefs, adding a template via DND or the "New" button.
     * - Template prefs - the Folder icon.
     * - MenuBar->Go->Templates - navigating directly to the Templates dir.
     *
     * Otherwise, we're fine without it, if the user never intends to use templates.
     */
    g_autofree gchar *templates_dir = NULL;
    const char *home_dir;

    templates_dir = nemo_get_xdg_dir ("TEMPLATES");
    home_dir = g_get_home_dir ();

    if (g_strcmp0 (templates_dir, home_dir) == 0) {
        // XDG_TEMPLATES_DIR is $HOME/, fix it.
        g_autofree gchar *fixed_templates_dir = NULL;
        fixed_templates_dir = g_build_filename (home_dir, "Templates", NULL);
        g_mkdir (fixed_templates_dir, DEFAULT_NEMO_DIRECTORY_MODE);

        update_xdg_user_dir ("TEMPLATES", fixed_templates_dir);
        g_reload_user_special_dirs_cache ();
        update_xdg_dir_cache ();
    } else {
        // XDG_TEMPLATES_DIR is reasonable, let's make sure it exists.
        g_mkdir (templates_dir, DEFAULT_NEMO_DIRECTORY_MODE);
    }
}

/* Returns: (transfer full): free with g_free */
char *
nemo_get_templates_directory_uri (void)
{
	char *directory, *uri;

	directory = nemo_get_templates_directory ();
	uri = g_filename_to_uri (directory, NULL, NULL);
	g_free (directory);
	return uri;
}

/* Returns: (transfer full): free with g_free */
char *
nemo_get_searches_directory (void)
{
	char *user_dir;
	char *searches_dir;

	user_dir = nemo_get_user_directory ();
	searches_dir = g_build_filename (user_dir, "searches", NULL);
	g_free (user_dir);

	if (!g_file_test (searches_dir, G_FILE_TEST_EXISTS))
		g_mkdir (searches_dir, DEFAULT_NEMO_DIRECTORY_MODE);

	return searches_dir;
}

/* These need to be reset to NULL when desktop_is_home_dir changes */
static GFile *desktop_dir = NULL;
static GFile *desktop_dir_dir = NULL;
static char *desktop_dir_filename = NULL;
static gboolean desktop_dir_changed_callback_installed = FALSE;


static void
desktop_dir_changed (void)
{
	if (desktop_dir) {
		g_object_unref (desktop_dir);
	}
	if (desktop_dir_dir) {
		g_object_unref (desktop_dir_dir);
	}
	g_free (desktop_dir_filename);
	desktop_dir = NULL;
	desktop_dir_dir = NULL;
	desktop_dir_filename = NULL;
}

static void
desktop_dir_changed_callback (G_GNUC_UNUSED gpointer callback_data)
{
	desktop_dir_changed ();
}

static void
update_desktop_dir (void)
{
	char *path;
	char *dirname;

	path = get_desktop_path ();
	desktop_dir = g_file_new_for_path (path);

	dirname = g_path_get_dirname (path);
	desktop_dir_dir = g_file_new_for_path (dirname);
	g_free (dirname);
	desktop_dir_filename = g_path_get_basename (path);
	g_free (path);
}

gboolean
nemo_is_home_directory_file (GFile *dir,
				 const char *filename)
{
	char *dirname;
	static GFile *home_dir_dir = NULL;
	static char *home_dir_filename = NULL;

	if (home_dir_dir == NULL) {
		dirname = g_path_get_dirname (g_get_home_dir ());
		home_dir_dir = g_file_new_for_path (dirname);
		g_free (dirname);
		home_dir_filename = g_path_get_basename (g_get_home_dir ());
	}

	return (g_file_equal (dir, home_dir_dir) &&
		strcmp (filename, home_dir_filename) == 0);
}

gboolean
nemo_is_home_directory (GFile *dir)
{
	static GFile *home_dir = NULL;

	if (home_dir == NULL) {
		home_dir = g_file_new_for_path (g_get_home_dir ());
	}

	return g_file_equal (dir, home_dir);
}

gboolean
nemo_is_root_directory (GFile *dir)
{
	static GFile *root_dir = NULL;

	if (root_dir == NULL) {
		root_dir = g_file_new_for_path ("/");
	}

	return g_file_equal (dir, root_dir);
}


gboolean
nemo_is_desktop_directory_file (GFile *dir,
				    const char *file)
{

	if (!desktop_dir_changed_callback_installed) {
		g_signal_connect_swapped (nemo_preferences, "changed::" NEMO_PREFERENCES_DESKTOP_IS_HOME_DIR,
					  G_CALLBACK(desktop_dir_changed_callback),
					  NULL);
		desktop_dir_changed_callback_installed = TRUE;
	}

	if (desktop_dir == NULL) {
		update_desktop_dir ();
	}

	return (g_file_equal (dir, desktop_dir_dir) &&
		strcmp (file, desktop_dir_filename) == 0);
}

gboolean
nemo_is_desktop_directory (GFile *dir)
{

	if (!desktop_dir_changed_callback_installed) {
		g_signal_connect_swapped (nemo_preferences, "changed::" NEMO_PREFERENCES_DESKTOP_IS_HOME_DIR,
					  G_CALLBACK(desktop_dir_changed_callback),
					  NULL);
		desktop_dir_changed_callback_installed = TRUE;
	}

	if (desktop_dir == NULL) {
		update_desktop_dir ();
	}

	return g_file_equal (dir, desktop_dir);
}


/**
 * nemo_get_gmc_desktop_directory:
 *
 * Get the path for the directory containing the legacy gmc desktop.
 *
 * Return value: (transfer full): the directory path. Free with g_free.
 **/
char *
nemo_get_gmc_desktop_directory (void)
{
	return g_build_filename (g_get_home_dir (), LEGACY_DESKTOP_DIRECTORY_NAME, NULL);
}

/* Returns: (transfer full): free with g_free */
char *
nemo_get_exe_path (void)
{
	char *exe = NULL;

#ifdef G_OS_WIN32
	{
		wchar_t wexe[32768];
		DWORD n = GetModuleFileNameW (NULL, wexe, G_N_ELEMENTS (wexe));

		if (n > 0 && n < G_N_ELEMENTS (wexe)) {
			exe = g_utf16_to_utf8 ((const gunichar2 *) wexe, -1, NULL, NULL, NULL);
		}
	}
#else
#if defined(__FreeBSD__) || defined(__DragonFly__)
	/* procfs is not mounted there by default */
	{
		int mib[4] = { CTL_KERN, KERN_PROC, KERN_PROC_PATHNAME, -1 };
		char path[4096];
		size_t len = sizeof (path);

		if (sysctl (mib, G_N_ELEMENTS (mib), path, &len, NULL, 0) == 0 && len > 1) {
			exe = g_strndup (path, len);
		}
	}
#endif
	if (exe == NULL) {
		exe = g_file_read_link ("/proc/self/exe", NULL);
	}
	if (exe == NULL) {
		/* NetBSD, or FreeBSD with procfs */
		exe = g_file_read_link ("/proc/curproc/file", NULL);
	}
#endif

	return exe;
}

/* The compiled-in prefix is only right for an install that stayed where it was
 * built. A relocated Linux prefix and every Windows layout put the data beside
 * the executable instead, which is why translations and the info-bar docs went
 * missing there. Resolve against the running binary first, fall back to the
 * built-in path. */
static const char *
runtime_dir_for (const char *tail, const char *built_in)
{
	char *exe;
	char *dir;
	const char *result = built_in;
	int i;

	exe = nemo_get_exe_path ();
	if (exe == NULL) {
		return built_in;
	}

	dir = g_path_get_dirname (exe);
	g_free (exe);

	/* bin/<exe> in a prefix, then the flat Windows layout. */
	for (i = 0; i < 2; i++) {
		char *candidate = i == 0
			? g_build_filename (dir, "..", tail, NULL)
			: g_build_filename (dir, tail, NULL);

		if (g_file_test (candidate, G_FILE_TEST_IS_DIR)) {
			result = g_strdup (candidate);
			g_free (candidate);
			break;
		}
		g_free (candidate);
	}

	g_free (dir);
	return result;
}

/* Returns: (transfer none): kept for the life of the process */
const char *
nemo_get_data_dir (void)
{
	static const char *dir;

	if (g_once_init_enter (&dir)) {
		g_once_init_leave (&dir,
				   runtime_dir_for ("share" G_DIR_SEPARATOR_S NEMO_APP_SLUG,
						    NEMO_DATADIR));
	}
	return dir;
}

/* Returns: (transfer none): kept for the life of the process */
const char *
nemo_get_locale_dir (void)
{
	static const char *dir;

	if (g_once_init_enter (&dir)) {
		g_once_init_leave (&dir,
				   runtime_dir_for ("share" G_DIR_SEPARATOR_S "locale",
						    LOCALEDIR));
	}
	return dir;
}

/* Where our sibling programs are - the document converters search uses. "" means
 * we could not work it out and the caller should just let PATH answer.
 * Returns: (transfer none): kept for the life of the process */
const char *
nemo_get_bin_dir (void)
{
	static const char *dir;

	if (g_once_init_enter (&dir)) {
		g_once_init_leave (&dir, runtime_dir_for ("bin", ""));
	}
	return dir;
}

#ifndef G_OS_WIN32
/* Put dir at the front of a colon-separated environment list, unless it is
 * already on it. fallback stands in for a list that is not set at all. */
static void
prepend_env_dir (const char *var, const char *dir, const char *fallback)
{
	const char *current = g_getenv (var);
	gboolean present = FALSE;
	char *joined;

	if (current == NULL || *current == '\0') {
		current = fallback;
	}

	if (current != NULL) {
		char **parts = g_strsplit (current, G_SEARCHPATH_SEPARATOR_S, -1);
		int i;

		for (i = 0; parts[i] != NULL && !present; i++) {
			present = strcmp (parts[i], dir) == 0;
		}
		g_strfreev (parts);
	}

	if (present) {
		return;
	}

	joined = (current != NULL && *current != '\0')
		? g_strconcat (dir, G_SEARCHPATH_SEPARATOR_S, current, NULL)
		: g_strdup (dir);
	g_setenv (var, joined, TRUE);
	g_free (joined);
}
#endif

/* A relocatable prefix used to be entered through a shell wrapper that set this
 * up, which meant two files where one would do. The program does it for itself
 * now. Everything scanned per data dir - actions, search helpers, icons, mime -
 * comes off XDG_DATA_DIRS, and GLib caches that list the first time anything
 * asks for it, so this has to run before anything else does.
 *
 * Nothing to do on Windows: the layout is flat and GLib there already reads the
 * share dir beside the exe. */
void
nemo_setup_runtime_environment (void)
{
#ifndef G_OS_WIN32
	char *exe;
	char *bindir;
	char *base;
	char *prefix;
	char *share;

	exe = nemo_get_exe_path ();
	if (exe == NULL) {
		return;
	}

	bindir = g_path_get_dirname (exe);
	g_free (exe);

	base = g_path_get_basename (bindir);
	prefix = strcmp (base, "bin") == 0 ? g_path_get_dirname (bindir) : g_strdup (bindir);
	g_free (base);

	share = g_build_filename (prefix, "share", NULL);

	/* An uninstalled build tree has no share dir beside the binary. Leave the
	 * session's own environment alone there. */
	if (g_file_test (share, G_FILE_TEST_IS_DIR)) {
		char *schemas = g_build_filename (share, "glib-2.0", "schemas", NULL);

		prepend_env_dir ("XDG_DATA_DIRS", share, "/usr/local/share:/usr/share");
		prepend_env_dir ("PATH", bindir, NULL);

		/* Settings are not on GSettings any more, so there is normally no
		 * schema here - but an action file may still name someone else's. */
		if (g_file_test (schemas, G_FILE_TEST_IS_DIR)) {
			prepend_env_dir ("GSETTINGS_SCHEMA_DIR", schemas, NULL);
		}
		g_free (schemas);
	}

	g_free (share);
	g_free (prefix);
	g_free (bindir);
#endif
}

/* g_get_system_data_dirs with the repeats taken out. The prefix wrapper, the
 * launcher and the packed exe's environment each put our own share dir on
 * XDG_DATA_DIRS, and GLib on win32 adds the exe's share dir on top of that -
 * so anything scanned per data dir (actions, search helpers, themes) showed
 * up once per copy.
 * Returns: (transfer none): kept for the life of the process */
const char * const *
nemo_get_system_data_dirs (void)
{
	static char **dirs;

	if (g_once_init_enter (&dirs)) {
		const char * const *sys = g_get_system_data_dirs ();
		GPtrArray *out = g_ptr_array_new ();
		GHashTable *seen = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);
		guint i;

		for (i = 0; sys != NULL && sys[i] != NULL; i++) {
			char *key;

			if (sys[i][0] == '\0') {
				continue;
			}
			key = g_canonicalize_filename (sys[i], NULL);
#ifdef G_OS_WIN32
			{
				char *folded = g_utf8_casefold (key, -1);
				g_free (key);
				key = folded;
			}
#endif
			if (g_hash_table_contains (seen, key)) {
				g_free (key);
				continue;
			}
			g_hash_table_add (seen, key);
			g_ptr_array_add (out, g_strdup (sys[i]));
		}
		g_ptr_array_add (out, NULL);
		g_hash_table_destroy (seen);
		g_once_init_leave (&dirs, (char **) g_ptr_array_free (out, FALSE));
	}
	return (const char * const *) dirs;
}

/* Returns: (transfer full): free with g_free */
char *
nemo_get_data_file_path (const char *partial_path)
{
	char *path;
	char *user_directory;

	/* first try the user's home directory */
	user_directory = nemo_get_user_directory ();
	path = g_build_filename (user_directory, partial_path, NULL);
	g_free (user_directory);
	if (g_file_test (path, G_FILE_TEST_EXISTS)) {
		return path;
	}
	g_free (path);

	/* next try the shared directory */
	path = g_build_filename (nemo_get_data_dir (), partial_path, NULL);
	if (g_file_test (path, G_FILE_TEST_EXISTS)) {
		return path;
	}
	g_free (path);

	return NULL;
}

/* Returns: (transfer full): free with g_free */
char *
nemo_ensure_unique_file_name (const char *directory_uri,
				  const char *base_name,
				  const char *extension)
{
	GFileInfo *info;
	char *filename;
	GFile *dir, *child;
	int copy;
	char *res;

	dir = g_file_new_for_uri (directory_uri);

	info = g_file_query_info (dir, G_FILE_ATTRIBUTE_STANDARD_TYPE, 0, NULL, NULL);
	if (info == NULL) {
		g_object_unref (dir);
		return NULL;
	}
	g_object_unref (info);

	filename = g_strdup_printf ("%s%s",
				    base_name,
				    extension);
	child = g_file_get_child (dir, filename);
	g_free (filename);

	copy = 1;
	while ((info = g_file_query_info (child, G_FILE_ATTRIBUTE_STANDARD_TYPE, 0, NULL, NULL)) != NULL) {
		g_object_unref (info);
		g_object_unref (child);

		filename = g_strdup_printf ("%s-%d%s",
					    base_name,
					    copy,
					    extension);
		child = g_file_get_child (dir, filename);
		g_free (filename);

		copy++;
	}

	res = g_file_get_uri (child);
	g_object_unref (child);
	g_object_unref (dir);

	return res;
}

/* Returns: (transfer full): free with g_free */
char *
nemo_unique_temporary_file_name (void)
{
	const char *prefix = "/tmp/nemo-temp-file";
	char *file_name;
	int fd;

	file_name = g_strdup_printf ("%sXXXXXX", prefix);

	fd = g_mkstemp (file_name);
	if (fd == -1) {
		g_free (file_name);
		file_name = NULL;
	} else {
		close (fd);
	}

	return file_name;
}

/* Returns: (transfer full): unref with g_object_unref */
GFile *
nemo_find_existing_uri_in_hierarchy (GFile *location)
{
	GFileInfo *info;
	GFile *tmp;

	g_assert (location != NULL);

	location = g_object_ref (location);
	while (location != NULL) {
		info = g_file_query_info (location,
					  G_FILE_ATTRIBUTE_STANDARD_NAME,
					  0, NULL, NULL);
		if (info != NULL) {
			g_object_unref (info);
			return location;
		}
		tmp = location;
		location = g_file_get_parent (location);
		g_object_unref (tmp);
	}

	return location;
}

/**
 * nemo_find_file_insensitive
 *
 * Attempt to find a file case-insentively. If the path can be found, the
 * returned file maps directly to it. Otherwise, a file using the
 * originally-cased path is returned. This function performs might perform
 * I/O.
 *
 * Return value: (transfer full): a #GFile to a child specified by @name. Unref with g_object_unref.
 **/
GFile *
nemo_find_file_insensitive (GFile *parent, const gchar *name)
{
	gchar **split_path;
	gchar *component;
	GFile *file, *next;
	gint i;

	split_path = g_strsplit (name, G_DIR_SEPARATOR_S, -1);

	file = g_object_ref (parent);

	for (i = 0; (component = split_path[i]) != NULL; i++) {
		if (!(next = nemo_find_file_insensitive_next (file,
		                                                  component))) {
			/* File does not exist */
			g_object_unref (file);
			file = NULL;
			break;
		}
		g_object_unref (file);
		file = next;
	}
	g_strfreev (split_path);

	if (file) {
		return file;
	}
	return g_file_get_child (parent, name);
}

static GFile *
nemo_find_file_insensitive_next (GFile *parent, const gchar *name)
{
	GFileEnumerator *children;
	GFileInfo *info;
	gboolean use_utf8, found;
	char *filename, *case_folded_name, *utf8_collation_key, *ascii_collation_key, *child_key;
	GFile *file;
	const char *child_name, *compare_key;

	/* First check the given version */
	file = g_file_get_child (parent, name);
	if (g_file_query_exists (file, NULL)) {
		return file;
	}
	g_object_unref (file);

	ascii_collation_key = g_ascii_strdown (name, -1);
	use_utf8 = g_utf8_validate (name, -1, NULL);
	utf8_collation_key = NULL;
	if (use_utf8) {
		case_folded_name = g_utf8_casefold (name, -1);
		utf8_collation_key = g_utf8_collate_key (case_folded_name, -1);
		g_free (case_folded_name);
	}

	/* Enumerate and compare insensitive */
	filename = NULL;
	children = nemo_enumerate_children (parent,
	                                      G_FILE_ATTRIBUTE_STANDARD_NAME,
	                                      0, NULL, NULL);
	if (children != NULL) {
		while ((info = g_file_enumerator_next_file (children, NULL, NULL))) {
			child_name = g_file_info_get_name (info);

			if (use_utf8 && g_utf8_validate (child_name, -1, NULL)) {
				gchar *case_folded;

				case_folded = g_utf8_casefold (child_name, -1);
				child_key = g_utf8_collate_key (case_folded, -1);
				g_free (case_folded);
				compare_key = utf8_collation_key;
			} else {
				child_key = g_ascii_strdown (child_name, -1);
				compare_key = ascii_collation_key;
			}

			found = strcmp (child_key, compare_key) == 0;
			g_free (child_key);
			if (found) {
				filename = g_strdup (child_name);
				break;
			}
		}
		g_file_enumerator_close (children, NULL, NULL);
		g_object_unref (children);
	}

	g_free (ascii_collation_key);
	g_free (utf8_collation_key);

	if (filename) {
		file = g_file_get_child (parent, filename);
		g_free (filename);
		return file;
	}

	return NULL;
}

static gboolean
have_program_in_path (const char *name)
{
	char *path;
	gboolean result;

	path = g_find_program_in_path (name);
	result = (path != NULL);
	g_free (path);
	return result;
}

gboolean
nemo_is_file_roller_installed (void)
{
	static int installed = - 1;

	if (installed < 0) {
		if (have_program_in_path ("file-roller")) {
			installed = 1;
		} else {
			installed = 0;
		}
	}

	return installed > 0 ? TRUE : FALSE;
}

#define GSM_NAME  "org.gnome.SessionManager"
#define GSM_PATH "/org/gnome/SessionManager"
#define GSM_INTERFACE "org.gnome.SessionManager"

/* The following values come from
 * http://www.gnome.org/~mccann/gnome-session/docs/gnome-session.html#org.gnome.SessionManager.Inhibit
 */
#define INHIBIT_LOGOUT (1U)
#define INHIBIT_SUSPEND (4U)

static GDBusConnection *
get_dbus_connection (void)
{
	static GDBusConnection *conn = NULL;

	if (conn == NULL) {
		GError *error = NULL;

	        conn = g_bus_get_sync (G_BUS_TYPE_SESSION, NULL, &error);

		if (conn == NULL) {
	                g_warning ("Could not connect to session bus: %s", error->message);
			g_error_free (error);
		}
	}

	return conn;
}

/**
 * nemo_inhibit_power_manager:
 * @message: a human readable message for the reason why power management
 *       is being suspended.
 *
 * Inhibits the power manager from logging out or suspending the machine
 * (e.g. whenever Nemo is doing file operations).
 *
 * Returns: an integer cookie, which must be passed to
 *    nemo_uninhibit_power_manager() to resume
 *    normal power management.
 */
int
nemo_inhibit_power_manager (const char *message)
{
	GDBusConnection *connection;
	GVariant *result;
	GError *error = NULL;
	guint cookie = 0;

	g_return_val_if_fail (message != NULL, -1);

        connection = get_dbus_connection ();

        if (connection == NULL) {
                return -1;
        }

	result = g_dbus_connection_call_sync (connection,
					      GSM_NAME,
					      GSM_PATH,
					      GSM_INTERFACE,
					      "Inhibit",
					      g_variant_new ("(susu)",
							     NEMO_APP_SLUG,
							     (guint) 0,
							     message,
							     (guint) (INHIBIT_LOGOUT | INHIBIT_SUSPEND)),
					      G_VARIANT_TYPE ("(u)"),
					      G_DBUS_CALL_FLAGS_NO_AUTO_START,
					      -1,
					      NULL,
					      &error);

	if (error != NULL) {
		g_warning ("Could not inhibit power management: %s", error->message);
		g_error_free (error);
		return -1;
	}

	g_variant_get (result, "(u)", &cookie);
	g_variant_unref (result);

	return (int) cookie;
}

/**
 * nemo_uninhibit_power_manager:
 * @cookie: the cookie value returned by nemo_inhibit_power_manager()
 *
 * Uninhibits power management. This function must be called after the task
 * which inhibited power management has finished, or the system will not
 * return to normal power management.
 */
void
nemo_uninhibit_power_manager (gint cookie)
{
	GDBusConnection *connection;
	GVariant *result;
	GError *error = NULL;

	g_return_if_fail (cookie > 0);

	connection = get_dbus_connection ();

	if (connection == NULL) {
		return;
	}

	result = g_dbus_connection_call_sync (connection,
					      GSM_NAME,
					      GSM_PATH,
					      GSM_INTERFACE,
					      "Uninhibit",
					      g_variant_new ("(u)", (guint) cookie),
					      NULL,
					      G_DBUS_CALL_FLAGS_NO_AUTO_START,
					      -1,
					      NULL,
					      &error);

	if (result == NULL) {
		g_warning ("Could not uninhibit power management: %s", error->message);
		g_error_free (error);
		return;
	}

	g_variant_unref (result);
}

/* Returns TRUE if the file is in XDG_DATA_DIRS or
   in "~/.gnome2/". This is used for deciding
   if a desktop file is "trusted" based on the path */
gboolean
nemo_is_in_system_dir (GFile *file)
{
	const char * const * data_dirs;
	char *path, *gnome2;
	int i;
	gboolean res;

	if (!g_file_is_native (file)) {
		return FALSE;
	}

	path = g_file_get_path (file);

	res = FALSE;

	data_dirs = g_get_system_data_dirs ();
	for (i = 0; path != NULL && data_dirs[i] != NULL; i++) {
		if (g_str_has_prefix (path, data_dirs[i])) {
			res = TRUE;
			break;
		}

	}

	if (!res) {
		/* Panel desktop files are here, trust them */
		gnome2 = g_build_filename (g_get_home_dir (), ".gnome2", NULL);
		if (g_str_has_prefix (path, gnome2)) {
			res = TRUE;
		}
		g_free (gnome2);
	}
	g_free (path);

	return res;
}

/* Returns: (transfer full): free with g_hash_table_destroy */
GHashTable *
nemo_trashed_files_get_original_directories (GList *files,
						 GList **unhandled_files)
{
	GHashTable *directories;
	NemoFile *file, *original_file, *original_dir;
	GList *l, *m;

	directories = NULL;

	if (unhandled_files != NULL) {
		*unhandled_files = NULL;
	}

	for (l = files; l != NULL; l = l->next) {
		file = NEMO_FILE (l->data);
		original_file = nemo_file_get_trash_original_file (file);

		original_dir = NULL;
		if (original_file != NULL) {
			original_dir = nemo_file_get_parent (original_file);
		}

		if (original_dir != NULL) {
			if (directories == NULL) {
				directories = g_hash_table_new_full (g_direct_hash, g_direct_equal,
								     (GDestroyNotify) nemo_file_unref,
								     (GDestroyNotify) nemo_file_list_free);
			}
			nemo_file_ref (original_dir);
			m = g_hash_table_lookup (directories, original_dir);
			if (m != NULL) {
				g_hash_table_steal (directories, original_dir);
				nemo_file_unref (original_dir);
			}
			m = g_list_append (m, nemo_file_ref (file));
			g_hash_table_insert (directories, original_dir, m);
		} else if (unhandled_files != NULL) {
			*unhandled_files = g_list_append (*unhandled_files, nemo_file_ref (file));
		}

		if (original_file != NULL) {
			nemo_file_unref (original_file);
		}

		if (original_dir != NULL) {
			nemo_file_unref (original_dir);
		}
	}

	return directories;
}

static GList *
locations_from_file_list (GList *file_list)
{
	NemoFile *file;
	GList *l, *ret;

	ret = NULL;

	for (l = file_list; l != NULL; l = l->next) {
		file = NEMO_FILE (l->data);
		ret = g_list_prepend (ret, nemo_file_get_location (file));
	}

	return g_list_reverse (ret);
}

typedef struct {
	GHashTable *original_dirs_hash;
	GtkWindow  *parent_window;
} RestoreFilesData;

static void
ensure_dirs_task_ready_cb (G_GNUC_UNUSED GObject *_source,
			   G_GNUC_UNUSED GAsyncResult *res,
			   gpointer user_data)
{
	NemoFile *original_dir;
	GFile *original_dir_location;
	GList *original_dirs, *files, *locations, *l;
	RestoreFilesData *data = user_data;

	original_dirs = g_hash_table_get_keys (data->original_dirs_hash);
	for (l = original_dirs; l != NULL; l = l->next) {
		original_dir = NEMO_FILE (l->data);
		original_dir_location = nemo_file_get_location (original_dir);

		files = g_hash_table_lookup (data->original_dirs_hash, original_dir);
		locations = locations_from_file_list (files);

		nemo_file_operations_move
			(locations, NULL,
			 original_dir_location,
			 data->parent_window,
			 NULL, NULL);

		g_list_free_full (locations, g_object_unref);
		g_object_unref (original_dir_location);
	}

	g_list_free (original_dirs);

	g_hash_table_unref (data->original_dirs_hash);
	g_free (data);
}

static void
ensure_dirs_task_thread_func (GTask *task,
			      G_GNUC_UNUSED gpointer source,
			      gpointer task_data,
			      GCancellable *cancellable)
{
	RestoreFilesData *data = task_data;
	NemoFile *original_dir;
	GFile *original_dir_location;
	GList *original_dirs, *l;

	original_dirs = g_hash_table_get_keys (data->original_dirs_hash);
	for (l = original_dirs; l != NULL; l = l->next) {
		original_dir = NEMO_FILE (l->data);
		original_dir_location = nemo_file_get_location (original_dir);

		g_file_make_directory_with_parents (original_dir_location, cancellable, NULL);
		g_object_unref (original_dir_location);
	}

	g_task_return_pointer (task, NULL, NULL);
}

static void
restore_files_ensure_parent_directories (GHashTable *original_dirs_hash,
					 GtkWindow  *parent_window)
{
	RestoreFilesData *data;
	GTask *ensure_dirs_task;

	data = g_new0 (RestoreFilesData, 1);
	data->parent_window = parent_window;
	data->original_dirs_hash = g_hash_table_ref (original_dirs_hash);

	ensure_dirs_task = g_task_new (NULL, NULL, ensure_dirs_task_ready_cb, data);
	g_task_set_task_data (ensure_dirs_task, data, NULL);
	g_task_run_in_thread (ensure_dirs_task, ensure_dirs_task_thread_func);
	g_object_unref (ensure_dirs_task);
}

void
nemo_restore_files_from_trash (GList *files,
				   GtkWindow *parent_window)
{
	NemoFile *file;
	GHashTable *original_dirs_hash;
	GList *unhandled_files, *l;
	char *message, *file_name;

	original_dirs_hash = nemo_trashed_files_get_original_directories (files, &unhandled_files);

	for (l = unhandled_files; l != NULL; l = l->next) {
		file = NEMO_FILE (l->data);
		file_name = nemo_file_get_display_name (file);
		message = g_strdup_printf (_("Could not determine original location of \"%s\" "), file_name);
		g_free (file_name);

		eel_show_warning_dialog (message,
					 _("The item cannot be restored from trash"),
					 parent_window);
		g_free (message);
	}

	if (original_dirs_hash != NULL) {
		restore_files_ensure_parent_directories (original_dirs_hash, parent_window);
		g_hash_table_unref (original_dirs_hash);
	}

	nemo_file_list_unref (unhandled_files);
}

typedef struct {
	NemoMountGetContent callback;
	gpointer user_data;
} GetContentTypesData;

static void
get_types_cb (GObject *source_object,
	      GAsyncResult *res,
	      gpointer user_data)
{
	GetContentTypesData *data;
	char **types;

	data = user_data;
	types = g_mount_guess_content_type_finish (G_MOUNT (source_object), res, NULL);

	g_object_set_data_full (source_object,
				"nemo-content-type-cache",
				g_strdupv (types),
				(GDestroyNotify)g_strfreev);

	if (data->callback) {
		data->callback ((const char **) types, data->user_data);
	}
	g_strfreev (types);
	g_free (data);
}

void
nemo_get_x_content_types_for_mount_async (GMount *mount,
					      NemoMountGetContent callback,
					      GCancellable *cancellable,
					      gpointer user_data)
{
	char **cached;
	GetContentTypesData *data;

	if (mount == NULL) {
		if (callback) {
			callback (NULL, user_data);
		}
		return;
	}

	cached = g_object_get_data (G_OBJECT (mount), "nemo-content-type-cache");
	if (cached != NULL) {
		if (callback) {
			callback ((const char **) cached, user_data);
		}
		return;
	}

	data = g_new0 (GetContentTypesData, 1);
	data->callback = callback;
	data->user_data = user_data;

	g_mount_guess_content_type (mount,
				    FALSE,
				    cancellable,
				    get_types_cb,
				    data);
}

/* Returns: (transfer full): free with g_strfreev */
char **
nemo_get_cached_x_content_types_for_mount (GMount *mount)
{
	char **cached;

	if (mount == NULL) {
		return NULL;
	}

	cached = g_object_get_data (G_OBJECT (mount), "nemo-content-type-cache");
	if (cached != NULL) {
		return g_strdupv (cached);
	}

	return NULL;
}

static void
debug_icon_names (const gchar *format, ...)
{
    static gboolean debug_removable_device_icons = FALSE;
    static gsize once_init = 0;

    if (g_once_init_enter (&once_init)) {
        debug_removable_device_icons = g_getenv ("NEMO_DEBUG_DEVICE_ICONS") != NULL;

        g_once_init_leave (&once_init, 1);
    }

    if (!debug_removable_device_icons) {
        return;
    }

    va_list args;
    va_start (args, format);
    g_logv (NULL, G_LOG_LEVEL_MESSAGE, format, args);
    va_end(args);
}

static gchar *
get_best_name (GtkIconTheme *icon_theme,
                        GIcon        *gicon,
                        G_GNUC_UNUSED const gchar  *dev_name,
                        const gchar  *type_name)
{
    gchar *icon_name = NULL;

    if (G_IS_THEMED_ICON (gicon)) {
        const gchar * const *names;
        guint i;

        // TODO: We should just use what gicon Gio gives us and let the theme deal with it.
        // but currently everywhere nemo needs this is looking for icon names, so this function
        // emulates using fallbacks to avoid a lot of refactoring elsewhere.

        names = g_themed_icon_get_names (G_THEMED_ICON (gicon));
        for (i = 0; i != g_strv_length ((gchar **) names); i++) {
            const gchar *name = names[i];
            if (gtk_icon_theme_has_icon (icon_theme, name)) {
                icon_name = g_strdup (name);
                break;
            }
        }
    }

    // Don't ever allow a non-symbolic icon
    // drawn from Gio gunixmounts.c

    if (icon_name == NULL || !g_str_has_suffix (icon_name, "symbolic")) {
        g_free (icon_name);

        if (g_strcmp0 (type_name, "volume") == 0 ||
            g_strcmp0 (type_name, "drive") == 0) {
            icon_name = g_strdup ("drive-removable-media-symbolic");
        }
        else {
            icon_name = g_strdup ("drive-harddisk-symbolic");
        }
    }

    return icon_name;
}

/* Returns: (transfer full): free with g_free */
gchar *
nemo_get_mount_icon_name (GMount *mount)
{
    GtkIconTheme *icon_theme;
    GIcon *gicon;
    gchar *icon_name;
    gchar *dev_name;

    g_return_val_if_fail (mount != NULL, NULL);

    dev_name = g_mount_get_name (mount);
    icon_name = NULL;

    gicon = g_mount_get_symbolic_icon (mount);
    icon_theme = gtk_icon_theme_get_default ();
    icon_name = get_best_name (icon_theme, gicon, dev_name, "mount");
    g_clear_object (&gicon);

    debug_icon_names ("mount %s: using icon name '%s'", dev_name, icon_name);
    g_free (dev_name);

    return icon_name;
}

/* Returns: (transfer full): free with g_free */
gchar *
nemo_get_volume_icon_name (GVolume *volume)
{
    GtkIconTheme *icon_theme;
    GIcon *gicon;
    gchar *icon_name;
    gchar *dev_name;

    g_return_val_if_fail (volume != NULL, NULL);

    dev_name = g_volume_get_name (volume);
    icon_name = NULL;

    gicon = g_volume_get_symbolic_icon (volume);
    icon_theme = gtk_icon_theme_get_default ();
    icon_name = get_best_name (icon_theme, gicon, dev_name, "volume");
    g_clear_object (&gicon);

    debug_icon_names ("volume %s: using icon name '%s'", dev_name, icon_name);
    g_free (dev_name);

    return icon_name;
}

/* Returns: (transfer full): free with g_free */
gchar *
nemo_get_drive_icon_name (GDrive *drive)
{
    GtkIconTheme *icon_theme;
    GIcon *gicon;
    gchar *icon_name;
    gchar *dev_name;

    g_return_val_if_fail (drive != NULL, NULL);

    dev_name = g_drive_get_name (drive);

    gicon = g_drive_get_symbolic_icon (drive);
    icon_theme = gtk_icon_theme_get_default ();
    icon_name = get_best_name (icon_theme, gicon, dev_name, "drive");
    g_clear_object (&gicon);

    debug_icon_names ("drive %s: using icon name '%s'", dev_name, icon_name);
    g_free (dev_name);

    return icon_name;
}

/* Returns: (transfer full): free with g_free */
gchar *
nemo_get_best_guess_file_mimetype (const gchar *filename,
                                   GFileInfo   *info,
                                   goffset      size)
{
    /* This is an attempt to do a better job at identifying file types than
     * the current gio implementation.
     *
     * The current behavior for empty (0 size) size files, is to return
     * "text/plain" so users can touch a file, or create an empty one in their
     * file manager, and immediately edit it.
     *
     * This behavior currently applies regardless of whether or not a file has
     * an extension.
     *
     * More discussion: https://bugzilla.gnome.org/show_bug.cgi?id=755795
     *
     * What we do here instead is take a file's extension into account if the file
     * is zero-length.  If, by doing so, we have a high confidence that a file is
     * a certain type, we go ahead and use that type.  We only fall back to Gio's
     * zero-length implementation if the extension is unknown, or it has none.
     *
     * - Files that are NOT zero-length are treated the same as before.
     * - Files that we are not certain about are treated the same as before.
     *
     * Again, this only has an effect on zero-length, known-extension files.  My
     * argument for this differing from the standing implementation is that if I
     * create a file with a particular extension, I did so for a reason, and I'm not
     * going to do something silly like make foo.mp3 and attempt to open it with my
     * media player.  I do, however, expect that if I touch a foo.h file and open it,
     * it will open up in my source code editor, and *not* my general purpose plain-
     * text handler.
     */

    g_return_val_if_fail (filename != NULL, g_strdup ("application/octet-stream"));
    g_return_val_if_fail (info != NULL, g_strdup ("application/octet-stream"));

    gchar *mime_type = NULL;

    /* A directory's type is always inode/directory - never guess it from the
     * name. On Windows dirs can report size 0, which otherwise dropped them into
     * the zero-length name-guess path below and mislabelled folders. */
    if (nemo_dir_enum_file_type (info) == G_FILE_TYPE_DIRECTORY) {
        return g_strdup ("inode/directory");
    }

    if (size > 0) {
        /* Default behavior */
        mime_type = g_strdup (g_file_info_get_attribute_string (info, G_FILE_ATTRIBUTE_STANDARD_CONTENT_TYPE));

        if (mime_type == NULL) {
            mime_type = g_strdup (g_file_info_get_attribute_string (info, G_FILE_ATTRIBUTE_STANDARD_FAST_CONTENT_TYPE));
        }
    } else {
        gboolean uncertain;
        gchar *guessed_type = NULL;

        /* Only give the file basename, not the full path.  a) We may not have it yet, and
         * b) we don't want g_content_type_guess to keep going and snoop the file.  This will
         * keep the guess based entirely on the extension, if there is one.
         */
        guessed_type = g_content_type_guess (filename, NULL, 0, &uncertain);

        /* Uncertain means, it's not a registered extension, so we fall back to our (gio's)
         * normal behavior - text/plain (currently at least.) */
        if (!uncertain) {
            mime_type = guessed_type;
        } else {
            mime_type = g_strdup (g_file_info_get_attribute_string (info, G_FILE_ATTRIBUTE_STANDARD_CONTENT_TYPE));
            g_free (guessed_type);
        }
    }

    return mime_type;
}

gboolean
nemo_treating_root_as_normal (void)
{
    static gboolean root_is_normal = FALSE;
    static gsize once_init = 0;

    // We only need to set this at startup then cache the result, as we check
    // quite a bit in various parts of the code. local_command_line reaches
    // here before the settings are open (--fix-cache as root), which used to
    // cache a critical-and-FALSE for the rest of the run.
    if (!nemo_config_is_ready ()) {
        return FALSE;
    }

    if (g_once_init_enter (&once_init)) {
        root_is_normal = nemo_config_get_boolean (nemo_config_get_group ("preferences"),
                                                  "treat-root-as-normal");

        g_once_init_leave (&once_init, 1);
    }

    return root_is_normal;
}

gboolean
nemo_user_is_root (void)
{
    static gboolean elevated = FALSE;
    static gsize once_init = 0;

    // We only need to set this at startup then cache the result, as we check
    // quite a bit in various parts of the code.
    if (g_once_init_enter (&once_init)) {
        elevated = (geteuid () == 0);
        g_once_init_leave (&once_init, 1);
    }

    return elevated;
}

static gint
sort_by_length (GMount *a, GMount *b)
{
    g_autoptr(GFile) a_root = g_mount_get_root (a);
    g_autoptr(GFile) b_root = g_mount_get_root (b);
    g_autofree gchar *a_uri = g_file_get_uri (a_root);
    g_autofree gchar *b_uri = g_file_get_uri (b_root);
    gint a_len = g_utf8_strlen (a_uri, -1);
    gint b_len = g_utf8_strlen (b_uri, -1);

    return b_len - a_len;
}

gboolean
nemo_uri_is_at_or_under (const char *uri, const char *root)
{
	gsize root_len;

	if (uri == NULL || root == NULL || !g_str_has_prefix (uri, root)) {
		return FALSE;
	}

	root_len = strlen (root);
	return root_len == 0 || root[root_len - 1] == '/' ||
	       uri[root_len] == '\0' || uri[root_len] == '/';
}

/* Returns: (transfer full): unref with g_object_unref */
GMount *
nemo_get_mount_for_location_safe (GFile *location)
{
    GVolumeMonitor *monitor;
    GList *mounts = NULL;
    GList *mount_iter;
    GMount *ret = NULL;


    monitor = g_volume_monitor_get ();
    mounts = g_volume_monitor_get_mounts (monitor);

    mounts = g_list_sort (mounts, (GCompareFunc) sort_by_length);

    for (mount_iter = mounts; mount_iter != NULL; mount_iter = mount_iter->next) {
        GMount *mount = G_MOUNT (mount_iter->data);
        GFile *mount_location = g_mount_get_root (mount);
        gchar *mount_root_uri = g_file_get_uri (mount_location);
        gchar *location_uri = g_file_get_uri (location);

        /* Boundary-guarded: a bare prefix test would let a longer sibling
         * mount root (tried first by the descending-length sort) swallow a
         * location that really sits under the true root. */
        if (nemo_uri_is_at_or_under (location_uri, mount_root_uri)) {
            // Add a ref for our match, as it will lose one when the list is freed.
            ret = g_object_ref (mount);
        }

        g_free (mount_root_uri);
        g_free (location_uri);
        g_object_unref (mount_location);

        if (ret != NULL)
            break;
    }

    g_list_free_full (mounts, (GDestroyNotify) g_object_unref);
    g_object_unref (monitor);

    return ret;
}

gboolean
nemo_location_is_network_safe (GFile *location)
{
    GVolume *volume;
    GMount *mount;
    gboolean is_network = FALSE;

    mount = nemo_get_mount_for_location_safe (location);
    if (mount != NULL) {
        volume = g_mount_get_volume (mount);
        if (volume != NULL) {
            g_autofree gchar *identifier = g_volume_get_identifier (volume, "class");

            if (g_strcmp0 (identifier, "network") == 0) {
                is_network = TRUE;
            }

            g_object_unref (volume);
        }

        g_object_unref (mount);
    }

    return is_network;
}

gboolean
nemo_path_is_network_safe (const gchar *path)
{
    g_autoptr(GFile) location = g_file_new_for_path (path);

    return nemo_location_is_network_safe (location);
}

/* Three places used to name a drive root three different ways: gio's display
   name is the basename, which is "\" for every drive alike; the volume monitor
   says "(C:) Windows"; and the sidebar built "Windows (C:)" itself. The drive
   letter is the part that identifies it, so that is what all of them show now. */
gboolean
nemo_location_is_drive_root (G_GNUC_UNUSED GFile *location)
{
#ifdef G_OS_WIN32
    g_autofree gchar *path = NULL;

    if (location == NULL || !g_file_is_native (location)) {
        return FALSE;
    }

    path = g_file_get_path (location);

    /* Exactly "X:\" or "X:/" - a path one character longer is already inside. */
    return path != NULL
           && g_ascii_isalpha (path[0])
           && path[1] == ':'
           && (path[2] == '\\' || path[2] == '/')
           && path[3] == '\0';
#else
    return FALSE;
#endif
}

/* Returns: (transfer full): free with g_free */
gchar *
nemo_get_drive_root_name (G_GNUC_UNUSED GFile *location)
{
#ifdef G_OS_WIN32
    g_autofree gchar *path = NULL;

    if (!nemo_location_is_drive_root (location)) {
        return NULL;
    }

    path = g_file_get_path (location);

    /* Spelled the way the user would type it, and the way every other Windows
       program writes it: an upper-case letter and a trailing separator. */
    return g_strdup_printf ("%c:%c", g_ascii_toupper (path[0]),
                            nemo_path_get_display_separator ());
#else
    return NULL;
#endif
}

/* One path per line, with the line ending the local shells and editors expect.
   A path pasted into cmd.exe or notepad has to carry CRLF to arrive as separate
   lines. */
#ifdef G_OS_WIN32
#define PATH_LIST_SEPARATOR "\r\n"
#else
#define PATH_LIST_SEPARATOR "\n"
#endif

/* Text for a list of locations (GFile *), one per line, for the clipboard.
   Anything with no local path - a remote uri - contributes its uri instead, so
   the text is never silently shorter than what was selected. Returns NULL when
   nothing at all could be named.
   Returns: (transfer full): free with g_free */
gchar *
nemo_build_path_list_text (GList *locations,
                           gchar  separator)
{
    GString *text;
    GList *l;
    guint count = 0;

    text = g_string_new (NULL);

    for (l = locations; l != NULL; l = l->next) {
        g_autofree gchar *path = NULL;

        if (l->data == NULL) {
            continue;
        }

        path = g_file_get_path (G_FILE (l->data));
        if (path != NULL) {
            nemo_path_apply_separator (path, separator);
        } else {
            /* A uri's slashes are not separators, so it is copied as it stands. */
            path = g_file_get_uri (G_FILE (l->data));
        }
        if (path == NULL) {
            continue;
        }

        if (count > 0) {
            g_string_append (text, PATH_LIST_SEPARATOR);
        }
        g_string_append (text, path);
        count++;
    }

    if (count == 0) {
        g_string_free (text, TRUE);
        return NULL;
    }

    return g_string_free (text, FALSE);
}

/* Windows takes either separator, so which one gets shown is the user's choice.
   Everywhere else there is only one and all of this stays out of the way. */
#ifdef G_OS_WIN32

static gboolean separator_preference_watched = FALSE;
static gchar    display_separator = '\\';
static gboolean slash_input_allowed = TRUE;

static void
separator_preference_changed (G_GNUC_UNUSED gpointer callback_data)
{
    g_autofree gchar *choice = nemo_config_get_string (nemo_windows_preferences,
                                                       NEMO_PREFERENCES_PATH_SEPARATOR);

    display_separator = (g_strcmp0 (choice, "slash") == 0) ? '/' : '\\';
    slash_input_allowed = nemo_config_get_boolean (nemo_windows_preferences,
                                                   NEMO_PREFERENCES_ALLOW_SLASH_INPUT);
}

static void
watch_separator_preferences (void)
{
    if (separator_preference_watched) {
        return;
    }

    nemo_global_preferences_init ();
    g_signal_connect_swapped (nemo_windows_preferences,
                              "changed::" NEMO_PREFERENCES_PATH_SEPARATOR,
                              G_CALLBACK (separator_preference_changed), NULL);
    g_signal_connect_swapped (nemo_windows_preferences,
                              "changed::" NEMO_PREFERENCES_ALLOW_SLASH_INPUT,
                              G_CALLBACK (separator_preference_changed), NULL);
    separator_preference_watched = TRUE;
    separator_preference_changed (NULL);
}

#endif

/* Called from nemo_global_preferences_init, and it has to stay there. GObject
   runs handlers in the order they were connected, so anything that spells out a
   path from its own "changed::path-separator" handler reads a stale separator
   unless this one was connected first. That put the breadcrumb a step behind
   the window title. */
void
nemo_path_init_display_separator (void)
{
#ifdef G_OS_WIN32
    watch_separator_preferences ();
#endif
}

gchar
nemo_path_get_display_separator (void)
{
#ifdef G_OS_WIN32
    watch_separator_preferences ();
    return display_separator;
#else
    return G_DIR_SEPARATOR;
#endif
}

/* The separator that is not being shown - what "copy the path the other way"
   means. There is only one off Windows, so it answers the same as the first. */
gchar
nemo_path_get_other_separator (void)
{
#ifdef G_OS_WIN32
    return (nemo_path_get_display_separator () == '/') ? '\\' : '/';
#else
    return G_DIR_SEPARATOR;
#endif
}

void
nemo_rename_region (const char *name,
                    gboolean    whole,
                    int        *start_offset,
                    int        *end_offset)
{
    *start_offset = 0;
    *end_offset = -1;

    if (!whole &&
        !nemo_config_get_boolean (nemo_preferences, NEMO_PREFERENCES_RENAME_SELECTS_WHOLE_NAME)) {
        eel_filename_get_rename_region (name, start_offset, end_offset);
    }
}

/* Rewrites in place - both separators are one ASCII byte, so nothing moves.
   Only ever hand this a local path; a uri's slashes are not separators. */
void
nemo_path_apply_separator (G_GNUC_UNUSED gchar *path,
                           G_GNUC_UNUSED gchar  separator)
{
#ifdef G_OS_WIN32
    gchar other = (separator == '/') ? '\\' : '/';
    gchar *p;

    if (path == NULL) {
        return;
    }

    for (p = path; *p != '\0'; p++) {
        if (*p == other) {
            *p = separator;
        }
    }
#endif
}

/* The part of a path no shortening may drop, since it says which drive, share or
   tree the rest is on. It always ends with the separator. */
static gchar *
path_anchor (const gchar *path, gchar separator, const gchar **rest)
{
	const gchar *p;
	int seen;

	/* A share's root is the server and the share together; neither alone is a
	   place anything opens. */
	if (path[0] == separator && path[1] == separator && path[2] != '\0') {
		seen = 0;
		for (p = path + 2; *p != '\0'; p++) {
			if (*p == separator && ++seen == 2) {
				break;
			}
		}
		*rest = (*p == '\0') ? p : p + 1;
		if (*p == '\0') {
			return g_strdup_printf ("%s%c", path, separator);
		}
		return g_strndup (path, p + 1 - path);
	}

	if (g_ascii_isalpha (path[0]) && path[1] == ':' && path[2] == separator) {
		*rest = path + 3;
		return g_strndup (path, 3);
	}

	if (path[0] == separator) {
		*rest = path + 1;
		return g_strndup (path, 1);
	}

	*rest = path;
	return g_strdup ("");
}

/* A hidden folder keeps the letter after its dot, or every one of them would
   come out as a bare dot. */
static gchar *
path_initial (const gchar *part)
{
	const gchar *end;

	end = g_utf8_next_char (part);
	if (*part == '.' && *end != '\0') {
		end = g_utf8_next_char (end);
	}
	return g_strndup (part, end - part);
}

static void
path_forms_add (GPtrArray *forms, gchar *candidate)
{
	const gchar *last;

	last = g_ptr_array_index (forms, forms->len - 1);
	if (g_utf8_strlen (candidate, -1) < g_utf8_strlen (last, -1)) {
		g_ptr_array_add (forms, candidate);
	} else {
		g_free (candidate);
	}
}

static gchar *
path_forms_join (const gchar *anchor, gchar **items, guint count, gchar separator)
{
	GString *out;
	guint i;

	out = g_string_new (anchor);
	for (i = 0; i < count; i++) {
		if (i > 0) {
			g_string_append_c (out, separator);
		}
		g_string_append (out, items[i]);
	}
	return g_string_free (out, FALSE);
}

/* Every shortening of a path, longest first, each shorter than the one before.
   An ellipsis eats the middle a folder at a time, so what is left is still
   readable; only when that has run out do the folders above the last one drop
   to their initials. The root and the last folder's name survive every step,
   since those are what tell one tab from another. A path under home reads as ~
   once it is being shortened at all.
   Returns: (transfer full): free with g_strfreev */
gchar **
nemo_path_forms (const gchar *path,
                 gchar        separator,
                 const gchar *home)
{
	GPtrArray *forms;
	gchar sep[2] = { separator, '\0' };
	gchar **split, **parts, **items;
	gchar *anchor;
	const gchar *rest;
	gsize home_len;
	guint count, last, keep, i;

	forms = g_ptr_array_new ();
	if (path == NULL) {
		g_ptr_array_add (forms, NULL);
		return (gchar **) g_ptr_array_free (forms, FALSE);
	}
	g_ptr_array_add (forms, g_strdup (path));

	home_len = (home != NULL) ? strlen (home) : 0;
	while (home_len > 1 && home[home_len - 1] == separator) {
		home_len--;
	}
	if (home_len > 1 && strncmp (path, home, home_len) == 0 &&
	    (path[home_len] == '\0' || path[home_len] == separator)) {
		anchor = g_strdup_printf ("~%c", separator);
		rest = path + home_len;
		while (*rest == separator) {
			rest++;
		}
	} else {
		anchor = path_anchor (path, separator, &rest);
	}

	/* Empty pieces come from doubled or trailing separators. */
	split = g_strsplit (rest, sep, -1);
	parts = g_new0 (gchar *, g_strv_length (split) + 1);
	count = 0;
	for (i = 0; split[i] != NULL; i++) {
		if (split[i][0] != '\0') {
			parts[count++] = split[i];
		}
	}

	if (count == 0) {
		if (anchor[0] == '~') {
			path_forms_add (forms, g_strdup ("~"));
		}
	} else {
		last = count - 1;
		path_forms_add (forms, path_forms_join (anchor, parts, count, separator));

		/* The ellipsis stands in for everything between what is kept and the
		   last folder, so the first steps replace one folder with three
		   characters and are dropped for being no shorter. */
		items = g_new0 (gchar *, count + 2);
		for (i = 0; i < last; i++) {
			items[i] = parts[i];
		}
		for (keep = last; keep-- > 0;) {
			items[keep] = (gchar *) "...";
			items[keep + 1] = parts[last];
			path_forms_add (forms, path_forms_join (anchor, items, keep + 2, separator));
			items[keep] = parts[keep];
		}

		/* Initials only get a look in on a shallow path, where an ellipsis costs
		   more than the folders it would cover. */
		for (i = 0; i < last; i++) {
			items[i] = path_initial (parts[i]);
		}
		items[last] = parts[last];
		path_forms_add (forms, path_forms_join (anchor, items, count, separator));

		for (i = 0; i < last; i++) {
			g_free (items[i]);
		}
		g_free (items);
	}

	g_free (parts);
	g_strfreev (split);
	g_free (anchor);

	g_ptr_array_add (forms, NULL);
	return (gchar **) g_ptr_array_free (forms, FALSE);
}

/* Windows shells never print a ~, and the drive a path is on is worth more than
   the four characters a ~ would save.
   Returns: (transfer none): owned by GLib */
const gchar *
nemo_path_display_home (void)
{
#ifdef G_OS_WIN32
	return NULL;
#else
	return g_get_home_dir ();
#endif
}

/* The longest of a set of forms that fits, or the shortest when none of them
   does. The forms get shorter as the index rises, so the first hit wins. */
guint
nemo_path_form_for_width (const gint *widths, guint count, gint avail)
{
	guint i;

	for (i = 0; i < count; i++) {
		if (widths[i] <= avail) {
			return i;
		}
	}
	return count - 1;
}

/* Picks a form for each tab so the row fits in avail. The tab in front shows as
   much of its path as the row can spare and is not capped, since the others are
   already as wide as they may get. They shorten together, one step at a time,
   until it can show the whole path or they have nothing left to give. Past the
   shortest form the notebook scrolls, as it always did. */
void
nemo_path_forms_fit (guint              count,
                     const gint *const *widths,
                     const guint       *form_counts,
                     guint              active,
                     gint               min_px,
                     gint               max_px,
                     gint               avail,
                     guint             *chosen)
{
	guint i, rung = 0;
	gint others = 0;

	/* They start where the one needing the most shortening first fits the cap,
	   so the row reads as one set rather than a jumble. */
	for (i = 0; i < count; i++) {
		if (i != active) {
			rung = MAX (rung, nemo_path_form_for_width (widths[i], form_counts[i], max_px));
		}
	}

	for (;;) {
		gboolean more = FALSE;

		others = 0;
		for (i = 0; i < count; i++) {
			if (i == active) {
				continue;
			}
			chosen[i] = MIN (rung, form_counts[i] - 1);
			others += CLAMP (widths[i][chosen[i]], min_px, max_px);
			if (chosen[i] + 1 < form_counts[i] && widths[i][chosen[i]] > min_px) {
				more = TRUE;
			}
		}
		if (!more || others + widths[active][0] <= avail) {
			break;
		}
		rung++;
	}

	chosen[active] = nemo_path_form_for_width (widths[active], form_counts[active],
						   MAX (avail - others, min_px));
}

void
nemo_path_apply_display_separator (gchar *path)
{
    nemo_path_apply_separator (path, nemo_path_get_display_separator ());
}

/* The parse name of a location, spelled with the separator the user picked.
   A remote location keeps its uri untouched.
   Returns: (transfer full): free with g_free */
gchar *
nemo_location_get_display_name (GFile *location)
{
    gchar *name;

    if (location == NULL) {
        return NULL;
    }

    name = g_file_get_parse_name (location);

    if (g_file_is_native (location)) {
        nemo_path_apply_display_separator (name);
    }

    return name;
}

/* A typed location is refused when it leans on a separator that has been turned
   off. Only the forward slash can be: a backslash is always a separator, and on
   POSIX it is a legal character in a name, so nothing is reserved there. */
gboolean
nemo_path_input_is_allowed (const gchar *text)
{
#ifdef G_OS_WIN32
    watch_separator_preferences ();

    if (slash_input_allowed || text == NULL || strchr (text, '/') == NULL) {
        return TRUE;
    }

    /* A uri is all slashes by definition and has nothing to do with this. */
    return strstr (text, "://") != NULL;
#else
    (void) text;
    return TRUE;
#endif
}

/* The value of the Ext column: the tail of the name after the last dot, without
 * the dot. A dot is often just part of a name, so the tail only counts when it
 * looks the part - short, letters and digits only, at least one letter. NULL when
 * there is not one worth showing.
 * Returns: (transfer full): free with g_free */
gchar *
nemo_filename_get_extension (const gchar *name)
{
	const gchar *tail;
	const gchar *p;
	gboolean has_alpha = FALSE;

	if (name == NULL) {
		return NULL;
	}

	tail = strrchr (name, '.');

	/* No dot, only the hidden-file dot up front, or nothing after it. */
	if (tail == NULL || tail == name || tail[1] == '\0') {
		return NULL;
	}

	for (p = tail + 1; *p != '\0'; p++) {
		if (!g_ascii_isalnum (*p)) {
			return NULL;
		}
		if (g_ascii_isalpha (*p)) {
			has_alpha = TRUE;
		}
	}

	/* Too long to be an extension, or all digits (a version, a date). */
	if (p - (tail + 1) > 10 || !has_alpha) {
		return NULL;
	}

	return g_strdup (tail + 1);
}

static gint archive_mount_state = -1;

/* GIO lists the archive scheme only when gvfs is running and has the archive
   backend installed. Never on Windows, where there is no gvfs. */
void
nemo_archive_mount_init (void)
{
	const gchar * const *schemes;

	if (archive_mount_state != -1) {
		return;
	}

	schemes = g_vfs_get_supported_uri_schemes (g_vfs_get_default ());
	archive_mount_state = schemes != NULL && g_strv_contains (schemes, "archive");
}

gboolean
nemo_archive_mount_supported (void)
{
	nemo_archive_mount_init ();
	return archive_mount_state == 1;
}

/* gvfs wants the archive's own URI escaped twice as the host part:
   archive://file%253A%252F%252F.../
   Returns: (transfer full): unref with g_object_unref */
GFile *
nemo_archive_mount_location (GFile *archive)
{
	g_autofree gchar *uri = g_file_get_uri (archive);
	g_autofree gchar *once = g_uri_escape_string (uri, NULL, FALSE);
	g_autofree gchar *twice = g_uri_escape_string (once, NULL, FALSE);
	g_autofree gchar *mount_uri = g_strconcat ("archive://", twice, "/", NULL);

	return g_file_new_for_uri (mount_uri);
}

#if !defined (NEMO_OMIT_SELF_CHECK)

void
nemo_self_check_file_utilities (void)
{
}

#endif /* !NEMO_OMIT_SELF_CHECK */

gboolean
nemo_content_type_is_a (const char *content_type, const char *mime_type)
{
	if (content_type == NULL) {
		return FALSE;
	}

	if (g_content_type_is_a (content_type, mime_type)) {
		return TRUE;
	}

#ifdef G_OS_WIN32
	{
		g_autofree char *mime = g_content_type_get_mime_type (content_type);
		gsize len = strlen (mime_type);

		if (mime == NULL) {
			return FALSE;
		}

		if (len >= 2 && g_str_has_suffix (mime_type, "/*")) {
			return strncmp (mime, mime_type, len - 1) == 0;
		}

		return g_ascii_strcasecmp (mime, mime_type) == 0;
	}
#else
	return FALSE;
#endif
}
