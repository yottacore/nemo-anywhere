/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */

/* nemo-instances.h - how running copies find each other on the bus.

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

/* Every running copy queues on the one name, so the oldest answers callers
   from outside and the name passes on when it quits. The list of copies comes
   from numbered slot names instead, since GLib's bus on Windows answers both
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

G_END_DECLS

#endif /* NEMO_INSTANCES_H */
