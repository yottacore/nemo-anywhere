/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */
/*
 * nemo-main-application: Nemo application subclass for standard windows
 *
 * Copyright (C) 1999, 2000 Red Hat, Inc.
 * Copyright (C) 2000, 2001 Eazel, Inc.
 * Copyright (C) 2010, Cosimo Cecchi <cosimoc@gnome.org>
 *
 * Nemo is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 *
 * Nemo is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Suite 500, MA 02110-1335, USA.
 *
 */

#include <config.h>
#include <nemo-build-number.h>

#include "nemo-main-application.h"

#if ENABLE_EMPTY_VIEW
#include "nemo-bookmark-list.h"
#include "nemo-empty-view.h"
#endif /* ENABLE_EMPTY_VIEW */

#include "nemo-freedesktop-dbus.h"
#include "nemo-icon-view.h"
#include "nemo-image-properties-page.h"
#include "nemo-list-view.h"
#include "nemo-previewer.h"
#include "nemo-progress-ui-handler.h"
#include "nemo-self-check-functions.h"
#include "nemo-splash.h"
#include "nemo-window.h"
#include "nemo-window-bookmarks.h"
#include "nemo-window-manage-views.h"
#include "nemo-window-private.h"
#include "nemo-window-slot.h"
#include "nemo-statusbar.h"
#include "nemo-notebook.h"
#include "nemo-tab-move.h"

#include <libnemo-private/nemo-directory-private.h>
#include <libnemo-private/nemo-file-utilities.h>
#include <libnemo-private/nemo-file-operations.h>
#include <libnemo-private/nemo-global-preferences.h>
#include <libnemo-private/nemo-lib-self-check-functions.h>
#include <libnemo-private/nemo-module.h>
#include <libnemo-private/nemo-signaller.h>
#include <libnemo-private/nemo-ui-utilities.h>
#include <libnemo-private/nemo-undo-manager.h>
#include <libnemo-private/nemo-thumbnails.h>
#include <libnemo-private/nemo-search-engine-advanced.h>
#include <libnemo-extension/nemo-menu-provider.h>

#define DEBUG_FLAG NEMO_DEBUG_APPLICATION
#include <libnemo-private/nemo-debug.h>

#include <sys/types.h>
#include <sys/stat.h>
#ifdef G_OS_UNIX
#include <pwd.h>
#endif
#include <fcntl.h>
#include <errno.h>
#ifdef GDK_WINDOWING_X11
#include <gdk/gdkx.h>
#endif
#include <glib/gstdio.h>
#include <glib/gi18n.h>
#include <gio/gio.h>
#include <eel/eel-gtk-extensions.h>
#include <eel/eel-vfs-extensions.h>
#include <eel/eel-stock-dialogs.h>


#include <libnemo-private/nemo-desktop-thumbnail.h>

/* Keep window from shrinking down ridiculously small; numbers are somewhat arbitrary */
#define APPLICATION_WINDOW_MIN_WIDTH	300
#define APPLICATION_WINDOW_MIN_HEIGHT	100

#define START_STATE_CONFIG "start-state"

#define NEMO_ACCEL_MAP_SAVE_DELAY 30

/* Disable the self-check functionality */
#define NEMO_OMIT_SELF_CHECK "omit"

#define NEMO_NOTIFICATION_UNMOUNT_ICON_NAME  "media-removable"
#define NEMO_NOTIFICATION_UNMOUNT_ID_PENDING "unmount-pending"
#define NEMO_NOTIFICATION_UNMOUNT_ID_DONE    "unmount-done"

static void     mount_removed_callback            (GVolumeMonitor            *monitor,
						   GMount                    *mount,
						   NemoMainApplication       *application);
static void     mount_added_callback              (GVolumeMonitor            *monitor,
						   GMount                    *mount,
						   NemoMainApplication       *application);

G_DEFINE_TYPE (NemoMainApplication, nemo_main_application, NEMO_TYPE_APPLICATION);

struct _NemoMainApplicationPriv {
	GVolumeMonitor *volume_monitor;

	NemoFreedesktopDBus *fdb_manager;

	/* Command line, kept for the open handler. */
	gchar *geometry;
	gboolean open_in_tabs;
	gboolean select;

	GDBusConnection *instance_connection;
	guint instance_name_id;
	guint instance_actions_id;
	guint instance_tabs_id;

	/* A tab moved here from another copy, carried on the command line. */
	gchar *tab_view;
	gchar **tab_select;
};

static void
publish_instance (NemoMainApplication *self)
{
	GDBusConnection *connection;

	connection = g_application_get_dbus_connection (G_APPLICATION (self));
	if (connection == NULL) {
		return;
	}
	self->priv->instance_connection = g_object_ref (connection);

	self->priv->instance_name_id = g_bus_own_name_on_connection (connection,
	                                                             NEMO_INSTANCE_BUS_NAME,
	                                                             G_BUS_NAME_OWNER_FLAGS_NONE,
	                                                             NULL, NULL, NULL, NULL);

	/* GApplication may already serve the same group at this path; then the
	 * export fails and nothing is missing. */
	self->priv->instance_actions_id = g_dbus_connection_export_action_group (connection,
	                                                                         NEMO_INSTANCE_OBJECT_PATH,
	                                                                         G_ACTION_GROUP (self),
	                                                                         NULL);

	self->priv->instance_tabs_id = nemo_tab_move_export (connection, NEMO_INSTANCE_OBJECT_PATH);
}

/* The activation file has to name a path, and a portable copy has no path until
 * it runs - the one that ships in a prefix names wherever it was built. So it is
 * written here instead, into the user's own service directory, and only when it
 * does not already describe this copy. */
static void
refresh_dbus_service_file (G_GNUC_UNUSED NemoMainApplication *self)
{
#ifndef G_OS_WIN32
	char *exe;
	char *dir;
	char *path;
	char *wanted;
	char *current = NULL;
	gboolean same;

	/* No connection means no session bus, so nothing would read it. */
	if (self->priv->instance_connection == NULL) {
		return;
	}

	exe = nemo_get_exe_path ();
	if (exe == NULL) {
		return;
	}

	wanted = g_strdup_printf ("[D-BUS Service]\nName=%s\nExec=%s --no-default-window\n",
	                          NEMO_INSTANCE_BUS_NAME, exe);
	g_free (exe);

	dir = g_build_filename (g_get_user_data_dir (), "dbus-1", "services", NULL);
	path = g_build_filename (dir, NEMO_INSTANCE_BUS_NAME ".service", NULL);

	same = g_file_get_contents (path, &current, NULL, NULL) &&
	       g_strcmp0 (current, wanted) == 0;
	g_free (current);

	if (!same && g_mkdir_with_parents (dir, 0755) == 0) {
		GError *error = NULL;

		if (!g_file_set_contents (path, wanted, -1, &error)) {
			g_warning ("Could not write the D-Bus activation file (%s): %s",
			           path, error->message);
			g_clear_error (&error);
		}
	}

	g_free (path);
	g_free (dir);
	g_free (wanted);
#endif
}

/* At finalize the application is no longer registered, so the connection is
 * the one kept from publishing, not asked for again. */
static void
unpublish_instance (NemoMainApplication *self)
{
	GDBusConnection *connection = self->priv->instance_connection;

	if (connection == NULL) {
		return;
	}

	if (self->priv->instance_name_id != 0) {
		g_bus_unown_name (self->priv->instance_name_id);
		self->priv->instance_name_id = 0;
	}
	if (self->priv->instance_actions_id != 0) {
		g_dbus_connection_unexport_action_group (connection, self->priv->instance_actions_id);
		self->priv->instance_actions_id = 0;
	}
	if (self->priv->instance_tabs_id != 0) {
		g_dbus_connection_unregister_object (connection, self->priv->instance_tabs_id);
		self->priv->instance_tabs_id = 0;
	}
	g_clear_object (&self->priv->instance_connection);
}

/* Unique bus names of every other running copy; NULL with no bus. */
static GStrv
other_instances (GApplication *application)
{
	GDBusConnection *connection;
	GVariant *reply;
	GStrv names = NULL;
	GPtrArray *others;
	const char *me;
	int i;

	connection = g_application_get_dbus_connection (application);
	if (connection == NULL) {
		return NULL;
	}

	reply = g_dbus_connection_call_sync (connection,
	                                     "org.freedesktop.DBus", "/org/freedesktop/DBus",
	                                     "org.freedesktop.DBus", "ListQueuedOwners",
	                                     g_variant_new ("(s)", NEMO_INSTANCE_BUS_NAME),
	                                     G_VARIANT_TYPE ("(as)"),
	                                     G_DBUS_CALL_FLAGS_NONE, -1, NULL, NULL);
	if (reply == NULL) {
		/* No owner at all is an error reply, and means nobody is running. */
		return g_new0 (char *, 1);
	}

	g_variant_get (reply, "(^as)", &names);
	g_variant_unref (reply);

	me = g_dbus_connection_get_unique_name (connection);
	others = g_ptr_array_new ();
	for (i = 0; names[i] != NULL; i++) {
		if (g_strcmp0 (names[i], me) != 0) {
			g_ptr_array_add (others, names[i]);
		} else {
			g_free (names[i]);
		}
	}
	g_free (names);
	g_ptr_array_add (others, NULL);

	return (GStrv) g_ptr_array_free (others, FALSE);
}

/* Returns: (transfer full): free with g_strfreev */
GStrv
nemo_main_application_other_instances (void)
{
	return other_instances (g_application_get_default ());
}

/* Asks every other copy to quit. FALSE only when there is no bus to ask on. */
static gboolean
quit_other_instances (GApplication *application)
{
	GDBusConnection *connection;
	GStrv others;
	int i;

	others = other_instances (application);
	if (others == NULL) {
		return FALSE;
	}

	connection = g_application_get_dbus_connection (application);
	for (i = 0; others[i] != NULL; i++) {
		GDBusActionGroup *group;

		group = g_dbus_action_group_get (connection, others[i], NEMO_INSTANCE_OBJECT_PATH);
		g_action_group_activate_action (G_ACTION_GROUP (group), "quit", NULL);
		g_object_unref (group);
	}
	/* The requests are only queued; a copy about to exit has to see them out. */
	g_dbus_connection_flush_sync (connection, NULL, NULL);

	g_strfreev (others);

	return TRUE;
}

static void
nemo_main_application_send_notification (NemoApplication *application,
                                         const gchar *title,
                                         const gchar *body,
                                         const gchar *icon_name,
                                         const gchar *notification_id,
                                         const GNotificationPriority prio)
{
	NemoMainApplication *app = NEMO_MAIN_APPLICATION (application);
	GNotification *notification;
	GIcon *icon;

	icon = g_themed_icon_new (icon_name);
	notification = g_notification_new (title);
	g_notification_set_body (notification, body);
	g_notification_set_icon (notification, icon);
	g_notification_set_priority (notification, prio);

	g_application_send_notification (G_APPLICATION (app), notification_id, notification);

	g_object_unref (notification);
	g_object_unref (icon);
}

static void
nemo_main_application_notify_unmount_done (NemoApplication *application,
                                           const gchar     *message)
{
	NemoMainApplication *app = NEMO_MAIN_APPLICATION (application);
	gchar **strings;

	// remove notification for pending unmount state
	g_application_withdraw_notification (G_APPLICATION (app), NEMO_NOTIFICATION_UNMOUNT_ID_PENDING);

	g_return_if_fail (message != NULL);
	strings = g_strsplit (message, "\n", 2);

	nemo_main_application_send_notification (application, strings[0], strings[1],
	                                         NEMO_NOTIFICATION_UNMOUNT_ICON_NAME,
	                                         NEMO_NOTIFICATION_UNMOUNT_ID_DONE,
	                                         G_NOTIFICATION_PRIORITY_NORMAL);
	
	g_strfreev (strings);
}

static void
nemo_main_application_notify_unmount_show (NemoApplication *application,
                                           const gchar     *message)
{
	gchar **strings;

	g_return_if_fail (message != NULL);
	strings = g_strsplit (message, "\n", 2);

	nemo_main_application_send_notification (application, strings[0], strings[1],
	                                         NEMO_NOTIFICATION_UNMOUNT_ICON_NAME,
	                                         NEMO_NOTIFICATION_UNMOUNT_ID_PENDING,
	                                         G_NOTIFICATION_PRIORITY_URGENT);
	g_strfreev (strings);
}

static void
nemo_main_application_close_all_windows (NemoApplication *self)
{
	GList *list_copy;
	GList *l;

	quit_other_instances (G_APPLICATION (self));

	list_copy = g_list_copy (gtk_application_get_windows (GTK_APPLICATION (self)));
	for (l = list_copy; l != NULL; l = l->next) {
		if (NEMO_IS_WINDOW (l->data)) {
			NemoWindow *window;

			window = NEMO_WINDOW (l->data);
			nemo_window_close (window);
		}
	}
	g_list_free (list_copy);
}

/* The splash stands in until the window is not merely up but finished - the
 * view in place and the folder listed to the end. */

/* Whichever comes first: the folder listed to the end, or a second after the
 * view is on screen. A big folder takes far longer to list than it does to be
 * worth looking at - C:\Windows\System32 measured 21 seconds, all of it spent
 * covering a window that was already usable. */
#define SPLASH_VIEW_GRACE_US	(1 * G_USEC_PER_SEC)
#define SPLASH_POLL_MS		100

/* And nothing may keep it up indefinitely, for the case where the view never
 * arrives at all because the location never answers. */
#define SPLASH_MAX_SECONDS	25

/* Weak, so a window closed while still loading cannot leave the watch holding
 * a freed pointer. */
static NemoWindow *splash_watch_window;

/* Monotonic microseconds at the first poll that had the view on screen. */
static gint64      splash_view_seen;

static gboolean
drop_splash_idle (G_GNUC_UNUSED gpointer data)
{
	nemo_splash_hide ();
	return G_SOURCE_REMOVE;
}

static void
splash_watch_stop (void)
{
	if (splash_watch_window != NULL) {
		g_object_remove_weak_pointer (G_OBJECT (splash_watch_window),
					      (gpointer *) &splash_watch_window);
		splash_watch_window = NULL;
	}
	splash_view_seen = 0;
}

/* Polled rather than hung off the window's draw signal: nothing guarantees a
 * frame arrives at the moment loading flips false, since the last rows may
 * already have been painted, and a splash that outlives the load by a frame
 * that never comes is worse than one checked ten times a second. */
static gboolean
splash_watch_cb (G_GNUC_UNUSED gpointer data)
{
	NemoWindowSlot *slot;
	NemoView       *view;

	if (!nemo_splash_is_up () || splash_watch_window == NULL) {
		splash_watch_stop ();
		nemo_splash_hide ();
		return G_SOURCE_REMOVE;
	}

	/* The frame goes up before the folder resolves, so an empty window is
	 * not the thing to hand over to. The pane holds the view and stays
	 * hidden until it is ready; the view keeps its own loading flag set
	 * until every file in the folder has been seen. */
	slot = nemo_window_get_active_slot (splash_watch_window);
	if (slot == NULL || slot->pane == NULL ||
	    !gtk_widget_get_visible (GTK_WIDGET (slot->pane))) {
		return G_SOURCE_CONTINUE;
	}

	view = nemo_window_slot_get_current_view (slot);
	if (view == NULL) {
		return G_SOURCE_CONTINUE;
	}

	/* The first poll with the view on screen starts its last second. */
	if (splash_view_seen == 0) {
		splash_view_seen = g_get_monotonic_time ();
	}

	if (nemo_view_get_loading (view) &&
	    g_get_monotonic_time () - splash_view_seen < SPLASH_VIEW_GRACE_US) {
		return G_SOURCE_CONTINUE;
	}

	splash_watch_stop ();

	/* One hop, so the frame holding the finished listing is painted before
	 * the splash over it goes away. */
	g_idle_add (drop_splash_idle, NULL);

	return G_SOURCE_REMOVE;
}

static gboolean
splash_deadline_cb (G_GNUC_UNUSED gpointer data)
{
	splash_watch_stop ();
	nemo_splash_hide ();

	return G_SOURCE_REMOVE;
}

static NemoWindow *
nemo_main_application_create_window (NemoApplication *application,
                                     GdkScreen       *screen)
{
	NemoWindow *window;
	char *geometry_string;
	gboolean maximized;

	g_return_val_if_fail (NEMO_IS_APPLICATION (application), NULL);

    window = nemo_window_new (GTK_APPLICATION (application), screen);

	maximized = nemo_config_get_boolean
		(nemo_window_state, NEMO_WINDOW_STATE_MAXIMIZED);
	if (maximized) {
		gtk_window_maximize (GTK_WINDOW (window));
	} else {
		gtk_window_unmaximize (GTK_WINDOW (window));
	}

    geometry_string = nemo_config_get_string (nemo_window_state, NEMO_WINDOW_STATE_GEOMETRY);

    if (NEMO_MAIN_APPLICATION (application)->priv->geometry == NULL && 
        geometry_string != NULL &&
        geometry_string[0] != 0) {
		/* Ignore saved window position if a window with the same
		 * location is already showing. That way the two windows
		 * wont appear at the exact same location on the screen.
		 */
		eel_gtk_window_set_initial_geometry_from_string 
			(GTK_WINDOW (window), 
			 geometry_string,
			 NEMO_WINDOW_MIN_WIDTH,
			 NEMO_WINDOW_MIN_HEIGHT,
			 TRUE);
	}

	g_free (geometry_string);

    nemo_undo_manager_attach (application->undo_manager, G_OBJECT (window));

	/* First window only - the splash belongs to the launch, not to every
	 * window the launch happens to open. */
	if (nemo_splash_is_up () && splash_watch_window == NULL) {
		splash_watch_window = window;
		g_object_add_weak_pointer (G_OBJECT (window),
					   (gpointer *) &splash_watch_window);
		g_timeout_add (SPLASH_POLL_MS, splash_watch_cb, NULL);
		g_timeout_add_seconds (SPLASH_MAX_SECONDS, splash_deadline_cb, NULL);
	}

	DEBUG ("Creating a new navigation window");

	return window;
}

static void
mount_added_callback (G_GNUC_UNUSED GVolumeMonitor *monitor,
		      GMount *mount,
		      G_GNUC_UNUSED NemoMainApplication *application)
{
	NemoDirectory *directory;
	GFile *root;
	gchar *uri;
		
	root = g_mount_get_root (mount);
	uri = g_file_get_uri (root);

	DEBUG ("Added mount at uri %s", uri);
	g_free (uri);
	
	directory = nemo_directory_get_existing (root);
	g_object_unref (root);
	if (directory != NULL) {
		nemo_directory_force_reload (directory);
		nemo_directory_unref (directory);
	}
}

/* Called whenever a mount is unmounted. Check and see if there are
 * any windows open displaying contents on the mount. If there are,
 * close them.  It would also be cool to save open window and position
 * info.
 */
static void
mount_removed_callback (G_GNUC_UNUSED GVolumeMonitor *monitor,
			GMount *mount,
			NemoMainApplication *application)
{
	GList *window_list, *node, *close_list;
	NemoWindow *window;
	NemoWindowSlot *slot;
	NemoWindowSlot *force_no_close_slot;
	GFile *root, *computer;
	gchar *uri;

	close_list = NULL;
	force_no_close_slot = NULL;

	/* Check and see if any of the open windows are displaying contents from the unmounted mount */
	window_list = gtk_application_get_windows (GTK_APPLICATION (application));

	root = g_mount_get_root (mount);
	uri = g_file_get_uri (root);
    g_object_unref (root);

	DEBUG ("Removed mount at uri %s", uri);
	g_free (uri);

	/* Construct a list of windows to be closed. Do not add the non-closable windows to the list. */
	for (node = window_list; node != NULL; node = node->next) {
		window = NEMO_WINDOW (node->data);
		if (window != NULL) {
			GList *l;
			GList *lp;

			for (lp = window->details->panes; lp != NULL; lp = lp->next) {
				NemoWindowPane *pane;
				pane = (NemoWindowPane*) lp->data;
				for (l = pane->slots; l != NULL; l = l->next) {
					slot = l->data;
					if (nemo_window_slot_should_close_with_mount (slot, mount)) {
						close_list = g_list_prepend (close_list, slot);
					}
				} /* for all slots */
			} /* for all panes */
		}
	}

	/* Handle the windows in the close list. */
	for (node = close_list; node != NULL; node = node->next) {
		slot = node->data;

		if (slot != force_no_close_slot) {
            if (nemo_config_get_boolean (nemo_preferences, NEMO_PREFERENCES_CLOSE_DEVICE_VIEW_ON_EJECT))
                nemo_window_pane_close_slot (slot->pane, slot);
            else
                nemo_window_slot_go_home (slot, FALSE);
		} else {
			computer = g_file_new_for_path (g_get_home_dir ());
			nemo_window_slot_open_location (slot, computer, 0);
			g_object_unref(computer);
		}
	}

	g_list_free (close_list);
}

/* Put the frame on screen at the size and place it will keep, without waiting
 * for the first folder to resolve - the panes stay hidden until their views are
 * ready (nemo_window_view_visible), so what appears is the window and its
 * chrome. That is the difference between a launch that looks stalled and one
 * that looks busy.
 *
 * Called once the window has somewhere to be, and not before: realising the
 * chrome makes it ask its slot where it is, and a slot with neither a location
 * nor a pending one has no answer. It also has to come after any --geometry has
 * been applied, which is only honored while the window is still hidden.
 */
static void
show_window_early (NemoWindow *window)
{
	NemoWindowSlot *slot = nemo_window_get_active_slot (window);

	if (slot == NULL ||
	    (slot->location == NULL && slot->pending_location == NULL)) {
		return;
	}

	nemo_splash_note (_("Opening the window"));
	gtk_widget_show (GTK_WIDGET (window));

	/* Showing a window maps it but does not activate it, so it sits one
	 * place behind whatever the user was looking at - which, with a splash
	 * covering the moment it appears, reads as nothing having opened at all
	 * until the taskbar button is noticed. Present it, as launching an app
	 * is supposed to. */
	gtk_window_present (GTK_WINDOW (window));
}

static void
open_window (NemoMainApplication *application,
	     GFile *location, GFile *selection, GdkScreen *screen, const char *geometry)
{
	NemoWindow *window;
	gchar *uri;
	gboolean have_geometry;
	GList *sel_list = NULL;

	uri = g_file_get_uri (location);
	DEBUG ("Opening new window at uri %s", uri);

	window = nemo_main_application_create_window (NEMO_APPLICATION (application),
						     screen);
	if (selection != NULL) {
		sel_list = g_list_prepend (NULL, nemo_file_get (selection));
	}
	nemo_window_slot_open_location_full (nemo_window_get_active_slot (window),
					     location, 0, sel_list, NULL, NULL);
	nemo_file_list_free (sel_list);

	have_geometry = geometry != NULL && strcmp(geometry, "") != 0;

	if (have_geometry && !gtk_widget_get_visible (GTK_WIDGET (window))) {
		/* never maximize windows opened from shell if a
		 * custom geometry has been requested.
		 */
		gtk_window_unmaximize (GTK_WINDOW (window));
		eel_gtk_window_set_initial_geometry_from_string (GTK_WINDOW (window),
								 geometry,
								 APPLICATION_WINDOW_MIN_WIDTH,
								 APPLICATION_WINDOW_MIN_HEIGHT,
								 FALSE);
	}

	show_window_early (window);

	g_free (uri);
}

static void
open_tabs (NemoMainApplication *application,
         GFile **locations,
         guint n_files,
         GdkScreen *screen,
         const char *geometry)
{
    NemoWindow *window;
    gchar *uri;
    gboolean have_geometry;

    window = nemo_main_application_create_window (NEMO_APPLICATION (application),
                             screen);

    /* open all locations */
    uri = g_file_get_uri (locations[0]);
    g_debug ("Opening new tab at uri %s\n", uri);
    nemo_window_go_to (window, locations[0]);
    g_free (uri);
    for (guint i = 1; i < n_files; i++) {
        /* open tabs in reverse order because each
         * tab is opened before the previous one */
        guint tab = n_files-i;
        uri = g_file_get_uri (locations[tab]);
        g_debug ("Opening new tab at uri %s\n", uri);
        nemo_window_go_to_tab (window, locations[tab]);
        g_free (uri);
    }

    have_geometry = geometry != NULL && strcmp(geometry, "") != 0;

    if (have_geometry && !gtk_widget_get_visible (GTK_WIDGET (window))) {
        /* never maximize windows opened from shell if a
         * custom geometry has been requested.
         */
        gtk_window_unmaximize (GTK_WINDOW (window));
        eel_gtk_window_set_initial_geometry_from_string (GTK_WINDOW (window),
                                 geometry,
                                 APPLICATION_WINDOW_MIN_WIDTH,
                                 APPLICATION_WINDOW_MIN_HEIGHT,
                                 FALSE);
    }

    show_window_early (window);
}

static void
open_windows (NemoMainApplication *application,
	      GFile **files,
	      gint n_files,
	      GdkScreen *screen,
	      const char *geometry,
	      gboolean open_in_tabs,
	      gboolean select)
{
	gint i;

	if (files == NULL || files[0] == NULL) {
		/* Open a window pointing at the default location. */
		open_window (application, NULL, NULL, screen, geometry);
	} else if (select) {
		/* One window per item, each showing the folder around it. */
		for (i = 0; i < n_files; i++) {
			GFile *parent = g_file_get_parent (files[i]);

			if (parent != NULL) {
				open_window (application, parent, files[i], screen, geometry);
				g_object_unref (parent);
			} else {
				open_window (application, files[i], NULL, screen, geometry);
			}
		}
	} else if (open_in_tabs) {
		/* Open one window with one tab at each requested location */
		open_tabs (application, files, n_files, screen, geometry);
	} else {
		/* Open windows at each requested location. */
		for (i = 0; i < n_files; i++) {
			open_window (application, files[i], NULL, screen, geometry);
		}
	}
}

static void
open_moved_tab (NemoMainApplication *self,
                GFile               *location,
                GdkScreen           *screen)
{
	NemoTabState state = { 0 };
	NemoWindow *window;

	state.uri = g_file_get_uri (location);
	state.view_id = self->priv->tab_view;
	state.selected = self->priv->tab_select;

	window = nemo_main_application_create_window (NEMO_APPLICATION (self), screen);
	nemo_window_take_tab (window, &state, 0);
	show_window_early (window);

	g_free (state.uri);
}

static void
nemo_main_application_open_location (NemoApplication     *application,
                                     GFile               *location,
                                     GFile               *selection,
                                     const char          *startup_id)
{
	NemoWindow *window;

	window = nemo_application_open_in_new_window (application, gdk_screen_get_default (),
	                                              location, selection);
	if (window != NULL) {
		gtk_window_set_startup_id (GTK_WINDOW (window), startup_id);
		show_window_early (window);
	}
}

static void
nemo_main_application_open (GApplication *app,
                            GFile       **files,
                            gint          n_files,
                            G_GNUC_UNUSED const gchar  *hint)
{
	NemoMainApplication *self = NEMO_MAIN_APPLICATION (app);

	DEBUG ("Open called on the GApplication instance; %d files, open in tabs: %s, select: %s, geometry: '%s'",
	       n_files,
	       self->priv->open_in_tabs ? "yes" : "no",
	       self->priv->select ? "yes" : "no",
	       self->priv->geometry ? self->priv->geometry : "none");

	if (n_files == 1 && !self->priv->select && !self->priv->open_in_tabs &&
	    (self->priv->tab_view != NULL || self->priv->tab_select != NULL)) {
		open_moved_tab (self, files[0], gdk_screen_get_default ());
		return;
	}

	open_windows (self, files, n_files, gdk_screen_get_default (),
	              self->priv->geometry, self->priv->open_in_tabs, self->priv->select);
}

static void
nemo_main_application_init (NemoMainApplication *application)
{
    application->priv = G_TYPE_INSTANCE_GET_PRIVATE (application,
                                                     NEMO_TYPE_MAIN_APPLICATION,
                                                     NemoMainApplicationPriv);
}

static void
nemo_main_application_finalize (GObject *object)
{
    NemoMainApplication *application;

    application = NEMO_MAIN_APPLICATION (object);

    nemo_bookmarks_exiting ();

    unpublish_instance (application);

    g_clear_object (&application->priv->volume_monitor);
    g_free (application->priv->geometry);
    g_free (application->priv->tab_view);
    g_strfreev (application->priv->tab_select);

    g_clear_object (&application->priv->fdb_manager);

    free_search_helpers ();

    G_OBJECT_CLASS (nemo_main_application_parent_class)->finalize (object);
}

static gboolean
do_cmdline_sanity_checks (NemoMainApplication *self,
			  gboolean perform_self_check,
			  G_GNUC_UNUSED gboolean version,
			  gboolean kill_shell,
			  gboolean open_in_tabs,
			  gchar **remaining)
{
	gboolean retval = FALSE;

	if (perform_self_check && (remaining != NULL || kill_shell)) {
		g_printerr ("%s\n",
			    _("--check cannot be used with other options."));
		goto out;
	}

	if (kill_shell && remaining != NULL) {
		g_printerr ("%s\n",
			    _("--quit cannot be used with URIs."));
		goto out;
	}

	if (self->priv->geometry != NULL &&
	    !open_in_tabs &&
	    remaining != NULL && remaining[0] != NULL && remaining[1] != NULL) {
		g_printerr ("%s\n",
			    _("--geometry cannot be used with more than one URI."));
		goto out;
	}

	retval = TRUE;

 out:
	return retval;
}

static void
do_perform_self_checks (gint *exit_status)
{
#ifndef NEMO_OMIT_SELF_CHECK
	/* Run the checks (each twice) for nemo and libnemo-private. */

	nemo_run_self_checks ();
	nemo_run_lib_self_checks ();
	eel_exit_if_self_checks_failed ();

	nemo_run_self_checks ();
	nemo_run_lib_self_checks ();
	eel_exit_if_self_checks_failed ();
#endif

	*exit_status = EXIT_SUCCESS;
}

static gboolean
nemo_main_application_local_command_line (GApplication *application,
					 gchar ***arguments,
					 gint *exit_status)
{
	gboolean perform_self_check = FALSE;
	gboolean version = FALSE;
	gboolean about = FALSE;
	gboolean browser = FALSE;
	gboolean open_in_existing_window = FALSE;
	gboolean kill_shell = FALSE;
	gboolean no_default_window = FALSE;
    gboolean no_desktop_ignored = FALSE;
	gboolean fix_cache = FALSE;
	gboolean reset_config = FALSE;
    gboolean debug = FALSE;
	gchar **remaining = NULL;
	NemoMainApplication *self = NEMO_MAIN_APPLICATION (application);

	const GOptionEntry options[] = {
#ifndef NEMO_OMIT_SELF_CHECK
		{ "check", 'c', 0, G_OPTION_ARG_NONE, &perform_self_check, 
		  N_("Perform a quick set of self-check tests."), NULL },
#endif
		/* dummy, only for compatibility reasons */
		{ "browser", '\0', G_OPTION_FLAG_HIDDEN, G_OPTION_ARG_NONE, &browser,
		  NULL, NULL },
		{ "version", '\0', 0, G_OPTION_ARG_NONE, &version,
		  N_("Show the version of the program."), NULL },
		{ "about", '\0', 0, G_OPTION_ARG_NONE, &about,
		  N_("Show the version, copyright and license."), NULL },
		{ "geometry", 'g', 0, G_OPTION_ARG_STRING, &self->priv->geometry,
		  N_("Create the initial window with the given geometry. "
             "Examples: nemo --geometry=+100+100, nemo --geometry=600x400, nemo --geometry=600x400+100+100."), N_("GEOMETRY") },
		{ "no-default-window", 'n', 0, G_OPTION_ARG_NONE, &no_default_window,
		  N_("Only create windows for explicitly specified URIs."), NULL },
        { "no-desktop", '\0', 0, G_OPTION_ARG_NONE, &no_desktop_ignored,
          N_("Ignored argument - left for compatibility only."), NULL },
		{ "tabs", 't', 0, G_OPTION_ARG_NONE, &self->priv->open_in_tabs,
		  N_("Open URIs in tabs."), NULL },
		{ "select", 's', 0, G_OPTION_ARG_NONE, &self->priv->select,
		  N_("Open the folder holding each URI, with the item selected."), NULL },
		/* How a tab moved out to a window of its own keeps its view and
		 * selection. Only for one URI. */
		{ "tab-view", 0, G_OPTION_FLAG_HIDDEN, G_OPTION_ARG_STRING, &self->priv->tab_view,
		  NULL, NULL },
		{ "tab-select", 0, G_OPTION_FLAG_HIDDEN, G_OPTION_ARG_STRING_ARRAY, &self->priv->tab_select,
		  NULL, NULL },
		/* Every launch is its own process now, so there is no window of ours
		 * to join; the nearest thing is one window with a tab per URI. */
		{ "existing-window", 0, G_OPTION_FLAG_HIDDEN, G_OPTION_ARG_NONE, &open_in_existing_window,
		  NULL, NULL },
		{ "fix-cache", '\0', 0, G_OPTION_ARG_NONE, &fix_cache,
		  N_("Repair the user thumbnail cache - this can be useful if you're having trouble with file thumbnails.  Must be run as root"), NULL },
		{ "reset", '\0', 0, G_OPTION_ARG_NONE, &reset_config,
		  N_("Clear the settings and bookmarks, putting everything back to defaults. Nemo must not be running."), NULL },
        { "debug", 0, 0, G_OPTION_ARG_NONE, &debug,
          "Enable debugging code.  Example usage: 'NEMO_DEBUG=Actions,Window nemo --debug'.  Use NEMO_DEBUG=help for more topics.", NULL },
		{ "quit", 'q', 0, G_OPTION_ARG_NONE, &kill_shell, 
		  N_("Quit Nemo."), NULL },
		{ G_OPTION_REMAINING, 0, 0, G_OPTION_ARG_STRING_ARRAY, &remaining, NULL,  N_("[URI...]") },

		{ 0 }
	};
	GOptionContext *context;
	GError *error = NULL;
	gint argc = 0;
	gchar **argv = NULL;
	gboolean open_display = TRUE;
	gint i;

	*exit_status = EXIT_SUCCESS;

	argv = *arguments;
	argc = g_strv_length (argv);

	/* The GTK group opens the display as part of the parse, so with no screen
	   the parse itself failed and --version never got looked at. A --display
	   given alongside is still read, and used when a window is opened. */
	for (i = 1; i < argc; i++) {
		if (g_strcmp0 (argv[i], "--version") == 0 || g_strcmp0 (argv[i], "--about") == 0) {
			open_display = FALSE;
		}
	}

	context = g_option_context_new (_("\n\nBrowse the file system with the file manager"));
	g_option_context_add_main_entries (context, options, NULL);
	g_option_context_add_group (context, gtk_get_option_group (open_display));

	/* The strv form frees what it takes out. The plain one leaves every parsed
	   option behind, since g_application_run frees only what is left. */
	if (!g_option_context_parse_strv (context, arguments, &error)) {
		g_printerr ("Could not parse arguments: %s\n", error->message);
		g_error_free (error);

		*exit_status = EXIT_FAILURE;
		goto out;
	}

#define NEMO_COPYRIGHT_LINE "Copyright \xc2\xa9 2026 t00mietum (CryptogID: ปʬϝღถɔ4რఠΔթะ9ƾǝu). Upstream copyrights held by the Nemo authors.\n"

	if (version) {
		g_print ("nemo-anywhere " NEMO_VERSION_STRING "\n"
			 NEMO_COPYRIGHT_LINE);
		goto out;
	}

	if (about) {
		g_print ("\nnemo-anywhere " NEMO_VERSION_STRING "\n"
			 NEMO_COPYRIGHT_LINE
			 "Project: https://github.com/yottacore/nemo-anywhere\n"
			 "Licensed under the GNU General Public License, version 2 only. Full text at:\n"
			 "  https://spdx.org/licenses/GPL-2.0-only.html\n"
			 "No warranty.\n"
			 "\n"
			 "A file manager that runs on its own, with nothing behind it to install.\n\n");
		goto out;
	}

    if (debug) {
#if (GLIB_CHECK_VERSION(2,80,0))
        const gchar* const domains[] = { "Nemo Anywhere", NULL };
        g_log_writer_default_set_debug_domains (domains);
#else
        g_setenv ("G_MESSAGES_DEBUG", "all", TRUE);
#endif
    }

	if (open_in_existing_window) {
		self->priv->open_in_tabs = TRUE;
	}

	if (!do_cmdline_sanity_checks (self, perform_self_check,
				       version, kill_shell, self->priv->open_in_tabs, remaining)) {
		*exit_status = EXIT_FAILURE;
		goto out;
	}

	if (perform_self_check) {
		do_perform_self_checks (exit_status);
		goto out;
	}

    if (fix_cache) {
        if (nemo_user_is_root () && nemo_treating_root_as_normal ()) {
            g_printerr ("Ignoring --fix-cache as root-is-normal setting is true and this "
                        "check is probably unnecessary.\n");
        } else
        if (!nemo_user_is_root ()) {
            g_printerr ("The --fix-cache option must be run with sudo or as the root user.\n");
        } else
        {
            nemo_desktop_thumbnail_cache_fix_permissions ();
            g_print ("User thumbnail cache successfully repaired.\n");
        }

        goto out;
    }

	DEBUG ("Parsing local command line, no_default_window %d, quit %d, "
	       "self checks %d",
	       no_default_window, kill_shell, perform_self_check);

	/* Non-unique: this is always the primary instance, whatever else is
	 * running. Registering still gets the bus connection the copies find
	 * each other on. */
	if (!g_application_register (application, NULL, &error)) {
		g_printerr ("Could not register nemo: %s\n", error->message);
		g_clear_error (&error);

		*exit_status = EXIT_FAILURE;
		goto out;
	}

	/* After registration, so a running copy is caught: it holds the settings in
	 * memory and would write them straight back over anything cleared here. */
	if (reset_config) {
		GStrv running = other_instances (application);
		gboolean busy = running != NULL && running[0] != NULL;

		g_strfreev (running);
		if (busy) {
			g_printerr ("Nemo is already running - quit it first, then --reset.\n");
			*exit_status = EXIT_FAILURE;
		} else {
			char *settings, *shortcuts;

			/* Clear the live store first so the flush leaves nothing queued,
			 * then take the file itself - anything hand-written that nemo does
			 * not recognize is part of "back to defaults" too. */
			nemo_config_reset_all ();
			nemo_config_flush ();

			settings = nemo_config_get_path ();
			g_unlink (settings);
			g_free (settings);

			/* Emptied rather than removed, since a missing file is what
			 * sends the next start to upstream Nemo's shortcuts. */
			shortcuts = nemo_get_accel_map_file ();
			g_file_set_contents (shortcuts, "", 0, NULL);
			g_free (shortcuts);

			nemo_bookmark_list_reset_files ();
			g_print ("Settings and bookmarks cleared.\n");
		}
		goto out;
	}

	if (kill_shell) {
		DEBUG ("Asking every running copy to quit");
		if (!quit_other_instances (application)) {
			g_printerr ("No session bus, so running copies cannot be reached.\n");
			*exit_status = EXIT_FAILURE;
		}
		goto out;
	}

	GFile **files;
	gint idx, len;

	len = 0;
	files = NULL;

	/* Convert args to GFiles */
	if (remaining != NULL) {
		GFile *file;
		GPtrArray *file_array;

		file_array = g_ptr_array_new ();

		for (idx = 0; remaining[idx] != NULL; idx++) {
			char *expanded = eel_expand_user_input (remaining[idx]);

			file = g_file_new_for_commandline_arg (remaining[idx]);

			/* Same rule as the location bar: the literal wins, and a shell
			 * that already expanded the argument leaves nothing to do here. */
			if (expanded != NULL && !g_file_query_exists (file, NULL)) {
				g_object_unref (file);
				file = g_file_new_for_commandline_arg (expanded);
			}
			g_free (expanded);

			if (file != NULL) {
				g_ptr_array_add (file_array, file);
			}
		}

		len = file_array->len;
		files = (GFile **) g_ptr_array_free (file_array, FALSE);
		g_strfreev (remaining);
	}

	if (files == NULL && !no_default_window) {
		files = g_malloc0 (2 * sizeof (GFile *));
		len = 1;

		files[0] = g_file_new_for_path (g_get_home_dir ());
		files[1] = NULL;
	}
	if (len > 0) {
		g_application_open (application, files, len, "");
	}

	for (idx = 0; idx < len; idx++) {
		g_object_unref (files[idx]);
	}
	g_free (files);

 out:
	g_option_context_free (context);

	return TRUE;	
}

static void
menu_state_changed_callback (G_GNUC_UNUSED NemoMainApplication *self)
{
    if (!nemo_config_get_boolean (nemo_window_state, NEMO_WINDOW_STATE_START_WITH_MENU_BAR) &&
        !nemo_config_get_boolean (nemo_preferences, NEMO_PREFERENCES_DISABLE_MENU_WARNING)) {

        GtkWidget *dialog;
        GtkWidget *msg_area;
        GtkWidget *checkbox;

        dialog = gtk_message_dialog_new (NULL,
                                         GTK_DIALOG_MODAL,
                                         GTK_MESSAGE_INFO,
                                         GTK_BUTTONS_OK,
                                         _("Nemo's main menu is now hidden"));

        gchar *secondary;
        secondary = g_strdup_printf (_("You have chosen to hide the main menu.  You can get it back temporarily by:\n\n"
                                     "- Tapping the <Alt> key\n"
                                     "- Right-clicking an empty region of the main toolbar\n"
                                     "- Right-clicking an empty region of the status bar.\n\n"
                                     "You can restore it permanently by selecting this option again from the View menu."));
        g_object_set (dialog,
                      "secondary-text", secondary,
                      NULL);
        g_free (secondary);

        msg_area = gtk_message_dialog_get_message_area (GTK_MESSAGE_DIALOG (dialog));
        checkbox = gtk_check_button_new_with_label (_("Don't show this message again."));
        gtk_box_pack_start (GTK_BOX (msg_area), checkbox, TRUE, TRUE, 2);

        nemo_config_bind (nemo_preferences,
                         NEMO_PREFERENCES_DISABLE_MENU_WARNING,
                         checkbox,
                         "active",
                         NEMO_CONFIG_BIND_DEFAULT);

        gtk_widget_show_all (dialog);

        g_signal_connect (dialog, "response",
                          G_CALLBACK (gtk_widget_destroy), NULL);
    }

}

static void
nemo_main_application_continue_startup (NemoApplication *app)
{
	NemoMainApplication *self = NEMO_MAIN_APPLICATION (app);

	self->priv->fdb_manager = nemo_freedesktop_dbus_new ();
	publish_instance (self);
	refresh_dbus_service_file (self);

    /* Check the user's ~/.config/nemo directory and post warnings
     * if there are problems.
     */

    nemo_application_check_required_directory (app, nemo_get_user_directory ());

	/* register views */
	nemo_icon_view_register ();
	nemo_list_view_register ();
	nemo_icon_view_compact_register ();
#if defined(ENABLE_EMPTY_VIEW) && ENABLE_EMPTY_VIEW
	nemo_empty_view_register ();
#endif

	/* Watch for unmounts so we can close open windows */
	/* TODO-gio: This should be using the UNMOUNTED feature of GFileMonitor instead */
	self->priv->volume_monitor = g_volume_monitor_get ();
	g_signal_connect_object (self->priv->volume_monitor, "mount_removed",
				 G_CALLBACK (mount_removed_callback), self, 0);
	g_signal_connect_object (self->priv->volume_monitor, "mount_added",
				 G_CALLBACK (mount_added_callback), self, 0);

    g_signal_connect_object (nemo_window_state, "changed::" NEMO_WINDOW_STATE_START_WITH_MENU_BAR,
                             G_CALLBACK (menu_state_changed_callback), self, G_CONNECT_SWAPPED);
}

static void
nemo_desktop_application_continue_quit (G_GNUC_UNUSED NemoApplication *app)
{
}

/* A notice outlives the program on the notification server, and an unmount
 * still running at quit would never take it down. */
static void
nemo_main_application_shutdown (GApplication *app)
{
    g_application_withdraw_notification (app, NEMO_NOTIFICATION_UNMOUNT_ID_PENDING);

    G_APPLICATION_CLASS (nemo_main_application_parent_class)->shutdown (app);
}

static void
nemo_main_application_class_init (NemoMainApplicationClass *class)
{
    GObjectClass *object_class;
    GApplicationClass *application_class;
    NemoApplicationClass *nemo_app_class;

    object_class = G_OBJECT_CLASS (class);
    object_class->finalize = nemo_main_application_finalize;

    application_class = G_APPLICATION_CLASS (class);
    application_class->open = nemo_main_application_open;
    application_class->local_command_line = nemo_main_application_local_command_line;
    application_class->shutdown = nemo_main_application_shutdown;

    nemo_app_class = NEMO_APPLICATION_CLASS (class);
    nemo_app_class->open_location = nemo_main_application_open_location;
    nemo_app_class->create_window = nemo_main_application_create_window;
    nemo_app_class->notify_unmount_show = nemo_main_application_notify_unmount_show;
    nemo_app_class->notify_unmount_done = nemo_main_application_notify_unmount_done;
    nemo_app_class->close_all_windows = nemo_main_application_close_all_windows;
    nemo_app_class->continue_startup = nemo_main_application_continue_startup;
    nemo_app_class->continue_quit = nemo_desktop_application_continue_quit;

    g_type_class_add_private (class, sizeof (NemoMainApplicationPriv));
}

/* Returns: (transfer full): unref with g_object_unref */
NemoApplication *
nemo_main_application_get_singleton (void)
{
    return nemo_application_initialize_singleton (NEMO_TYPE_MAIN_APPLICATION,
                                                  "application-id", "org.NemoAnywhere",
                                                  "flags", G_APPLICATION_HANDLES_OPEN | G_APPLICATION_NON_UNIQUE,
                                                  NULL);
}
