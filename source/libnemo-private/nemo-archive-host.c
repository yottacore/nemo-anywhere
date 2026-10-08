/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */

/* nemo-archive-host.c - the app's side of the archive core.

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

#include <string.h>

#include "nemo-archive-host.h"
#include "nemo-archive-commands.h"
#include "nemo-config.h"
#include "nemo-dir-enum.h"
#include "nemo-share.h"

/* The old pair meant one choice for both kinds of link, so Junctions is left
   to follow Symlinks. An old store links that was never changed reads as its
   old default, on, which is what the job did with it. */
static void
map_old_boxes (NemoConfigGroup *group)
{
	ArcLinkChoice was;

	if (!nemo_config_is_set (group, NEMO_ARCHIVE_STATE_KEY_STORE_LINKS) &&
	    !nemo_config_is_set (group, NEMO_ARCHIVE_STATE_KEY_FOLLOW_LINKS)) {
		return;
	}

	was = arc_link_choice_from_boxes (
		nemo_config_get_boolean (group, NEMO_ARCHIVE_STATE_KEY_STORE_LINKS),
		nemo_config_get_boolean (group, NEMO_ARCHIVE_STATE_KEY_FOLLOW_LINKS));

	nemo_config_set_enum (group, NEMO_ARCHIVE_STATE_KEY_SYMLINKS, was);
	nemo_config_reset (group, NEMO_ARCHIVE_STATE_KEY_STORE_LINKS);
	nemo_config_reset (group, NEMO_ARCHIVE_STATE_KEY_FOLLOW_LINKS);
}

void
nemo_archive_link_options_load (ArcLinkOptions *options)
{
	NemoConfigGroup *group;
	gint junctions;

	g_return_if_fail (options != NULL);

	arc_link_options_init (options);

	group = nemo_config_get_group (NEMO_ARCHIVE_COMMANDS_GROUP);
	if (group == NULL) {
		return;
	}

	map_old_boxes (group);

	arc_link_options_set_symlinks (options,
				       nemo_config_get_enum (group, NEMO_ARCHIVE_STATE_KEY_SYMLINKS));

	junctions = nemo_config_get_enum (group, NEMO_ARCHIVE_STATE_KEY_JUNCTIONS);
	if (junctions != NEMO_ARCHIVE_JUNCTIONS_LIKE_SYMLINKS) {
		arc_link_options_set_junctions (options, junctions);
	}

	options->follow_nested = nemo_config_get_boolean (group, NEMO_ARCHIVE_STATE_KEY_FOLLOW_NESTED);
}

/* follow_other is never saved, so every dialog starts with it off. */
void
nemo_archive_link_options_save (const ArcLinkOptions *options)
{
	NemoConfigGroup *group;

	g_return_if_fail (options != NULL);

	group = nemo_config_get_group (NEMO_ARCHIVE_COMMANDS_GROUP);
	if (group == NULL) {
		return;
	}

	nemo_config_set_enum (group, NEMO_ARCHIVE_STATE_KEY_SYMLINKS, options->symlinks);
	nemo_config_set_enum (group, NEMO_ARCHIVE_STATE_KEY_JUNCTIONS,
			      options->junctions_set ? (gint) options->junctions
						     : NEMO_ARCHIVE_JUNCTIONS_LIKE_SYMLINKS);
	nemo_config_set_boolean (group, NEMO_ARCHIVE_STATE_KEY_FOLLOW_NESTED, options->follow_nested);
}

static GFileEnumerator *
walk_children (gpointer              data,
	       GFile                *dir,
	       const char           *attributes,
	       GFileQueryInfoFlags   flags,
	       GCancellable         *cancellable,
	       GError              **error)
{
	(void) data;
	return nemo_enumerate_children (dir, attributes, flags, cancellable, error);
}

static gboolean
leaves_for_share (gpointer    data,
		  const char *folder,
		  const char *path)
{
	(void) data;
	return nemo_share_link_leaves_for_a_share (folder, path);
}

void
nemo_archive_host_init (ArcHost *host)
{
	g_return_if_fail (host != NULL);

	memset (host, 0, sizeof *host);
	host->walk.children = walk_children;
	host->shares.leaves_for = leaves_for_share;
}
