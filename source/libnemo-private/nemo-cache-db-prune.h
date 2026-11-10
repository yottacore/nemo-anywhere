/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */

/* nemo-cache-db-prune.h - when the file cache is cleaned up.

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

/* The pass itself is nemo_cache_db_prune. This decides when to run one: at
 * random somewhere between prune-min-hours and prune-max-hours after the last,
 * and only once nothing has been drawn for prune-idle-minutes, so it doesn't
 * compete with a folder that is filling with thumbnails. */

#ifndef NEMO_CACHE_DB_PRUNE_H
#define NEMO_CACHE_DB_PRUNE_H

#include "nemo-cache-db.h"

G_BEGIN_DECLS

/* Starts the check timer. Safe to call more than once. */
void nemo_cache_db_prune_schedule (void);

typedef void (*NemoCachePruneDone) (NemoCachePruneResult result,
				    gint64               removed,
				    gpointer             user_data);

/* A pass straight away whether one is due or not, for the button that asks for
 * one. `done` is called on the main loop with how it went, unless quitting
 * stops it first. False, and nothing called, if this process is already
 * running one. */
gboolean nemo_cache_db_prune_now (NemoCachePruneDone done, gpointer user_data);

/* Whether this process has a pass going. */
gboolean nemo_cache_db_prune_running (void);

/* Stops the timer, and stops a pass that is running, waiting a moment for it
 * to let go of its claim. For quitting. */
void nemo_cache_db_prune_stop (void);

G_END_DECLS

#endif /* NEMO_CACHE_DB_PRUNE_H */
