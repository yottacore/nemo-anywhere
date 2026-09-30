/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */

/* test-nemo-list-view-counts.c - counts the list view's per-row work.

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

/* Preloaded into the real program by test-nemo-list-view-work. It stands in
 * front of the GTK and GObject calls the list view makes per row and per
 * cell, counts them, and passes each one on. libgtk-3 binds its own calls to
 * itself, so what is counted is what the program asks for, not what GTK does
 * inside. The counts go to $NEMO_COUNTS_OUT ten times a second, since the
 * program is stopped from outside and never gets to say goodbye. */

#define _GNU_SOURCE

#include <dlfcn.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <gtk/gtk.h>

static gint cells;	/* cells the program set up itself, as measuring does */
static gint plain;	/* "no background" handed to a renderer */
static gint tinted;	/* a shaded background handed to a renderer */
static gint areas;	/* where a row sits on screen, asked for */
static gint column_lists;
static gint style_reads;	/* the two theme sizes measuring needs */

static void *
real (const char *name)
{
	void *found = dlsym (RTLD_NEXT, name);

	if (found == NULL) {
		fprintf (stderr, "list-view-counts: no %s to pass on to\n", name);
		_exit (99);
	}
	return found;
}

void
g_object_set (gpointer     object,
	      const gchar *first_property_name,
	      ...)
{
	va_list args;

	if (first_property_name != NULL) {
		if (strcmp (first_property_name, "cell-background-set") == 0) {
			g_atomic_int_inc (&plain);
		} else if (strcmp (first_property_name, "cell-background-rgba") == 0) {
			g_atomic_int_inc (&tinted);
		}
	}

	va_start (args, first_property_name);
	g_object_set_valist (object, first_property_name, args);
	va_end (args);
}

void
gtk_widget_style_get (GtkWidget   *widget,
		      const gchar *first_property_name,
		      ...)
{
	va_list args;

	if (first_property_name != NULL &&
	    (strcmp (first_property_name, "horizontal-separator") == 0 ||
	     strcmp (first_property_name, "expander-size") == 0)) {
		g_atomic_int_inc (&style_reads);
	}

	va_start (args, first_property_name);
	gtk_widget_style_get_valist (widget, first_property_name, args);
	va_end (args);
}

void
gtk_tree_view_get_background_area (GtkTreeView       *tree_view,
				   GtkTreePath       *path,
				   GtkTreeViewColumn *column,
				   GdkRectangle      *rect)
{
	static void (*pass_on) (GtkTreeView *, GtkTreePath *, GtkTreeViewColumn *, GdkRectangle *);

	if (pass_on == NULL) {
		pass_on = real ("gtk_tree_view_get_background_area");
	}
	g_atomic_int_inc (&areas);
	pass_on (tree_view, path, column, rect);
}

GList *
gtk_tree_view_get_columns (GtkTreeView *tree_view)
{
	static GList *(*pass_on) (GtkTreeView *);

	if (pass_on == NULL) {
		pass_on = real ("gtk_tree_view_get_columns");
	}
	g_atomic_int_inc (&column_lists);
	return pass_on (tree_view);
}

void
gtk_tree_view_column_cell_set_cell_data (GtkTreeViewColumn *column,
					 GtkTreeModel      *model,
					 GtkTreeIter       *iter,
					 gboolean           is_expander,
					 gboolean           is_expanded)
{
	static void (*pass_on) (GtkTreeViewColumn *, GtkTreeModel *, GtkTreeIter *, gboolean, gboolean);

	if (pass_on == NULL) {
		pass_on = real ("gtk_tree_view_column_cell_set_cell_data");
	}
	g_atomic_int_inc (&cells);
	pass_on (column, model, iter, is_expander, is_expanded);
}

static void *
write_counts (void *data)
{
	const char *out = data;
	char *part = g_strconcat (out, ".part", NULL);

	for (;;) {
		FILE *f = fopen (part, "w");

		if (f != NULL) {
			fprintf (f, "cells %d\nplain %d\ntinted %d\nareas %d\ncolumn_lists %d\nstyle_reads %d\n",
				 g_atomic_int_get (&cells), g_atomic_int_get (&plain),
				 g_atomic_int_get (&tinted), g_atomic_int_get (&areas),
				 g_atomic_int_get (&column_lists), g_atomic_int_get (&style_reads));
			fclose (f);
			rename (part, out);
		}
		usleep (100 * 1000);
	}

	return NULL;
}

__attribute__ ((constructor)) static void
start_counting (void)
{
	const char *out = getenv ("NEMO_COUNTS_OUT");
	pthread_t thread;

	/* Only the program under test; anything it starts must not write here. */
	if (out != NULL && *out != '\0') {
		out = strdup (out);
		unsetenv ("NEMO_COUNTS_OUT");
		unsetenv ("LD_PRELOAD");
		pthread_create (&thread, NULL, write_counts, (void *) out);
		pthread_detach (thread);
	}
}
