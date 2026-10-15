/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */

/* test-nemo-places-bookmarks-heading-probe.c - reads the rows of "Places"
   from inside the program.

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

/* Preloaded into the real program by test-nemo-places-bookmarks-heading.
 * Once Places lists Desktop it checks for a Bookmarks heading and writes
 * "ready" to $NEMO_BMHEAD_OUT. The driver then edits the settings file so
 * Desktop goes away. Reload handlers run together, so once Desktop is gone
 * every one of them has run, and the heading is checked again. */

#include <stdio.h>

#include <gtk/gtk.h>

static FILE      *out;
static GtkWidget *places;
static int        step;
static int        waited;

static void
report (gboolean passed, const char *what)
{
	fprintf (out, "%s %s\n", passed ? "ok" : "FAIL", what);
	fflush (out);
}

static gboolean
is_a (GtkWidget *widget, const char *type_name)
{
	GType type = g_type_from_name (type_name);

	return type != 0 && widget != NULL && g_type_is_a (G_OBJECT_TYPE (widget), type);
}

static void
find_cb (GtkWidget *widget, gpointer data)
{
	GtkWidget **found = data;

	if (*found != NULL) {
		return;
	}
	if (is_a (widget, "NemoPlacesTreeView") && gtk_widget_get_mapped (widget)) {
		*found = widget;
	} else if (GTK_IS_CONTAINER (widget)) {
		gtk_container_forall (GTK_CONTAINER (widget), find_cb, data);
	}
}

static GtkWidget *
find_places (void)
{
	GList *toplevels = gtk_window_list_toplevels ();
	GList *l;
	GtkWidget *found = NULL;

	for (l = toplevels; l != NULL && found == NULL; l = l->next) {
		if (is_a (l->data, "NemoWindow") && gtk_widget_get_mapped (l->data)) {
			find_cb (l->data, &found);
		}
	}
	g_list_free (toplevels);

	return found;
}

/* Any string column holding text exactly, over the rows the view shows. */
static gboolean
row_has_text (GtkTreeModel *model, GtkTreeIter *iter, const char *text)
{
	int column;

	for (column = 0; column < gtk_tree_model_get_n_columns (model); column++) {
		GValue value = G_VALUE_INIT;
		gboolean match;

		if (gtk_tree_model_get_column_type (model, column) != G_TYPE_STRING) {
			continue;
		}
		gtk_tree_model_get_value (model, iter, column, &value);
		match = g_strcmp0 (g_value_get_string (&value), text) == 0;
		g_value_unset (&value);
		if (match) {
			return TRUE;
		}
	}
	return FALSE;
}

static gboolean
shown (const char *text, gboolean top_level_only)
{
	GtkTreeModel *model = gtk_tree_view_get_model (GTK_TREE_VIEW (places));
	GtkTreeIter heading, child;
	gboolean more, more_children;

	if (model == NULL) {
		return FALSE;
	}
	for (more = gtk_tree_model_get_iter_first (model, &heading); more;
	     more = gtk_tree_model_iter_next (model, &heading)) {
		if (row_has_text (model, &heading, text)) {
			return TRUE;
		}
		if (top_level_only) {
			continue;
		}
		for (more_children = gtk_tree_model_iter_children (model, &child, &heading); more_children;
		     more_children = gtk_tree_model_iter_next (model, &child)) {
			if (row_has_text (model, &child, text)) {
				return TRUE;
			}
		}
	}
	return FALSE;
}

static void
finish (void)
{
	fprintf (out, "done\n");
	fflush (out);
}

/* Up to fifteen seconds a step; the driver waits on the settings save. */
static gboolean
tick (G_GNUC_UNUSED gpointer data)
{
	if (++waited > 150) {
		report (FALSE, "timed out waiting");
		finish ();
		return G_SOURCE_REMOVE;
	}

	switch (step) {
	case 0:
		places = find_places ();
		if (places == NULL || !shown ("Desktop", FALSE)) {
			return G_SOURCE_CONTINUE;
		}
		report (!shown ("Bookmarks", TRUE), "no Bookmarks heading with no bookmarks");
		fprintf (out, "ready\n");
		fflush (out);
		break;
	case 1:
		if (shown ("Desktop", FALSE)) {
			return G_SOURCE_CONTINUE;
		}
		report (TRUE, "the settings edit was read back");
		report (!shown ("Bookmarks", TRUE), "still none after the settings file changed");
		finish ();
		return G_SOURCE_REMOVE;
	}

	step++;
	waited = 0;
	return G_SOURCE_CONTINUE;
}

__attribute__((constructor)) static void
probe_start (void)
{
	const char *path = g_getenv ("NEMO_BMHEAD_OUT");

	if (path == NULL) {
		return;
	}
	out = fopen (path, "w");
	if (out == NULL) {
		return;
	}
	g_timeout_add (100, tick, NULL);
}
