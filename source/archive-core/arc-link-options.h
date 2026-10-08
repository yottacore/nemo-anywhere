/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */

/* arc-link-options.h - what happens to links and mounted filesystems.

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

/* The Symlinks and Junctions choices and the 2 filesystem options, as chosen.
 * A writer that can't do a store choice gets Ignore in its place, but the
 * choice itself is kept, so a format that can store it puts it back. "Link
 * options" and "Junction defaults" in
 * project/design_docs/20260929-101432_compression.md.
 */

#ifndef ARC_LINK_OPTIONS_H
#define ARC_LINK_OPTIONS_H

#include <glib.h>

G_BEGIN_DECLS

typedef enum {
	ARC_LINK_IGNORE,
	ARC_LINK_FOLLOW,
	ARC_LINK_STORE_SYMLINK,
	ARC_LINK_STORE_JUNCTION		/* Junctions only */
} ArcLinkChoice;

/* What a writer can store, from what it claims. */
typedef enum {
	ARC_STORES_SYMLINKS              = 1 << 0,
	ARC_STORES_JUNCTIONS             = 1 << 1,	/* a junction as a junction */
	ARC_STORES_JUNCTIONS_AS_SYMLINKS = 1 << 2
} ArcLinkStores;

typedef struct {
	ArcLinkChoice symlinks;
	/* Read only when junctions_set. Until Junctions is changed by hand it
	   takes its default from Symlinks, by the table. */
	ArcLinkChoice junctions;
	gboolean      junctions_set;
	gboolean      follow_nested;
	gboolean      follow_other;
} ArcLinkOptions;

/* Ignore, Junctions by the table, nested on, other off. */
void          arc_link_options_init          (ArcLinkOptions *options);

/* A Symlinks change. Junctions follows it unless set by hand. */
void          arc_link_options_set_symlinks  (ArcLinkOptions *options,
					      ArcLinkChoice   choice);
/* A hand change to Junctions. It sticks. */
void          arc_link_options_set_junctions (ArcLinkOptions *options,
					      ArcLinkChoice   choice);

/* The Junctions default for a Symlinks choice, given what the writer stores. */
ArcLinkChoice arc_junctions_default          (ArcLinkChoice   symlinks,
					      guint           stores);

/* The choices with this writer: a store it can't do is Ignore. */
ArcLinkChoice arc_link_options_symlinks      (const ArcLinkOptions *options,
					      guint                 stores);
ArcLinkChoice arc_link_options_junctions     (const ArcLinkOptions *options,
					      guint                 stores);

/* The ArcFollow bits for the path list and the scan. */
guint         arc_link_options_follow        (const ArcLinkOptions *options,
					      guint                 stores);

/* The Symlinks choice for the 2 boxes it replaces, "store links" and "follow
   links". Store wins, since storing a link never followed it. */
ArcLinkChoice arc_link_choice_from_boxes     (gboolean store_links,
					      gboolean follow_links);

G_END_DECLS

#endif /* ARC_LINK_OPTIONS_H */
