/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */

/* nemo-desktop-settings.c - read-only interop with the desktop's own settings.

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

#include <config.h>

#include "nemo-desktop-settings.h"
#include "nemo-config.h"

#include <gio/gio.h>

/* Present only where the running desktop installs them. */
static GSettings *de_terminal;
static GSettings *de_privacy;
static GSettings *de_interface;

/* Our stand-ins, always present. */
static NemoConfigGroup *own_terminal;
static NemoConfigGroup *own_privacy;
static NemoConfigGroup *own_interface;

static gboolean ready;

/* g_settings_new aborts on a schema that is not installed, so look first. */
static GSettings *
open_if_installed (const char *schema_id)
{
	GSettingsSchemaSource *source = g_settings_schema_source_get_default ();
	GSettingsSchema       *schema;

	if (source == NULL)
		return NULL;

	schema = g_settings_schema_source_lookup (source, schema_id, TRUE);
	if (schema == NULL)
		return NULL;

	g_settings_schema_unref (schema);
	return g_settings_new (schema_id);
}

void
nemo_desktop_settings_init (void)
{
	if (ready)
		return;

	de_terminal  = open_if_installed ("org.cinnamon.desktop.default-applications.terminal");
	de_privacy   = open_if_installed ("org.cinnamon.desktop.privacy");
	de_interface = open_if_installed ("org.cinnamon.desktop.interface");

	own_terminal  = nemo_config_get_group ("terminal");
	own_privacy   = nemo_config_get_group ("privacy");
	own_interface = nemo_config_get_group ("interface");

	ready = TRUE;
}

void
nemo_desktop_settings_finalize (void)
{
	g_clear_object (&de_terminal);
	g_clear_object (&de_privacy);
	g_clear_object (&de_interface);
	ready = FALSE;
}

void
nemo_desktop_settings_set_filechooser_bool (const char *key, gboolean value)
{
	GSettingsSchemaSource *source = g_settings_schema_source_get_default ();
	GSettingsSchema       *schema;
	GSettings             *fc;

	if (source == NULL)
		return;

	schema = g_settings_schema_source_lookup (source, "org.gtk.Settings.FileChooser", TRUE);
	if (schema == NULL)
		return;
	g_settings_schema_unref (schema);

	fc = g_settings_new_with_path ("org.gtk.Settings.FileChooser",
	                               "/org/gtk/settings/file-chooser/");
	g_settings_set_boolean (fc, key, value);
	g_object_unref (fc);
}

/* A terminal named in our own preferences beats whatever the desktop publishes:
   the desktop's is a fallback, not an override. */
static gboolean
own_terminal_chosen (void)
{
	g_autofree char *chosen = nemo_config_get_string (own_terminal, "exec");

	return chosen != NULL && *chosen != '\0';
}

/* Returns: (transfer full): free with g_free */
char *
nemo_desktop_settings_get_terminal_exec (void)
{
	if (de_terminal != NULL && !own_terminal_chosen ())
		return g_settings_get_string (de_terminal, "exec");
	return nemo_config_get_string (own_terminal, "exec");
}

/* Returns: (transfer full): free with g_free */
char *
nemo_desktop_settings_get_terminal_exec_arg (void)
{
	if (de_terminal != NULL && !own_terminal_chosen ())
		return g_settings_get_string (de_terminal, "exec-arg");
	return nemo_config_get_string (own_terminal, "exec-arg");
}

gboolean
nemo_desktop_settings_get_recent_enabled (void)
{
	if (de_privacy != NULL)
		return g_settings_get_boolean (de_privacy, "remember-recent-files");
	return nemo_config_get_boolean (own_privacy, "remember-recent-files");
}

gboolean
nemo_desktop_settings_get_clock_use_24h (void)
{
	if (de_interface != NULL)
		return g_settings_get_boolean (de_interface, "clock-use-24h");
	return nemo_config_get_boolean (own_interface, "clock-use-24h");
}

void
nemo_desktop_settings_watch (const char *key, GCallback callback, gpointer user_data)
{
	GSettings       *de  = NULL;
	NemoConfigGroup *own = NULL;
	char            *detailed;

	if (g_strcmp0 (key, "remember-recent-files") == 0) {
		de = de_privacy;
		own = own_privacy;
	} else if (g_strcmp0 (key, "clock-use-24h") == 0) {
		de = de_interface;
		own = own_interface;
	} else {
		g_critical ("nemo-desktop-settings: '%s' is not watchable", key);
		return;
	}

	detailed = g_strdup_printf ("changed::%s", key);
	if (de != NULL)
		g_signal_connect_swapped (de, detailed, callback, user_data);
	else
		g_signal_connect_swapped (own, detailed, callback, user_data);
	g_free (detailed);
}

void
nemo_desktop_settings_unwatch (gpointer user_data)
{
	if (de_privacy != NULL)
		g_signal_handlers_disconnect_by_data (de_privacy, user_data);
	else if (own_privacy != NULL)
		g_signal_handlers_disconnect_by_data (own_privacy, user_data);

	if (de_interface != NULL)
		g_signal_handlers_disconnect_by_data (de_interface, user_data);
	else if (own_interface != NULL)
		g_signal_handlers_disconnect_by_data (own_interface, user_data);
}
