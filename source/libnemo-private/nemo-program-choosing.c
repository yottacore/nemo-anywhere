/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */

/* nemo-program-choosing.c - functions for selecting and activating
 				 programs for opening/viewing particular files.

   Copyright (C) 2000 Eazel, Inc.

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

   Author: John Sullivan <sullivan@eazel.com>
*/

#include <config.h>
#include "nemo-program-choosing.h"

#include "nemo-global-preferences.h"
#ifdef G_OS_WIN32
#include "nemo-associations-win32.h"
#include "nemo-launch-win32.h"
#include "nemo-user-text.h"
#endif
#include "nemo-icon-info.h"
#include "nemo-recent.h"
#include <eel/eel-gnome-extensions.h>
#include <eel/eel-stock-dialogs.h>
#include <gtk/gtk.h>
#include <glib/gi18n.h>
#include <gio/gio.h>
#ifdef G_OS_UNIX
#include <gio/gdesktopappinfo.h>
#endif
#include <stdlib.h>

#include <gdk/gdk.h>
#ifdef GDK_WINDOWING_X11
#include <gdk/gdkx.h>
#endif

void
nemo_launch_application_for_mount (GAppInfo *app_info,
				       GMount *mount,
				       GtkWindow *parent_window)
{
	GFile *root;
	NemoFile *file;
	GList *files;

	root = g_mount_get_root (mount);
	file = nemo_file_get (root);
	g_object_unref (root);

	files = g_list_append (NULL, file);
	nemo_launch_application (app_info,
				     files,
				     parent_window);

	g_list_free_full (files, (GDestroyNotify) nemo_file_unref);
}

/**
 * nemo_launch_application:
 * 
 * Fork off a process to launch an application with a given file as a
 * parameter. Provide a parent window for error dialogs. 
 * 
 * @application: The application to be launched.
 * @uris: The files whose locations should be passed as a parameter to the application.
 * @parent_window: A window to use as the parent for any error dialogs.
 */
void
nemo_launch_application (GAppInfo *application, 
			     GList *files,
			     GtkWindow *parent_window)
{
	GList *uris, *l;

	uris = NULL;
	for (l = files; l != NULL; l = l->next) {
		uris = g_list_prepend (uris, nemo_file_get_activation_uri (l->data));
	}
	uris = g_list_reverse (uris);
	nemo_launch_application_by_uri (application, uris,
					    parent_window);
	g_list_free_full (uris, g_free);
}

#ifdef G_OS_UNIX
static void
dummy_child_watch (G_GNUC_UNUSED GPid     pid,
                   G_GNUC_UNUSED gint     status,
                   G_GNUC_UNUSED gpointer user_data)
{
  /* Nothing, this is just to ensure we don't double fork
   * and break pkexec:
   * https://bugzilla.gnome.org/show_bug.cgi?id=675789
   */
}

static void
gather_pid_callback (G_GNUC_UNUSED GDesktopAppInfo *appinfo,
                     GPid            pid,
                     G_GNUC_UNUSED gpointer        data)
{
    g_child_watch_add(pid, dummy_child_watch, NULL);
}
#endif /* G_OS_UNIX */

void
nemo_launch_application_by_uri (GAppInfo *application, 
				    GList *uris,
				    GtkWindow *parent_window)
{
	char *uri;
	GList *locations, *l;
	GFile *location;
	NemoFile *file;
	gboolean result;
	GError *error;
	GdkDisplay *display;
	GdkAppLaunchContext *launch_context;
	NemoIconInfo *icon;

	g_assert (uris != NULL);

	locations = NULL;
	for (l = uris; l != NULL; l = l->next) {
		uri = l->data;
		
		location = g_file_new_for_uri (uri);
		locations = g_list_prepend (locations, location);
	}
	locations = g_list_reverse (locations);

	if (parent_window != NULL) {
		display = gtk_widget_get_display (GTK_WIDGET (parent_window));
	} else {
		display = gdk_display_get_default ();
	}

	launch_context = gdk_display_get_app_launch_context (display);

	if (parent_window != NULL) {
		gdk_app_launch_context_set_screen (launch_context,
						   gtk_window_get_screen (parent_window));
	}

	file = nemo_file_get_by_uri (uris->data);
	/* parent_window is optional here (the block above guards it), and casting
	   NULL to a widget for the scale factor warns and returns nothing useful. */
	icon = nemo_file_get_icon (file,
                               48, 0,
                               parent_window != NULL
                                   ? gtk_widget_get_scale_factor (GTK_WIDGET (parent_window))
                                   : 1,
                               0);
	nemo_file_unref (file);
	if (icon) {
		gdk_app_launch_context_set_icon_name (launch_context,
							nemo_icon_info_get_used_name (icon));
		nemo_icon_info_unref (icon);
	}
	
	error = NULL;

#ifdef G_OS_UNIX
    result = g_desktop_app_info_launch_uris_as_manager (G_DESKTOP_APP_INFO (application),
                                                        uris,
                                                        G_APP_LAUNCH_CONTEXT (launch_context),
                                                        G_SPAWN_SEARCH_PATH | G_SPAWN_DO_NOT_REAP_CHILD,
                                                        NULL, NULL,
                                                        gather_pid_callback, application,
                                                        &error);
#elif defined (G_OS_WIN32)
    {
        /* Everything with a command line goes through the broker, so the
         * program is never started as our child - GIO's own launch makes it
         * one, hooks and inherited environment and all. Only a store app has
         * no command line to give. */
        const gchar *command = nemo_associations_win32_command_for_app (application);

        if (command != NULL) {
            result = nemo_associations_win32_launch (command, locations, &error);
        } else {
            result = g_app_info_launch_uris (application,
                                             uris,
                                             G_APP_LAUNCH_CONTEXT (launch_context),
                                             &error);
        }
    }
#else
    result = g_app_info_launch_uris (application,
                                     uris,
                                     G_APP_LAUNCH_CONTEXT (launch_context),
                                     &error);
#endif

    if (!result && error != NULL) {
        g_warning ("Failed to launch application: %s", error->message);
        g_clear_error (&error);
    }

    g_object_unref (launch_context);

	if (result) {
		for (l = uris; l != NULL; l = l->next) {
			file = nemo_file_get_by_uri (l->data);
			nemo_recent_add_file (file, application);
			nemo_file_unref (file);
		}
	}

	g_list_free_full (locations, g_object_unref);
}

#ifndef G_OS_WIN32
static void
launch_application_from_command_internal (const gchar *full_command,
					  GdkScreen *screen,
					  gboolean use_terminal)
{
	GAppInfo *app;
	GdkAppLaunchContext *ctx;
	GdkDisplay *display;

	if (use_terminal) {
		eel_gnome_open_terminal_on_screen (full_command, screen);
	} else {
		app = g_app_info_create_from_commandline (full_command, NULL, 0, NULL);

		if (app != NULL) {
			display = gdk_screen_get_display (screen);
			ctx = gdk_display_get_app_launch_context (display);
			gdk_app_launch_context_set_screen (ctx, screen);

			g_app_info_launch (app, NULL, G_APP_LAUNCH_CONTEXT (ctx), NULL);

			g_object_unref (app);
			g_object_unref (ctx);
		}
	}
}					  
#endif

#ifdef G_OS_WIN32
/* GLib's launch from a command line goes through its spawn helper, which gives
 * a console program a console window and may never start it in the single
 * exe. Its split also reads a backslash as an escape. So the line is split the
 * Windows way and each parameter is an argument of its own. */
static void
launch_from_command (G_GNUC_UNUSED GdkScreen *screen,
		     const char *command_string,
		     gboolean use_terminal,
		     const char * const *parameters)
{
	char **command_argv = NULL;
	GPtrArray *args;
	GError *error = NULL;
	int i;

	if (!nemo_user_text_split_command (command_string, NULL, &command_argv, &error)) {
		g_warning ("Cannot run '%s': %s", command_string, error->message);
		g_clear_error (&error);
		return;
	}

	args = g_ptr_array_new ();
	for (i = 0; command_argv[i] != NULL; i++) {
		g_ptr_array_add (args, command_argv[i]);
	}
	for (i = 0; parameters != NULL && parameters[i] != NULL; i++) {
		g_ptr_array_add (args, (gpointer) parameters[i]);
	}
	g_ptr_array_add (args, NULL);

	if (!nemo_launch_win32_spawn ((const gchar * const *) args->pdata, use_terminal, &error)) {
		g_warning ("Could not start '%s': %s", command_argv[0], error->message);
		g_clear_error (&error);
	}

	g_ptr_array_free (args, TRUE);
	g_strfreev (command_argv);
}
#else
static void
launch_from_command (GdkScreen *screen,
		     const char *command_string,
		     gboolean use_terminal,
		     const char * const *parameters)
{
	char *full_command, *tmp;
	char *quoted_parameter;
	int i;

	full_command = g_strdup (command_string);

	for (i = 0; parameters != NULL && parameters[i] != NULL; i++) {
		quoted_parameter = g_shell_quote (parameters[i]);
		tmp = g_strconcat (full_command, " ", quoted_parameter, NULL);
		g_free (quoted_parameter);

		g_free (full_command);
		full_command = tmp;
	}

	launch_application_from_command_internal (full_command, screen, use_terminal);

	g_free (full_command);
}
#endif

/**
 * nemo_launch_application_from_command:
 * 
 * Fork off a process to launch an application with a given uri as
 * a parameter.
 * 
 * @command_string: The application to be launched, with any desired
 * command-line options. A path in it is quoted with nemo_user_text_quote.
 * @...: Passed as parameters to the application after quoting each of them.
 */
void
nemo_launch_application_from_command (GdkScreen  *screen,
					  const char *command_string, 
					  gboolean use_terminal,
					  ...)
{
	GPtrArray *parameters;
	char *parameter;
	va_list ap;

	parameters = g_ptr_array_new ();

	va_start (ap, use_terminal);
	while ((parameter = va_arg (ap, char *)) != NULL) {
		g_ptr_array_add (parameters, parameter);
	}
	va_end (ap);
	g_ptr_array_add (parameters, NULL);

	launch_from_command (screen, command_string, use_terminal,
			     (const char * const *) parameters->pdata);

	g_ptr_array_free (parameters, TRUE);
}

/**
 * nemo_launch_application_from_command_array:
 * 
 * Fork off a process to launch an application with a given uri as
 * a parameter.
 * 
 * @command_string: The application to be launched, with any desired
 * command-line options. A path in it is quoted with nemo_user_text_quote.
 * @parameters: Passed as parameters to the application after quoting each of them.
 */
void
nemo_launch_application_from_command_array (GdkScreen  *screen,
						const char *command_string,
						gboolean use_terminal,
						const char * const * parameters)
{
	launch_from_command (screen, command_string, use_terminal, parameters);
}

/* On Windows GTK's own way goes through GLib's spawn helper, a program of its
 * own in the single exe. */
gboolean
nemo_show_uri (GtkWindow   *parent_window,
	       const char  *uri,
	       GError     **error)
{
#ifdef G_OS_WIN32
	(void) parent_window;
	return nemo_launch_win32_open_uri (uri, error);
#else
	return gtk_show_uri (parent_window != NULL ? gtk_window_get_screen (parent_window) : NULL,
			     uri, gtk_get_current_event_time (), error);
#endif
}

void
nemo_launch_desktop_file (G_GNUC_UNUSED GdkScreen   *screen,
			      const char  *desktop_file_uri,
			      const GList *parameter_uris,
			      GtkWindow   *parent_window)
{
#ifdef G_OS_UNIX
	GError *error;
	char *message, *desktop_file_path;
	const GList *p;
	GList *files;
	int total, count;
	GFile *file, *desktop_file;
	GDesktopAppInfo *app_info;
	GdkAppLaunchContext *context;

	/* Don't allow command execution from remote locations
	 * to partially mitigate the security
	 * risk of executing arbitrary commands.
	 */
	desktop_file = g_file_new_for_uri (desktop_file_uri);
	desktop_file_path = g_file_get_path (desktop_file);
	if (!g_file_is_native (desktop_file)) {
		g_free (desktop_file_path);
		g_object_unref (desktop_file);
		eel_show_error_dialog
			(_("Sorry, but you cannot execute commands from "
			   "a remote site."), 
			 _("This is disabled due to security considerations."),
			 parent_window);
			 
		return;
	}
	g_object_unref (desktop_file);

	app_info = g_desktop_app_info_new_from_filename (desktop_file_path);
	g_free (desktop_file_path);
	if (app_info == NULL) {
		eel_show_error_dialog
			(_("There was an error launching the application."),
			 NULL,
			 parent_window);
		return;
	}
	
	/* count the number of uris with local paths */
	count = 0;
	total = g_list_length ((GList *) parameter_uris);
	files = NULL;
	for (p = parameter_uris; p != NULL; p = p->next) {
		file = g_file_new_for_uri ((const char *) p->data);
		if (g_file_is_native (file)) {
			count++;
		}
		files = g_list_prepend (files, file);
	}

	/* check if this app only supports local files */
	if (g_app_info_supports_files (G_APP_INFO (app_info)) &&
	    !g_app_info_supports_uris (G_APP_INFO (app_info)) &&
	    parameter_uris != NULL) {
		if (count == 0) {
			/* all files are non-local */
			eel_show_error_dialog
				(_("This drop target only supports local files."),
				 _("To open non-local files copy them to a local folder and then"
				   " drop them again."),
				 parent_window);

			g_list_free_full (files, g_object_unref);
			g_object_unref (app_info);
			return;
		} else if (count != total) {
			/* some files are non-local */
			eel_show_warning_dialog
				(_("This drop target only supports local files."),
				 _("To open non-local files copy them to a local folder and then"
				   " drop them again. The local files you dropped have already been opened."),
				 parent_window);
		}
	}

	error = NULL;
	context = gdk_display_get_app_launch_context (gtk_widget_get_display (GTK_WIDGET (parent_window)));
	/* TODO: Ideally we should accept a timestamp here instead of using GDK_CURRENT_TIME */
	gdk_app_launch_context_set_timestamp (context, GDK_CURRENT_TIME);
	gdk_app_launch_context_set_screen (context,
					   gtk_window_get_screen (parent_window));

    g_desktop_app_info_launch_uris_as_manager (app_info,
                                               (GList *) parameter_uris,
                                               G_APP_LAUNCH_CONTEXT (context),
                                               G_SPAWN_SEARCH_PATH | G_SPAWN_DO_NOT_REAP_CHILD,
                                               NULL, NULL,
                                               gather_pid_callback, app_info,
                                               &error);

	if (error != NULL) {
		message = g_strconcat (_("Details: "), error->message, NULL);
		eel_show_error_dialog
			(_("There was an error launching the application."),
			 message,
			 parent_window);
		
		g_error_free (error);
		g_free (message);
	}

	g_list_free_full (files, g_object_unref);
	g_object_unref (context);
	g_object_unref (app_info);
#else /* !G_OS_UNIX */
	/* .desktop launchers are a freedesktop concept; unsupported here. */
	(void) screen;
	(void) desktop_file_uri;
	(void) parameter_uris;
	eel_show_error_dialog (_("There was an error launching the application."),
			       NULL, parent_window);
#endif /* G_OS_UNIX */
}
