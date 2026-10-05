/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */

/* eel-gnome-extensions.c - implementation of new functions that operate on
                            gnome classes. Perhaps some of these should be
  			    rolled into gnome someday.

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

   Authors: Darin Adler <darin@eazel.com>
*/

#include <config.h>

#define GNOME_DESKTOP_USE_UNSTABLE_API

#include "eel-gnome-extensions.h"

#include <gtk/gtk.h>

/* Adapted from gio - gdesktopappinfo.c - prepend_terminal_to_vector */
static const struct {
    const char *exec;
    const char *exec_arg;
    const gboolean escape_command;
} known_terminals[] = {
    { "alacritty", "-e", TRUE },
    { "color-xterm", "-e", TRUE },
    { "dtterm", "-e", TRUE },
    { "foot", "--", FALSE },
    { "gnome-terminal", "--", FALSE },
    { "kgx", "-e", TRUE },
    { "kitty", "--", FALSE },
    { "konsole", "-e", TRUE },
    { "lxterminal", "-e", TRUE },
    { "mate-terminal", "-x", TRUE },
    { "nxterm", "-e", TRUE },
    { "rxvt", "-e", TRUE },
    { "sakura", "-x", TRUE },
    { "st", "-e", TRUE },
    { "terminator", "-x", TRUE },
    { "terminology", "-e", TRUE },
    { "tilix", "-e", TRUE },
    { "wezterm", "--", TRUE },
    { "xfce4-terminal", "-x", FALSE },
    { "xterm", "-e", TRUE },
};

/* The desktop's terminal choice, where a desktop publishes one. NULL
   otherwise - the known-terminals scan below is the answer everywhere else.
   (eel sits under libnemo-private, so it cannot ask the config store.) */
static GSettings *
terminal_settings_new (void)
{
    GSettingsSchemaSource *source = g_settings_schema_source_get_default ();
    GSettingsSchema *schema = source ? g_settings_schema_source_lookup (source, "org.cinnamon.desktop.default-applications.terminal", TRUE) : NULL;

    if (schema != NULL) {
        g_settings_schema_unref (schema);
        return g_settings_new ("org.cinnamon.desktop.default-applications.terminal");
    }
    return NULL;
}

/* Returns: (transfer full): free with g_free */
char *
eel_gnome_get_fallback_terminal_exec (void)
{
    gsize i;

    for (i = 0; i < G_N_ELEMENTS (known_terminals); i++) {
        gchar *term_path = g_find_program_in_path (known_terminals[i].exec);

        if (term_path != NULL) {
            g_free (term_path);
            return g_strdup (known_terminals[i].exec);
        }
    }

    return NULL;
}

static char *
prepend_terminal_to_command_line (const char *command_line)
{
    GSettings *settings;
    GString *prefix = NULL;
    gchar *terminal = NULL;
    gchar *ret = NULL;
    GString *escaped_command_line = NULL;
    gsize i;

    g_return_val_if_fail (command_line != NULL, g_strdup (command_line));

    settings = terminal_settings_new ();
    terminal = settings ? g_settings_get_string (settings, "exec") : NULL;

    if (terminal != NULL) {
        for (i = 0; i < G_N_ELEMENTS (known_terminals); i++) {
            if (g_strcmp0 (known_terminals[i].exec, terminal) == 0) {
                gchar *tmp = g_strdup_printf ("%s %s", known_terminals[i].exec, known_terminals[i].exec_arg);
                prefix = g_string_new (tmp);
                g_free (tmp);
                break;
            }
        }
    }

    if (prefix == NULL) {
        gchar *term_path = NULL;

        for (i = 0; i < G_N_ELEMENTS (known_terminals); i++) {
            term_path = g_find_program_in_path (known_terminals[i].exec);
            if (term_path != NULL) {
                gchar *tmp = g_strdup_printf ("%s %s", known_terminals[i].exec, known_terminals[i].exec_arg);
                prefix = g_string_new (tmp);
                g_free (tmp);
                break;
            }
        }
        g_free (term_path);
    }

    /* No configured terminal and none of the known ones on PATH - a real
       case off Cinnamon. Decline rather than crash on prefix below. */
    if (prefix == NULL) {
        g_clear_object (&settings);
        g_free (terminal);
        return NULL;
    }

    // Escape space characters in the command line if needed
    if (prefix != NULL && known_terminals[i].escape_command) {
        escaped_command_line = g_string_new("");
        for (const gchar *p = command_line; *p != '\0'; p++) {
            if (*p == ' ') {
                g_string_append(escaped_command_line, "\\ ");
            } else {
                g_string_append_c(escaped_command_line, *p);
            }
        }
    }

    if (escaped_command_line == NULL) {
        escaped_command_line = g_string_new (command_line);
    }

    g_clear_object (&settings);

    ret = g_strdup_printf ("%s %s", prefix->str, escaped_command_line->str);
    g_string_free (prefix, TRUE);
    g_free (terminal);
    g_string_free (escaped_command_line, TRUE);

    return ret;
}

/* Return a command string containing the path to a terminal on this system. */

void
eel_gnome_open_terminal_on_screen (const gchar *command,
                                   GdkScreen   *screen)
{
    gchar *command_line;
    GAppInfo *app;
    GdkAppLaunchContext *ctx;
    GError *error = NULL;
    GdkDisplay *display;

    command_line = prepend_terminal_to_command_line (command);
    if (command_line == NULL) {
        g_message ("Could not find a terminal emulator");
        return;
    }

    app = g_app_info_create_from_commandline (command_line, NULL, 0, &error);

	if (app != NULL && screen != NULL) {
		display = gdk_screen_get_display (screen);
		ctx = gdk_display_get_app_launch_context (display);
		gdk_app_launch_context_set_screen (ctx, screen);

		g_app_info_launch (app, NULL, G_APP_LAUNCH_CONTEXT (ctx), &error);

		g_object_unref (app);
		g_object_unref (ctx);
	}

	if (error != NULL) {
		g_message ("Could not start application on terminal: %s", error->message);

		g_error_free (error);
	}

    g_free (command_line);
}
