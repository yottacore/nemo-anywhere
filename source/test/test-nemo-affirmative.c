/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */

/* test-nemo-affirmative.c - the affirmative switch is off everywhere.

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

/* The tunable is off, so under every model every place goes the off way and
 * the wording is the "Switch off" column, the file operation messages too. Each place is asked about at least
 * once in the source, so none of the design doc's list is left without its
 * conditional. Takes the source root as its argument. */

#include <config.h>

#include <stdlib.h>
#include <string.h>
#include <glib.h>

#include <libnemo-private/nemo-affirmative.h>
#include <libnemo-private/nemo-global-preferences.h>

#include "test-scratch.h"
#include "test-check.h"

static const char * const models[] = { "nemo-anywhere", "nemo-anywhere-v1", "nemo-classic" };

static const char * const off_words[NEMO_AFFIRM_N_WORDS] = {
	[NEMO_AFFIRM_WORD_SELECT_ALL]     = "Select _all",
	[NEMO_AFFIRM_WORD_SELECT_NONE]    = "Select _none",
	[NEMO_AFFIRM_WORD_INVERT]         = "_Invert selection",
};

/* The "selected items" row in the file operation messages, for 1 and for 3. */
static const char * const off_counted[NEMO_AFFIRM_N_COUNTED][2] = {
	[NEMO_AFFIRM_COUNTED_TRASH] = {
		"unused %'d",
		"Are you sure you want to move the %'d selected items to the trash?" },
	[NEMO_AFFIRM_COUNTED_DELETE_FROM_TRASH] = {
		"Are you sure you want to permanently delete the %'d selected item from the trash?",
		"Are you sure you want to permanently delete the %'d selected items from the trash?" },
	[NEMO_AFFIRM_COUNTED_DELETE] = {
		"Are you sure you want to permanently delete the %'d selected item?",
		"Are you sure you want to permanently delete the %'d selected items?" },
	[NEMO_AFFIRM_COUNTED_CUT] = {
		"The %'d selected item will be moved if you select the Paste command",
		"The %'d selected items%s will be moved if you select the Paste command" },
	[NEMO_AFFIRM_COUNTED_COPY] = {
		"The %'d selected item will be copied if you select the Paste command",
		"The %'d selected items%s will be copied if you select the Paste command" },
};

static void
check_off (const char *model)
{
	int i;

	nemo_config_set_string (nemo_preferences, NEMO_PREFERENCES_SELECTION_MODEL, model);
	check (!nemo_affirmative_switch ());

	for (i = 0; i < NEMO_AFFIRM_N_PLACES; i++) {
		if (nemo_affirmative_at ((NemoAffirmPlace) i)) {
			g_printerr ("FAIL %s goes the on way under %s\n",
				    nemo_affirmative_place_name ((NemoAffirmPlace) i), model);
			failures++;
		}
	}

	for (i = 0; i < NEMO_AFFIRM_N_WORDS; i++) {
		const char *word = nemo_affirmative_word ((NemoAffirmWord) i);

		if (g_strcmp0 (word, off_words[i]) != 0) {
			g_printerr ("FAIL word %d under %s is \"%s\", wanted \"%s\"\n", i, model, word, off_words[i]);
			failures++;
		}
	}

	for (i = 0; i < NEMO_AFFIRM_N_COUNTED; i++) {
		const char *one = nemo_affirmative_counted ((NemoAffirmCounted) i, 1);
		const char *many = nemo_affirmative_counted ((NemoAffirmCounted) i, 3);

		if (g_strcmp0 (one, off_counted[i][0]) != 0 || g_strcmp0 (many, off_counted[i][1]) != 0) {
			g_printerr ("FAIL message %d under %s is \"%s\" / \"%s\"\n", i, model, one, many);
			failures++;
		}
	}
}

static void
read_sources (const char *dir, GString *all)
{
	GDir *d = g_dir_open (dir, 0, NULL);
	const char *name;

	if (d == NULL) {
		g_printerr ("FAIL can't read %s\n", dir);
		failures++;
		return;
	}
	while ((name = g_dir_read_name (d)) != NULL) {
		char *path, *text;

		if (!g_str_has_suffix (name, ".c") || g_str_has_prefix (name, "nemo-affirmative.")) {
			continue;
		}
		path = g_build_filename (dir, name, NULL);
		if (g_file_get_contents (path, &text, NULL, NULL)) {
			g_string_append (all, text);
			g_free (text);
		}
		g_free (path);
	}
	g_dir_close (d);
}

static void
check_places_used (const char *source_root)
{
	GString *all = g_string_new (NULL);
	char *dir;
	int i;

	dir = g_build_filename (source_root, "src", NULL);
	read_sources (dir, all);
	g_free (dir);
	dir = g_build_filename (source_root, "libnemo-private", NULL);
	read_sources (dir, all);
	g_free (dir);

	for (i = 0; i < NEMO_AFFIRM_N_PLACES; i++) {
		char *call = g_strdup_printf ("nemo_affirmative_at (%s)",
					      nemo_affirmative_place_name ((NemoAffirmPlace) i));
		if (strstr (all->str, call) == NULL) {
			g_printerr ("FAIL no place asks %s\n", call);
			failures++;
		}
		g_free (call);
	}

	/* The counted messages come from the wording table, not their own copies. */
	if (strstr (all->str, "%'d selected item") != NULL) {
		g_printerr ("FAIL a file operation message counts selected items outside the wording table\n");
		failures++;
	}
	g_string_free (all, TRUE);
}

int
main (int argc, char **argv)
{
	char *tmp;
	guint i;

	if (argc != 2) {
		g_printerr ("usage: %s <source root>\n", argv[0]);
		return EXIT_FAILURE;
	}

	tmp = test_scratch_config_home ("nemo-affirmative-test-XXXXXX");
	nemo_global_preferences_init ();

	check (NEMO_AFFIRMATIVE_SWITCH == 0);

	/* The default model first, then each one by name. */
	nemo_config_reset (nemo_preferences, NEMO_PREFERENCES_SELECTION_MODEL);
	check (!nemo_affirmative_switch ());
	for (i = 0; i < G_N_ELEMENTS (models); i++) {
		check_off (models[i]);
	}
	nemo_config_reset (nemo_preferences, NEMO_PREFERENCES_SELECTION_MODEL);

	check_places_used (argv[1]);
	g_free (tmp);

	if (failures == 0) {
		g_print ("nemo-affirmative: all checks passed\n");
	}

	return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
