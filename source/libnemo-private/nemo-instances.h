/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */

/* nemo-instances.h - how running copies find and call each other.

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

#ifndef NEMO_INSTANCES_H
#define NEMO_INSTANCES_H

#include <gio/gio.h>

G_BEGIN_DECLS

/* The copies reach each other through the calls below, which carry D-Bus
   style calls: an interface, a method and a GVariant tuple. Over the session
   bus where there is one. On Windows there is none, since GLib would have to
   start a bus of its own there, so each copy serves a named pipe instead (see
   nemo-instances-win32.c). */

/* Runs on the main thread. Returns the reply tuple, NULL for a method with no
   out arguments, or NULL with error set. */
typedef GVariant *(*NemoInstanceHandler) (const char *method_name,
                                         GVariant   *parameters,
                                         gpointer    user_data,
                                         GError    **error);

/* What the other copies may call here. interface_xml is introspection data
   for one interface, and arguments are checked against it before the handler
   sees them. Before nemo_instances_start. */
void      nemo_instances_serve  (const char          *interface_xml,
                                 NemoInstanceHandler  handler,
                                 gpointer             user_data);

/* Starts serving, shares settings changes, and takes this copy's place in the
   list. The application's actions can be activated from the other copies,
   which is how they are asked to quit. FALSE when the others cannot be
   reached at all, such as with no session bus. */
gboolean  nemo_instances_start  (GApplication *application);

/* Sends any settings change still waiting, then leaves the list. */
void      nemo_instances_stop   (void);

/* Every other running copy, by a name only good for nemo_instances_call.
   NULL when they cannot be reached.
   Returns: (transfer full): free with g_strfreev */
GStrv     nemo_instances_list_others (void);

/* One call to every other copy, with no answer waited for. Takes a floating
   parameters. FALSE when they cannot be reached. */
gboolean  nemo_instances_send_to_others (const char *interface_name,
                                         const char *method_name,
                                         GVariant   *parameters);

/* One call to one copy, waiting up to timeout_ms for the answer. Takes a
   floating parameters, which may be NULL for none.
   Returns: (transfer full): the reply tuple, or NULL with error set */
GVariant *nemo_instances_call   (const char          *other,
                                 const char          *interface_name,
                                 const char          *method_name,
                                 GVariant            *parameters,
                                 const GVariantType  *reply_type,
                                 int                  timeout_ms,
                                 GError             **error);

#ifndef G_OS_WIN32
/* Every running copy queues on the one name, so the oldest answers callers
   from outside and the name passes on when it quits. The list of copies comes
   from numbered slot names instead, since GLib's own bus answers both
   ListQueuedOwners and ListNames with an empty list. Each copy takes the first
   free slot and queues on every slot below it, so a slot whose copy ends
   passes up to a live one and the taken slots never have a gap. Asking who
   owns each slot in turn, up to the first free one, finds every copy. See
   design.md, "One process per window". The actions ride at the path
   GApplication would use, so an older copy that still registers the old way
   is reachable the same way. Tab moves and settings changes are served at
   the same path. */
#define NEMO_INSTANCE_BUS_NAME    "org.NemoAnywhere"
#define NEMO_INSTANCE_SLOT_PREFIX NEMO_INSTANCE_BUS_NAME ".Slot"
#define NEMO_INSTANCE_OBJECT_PATH "/org/NemoAnywhere"

/* Queues on the shared name and takes a slot. Returns the number of slot
   names asked for, which goes back to nemo_instance_unpublish along with
   shared_id. */
guint  nemo_instance_publish   (GDBusConnection *connection,
                                guint           *shared_id);

void   nemo_instance_unpublish (GDBusConnection *connection,
                                guint            shared_id,
                                guint            slots);

/* Unique bus names of every other running copy, empty when the bus would
   not say.
   Returns: (transfer full): free with g_strfreev. */
GStrv  nemo_instance_list_others (GDBusConnection *connection);

/* One call to every other copy, at the instance path, with no answer waited
   for. Takes a floating parameters. */
void   nemo_instance_send_to_others (GDBusConnection *connection,
                                     const char      *interface_name,
                                     const char      *method_name,
                                     GVariant        *parameters);

/* Takes setting changes from the other copies and sends them this copy's.
   Before nemo_instance_publish, so no copy finds this one before it can take
   them. Returns the registration id for nemo_instance_unshare_settings, or 0. */
guint  nemo_instance_share_settings   (GDBusConnection *connection);

/* Sends what is still waiting, then stops. */
void   nemo_instance_unshare_settings (GDBusConnection *connection,
                                       guint            registration_id);
#endif

G_END_DECLS

#endif /* NEMO_INSTANCES_H */
