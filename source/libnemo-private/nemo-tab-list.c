/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */

/* nemo-tab-list.c - a list of tabs down the side of a stack, sub-tabs and all.

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

#include "nemo-tab-list.h"

#include <eel/eel-gtk-extensions.h>

/* Per level, on top of the theme's own padding. */
#define SUB_TAB_INDENT 18

#define PAGE_KEY "nemo-tab-list-page"
#define WATCHED_KEY "nemo-tab-list-watched"

struct _NemoTabList {
	GtkBin     parent_instance;

	GtkStack  *stack;
	GtkWidget *box;
};

enum {
	PROP_0,
	PROP_STACK,
	N_PROPS
};

static GParamSpec *props[N_PROPS];

static GtkBuildableIface *parent_buildable_iface;

static void nemo_tab_list_buildable_init (GtkBuildableIface *iface);

G_DEFINE_TYPE_WITH_CODE (NemoTabList, nemo_tab_list, GTK_TYPE_BIN,
			 G_IMPLEMENT_INTERFACE (GTK_TYPE_BUILDABLE, nemo_tab_list_buildable_init))

static gboolean
alive (NemoTabList *list)
{
	return list->box != NULL && list->stack != NULL;
}

static GtkWidget *
row_page (GtkWidget *row)
{
	return g_object_get_data (G_OBJECT (row), PAGE_KEY);
}

/* Whether the page and every page above it is visible, and whether each is
   the one its stack is showing. */
static void
page_state (NemoTabList *list, GtkWidget *page, gboolean *shown, gboolean *showing)
{
	GtkWidget *w, *parent;

	*shown = TRUE;
	*showing = TRUE;
	for (w = page; w != NULL && w != GTK_WIDGET (list->stack); w = parent) {
		parent = gtk_widget_get_parent (w);
		if (!GTK_IS_STACK (parent)) {
			continue;
		}
		*shown = *shown && gtk_widget_get_visible (w);
		*showing = *showing && gtk_stack_get_visible_child (GTK_STACK (parent)) == w;
	}
}

/* Another stack inside a page is the page's own business unless marked. */
static GtkWidget *
find_marked_stack (GtkWidget *widget)
{
	GList *children, *l;
	GtkWidget *found = NULL;

	if (GTK_IS_STACK (widget)) {
		return gtk_style_context_has_class (gtk_widget_get_style_context (widget), NEMO_TAB_LIST_SUB_TABS)
			? widget : NULL;
	}
	if (!GTK_IS_CONTAINER (widget)) {
		return NULL;
	}

	children = gtk_container_get_children (GTK_CONTAINER (widget));
	for (l = children; l != NULL && found == NULL; l = l->next) {
		found = find_marked_stack (l->data);
	}
	g_list_free (children);

	return found;
}

static GtkWidget *
sub_tabs (GtkWidget *page)
{
	return GTK_IS_STACK (page) ? page : find_marked_stack (page);
}

static gboolean
has_visible_pages (GtkWidget *page)
{
	GList *children, *l;
	gboolean any = FALSE;

	page = sub_tabs (page);
	if (page == NULL) {
		return FALSE;
	}

	children = gtk_container_get_children (GTK_CONTAINER (page));
	for (l = children; l != NULL && !any; l = l->next) {
		any = gtk_widget_get_visible (l->data);
	}
	g_list_free (children);

	return any;
}

static void
refresh (NemoTabList *list)
{
	GList *rows, *l;

	if (!alive (list)) {
		return;
	}

	rows = gtk_container_get_children (GTK_CONTAINER (list->box));
	for (l = rows; l != NULL; l = l->next) {
		GtkWidget *row = l->data;
		GtkWidget *page = row_page (row);
		GtkWidget *parent = gtk_widget_get_parent (page);
		gboolean shown, showing;
		char *title = NULL;

		page_state (list, page, &shown, &showing);
		gtk_widget_set_visible (row, shown);
		if (showing) {
			gtk_widget_set_state_flags (row, GTK_STATE_FLAG_SELECTED, FALSE);
		} else {
			gtk_widget_unset_state_flags (row, GTK_STATE_FLAG_SELECTED);
		}

		if (GTK_IS_STACK (parent)) {
			gtk_container_child_get (GTK_CONTAINER (parent), page, "title", &title, NULL);
		}
		gtk_label_set_text (GTK_LABEL (gtk_bin_get_child (GTK_BIN (row))), title != NULL ? title : "");
		g_free (title);
	}
	g_list_free (rows);
}

static void rebuild (NemoTabList *list);

static void
changed_cb (G_GNUC_UNUSED GObject *object, G_GNUC_UNUSED gpointer arg, NemoTabList *list)
{
	refresh (list);
}

static void
pages_changed_cb (G_GNUC_UNUSED GtkContainer *stack, G_GNUC_UNUSED GtkWidget *page, NemoTabList *list)
{
	rebuild (list);
}

/* Signals go away with whichever end dies first. The mark on the object keeps
   a rebuild from connecting twice. */
static void
watch (NemoTabList *list, GtkWidget *widget)
{
	if (g_object_get_data (G_OBJECT (widget), WATCHED_KEY) == list) {
		return;
	}
	g_object_set_data (G_OBJECT (widget), WATCHED_KEY, list);

	g_signal_connect_object (widget, "notify::visible", G_CALLBACK (changed_cb), list, 0);
	g_signal_connect_object (widget, "child-notify::title", G_CALLBACK (changed_cb), list, 0);
	if (GTK_IS_STACK (widget)) {
		g_signal_connect_object (widget, "notify::visible-child", G_CALLBACK (changed_cb), list, 0);
		g_signal_connect_object (widget, "add", G_CALLBACK (pages_changed_cb), list, G_CONNECT_AFTER);
		g_signal_connect_object (widget, "remove", G_CALLBACK (pages_changed_cb), list, G_CONNECT_AFTER);
	}
}

static void
add_rows (NemoTabList *list, GtkStack *stack, int depth)
{
	GList *pages, *l;

	watch (list, GTK_WIDGET (stack));

	/* A stack lists its pages in their "position" order. */
	pages = gtk_container_get_children (GTK_CONTAINER (stack));
	for (l = pages; l != NULL; l = l->next) {
		GtkWidget *page = l->data;
		GtkWidget *row = gtk_list_box_row_new ();
		GtkWidget *label = gtk_label_new (NULL);

		gtk_label_set_xalign (GTK_LABEL (label), 0.0);
		gtk_widget_set_margin_start (label, depth * SUB_TAB_INDENT);
		gtk_widget_show (label);
		gtk_container_add (GTK_CONTAINER (row), label);
		g_object_set_data (G_OBJECT (row), PAGE_KEY, page);
		gtk_container_add (GTK_CONTAINER (list->box), row);

		watch (list, page);
		if (sub_tabs (page) != NULL) {
			add_rows (list, GTK_STACK (sub_tabs (page)), depth + 1);
		}
	}
	g_list_free (pages);
}

static void
rebuild (NemoTabList *list)
{
	GList *rows, *l;

	if (list->box == NULL || gtk_widget_in_destruction (GTK_WIDGET (list))) {
		return;
	}

	rows = gtk_container_get_children (GTK_CONTAINER (list->box));
	for (l = rows; l != NULL; l = l->next) {
		gtk_widget_destroy (l->data);
	}
	g_list_free (rows);

	if (list->stack != NULL) {
		add_rows (list, list->stack, 0);
	}
	refresh (list);
}

void
nemo_tab_list_show_page (NemoTabList *list, GtkWidget *page)
{
	GtkWidget *w, *parent;

	g_return_if_fail (NEMO_IS_TAB_LIST (list));

	if (!alive (list)) {
		return;
	}

	for (w = page; w != NULL && w != GTK_WIDGET (list->stack); w = parent) {
		parent = gtk_widget_get_parent (w);
		if (GTK_IS_STACK (parent)) {
			gtk_stack_set_visible_child (GTK_STACK (parent), w);
		}
	}
}

static void
row_activated_cb (G_GNUC_UNUSED GtkListBox *box, GtkListBoxRow *row, NemoTabList *list)
{
	nemo_tab_list_show_page (list, row_page (GTK_WIDGET (row)));
}

gboolean
nemo_tab_list_step (NemoTabList *list, int step)
{
	GList *rows, *l;
	GPtrArray *leaves;
	int here = -1;
	int count;

	g_return_val_if_fail (NEMO_IS_TAB_LIST (list), FALSE);

	if (!alive (list)) {
		return FALSE;
	}

	leaves = g_ptr_array_new ();
	rows = gtk_container_get_children (GTK_CONTAINER (list->box));
	for (l = rows; l != NULL; l = l->next) {
		GtkWidget *page = row_page (l->data);
		gboolean shown, showing;

		if (has_visible_pages (page)) {
			continue;
		}
		page_state (list, page, &shown, &showing);
		if (!shown) {
			continue;
		}
		if (showing) {
			here = (int) leaves->len;
		}
		g_ptr_array_add (leaves, page);
	}
	g_list_free (rows);

	count = (int) leaves->len;
	if (count < 2 || here < 0) {
		g_ptr_array_free (leaves, TRUE);
		return FALSE;
	}

	nemo_tab_list_show_page (list, leaves->pdata[((here + step) % count + count) % count]);
	g_ptr_array_free (leaves, TRUE);

	return TRUE;
}

/* Returns: (transfer none) */
GtkWidget *
nemo_tab_list_get_label (NemoTabList *list, GtkWidget *widget)
{
	GList *rows, *l;
	GtkWidget *best = NULL, *best_page = NULL;

	g_return_val_if_fail (NEMO_IS_TAB_LIST (list), NULL);

	if (!alive (list) || widget == NULL) {
		return NULL;
	}

	rows = gtk_container_get_children (GTK_CONTAINER (list->box));
	for (l = rows; l != NULL; l = l->next) {
		GtkWidget *page = row_page (l->data);

		if (page != widget && !gtk_widget_is_ancestor (widget, page)) {
			continue;
		}
		if (best_page == NULL || gtk_widget_is_ancestor (page, best_page)) {
			best_page = page;
			best = gtk_bin_get_child (GTK_BIN (l->data));
		}
	}
	g_list_free (rows);

	return best;
}

void
nemo_tab_list_set_stack (NemoTabList *list, GtkStack *stack)
{
	g_return_if_fail (NEMO_IS_TAB_LIST (list));
	g_return_if_fail (stack == NULL || GTK_IS_STACK (stack));

	if (list->stack == stack) {
		return;
	}

	if (list->stack != NULL) {
		g_object_remove_weak_pointer (G_OBJECT (list->stack), (gpointer *) &list->stack);
	}
	list->stack = stack;
	if (stack != NULL) {
		g_object_add_weak_pointer (G_OBJECT (stack), (gpointer *) &list->stack);
	}

	rebuild (list);
	g_object_notify_by_pspec (G_OBJECT (list), props[PROP_STACK]);
}

/* Returns: (transfer none) */
GtkStack *
nemo_tab_list_get_stack (NemoTabList *list)
{
	g_return_val_if_fail (NEMO_IS_TAB_LIST (list), NULL);

	return list->stack;
}

/* Returns: (transfer floating) */
GtkWidget *
nemo_tab_list_new (void)
{
	return g_object_new (NEMO_TYPE_TAB_LIST, NULL);
}

static void
nemo_tab_list_set_property (GObject *object, guint prop_id, const GValue *value, GParamSpec *pspec)
{
	switch (prop_id) {
	case PROP_STACK:
		nemo_tab_list_set_stack (NEMO_TAB_LIST (object), g_value_get_object (value));
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
nemo_tab_list_get_property (GObject *object, guint prop_id, GValue *value, GParamSpec *pspec)
{
	switch (prop_id) {
	case PROP_STACK:
		g_value_set_object (value, NEMO_TAB_LIST (object)->stack);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
nemo_tab_list_destroy (GtkWidget *widget)
{
	NemoTabList *list = NEMO_TAB_LIST (widget);

	if (list->stack != NULL) {
		g_object_remove_weak_pointer (G_OBJECT (list->stack), (gpointer *) &list->stack);
		list->stack = NULL;
	}
	list->box = NULL;

	GTK_WIDGET_CLASS (nemo_tab_list_parent_class)->destroy (widget);
}

/* A builder file sets the stack before it adds any style class, so a marked
   stack is not marked yet when the rows are first made. */
static void
nemo_tab_list_parser_finished (GtkBuildable *buildable, GtkBuilder *builder)
{
	if (parent_buildable_iface->parser_finished != NULL) {
		parent_buildable_iface->parser_finished (buildable, builder);
	}

	rebuild (NEMO_TAB_LIST (buildable));
}

static void
nemo_tab_list_buildable_init (GtkBuildableIface *iface)
{
	parent_buildable_iface = g_type_interface_peek_parent (iface);
	iface->parser_finished = nemo_tab_list_parser_finished;
}

static void
nemo_tab_list_init (NemoTabList *list)
{
	GtkWidget *scrolled;

	gtk_style_context_add_class (gtk_widget_get_style_context (GTK_WIDGET (list)), GTK_STYLE_CLASS_SIDEBAR);

	scrolled = gtk_scrolled_window_new (NULL, NULL);
	gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW (scrolled), GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
	gtk_container_add (GTK_CONTAINER (list), scrolled);

	/* Selection is drawn by hand, since more than one row shows it. */
	list->box = gtk_list_box_new ();
	gtk_list_box_set_selection_mode (GTK_LIST_BOX (list->box), GTK_SELECTION_NONE);
	gtk_container_add (GTK_CONTAINER (scrolled), list->box);
	g_signal_connect (list->box, "row-activated", G_CALLBACK (row_activated_cb), list);

	/* Tabs are for the pointer. The scrolled window would take the focus in
	   place of the list. Ctrl+Page Up and Down are the window's to bind, through
	   nemo_tab_list_step. */
	eel_gtk_widget_refuse_focus (GTK_WIDGET (list));
	eel_gtk_widget_refuse_focus (scrolled);
	eel_gtk_list_box_keep_focus_off_rows (GTK_LIST_BOX (list->box));

	gtk_widget_show_all (scrolled);
}

static void
nemo_tab_list_class_init (NemoTabListClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

	object_class->set_property = nemo_tab_list_set_property;
	object_class->get_property = nemo_tab_list_get_property;
	widget_class->destroy = nemo_tab_list_destroy;

	props[PROP_STACK] = g_param_spec_object ("stack", "Stack", "The stack whose pages are listed",
						 GTK_TYPE_STACK,
						 G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);
	g_object_class_install_properties (object_class, N_PROPS, props);

	/* So the theme draws it as it would GtkStackSidebar. */
	gtk_widget_class_set_css_name (widget_class, "stacksidebar");
}
