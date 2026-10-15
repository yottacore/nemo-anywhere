/* -*- Mode: C; indent-tabs-mode: f; c-basic-offset: 4; tab-width: 4 -*- */

/* nemo-global-preferences.c - Nemo specific preference keys and
                                   functions.

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

   Authors: Ramiro Estrugo <ramiro@eazel.com>
*/

#include <config.h>
#include "nemo-global-preferences.h"

#include "nemo-file-utilities.h"
#include "nemo-file.h"
#include <eel/eel-glib-extensions.h>
#include <eel/eel-gtk-extensions.h>
#include <eel/eel-stock-dialogs.h>
#include <eel/eel-string.h>
#include <eel/eel-debug.h>
#include <glib/gi18n.h>
#ifdef G_OS_UNIX
#include <gio/gdesktopappinfo.h>
#endif

#define DEBUG_FLAG NEMO_DEBUG_PREFERENCES
#include <libnemo-private/nemo-debug.h>

NemoConfigGroup *nemo_preferences;
NemoConfigGroup *nemo_icon_view_preferences;
NemoConfigGroup *nemo_list_view_preferences;
NemoConfigGroup *nemo_compact_view_preferences;
NemoConfigGroup *nemo_desktop_preferences;
NemoConfigGroup *nemo_window_state;
NemoConfigGroup *nemo_plugin_preferences;
NemoConfigGroup *nemo_menu_config_preferences;
NemoConfigGroup *nemo_search_preferences;
NemoConfigGroup *nemo_media_handling_preferences;
NemoConfigGroup *nemo_appearance_preferences;
NemoConfigGroup *nemo_windows_preferences;

GTimeZone      *prefs_current_timezone;
gboolean        prefs_current_24h_time_format;
NemoDateFormat  prefs_current_date_format;

GTimer    *nemo_startup_timer;

static gboolean remember_folder_settings = FALSE;
static gboolean inherit_view_settings = TRUE;
static gboolean inherit_show_thumbnails_preference = FALSE;
static int      size_prefixes_preference = 0;

static gchar **file_roller_mimetypes = NULL;


GFileMonitor *tz_mon;

/*
 * Public functions
 * Returns: (transfer full): free with g_free
 */
char *
nemo_global_preferences_get_default_folder_viewer_preference_as_iid (void)
{
	int preference_value;
	const char *viewer_iid;

	preference_value =
		nemo_config_get_enum (nemo_preferences, NEMO_PREFERENCES_DEFAULT_FOLDER_VIEWER);

	if (preference_value == NEMO_DEFAULT_FOLDER_VIEWER_LIST_VIEW) {
		viewer_iid = NEMO_LIST_VIEW_IID;
	} else if (preference_value == NEMO_DEFAULT_FOLDER_VIEWER_COMPACT_VIEW) {
		viewer_iid = NEMO_COMPACT_VIEW_IID;
	} else {
		viewer_iid = NEMO_ICON_VIEW_IID;
	}

	return g_strdup (viewer_iid);
}

gboolean
nemo_global_preferences_get_inherit_view_settings (void)
{
    return inherit_view_settings;
}

gboolean
nemo_global_preferences_get_remember_folder_settings (void)
{
    return remember_folder_settings;
}

#ifdef G_OS_WIN32
/* The two can be set apart in preferences or with the dot-file item. Whatever
   value the attribute switch takes, the dot-file one is brought to it. */
void
nemo_global_preferences_set_show_all_hidden (gboolean show)
{
    nemo_config_set_boolean (nemo_preferences, NEMO_PREFERENCES_SHOW_HIDDEN_FILES, show);
    nemo_config_set_boolean (nemo_windows_preferences, NEMO_PREFERENCES_SHOW_DOT_FILES, show);
}
#endif

gboolean
nemo_global_preferences_get_inherit_show_thumbnails_preference (void)
{
    return inherit_show_thumbnails_preference;
}

int
nemo_global_preferences_get_size_prefix_preference (void)
{
    switch (size_prefixes_preference) {
        case 0: // base-10
            return G_FORMAT_SIZE_DEFAULT;
        case 1: // base-10 full
            return G_FORMAT_SIZE_DEFAULT |                           G_FORMAT_SIZE_LONG_FORMAT;
        case 2: // base-2
            return G_FORMAT_SIZE_DEFAULT | G_FORMAT_SIZE_IEC_UNITS;
        case 3: // base-2 full
            return G_FORMAT_SIZE_DEFAULT | G_FORMAT_SIZE_IEC_UNITS | G_FORMAT_SIZE_LONG_FORMAT;
    }

    return 0;
}

/* How many worker threads a job on this machine should ask for: a share of the
 * cores, as a percentage, rounded up so the answer is never nothing. Kept in
 * one place rather than per-format, so anything else that can be spread over
 * cores later reads the same number instead of inventing one of its own.
 * Compression is the only caller today. */
int
nemo_global_preferences_get_cpu_thread_count (void)
{
    int cores = (int) g_get_num_processors ();
    int percent = 50;

    if (nemo_config_is_ready ()) {
        percent = nemo_config_get_int (nemo_config_get_group (NEMO_PERFORMANCE_GROUP),
                                       NEMO_PREFERENCES_CPU_PERCENT);
    }

    percent = CLAMP (percent, 1, 100);

    return MAX (1, (cores * percent + 99) / 100);
}

/* Returns: (transfer full): free with g_free */
char *
nemo_global_preferences_get_desktop_iid (void)
{
    gboolean use_grid;
    const char *viewer_iid;

    use_grid = nemo_config_get_boolean (nemo_desktop_preferences, NEMO_PREFERENCES_USE_DESKTOP_GRID);

    if (use_grid) {
        viewer_iid = NEMO_DESKTOP_ICON_GRID_VIEW_IID;
    } else {
        viewer_iid = NEMO_DESKTOP_ICON_VIEW_IID;
    }

    return g_strdup (viewer_iid);
}

gint
nemo_global_preferences_get_tooltip_flags (void)
{
    NemoFileTooltipFlags flags = NEMO_FILE_TOOLTIP_FLAGS_NONE;

    if (nemo_config_get_boolean (nemo_preferences, NEMO_PREFERENCES_TOOLTIP_FILE_TYPE))
        flags |= NEMO_FILE_TOOLTIP_FLAGS_FILE_TYPE;
    if (nemo_config_get_boolean (nemo_preferences, NEMO_PREFERENCES_TOOLTIP_MOD_DATE))
        flags |= NEMO_FILE_TOOLTIP_FLAGS_MOD_DATE;
    if (nemo_config_get_boolean (nemo_preferences, NEMO_PREFERENCES_TOOLTIP_ACCESS_DATE))
        flags |= NEMO_FILE_TOOLTIP_FLAGS_ACCESS_DATE;
    if (nemo_config_get_boolean (nemo_preferences, NEMO_PREFERENCES_TOOLTIP_FULL_PATH))
        flags |= NEMO_FILE_TOOLTIP_FLAGS_PATH;
    if (nemo_config_get_boolean (nemo_preferences, NEMO_PREFERENCES_TOOLTIP_CREATED_DATE))
        flags |= NEMO_FILE_TOOLTIP_FLAGS_CREATED_DATE;
    return flags;
}

gboolean
nemo_global_preferences_should_load_plugin (const gchar *name, const gchar *key)
{
    gchar **disabled_list = nemo_config_get_strv (nemo_plugin_preferences, key);

    gboolean ret = TRUE;
    guint i = 0;

    for (i = 0; i < g_strv_length (disabled_list); i++) {
        if (g_strcmp0 (disabled_list[i], name) == 0)
            ret = FALSE;
    }

    g_strfreev (disabled_list);
    return ret;
}

static void
boolean_changed_cb (NemoConfigGroup *settings,
                    gchar     *key,
                    gboolean  *user_data)
{
    *user_data = nemo_config_get_boolean (settings, key);
}

static void
enum_changed_cb (NemoConfigGroup *settings,
                 gchar     *key,
                 int       *user_data)
{
    *user_data = nemo_config_get_enum (settings, key);
}

static void
setup_cached_pref_keys (void)
{
    g_signal_connect (nemo_preferences,
                      "changed::" NEMO_PREFERENCES_REMEMBER_FOLDER_SETTINGS,
                      G_CALLBACK (boolean_changed_cb), &remember_folder_settings);

    boolean_changed_cb (nemo_preferences, NEMO_PREFERENCES_REMEMBER_FOLDER_SETTINGS, &remember_folder_settings);

    g_signal_connect (nemo_preferences,
                      "changed::" NEMO_PREFERENCES_INHERIT_VIEW_SETTINGS,
                      G_CALLBACK (boolean_changed_cb), &inherit_view_settings);

    boolean_changed_cb (nemo_preferences, NEMO_PREFERENCES_INHERIT_VIEW_SETTINGS, &inherit_view_settings);

    g_signal_connect (nemo_preferences,
                      "changed::" NEMO_PREFERENCES_INHERIT_SHOW_THUMBNAILS,
                      G_CALLBACK (boolean_changed_cb), &inherit_show_thumbnails_preference);

    boolean_changed_cb (nemo_preferences, NEMO_PREFERENCES_INHERIT_SHOW_THUMBNAILS, &inherit_show_thumbnails_preference);

    g_signal_connect (nemo_preferences,
                      "changed::" NEMO_PREFERENCES_SIZE_PREFIXES,
                      G_CALLBACK (enum_changed_cb), &size_prefixes_preference);

    enum_changed_cb (nemo_preferences, NEMO_PREFERENCES_SIZE_PREFIXES, &size_prefixes_preference);
}


/* Returns: (transfer none): kept for the life of the process */
gchar **
nemo_global_preferences_get_fileroller_mimetypes (void)
{
    static gsize once_init = 0;

    if (g_once_init_enter (&once_init)) {
        if (nemo_is_file_roller_installed ()) {
#ifdef G_OS_UNIX
            GAppInfo *app_info;
            gchar ***results;
            gchar **result;
            gint i;

            results = g_desktop_app_info_search ("file-roller");

            if (results != NULL && results[0] != NULL) {
                const gchar *best;

                best = results[0][0];

                app_info = G_APP_INFO (g_desktop_app_info_new (best));

                if (app_info) {
                    file_roller_mimetypes = g_strdupv ((gchar **) g_app_info_get_supported_types (app_info));
                    g_object_unref (app_info);
                }

                if (app_info == NULL) {
                    g_warning ("Unable to retrieve list of file-roller mimetypes");
                }

                i = 0;
                result = results[i];

                while (result != NULL) {
                    g_strfreev (result);
                    result = results[++i];
                }

                g_free (results);
            }
#endif /* G_OS_UNIX */
        }

        g_once_init_leave (&once_init, 1);
    }

    return file_roller_mimetypes;
}


static void
on_time_data_changed (G_GNUC_UNUSED gpointer user_data)
{
    prefs_current_date_format = nemo_config_get_enum (nemo_preferences, NEMO_PREFERENCES_DATE_FORMAT);
    prefs_current_24h_time_format = nemo_desktop_settings_get_clock_use_24h ();

    if (prefs_current_timezone != NULL) {
        g_time_zone_unref (prefs_current_timezone);
    }

    prefs_current_timezone = g_time_zone_new_local ();
}

static void
setup_cached_time_data (void)
{
    GFile *tz;

    prefs_current_timezone = NULL;

    g_signal_connect_swapped (nemo_preferences,
                              "changed::" NEMO_PREFERENCES_DATE_FORMAT,
                              G_CALLBACK (on_time_data_changed), NULL);

    nemo_desktop_settings_watch ("clock-use-24h",
                                 G_CALLBACK (on_time_data_changed), NULL);


    tz = g_file_new_for_path ("/etc/localtime");

    tz_mon = g_file_monitor_file (tz, 0, NULL, NULL);
    g_object_unref (tz);

    g_signal_connect_swapped (tz_mon,
                              "changed",
                              G_CALLBACK (on_time_data_changed), NULL);

    on_time_data_changed (NULL);
}

void
nemo_global_preferences_init (void)
{
	static gboolean initialized = FALSE;

	if (initialized) {
		return;
	}

	initialized = TRUE;

	nemo_config_init ();
	nemo_desktop_settings_init ();

	/* Groups are owned by the config store, not by us - no refs to drop. */
	nemo_preferences                = nemo_config_get_group ("preferences");
	nemo_window_state               = nemo_config_get_group ("window-state");
	nemo_icon_view_preferences      = nemo_config_get_group ("icon-view");
	nemo_list_view_preferences      = nemo_config_get_group ("list-view");
	nemo_compact_view_preferences   = nemo_config_get_group ("compact-view");
	nemo_desktop_preferences        = nemo_config_get_group ("desktop");
	nemo_plugin_preferences         = nemo_config_get_group ("plugins");
	nemo_menu_config_preferences    = nemo_config_get_group ("preferences.menu-config");
	nemo_search_preferences         = nemo_config_get_group ("search");
	nemo_media_handling_preferences = nemo_config_get_group ("media-handling");
	nemo_appearance_preferences     = nemo_config_get_group ("appearance");
	nemo_windows_preferences        = nemo_config_get_group ("windows");

    setup_cached_pref_keys ();
    setup_cached_time_data ();
    nemo_path_init_display_separator ();

    eel_debug_call_at_shutdown (nemo_global_preferences_finalize);
}

void
nemo_global_preferences_finalize (void)
{
    g_strfreev (file_roller_mimetypes);

    g_object_unref (tz_mon);

    nemo_desktop_settings_finalize ();
    nemo_config_shutdown ();
}
