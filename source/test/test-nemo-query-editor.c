/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */

/* test-nemo-query-editor.c - Enter in the search box straight after typing.

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

/* Whether a search may run is checked a moment after the typing stops. Enter
 * pressed before that moment once threw the check away and went by the answer
 * for the text before, so a quick "type, Enter" did nothing. A real editor,
 * built from its resource, with a pattern that does not parse standing in for
 * the text before. */

#include <config.h>

#include <stdlib.h>
#include <gtk/gtk.h>

#include <libnemo-private/nemo-global-preferences.h>
#include <libnemo-private/nemo-query.h>

#include "nemo-query-editor.h"

#include "test-scratch.h"
#include "test-check.h"

typedef struct {
	int count;
	char *pattern;
} Seen;

static void
wait_ms (int ms)
{
	gint64 until = g_get_monotonic_time () + ms * 1000;

	while (g_get_monotonic_time () < until) {
		while (gtk_events_pending ()) {
			gtk_main_iteration ();
		}
		g_usleep (5000);
	}
}

static GtkWidget *
find_named (GtkWidget *widget, const char *name)
{
	GList *children, *l;
	GtkWidget *found = NULL;

	if (g_strcmp0 (gtk_buildable_get_name (GTK_BUILDABLE (widget)), name) == 0) {
		return widget;
	}
	if (!GTK_IS_CONTAINER (widget)) {
		return NULL;
	}

	children = gtk_container_get_children (GTK_CONTAINER (widget));
	for (l = children; l != NULL && found == NULL; l = l->next) {
		found = find_named (l->data, name);
	}
	g_list_free (children);

	return found;
}

static void
changed_cb (NemoQueryEditor *editor, NemoQuery *query, gboolean reload, Seen *seen)
{
	seen->count++;
	g_free (seen->pattern);
	seen->pattern = nemo_query_get_file_pattern (query);
}

int
main (int argc, char *argv[])
{
	GtkWidget *window, *editor, *entry;
	GFile *location;
	Seen seen = { 0, NULL };
	char *tmp;

	tmp = test_scratch_config_home ("nemo-query-editor-test-XXXXXX");

	if (!gtk_init_check (&argc, &argv)) {
		g_print ("SKIP: no display\n");
		g_free (tmp);
		return 77;
	}

	nemo_global_preferences_init ();
	/* A regular expression can fail to parse, which a plain pattern cannot. */
	nemo_config_set_boolean (nemo_search_preferences, NEMO_PREFERENCES_SEARCH_FILES_REGEX, TRUE);

	editor = nemo_query_editor_new ();
	window = gtk_window_new (GTK_WINDOW_TOPLEVEL);
	gtk_container_add (GTK_CONTAINER (window), editor);

	location = g_file_new_for_path (tmp);
	nemo_query_editor_set_location (NEMO_QUERY_EDITOR (editor), location);
	nemo_query_editor_set_active (NEMO_QUERY_EDITOR (editor), g_file_get_uri (location), TRUE);
	g_signal_connect (editor, "changed", G_CALLBACK (changed_cb), &seen);
	gtk_widget_show_all (window);

	entry = find_named (editor, "file_search_entry");
	check (entry != NULL);
	if (entry == NULL) {
		return EXIT_FAILURE;
	}

	/* Left long enough to be checked, a broken pattern is refused. */
	gtk_entry_set_text (GTK_ENTRY (entry), "[");
	wait_ms (300);
	gtk_widget_activate (entry);
	check (seen.count == 0);

	/* Fixed and entered at once, before the check has had its moment. */
	gtk_entry_set_text (GTK_ENTRY (entry), "report");
	gtk_widget_activate (entry);
	check (seen.count == 1);
	check (g_strcmp0 (seen.pattern, "report") == 0);

	/* The same the other way: a pattern broken and entered at once. */
	gtk_entry_set_text (GTK_ENTRY (entry), "report[");
	gtk_widget_activate (entry);
	check (seen.count == 1);

	wait_ms (300);

	gtk_widget_destroy (window);
	g_object_unref (location);
	g_free (seen.pattern);
	g_free (tmp);

	if (failures > 0) {
		g_printerr ("%d check(s) failed\n", failures);
		return EXIT_FAILURE;
	}
	g_print ("PASS\n");
	return EXIT_SUCCESS;
}
