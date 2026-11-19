/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */

/* test-nemo-settings-search.c - how Settings search matches and ranks.

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

/* The design doc's own examples, the order of the 3 groups, the order inside
 * each, and the markup a match is shown with. */

#include <config.h>

#include <string.h>

#include "nemo-settings-search.h"

#include "test-check.h"

static char *
marked (const char *text, GArray *marks)
{
	GString *out = g_string_new (NULL);
	const char *p;
	guint n = 0, next = 0;
	gboolean in = FALSE;

	for (p = text; *p != '\0'; p = g_utf8_next_char (p), n++) {
		gboolean m = next < marks->len && g_array_index (marks, guint, next) == n;

		if (m) {
			next++;
		}
		if (m && !in) {
			g_string_append_c (out, '[');
		} else if (!m && in) {
			g_string_append_c (out, ']');
		}
		in = m;
		g_string_append_len (out, p, g_utf8_next_char (p) - p);
	}
	if (in) {
		g_string_append_c (out, ']');
	}

	return g_string_free (out, FALSE);
}

static gboolean
literal_marks (const char *text, const char *query, const char *expected)
{
	GArray *marks = g_array_new (FALSE, FALSE, sizeof (guint));
	gboolean ok = nemo_settings_search_literal (text, query, NULL, marks);
	char *got = marked (text, marks);

	if (ok) {
		ok = g_strcmp0 (got, expected) == 0;
	}
	if (!ok) {
		g_printerr ("literal %s in %s: got %s\n", query, text, got);
	}
	g_free (got);
	g_array_unref (marks);

	return ok;
}

static gboolean
fuzzy_marks (const char *text, const char *query, const char *expected)
{
	GArray *marks = g_array_new (FALSE, FALSE, sizeof (guint));
	gboolean ok = nemo_settings_search_fuzzy (text, query, NULL, marks);
	char *got = marked (text, marks);

	if (ok) {
		ok = g_strcmp0 (got, expected) == 0;
	}
	if (!ok) {
		g_printerr ("fuzzy %s in %s: got %s\n", query, text, got);
	}
	g_free (got);
	g_array_unref (marks);

	return ok;
}

static void
test_design_examples (void)
{
	check (literal_marks ("Open in another universe", "open", "[Open] in another universe"));
	check (literal_marks ("Open every new folder underwater", "new", "Open every [new] folder underwater"));
	check (!nemo_settings_search_literal ("Actually, NOBODY dies", "Andies", NULL, NULL));
	check (fuzzy_marks ("Actually, NOBODY dies", "Andies", "[A]ctually, [N]OBODY [dies]"));
}

static void
test_literal (void)
{
	int whole = 0, start = 0, word = 0, middle = 0;

	check (nemo_settings_search_literal ("Trash", "trash", &whole, NULL));
	check (nemo_settings_search_literal ("Trash can", "trash", &start, NULL));
	check (nemo_settings_search_literal ("Empty the trash", "trash", &word, NULL));
	check (nemo_settings_search_literal ("Untrashed", "trash", &middle, NULL));
	check (whole < start && start < word && word < middle);

	/* The word start wins over an earlier hit inside a word. */
	check (literal_marks ("Undo the do", "do", "Undo the [do]"));
	/* A sharp s doesn't fold to "ss", and an O with umlaut folds to its lowercase. */
	check (literal_marks ("Gr\xc3\xb6\xc3\x9f" "e der Symbole", "GR\xc3\x96SSE", "") == FALSE);
	check (literal_marks ("Gr\xc3\xb6\xc3\x9f" "e der Symbole", "GR\xc3\x96", "[Gr\xc3\xb6]\xc3\x9f" "e der Symbole"));
	check (literal_marks ("Show hidden files", "  hidden ", "Show [hidden] files"));

	check (!nemo_settings_search_literal ("Anything", "", NULL, NULL));
	check (!nemo_settings_search_literal ("Anything", "   ", NULL, NULL));
	check (!nemo_settings_search_literal ("Any", "Anything", NULL, NULL));
}

static void
test_fuzzy (void)
{
	int tight = 0, loose = 0;

	/* Spaces typed don't count as letters. */
	check (fuzzy_marks ("Show hidden files", "shi fi", "[S]how [hi]dden [fi]les"));
	check (!nemo_settings_search_fuzzy ("Show hidden files", "sh", NULL, NULL));
	check (!nemo_settings_search_fuzzy ("Show hidden files", "fsh", NULL, NULL));

	/* A word's start is worth a few skipped characters. */
	check (fuzzy_marks ("Undo deleted items", "del", "Undo [del]eted items"));

	/* The tightest place wins, even when it isn't the first. */
	check (fuzzy_marks ("a x x x b a b c", "abc", "a x x x b [a] [b] [c]"));

	check (nemo_settings_search_fuzzy ("Grid spacing", "grsp", &tight, NULL));
	check (nemo_settings_search_fuzzy ("Grid big spacing", "grsp", &loose, NULL));
	check (tight < loose);

	/* 3 letters picked out of a sentence aren't a match. */
	check (!nemo_settings_search_fuzzy ("Visible action and extension entries can be configured in the Plugins tab",
					    "ves", NULL, NULL));
}

static GPtrArray *
entries_of (const char *const *labels)
{
	GPtrArray *entries = g_ptr_array_new_with_free_func ((GDestroyNotify) nemo_settings_entry_free);

	for (; *labels != NULL; labels++) {
		g_ptr_array_add (entries, nemo_settings_entry_new (*labels, "Tab", NULL));
	}

	return entries;
}

static const char *
hit_label (GPtrArray *entries, GPtrArray *hits, guint i)
{
	NemoSettingsHit *hit = hits->pdata[i];

	return ((NemoSettingsEntry *) entries->pdata[hit->index])->label;
}

static void
test_groups (void)
{
	const char *labels[] = {
		"Delete permanently",       /* fuzzy: d-e-l spread out */
		"Dark mode",                /* alias "theme" */
		"Theme",                    /* label */
		"Mid theme here",           /* label, a later word */
		"The meow mixer",           /* fuzzy t-h-e-m-e */
		"Nothing to see",
		NULL
	};
	GPtrArray *entries = entries_of (labels);
	GPtrArray *hits;
	NemoSettingsEntry *dark = entries->pdata[1];

	dark->aliases = g_new0 (char *, 3);
	dark->aliases[0] = g_strdup ("night");
	dark->aliases[1] = g_strdup ("theme");

	hits = nemo_settings_search_run (entries, "theme");
	check (hits->len == 4);
	if (hits->len == 4) {
		check (strcmp (hit_label (entries, hits, 0), "Theme") == 0);
		check (strcmp (hit_label (entries, hits, 1), "Mid theme here") == 0);
		check (strcmp (hit_label (entries, hits, 2), "Dark mode") == 0);
		check (strcmp (hit_label (entries, hits, 3), "The meow mixer") == 0);
		check (((NemoSettingsHit *) hits->pdata[0])->kind == NEMO_SETTINGS_MATCH_LABEL);
		check (((NemoSettingsHit *) hits->pdata[2])->kind == NEMO_SETTINGS_MATCH_ALIAS);
		check (strcmp (((NemoSettingsHit *) hits->pdata[2])->text, "theme") == 0);
		check (((NemoSettingsHit *) hits->pdata[3])->kind == NEMO_SETTINGS_MATCH_FUZZY);
	}
	g_ptr_array_free (hits, TRUE);

	/* An entry that matches as typed isn't listed again loosely. */
	hits = nemo_settings_search_run (entries, "del");
	check (hits->len == 1);
	g_ptr_array_free (hits, TRUE);

	hits = nemo_settings_search_run (entries, "zzz");
	check (hits->len == 0);
	g_ptr_array_free (hits, TRUE);

	g_ptr_array_free (entries, TRUE);
}

/* 2026111912460983: the table made from the schema's alias rows. */
static void
test_alias_table (void)
{
	const char *const *aliases = nemo_settings_search_aliases_for ("appearance.mode");
	gboolean dark = FALSE;

	check (aliases != NULL);
	for (; aliases != NULL && *aliases != NULL; aliases++) {
		dark = dark || strcmp (*aliases, "dark mode") == 0;
	}
	check (dark);
	check (nemo_settings_search_aliases_for ("state.first-run-done") == NULL);
	check (nemo_settings_search_aliases_for ("no.such-key") == NULL);
	check (nemo_settings_search_aliases_for (NULL) == NULL);
}

static void
test_markup (void)
{
	GArray *marks = g_array_new (FALSE, FALSE, sizeof (guint));
	char *markup;

	nemo_settings_search_literal ("Copy & move", "& m", NULL, marks);
	markup = nemo_settings_search_markup ("Copy & move", marks, "#3584e4");
	check (strcmp (markup,
		       "Copy <span weight=\"bold\" size=\"larger\" foreground=\"#3584e4\">&amp; m</span>ove") == 0);
	check (pango_parse_markup (markup, -1, 0, NULL, NULL, NULL, NULL));
	g_free (markup);

	markup = nemo_settings_search_markup ("Copy & move", marks, NULL);
	check (strstr (markup, "foreground") == NULL);
	check (pango_parse_markup (markup, -1, 0, NULL, NULL, NULL, NULL));
	g_free (markup);
	g_array_unref (marks);

	markup = nemo_settings_search_join ("Views" " \xe2\x8f\xb5 " "Default", "Icon size");
	check (strcmp (markup, "Views \xe2\x8f\xb5 Default \xe2\x8f\xb5 Icon size") == 0);
	g_free (markup);
	markup = nemo_settings_search_join (NULL, "Views");
	check (strcmp (markup, "Views") == 0);
	g_free (markup);
}

int
main (void)
{
	test_design_examples ();
	test_literal ();
	test_fuzzy ();
	test_groups ();
	test_alias_table ();
	test_markup ();

	if (failures == 0) {
		g_print ("All settings search checks passed\n");
	}
	return failures == 0 ? 0 : 1;
}
