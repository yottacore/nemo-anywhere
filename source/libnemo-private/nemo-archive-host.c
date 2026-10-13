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
#include "nemo-archive.h"
#include "nemo-archive-commands.h"
#include "nemo-command-template.h"
#include "nemo-config.h"
#include "nemo-dir-enum.h"
#include "nemo-global-preferences.h"
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

/* The defaults are the settings table's, read back with nothing set. The
   settings test holds the two to each other. */
void
nemo_archive_choices_init (NemoArchiveChoices *choices)
{
	g_return_if_fail (choices != NULL);

	memset (choices, 0, sizeof *choices);
	choices->level = NEMO_ARCHIVE_LEVEL_DEFAULT;
	choices->split_size = g_strdup (NEMO_ARCHIVE_STATE_DEFAULT_SPLIT_SIZE);
	arc_link_options_init (&choices->links);
	choices->recovery = TRUE;
}

void
nemo_archive_choices_clear (NemoArchiveChoices *choices)
{
	g_return_if_fail (choices != NULL);

	g_clear_pointer (&choices->split_size, g_free);
}

void
nemo_archive_choices_copy (NemoArchiveChoices       *to,
			   const NemoArchiveChoices *from)
{
	char *split_size;

	g_return_if_fail (to != NULL && from != NULL);

	split_size = g_strdup (from->split_size);
	nemo_archive_choices_clear (to);
	*to = *from;
	to->split_size = split_size;
}

void
nemo_archive_choices_load (NemoArchiveChoices *choices)
{
	NemoConfigGroup *group;
	char *split_size;

	g_return_if_fail (choices != NULL);

	nemo_archive_choices_init (choices);
	nemo_archive_link_options_load (&choices->links);

	group = nemo_config_get_group (NEMO_ARCHIVE_COMMANDS_GROUP);
	if (group == NULL) {
		return;
	}

	choices->level = nemo_config_get_int (group, NEMO_ARCHIVE_STATE_KEY_LEVEL);
	choices->encrypt_names = nemo_config_get_boolean (group, NEMO_ARCHIVE_STATE_KEY_ENCRYPT_NAMES);
	choices->split = nemo_config_get_boolean (group, NEMO_ARCHIVE_STATE_KEY_SPLIT);
	choices->solid = nemo_config_get_boolean (group, NEMO_ARCHIVE_STATE_KEY_SOLID);
	choices->dedupe = nemo_config_get_boolean (group, NEMO_ARCHIVE_STATE_KEY_DEDUPE);
	choices->recovery = nemo_config_get_boolean (group, NEMO_ARCHIVE_STATE_KEY_RECOVERY);
	choices->lock = nemo_config_get_boolean (group, NEMO_ARCHIVE_STATE_KEY_LOCK);

	split_size = nemo_config_get_string (group, NEMO_ARCHIVE_STATE_KEY_SPLIT_SIZE);
	if (split_size != NULL && split_size[0] != '\0') {
		g_free (choices->split_size);
		choices->split_size = split_size;
	} else {
		g_free (split_size);
	}
}

/* A preset is kept as its size alone, so it still reads in another
   language. */
void
nemo_archive_choices_save (const NemoArchiveChoices *choices)
{
	NemoConfigGroup *group;
	const char *split_size;
	int preset;

	g_return_if_fail (choices != NULL);

	group = nemo_config_get_group (NEMO_ARCHIVE_COMMANDS_GROUP);
	if (group == NULL) {
		return;
	}

	nemo_config_set_int (group, NEMO_ARCHIVE_STATE_KEY_LEVEL, choices->level);
	nemo_config_set_boolean (group, NEMO_ARCHIVE_STATE_KEY_ENCRYPT_NAMES, choices->encrypt_names);
	nemo_config_set_boolean (group, NEMO_ARCHIVE_STATE_KEY_SPLIT, choices->split);
	nemo_config_set_boolean (group, NEMO_ARCHIVE_STATE_KEY_SOLID, choices->solid);
	nemo_config_set_boolean (group, NEMO_ARCHIVE_STATE_KEY_DEDUPE, choices->dedupe);
	nemo_archive_link_options_save (&choices->links);
	nemo_config_set_boolean (group, NEMO_ARCHIVE_STATE_KEY_RECOVERY, choices->recovery);
	nemo_config_set_boolean (group, NEMO_ARCHIVE_STATE_KEY_LOCK, choices->lock);

	split_size = choices->split_size != NULL ? choices->split_size : "";
	preset = nemo_archive_volume_preset_find (split_size);
	if (preset >= 0) {
		nemo_archive_volume_preset ((guint) preset, &split_size);
	}
	nemo_config_set_string (group, NEMO_ARCHIVE_STATE_KEY_SPLIT_SIZE, split_size);
}

static gboolean
same_size (const char *a,
	   const char *b)
{
	int preset = nemo_archive_volume_preset_find (a);
	char *left, *right;
	gboolean same;

	if (preset >= 0) {
		return preset == nemo_archive_volume_preset_find (b);
	}

	left = g_strstrip (g_strdup (a != NULL ? a : ""));
	right = g_strstrip (g_strdup (b != NULL ? b : ""));
	same = strcmp (left, right) == 0;
	g_free (left);
	g_free (right);

	return same;
}

/* Other filesystems counts, though it's never remembered, since it's a
   choice under Options all the same. */
gboolean
nemo_archive_choices_at_default (const NemoArchiveChoices *choices)
{
	NemoArchiveChoices fresh;
	gboolean same;

	g_return_val_if_fail (choices != NULL, TRUE);

	nemo_archive_choices_init (&fresh);
	same = choices->level == fresh.level &&
	       !choices->encrypt_names == !fresh.encrypt_names &&
	       !choices->split == !fresh.split &&
	       same_size (choices->split_size, fresh.split_size) &&
	       !choices->solid == !fresh.solid &&
	       !choices->dedupe == !fresh.dedupe &&
	       choices->links.symlinks == fresh.links.symlinks &&
	       !choices->links.junctions_set == !fresh.links.junctions_set &&
	       !choices->links.follow_nested == !fresh.links.follow_nested &&
	       !choices->links.follow_other == !fresh.links.follow_other &&
	       !choices->recovery == !fresh.recovery &&
	       !choices->lock == !fresh.lock;
	nemo_archive_choices_clear (&fresh);

	return same;
}

void
nemo_archive_choices_forget (void)
{
	static const char *const keys[] = {
		NEMO_ARCHIVE_STATE_KEY_LEVEL,
		NEMO_ARCHIVE_STATE_KEY_ENCRYPT_NAMES,
		NEMO_ARCHIVE_STATE_KEY_SPLIT,
		NEMO_ARCHIVE_STATE_KEY_SPLIT_SIZE,
		NEMO_ARCHIVE_STATE_KEY_SOLID,
		NEMO_ARCHIVE_STATE_KEY_DEDUPE,
		NEMO_ARCHIVE_STATE_KEY_SYMLINKS,
		NEMO_ARCHIVE_STATE_KEY_JUNCTIONS,
		NEMO_ARCHIVE_STATE_KEY_FOLLOW_NESTED,
		NEMO_ARCHIVE_STATE_KEY_STORE_LINKS,
		NEMO_ARCHIVE_STATE_KEY_FOLLOW_LINKS,
		NEMO_ARCHIVE_STATE_KEY_RECOVERY,
		NEMO_ARCHIVE_STATE_KEY_LOCK,
	};
	NemoConfigGroup *group = nemo_config_get_group (NEMO_ARCHIVE_COMMANDS_GROUP);
	guint i;

	if (group == NULL) {
		return;
	}

	for (i = 0; i < G_N_ELEMENTS (keys); i++) {
		nemo_config_reset (group, keys[i]);
	}
}

void
nemo_archive_choices_shown (const NemoArchiveChoices *chosen,
			    guint                     caps,
			    gboolean                  has_password,
			    NemoArchiveChoices       *shown)
{
	g_return_if_fail (chosen != NULL && shown != NULL);

	nemo_archive_choices_copy (shown, chosen);

	shown->encrypt_names = chosen->encrypt_names && has_password &&
			       (caps & NEMO_ARCHIVE_CAP_PASSWORD) != 0 &&
			       (caps & NEMO_ARCHIVE_CAP_ENCRYPT_NAMES) != 0;
	shown->split = chosen->split && (caps & NEMO_ARCHIVE_CAP_SPLIT) != 0;
	shown->solid = chosen->solid && (caps & NEMO_ARCHIVE_CAP_SOLID) != 0;
	shown->dedupe = chosen->dedupe && (caps & NEMO_ARCHIVE_CAP_DEDUPE) != 0;
	shown->recovery = chosen->recovery && (caps & NEMO_ARCHIVE_CAP_RECOVERY) != 0;
	shown->lock = chosen->lock && (caps & NEMO_ARCHIVE_CAP_LOCK) != 0;
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

/* The core's tokens are the template's, field for field. */
static NemoCommandToken *
template_tokens (const ArcCommandToken *tokens)
{
	NemoCommandToken *out;
	guint count = 0, i;

	while (tokens != NULL && tokens[count].name != NULL) {
		count++;
	}
	out = g_new0 (NemoCommandToken, count + 1);
	for (i = 0; i < count; i++) {
		out[i].name = tokens[i].name;
		out[i].values = tokens[i].values;
		out[i].warn_if_dropped = tokens[i].warn_if_dropped;
	}
	return out;
}

static char **
expand_line (gpointer               data,
	     const char            *text,
	     const ArcCommandToken *tokens,
	     GError               **error)
{
	NemoCommandToken *marks = template_tokens (tokens);
	char **argv;

	(void) data;
	argv = nemo_command_template_expand (text, marks, error);
	g_free (marks);
	return argv;
}

static void
warn_unused (gpointer               data,
	     ArcLine                line,
	     const char            *text,
	     const ArcCommandToken *tokens)
{
	NemoCommandToken *marks = template_tokens (tokens);

	(void) data;
	nemo_command_template_warn_unused (arc_line_name (line), text, marks);
	g_free (marks);
}

void
nemo_archive_host_init (ArcHost *host)
{
	g_return_if_fail (host != NULL);

	memset (host, 0, sizeof *host);
	host->walk.children = walk_children;
	host->shares.leaves_for = leaves_for_share;
	host->commands.expand = expand_line;
	host->commands.warn_unused = warn_unused;
}

/* The built-in line where one was emptied, or before the settings are up. */
void
nemo_archive_settings_load (ArcSettings *settings)
{
	static const struct {
		ArcLine     line;
		const char *fallback;
	} lines[] = {
		{ ARC_LINE_CREATE_7Z,   NEMO_ARCHIVE_COMMAND_7Z_DEFAULT },
		{ ARC_LINE_CREATE_RAR,  NEMO_ARCHIVE_COMMAND_RAR_DEFAULT },
		{ ARC_LINE_EXTRACT_7Z,  NEMO_EXTRACT_COMMAND_7Z_DEFAULT },
		{ ARC_LINE_EXTRACT_RAR, NEMO_EXTRACT_COMMAND_RAR_DEFAULT },
	};
	guint i;

	g_return_if_fail (settings != NULL);

	for (i = 0; i < G_N_ELEMENTS (lines); i++) {
		g_free (settings->lines[lines[i].line]);
		settings->lines[lines[i].line] =
			nemo_command_template_from_config (NEMO_ARCHIVE_COMMANDS_GROUP,
							   arc_line_name (lines[i].line),
							   lines[i].fallback);
	}
	settings->threads = (guint) nemo_global_preferences_get_cpu_thread_count ();
}
