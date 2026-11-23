/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */

/* test-nemo-paste-target.c - where a paste goes, per row of the design doc's table and per model.

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

/* Each case is a list view selection as tree paths, with the rows that are
 * expanded folders, and the folder row each model sends a paste to, "" being
 * the viewed folder. Then where Create link, Compress and Extract here put
 * what they make, what Ctrl+Shift+V is, and the setting by name. See
 * design_docs/20261020-103804_behavior-models.md, "The 3 target models". */

#include <config.h>

#include <stdlib.h>
#include <string.h>
#include <gtk/gtk.h>

#include <libnemo-private/nemo-paste-target.h>
#include <libnemo-private/nemo-global-preferences.h>

#include "test-scratch.h"
#include "test-check.h"

typedef struct {
	const char *row;
	const char *selected;   /* space separated paths */
	const char *expanded;   /* the selected rows that are expanded folders */
	const char *anywhere;
	const char *classic;
	const char *dolphin;
	const char *beside;     /* Create link and the rest, under Nemo Anywhere and macOS/Dolphin */
} Case;

/* The tree: 0 and 1 are folders at the top. 0 has 0:0 to 0:3 in it, 0:3 a
   folder with more below. 2 and 3 are files at the top. */
static const Case cases[] = {
	{ "Nothing",                         "",              "",        "",    "",    "", ""    },
	{ "One expanded folder, alone",      "0:3",           "0:3",     "0:3", "0:3", "", "0"   },
	{ "One expanded folder at the top",  "0",             "0",       "0",   "0",   "", ""    },
	{ "One other object, alone",         "0:1",           "",        "0",   "0",   "", "0"   },
	{ "One collapsed folder, alone",     "0:2",           "",        "0",   "0",   "", "0"   },
	{ "One object at the top, alone",    "2",             "",        "",    "",    "", ""    },
	{ "Several objects in one folder",   "0:0 0:1 0:2",   "",        "0",   "0",   "", "0"   },
	{ "Several objects at the top",      "1 2 3",         "",        "",    "",    "", ""    },
	{ "Objects in more than one folder", "0:1 0:3:0",     "",        "0",   "",    "", "0"   },
	{ "Objects deeper, in 2 folders",    "0:3:1:0 0:3:2", "",        "0:3", "",    "", "0:3" },
	{ "Objects in 2 top folders",        "0:1 1:0",       "",        "",    "",    "", ""    },
	{ "Objects at the top and below",    "2 0:1",         "",        "",    "",    "", ""    },
	{ "Expanded folder with others",     "0:3 0:1",       "0:3",     "0",   "0",   "", "0"   },
	{ "Expanded folder with its child",  "0:3 0:3:0",     "0:3",     "0",   "",    "", "0"   },
	{ "The \"(Empty)\" row of a folder", "0:3:5",         "",        "0:3", "0:3", "", "0:3" },
};

static gboolean
expanded_cb (GtkTreePath *path, gpointer user_data)
{
	char **expanded = user_data;
	char *s = gtk_tree_path_to_string (path);
	gboolean found = g_strv_contains ((const char * const *) expanded, s);

	g_free (s);
	return found;
}

typedef GtkTreePath *(* Rule) (NemoPasteTarget, GList *, NemoPasteTargetExpandedFunc, gpointer);

static void
check_case (const Case *c, Rule rule, NemoPasteTarget model, const char *want)
{
	char **selected = g_strsplit (c->selected, " ", -1);
	char **expanded = g_strsplit (c->expanded, " ", -1);
	GList *paths = NULL;
	GtkTreePath *got;
	char *got_s;
	int i;

	for (i = 0; selected[i] != NULL; i++) {
		if (*selected[i] != '\0') {
			paths = g_list_append (paths, gtk_tree_path_new_from_string (selected[i]));
		}
	}

	got = rule (model, paths, expanded_cb, expanded);
	got_s = got != NULL ? gtk_tree_path_to_string (got) : g_strdup ("");
	if (strcmp (got_s, want) != 0) {
		g_printerr ("FAIL %s, model %d%s: got \"%s\", want \"%s\"\n", c->row, model,
			    rule == nemo_paste_target_row ? "" : ", beside", got_s, want);
		failures++;
	}

	g_free (got_s);
	if (got != NULL) {
		gtk_tree_path_free (got);
	}
	g_list_free_full (paths, (GDestroyNotify) gtk_tree_path_free);
	g_strfreev (selected);
	g_strfreev (expanded);
}

/* Ctrl+Shift+V: the viewed folder, nothing, and Paste into folder. */
static void
check_key (void)
{
	const NemoPasteKey *key;

	key = nemo_paste_target_key (NEMO_PASTE_TARGET_ANYWHERE);
	check (key != NULL && strcmp (key->title, "Paste into viewed folder") == 0);
	check (nemo_paste_target_key (NEMO_PASTE_TARGET_CLASSIC) == NULL);
	key = nemo_paste_target_key (NEMO_PASTE_TARGET_VIEWED_FOLDER);
	check (key != NULL && strcmp (key->title, "Paste into folder") == 0);
}

static void
check_setting (void)
{
	nemo_config_reset (nemo_preferences, NEMO_PREFERENCES_PASTE_TARGET);
	check (nemo_global_preferences_get_paste_target () == NEMO_PASTE_TARGET_ANYWHERE);
	nemo_config_set_string (nemo_preferences, NEMO_PREFERENCES_PASTE_TARGET, "nemo-classic");
	check (nemo_global_preferences_get_paste_target () == NEMO_PASTE_TARGET_CLASSIC);
	nemo_config_set_string (nemo_preferences, NEMO_PREFERENCES_PASTE_TARGET, "macos-dolphin");
	check (nemo_global_preferences_get_paste_target () == NEMO_PASTE_TARGET_VIEWED_FOLDER);
	nemo_config_set_string (nemo_preferences, NEMO_PREFERENCES_PASTE_TARGET, "nemo-anywhere");
	check (nemo_global_preferences_get_paste_target () == NEMO_PASTE_TARGET_ANYWHERE);
	nemo_config_reset (nemo_preferences, NEMO_PREFERENCES_PASTE_TARGET);
}

int
main (void)
{
	char *tmp;
	guint i;

	tmp = test_scratch_config_home ("nemo-paste-target-test-XXXXXX");
	nemo_global_preferences_init ();

	for (i = 0; i < G_N_ELEMENTS (cases); i++) {
		check_case (&cases[i], nemo_paste_target_row, NEMO_PASTE_TARGET_ANYWHERE, cases[i].anywhere);
		check_case (&cases[i], nemo_paste_target_row, NEMO_PASTE_TARGET_CLASSIC, cases[i].classic);
		check_case (&cases[i], nemo_paste_target_row, NEMO_PASTE_TARGET_VIEWED_FOLDER, cases[i].dolphin);
		/* Under Nemo Classic they go where a paste does. */
		check_case (&cases[i], nemo_paste_target_beside_row, NEMO_PASTE_TARGET_ANYWHERE, cases[i].beside);
		check_case (&cases[i], nemo_paste_target_beside_row, NEMO_PASTE_TARGET_CLASSIC, cases[i].classic);
		check_case (&cases[i], nemo_paste_target_beside_row, NEMO_PASTE_TARGET_VIEWED_FOLDER, cases[i].beside);
	}
	check_key ();
	check_setting ();
	g_free (tmp);

	if (failures == 0) {
		g_print ("nemo-paste-target: all checks passed\n");
	}

	return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
