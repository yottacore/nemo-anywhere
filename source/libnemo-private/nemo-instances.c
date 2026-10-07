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
