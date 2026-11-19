/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */

/* nemo-affirmative.c - the affirmative switch of the Nemo Anywhere model.

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

#include <config.h>

#include "nemo-affirmative.h"

#include <glib/gi18n.h>

#include "nemo-global-preferences.h"

/* Switch on is NULL until 2026102019174514 fills it in. */
static const struct {
	const char *off;
	const char *on;
} words[NEMO_AFFIRM_N_WORDS] = {
	[NEMO_AFFIRM_WORD_SELECT_ALL]     = { N_("Select _all"),       NULL },
	[NEMO_AFFIRM_WORD_SELECT_NONE]    = { N_("Select _none"),      NULL },
	[NEMO_AFFIRM_WORD_INVERT]         = { N_("_Invert selection"), NULL },
};

static const char * const place_names[NEMO_AFFIRM_N_PLACES] = {
	[NEMO_AFFIRM_SPACE]        = "NEMO_AFFIRM_SPACE",
	[NEMO_AFFIRM_SELECT_ALL]   = "NEMO_AFFIRM_SELECT_ALL",
	[NEMO_AFFIRM_INVERT]       = "NEMO_AFFIRM_INVERT",
	[NEMO_AFFIRM_MENU_ENTRIES] = "NEMO_AFFIRM_MENU_ENTRIES",
	[NEMO_AFFIRM_COLUMN_DRAG]  = "NEMO_AFFIRM_COLUMN_DRAG",
	[NEMO_AFFIRM_ACTS_ON]      = "NEMO_AFFIRM_ACTS_ON",
	[NEMO_AFFIRM_LISTS_DIFFER] = "NEMO_AFFIRM_LISTS_DIFFER",
	[NEMO_AFFIRM_DRAG_PUNT]    = "NEMO_AFFIRM_DRAG_PUNT",
};

gboolean
nemo_affirmative_switch (void)
{
#if NEMO_AFFIRMATIVE_SWITCH
	return nemo_global_preferences_get_selection_model () == NEMO_SELECTION_MODEL_ANYWHERE;
#else
	return FALSE;
#endif
}

gboolean
nemo_affirmative_at (NemoAffirmPlace place)
{
	g_return_val_if_fail (place < NEMO_AFFIRM_N_PLACES, FALSE);

	return nemo_affirmative_switch ();
}

/* Returns: (transfer none): static, or gettext's */
const char *
nemo_affirmative_word (NemoAffirmWord word)
{
	g_return_val_if_fail (word < NEMO_AFFIRM_N_WORDS, "");

	if (nemo_affirmative_switch () && words[word].on != NULL) {
		return _(words[word].on);
	}
	return _(words[word].off);
}

/* The "checked items" sentences go here (2026102019174514). */
static const char *
counted_checked (G_GNUC_UNUSED NemoAffirmCounted line, G_GNUC_UNUSED gulong n)
{
	return NULL;
}

/* Returns: (transfer none): static, or gettext's */
const char *
nemo_affirmative_counted (NemoAffirmCounted line, gulong n)
{
	g_return_val_if_fail (line < NEMO_AFFIRM_N_COUNTED, "");

	if (nemo_affirmative_switch ()) {
		const char *checked = counted_checked (line, n);

		if (checked != NULL) {
			return checked;
		}
	}

	switch (line) {
	case NEMO_AFFIRM_COUNTED_TRASH:
		/* translators: the singular form here can be skipped. */
		return ngettext ("unused %'d",
				 "Are you sure you want to move "
				 "the %'d selected items to the trash?",
				 n);
	case NEMO_AFFIRM_COUNTED_DELETE_FROM_TRASH:
		return ngettext ("Are you sure you want to permanently delete "
				 "the %'d selected item from the trash?",
				 "Are you sure you want to permanently delete "
				 "the %'d selected items from the trash?",
				 n);
	case NEMO_AFFIRM_COUNTED_DELETE:
		return ngettext ("Are you sure you want to permanently delete "
				 "the %'d selected item?",
				 "Are you sure you want to permanently delete "
				 "the %'d selected items?",
				 n);
	case NEMO_AFFIRM_COUNTED_CUT:
		/* translators: this is preceded with a string of form 'N selected items' (N more than 1) */
		return ngettext ("The %'d selected item will be moved "
				 "if you select the Paste command",
				 "The %'d selected items%s will be moved "
				 "if you select the Paste command",
				 n);
	case NEMO_AFFIRM_COUNTED_COPY:
		/* translators: this is preceded with a string of form 'N selected items' (N more than 1) */
		return ngettext ("The %'d selected item will be copied "
				 "if you select the Paste command",
				 "The %'d selected items%s will be copied "
				 "if you select the Paste command",
				 n);
	case NEMO_AFFIRM_N_COUNTED:
		break;
	}
	return "";
}

/* Returns: (transfer none): static */
const char *
nemo_affirmative_place_name (NemoAffirmPlace place)
{
	g_return_val_if_fail (place < NEMO_AFFIRM_N_PLACES, "");

	return place_names[place];
}
