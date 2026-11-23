/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */

/* test-nemo-view-keys-probe.c - checks the order of the views in the View menu
   and the toolbar, and presses Ctrl+4 down to Ctrl+1 from inside the program.
   With $NEMO_VIEWKEYS_IMAGES set it checks a folder of pictures instead: that
   it opens in Image thumbnail view, and that icon view keeps the plain size there.

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

/* Preloaded into the real program by test-nemo-view-keys. One "ok" or "FAIL"
 * line per check goes to $NEMO_VIEWKEYS_OUT, and "done" at the end. */

#include <stdio.h>
#include <string.h>

#include <gtk/gtk.h>
#include <gdk/gdkkeysyms.h>

static const char *const order[] = { "Detailed view", "Compact view", "Icon view", "Image thumbnail view" };

/* Each press, the view it has to show, and the widget type that shows it. */
static const struct {
	guint       key;
	const char *label;
	const char *type_name;
} presses[] = {
	{ GDK_KEY_4, "Image thumbnail view", "NemoIconView" },
	{ GDK_KEY_3, "Icon view",            "NemoIconView" },
	{ GDK_KEY_2, "Compact view",         "NemoIconView" },
	{ GDK_KEY_1, "Detailed view",        "NemoListView" },
};

static FILE      *out;
static GtkWidget *window;
static guint      step;
static int        waited;
static guint      image_height;
static gboolean   images;

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

typedef struct {
	const char *type_name;
	GtkWidget  *found;
} Find;

static void
find_view_cb (GtkWidget *widget, gpointer data)
{
	Find *find = data;

	if (find->found != NULL) {
		return;
	}
	if (is_a (widget, "NemoView") && gtk_widget_get_mapped (widget)) {
		find->found = widget;
	} else if (GTK_IS_CONTAINER (widget)) {
		gtk_container_forall (GTK_CONTAINER (widget), find_view_cb, data);
	}
}

static gboolean
view_is (const char *type_name)
{
	Find find = { NULL, NULL };

	find_view_cb (window, &find);
	return is_a (find.found, type_name);
}

static void
find_layout_cb (GtkWidget *widget, gpointer data)
{
	GtkWidget **found = data;

	if (*found != NULL) {
		return;
	}
	if (GTK_IS_LAYOUT (widget) && gtk_widget_get_mapped (widget)) {
		*found = widget;
	} else if (GTK_IS_CONTAINER (widget)) {
		gtk_container_forall (GTK_CONTAINER (widget), find_layout_cb, data);
	}
}

/* How tall the icons laid out in the view are, which grows with their size. */
static guint
laid_out_height (void)
{
	Find find = { NULL, NULL };
	GtkWidget *layout = NULL;
	guint width = 0, height = 0;

	find_view_cb (window, &find);
	if (find.found != NULL) {
		find_layout_cb (find.found, &layout);
	}
	if (layout != NULL) {
		gtk_layout_get_size (GTK_LAYOUT (layout), &width, &height);
	}
	return height;
}

typedef struct {
	const char *label;
	GtkWidget  *found;
} ItemSearch;

static void
find_item_cb (GtkWidget *widget, gpointer data)
{
	ItemSearch *search = data;

	if (search->found != NULL) {
		return;
	}
	if (GTK_IS_MENU_ITEM (widget) &&
	    g_strcmp0 (gtk_menu_item_get_label (GTK_MENU_ITEM (widget)), search->label) == 0) {
		search->found = widget;
		return;
	}
	if (GTK_IS_CONTAINER (widget)) {
		gtk_container_forall (GTK_CONTAINER (widget), find_item_cb, data);
	}
}

/* Menus hang off their own toplevels, shown or not. */
static GtkWidget *
menu_item (const char *label)
{
	GList *toplevels = gtk_window_list_toplevels ();
	GList *l;
	ItemSearch search = { label, NULL };

	for (l = toplevels; l != NULL && search.found == NULL; l = l->next) {
		find_item_cb (l->data, &search);
	}
	g_list_free (toplevels);

	return search.found;
}

static gboolean
showing (const char *label)
{
	GtkWidget *item = menu_item (label);

	return GTK_IS_CHECK_MENU_ITEM (item) && gtk_check_menu_item_get_active (GTK_CHECK_MENU_ITEM (item));
}

/* -1 when one of the first n labels is missing or out of place. */
static int
place_in (GList *labels_seen, guint n)
{
	int last = -1;
	guint i;

	for (i = 0; i < n; i++) {
		GList *hit = g_list_find_custom (labels_seen, order[i], (GCompareFunc) g_strcmp0);
		int at = hit != NULL ? g_list_position (labels_seen, hit) : -1;

		if (at <= last) {
			return -1;
		}
		last = at;
	}
	return last;
}

static void
check_menu_order (void)
{
	GtkWidget *first = menu_item (order[0]);
	GtkWidget *menu = first != NULL ? gtk_widget_get_parent (first) : NULL;
	GList *children = NULL, *labels = NULL, *l;

	if (GTK_IS_MENU_SHELL (menu)) {
		children = gtk_container_get_children (GTK_CONTAINER (menu));
	}
	for (l = children; l != NULL; l = l->next) {
		if (GTK_IS_MENU_ITEM (l->data)) {
			labels = g_list_append (labels, (gpointer) gtk_menu_item_get_label (l->data));
		}
	}
	report (place_in (labels, G_N_ELEMENTS (order)) >= 0,
		"the View menu lists Detailed, Compact, Icon and Image thumbnail view in that order");
	g_list_free (labels);
	g_list_free (children);
}

static void
collect_tips_cb (GtkWidget *widget, gpointer data)
{
	GList **tips = data;
	char *tip;

	if (GTK_IS_BUTTON (widget)) {
		tip = gtk_widget_get_tooltip_text (widget);
		if (tip != NULL) {
			*tips = g_list_append (*tips, tip);
		}
	}
	if (GTK_IS_CONTAINER (widget)) {
		gtk_container_forall (GTK_CONTAINER (widget), collect_tips_cb, data);
	}
}

static void
check_toolbar_order (void)
{
	GList *tips = NULL;

	collect_tips_cb (window, &tips);
	report (place_in (tips, G_N_ELEMENTS (order)) >= 0,
		"the toolbar has Detailed, Compact, Icon and Image thumbnail view in that order");
	g_list_free_full (tips, g_free);
}

typedef struct {
	const char *tip;
	GtkWidget  *found;
} TipSearch;

static void
find_tip_cb (GtkWidget *widget, gpointer data)
{
	TipSearch *search = data;
	char *tip;

	if (search->found != NULL) {
		return;
	}
	if (GTK_IS_TOGGLE_BUTTON (widget)) {
		tip = gtk_widget_get_tooltip_text (widget);
		if (g_strcmp0 (tip, search->tip) == 0) {
			search->found = widget;
		}
		g_free (tip);
	}
	if (search->found == NULL && GTK_IS_CONTAINER (widget)) {
		gtk_container_forall (GTK_CONTAINER (widget), find_tip_cb, data);
	}
}

static gboolean
toolbar_pressed (const char *tip)
{
	TipSearch search = { tip, NULL };

	find_tip_cb (window, &search);
	return search.found != NULL && gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (search.found));
}

static void
finish (void)
{
	fprintf (out, "done\n");
	fflush (out);
}

static void
press (guint key)
{
	gtk_accel_groups_activate (G_OBJECT (window), key, GDK_CONTROL_MASK);
}

/* A folder of pictures: the switch has to move it from Detailed view to
 * Image thumbnail view once it has loaded, and Ctrl+3 then shows it in icon
 * view at the plain size, smaller than in Image thumbnail view. */
static gboolean
tick_images (void)
{
	if (step == 0) {
		if (!(showing ("Image thumbnail view") && view_is ("NemoIconView") && waited >= 15) &&
		    waited <= 100) {
			return G_SOURCE_CONTINUE;
		}
		report (waited <= 100, "a folder of pictures opens in Image thumbnail view");
		report (toolbar_pressed ("Image thumbnail view"), "the toolbar shows Image thumbnail view pressed");
		image_height = laid_out_height ();
		press (GDK_KEY_3);
		step = 1;
		waited = 0;
		return G_SOURCE_CONTINUE;
	}

	if (!(showing ("Icon view") && view_is ("NemoIconView") && waited >= 15) && waited <= 100) {
		return G_SOURCE_CONTINUE;
	}
	if (waited <= 100) {
		guint icon_height = laid_out_height ();

		if (icon_height == 0 || icon_height >= image_height) {
			fprintf (out, "laid out %u high in image thumbnail view, %u in icon view\n", image_height, icon_height);
		}
		report (icon_height > 0 && icon_height < image_height,
			"icon view keeps the plain size in a folder of pictures");
	} else {
		report (FALSE, "Ctrl+3 shows Icon view in a folder of pictures");
	}
	finish ();
	return G_SOURCE_REMOVE;
}

/* Up to 10 seconds for each step. */
static gboolean
tick (G_GNUC_UNUSED gpointer data)
{
	char *what;

	waited++;

	if (window == NULL) {
		GList *toplevels = gtk_window_list_toplevels ();
		GList *l;

		for (l = toplevels; l != NULL; l = l->next) {
			if (is_a (l->data, "NemoWindow") && gtk_widget_get_mapped (l->data)) {
				window = l->data;
			}
		}
		g_list_free (toplevels);
		if (window == NULL && waited <= 100) {
			return G_SOURCE_CONTINUE;
		}
		if (window == NULL) {
			report (FALSE, "timed out waiting for the window");
			finish ();
			return G_SOURCE_REMOVE;
		}
	}

	if (images) {
		return tick_images ();
	}

	if (step == 0) {
		/* Let the first layout settle. */
		if (window != NULL && (!view_is ("NemoListView") || waited < 20) && waited <= 100) {
			return G_SOURCE_CONTINUE;
		}
		if (window == NULL || !view_is ("NemoListView")) {
			report (FALSE, "timed out waiting for the folder in Detailed view");
			finish ();
			return G_SOURCE_REMOVE;
		}
		check_menu_order ();
		check_toolbar_order ();
		press (presses[0].key);
		step = 1;
		waited = 0;
		return G_SOURCE_CONTINUE;
	}

	/* Some time after it shows, for the icons to be laid out. */
	if (!(showing (presses[step - 1].label) && view_is (presses[step - 1].type_name) && waited >= 15) &&
	    waited <= 100) {
		return G_SOURCE_CONTINUE;
	}
	what = g_strdup_printf ("Ctrl+%s shows %s", gdk_keyval_name (presses[step - 1].key), presses[step - 1].label);
	report (waited <= 100, what);
	g_free (what);

	if (presses[step - 1].key == GDK_KEY_4) {
		image_height = laid_out_height ();
	} else if (presses[step - 1].key == GDK_KEY_3) {
		guint icon_height = laid_out_height ();

		if (icon_height == 0 || image_height <= icon_height) {
			fprintf (out, "laid out %u high in image thumbnail view, %u in icon view\n", image_height, icon_height);
		}
		report (icon_height > 0 && image_height > icon_height,
			"Image thumbnail view shows the icons bigger than icon view");
	}

	if (step == G_N_ELEMENTS (presses) || waited > 100) {
		finish ();
		return G_SOURCE_REMOVE;
	}
	press (presses[step].key);
	step++;
	waited = 0;
	return G_SOURCE_CONTINUE;
}

__attribute__((constructor)) static void
probe_start (void)
{
	const char *path = g_getenv ("NEMO_VIEWKEYS_OUT");

	if (path == NULL) {
		return;
	}
	out = fopen (path, "w");
	images = g_getenv ("NEMO_VIEWKEYS_IMAGES") != NULL;
	/* A thumbnailer the program starts loads this too, and would empty the
	   file again. */
	g_unsetenv ("NEMO_VIEWKEYS_OUT");
	g_unsetenv ("NEMO_VIEWKEYS_IMAGES");
	if (out == NULL) {
		return;
	}
	g_timeout_add (100, tick, NULL);
}
