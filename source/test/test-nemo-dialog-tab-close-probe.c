/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */

/* test-nemo-dialog-tab-close-probe.c - closes a tab while one of its dialogs
   is up.

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

/* Preloaded into the real program by test-nemo-dialog-tab-close. For Move to,
 * Copy to and Select items matching in turn, it opens a tab, selects all,
 * opens the dialog there, and closes the tab. The dialog has to go with it.
 * Then Move to is opened in the first tab, and closing another tab has to
 * leave it up. Actions are found by name through every GtkUIManager the
 * program makes. One "ok" or "FAIL" line per check goes to $NEMO_DLGTAB_OUT,
 * and "done" at the end. */

#define _GNU_SOURCE

#include <dlfcn.h>
#include <stdio.h>

#include <gtk/gtk.h>
#include <gdk/gdkkeysyms.h>

typedef struct {
	const char *action;
	const char *what;
	gboolean    keep;	/* opened in the first tab, and another tab closes */
} Case;

static const Case cases[] = {
	{ "BrowseMoveTo", "Move to", FALSE },
	{ "BrowseCopyTo", "Copy to", FALSE },
	{ "Select Pattern", "Select items matching", FALSE },
	{ "BrowseMoveTo", "Move to in another tab", TRUE },
};

static FILE      *out;
static GList     *managers;
static GtkWidget *window;
static GList     *before;
static GtkWidget *dialog;
static guint      which;
static int        step;
static int        waited;

typedef GtkUIManager *(*UiManagerNewFunc) (void);

static void
manager_gone_cb (G_GNUC_UNUSED gpointer data, GObject *gone)
{
	managers = g_list_remove (managers, gone);
}

GtkUIManager *
gtk_ui_manager_new (void)
{
	static UiManagerNewFunc real;
	GtkUIManager *manager;

	if (real == NULL) {
		*(void **) &real = dlsym (RTLD_NEXT, "gtk_ui_manager_new");
	}
	manager = real ();
	managers = g_list_prepend (managers, manager);
	g_object_weak_ref (G_OBJECT (manager), manager_gone_cb, NULL);
	return manager;
}

static gboolean
activate (const char *name)
{
	GList *m, *g;

	G_GNUC_BEGIN_IGNORE_DEPRECATIONS
	for (m = managers; m != NULL; m = m->next) {
		for (g = gtk_ui_manager_get_action_groups (m->data); g != NULL; g = g->next) {
			GtkAction *action = gtk_action_group_get_action (g->data, name);

			if (action != NULL && gtk_action_is_sensitive (action)) {
				gtk_action_activate (action);
				return TRUE;
			}
		}
	}
	G_GNUC_END_IGNORE_DEPRECATIONS
	return FALSE;
}

static gboolean
is_a (gpointer object, const char *type_name)
{
	GType type = g_type_from_name (type_name);

	return type != 0 && object != NULL && g_type_is_a (G_OBJECT_TYPE (object), type);
}

static int
tabs (void)
{
	GList *l, *children;
	int count = 0;
	GtkWidget *notebook = NULL;
	GList *stack = g_list_prepend (NULL, window);

	/* The first NemoNotebook under the window. */
	while (stack != NULL && notebook == NULL) {
		GtkWidget *widget = stack->data;

		stack = g_list_delete_link (stack, stack);
		if (is_a (widget, "NemoNotebook")) {
			notebook = widget;
		} else if (GTK_IS_CONTAINER (widget)) {
			children = gtk_container_get_children (GTK_CONTAINER (widget));
			for (l = children; l != NULL; l = l->next) {
				stack = g_list_append (stack, l->data);
			}
			g_list_free (children);
		}
	}
	g_list_free (stack);
	if (notebook != NULL) {
		count = gtk_notebook_get_n_pages (GTK_NOTEBOOK (notebook));
	}
	return count;
}

static GtkWidget *
new_dialog (void)
{
	GList *toplevels = gtk_window_list_toplevels ();
	GList *l;
	GtkWidget *found = NULL;

	for (l = toplevels; l != NULL && found == NULL; l = l->next) {
		if (GTK_IS_DIALOG (l->data) && gtk_widget_get_visible (l->data) &&
		    g_list_find (before, l->data) == NULL) {
			found = l->data;
		}
	}
	g_list_free (toplevels);
	return found;
}

static void
report (gboolean passed, const char *what, const char *how)
{
	fprintf (out, "%s %s: %s\n", passed ? "ok" : "FAIL", what, how);
	fflush (out);
}

static void
finish (void)
{
	fprintf (out, "done\n");
	fflush (out);
}

static void
next_case (void)
{
	if (dialog != NULL) {
		g_object_remove_weak_pointer (G_OBJECT (dialog), (gpointer *) &dialog);
		gtk_widget_destroy (dialog);
		dialog = NULL;
	}
	which++;
	step = 1;
	waited = 0;
}

/* Steps per case: 1 a new tab, 2 select all and the dialog, 3 the dialog up,
   4 for a kept one, a second tab to close, 5 the tab closed. */
static gboolean
tick (G_GNUC_UNUSED gpointer data)
{
	const Case *c;

	waited++;
	if (step == 0) {
		GList *toplevels, *l;

		if (waited > 100) {
			report (FALSE, "start", "timed out waiting for the window");
			finish ();
			return G_SOURCE_REMOVE;
		}
		toplevels = gtk_window_list_toplevels ();
		for (l = toplevels; l != NULL; l = l->next) {
			if (is_a (l->data, "NemoWindow") && gtk_widget_get_mapped (l->data)) {
				window = l->data;
			}
		}
		g_list_free (toplevels);
		/* Let the first load settle. */
		if (window != NULL && waited >= 20) {
			step = 1;
			waited = 0;
		}
		return G_SOURCE_CONTINUE;
	}

	if (which >= G_N_ELEMENTS (cases)) {
		finish ();
		return G_SOURCE_REMOVE;
	}
	c = &cases[which];

	switch (step) {
	case 1:
		if (!c->keep && !activate ("New Tab")) {
			report (FALSE, c->what, "no New Tab action");
			next_case ();
			break;
		}
		step = 2;
		waited = 0;
		break;
	case 2:
		if (waited < 15) {
			break;
		}
		if (tabs () != (c->keep ? 1 : 2)) {
			report (FALSE, c->what, "the tab count is off before the dialog");
			next_case ();
			break;
		}
		gtk_accel_groups_activate (G_OBJECT (window), GDK_KEY_a, GDK_CONTROL_MASK);
		g_list_free (before);
		before = gtk_window_list_toplevels ();
		step = 3;
		waited = 0;
		break;
	case 3:
		/* Selecting updates the menus from an idle. */
		if (waited < 5) {
			break;
		}
		if (waited == 5 && !activate (c->action)) {
			report (FALSE, c->what, "no action, or not sensitive");
			next_case ();
			break;
		}
		dialog = new_dialog ();
		if (dialog == NULL) {
			if (waited > 50) {
				report (FALSE, c->what, "the dialog never opened");
				next_case ();
			}
			break;
		}
		g_object_add_weak_pointer (G_OBJECT (dialog), (gpointer *) &dialog);
		step = c->keep ? 4 : 5;
		waited = 0;
		if (!c->keep) {
			activate ("Close");
		}
		break;
	case 4:
		if (waited == 1) {
			activate ("New Tab");
		} else if (waited == 15) {
			activate ("Close");
			step = 5;
			waited = 0;
		}
		break;
	case 5:
		if (waited < 20 || tabs () != 1) {
			if (waited > 50) {
				report (FALSE, c->what, "the tab never closed");
				next_case ();
			}
			break;
		}
		if (c->keep) {
			report (dialog != NULL, c->what, dialog != NULL ? "the dialog stays up" : "the dialog closed with another tab");
		} else {
			report (dialog == NULL, c->what, dialog == NULL ? "the dialog closed with its tab" : "the dialog stays up after its tab closed");
		}
		next_case ();
		break;
	default:
		break;
	}
	return G_SOURCE_CONTINUE;
}

__attribute__((constructor)) static void
probe_start (void)
{
	const char *path = g_getenv ("NEMO_DLGTAB_OUT");

	if (path == NULL) {
		return;
	}
	// codeql[cpp/world-writable-file-creation] a test's own scratch file
	out = fopen (path, "w");
	if (out == NULL) {
		return;
	}
	g_timeout_add (100, tick, NULL);
}
