/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */

/* test-nemo-bulk-rename-run-probe.c - selects every file and presses F2, from
   inside the program.

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

/* Preloaded into the real program by test-nemo-bulk-rename-run. Once the
 * window shows $NEMO_BRUN_FOLDER with its files, it selects them all and
 * presses F2, which starts the bulk renamer. What the renamer was handed is the
 * driver's to check. "done" goes to $NEMO_BRUN_OUT once the keys are in, or
 * "FAIL" with the reason. */

#include <stdio.h>
#include <string.h>

#include <gtk/gtk.h>

#define FOLDER_ITEMS 3

static FILE       *out;
static const char *folder_name;
static GtkWidget  *window;
static int         waited;
static int         selected_at;

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
	if (is_a (widget, "NemoView")) {
		*found = widget;
	} else if (GTK_IS_CONTAINER (widget)) {
		gtk_container_forall (GTK_CONTAINER (widget), find_cb, data);
	}
}

static GtkWidget *
tree (void)
{
	GtkWidget *view = NULL;
	GtkWidget *items;

	find_cb (window, &view);
	items = view != NULL ? gtk_bin_get_child (GTK_BIN (view)) : NULL;
	return items != NULL && GTK_IS_TREE_VIEW (items) && gtk_widget_get_mapped (items) ? items : NULL;
}

static int
item_count (void)
{
	GtkWidget *items = tree ();
	GtkTreeModel *model = items != NULL ? gtk_tree_view_get_model (GTK_TREE_VIEW (items)) : NULL;

	return model != NULL ? gtk_tree_model_iter_n_children (model, NULL) : -1;
}

static gboolean
tick (G_GNUC_UNUSED gpointer data)
{
	GList *toplevels, *l;
	const char *title;

	if (++waited > 300) {
		fprintf (out, "FAIL the folder never showed its files (title \"%s\", %d items)\n",
			 window != NULL ? gtk_window_get_title (GTK_WINDOW (window)) : "", window != NULL ? item_count () : -1);
		fflush (out);
		return G_SOURCE_REMOVE;
	}

	toplevels = gtk_window_list_toplevels ();
	for (l = toplevels; l != NULL; l = l->next) {
		if (is_a (l->data, "NemoWindow") && gtk_widget_get_mapped (l->data)) {
			window = l->data;
		}
	}
	g_list_free (toplevels);
	title = window != NULL ? gtk_window_get_title (GTK_WINDOW (window)) : NULL;
	/* A name with a space is shown in quotes. */
	if (title == NULL || strstr (title, folder_name) == NULL || item_count () != FOLDER_ITEMS) {
		return G_SOURCE_CONTINUE;
	}

	/* F2 is looked at from an idle after the selection changes. */
	if (selected_at == 0) {
		gtk_widget_grab_focus (tree ());
		gtk_tree_selection_select_all (gtk_tree_view_get_selection (GTK_TREE_VIEW (tree ())));
		selected_at = waited;
		return G_SOURCE_CONTINUE;
	}
	if (waited - selected_at < 5) {
		return G_SOURCE_CONTINUE;
	}
	if (!gtk_accel_groups_activate (G_OBJECT (window), GDK_KEY_F2, 0)) {
		fprintf (out, "FAIL F2 isn't a shortcut in the window\n");
	} else {
		fprintf (out, "done\n");
	}
	fflush (out);
	return G_SOURCE_REMOVE;
}

__attribute__((constructor)) static void
probe_start (void)
{
	const char *path = g_getenv ("NEMO_BRUN_OUT");

	folder_name = g_strdup (g_getenv ("NEMO_BRUN_FOLDER"));
	if (path == NULL || folder_name == NULL) {
		return;
	}
	out = fopen (path, "w");
	/* The renamer the program starts loads this too. */
	g_unsetenv ("NEMO_BRUN_OUT");
	g_unsetenv ("NEMO_BRUN_FOLDER");
	g_unsetenv ("LD_PRELOAD");
	if (out == NULL) {
		return;
	}
	g_timeout_add (100, tick, NULL);
}
