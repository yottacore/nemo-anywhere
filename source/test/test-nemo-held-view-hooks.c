/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */

/* test-nemo-held-view-hooks.c - what a closed tab's view leaves on the settings.

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

/* Preloaded into the real program by test-nemo-held-view. It notes every
 * handler connected on a settings group, since the groups are not exported and
 * this is the only way to find them. When the program destroys a list or icon
 * view, it keeps a ref on the view, as an unmount, eject or rename still
 * running does, and then asks each group for any handler still connected with
 * the view or a widget inside it as its data. Only pointers are compared, so
 * nothing freed is read.
 *
 * A handler whose data is NULL or a static is there for the whole process, so
 * one of those found gone is reported as lost.
 *
 * It also counts the program's redraws, expander changes and shaded rows on
 * live tree views, and buttons taken off a path bar, so the driver can tell a
 * settings change reached the open tab. All of it goes to $NEMO_HELD_VIEW_OUT
 * ten times a second. */

#define _GNU_SOURCE

#include <dlfcn.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <gtk/gtk.h>

typedef struct {
	gpointer group;
	gulong   id;
	gpointer data;
	char    *detail;
	gboolean whole_process;
} Handler;

typedef struct {
	gpointer widget;
	char    *type;	/* taken before the destroy */
} Inside;

static char      *out_path;
static GMutex     lock;
static GArray    *handlers;	/* every handler connected on a group */
static GPtrArray *destroyed;	/* views and the widgets inside them, gone */
static GString   *report;	/* "destroyed" and "hit" lines so far */
static gint       draws;
static gint       expander_sets;
static gint       shaded_rows;
static gint       pathbar_removes;

static void *
real (const char *name)
{
	void *found = dlsym (RTLD_NEXT, name);

	if (found == NULL) {
		fprintf (stderr, "held-view-hooks: no %s to pass on to\n", name);
		_exit (99);
	}
	return found;
}

static gboolean
is_group (gpointer instance)
{
	GType group_type;

	if (out_path == NULL || instance == NULL) {
		return FALSE;
	}
	group_type = g_type_from_name ("NemoConfigGroup");
	return group_type != 0 && G_TYPE_CHECK_INSTANCE_TYPE (instance, group_type);
}

static void
note_handler (gpointer instance, const gchar *detailed_signal, gulong id, gpointer data)
{
	Dl_info image;
	/* dladdr finds only addresses inside a loaded program or library, which
	   a static is and anything allocated is not. */
	Handler handler = { instance, id, data, g_strdup (detailed_signal),
			    data == NULL || dladdr (data, &image) != 0 };

	g_mutex_lock (&lock);
	g_array_append_val (handlers, handler);
	g_mutex_unlock (&lock);
}

gulong
g_signal_connect_data (gpointer        instance,
		       const gchar    *detailed_signal,
		       GCallback       c_handler,
		       gpointer        data,
		       GClosureNotify  destroy_data,
		       GConnectFlags   connect_flags)
{
	static gulong (*pass_on) (gpointer, const gchar *, GCallback, gpointer, GClosureNotify, GConnectFlags);
	gulong id;

	if (pass_on == NULL) {
		pass_on = real ("g_signal_connect_data");
	}
	id = pass_on (instance, detailed_signal, c_handler, data, destroy_data, connect_flags);
	if (id != 0 && is_group (instance)) {
		note_handler (instance, detailed_signal, id, data);
	}
	return id;
}

gulong
g_signal_connect_object (gpointer       instance,
			 const gchar   *detailed_signal,
			 GCallback      c_handler,
			 gpointer       gobject,
			 GConnectFlags  connect_flags)
{
	static gulong (*pass_on) (gpointer, const gchar *, GCallback, gpointer, GConnectFlags);
	gulong id;

	if (pass_on == NULL) {
		pass_on = real ("g_signal_connect_object");
	}
	id = pass_on (instance, detailed_signal, c_handler, gobject, connect_flags);
	if (id != 0 && is_group (instance)) {
		note_handler (instance, detailed_signal, id, gobject);
	}
	return id;
}

static void
collect (GtkWidget *widget, gpointer found)
{
	Inside inside = { widget, g_strdup (G_OBJECT_TYPE_NAME (widget)) };

	g_array_append_val (found, inside);
	if (GTK_IS_CONTAINER (widget)) {
		gtk_container_forall (GTK_CONTAINER (widget), collect, found);
	}
}

static void
note_hits (GArray *found)
{
	GHashTable *groups = g_hash_table_new (NULL, NULL);
	GHashTable *known = g_hash_table_new (NULL, NULL);
	guint i, j;

	for (i = 0; i < handlers->len; i++) {
		Handler *handler = &g_array_index (handlers, Handler, i);

		g_hash_table_add (groups, handler->group);
		g_hash_table_add (known, GSIZE_TO_POINTER (handler->id));
		for (j = 0; j < found->len; j++) {
			Inside *inside = &g_array_index (found, Inside, j);

			if (handler->data == inside->widget &&
			    g_signal_handler_is_connected (handler->group, handler->id)) {
				g_string_append_printf (report, "hit %s %s\n", inside->type, handler->detail);
			}
		}
	}

	/* Anything connected some way the hooks above did not see. */
	for (j = 0; j < found->len; j++) {
		Inside *inside = &g_array_index (found, Inside, j);
		GHashTableIter iter;
		gpointer group;

		g_hash_table_iter_init (&iter, groups);
		while (g_hash_table_iter_next (&iter, &group, NULL)) {
			gulong id = g_signal_handler_find (group, G_SIGNAL_MATCH_DATA, 0, 0,
							   NULL, NULL, inside->widget);

			if (id != 0 && !g_hash_table_contains (known, GSIZE_TO_POINTER (id))) {
				g_string_append_printf (report, "hit %s handler %lu\n", inside->type, id);
			}
		}
	}

	g_hash_table_destroy (known);
	g_hash_table_destroy (groups);
}

void
gtk_widget_destroy (GtkWidget *widget)
{
	static void (*pass_on) (GtkWidget *);
	const char *type;
	GArray *found;
	guint i;

	if (pass_on == NULL) {
		pass_on = real ("gtk_widget_destroy");
	}
	type = out_path != NULL && widget != NULL ? G_OBJECT_TYPE_NAME (widget) : "";
	if (strcmp (type, "NemoListView") != 0 && strcmp (type, "NemoIconView") != 0) {
		pass_on (widget);
		return;
	}

	/* Never let go, so the view outlives its widgets as a held one does. */
	g_object_ref (widget);

	found = g_array_new (FALSE, FALSE, sizeof (Inside));
	collect (widget, found);

	pass_on (widget);

	g_mutex_lock (&lock);
	note_hits (found);
	for (i = 0; i < found->len; i++) {
		Inside *inside = &g_array_index (found, Inside, i);

		g_ptr_array_add (destroyed, inside->widget);
		g_free (inside->type);
	}
	g_string_append_printf (report, "destroyed %s\n", G_OBJECT_TYPE_NAME (widget));
	g_mutex_unlock (&lock);

	g_array_free (found, TRUE);
}

/* A widget the program destroyed may be freed, so it is never looked at. */
static gboolean
live_tree_view (gpointer widget)
{
	gboolean gone;

	if (out_path == NULL || widget == NULL) {
		return FALSE;
	}
	g_mutex_lock (&lock);
	gone = g_ptr_array_find (destroyed, widget, NULL);
	g_mutex_unlock (&lock);

	return !gone && GTK_IS_TREE_VIEW (widget);
}

void
gtk_widget_queue_draw (GtkWidget *widget)
{
	static void (*pass_on) (GtkWidget *);

	if (pass_on == NULL) {
		pass_on = real ("gtk_widget_queue_draw");
	}
	if (live_tree_view (widget)) {
		g_atomic_int_inc (&draws);
	}
	pass_on (widget);
}

void
gtk_tree_view_set_show_expanders (GtkTreeView *tree_view,
				  gboolean     enabled)
{
	static void (*pass_on) (GtkTreeView *, gboolean);

	if (pass_on == NULL) {
		pass_on = real ("gtk_tree_view_set_show_expanders");
	}
	if (live_tree_view (tree_view)) {
		g_atomic_int_inc (&expander_sets);
	}
	pass_on (tree_view, enabled);
}

/* The list view asks for a row's background area only to pick its shade, so
   this moves only while row shading is on. A redraw comes with any value. */
void
gtk_tree_view_get_background_area (GtkTreeView       *tree_view,
				   GtkTreePath       *path,
				   GtkTreeViewColumn *column,
				   GdkRectangle      *rect)
{
	static void (*pass_on) (GtkTreeView *, GtkTreePath *, GtkTreeViewColumn *,
				GdkRectangle *);

	if (pass_on == NULL) {
		pass_on = real ("gtk_tree_view_get_background_area");
	}
	if (live_tree_view (tree_view)) {
		g_atomic_int_inc (&shaded_rows);
	}
	pass_on (tree_view, path, column, rect);
}

void
gtk_container_remove (GtkContainer *container,
		      GtkWidget    *widget)
{
	static void (*pass_on) (GtkContainer *, GtkWidget *);

	if (pass_on == NULL) {
		pass_on = real ("gtk_container_remove");
	}
	if (out_path != NULL && container != NULL &&
	    strcmp (G_OBJECT_TYPE_NAME (container), "NemoPathBar") == 0) {
		g_atomic_int_inc (&pathbar_removes);
	}
	pass_on (container, widget);
}

static void
note_lost (FILE *f)
{
	guint i;

	for (i = 0; i < handlers->len; i++) {
		Handler *handler = &g_array_index (handlers, Handler, i);

		if (handler->whole_process &&
		    !g_signal_handler_is_connected (handler->group, handler->id)) {
			fprintf (f, "lost %s\n", handler->detail);
		}
	}
}

static void *
write_report (void *data)
{
	char *part = g_strconcat (out_path, ".part", NULL);

	for (;;) {
		FILE *f = fopen (part, "w");

		if (f != NULL) {
			g_mutex_lock (&lock);
			fputs (report->str, f);
			note_lost (f);
			g_mutex_unlock (&lock);
			fprintf (f, "draws %d\nexpander_sets %d\nshaded_rows %d\npathbar_removes %d\n",
				 g_atomic_int_get (&draws), g_atomic_int_get (&expander_sets),
				 g_atomic_int_get (&shaded_rows), g_atomic_int_get (&pathbar_removes));
			fclose (f);
			rename (part, out_path);
		}
		usleep (100 * 1000);
	}

	return NULL;
}

__attribute__ ((constructor)) static void
start_watching (void)
{
	const char *out = getenv ("NEMO_HELD_VIEW_OUT");
	pthread_t thread;

	/* Only the program under test; anything it starts must not write here. */
	if (out != NULL && *out != '\0') {
		handlers = g_array_new (FALSE, FALSE, sizeof (Handler));
		destroyed = g_ptr_array_new ();
		report = g_string_new (NULL);
		out_path = strdup (out);
		unsetenv ("NEMO_HELD_VIEW_OUT");
		unsetenv ("LD_PRELOAD");
		pthread_create (&thread, NULL, write_report, NULL);
		pthread_detach (thread);
	}
}
