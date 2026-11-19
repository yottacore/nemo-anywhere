/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */

/* nemo-settings-search.c - the search field at the top of Settings.

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

#include "nemo-settings-search.h"

#include <string.h>
#include <glib/gi18n.h>

#include <libnemo-private/nemo-tab-list.h>

/* U+23F5, the arrow the settings design doc shows between levels. */
#define PATH_ARROW " \xe2\x8f\xb5 "

/* Under 3 letters nearly every label would match loosely. */
#define FUZZY_MIN_LETTERS 3
/* A match spread wider than this many characters per letter typed is noise,
   such as 3 letters picked out of a whole sentence. */
#define FUZZY_MAX_SPREAD 4
#define FUZZY_HIT 16
#define FUZZY_WORD 8
#define FUZZY_SKIP 1
#define FUZZY_NONE G_MININT

#define FOUND_MS 1500

#define STATE_KEY "nemo-settings-search"
#define TARGET_KEY "nemo-settings-search-target"

/* Lowercase, one gunichar per character of text, so offsets stay the same. */
static gunichar *
fold (const char *text, glong *len, gboolean drop_spaces)
{
	gunichar *chars = g_utf8_to_ucs4_fast (text, -1, len);
	glong i, kept = 0;

	for (i = 0; i < *len; i++) {
		gunichar c = g_unichar_tolower (chars[i]);

		if (drop_spaces && g_unichar_isspace (c)) {
			continue;
		}
		chars[kept++] = c;
	}
	*len = kept;

	return chars;
}

static void
mark_run (GArray *marks, glong from, glong count)
{
	glong i;

	if (marks == NULL) {
		return;
	}
	for (i = 0; i < count; i++) {
		guint at = (guint) (from + i);

		g_array_append_val (marks, at);
	}
}

gboolean
nemo_settings_search_literal (const char *text,
			      const char *query,
			      int        *score,
			      GArray     *marks)
{
	gunichar *t, *q;
	glong tlen, qlen, i, best = -1;
	int best_score = G_MAXINT;
	char *trimmed;

	trimmed = g_strstrip (g_strdup (query));
	q = fold (trimmed, &qlen, FALSE);
	g_free (trimmed);
	if (qlen == 0) {
		g_free (q);
		return FALSE;
	}

	t = fold (text, &tlen, FALSE);
	for (i = 0; i + qlen <= tlen; i++) {
		int s;

		if (memcmp (t + i, q, (gsize) qlen * sizeof (gunichar)) != 0) {
			continue;
		}

		/* The whole label, then its start, then a word's start, then the
		   middle of a word. Earlier beats later within each. */
		if (i == 0 && qlen == tlen) {
			s = 0;
		} else if (i == 0) {
			s = 1000;
		} else if (!g_unichar_isalnum (t[i - 1])) {
			s = 2000 + (int) MIN (i, 999);
		} else {
			s = 3000 + (int) MIN (i, 999);
		}

		if (s < best_score) {
			best_score = s;
			best = i;
		}
	}

	if (best >= 0) {
		if (score != NULL) {
			*score = best_score;
		}
		mark_run (marks, best, qlen);
	}

	g_free (t);
	g_free (q);

	return best >= 0;
}

static gboolean
word_start (const gunichar *t, glong i)
{
	return i == 0 || !g_unichar_isalnum (t[i - 1]);
}

/* Scored the way fzf does it, roughly: each letter counts, a letter at the
   start of a word counts more, and each character skipped counts against. */
gboolean
nemo_settings_search_fuzzy (const char *text,
			    const char *query,
			    int        *score,
			    GArray     *marks)
{
	gunichar *t, *q;
	gint *best;
	glong *from;
	glong tlen, qlen, i, k, end = -1, start;
	gint top = FUZZY_NONE;

	q = fold (query, &qlen, TRUE);
	if (qlen < FUZZY_MIN_LETTERS) {
		g_free (q);
		return FALSE;
	}

	t = fold (text, &tlen, FALSE);
	if (tlen < qlen) {
		g_free (t);
		g_free (q);
		return FALSE;
	}

	/* best[k * tlen + i] is the top score with letter k of the query on
	   character i, and from[] the character letter k - 1 was on. */
	best = g_new (gint, (gsize) (qlen * tlen));
	from = g_new (glong, (gsize) (qlen * tlen));

	for (i = 0; i < tlen; i++) {
		best[i] = t[i] == q[0] ? FUZZY_HIT + (word_start (t, i) ? FUZZY_WORD : 0) : FUZZY_NONE;
		from[i] = -1;
	}
	for (k = 1; k < qlen; k++) {
		const gint *prev = best + (k - 1) * tlen;
		gint *row = best + k * tlen;
		/* The skip penalty is linear, so the best earlier letter can be
		   carried along as one running value. */
		gint run = FUZZY_NONE;
		glong run_at = -1;

		for (i = 0; i < tlen; i++) {
			if (i > 0 && prev[i - 1] != FUZZY_NONE &&
			    prev[i - 1] + FUZZY_SKIP * (gint) (i - 1) > run) {
				run = prev[i - 1] + FUZZY_SKIP * (gint) (i - 1);
				run_at = i - 1;
			}
			row[i] = FUZZY_NONE;
			from[k * tlen + i] = -1;
			if (t[i] == q[k] && run != FUZZY_NONE) {
				row[i] = run - FUZZY_SKIP * (gint) (i - 1) + FUZZY_HIT +
					 (word_start (t, i) ? FUZZY_WORD : 0);
				from[k * tlen + i] = run_at;
			}
		}
	}

	for (i = 0; i < tlen; i++) {
		if (best[(qlen - 1) * tlen + i] > top) {
			top = best[(qlen - 1) * tlen + i];
			end = i;
		}
	}

	start = end;
	for (k = qlen - 1; k > 0 && start >= 0; k--) {
		start = from[k * tlen + start];
	}

	if (end < 0 || start < 0 || end - start + 1 > FUZZY_MAX_SPREAD * qlen) {
		g_free (best);
		g_free (from);
		g_free (t);
		g_free (q);
		return FALSE;
	}

	if (score != NULL) {
		*score = -top;
	}
	if (marks != NULL) {
		guint first = marks->len;
		glong at = end;

		g_array_set_size (marks, first + (guint) qlen);
		for (k = qlen - 1; k >= 0; k--) {
			g_array_index (marks, guint, first + (guint) k) = (guint) at;
			at = from[k * tlen + at];
		}
	}

	g_free (best);
	g_free (from);
	g_free (t);
	g_free (q);

	return TRUE;
}

static void
hit_free (gpointer data)
{
	NemoSettingsHit *hit = data;

	g_array_unref (hit->marks);
	g_free (hit);
}

static gint
hit_order (gconstpointer a, gconstpointer b)
{
	const NemoSettingsHit *x = *(NemoSettingsHit * const *) a;
	const NemoSettingsHit *y = *(NemoSettingsHit * const *) b;

	if (x->kind != y->kind) {
		return x->kind < y->kind ? -1 : 1;
	}
	if (x->score != y->score) {
		return x->score < y->score ? -1 : 1;
	}
	if (x->index != y->index) {
		return x->index < y->index ? -1 : 1;
	}
	return 0;
}

/* Returns: (transfer full) */
GPtrArray *
nemo_settings_search_run (GPtrArray *entries, const char *query)
{
	GPtrArray *hits = g_ptr_array_new_with_free_func (hit_free);
	guint i;

	for (i = 0; i < entries->len; i++) {
		NemoSettingsEntry *entry = entries->pdata[i];
		NemoSettingsMatchKind kind = NEMO_SETTINGS_MATCH_NONE;
		GArray *marks = g_array_new (FALSE, FALSE, sizeof (guint));
		const char *text = entry->label;
		NemoSettingsHit *hit;
		int score = 0;

		if (nemo_settings_search_literal (entry->label, query, &score, marks)) {
			kind = NEMO_SETTINGS_MATCH_LABEL;
		}
		for (char **alias = entry->aliases;
		     kind == NEMO_SETTINGS_MATCH_NONE && alias != NULL && *alias != NULL;
		     alias++) {
			if (nemo_settings_search_literal (*alias, query, &score, marks)) {
				kind = NEMO_SETTINGS_MATCH_ALIAS;
				text = *alias;
			}
		}
		if (kind == NEMO_SETTINGS_MATCH_NONE &&
		    nemo_settings_search_fuzzy (entry->label, query, &score, marks)) {
			kind = NEMO_SETTINGS_MATCH_FUZZY;
		}

		if (kind == NEMO_SETTINGS_MATCH_NONE) {
			g_array_unref (marks);
			continue;
		}

		hit = g_new0 (NemoSettingsHit, 1);
		hit->index = i;
		hit->kind = kind;
		hit->score = score;
		hit->text = text;
		hit->marks = marks;
		g_ptr_array_add (hits, hit);
	}

	g_ptr_array_sort (hits, hit_order);

	return hits;
}

/* Returns: (transfer full) */
char *
nemo_settings_search_markup (const char *text,
			     GArray     *marks,
			     const char *mark_color)
{
	GString *out = g_string_new (NULL);
	gboolean in_mark = FALSE;
	const char *p;
	guint n = 0, next = 0;

	for (p = text; *p != '\0'; p = g_utf8_next_char (p), n++) {
		gboolean marked = FALSE;
		char *escaped;

		while (marks != NULL && next < marks->len && g_array_index (marks, guint, next) < n) {
			next++;
		}
		if (marks != NULL && next < marks->len && g_array_index (marks, guint, next) == n) {
			marked = TRUE;
		}

		if (marked && !in_mark) {
			g_string_append (out, "<span weight=\"bold\" size=\"larger\"");
			if (mark_color != NULL) {
				g_string_append_printf (out, " foreground=\"%s\"", mark_color);
			}
			g_string_append (out, ">");
		} else if (!marked && in_mark) {
			g_string_append (out, "</span>");
		}
		in_mark = marked;

		escaped = g_markup_escape_text (p, g_utf8_next_char (p) - p);
		g_string_append (out, escaped);
		g_free (escaped);
	}
	if (in_mark) {
		g_string_append (out, "</span>");
	}

	return g_string_free (out, FALSE);
}

/* Returns: (transfer full) */
char *
nemo_settings_search_join (const char *path, const char *label)
{
	if (path == NULL || *path == '\0') {
		return g_strdup (label);
	}
	return g_strconcat (path, PATH_ARROW, label, NULL);
}

/* Returns: (transfer full) */
NemoSettingsEntry *
nemo_settings_entry_new (const char *label, const char *path, gpointer target)
{
	NemoSettingsEntry *entry = g_new0 (NemoSettingsEntry, 1);

	entry->label = g_strdup (label);
	entry->path = g_strdup (path);
	entry->target = target;

	return entry;
}

void
nemo_settings_entry_free (NemoSettingsEntry *entry)
{
	if (entry == NULL) {
		return;
	}
	g_free (entry->label);
	g_free (entry->path);
	g_strfreev (entry->aliases);
	g_free (entry);
}

/* The window's side of it. */

typedef struct {
	GtkWidget *dialog;
	GtkWidget *entry;
	GtkWidget *results;
	GtkWidget *scroll;
	GtkWidget *pages;
	GtkWidget *sidebar;
} SearchState;

static gboolean
is_heading (GtkLabel *label)
{
	return gtk_label_get_use_markup (label) &&
	       g_str_has_prefix (gtk_label_get_label (label), "<b>");
}

/* The last level of path, to tell a heading that only repeats its tab. */
static gboolean
path_ends_with (const char *path, const char *text)
{
	const char *last;

	if (path == NULL) {
		return FALSE;
	}
	last = g_strrstr (path, PATH_ARROW);
	last = last != NULL ? last + strlen (PATH_ARROW) : path;

	return g_utf8_collate (last, text) == 0;
}

static void
add_label (GtkLabel *label, GPtrArray *entries, const char *path)
{
	GtkWidget *target;
	char *text;
	const char *p;
	gboolean has_letters = FALSE;

	/* A label that wraps is a description, not a setting. */
	if (gtk_label_get_line_wrap (label)) {
		return;
	}

	text = g_strstrip (g_strdup (gtk_label_get_text (label)));
	if (g_str_has_suffix (text, ":")) {
		text[strlen (text) - 1] = '\0';
		g_strchomp (text);
	}
	for (p = text; *p != '\0'; p = g_utf8_next_char (p)) {
		if (g_unichar_isalpha (g_utf8_get_char (p))) {
			has_letters = TRUE;
			break;
		}
	}

	if (has_letters) {
		target = gtk_widget_get_ancestor (GTK_WIDGET (label), GTK_TYPE_BUTTON);
		if (target == NULL) {
			target = gtk_label_get_mnemonic_widget (label);
		}
		if (target == NULL) {
			target = GTK_WIDGET (label);
		}
		g_ptr_array_add (entries, nemo_settings_entry_new (text, path, target));
	}
	g_free (text);
}

static void walk (GtkWidget *widget, GPtrArray *entries, const char *path);

static void
walk_stack (GtkStack *stack, GPtrArray *entries, const char *path)
{
	GList *children, *l;

	children = gtk_container_get_children (GTK_CONTAINER (stack));
	for (l = children; l != NULL; l = l->next) {
		GtkWidget *page = l->data;
		char *title = NULL;
		char *page_path;

		if (!gtk_widget_get_visible (page)) {
			continue;
		}
		gtk_container_child_get (GTK_CONTAINER (stack), page, "title", &title, NULL);
		if (title == NULL || *title == '\0') {
			g_free (title);
			walk (page, entries, path);
			continue;
		}

		g_ptr_array_add (entries, nemo_settings_entry_new (title, path, page));
		page_path = nemo_settings_search_join (path, title);
		walk (page, entries, page_path);
		g_free (page_path);
		g_free (title);
	}
	g_list_free (children);
}

/* A heading covers what comes after it in its own box. */
static void
walk_children (GtkContainer *container, GPtrArray *entries, const char *path)
{
	GList *children, *l;
	char *under = NULL;

	children = gtk_container_get_children (container);
	for (l = children; l != NULL; l = l->next) {
		GtkWidget *child = l->data;

		if (!gtk_widget_get_visible (child)) {
			continue;
		}

		if (GTK_IS_LABEL (child) && is_heading (GTK_LABEL (child))) {
			const char *text = gtk_label_get_text (GTK_LABEL (child));

			g_free (under);
			under = NULL;
			if (path_ends_with (path, text)) {
				continue;
			}
			g_ptr_array_add (entries, nemo_settings_entry_new (text, path, child));
			under = nemo_settings_search_join (path, text);
			continue;
		}

		walk (child, entries, under != NULL ? under : path);
	}
	g_list_free (children);
	g_free (under);
}

static void
walk (GtkWidget *widget, GPtrArray *entries, const char *path)
{
	if (GTK_IS_LABEL (widget)) {
		add_label (GTK_LABEL (widget), entries, path);
	} else if (GTK_IS_TREE_VIEW (widget) || GTK_IS_COMBO_BOX (widget) || GTK_IS_ENTRY (widget)) {
		return;
	} else if (GTK_IS_STACK (widget)) {
		walk_stack (GTK_STACK (widget), entries, path);
	} else if (GTK_IS_CONTAINER (widget)) {
		walk_children (GTK_CONTAINER (widget), entries, path);
	}
}

static char *
mark_color (GtkWidget *widget)
{
	GdkRGBA rgba;

	if (!gtk_style_context_lookup_color (gtk_widget_get_style_context (widget),
					     "theme_selected_bg_color", &rgba)) {
		return NULL;
	}
	return g_strdup_printf ("#%02x%02x%02x",
				(guint) (rgba.red * 255 + 0.5),
				(guint) (rgba.green * 255 + 0.5),
				(guint) (rgba.blue * 255 + 0.5));
}

static GtkWidget *
hit_row (const NemoSettingsEntry *entry, const NemoSettingsHit *hit, const char *color)
{
	GtkWidget *row, *label;
	GString *markup = g_string_new (NULL);
	char *part;

	if (entry->path != NULL) {
		part = g_markup_escape_text (entry->path, -1);
		g_string_append (markup, part);
		g_string_append (markup, PATH_ARROW);
		g_free (part);
	}
	if (hit->kind == NEMO_SETTINGS_MATCH_ALIAS) {
		/* The label has nothing to mark, so the alias goes after it. */
		part = g_markup_escape_text (entry->label, -1);
		g_string_append (markup, part);
		g_free (part);
		part = nemo_settings_search_markup (hit->text, hit->marks, color);
		g_string_append_printf (markup, " (%s)", part);
	} else {
		part = nemo_settings_search_markup (hit->text, hit->marks, color);
		g_string_append (markup, part);
	}
	g_free (part);

	label = gtk_label_new (NULL);
	gtk_label_set_markup (GTK_LABEL (label), markup->str);
	gtk_label_set_xalign (GTK_LABEL (label), 0);
	gtk_label_set_ellipsize (GTK_LABEL (label), PANGO_ELLIPSIZE_END);
	gtk_widget_set_margin_start (label, 8);
	gtk_widget_set_margin_end (label, 8);
	gtk_widget_set_margin_top (label, 4);
	gtk_widget_set_margin_bottom (label, 4);
	g_string_free (markup, TRUE);

	row = gtk_list_box_row_new ();
	gtk_widget_set_can_focus (row, FALSE);
	gtk_container_add (GTK_CONTAINER (row), label);
	g_object_set_data (G_OBJECT (row), TARGET_KEY, entry->target);
	gtk_widget_show_all (row);

	return row;
}

static void
refresh (SearchState *state)
{
	const char *query = gtk_entry_get_text (GTK_ENTRY (state->entry));
	GList *rows, *l;
	GPtrArray *entries, *hits;
	GtkListBoxRow *first;
	gboolean searching;
	char *trimmed, *color;
	guint i;

	trimmed = g_strstrip (g_strdup (query));
	searching = *trimmed != '\0';
	g_free (trimmed);

	rows = gtk_container_get_children (GTK_CONTAINER (state->results));
	for (l = rows; l != NULL; l = l->next) {
		gtk_widget_destroy (l->data);
	}
	g_list_free (rows);

	gtk_widget_set_visible (state->pages, !searching);
	gtk_widget_set_visible (state->scroll, searching);
	if (!searching) {
		return;
	}

	entries = g_ptr_array_new_with_free_func ((GDestroyNotify) nemo_settings_entry_free);
	walk (state->pages, entries, NULL);
	hits = nemo_settings_search_run (entries, query);
	color = mark_color (state->results);

	for (i = 0; i < hits->len; i++) {
		NemoSettingsHit *hit = hits->pdata[i];

		gtk_container_add (GTK_CONTAINER (state->results),
				   hit_row (entries->pdata[hit->index], hit, color));
	}

	first = gtk_list_box_get_row_at_index (GTK_LIST_BOX (state->results), 0);
	if (first != NULL) {
		gtk_list_box_select_row (GTK_LIST_BOX (state->results), first);
	}

	g_free (color);
	g_ptr_array_free (hits, TRUE);
	g_ptr_array_free (entries, TRUE);
}

static gboolean
unmark_found (gpointer data)
{
	GtkWidget *target = data;

	gtk_style_context_remove_class (gtk_widget_get_style_context (target), "nemo-settings-found");
	g_object_unref (target);

	return G_SOURCE_REMOVE;
}

/* After the page is allocated, so its rows have places to scroll to. */
static gboolean
reveal_target (gpointer data)
{
	GtkWidget *target = data;
	GtkWidget *scroll, *child;
	gint x, y;

	if (gtk_widget_in_destruction (target) || gtk_widget_get_toplevel (target) == target) {
		g_object_unref (target);
		return G_SOURCE_REMOVE;
	}

	scroll = gtk_widget_get_ancestor (target, GTK_TYPE_SCROLLED_WINDOW);
	child = scroll != NULL ? gtk_bin_get_child (GTK_BIN (scroll)) : NULL;
	if (child != NULL && target != child &&
	    gtk_widget_translate_coordinates (target, child, 0, 0, &x, &y)) {
		GtkAdjustment *adj = gtk_scrolled_window_get_vadjustment (GTK_SCROLLED_WINDOW (scroll));
		gdouble top = gtk_adjustment_get_value (adj) + y;

		gtk_adjustment_clamp_page (adj, top, top + gtk_widget_get_allocated_height (target));
	}

	if (gtk_widget_get_can_focus (target) && gtk_widget_is_sensitive (target)) {
		gtk_widget_grab_focus (target);
	}

	gtk_style_context_add_class (gtk_widget_get_style_context (target), "nemo-settings-found");
	g_timeout_add (FOUND_MS, unmark_found, target);

	return G_SOURCE_REMOVE;
}

static void
entry_changed (G_GNUC_UNUSED GtkEditable *editable, SearchState *state)
{
	refresh (state);
}

static void
go_to (SearchState *state, GtkListBoxRow *row)
{
	GtkWidget *target;

	if (row == NULL) {
		return;
	}
	target = g_object_get_data (G_OBJECT (row), TARGET_KEY);
	if (target == NULL) {
		return;
	}

	/* The rows stay, hidden, until the next search. This runs from a row's
	   own activation, which is no time to destroy it. */
	g_object_ref (target);
	g_signal_handlers_block_by_func (state->entry, entry_changed, state);
	gtk_entry_set_text (GTK_ENTRY (state->entry), "");
	g_signal_handlers_unblock_by_func (state->entry, entry_changed, state);
	gtk_widget_hide (state->scroll);
	gtk_widget_show (state->pages);
	nemo_tab_list_show_page (NEMO_TAB_LIST (state->sidebar), target);
	g_idle_add (reveal_target, target);
}

static void
row_activated (G_GNUC_UNUSED GtkListBox *box, GtkListBoxRow *row, SearchState *state)
{
	go_to (state, row);
}

static void
entry_activated (G_GNUC_UNUSED GtkEntry *entry, SearchState *state)
{
	go_to (state, gtk_list_box_get_selected_row (GTK_LIST_BOX (state->results)));
}

/* Escape empties the field first, and closes Settings only once it's empty. */
static void
stop_search (GtkSearchEntry *entry, SearchState *state)
{
	if (*gtk_entry_get_text (GTK_ENTRY (entry)) != '\0') {
		gtk_entry_set_text (GTK_ENTRY (entry), "");
	} else {
		gtk_widget_destroy (state->dialog);
	}
}

static gboolean
entry_keys (G_GNUC_UNUSED GtkWidget *widget, GdkEventKey *event, SearchState *state)
{
	GtkListBox *box = GTK_LIST_BOX (state->results);
	GtkListBoxRow *row, *next;
	int step;

	if ((event->state & gtk_accelerator_get_default_mod_mask ()) != 0) {
		return GDK_EVENT_PROPAGATE;
	}
	switch (event->keyval) {
	case GDK_KEY_Down:
	case GDK_KEY_KP_Down:
		step = 1;
		break;
	case GDK_KEY_Up:
	case GDK_KEY_KP_Up:
		step = -1;
		break;
	default:
		return GDK_EVENT_PROPAGATE;
	}

	if (!gtk_widget_get_visible (state->scroll)) {
		return GDK_EVENT_PROPAGATE;
	}

	row = gtk_list_box_get_selected_row (box);
	next = row == NULL
		? gtk_list_box_get_row_at_index (box, 0)
		: gtk_list_box_get_row_at_index (box, gtk_list_box_row_get_index (row) + step);
	if (next != NULL) {
		GtkAdjustment *adj = gtk_list_box_get_adjustment (box);
		GtkAllocation at;

		gtk_list_box_select_row (box, next);
		gtk_widget_get_allocation (GTK_WIDGET (next), &at);
		if (adj != NULL) {
			gtk_adjustment_clamp_page (adj, at.y, at.y + at.height);
		}
	}

	return GDK_EVENT_STOP;
}

/* Ctrl+F goes to the field, as find does elsewhere. */
static gboolean
dialog_keys (G_GNUC_UNUSED GtkWidget *widget, GdkEventKey *event, SearchState *state)
{
	if ((event->state & gtk_accelerator_get_default_mod_mask ()) == GDK_CONTROL_MASK &&
	    (event->keyval == GDK_KEY_f || event->keyval == GDK_KEY_F)) {
		gtk_widget_grab_focus (state->entry);
		return GDK_EVENT_STOP;
	}

	return GDK_EVENT_PROPAGATE;
}

/* Picking a tab while matches are up means leaving the search. */
static void
tab_picked (G_GNUC_UNUSED GtkListBox    *box,
	    G_GNUC_UNUSED GtkListBoxRow *row,
	    SearchState *state)
{
	if (*gtk_entry_get_text (GTK_ENTRY (state->entry)) != '\0') {
		gtk_entry_set_text (GTK_ENTRY (state->entry), "");
	}
}

static GtkWidget *
find_list_box (GtkWidget *widget)
{
	GList *children, *l;
	GtkWidget *found = NULL;

	if (GTK_IS_LIST_BOX (widget)) {
		return widget;
	}
	if (!GTK_IS_CONTAINER (widget)) {
		return NULL;
	}
	children = gtk_container_get_children (GTK_CONTAINER (widget));
	for (l = children; l != NULL && found == NULL; l = l->next) {
		found = find_list_box (l->data);
	}
	g_list_free (children);

	return found;
}

/* The picked match gets a plain shade, since the selection color is the color
   the matched letters are in. */
static void
add_css_once (void)
{
	static gboolean added = FALSE;
	GtkCssProvider *provider;

	if (added || gdk_screen_get_default () == NULL) {
		return;
	}
	added = TRUE;

	provider = gtk_css_provider_new ();
	gtk_css_provider_load_from_data (provider,
		"#nemo-settings-search-results row:selected {"
		" background-color: alpha(@theme_fg_color, 0.12); color: @theme_fg_color; }"
		"#nemo-settings-search-results row:selected label { color: @theme_fg_color; }"
		".nemo-settings-found { background-color: alpha(@theme_selected_bg_color, 0.35); }",
		-1, NULL);
	gtk_style_context_add_provider_for_screen (gdk_screen_get_default (),
						   GTK_STYLE_PROVIDER (provider),
						   GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
	g_object_unref (provider);
}

void
nemo_settings_search_setup (GtkBuilder *builder)
{
	SearchState *state;
	GtkWidget *empty, *tabs;

	state = g_new0 (SearchState, 1);
	state->dialog = GTK_WIDGET (gtk_builder_get_object (builder, "file_management_dialog"));
	state->entry = GTK_WIDGET (gtk_builder_get_object (builder, "settings_search_entry"));
	state->results = GTK_WIDGET (gtk_builder_get_object (builder, "settings_search_results"));
	state->scroll = GTK_WIDGET (gtk_builder_get_object (builder, "settings_search_scroll"));
	state->pages = GTK_WIDGET (gtk_builder_get_object (builder, "page_stack"));
	state->sidebar = GTK_WIDGET (gtk_builder_get_object (builder, "page_sidebar"));
	g_object_set_data_full (G_OBJECT (state->entry), STATE_KEY, state, g_free);

	add_css_once ();

	empty = gtk_label_new (_("No settings match"));
	gtk_widget_set_margin_top (empty, 12);
	gtk_widget_show (empty);
	gtk_list_box_set_placeholder (GTK_LIST_BOX (state->results), empty);
	gtk_list_box_set_selection_mode (GTK_LIST_BOX (state->results), GTK_SELECTION_SINGLE);

	g_signal_connect (state->entry, "changed", G_CALLBACK (entry_changed), state);
	g_signal_connect (state->entry, "activate", G_CALLBACK (entry_activated), state);
	g_signal_connect (state->entry, "stop-search", G_CALLBACK (stop_search), state);
	g_signal_connect (state->entry, "key-press-event", G_CALLBACK (entry_keys), state);
	g_signal_connect (state->results, "row-activated", G_CALLBACK (row_activated), state);
	g_signal_connect (state->dialog, "key-press-event", G_CALLBACK (dialog_keys), state);
	tabs = find_list_box (state->sidebar);
	if (tabs != NULL) {
		g_signal_connect (tabs, "row-activated", G_CALLBACK (tab_picked), state);
	}
}
