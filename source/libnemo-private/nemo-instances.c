/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */

/* nemo-instances.c - how running copies find and call each other.

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

#include <string.h>

#include "nemo-config.h"
#ifdef G_OS_WIN32
#include "nemo-instances-win32.h"
#endif

#define SETTINGS_INTERFACE "org.NemoAnywhere.Settings"

static const char settings_xml[] =
	"<node>"
	"  <interface name='" SETTINGS_INTERFACE "'>"
	"    <method name='TakeSettings'>"
	"      <arg type='t' name='origin' direction='in'/>"
	"      <arg type='a(sxbaay)' name='changes' direction='in'/>"
	"    </method>"
	"  </interface>"
	"</node>";

#ifndef G_OS_WIN32

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
#endif /* !G_OS_WIN32 */

/* What callers use, over whichever link this platform has. */

typedef struct {
	GDBusNodeInfo       *node;
	NemoInstanceHandler  handler;
	gpointer             user_data;
	guint                registration_id;
} Served;

static GPtrArray *served;
static gboolean   instances_up;

#ifndef G_OS_WIN32
static GDBusConnection *instances_bus;
static guint            instances_name_id;
static guint            instances_slots;
static guint            instances_settings_id;
static guint            instances_actions_id;
#endif

static void
served_free (gpointer data)
{
	Served *one = data;

	g_dbus_node_info_unref (one->node);
	g_free (one);
}

void
nemo_instances_serve (const char          *interface_xml,
                      NemoInstanceHandler  handler,
                      gpointer             user_data)
{
	GError *error = NULL;
	GDBusNodeInfo *node;
	Served *one;

	g_return_if_fail (!instances_up);

	node = g_dbus_node_info_new_for_xml (interface_xml, &error);
	g_assert_no_error (error);
	g_return_if_fail (node->interfaces != NULL && node->interfaces[0] != NULL);

	if (served == NULL) {
		served = g_ptr_array_new_with_free_func (served_free);
	}
	one = g_new0 (Served, 1);
	one->node = node;
	one->handler = handler;
	one->user_data = user_data;
	g_ptr_array_add (served, one);
}

/* Returns: (transfer full): the reply, or NULL with error set */
static GVariant *
run_handler (Served *one, const char *method_name, GVariant *parameters, GError **error)
{
	GError *local = NULL;
	GVariant *reply = one->handler (method_name, parameters, one->user_data, &local);

	if (local != NULL) {
		g_clear_pointer (&reply, g_variant_unref);
		g_propagate_error (error, local);
		return NULL;
	}
	return reply != NULL ? g_variant_ref_sink (reply) : g_variant_ref_sink (g_variant_new ("()"));
}

#ifndef G_OS_WIN32

static void
served_method_call (G_GNUC_UNUSED GDBusConnection       *connection,
                    G_GNUC_UNUSED const char            *sender,
                    G_GNUC_UNUSED const char            *object_path,
                    G_GNUC_UNUSED const char            *interface_name,
                    const char            *method_name,
                    GVariant              *parameters,
                    GDBusMethodInvocation *invocation,
                    gpointer               user_data)
{
	GError *error = NULL;
	GVariant *reply = run_handler (user_data, method_name, parameters, &error);

	if (reply == NULL) {
		g_dbus_method_invocation_take_error (invocation, error);
		return;
	}
	g_dbus_method_invocation_return_value (invocation, reply);
	g_variant_unref (reply);
}

gboolean
nemo_instances_start (GApplication *application)
{
	static const GDBusInterfaceVTable vtable = { served_method_call, NULL, NULL, { NULL } };
	GDBusConnection *connection;
	guint i;

	if (instances_up) {
		return TRUE;
	}
	connection = g_application_get_dbus_connection (application);
	if (connection == NULL) {
		return FALSE;
	}
	instances_bus = g_object_ref (connection);
	instances_up = TRUE;

	instances_settings_id = nemo_instance_share_settings (connection);
	for (i = 0; served != NULL && i < served->len; i++) {
		Served *one = g_ptr_array_index (served, i);
		GError *error = NULL;

		one->registration_id = g_dbus_connection_register_object (connection, NEMO_INSTANCE_OBJECT_PATH,
		                                                          one->node->interfaces[0], &vtable,
		                                                          one, NULL, &error);
		if (one->registration_id == 0) {
			g_warning ("Could not offer %s to the other copies: %s",
			           one->node->interfaces[0]->name, error->message);
			g_clear_error (&error);
		}
	}
	instances_slots = nemo_instance_publish (connection, &instances_name_id);

	/* GApplication may already serve the same group at this path; then the
	 * export fails and nothing is missing. */
	instances_actions_id = g_dbus_connection_export_action_group (connection, NEMO_INSTANCE_OBJECT_PATH,
	                                                              G_ACTION_GROUP (application), NULL);
	return TRUE;
}

void
nemo_instances_stop (void)
{
	guint i;

	if (!instances_up) {
		return;
	}
	nemo_instance_unshare_settings (instances_bus, instances_settings_id);
	instances_settings_id = 0;
	nemo_instance_unpublish (instances_bus, instances_name_id, instances_slots);
	instances_name_id = 0;
	instances_slots = 0;
	if (instances_actions_id != 0) {
		g_dbus_connection_unexport_action_group (instances_bus, instances_actions_id);
		instances_actions_id = 0;
	}
	for (i = 0; served != NULL && i < served->len; i++) {
		Served *one = g_ptr_array_index (served, i);

		if (one->registration_id != 0) {
			g_dbus_connection_unregister_object (instances_bus, one->registration_id);
			one->registration_id = 0;
		}
	}
	g_clear_object (&instances_bus);
	instances_up = FALSE;
}

/* Returns: (transfer full): free with g_strfreev */
GStrv
nemo_instances_list_others (void)
{
	return instances_bus != NULL ? nemo_instance_list_others (instances_bus) : NULL;
}

gboolean
nemo_instances_send_to_others (const char *interface_name,
                               const char *method_name,
                               GVariant   *parameters)
{
	if (instances_bus == NULL) {
		g_variant_unref (g_variant_ref_sink (parameters));
		return FALSE;
	}
	nemo_instance_send_to_others (instances_bus, interface_name, method_name, parameters);
	return TRUE;
}

/* Returns: (transfer full): NULL with error set */
GVariant *
nemo_instances_call (const char          *other,
                     const char          *interface_name,
                     const char          *method_name,
                     GVariant            *parameters,
                     const GVariantType  *reply_type,
                     int                  timeout_ms,
                     GError             **error)
{
	if (instances_bus == NULL) {
		if (parameters != NULL) {
			g_variant_unref (g_variant_ref_sink (parameters));
		}
		g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_NOT_CONNECTED, "No session bus");
		return NULL;
	}
	return g_dbus_connection_call_sync (instances_bus, other, NEMO_INSTANCE_OBJECT_PATH,
	                                    interface_name, method_name, parameters, reply_type,
	                                    G_DBUS_CALL_FLAGS_NO_AUTO_START, timeout_ms, NULL, error);
}

#else /* G_OS_WIN32 */

/* What GDBusActionGroup sends, so quit goes out the same on every platform. */
static const char actions_xml[] =
	"<node>"
	"  <interface name='org.gtk.Actions'>"
	"    <method name='Activate'>"
	"      <arg type='s' name='action_name' direction='in'/>"
	"      <arg type='av' name='parameter' direction='in'/>"
	"      <arg type='a{sv}' name='platform_data' direction='in'/>"
	"    </method>"
	"  </interface>"
	"</node>";

/* A copy waits this long for each of the others to take a message. Only how
   long a copy that has hung can hold things up; a busy one takes it at once. */
#define SEND_TIMEOUT_MS 1000

static GVariant *
activate_action (G_GNUC_UNUSED const char *method_name,
                 GVariant   *parameters,
                 gpointer    user_data,
                 GError    **error)
{
	GActionGroup *actions = user_data;
	const GVariantType *wants = NULL;
	GVariant *list, *parameter = NULL;
	const char *name;
	gboolean enabled = FALSE;

	g_variant_get (parameters, "(&s@av@a{sv})", &name, &list, NULL);
	if (g_variant_n_children (list) > 0) {
		GVariant *boxed = g_variant_get_child_value (list, 0);

		parameter = g_variant_get_variant (boxed);
		g_variant_unref (boxed);
	}
	g_variant_unref (list);

	if (!g_action_group_query_action (actions, name, &enabled, &wants, NULL, NULL, NULL) || !enabled ||
	    (wants == NULL) != (parameter == NULL) ||
	    (parameter != NULL && !g_variant_is_of_type (parameter, wants))) {
		g_set_error (error, G_DBUS_ERROR, G_DBUS_ERROR_INVALID_ARGS, "No action %s here to take that", name);
	} else {
		g_action_group_activate_action (actions, name, parameter);
	}
	g_clear_pointer (&parameter, g_variant_unref);
	return NULL;
}

static GVariant *
take_settings (G_GNUC_UNUSED const char *method_name,
               GVariant   *parameters,
               G_GNUC_UNUSED gpointer    user_data,
               G_GNUC_UNUSED GError    **error)
{
	nemo_config_take_shared (parameters);
	return NULL;
}

static void
send_settings (GVariant *changes, G_GNUC_UNUSED gpointer data)
{
	nemo_instances_send_to_others (SETTINGS_INTERFACE, "TakeSettings", changes);
}

static gboolean
args_match (GDBusArgInfo **args, GVariant *tuple)
{
	GString *wanted = g_string_new ("(");
	gboolean match;
	int i;

	for (i = 0; args != NULL && args[i] != NULL; i++) {
		g_string_append (wanted, args[i]->signature);
	}
	g_string_append_c (wanted, ')');
	match = strcmp (wanted->str, g_variant_get_type_string (tuple)) == 0;
	g_string_free (wanted, TRUE);

	return match;
}

/* The pipe server's way in, on the main thread. */
static GVariant *
dispatch (const char *interface_name,
          const char *method_name,
          GVariant   *parameters,
          GError    **error)
{
	guint i;

	if (!instances_up) {
		g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_CLOSED, "This copy is on its way out");
		return NULL;
	}
	for (i = 0; served != NULL && i < served->len; i++) {
		Served *one = g_ptr_array_index (served, i);
		GDBusInterfaceInfo *info = one->node->interfaces[0];
		GDBusMethodInfo *method;
		GVariant *reply;

		if (strcmp (info->name, interface_name) != 0) {
			continue;
		}
		method = g_dbus_interface_info_lookup_method (info, method_name);
		if (method == NULL) {
			g_set_error (error, G_DBUS_ERROR, G_DBUS_ERROR_UNKNOWN_METHOD, "No method %s", method_name);
			return NULL;
		}
		if (!args_match (method->in_args, parameters)) {
			g_set_error (error, G_DBUS_ERROR, G_DBUS_ERROR_INVALID_ARGS, "Wrong arguments for %s",
			             method_name);
			return NULL;
		}
		reply = run_handler (one, method_name, parameters, error);
		if (reply != NULL && !args_match (method->out_args, reply)) {
			g_critical ("%s answered %s", method_name, g_variant_get_type_string (reply));
			g_clear_pointer (&reply, g_variant_unref);
			g_set_error (error, G_DBUS_ERROR, G_DBUS_ERROR_FAILED, "Bad answer from %s", method_name);
		}
		return reply;
	}
	g_set_error (error, G_DBUS_ERROR, G_DBUS_ERROR_UNKNOWN_INTERFACE, "No interface %s", interface_name);
	return NULL;
}

gboolean
nemo_instances_start (GApplication *application)
{
	GError *error = NULL;

	if (instances_up) {
		return TRUE;
	}
	nemo_instances_serve (settings_xml, take_settings, NULL);
	nemo_instances_serve (actions_xml, activate_action, application);

	/* Up before the pipe, which is what lists this copy. */
	instances_up = TRUE;
	if (!nemo_instances_win32_start (dispatch, &error)) {
		g_warning ("Other copies cannot reach this one: %s", error->message);
		g_clear_error (&error);
		instances_up = FALSE;
		return FALSE;
	}
	nemo_config_set_share_func (send_settings, NULL, NULL);
	return TRUE;
}

void
nemo_instances_stop (void)
{
	if (!instances_up) {
		return;
	}
	nemo_config_set_share_func (NULL, NULL, NULL);
	nemo_instances_win32_stop ();
	instances_up = FALSE;
}

/* Returns: (transfer full): free with g_strfreev */
GStrv
nemo_instances_list_others (void)
{
	return nemo_instances_win32_list_others ();
}

gboolean
nemo_instances_send_to_others (const char *interface_name,
                               const char *method_name,
                               GVariant   *parameters)
{
	GStrv others = nemo_instances_win32_list_others ();
	int i;

	g_variant_ref_sink (parameters);
	for (i = 0; others[i] != NULL; i++) {
		GError *error = NULL;
		GVariant *ack = nemo_instances_win32_call (others[i], interface_name, method_name, parameters,
		                                           FALSE, SEND_TIMEOUT_MS, &error);

		if (ack == NULL) {
			/* A copy that has just gone is no news. */
			g_debug ("%s.%s to %s: %s", interface_name, method_name, others[i], error->message);
			g_clear_error (&error);
		} else {
			g_variant_unref (ack);
		}
	}
	g_variant_unref (parameters);
	g_strfreev (others);

	return TRUE;
}

/* Returns: (transfer full): NULL with error set */
GVariant *
nemo_instances_call (const char          *other,
                     const char          *interface_name,
                     const char          *method_name,
                     GVariant            *parameters,
                     const GVariantType  *reply_type,
                     int                  timeout_ms,
                     GError             **error)
{
	GVariant *reply;

	if (parameters == NULL) {
		parameters = g_variant_new ("()");
	}
	g_variant_ref_sink (parameters);
	reply = nemo_instances_win32_call (other, interface_name, method_name, parameters, TRUE,
	                                   timeout_ms < 0 ? 25000 : (guint) timeout_ms, error);
	g_variant_unref (parameters);

	if (reply != NULL && reply_type != NULL && !g_variant_is_of_type (reply, reply_type)) {
		g_set_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA, "%s.%s answered %s, not %.*s",
		             interface_name, method_name, g_variant_get_type_string (reply),
		             (int) g_variant_type_get_string_length (reply_type),
		             g_variant_type_peek_string (reply_type));
		g_clear_pointer (&reply, g_variant_unref);
	}
	return reply;
}

#endif /* G_OS_WIN32 */
