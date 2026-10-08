/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */

/* arc-link-options.c - what happens to links and mounted filesystems.

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

#include "arc-link-options.h"
#include "arc-path-list.h"

void
arc_link_options_init (ArcLinkOptions *options)
{
	g_return_if_fail (options != NULL);

	options->symlinks = ARC_LINK_IGNORE;
	options->junctions = ARC_LINK_IGNORE;
	options->junctions_set = FALSE;
	options->follow_nested = TRUE;
	/* Never on by default. Another filesystem can be a whole drive or a
	   share, so it's asked for each time. */
	options->follow_other = FALSE;
}

void
arc_link_options_set_symlinks (ArcLinkOptions *options,
			       ArcLinkChoice   choice)
{
	g_return_if_fail (options != NULL);
	g_return_if_fail (choice == ARC_LINK_IGNORE || choice == ARC_LINK_FOLLOW ||
			  choice == ARC_LINK_STORE_SYMLINK);

	options->symlinks = choice;
}

void
arc_link_options_set_junctions (ArcLinkOptions *options,
				ArcLinkChoice   choice)
{
	g_return_if_fail (options != NULL);
	g_return_if_fail (choice <= ARC_LINK_STORE_JUNCTION);

	options->junctions = choice;
	options->junctions_set = TRUE;
}

ArcLinkChoice
arc_junctions_default (ArcLinkChoice symlinks,
		       guint         stores)
{
	switch (symlinks) {
	case ARC_LINK_FOLLOW:
		return ARC_LINK_FOLLOW;
	case ARC_LINK_STORE_SYMLINK:
		if (stores & ARC_STORES_JUNCTIONS) {
			return ARC_LINK_STORE_JUNCTION;
		}
		if (stores & ARC_STORES_JUNCTIONS_AS_SYMLINKS) {
			return ARC_LINK_STORE_SYMLINK;
		}
		return ARC_LINK_IGNORE;
	case ARC_LINK_IGNORE:
	case ARC_LINK_STORE_JUNCTION:
	default:
		return ARC_LINK_IGNORE;
	}
}

ArcLinkChoice
arc_link_options_symlinks (const ArcLinkOptions *options,
			   guint                 stores)
{
	g_return_val_if_fail (options != NULL, ARC_LINK_IGNORE);

	switch (options->symlinks) {
	case ARC_LINK_FOLLOW:
		return ARC_LINK_FOLLOW;
	case ARC_LINK_STORE_SYMLINK:
		return (stores & ARC_STORES_SYMLINKS) ? ARC_LINK_STORE_SYMLINK : ARC_LINK_IGNORE;
	case ARC_LINK_IGNORE:
	case ARC_LINK_STORE_JUNCTION:
	default:
		return ARC_LINK_IGNORE;
	}
}

ArcLinkChoice
arc_link_options_junctions (const ArcLinkOptions *options,
			    guint                 stores)
{
	ArcLinkChoice choice;

	g_return_val_if_fail (options != NULL, ARC_LINK_IGNORE);

	choice = options->junctions_set ? options->junctions
		: arc_junctions_default (options->symlinks, stores);

	switch (choice) {
	case ARC_LINK_FOLLOW:
		return ARC_LINK_FOLLOW;
	case ARC_LINK_STORE_JUNCTION:
		return (stores & ARC_STORES_JUNCTIONS) ? choice : ARC_LINK_IGNORE;
	case ARC_LINK_STORE_SYMLINK:
		return (stores & ARC_STORES_JUNCTIONS_AS_SYMLINKS) ? choice : ARC_LINK_IGNORE;
	case ARC_LINK_IGNORE:
	default:
		return ARC_LINK_IGNORE;
	}
}

guint
arc_link_options_follow (const ArcLinkOptions *options,
			 guint                 stores)
{
	guint follow = 0;

	g_return_val_if_fail (options != NULL, 0);

	if (arc_link_options_symlinks (options, stores) == ARC_LINK_FOLLOW) {
		follow |= ARC_FOLLOW_SYMLINKS;
	}
	if (arc_link_options_junctions (options, stores) == ARC_LINK_FOLLOW) {
		follow |= ARC_FOLLOW_JUNCTIONS;
	}
	if (options->follow_nested) {
		follow |= ARC_FOLLOW_NESTED_FS;
	}
	if (options->follow_other) {
		follow |= ARC_FOLLOW_OTHER_FS;
	}

	return follow;
}

ArcLinkChoice
arc_link_choice_from_boxes (gboolean store_links,
			    gboolean follow_links)
{
	if (store_links) {
		return ARC_LINK_STORE_SYMLINK;
	}
	return follow_links ? ARC_LINK_FOLLOW : ARC_LINK_IGNORE;
}
