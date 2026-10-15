/* nemo-instances-win32.h - the copies' named pipes, under nemo-instances.c
 *
 * Copyright © 2026 t00mietum (CryptogID: ปʬϝღถɔ4რఠΔթะ9ƾǝu)
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published
 * by the Free Software Foundation.
 */

#ifndef NEMO_INSTANCES_WIN32_H
#define NEMO_INSTANCES_WIN32_H

#include <gio/gio.h>

/* Answers one call from another copy, on the main thread. Returns the reply
   tuple, or NULL with error set. */
typedef GVariant *(*NemoInstancesDispatch) (const char  *interface_name,
                                            const char  *method_name,
                                            GVariant    *parameters,
                                            GError     **error);

/* Opens this copy's pipe, which is what puts it in the others' list. */
gboolean  nemo_instances_win32_start       (NemoInstancesDispatch   dispatch,
                                            GError                **error);
void      nemo_instances_win32_stop        (void);

/* Pipe names of the other copies of this user in this logon session.
   Returns: (transfer full): never NULL; free with g_strfreev */
GStrv     nemo_instances_win32_list_others (void);

/* parameters is a tuple, and stays the caller's. With wants_reply FALSE the
   other copy answers () as soon as it has the message, before it runs it.
   Returns: (transfer full): the reply tuple, or NULL with error set */
GVariant *nemo_instances_win32_call        (const char  *other,
                                            const char  *interface_name,
                                            const char  *method_name,
                                            GVariant    *parameters,
                                            gboolean     wants_reply,
                                            guint        timeout_ms,
                                            GError     **error);

#endif /* NEMO_INSTANCES_WIN32_H */
