/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */

/* nemo-affirmative.h - the affirmative switch of the Nemo Anywhere model.

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

/* The switch, the places it plugs into, and the wording that changes with
 * it. Nothing is built behind it yet, so every place does what it does with
 * the switch off either way. See design_docs/20261020-103804_behavior-models.md,
 * "The affirmative switch".
 */

#ifndef NEMO_AFFIRMATIVE_H
#define NEMO_AFFIRMATIVE_H

#include <glib.h>

/* The code-level tunable. The setting's checkbox, once there is one, reads it
   as its default. Stays 0 until the affirmative half is built (2026102019174514). */
#define NEMO_AFFIRMATIVE_SWITCH 0

/* The design doc's "Off removes" list, one per place. */
typedef enum {
	NEMO_AFFIRM_SPACE,		/* Space checkmarks rather than previews */
	NEMO_AFFIRM_SELECT_ALL,		/* Ctrl+A checkmarks as well as selects */
	NEMO_AFFIRM_INVERT,		/* Ctrl+I inverts the checkmarks */
	NEMO_AFFIRM_MENU_ENTRIES,	/* the 4 checkmark entries and Uncheck all */
	NEMO_AFFIRM_COLUMN_DRAG,	/* a band from the checkmark column checks what it touched */
	NEMO_AFFIRM_ACTS_ON,		/* a command takes the checked group over the selection */
	NEMO_AFFIRM_LISTS_DIFFER,	/* the dialog alert and the status bar note */
	NEMO_AFFIRM_DRAG_PUNT,		/* no drag from an unchecked item while others are checked */
	NEMO_AFFIRM_N_PLACES
} NemoAffirmPlace;

/* The wording table's rows. */
typedef enum {
	NEMO_AFFIRM_WORD_SELECT_ALL,
	NEMO_AFFIRM_WORD_SELECT_NONE,
	NEMO_AFFIRM_WORD_INVERT,
	NEMO_AFFIRM_WORD_SELECTED_ITEMS,
	NEMO_AFFIRM_N_WORDS
} NemoAffirmWord;

/* On only under the Nemo Anywhere model, and only with the tunable on. */
gboolean     nemo_affirmative_switch (void);

/* Whether `place` takes its affirmative way. Every place asks through here,
   so they can be found and turned on one at a time. */
gboolean     nemo_affirmative_at     (NemoAffirmPlace place);

/* The row's label for the switch as it is now, translated. A row with no
   "Switch on" label yet gives its "Switch off" one. */
const char  *nemo_affirmative_word   (NemoAffirmWord word);

/* The place's name, as used in the source. For tests. */
const char  *nemo_affirmative_place_name (NemoAffirmPlace place);

#endif /* NEMO_AFFIRMATIVE_H */
