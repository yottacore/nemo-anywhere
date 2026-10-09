/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */

/* nemo-instances.c - how running copies find each other on the bus.

   Copyright © 2026 t00mietum (CryptogID: ปʬϝღถɔ4რఠΔթะ9ƾǝu).

   This program is free software; you can redistribute it and/or
   modify it under the terms of the GNU General Public License as
   published by the Free Software Foundation; version 2 of the
   License.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, see <http://www.gnu.org/licenses/>.
*/

#include <config.h>
#include "nemo-instances.h"

#include "nemo-config.h"

#define SETTINGS_INTERFACE NEMO_INSTANCE_BUS_NAME ".Settings"

/* Only a stop for a bus that answers nonsense. */
#define SLOT_LIMIT 4096

/* RequestName replies, from the D-Bus spec. */
#define REPLY_PRIMARY_OWNER 1
#define REPLY_ALREADY_OWNER 4

static char *
slot_name (guint slot)
{
	return g_strdup_printf (NEMO_INSTANCE_SLOT_PREFIX "%u", slot);
}

static GVariant *
ask_bus (GDBusConnection *connection, const char *method, GVariant *args, const char *reply_type)
{
	return g_dbus_connection_call_sync (connection,
	                                    "org.freedesktop.DBus", "/org/freedesktop/DBus",
	                                    "org.freedesktop.DBus", method, args,
	                                    G_VARIANT_TYPE (reply_type),
	                                    G_DBUS_CALL_FLAGS_NONE, -1, NULL, NULL);
}

guint
nemo_instance_publish (GDBusConnection *connection,
                       guint           *shared_id)
{
	guint slot;

	*shared_id = g_bus_own_name_on_connection (connection, NEMO_INSTANCE_BUS_NAME,
	                                           G_BUS_NAME_OWNER_FLAGS_NONE,
	                                           NULL, NULL, NULL, NULL);

	/* Queued on every taken slot on the way up, which is what fills a gap
	 * when one of those copies ends. */
	for (slot = 0; slot < SLOT_LIMIT; slot++) {
		char *name = slot_name (slot);
		GVariant *reply = ask_bus (connection, "RequestName",
		                           g_variant_new ("(su)", name, 0), "(u)");
		guint32 result = 0;

		g_free (name);
		if (reply == NULL) {
			return slot;
		}
		g_variant_get (reply, "(u)", &result);
		g_variant_unref (reply);
		if (result == REPLY_PRIMARY_OWNER || result == REPLY_ALREADY_OWNER) {
			return slot + 1;
		}
	}

	return slot;
}

void
nemo_instance_unpublish (GDBusConnection *connection,
                         guint            shared_id,
                         guint            slots)
{
	guint slot;

	if (shared_id != 0) {
		g_bus_unown_name (shared_id);
	}
	for (slot = 0; slot < slots; slot++) {
		char *name = slot_name (slot);

		g_dbus_connection_call (connection, "org.freedesktop.DBus", "/org/freedesktop/DBus",
		                        "org.freedesktop.DBus", "ReleaseName",
		                        g_variant_new ("(s)", name), NULL,
		                        G_DBUS_CALL_FLAGS_NONE, -1, NULL, NULL, NULL);
		g_free (name);
	}
}

/* Returns: (transfer full): free with g_strfreev */
GStrv
nemo_instance_list_others (GDBusConnection *connection)
{
	GPtrArray *others = g_ptr_array_new ();
	const char *me = g_dbus_connection_get_unique_name (connection);
	guint slot;

	for (slot = 0; slot < SLOT_LIMIT; slot++) {
		char *name = slot_name (slot);
		GVariant *reply = ask_bus (connection, "GetNameOwner",
		                           g_variant_new ("(s)", name), "(s)");
		char *owner = NULL;

		g_free (name);
		if (reply == NULL) {
			break;
		}
		g_variant_get (reply, "(s)", &owner);
		g_variant_unref (reply);

		/* A copy holds every slot that passed up to it. */
		if (g_strcmp0 (owner, me) == 0 ||
		    g_ptr_array_find_with_equal_func (others, owner, g_str_equal, NULL)) {
			g_free (owner);
		} else {
			g_ptr_array_add (others, owner);
		}
	}
	g_ptr_array_add (others, NULL);

	return (GStrv) g_ptr_array_free (others, FALSE);
}

void
nemo_instance_send_to_others (GDBusConnection *connection,
                              const char      *interface_name,
                              const char      *method_name,
                              GVariant        *parameters)
{
	GStrv others = nemo_instance_list_others (connection);
	int i;

	g_variant_ref_sink (parameters);
	/* No callback, so no reply is asked for, and a copy that has just gone
	 * costs nothing. */
	for (i = 0; others[i] != NULL; i++) {
		g_dbus_connection_call (connection, others[i], NEMO_INSTANCE_OBJECT_PATH,
		                        interface_name, method_name, parameters, NULL,
		                        G_DBUS_CALL_FLAGS_NONE, -1, NULL, NULL, NULL);
	}
	/* Only queued so far, and a copy about to exit has to see them out. */
	if (others[0] != NULL) {
		g_dbus_connection_flush_sync (connection, NULL, NULL);
	}
	g_variant_unref (parameters);
	g_strfreev (others);
}

static const char settings_xml[] =
	"<node>"
	"  <interface name='" SETTINGS_INTERFACE "'>"
	"    <method name='TakeSettings'>"
	"      <arg type='t' name='origin' direction='in'/>"
	"      <arg type='a(sxbaay)' name='changes' direction='in'/>"
	"    </method>"
	"  </interface>"
	"</node>";

static void
settings_method_call (G_GNUC_UNUSED GDBusConnection       *connection,
                      G_GNUC_UNUSED const char            *sender,
                      G_GNUC_UNUSED const char            *object_path,
                      G_GNUC_UNUSED const char            *interface_name,
                      G_GNUC_UNUSED const char            *method_name,
                      GVariant              *parameters,
                      GDBusMethodInvocation *invocation,
                      G_GNUC_UNUSED gpointer               user_data)
{
	nemo_config_take_shared (parameters);
	g_dbus_method_invocation_return_value (invocation, NULL);
}

static void
send_settings (GVariant *changes, gpointer connection)
{
	nemo_instance_send_to_others (connection, SETTINGS_INTERFACE, "TakeSettings", changes);
}

guint
nemo_instance_share_settings (GDBusConnection *connection)
{
	static const GDBusInterfaceVTable vtable = { settings_method_call, NULL, NULL, { NULL } };
	GDBusNodeInfo *info;
	GError *error = NULL;
	guint id;

	info = g_dbus_node_info_new_for_xml (settings_xml, &error);
	g_assert_no_error (error);

	id = g_dbus_connection_register_object (connection, NEMO_INSTANCE_OBJECT_PATH,
	                                        info->interfaces[0], &vtable, NULL, NULL, &error);
	g_dbus_node_info_unref (info);
	if (id == 0) {
		/* Then the file watch is the only way a change gets here. */
		g_warning ("Could not take settings changes from other copies: %s", error->message);
		g_clear_error (&error);
		return 0;
	}
	nemo_config_set_share_func (send_settings, g_object_ref (connection), g_object_unref);

	return id;
}

void
nemo_instance_unshare_settings (GDBusConnection *connection,
                                guint            registration_id)
{
	if (registration_id == 0) {
		return;
	}
	nemo_config_set_share_func (NULL, NULL, NULL);
	g_dbus_connection_unregister_object (connection, registration_id);
}
